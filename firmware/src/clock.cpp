#include "clock.h"

namespace Clock {

const char *const TZ_LABELS[TZ_OPTION_COUNT] = {
    "UTC-12", "UTC-11", "UTC-10", "UTC-9", "UTC-8", "UTC-7", "UTC-6",
    "UTC-5",  "UTC-4",  "UTC-3",  "UTC-2", "UTC-1", "UTC",   "UTC+1",
    "UTC+2",  "UTC+3",  "UTC+4",  "UTC+5", "UTC+6", "UTC+7", "UTC+8",
    "UTC+9",  "UTC+10", "UTC+11", "UTC+12", "UTC+13", "UTC+14"};

const char *const OFF_AT_LABELS[OFF_AT_OPTION_COUNT] = {
    "NO", "20:00", "21:00", "22:00", "23:00", "00:00", "01:00", "02:00"};
static const uint8_t OFF_AT_HOURS[OFF_AT_OPTION_COUNT] = {0,  20, 21, 22,
                                                          23, 0,  1,  2};

}  // namespace Clock

#ifdef WAVESHARE_154

#include <WiFi.h>
#include <driver/i2c.h>
#include <esp_sntp.h>
#include <sys/time.h>

namespace Clock {

static const uint8_t RTC_ADDR = 0x51;  // PCF85063
static const uint8_t REG_CONTROL_1 = 0x00;
static const uint8_t REG_SECONDS = 0x04;  // Then minutes .. years
static const uint8_t CONTROL_1_STOP = 0x20;
static const uint8_t CONTROL_1_12H = 0x02;
static const uint8_t SECONDS_OS = 0x80;  // Oscillator stopped: time invalid

// 2024-01-01: anything earlier means the system clock was never set
static const time_t MIN_VALID_TIME = 1704067200;

static bool s_hasRtc = false;
static bool s_valid = false;
static char s_tz[16] = "UTC0";

static uint8_t toBcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static int fromBcd(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }

// UTC fields -> epoch (newlib has no timegm). Days-from-civil algorithm.
static time_t utcFromFields(int y, int mon, int d, int h, int mi, int s) {
  y -= mon <= 2;
  const int era = y / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (mon + (mon > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const long days = (long)era * 146097 + (long)doe - 719468;
  return (time_t)days * 86400 + h * 3600 + mi * 60 + s;
}

static bool rtcRead(uint8_t reg, uint8_t *buf, size_t len) {
  return i2c_master_write_read_device(I2C_NUM_1, RTC_ADDR, &reg, 1, buf, len,
                                      pdMS_TO_TICKS(50)) == ESP_OK;
}

static bool rtcWrite(const uint8_t *buf, size_t len) {
  return i2c_master_write_to_device(I2C_NUM_1, RTC_ADDR, buf, len,
                                    pdMS_TO_TICKS(50)) == ESP_OK;
}

static bool rtcLoad() {
  uint8_t r[7];
  if (!rtcRead(REG_SECONDS, r, sizeof(r))) return false;
  if (r[0] & SECONDS_OS) {
    log_w("RTC oscillator was stopped: time not valid");
    return false;
  }
  int sec = fromBcd(r[0] & 0x7F);
  int min = fromBcd(r[1] & 0x7F);
  int hour = fromBcd(r[2] & 0x3F);
  int day = fromBcd(r[3] & 0x3F);
  int mon = fromBcd(r[5] & 0x1F);
  int year = 2000 + fromBcd(r[6]);
  if (sec > 59 || min > 59 || hour > 23 || day < 1 || day > 31 || mon < 1 ||
      mon > 12) {
    return false;
  }
  time_t t = utcFromFields(year, mon, day, hour, min, sec);
  if (t < MIN_VALID_TIME) return false;
  timeval tv = {t, 0};
  settimeofday(&tv, nullptr);
  log_i("Clock set from RTC: %04d-%02d-%02d %02d:%02d:%02d UTC", year, mon, day,
        hour, min, sec);
  return true;
}

static void rtcStore(time_t t) {
  if (!s_hasRtc) return;
  struct tm u;
  gmtime_r(&t, &u);

  // Running, 24 h mode; keep the other bits (e.g. the crystal load setting)
  uint8_t ctrl = 0;
  if (rtcRead(REG_CONTROL_1, &ctrl, 1) &&
      (ctrl & (CONTROL_1_STOP | CONTROL_1_12H))) {
    uint8_t w[2] = {REG_CONTROL_1,
                    (uint8_t)(ctrl & ~(CONTROL_1_STOP | CONTROL_1_12H))};
    rtcWrite(w, sizeof(w));
  }

  uint8_t w[8] = {REG_SECONDS,
                  toBcd(u.tm_sec),  // OS bit cleared
                  toBcd(u.tm_min),
                  toBcd(u.tm_hour),
                  toBcd(u.tm_mday),
                  (uint8_t)u.tm_wday,
                  toBcd(u.tm_mon + 1),
                  toBcd(u.tm_year % 100)};
  if (!rtcWrite(w, sizeof(w))) log_w("RTC write failed");
}

void setTimezone(uint8_t tzOption) {
  if (tzOption >= TZ_OPTION_COUNT) tzOption = TZ_DEFAULT;
  int offset = TZ_FIRST_OFFSET + tzOption;
  // POSIX TZ signs are inverted: "<-03>3" is UTC-3
  snprintf(s_tz, sizeof(s_tz), "<%+03d>%d", offset, -offset);
  setenv("TZ", s_tz, 1);
  tzset();
}

void init(uint8_t tzOption) {
  setTimezone(tzOption);
  uint8_t probe;
  s_hasRtc = rtcRead(REG_CONTROL_1, &probe, 1);
  if (!s_hasRtc) {
    log_w("RTC PCF85063 not found");
    return;
  }
  s_valid = rtcLoad();
}

bool hasRtc() { return s_hasRtc; }

bool valid() { return s_valid && time(nullptr) >= MIN_VALID_TIME; }

bool syncNtp(uint32_t timeoutMs) {
  if (WiFi.status() != WL_CONNECTED) return false;
  sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
  // Also sets TZ (to the same string), so local time stays right
  configTzTime(s_tz, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  uint32_t start = millis();
  bool ok = false;
  while (millis() - start < timeoutMs) {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED &&
        time(nullptr) >= MIN_VALID_TIME) {
      ok = true;
      break;
    }
    delay(50);
  }
  sntp_stop();  // The radio goes off soon; no periodic resync
  if (!ok) {
    log_w("NTP sync timed out");
    return false;
  }
  s_valid = true;
  rtcStore(time(nullptr));
  char buf[8];
  format(buf, sizeof(buf));
  log_i("Clock set from NTP: %s (%s)", buf, s_tz);
  return true;
}

bool localTime(struct tm &out) {
  if (!valid()) return false;
  time_t now = time(nullptr);
  localtime_r(&now, &out);
  return true;
}

void format(char *buf, size_t len) {
  struct tm t;
  if (localTime(t)) {
    snprintf(buf, len, "%02d:%02d", t.tm_hour, t.tm_min);
  } else {
    snprintf(buf, len, "--:--");
  }
}

uint32_t msUntilOffAt(uint8_t offAtOption) {
  if (offAtOption == OFF_AT_NONE || offAtOption >= OFF_AT_OPTION_COUNT) {
    return UINT32_MAX;
  }
  struct tm t;
  if (!localTime(t)) return UINT32_MAX;
  int32_t nowSec = t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
  int32_t target = OFF_AT_HOURS[offAtOption] * 3600;
  int32_t diff = (target - nowSec + 86400) % 86400;
  return (uint32_t)diff * 1000;
}

}  // namespace Clock

#endif  // WAVESHARE_154
