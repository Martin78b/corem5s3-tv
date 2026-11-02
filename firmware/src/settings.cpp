#include "settings.h"

#include "auto_off.h"
#include "clock.h"
#include "rotation.h"

#include <Preferences.h>

namespace SettingsStore {

static const char *NAMESPACE = "coretv";

Settings defaults() {
  Settings s;
  s.volumePct = 90;
  s.brightnessPct = 100;
  s.rotation = Rotation::ROT_DEFAULT;
  s.idleOffOption = AutoOff::IDLE_DEFAULT;
  s.wifiEnabled = false;
  s.tzOption = Clock::TZ_DEFAULT;
  s.offAtOption = Clock::OFF_AT_NONE;
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
    s.idleOffOption = prefs.getUChar("idle", s.idleOffOption);
    s.wifiEnabled = prefs.getBool("wifi", s.wifiEnabled);
    s.tzOption = prefs.getUChar("tz", s.tzOption);
    s.offAtOption = prefs.getUChar("offat", s.offAtOption);
  } else if (schema != 0) {
    log_w("Settings schema %u unknown, using defaults", schema);
  }
  prefs.end();

  // Never trust stored values blindly
  if (s.volumePct > 100) s.volumePct = 100;
  if (s.brightnessPct < 10 || s.brightnessPct > 100) s.brightnessPct = 100;
  if (s.rotation >= Rotation::OPTION_COUNT) {
    s.rotation = Rotation::ROT_DEFAULT;
  }
  if (s.idleOffOption >= AutoOff::IDLE_OPTION_COUNT) {
    s.idleOffOption = AutoOff::IDLE_DEFAULT;
  }
  if (s.tzOption >= Clock::TZ_OPTION_COUNT) s.tzOption = Clock::TZ_DEFAULT;
  if (s.offAtOption >= Clock::OFF_AT_OPTION_COUNT) {
    s.offAtOption = Clock::OFF_AT_NONE;
  }
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
  prefs.putUChar("idle", s.idleOffOption);
  prefs.putBool("wifi", s.wifiEnabled);
  prefs.putUChar("tz", s.tzOption);
  prefs.putUChar("offat", s.offAtOption);
  prefs.end();
  log_i("Settings saved (vol=%u bri=%u rot=%u)", s.volumePct, s.brightnessPct,
        s.rotation);
}

}  // namespace SettingsStore
