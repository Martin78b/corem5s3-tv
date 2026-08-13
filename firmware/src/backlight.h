#pragma once

#include <Arduino.h>
#include <driver/ledc.h>
#include <esp_log.h>

// Backlight PWM abstraction for ESP32 LEDC
// Provides percentage-based brightness control for displays using a GPIO backlight pin.

namespace Backlight {

void begin(int gpio, uint8_t initialPct = 100);
void setBrightness(uint8_t pct); // 0-100%
uint8_t getBrightness();         // 0-100%

} // namespace Backlight
