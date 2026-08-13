#include "backlight.h"

#ifdef WAVESHARE_154

static const char* BL_TAG = "Backlight";

// LEDC configuration
static const ledc_channel_t BL_LEDC_CHANNEL = LEDC_CHANNEL_0;
static const ledc_timer_t   BL_LEDC_TIMER   = LEDC_TIMER_0;
static const uint32_t       BL_LEDC_FREQ_HZ = 5000;
static const ledc_timer_bit_t BL_LEDC_RESOLUTION = LEDC_TIMER_8_BIT; // 0-255

static uint8_t s_currentPct = 100;
static bool    s_initialized = false;

static uint32_t pctToDuty(uint8_t pct) {
  if (pct >= 100) return 255;
  if (pct == 0)   return 0;
  return (uint32_t)pct * 255 / 100;
}

void Backlight::begin(int gpio, uint8_t initialPct) {
  ledc_timer_config_t timer_cfg = {};
  timer_cfg.speed_mode      = LEDC_LOW_SPEED_MODE;
  timer_cfg.timer_num       = BL_LEDC_TIMER;
  timer_cfg.duty_resolution = BL_LEDC_RESOLUTION;
  timer_cfg.freq_hz         = BL_LEDC_FREQ_HZ;
  timer_cfg.clk_cfg         = LEDC_AUTO_CLK;

  esp_err_t err = ledc_timer_config(&timer_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(BL_TAG, "LEDC timer config failed: %d", err);
    // Fallback to digital HIGH
    pinMode(gpio, OUTPUT);
    digitalWrite(gpio, HIGH);
    return;
  }

  ledc_channel_config_t ch_cfg = {};
  ch_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
  ch_cfg.channel    = BL_LEDC_CHANNEL;
  ch_cfg.timer_sel  = BL_LEDC_TIMER;
  ch_cfg.intr_type  = LEDC_INTR_DISABLE;
  ch_cfg.gpio_num   = gpio;
  ch_cfg.duty       = pctToDuty(initialPct);
  ch_cfg.hpoint     = 0;

  err = ledc_channel_config(&ch_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(BL_TAG, "LEDC channel config failed: %d", err);
    pinMode(gpio, OUTPUT);
    digitalWrite(gpio, HIGH);
    return;
  }

  s_currentPct = initialPct;
  s_initialized = true;
  ESP_LOGI(BL_TAG, "Backlight initialized on GPIO %d at %d%%", gpio, initialPct);
}

void Backlight::setBrightness(uint8_t pct) {
  if (pct > 100) pct = 100;
  if (!s_initialized) return;

  s_currentPct = pct;
  ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL, pctToDuty(pct));
  ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL);
}

uint8_t Backlight::getBrightness() {
  return s_currentPct;
}

#endif // WAVESHARE_154
