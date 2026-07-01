#pragma once

#include <Arduino.h>
#include "config.h"

#ifdef M5STACK
  #include <M5Unified.h>
  #define SPEAKER_OBJ M5.Speaker
#else

#include <driver/i2s.h>
#include <esp_err.h>

class I2SSpeaker {
public:
  I2SSpeaker() : _started(false), _playing(false), _volume(128), _mclkPin(-1), _writeUs(0), _writeBytes(0) {}

  bool begin(int bclk, int ws, int dout, int sampleRate, int mclk = -1) {
    if (_started) return true;
    _sampleRate = sampleRate;
    _mclkPin = mclk;

    i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = (uint32_t)sampleRate,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = AUDIO_DMA_BUF_COUNT,
      .dma_buf_len = AUDIO_DMA_BUF_LEN,
    };

    i2s_pin_config_t pin_config = {
      .mck_io_num = mclk,
      .bck_io_num = bclk,
      .ws_io_num = ws,
      .data_out_num = dout,
      .data_in_num = I2S_PIN_NO_CHANGE,
    };

    esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    if (err != ESP_OK) { log_e("I2S install failed"); return false; }

    err = i2s_set_pin(I2S_NUM_0, &pin_config);
    if (err != ESP_OK) { log_e("I2S set pin failed"); return false; }

    _started = true;

    // Start I2S clocks (BCLK, WS, MCLK)
    i2s_start(I2S_NUM_0);
    return true;
  }

  void playRaw(const int16_t* data, size_t samples, uint32_t sampleRate, bool, int, uint8_t vol, bool) {
    if (!_started) return;
    _playing = true;
    size_t written;
    _writeUs = micros();
    _writeBytes = samples * 4;  // stereo: 2 bytes per sample × 2 channels
    // Duplicate mono samples into L+R stereo pairs for I2S_CHANNEL_FMT_RIGHT_LEFT
    // Write in chunks to avoid allocating a large temporary buffer
    const size_t CHUNK = 256;  // stereo samples per chunk
    int16_t stereo[CHUNK * 2];
    size_t remaining = samples;
    const int16_t* src = data;
    while (remaining > 0) {
      size_t n = (remaining > CHUNK) ? CHUNK : remaining;
      for (size_t i = 0; i < n; i++) {
        stereo[i * 2]     = src[i];  // Left
        stereo[i * 2 + 1] = src[i];  // Right (duplicate)
      }
      i2s_write(I2S_NUM_0, stereo, n * 4, &written, portMAX_DELAY);
      src += n;
      remaining -= n;
    }
  }

  void startClocks() {
    if (!_started) return;
    i2s_start(I2S_NUM_0);
  }

  void stop(int) {
    _playing = false;
    i2s_zero_dma_buffer(I2S_NUM_0);
  }

  bool isPlaying(int = 0) {
    if (!_started || !_playing) return false;
    uint64_t elapsed = micros() - _writeUs;
    uint64_t expected = (uint64_t)_writeBytes * 1000000 / (4 * _sampleRate);  // 4 bytes per stereo sample pair
    return elapsed < expected;
  }

  void setVolume(uint8_t vol) {
    _volume = vol;
  }

private:
  bool _started;
  bool _playing;
  uint8_t _volume;
  int _sampleRate;
  int _mclkPin;
  uint64_t _writeUs;
  size_t _writeBytes;
};

extern I2SSpeaker Speaker;
#define SPEAKER_OBJ Speaker

#endif
