#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "config.h"

#include "audio_player.h"
#include "config.h"
#include "crt_effects.h"
#include "video_player.h"
#include "display_hal.h"
#include "audio_hal.h"
#ifdef WAVESHARE_154
#include "es8311.h"
#include <driver/i2c.h>
#endif

#if defined(M5STACK) || defined(WAVESHARE_154)
  #define TOUCH_SUPPORT 1
#endif

static VideoPlayer s_video;
static AudioPlayer s_audio;
static VideoFile s_episodes[MAX_EPISODES];
static int s_episodeCount = 0;
static int s_currentEpisode = -1;
static uint16_t *s_framebuffer = nullptr;
static bool s_playing = false;
static bool s_staticTransition = false;
static uint32_t s_staticStart = 0;
static uint32_t s_channelOsdEnd = 0;
static int s_channelNumber = 1;
static int s_playlist[MAX_EPISODES];
static int s_playlistIndex = 0;
static int s_playlistSize = 0;
static uint32_t s_lastTouchMs = 0;
static uint32_t s_episodeStartMs = 0;
static bool s_firstFrame = true;

#ifndef M5STACK
  TFT_eSPI Display;
  I2SSpeaker Speaker;
#endif

static SemaphoreHandle_t s_sdMutex = NULL;

static void initDisplay();
static bool initSDCard();
static void buildPlaylist(bool shuffle);
static void playEpisode(int index);
static void nextEpisode();
static void showTVStatic(uint32_t durationMs);
static void showChannelOSD(int channel);
static void showBootAnimation();
static void handleTouch();

static void audioTask(void *arg) {
  while (true) {
    if (s_playing && s_audio.isPlaying()) {
      if (!SPEAKER_OBJ.isPlaying()) {
        if (xSemaphoreTake(s_sdMutex, portMAX_DELAY) == pdTRUE) {
          s_audio.loop();
          xSemaphoreGive(s_sdMutex);
        }
      }
      vTaskDelay(1);
    } else {
      vTaskDelay(10);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);

  log_i("PSRAM found: %s", psramFound() ? "YES" : "NO");
  if (psramFound()) {
    log_i("PSRAM size: %u bytes", ESP.getPsramSize());
  }

  s_sdMutex = xSemaphoreCreateMutex();

#ifdef M5STACK
  auto cfg = M5.config();
  cfg.clear_display = true;
  M5.begin(cfg);
  Display.setBrightness(200);
#else
  Display.begin();
  #ifdef TFT_BL
    #if TFT_BL >= 0
      pinMode(TFT_BL, OUTPUT);
      digitalWrite(TFT_BL, HIGH);
    #endif
  #endif
#endif

  Display.setRotation(0);
  Display.setSwapBytes(true);
  Display.fillScreen(TFT_BLACK);

#ifdef M5STACK
  SPEAKER_OBJ.setVolume(128);
  auto spk_cfg = SPEAKER_OBJ.config();
  spk_cfg.sample_rate = AUDIO_SAMPLE_RATE;
  spk_cfg.dma_buf_count = 32;
  spk_cfg.dma_buf_len = 128;
  SPEAKER_OBJ.config(spk_cfg);
#else
  #ifdef AUDIO_I2S_MCLK
    Speaker.begin(AUDIO_I2S_BCLK, AUDIO_I2S_WS, AUDIO_I2S_DOUT, AUDIO_SAMPLE_RATE, AUDIO_I2S_MCLK);
  #else
    Speaker.begin(AUDIO_I2S_BCLK, AUDIO_I2S_WS, AUDIO_I2S_DOUT, AUDIO_SAMPLE_RATE);
  #endif
#endif

  showBootAnimation();

#ifdef AUDIO_PA_CTRL
  pinMode(AUDIO_PA_CTRL, OUTPUT);
  digitalWrite(AUDIO_PA_CTRL, HIGH);
  delay(10);
#endif

#ifdef WAVESHARE_154
  pinMode(2, OUTPUT);
  digitalWrite(2, HIGH);
  delay(10);
  // Initialize ES8311 audio codec via I2C (SDA=42, SCL=41, addr=0x18)
  if (!es8311_init(42, 41, 0x18, AUDIO_SAMPLE_RATE)) {
    log_w("ES8311 init failed - audio may be silent");
  } else {
    es8311_set_volume(128);
  }
  // Initialize CST816T touch (reset pulse, shares I2C bus I2C_NUM_1 with ES8311)
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);
  delay(10);
  digitalWrite(TOUCH_RST, HIGH);
  delay(50);
  // Configure button pins
  pinMode(BTN_BOOT, INPUT_PULLUP);
  pinMode(BTN_PWR, INPUT_PULLUP);
  pinMode(BTN_PLUS, INPUT_PULLUP);
#endif

  if (!initSDCard()) {
    Display.setTextSize(2);
    Display.setTextColor(TFT_RED, TFT_BLACK);
    Display.drawString("NO SD CARD", 40, 100);
    while (true)
      delay(1000);
  }

  s_framebuffer = (uint16_t *)ps_malloc(DISPLAY_WIDTH * DISPLAY_HEIGHT * 2);
  if (!s_framebuffer) {
    s_framebuffer = (uint16_t *)malloc(DISPLAY_WIDTH * DISPLAY_HEIGHT * 2);
  }
  if (!s_framebuffer) {
    Display.drawString("NO MEMORY", 40, 120);
    while (true)
      delay(1000);
  }
  log_i("Framebuffer: %p (PSRAM)", s_framebuffer);

  if (!s_video.begin()) {
    Display.drawString("VIDEO INIT FAIL", 40, 140);
    while (true)
      delay(1000);
  }

  if (!s_audio.begin()) {
    Display.drawString("AUDIO INIT FAIL", 40, 160);
    while (true)
      delay(1000);
  }

  s_episodeCount = VideoPlayer::scanEpisodes(s_episodes, MAX_EPISODES);
  if (s_episodeCount == 0) {
    Display.setTextSize(2);
    Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    Display.drawString("NO .mjpeg FILES", 20, 100);
    Display.drawString("ON SD CARD", 40, 130);
    while (true)
      delay(1000);
  }

  log_i("Found %d episodes", s_episodeCount);

  xTaskCreatePinnedToCore(audioTask, "audio", 16384, NULL, 5, NULL, 0);

  buildPlaylist(true);

  delay(1000);
  showTVStatic(STATIC_TRANSITION_MS);
  s_channelNumber = 1;
  playEpisode(s_playlist[0]);
}

