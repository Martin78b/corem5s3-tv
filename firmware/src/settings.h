#pragma once

#include <Arduino.h>

// User settings persisted in NVS (Preferences namespace "coretv").
struct Settings {
  uint8_t volumePct;      // 0-100
  uint8_t brightnessPct;  // 10-100
  uint8_t idleOffOption;  // AutoOff::IDLE_LABELS index: off / 1 h / 2 h / 4 h
  // Reserved for upcoming features (stored, not used yet):
  uint8_t rotation;       // F3: 0 = auto, 1-4 = 0/90/180/270 degrees
  bool wifiEnabled;       // F4

  bool operator==(const Settings &o) const {
    return volumePct == o.volumePct && brightnessPct == o.brightnessPct &&
           rotation == o.rotation && idleOffOption == o.idleOffOption &&
           wifiEnabled == o.wifiEnabled;
  }
  bool operator!=(const Settings &o) const { return !(*this == o); }
};

namespace SettingsStore {

// Bump when the stored layout changes, and migrate old values in load().
constexpr uint8_t SCHEMA_VERSION = 1;

Settings defaults();
Settings load();   // Defaults for any missing key, or if NVS is unavailable
void save(const Settings &s);

}  // namespace SettingsStore
