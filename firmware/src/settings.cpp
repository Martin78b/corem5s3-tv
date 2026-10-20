#include "settings.h"

#include <Preferences.h>

namespace SettingsStore {

static const char *NAMESPACE = "coretv";

Settings defaults() {
  Settings s;
  s.volumePct = 90;
  s.brightnessPct = 100;
  s.rotation = 0;
  s.idleOffHours = 2;
  s.wifiEnabled = false;
  return s;
}

Settings load() {
  Settings s = defaults();
  Preferences prefs;
  if (!prefs.begin(NAMESPACE, true)) return s;  // Nothing saved yet

  uint8_t schema = prefs.getUChar("schema", 0);
  if (schema == SCHEMA_VERSION) {
    s.volumePct = prefs.getUChar("vol", s.volumePct);
    s.brightnessPct = prefs.getUChar("bri", s.brightnessPct);
    s.rotation = prefs.getUChar("rot", s.rotation);
    s.idleOffHours = prefs.getUChar("idle", s.idleOffHours);
    s.wifiEnabled = prefs.getBool("wifi", s.wifiEnabled);
  } else if (schema != 0) {
    log_w("Settings schema %u unknown, using defaults", schema);
  }
  prefs.end();

  // Never trust stored values blindly
  if (s.volumePct > 100) s.volumePct = 100;
  if (s.brightnessPct < 10 || s.brightnessPct > 100) s.brightnessPct = 100;
  return s;
}

void save(const Settings &s) {
  Preferences prefs;
  if (!prefs.begin(NAMESPACE, false)) {
    log_e("Settings: NVS open failed");
    return;
  }
  prefs.putUChar("schema", SCHEMA_VERSION);
  prefs.putUChar("vol", s.volumePct);
  prefs.putUChar("bri", s.brightnessPct);
  prefs.putUChar("rot", s.rotation);
  prefs.putUChar("idle", s.idleOffHours);
  prefs.putBool("wifi", s.wifiEnabled);
  prefs.end();
  log_i("Settings saved (vol=%u bri=%u)", s.volumePct, s.brightnessPct);
}

}  // namespace SettingsStore