void loop() {
#ifdef WAVESHARE_154
  handleTouch();
  {
    // BOOT button (GPIO0): short press = next, long 3s = power off
    static uint32_t bootPressStart = 0;
    static bool longPressHandled = false;
    bool btnPressed = (digitalRead(BTN_BOOT) == LOW);
    if (btnPressed) {
      if (bootPressStart == 0) {
        bootPressStart = millis();
        longPressHandled = false;
      } else if (!longPressHandled && millis() - bootPressStart > 3000) {
        longPressHandled = true;
        showTVStatic(STATIC_TRANSITION_MS);
        log_i("Power-off by long-press");
        digitalWrite(2, LOW);
        while (1) delay(1000);
      }
    } else {
      if (bootPressStart > 0 && !longPressHandled) {
        uint32_t held = millis() - bootPressStart;
        if (held > 100 && held < 3000) {
          showTVStatic(800);
          nextEpisode();
        }
      }
      bootPressStart = 0;
      longPressHandled = false;
    }
    // PWR button (GPIO5): short press = next channel
    if (digitalRead(BTN_PWR) == LOW) {
      static uint32_t pwrPressMs = 0;
      if (pwrPressMs == 0) {
        pwrPressMs = millis();
      } else if (millis() - pwrPressMs > 50) {
        showTVStatic(800);
        nextEpisode();
        while (digitalRead(BTN_PWR) == LOW) delay(10);
        pwrPressMs = 0;
      }
    }
    // PLUS button (GPIO4): short press = prev channel
    if (digitalRead(BTN_PLUS) == LOW) {
      static uint32_t plusPressMs = 0;
      if (plusPressMs == 0) {
        plusPressMs = millis();
      } else if (millis() - plusPressMs > 50) {
        showTVStatic(800);
        s_channelNumber = (s_channelNumber <= 1) ? s_episodeCount : s_channelNumber - 1;
        playEpisode(s_playlist[s_channelNumber - 1]);
        while (digitalRead(BTN_PLUS) == LOW) delay(10);
        plusPressMs = 0;
      }
    }
  }
#endif

#ifdef M5STACK
  M5.update();
  handleTouch();
#endif

  static uint32_t audioFinishedAt = 0;
  static bool audioEndHandled = true;

  if (!s_playing) {
    if (!s_staticTransition) {
      s_staticTransition = true;
      s_staticStart = millis();
    }

    if (audioFinishedAt == 0) {
      audioFinishedAt = millis();
    }

    if (millis() - audioFinishedAt > 200) {
      if (!s_audio.isPlaying()) {
        showTVStatic(1500);
        nextEpisode();
        audioFinishedAt = 0;
        s_staticTransition = false;
        audioEndHandled = false;
      }
    }
    delay(10);
    return;
  }

  if (!s_audio.isPlaying()) {
    if (!audioEndHandled) {
      audioEndHandled = true;
      audioFinishedAt = millis();
    }
    if (millis() - audioFinishedAt > 500) {
      s_playing = false;
      s_video.close();
    }
    delay(5);
    return;
  }

  if (s_firstFrame) {
    s_firstFrame = false;
    return;
  }

  uint32_t audioUs = s_audio.getPlaybackTimeUs();
  int targetFrame = (uint64_t)audioUs * VIDEO_FPS / 1000000;

  int lag = targetFrame - s_video.currentFrame();
  if (lag < 0) {
    delay(1);
    return;
  }

  if (lag > 0) {
    int toSkip = (lag > 5) ? 5 : lag;
    while (toSkip-- > 0) {
      bool got = xSemaphoreTake(s_sdMutex, pdMS_TO_TICKS(100)) == pdTRUE;
      if (!got)
        continue;
      uint32_t fs = s_video.readNextFrame();
      xSemaphoreGive(s_sdMutex);
      if (fs == 0)
        break;
    }
  }

  bool decoded = false;
  {
    bool got = xSemaphoreTake(s_sdMutex, pdMS_TO_TICKS(100)) == pdTRUE;
    if (got) {
      uint32_t fs = s_video.readNextFrame();
      xSemaphoreGive(s_sdMutex);
      if (fs > 0)
        decoded = s_video.decodeFrame(s_framebuffer);
    }
  }

  if (!decoded) {
    static uint32_t lastLog = 0;
    if (millis() - lastLog > 3000) {
      log_w("Frame decode failed (frame %d)", s_video.currentFrame());
      lastLog = millis();
    }
    return;
  }

  Display.startWrite();
  Display.setAddrWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  displayWritePixels(s_framebuffer, DISPLAY_WIDTH * DISPLAY_HEIGHT);
  Display.endWrite();

  static uint32_t frameCount = 0;
  if (++frameCount % 30 == 0) {
    log_i("Displayed frame %d/%d (audioUs=%u)", s_video.currentFrame(), s_video.totalFrames(), audioUs);
  }

  uint32_t nowMs = millis();
  if (nowMs < s_channelOsdEnd) {
    showChannelOSD(s_channelNumber);
  }
}

