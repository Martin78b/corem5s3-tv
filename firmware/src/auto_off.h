#pragma once

#include <Arduino.h>
#include "config.h"

// Automatic power-off (HANDOFF F2): a TV-style sleep timer, not persisted,
// and power-off after a long time without input, persisted in Settings.
// Pure timekeeping: the caller draws the warning and powers off.
namespace AutoOff {

enum class Source : uint8_t { NONE, SLEEP, IDLE };

// Sleep timer options (menu index). The last one powers off when the
// current episode ends instead of after a fixed time.
constexpr uint8_t SLEEP_OPTION_COUNT = 7;
constexpr uint8_t SLEEP_OFF = 0;
constexpr uint8_t SLEEP_END_OF_EPISODE = SLEEP_OPTION_COUNT - 1;
extern const char *const SLEEP_LABELS[SLEEP_OPTION_COUNT];

// Inactivity options (menu index, stored in Settings::idleOffOption)
constexpr uint8_t IDLE_OPTION_COUNT = 4;
constexpr uint8_t IDLE_DEFAULT = 2;  // 2 h
extern const char *const IDLE_LABELS[IDLE_OPTION_COUNT];

#ifdef WAVESHARE_154

struct Status {
  Source warning;        // Which timer is in its last AUTO_OFF_WARN_MS
  uint32_t remainingMs;  // Until power-off, while warning != NONE
  bool expired;          // Power off now
};

void setSleep(uint8_t option, uint32_t now);  // Restarts the countdown
uint8_t sleepOption();
bool offAtEpisodeEnd();
// Time left on a fixed-time sleep timer; 0 if off or "end of episode"
uint32_t sleepRemainingMs(uint32_t now);

void setIdle(uint8_t option);
void activity(uint32_t now);  // Any touch or button press

// While `charging`, the inactivity countdown is held (the sleep timer is not).
Status tick(uint32_t now, bool charging);

// Input while the warning is shown: inactivity restarts, the sleep timer
// is switched off.
void cancelWarning(uint32_t now);

#endif  // WAVESHARE_154

}  // namespace AutoOff
