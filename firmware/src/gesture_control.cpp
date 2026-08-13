#include "gesture_control.h"

#ifdef WAVESHARE_154

#include "audio_player.h"
#include "backlight.h"
#include "es8311.h"
#include <cmath>

// ── OSD Layout Constants ──
// Volume OSD: centered horizontally, upper-third vertically
static const int OSD_W          = 100;
static const int OSD_H          = 28;
static const int OSD_VOL_X      = (DISPLAY_WIDTH - OSD_W) / 2;
static const int OSD_VOL_Y      = 40;
// Brightness OSD: same position (they don't overlap because only one swipe
// direction is active at a time)
static const int OSD_BRI_X      = (DISPLAY_WIDTH - OSD_W) / 2;
static const int OSD_BRI_Y      = 40;
// Progress bar inside OSD
static const int OSD_BAR_MARGIN = 4;
static const int OSD_BAR_H      = 6;
static const int OSD_TEXT_Y_OFF = 4;

// Max % change applied per touch-read poll. Keeps swipes smooth and
// proportional instead of a fast flick jumping the whole range in one read.
static const int MAX_POLL_DELTA_PCT = 10;

// ── Constructor ──
GestureController::GestureController()
    : _cfg(GESTURE_DEFAULT_CONFIG),
      _state(GestureState::IDLE),
      _audio(nullptr),
      _startX(0), _startY(0),
      _lastProcessedX(0), _lastProcessedY(0),
      _startTimeMs(0),
      _volumePct(50),
      _brightnessPct(100),
      _volumeOsdEnd(0),
      _brightnessOsdEnd(0) {}

void GestureController::begin(AudioPlayer* audio, uint8_t initialVolumePct, uint8_t initialBrightnessPct) {
  _audio = audio;
  _volumePct = initialVolumePct;
  _brightnessPct = initialBrightnessPct;
}

// ── Main Update ──
GestureResult GestureController::update(uint8_t fingerCount, int16_t x, int16_t y) {
  GestureResult result = GestureResult::NONE;

  if (fingerCount > 0) {
    // ─── Touch is active ───
    switch (_state) {
      case GestureState::IDLE: {
        // Finger just went down — start tracking
        _startX = x;
        _startY = y;
        _lastProcessedX = x;
        _lastProcessedY = y;
        _startTimeMs = millis();
        _state = GestureState::TRACKING;
        GESTURE_LOG("DOWN x=%d y=%d", x, y);
        break;
      }

      case GestureState::TRACKING: {
        // Finger is moving — determine direction
        int dx = x - _startX;
        int dy = y - _startY;
        int absDx = abs(dx);
        int absDy = abs(dy);

        GESTURE_LOG("MOVE dx=%d dy=%d", dx, dy);

        // Check vertical swipe first
        if (absDy >= _cfg.min_swipe_distance_px) {
          // Verify direction dominance: absDy/absDx > direction_ratio
          // Avoid division by zero: if absDx==0, it's purely vertical
          if (absDx == 0 || (float)absDy / (float)absDx >= _cfg.direction_ratio) {
            _state = GestureState::SWIPING_V;
            _lastProcessedY = _startY; // Reset so first delta is from start
            GESTURE_LOG("VERTICAL %s", dy < 0 ? "UP" : "DOWN");
            // Apply initial delta
            applyVolumeDelta(_lastProcessedY - y);
            _lastProcessedY = y;
            break;
          }
        }

        // Check horizontal swipe
        if (absDx >= _cfg.min_swipe_distance_px) {
          if (absDy == 0 || (float)absDx / (float)absDy >= _cfg.direction_ratio) {
            _state = GestureState::SWIPING_H;
            _lastProcessedX = _startX;
            GESTURE_LOG("HORIZONTAL %s", dx > 0 ? "RIGHT" : "LEFT");
            applyBrightnessDelta(x - _lastProcessedX);
            _lastProcessedX = x;
            break;
          }
        }
        break;
      }

      case GestureState::SWIPING_V: {
        // Continuous vertical swipe — volume control
        int deltaY = _lastProcessedY - y; // positive = finger moved up = volume up
        if (abs(deltaY) >= _cfg.process_delta_px) {
          applyVolumeDelta(deltaY);
          _lastProcessedY = y;
        }
        break;
      }

      case GestureState::SWIPING_H: {
        // Continuous horizontal swipe — brightness control
        int deltaX = x - _lastProcessedX; // positive = finger moved right = brightness up
        if (abs(deltaX) >= _cfg.process_delta_px) {
          applyBrightnessDelta(deltaX);
          _lastProcessedX = x;
        }
        break;
      }
    }
  } else {
    // ─── No touch (finger lifted) ───
    if (_state == GestureState::TRACKING) {
      // Was tracking but never entered a swipe — check if it's a tap
      uint32_t duration = millis() - _startTimeMs;
      int dx = abs(_lastProcessedX - _startX);
      int dy = abs(_lastProcessedY - _startY);
      // Also check against the last known position (which is the start since
      // we never got far enough to update lastProcessed in TRACKING)
      if (duration <= _cfg.max_tap_duration_ms &&
          dx < _cfg.min_swipe_distance_px &&
          dy < _cfg.min_swipe_distance_px) {
        GESTURE_LOG("TAP at x=%d y=%d duration=%ums", _startX, _startY, duration);
        result = GestureResult::TAP;
      }
    } else if (_state == GestureState::SWIPING_V || _state == GestureState::SWIPING_H) {
      GESTURE_LOG("UP (end swipe)");
    }
    _state = GestureState::IDLE;
  }

  return result;
}