#ifdef M5STACK
static void handleTouch() {
  uint32_t now = millis();
  if (now - s_lastTouchMs < TOUCH_DEBOUNCE_MS)
    return;

  auto t = M5.Touch.getDetail();
  if (t.wasClicked()) {
    s_lastTouchMs = now;
    if (t.x < DISPLAY_WIDTH / 2) {
      showTVStatic(800);
      s_channelNumber =
          (s_channelNumber <= 1) ? s_episodeCount : s_channelNumber - 1;
      playEpisode(s_playlist[s_channelNumber - 1]);
    } else {
      showTVStatic(800);
      nextEpisode();
    }
  }
}
#elif defined(WAVESHARE_154)
static bool touchReadBytes(uint8_t reg, uint8_t *buf, size_t len) {
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (TOUCH_ADDR << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, reg, true);
  i2c_master_stop(cmd);
  esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_1, cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);
  if (ret != ESP_OK) return false;

  cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (TOUCH_ADDR << 1) | I2C_MASTER_READ, true);
  for (size_t i = 0; i < len; i++) {
    i2c_master_read_byte(cmd, &buf[i], (i == len - 1) ? I2C_MASTER_NACK : I2C_MASTER_ACK);
  }
  i2c_master_stop(cmd);
  ret = i2c_master_cmd_begin(I2C_NUM_1, cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);
  return ret == ESP_OK;
}

static void handleTouch() {
  uint32_t now = millis();
  if (now - s_lastTouchMs < TOUCH_DEBOUNCE_MS)
    return;

  uint8_t data[5];
  if (!touchReadBytes(0x00, data, 5)) return;
  uint8_t gesture = data[0];
  uint8_t finger  = data[1];
  if (finger == 0 || gesture == 0x00) return;

  s_lastTouchMs = now;
  // Decode touch point: X = (d[2]<<4 | d[3]>>4), Y = ((d[3]&0x0F)<<8 | d[4])
  uint16_t tx = ((uint16_t)data[2] << 4) | (data[3] >> 4);
  uint16_t ty = ((uint16_t)(data[3] & 0x0F) << 8) | data[4];
  tx = (uint32_t)tx * DISPLAY_WIDTH / 4096;
  ty = (uint32_t)ty * DISPLAY_HEIGHT / 4096;

  (void)ty; // unused but available
  if (tx < DISPLAY_WIDTH / 2) {
    showTVStatic(800);
    s_channelNumber =
        (s_channelNumber <= 1) ? s_episodeCount : s_channelNumber - 1;
    playEpisode(s_playlist[s_channelNumber - 1]);
  } else {
    showTVStatic(800);
    nextEpisode();
  }
}
#endif

