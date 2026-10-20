#pragma once

#include <Arduino.h>
#include "config.h"

#ifdef M5STACK
  #include <M5Unified.h>
  #define Display           M5.Display
  #define displayWritePixels(buf, len) Display.writePixels(buf, len)
  // Drawing target for OSDs: M5.Display and M5Canvas both derive from it
  using Gfx = lgfx::LovyanGFX;
#else
  #include <TFT_eSPI.h>
  extern TFT_eSPI Display;
  inline void displayWritePixels(uint16_t* buf, uint32_t len) {
    Display.pushPixels(buf, len);
  }
  // Drawing target for OSDs: the display itself or a TFT_eSprite frame
  using Gfx = TFT_eSPI;
#endif