// ── Helper: Dibuja un sol verde estilo TV vintage ──
static void drawSunIcon(int cx, int cy, uint16_t color) {
  // Círculo central
  Display.drawCircle(cx, cy, 3, color);
  Display.drawCircle(cx, cy, 2, color);

  // 8 Rayos solares (arriba, abajo, izq, der y 4 diagonales)
  Display.drawLine(cx, cy - 5, cx, cy - 8, color);     // Arriba
  Display.drawLine(cx, cy + 5, cx, cy + 8, color);     // Abajo
  Display.drawLine(cx - 5, cy, cx - 8, cy, color);     // Izquierda
  Display.drawLine(cx + 5, cy, cx + 8, cy, color);     // Derecha

  Display.drawLine(cx - 4, cy - 4, cx - 6, cy - 6, color); // Arriba-Izq
  Display.drawLine(cx + 4, cy - 4, cx + 6, cy - 6, color); // Arriba-Der
  Display.drawLine(cx - 4, cy + 4, cx - 6, cy + 6, color); // Abajo-Izq
  Display.drawLine(cx + 4, cy + 4, cx + 6, cy + 6, color); // Abajo-Der
}

void GestureController::triggerVolumeStep(int deltaPercent) {
  int newVol = (int)_volumePct + deltaPercent;
  if (newVol < 0)   newVol = 0;
  if (newVol > 100) newVol = 100;

  if ((uint8_t)newVol != _volumePct) {
    _volumePct = (uint8_t)newVol;
    uint8_t hwVol = pctToHwVolume(_volumePct);
    if (_audio) {
      _audio->setVolume(hwVol);
    }
    _volumeOsdEnd = millis() + _cfg.osd_timeout_ms;
    showVolumeOSD();
  }
}

void GestureController::triggerBrightnessStep(int deltaPercent) {
  int newBri = (int)_brightnessPct + deltaPercent;
  if (newBri < (int)_cfg.min_brightness_pct) newBri = _cfg.min_brightness_pct;
  if (newBri > (int)_cfg.max_brightness_pct) newBri = _cfg.max_brightness_pct;

  if ((uint8_t)newBri != _brightnessPct) {
    _brightnessPct = (uint8_t)newBri;
    Backlight::setBrightness(_brightnessPct);
    _brightnessOsdEnd = millis() + _cfg.osd_timeout_ms;
    showBrightnessOSD();
  }
}

// ── Volume Delta ──
void GestureController::applyVolumeDelta(int deltaPx) {
  // Pasos fijos de 5 unidades (5%) por cada paso de movimiento
  int steps = deltaPx / _cfg.process_delta_px;
  if (steps == 0) return;

  int deltaPercent = steps * 5; // 5% por paso
  // Limitar el cambio por lectura para que el control sea proporcional y suave
  if (deltaPercent > MAX_POLL_DELTA_PCT)  deltaPercent =  MAX_POLL_DELTA_PCT;
  if (deltaPercent < -MAX_POLL_DELTA_PCT) deltaPercent = -MAX_POLL_DELTA_PCT;

  int newVol = (int)_volumePct + deltaPercent;
  if (newVol < 0)   newVol = 0;
  if (newVol > 100) newVol = 100;

  if ((uint8_t)newVol != _volumePct) {
    uint8_t oldVol = _volumePct;
    _volumePct = (uint8_t)newVol;

    // Ajuste digital de volumen instantáneo (sin tocar I2C para evitar congelar el video)
    uint8_t hwVol = pctToHwVolume(_volumePct);
    if (_audio) {
      _audio->setVolume(hwVol);
    }

    GESTURE_LOG("volume %d%% -> %d%%", oldVol, _volumePct);

    _volumeOsdEnd = millis() + _cfg.osd_timeout_ms;
    // OSD is rendered by the main loop via renderOSD(), not here —
    // calling Display functions from the touch task causes SPI bus contention.
  }
}