static bool initSDCard() {
#ifdef WAVESHARE_154
  if (!SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3)) {
    log_e("SD_MMC setPins failed");
    return false;
  }
  if (!SD_MMC.begin()) {
    log_e("SD_MMC init failed");
    return false;
  }
  uint8_t cardType = SD_MMC.cardType();
  if (cardType != CARD_NONE && cardType != CARD_MMC) {
    uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
    log_i("SD_MMC card detected: %llu MB", cardSize);
    return true;
  }
  log_e("No SD card found");
  return false;
#else
  SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  for (int retry = 0; retry < 3; retry++) {
    if (SD.begin(SD_CS, SPI, SD_SPI_FREQ)) {
      uint8_t cardType = SD.cardType();
      if (cardType != CARD_NONE) {
        uint64_t cardSize = SD.cardSize() / (1024 * 1024);
        log_i("SD card detected: %llu MB", cardSize);
        return true;
      }
    }
    delay(200);
  }
  log_e("SD card init failed");
  return false;
#endif
}

static void buildPlaylist(bool shuffle) {
  s_playlistSize = 0;
  for (int i = 0; i < s_episodeCount; i++) {
    s_playlist[s_playlistSize++] = i;
  }

  if (shuffle && s_playlistSize > 1) {
    for (int i = s_playlistSize - 1; i > 0; i--) {
      int j = esp_random() % (i + 1);
      int tmp = s_playlist[i];
      s_playlist[i] = s_playlist[j];
      s_playlist[j] = tmp;
    }
  }

  s_playlistIndex = 0;
  log_i("Playlist: %d episodes, shuffle=%d", s_playlistSize, shuffle);
}

static void nextEpisode() {
  s_playlistIndex = (s_playlistIndex + 1) % s_playlistSize;
  s_channelNumber = (s_channelNumber % 99) + 1;
  playEpisode(s_playlist[s_playlistIndex]);
}

static void playEpisode(int index) {
  if (index < 0 || index >= s_episodeCount)
    return;

  const char *videoPath = s_episodes[index].path;
  const char *audioPath = s_episodes[index].pcmPath;

  log_i("Playing episode %d: %s", index, videoPath);

  s_episodeStartMs = millis();

  if (xSemaphoreTake(s_sdMutex, portMAX_DELAY) == pdTRUE) {
    if (!s_video.openFile(videoPath)) {
      xSemaphoreGive(s_sdMutex);
      log_e("Failed to open video");
      return;
    }

    if (VideoPlayer::hasAudio(videoPath)) {
      s_audio.playFile(audioPath);
    }
    xSemaphoreGive(s_sdMutex);
  }

  s_firstFrame = true;
  s_currentEpisode = index;
  s_playing = true;
  s_channelOsdEnd = millis() + CHANNEL_OSD_MS;
}

static void showTVStatic(uint32_t durationMs) {
  uint32_t start = millis();
  uint32_t frameTime = 1000 / 25;

  while (millis() - start < durationMs) {
    CRTEffects::generateStatic(s_framebuffer);
    Display.startWrite();
    Display.setAddrWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    displayWritePixels(s_framebuffer, DISPLAY_WIDTH * DISPLAY_HEIGHT);
    Display.endWrite();
    delay(frameTime);
  }
}

static void showBootAnimation() {
  Display.fillScreen(TFT_BLACK);

  for (int i = 0; i < 60; i++) {
    float t = (float)i / 60.0f;
    int cx = DISPLAY_WIDTH / 2;
    int cy = DISPLAY_HEIGHT / 2;
    int radius = (int)(t * 140);
    int color =
        Display.color565((int)(255 * t), (int)(200 * t), (int)(100 * t));
    Display.fillCircle(cx, cy, radius, color);
    delay(30);
  }

  Display.fillScreen(TFT_BLACK);
  Display.setTextSize(2);
  Display.setTextColor(TFT_WHITE, TFT_BLACK);
  Display.drawString("CoreM5S3 TV", 60, 60);
  Display.setTextSize(1);
  Display.drawString("Channel 3", 110, 100);
  Display.drawString("Loading...", 110, 130);
  delay(1500);
}

static void showChannelOSD(int channel) {
  char buf[16];
  snprintf(buf, sizeof(buf), "CH %02d", channel);

  int osdX = DISPLAY_WIDTH - 80;
  Display.fillRect(osdX, 4, 76, 22, TFT_BLACK);
  Display.drawRect(osdX, 4, 76, 22, TFT_WHITE);
  Display.setTextSize(1);
  Display.setTextColor(TFT_GREEN, TFT_BLACK);
  Display.drawString(buf, osdX + 6, 8);
}
