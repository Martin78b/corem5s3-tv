#pragma once

#include <Arduino.h>
#include "config.h"
#include "display_hal.h"

class CRTEffects {
public:
  static void applyScanlines(uint16_t* framebuffer);
  static void applyNoise(uint16_t* framebuffer);
  static void generateStatic(uint16_t* framebuffer);
  static void applyCurvature(uint16_t* framebuffer);

private:
  static uint16_t dimColor(uint16_t color, uint8_t amount);
  static uint16_t randomColor();
  static uint8_t _noiseSeed;
};
