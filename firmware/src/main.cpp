#include <Arduino.h>
#include <M5Unified.h>
#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "audio_player.h"
#include "config.h"
#include "crt_effects.h"
#include "video_player.h"

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
      if (!M5.Speaker.isPlaying()) {
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

  auto cfg = M5.config();
  cfg.clear_display = true;
  M5.begin(cfg);

  M5.Display.setBrightness(200);
  M5.Display.setRotation(1);
  M5.Display.setSwapBytes(true);
  M5.Display.fillScreen(TFT_BLACK);

  M5.Speaker.setVolume(128);

  auto spk_cfg = M5.Speaker.config();
  spk_cfg.sample_rate = AUDIO_SAMPLE_RATE;
  spk_cfg.dma_buf_count = 32;
  spk_cfg.dma_buf_len = 128;
  M5.Speaker.config(spk_cfg);

  showBootAnimation();

  if (!initSDCard()) {
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_RED, TFT_BLACK);
    M5.Display.drawString("NO SD CARD", 40, 100);
    while (true)
      delay(1000);
  }

  s_framebuffer = (uint16_t *)ps_malloc(DISPLAY_WIDTH * DISPLAY_HEIGHT * 2);
  if (!s_framebuffer) {
    M5.Display.drawString("PSRAM FAIL", 40, 120);
    while (true)
      delay(1000);
  }
  log_i("Framebuffer: %p (PSRAM)", s_framebuffer);

  if (!s_video.begin()) {
    M5.Display.drawString("VIDEO INIT FAIL", 40, 140);
    while (true)
      delay(1000);
  }

  if (!s_audio.begin()) {
    M5.Display.drawString("AUDIO INIT FAIL", 40, 160);
    while (true)
      delay(1000);
  }

  s_episodeCount = VideoPlayer::scanEpisodes(s_episodes, MAX_EPISODES);
  if (s_episodeCount == 0) {
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.drawString("NO .mjpeg FILES", 20, 100);
    M5.Display.drawString("ON SD CARD", 40, 130);
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
  M5.update();

  handleTouch();

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

  if (!decoded)
    return;

  M5.Display.startWrite();
  M5.Display.setAddrWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  M5.Display.writePixels(s_framebuffer, DISPLAY_WIDTH * DISPLAY_HEIGHT);
  M5.Display.endWrite();

  uint32_t nowMs = millis();
  if (nowMs < s_channelOsdEnd) {
    showChannelOSD(s_channelNumber);
  }
}

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

static bool initSDCard() {
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
    M5.Display.startWrite();
    M5.Display.setAddrWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    M5.Display.writePixels(s_framebuffer, DISPLAY_WIDTH * DISPLAY_HEIGHT);
    M5.Display.endWrite();
    delay(frameTime);
  }
}

static void showBootAnimation() {
  M5.Display.fillScreen(TFT_BLACK);

  for (int i = 0; i < 60; i++) {
    float t = (float)i / 60.0f;
    int cx = DISPLAY_WIDTH / 2;
    int cy = DISPLAY_HEIGHT / 2;
    int radius = (int)(t * 140);
    int color =
        M5.Display.color565((int)(255 * t), (int)(200 * t), (int)(100 * t));
    M5.Display.fillCircle(cx, cy, radius, color);
    delay(30);
  }

  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.drawString("CoreM5S3 TV", 60, 60);
  M5.Display.setTextSize(1);
  M5.Display.drawString("Channel 3", 110, 100);
  M5.Display.drawString("Loading...", 110, 130);
  delay(1500);
}

static void showChannelOSD(int channel) {
  char buf[16];
  snprintf(buf, sizeof(buf), "CH %02d", channel);

  M5.Display.fillRect(240, 4, 76, 22, TFT_BLACK);
  M5.Display.drawRect(240, 4, 76, 22, TFT_WHITE);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  M5.Display.drawString(buf, 246, 8);
}
