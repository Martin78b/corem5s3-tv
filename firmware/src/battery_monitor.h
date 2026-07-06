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
    int raw = analogRead(BAT_ADC_PIN);
    if (raw < 0) {
      _state.valid = false;
      return _state;
    }
    // Voltage at pin: raw/4095 * 3.3V
    // Actual battery: pin_voltage * (200k+100k)/100k = pin_voltage * 3
    uint32_t v_pin_mv = (raw * 3300) / 4095;
    _state.voltage_mv = v_pin_mv * 3;

    // LiPo percentage (3.0V=0%, 4.2V=100%)
    if (_state.voltage_mv >= 4200) {
      _state.percentage = 100;
    } else if (_state.voltage_mv <= 3000) {
      _state.percentage = 0;
    } else {
      _state.percentage = (_state.voltage_mv - 3000) / 12;
    }

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
  static inline State _state = {};
  static inline uint32_t _lastReadMs = 0;
};
