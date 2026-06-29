#include "crt_effects.h"

uint8_t CRTEffects::_noiseSeed = 0;

void CRTEffects::applyScanlines(uint16_t* framebuffer) {
  int stride = DISPLAY_WIDTH;
  uint32_t factor = (64 - (CRT_SCANLINE_ALPHA >> 2));

  for (int y = 1; y < DISPLAY_HEIGHT; y += 2) {
    uint32_t* p = (uint32_t*)&framebuffer[y * stride];
    for (int x = 0; x < DISPLAY_WIDTH; x += 2) {
      uint32_t c = *p;
      uint32_t r = (c >> 11) & 0x1F001F;
      uint32_t g = (c >> 5) & 0x3F003F;
      uint32_t b = c & 0x1F001F;
      r = (r * factor) >> 6;
      g = (g * factor) >> 6;
      b = (b * factor) >> 6;
      *p++ = (r << 11) | (g << 5) | b;
    }
  }
}

void CRTEffects::applyNoise(uint16_t* framebuffer) {
  int total = DISPLAY_WIDTH * DISPLAY_HEIGHT;
  for (int i = 0; i < CRT_NOISE_PIXELS; i++) {
    int idx = esp_random() % total;
    framebuffer[idx] ^= 0x0841;
  }
}

void CRTEffects::generateStatic(uint16_t* framebuffer) {
  int total = DISPLAY_WIDTH * DISPLAY_HEIGHT;
  uint32_t* p = (uint32_t*)framebuffer;
  for (int i = 0; i < total / 2; i++) {
    uint8_t g = esp_random() & 0xFF;
    uint16_t c = ((g >> 3) << 11) | ((g >> 2) << 5) | (g >> 3);
    *p++ = c | (c << 16);
  }
  if (total & 1) {
    uint8_t g = esp_random() & 0xFF;
    framebuffer[total - 1] = ((g >> 3) << 11) | ((g >> 2) << 5) | (g >> 3);
  }
}

void CRTEffects::applyCurvature(uint16_t* framebuffer) {
#if CRT_CURVATURE > 0
  // Curvature disabled for performance
#endif
}

uint16_t CRTEffects::dimColor(uint16_t color, uint8_t amount) {
  uint8_t r = (color >> 11) & 0x1F;
  uint8_t g = (color >> 5) & 0x3F;
  uint8_t b = color & 0x1F;
  r = (r * (64 - (amount >> 2))) >> 6;
  g = (g * (64 - (amount >> 2))) >> 6;
  b = (b * (64 - (amount >> 2))) >> 6;
  return (r << 11) | (g << 5) | b;
}

uint16_t CRTEffects::randomColor() {
  return Display.color565(esp_random() & 0xFF, esp_random() & 0xFF, esp_random() & 0xFF);
}
