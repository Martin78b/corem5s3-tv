#pragma once

#include <Arduino.h>
#include "config.h"

// Wall clock (HANDOFF F5.1): PCF85063 RTC on the shared I2C bus, set from NTP
// whenever WiFi is connected. The RTC keeps UTC; the time zone is a fixed
// UTC offset chosen in the menu (no daylight saving).
namespace Clock {

// Time zone options (menu index, stored in Settings::tzOption)
constexpr uint8_t TZ_OPTION_COUNT = 27;  // UTC-12 .. UTC+14
constexpr int8_t TZ_FIRST_OFFSET = -12;
constexpr uint8_t TZ_DEFAULT = 9;        // UTC-3
extern const char *const TZ_LABELS[TZ_OPTION_COUNT];

// "Power off at" options (menu index, stored in Settings::offAtOption)
constexpr uint8_t OFF_AT_OPTION_COUNT = 8;
constexpr uint8_t OFF_AT_NONE = 0;
extern const char *const OFF_AT_LABELS[OFF_AT_OPTION_COUNT];

#ifdef WAVESHARE_154

// Applies the time zone, then loads the RTC time into the system clock
// (if the RTC is present and its oscillator never stopped).
void init(uint8_t tzOption);
void setTimezone(uint8_t tzOption);

bool hasRtc();
bool valid();  // System time is set (from the RTC or NTP)

// Needs a connected WiFi STA. Blocks up to `timeoutMs`; on success the RTC
// is written too.
bool syncNtp(uint32_t timeoutMs = NTP_SYNC_TIMEOUT_MS);

bool localTime(struct tm &out);
void format(char *buf, size_t len);  // "HH:MM", or "--:--" if not valid

// Milliseconds until the "power off at" hour, UINT32_MAX if the option is
// off or the clock is not valid.
uint32_t msUntilOffAt(uint8_t offAtOption);

#endif  // WAVESHARE_154

}  // namespace Clock
