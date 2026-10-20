#pragma once

#include <Arduino.h>
#include "config.h"

#ifdef M5STACK
#include <M5Unified.h>
#endif

#ifdef WAVESHARE_154
#include <driver/adc.h>
#include <driver/gpio.h>
#endif

class BatteryMonitor {
public:
  struct State {
    uint8_t percentage;  // 0-100
    uint16_t voltage_mv; // millivolts
    bool charging;       // true = connected to charger
    bool valid;          // false = no battery hardware or read failed
  };

  static void begin() {
#ifdef WAVESHARE_154
    // Configure BAT_ADC (GPIO1) as analog input
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);  // 0-3.3V range
    // CHG_STAT (GPIO3) as input
    pinMode(BAT_CHG_STAT, INPUT_PULLUP);
#endif
    _lastReadMs = 0;
    _state = {};
  }

  // Call every ~5s. Returns cached state between reads.
  static State update() {
    uint32_t now = millis();
    if (now - _lastReadMs < 5000 && _lastReadMs != 0) {
      return _state;
    }
    _lastReadMs = now;

#ifdef M5STACK
    _state.voltage_mv = M5.Power.getBatteryVoltage();
    _state.percentage = M5.Power.getBatteryLevel();
    _state.charging   = M5.Power.isCharging();
    _state.valid      = true;
#elif defined(WAVESHARE_154)
    // eFuse-calibrated reading, averaged to smooth out ADC noise
    uint32_t v_pin_mv = 0;
    for (int i = 0; i < BAT_ADC_SAMPLES; i++) {
      v_pin_mv += analogReadMilliVolts(BAT_ADC_PIN);
    }
    v_pin_mv /= BAT_ADC_SAMPLES;
    // Actual battery: pin_voltage * (200k+100k)/100k = pin_voltage * 3
    _state.voltage_mv = v_pin_mv * 3;
    _state.percentage = lipoPercent(_state.voltage_mv);

    _state.charging = (digitalRead(BAT_CHG_STAT) == LOW);
    _state.valid    = true;
#else
    _state = {};
    _state.valid = false;
#endif
    return _state;
  }

  static const State& getState() { return _state; }

private:
#ifdef WAVESHARE_154
  static constexpr int BAT_ADC_SAMPLES = 16;

  // Single-cell LiPo discharge curve (approximate), linearly interpolated.
  static uint8_t lipoPercent(uint16_t mv) {
    static const struct { uint16_t mv; uint8_t pct; } curve[] = {
      {4200, 100}, {4050, 85}, {3900, 65}, {3750, 40},
      {3650, 20},  {3500, 5},  {3300, 0},
    };
    const int n = sizeof(curve) / sizeof(curve[0]);
    if (mv >= curve[0].mv) return 100;
    if (mv <= curve[n - 1].mv) return 0;
    for (int i = 1; i < n; i++) {
      if (mv >= curve[i].mv) {
        uint16_t dv = curve[i - 1].mv - curve[i].mv;
        uint8_t dp = curve[i - 1].pct - curve[i].pct;
        return curve[i].pct + (uint32_t)(mv - curve[i].mv) * dp / dv;
      }
    }
    return 0;
  }
#endif

  static inline State _state = {};
  static inline uint32_t _lastReadMs = 0;
};
