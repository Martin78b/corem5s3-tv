#include "auto_off.h"

#ifdef WAVESHARE_154

namespace AutoOff {

const char *const SLEEP_LABELS[SLEEP_OPTION_COUNT] = {
    "NO", "15m", "30m", "60m", "2h", "4h", "FIN EP"};
static const uint16_t SLEEP_MINUTES[SLEEP_OPTION_COUNT] = {
    0, 15, 30, 60, 120, 240, 0};

const char *const IDLE_LABELS[IDLE_OPTION_COUNT] = {"NO", "1h", "2h", "4h"};
static const uint8_t IDLE_HOURS[IDLE_OPTION_COUNT] = {0, 1, 2, 4};

static uint8_t s_sleepOption = SLEEP_OFF;
static uint32_t s_sleepStart = 0;
static uint8_t s_idleOption = IDLE_DEFAULT;
static uint32_t s_lastActivity = 0;

// Elapsed-time math (now - start) stays right across the millis() overflow.
static uint32_t remaining(uint32_t start, uint32_t duration, uint32_t now) {
  uint32_t elapsed = now - start;
  return elapsed >= duration ? 0 : duration - elapsed;
}

static uint32_t sleepDurationMs() {
  return (uint32_t)SLEEP_MINUTES[s_sleepOption] * AUTO_OFF_MINUTE_MS;
}

static uint32_t idleDurationMs() {
  return (uint32_t)IDLE_HOURS[s_idleOption] * 60 * AUTO_OFF_MINUTE_MS;
}

void setSleep(uint8_t option, uint32_t now) {
  s_sleepOption = option < SLEEP_OPTION_COUNT ? option : SLEEP_OFF;
  s_sleepStart = now;
}

uint8_t sleepOption() { return s_sleepOption; }

bool offAtEpisodeEnd() { return s_sleepOption == SLEEP_END_OF_EPISODE; }

uint32_t sleepRemainingMs(uint32_t now) {
  uint32_t d = sleepDurationMs();
  return d ? remaining(s_sleepStart, d, now) : 0;
}

void setIdle(uint8_t option) {
  s_idleOption = option < IDLE_OPTION_COUNT ? option : IDLE_DEFAULT;
}

void activity(uint32_t now) { s_lastActivity = now; }

Status tick(uint32_t now, bool charging) {
  if (charging) s_lastActivity = now;

  Status st = {Source::NONE, 0, false};
  uint32_t sleepD = sleepDurationMs();
  uint32_t idleD = idleDurationMs();
  uint32_t best = UINT32_MAX;
  if (sleepD) {
    best = remaining(s_sleepStart, sleepD, now);
    st.warning = Source::SLEEP;
  }
  if (idleD) {
    uint32_t r = remaining(s_lastActivity, idleD, now);
    if (r < best) {
      best = r;
      st.warning = Source::IDLE;
    }
  }
  if (st.warning == Source::NONE) return st;

  st.expired = (best == 0);
  if (best > AUTO_OFF_WARN_MS) {
    st.warning = Source::NONE;
  } else {
    st.remainingMs = best;
  }
  return st;
}

void cancelWarning(uint32_t now) {
  uint32_t sleepD = sleepDurationMs();
  if (sleepD && remaining(s_sleepStart, sleepD, now) <= AUTO_OFF_WARN_MS) {
    s_sleepOption = SLEEP_OFF;
  }
  s_lastActivity = now;
}

}  // namespace AutoOff

#endif  // WAVESHARE_154
