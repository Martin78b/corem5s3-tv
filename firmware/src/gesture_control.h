#pragma once

#include <Arduino.h>
#include "config.h"
#include "display_hal.h"

// ── Gesture Debug Logging ──
// Set to 1 to enable detailed gesture logging.
// WARNING: Excessive logging may affect playback performance.
#ifndef GESTURE_DEBUG
#define GESTURE_DEBUG 0
#endif

#if GESTURE_DEBUG
#define GESTURE_LOG(fmt, ...) Serial.printf("GESTURE: " fmt "\n", ##__VA_ARGS__)
#else
#define GESTURE_LOG(fmt, ...) ((void)0)
#endif

// ── Gesture Configuration ──
struct GestureControlConfig {
  uint16_t min_swipe_distance_px;     // Minimum displacement to recognize swipe
  float    direction_ratio;           // Min ratio dominant/secondary axis (tan of angle)
  uint16_t max_tap_duration_ms;       // Maximum duration for a tap
  uint8_t  volume_step_per_10px;      // Volume % change per 10px displacement
  uint8_t  brightness_step_per_10px;  // Brightness % change per 10px displacement
  uint8_t  min_brightness_pct;        // Minimum brightness (%)
  uint8_t  max_brightness_pct;        // Maximum brightness (%)
  uint16_t osd_timeout_ms;            // OSD auto-hide timeout
  uint8_t  process_delta_px;          // Minimum delta to process an incremental change
};

// Default configuration — inspired by Waveshare/ESP-Brookesia gesture_data.hpp thresholds
static constexpr GestureControlConfig GESTURE_DEFAULT_CONFIG = {
  .min_swipe_distance_px    = 15,
  .direction_ratio          = 1.20f,  // tan(50°)
  .max_tap_duration_ms      = 300,
  .volume_step_per_10px     = 5,      // 5% por paso
  .brightness_step_per_10px = 5,      // 5% por paso
  .min_brightness_pct       = 10,
  .max_brightness_pct       = 100,
  .osd_timeout_ms           = 1500,
  .process_delta_px         = 8,      // 8px movimiento = 1 paso (5%)
};

// ── Gesture State ──
enum class GestureState : uint8_t {
  IDLE,         // No touch active
  TRACKING,     // Finger down, determining direction
  SWIPING_V,    // Vertical swipe in progress (volume)
  SWIPING_H,    // Horizontal swipe in progress (brightness)
};

// ── Gesture Result ──
// Returned by update() to inform caller whether a tap occurred
enum class GestureResult : uint8_t {
  NONE,         // Nothing actionable (swipe in progress, or no gesture)
  TAP,          // A tap was detected (finger lifted without significant movement)
};

// Forward declarations for audio/backlight control callbacks
class AudioPlayer;

class GestureController {
public:
  GestureController();

  // Initialize with pointers to the audio player and current volume/brightness.
  // Must be called before update().
  void begin(AudioPlayer* audio, uint8_t initialVolumePct, uint8_t initialBrightnessPct);

  // Call every touch read cycle.
  // fingerCount: 0 = no touch, >0 = touch active
  // x, y: touch coordinates (already mapped to display pixels)
  // Returns TAP if a tap was detected on finger release.
  GestureResult update(uint8_t fingerCount, int16_t x, int16_t y);

  // Direct trigger for hardware gesture events (step in +/- %)
  void triggerVolumeStep(int deltaPercent);
  void triggerBrightnessStep(int deltaPercent);

  // Render OSD overlay if active. Call after drawing each video frame.
  void renderOSD();

  // Current volume percentage (0-100)
  uint8_t getVolumePct() const { return _volumePct; }

  // Current brightness percentage (0-100)
  uint8_t getBrightnessPct() const { return _brightnessPct; }

  // Check if a swipe gesture is currently in progress
  bool isSwiping() const {
    return _state == GestureState::SWIPING_V || _state == GestureState::SWIPING_H;
  }

  // Configuration access
  const GestureControlConfig& config() const { return _cfg; }

  // Convert volume percentage (0-100) to hardware value (0-255)
  static uint8_t pctToHwVolume(uint8_t pct);

private:
  GestureControlConfig _cfg;
  GestureState _state;
  AudioPlayer* _audio;

  // Touch tracking
  int16_t  _startX, _startY;
  int16_t  _lastProcessedX, _lastProcessedY;
  uint32_t _startTimeMs;

  // Current values
  uint8_t _volumePct;       // 0-100
  uint8_t _brightnessPct;   // min..max

  // OSD state
  uint32_t _volumeOsdEnd;
  uint32_t _brightnessOsdEnd;

  // Internal helpers
  void applyVolumeDelta(int deltaPx);
  void applyBrightnessDelta(int deltaPx);
  void showVolumeOSD();
  void showBrightnessOSD();
};
