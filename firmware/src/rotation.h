#pragma once

#include <Arduino.h>
#include "config.h"

// Screen rotation (HANDOFF F3): manual fixed angles (0 / 90 / 180 / 270 deg)
// or automatic orientation via the QMI8658 accelerometer on the shared I2C bus.
// The IMU is polled from touchTask (core 0); Display.setRotation() is applied
// from loop() (core 1) so SPI access stays on a single core.
namespace Rotation {

constexpr uint8_t OPTION_COUNT = 5;
constexpr uint8_t ROT_0 = 0;
constexpr uint8_t ROT_90 = 1;
constexpr uint8_t ROT_180 = 2;
constexpr uint8_t ROT_270 = 3;
constexpr uint8_t ROT_AUTO = 4;
constexpr uint8_t ROT_DEFAULT = ROT_0;  // Fixed 0 deg by default; Auto is optional
extern const char *const LABELS[OPTION_COUNT];

#ifdef WAVESHARE_154

// Probes QMI8658 on I2C_NUM_1 (0x6B / 0x6A) and configures accelerometer-only
// low-ODR mode. Safe to call after I2C_NUM_1 is initialized.
bool init();

// True if QMI8658 responded with WHO_AM_I == 0x05 during init().
bool hasImu();

// Sets the menu option (ROT_0..ROT_270 or ROT_AUTO). For fixed options,
// updates the target rotation immediately.
void setMode(uint8_t option);
uint8_t mode();

// Call from touchTask (core 0). Polls QMI8658 at ~10 Hz only while mode() ==
// ROT_AUTO and updates the target rotation after IMU_STABLE_MS of stability.
void pollImu(uint32_t nowMs);

// Currently applied TFT_eSPI rotation (0..3). Safe to read from any core.
uint8_t current();

// Desired TFT_eSPI rotation (0..3).
uint8_t target();

// Call from loop() (core 1). If target() differs from current(), updates
// current(), writes the new value to `outRotation`, and returns true so the
// caller can call Display.setRotation(outRotation).
bool consumeChange(uint8_t &outRotation);

// Transforms raw CST816T panel coordinates (0..DISPLAY_WIDTH-1,
// 0..DISPLAY_HEIGHT-1) into the active rotation's logical coordinates.
void mapTouch(uint16_t rawX, uint16_t rawY, int16_t &outX, int16_t &outY);

#endif  // WAVESHARE_154

}  // namespace Rotation