// ── Brightness Delta ──
void GestureController::applyBrightnessDelta(int deltaPx) {
  int steps = deltaPx / _cfg.process_delta_px;
  if (steps == 0) return;

  int deltaPercent = steps * 5; // 5% por paso
  // Limitar el cambio por lectura para que el control sea proporcional y suave
  if (deltaPercent > MAX_POLL_DELTA_PCT)  deltaPercent =  MAX_POLL_DELTA_PCT;
  if (deltaPercent < -MAX_POLL_DELTA_PCT) deltaPercent = -MAX_POLL_DELTA_PCT;

  int newBri = (int)_brightnessPct + deltaPercent;
  if (newBri < (int)_cfg.min_brightness_pct) newBri = _cfg.min_brightness_pct;
  if (newBri > (int)_cfg.max_brightness_pct) newBri = _cfg.max_brightness_pct;

  if ((uint8_t)newBri != _brightnessPct) {
    uint8_t oldBri = _brightnessPct;
    _brightnessPct = (uint8_t)newBri;

    Backlight::setBrightness(_brightnessPct);

    GESTURE_LOG("brightness %d%% -> %d%%", oldBri, _brightnessPct);

    _brightnessOsdEnd = millis() + _cfg.osd_timeout_ms;
    // OSD is rendered by the main loop via renderOSD(), not here —
    // calling Display functions from the touch task causes SPI bus contention.
  }
}

// ── OSD Rendering (Estilo TV CRT Verde) ──
void GestureController::showVolumeOSD() {
  int w = 110, h = 32;
  int x = (DISPLAY_WIDTH - w) / 2;
  int y = 35;

  // Caja de TV estilo retro negro con borde verde
  Display.fillRect(x, y, w, h, TFT_BLACK);
  Display.drawRect(x, y, w, h, TFT_GREEN);
  Display.drawRect(x + 1, y + 1, w - 2, h - 2, TFT_GREEN);

  // Texto VOL + Porcentaje en Verde CRT
  char buf[16];
  if (_volumePct == 0) {
    snprintf(buf, sizeof(buf), "VOL  MUTE");
  } else {
    snprintf(buf, sizeof(buf), "VOL  %3d%%", _volumePct);
  }
  Display.setTextSize(1);
  Display.setTextColor(TFT_GREEN, TFT_BLACK);
  Display.drawString(buf, x + 8, y + 5);

  // Barra de progreso en bloques verdes
  int barY = y + 20;
  int barW = w - 16;
  Display.fillRect(x + 8, barY, barW, 6, TFT_BLACK);
  Display.drawRect(x + 8, barY, barW, 6, TFT_GREEN);

  int fillW = (barW - 2) * _volumePct / 100;
  if (fillW > 0) {
    Display.fillRect(x + 9, barY + 1, fillW, 4, TFT_GREEN);
  }
}

void GestureController::showBrightnessOSD() {
  int w = 120, h = 32;
  int x = (DISPLAY_WIDTH - w) / 2;
  int y = 35;

  // Caja retro negro con borde verde
  Display.fillRect(x, y, w, h, TFT_BLACK);
  Display.drawRect(x, y, w, h, TFT_GREEN);
  Display.drawRect(x + 1, y + 1, w - 2, h - 2, TFT_GREEN);

  // Icono del Sol Verde a la izquierda (centro x+14, y+16)
  drawSunIcon(x + 14, y + 16, TFT_GREEN);

  // Texto BRI + Porcentaje
  char buf[16];
  snprintf(buf, sizeof(buf), "BRI  %3d%%", _brightnessPct);
  Display.setTextSize(1);
  Display.setTextColor(TFT_GREEN, TFT_BLACK);
  Display.drawString(buf, x + 28, y + 5);

  // Barra de brillo en verde CRT
  int barY = y + 20;
  int barW = w - 36;
  Display.fillRect(x + 28, barY, barW, 6, TFT_BLACK);
  Display.drawRect(x + 28, barY, barW, 6, TFT_GREEN);

  int fillW = (barW - 2) * _brightnessPct / 100;
  if (fillW > 0) {
    Display.fillRect(x + 29, barY + 1, fillW, 4, TFT_GREEN);
  }
}

void GestureController::renderOSD() {
  uint32_t now = millis();
  if (now < _volumeOsdEnd) {
    showVolumeOSD();
  }
  if (now < _brightnessOsdEnd) {
    showBrightnessOSD();
  }
}

// ── Conversion ──
uint8_t GestureController::pctToHwVolume(uint8_t pct) {
  if (pct >= 100) return 255;
  if (pct == 0)   return 0;
  return (uint8_t)((uint32_t)pct * 255 / 100);
}

#endif // WAVESHARE_154
