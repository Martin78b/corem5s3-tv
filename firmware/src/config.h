#pragma once

// ── Display ──
#if defined(GENERIC_154) || defined(WAVESHARE_154)
#ifndef DISPLAY_WIDTH
#define DISPLAY_WIDTH 240
#endif
#ifndef DISPLAY_HEIGHT
#define DISPLAY_HEIGHT 240
#endif
#else
#define DISPLAY_WIDTH 320
#define DISPLAY_HEIGHT 240
#endif

// ── Video ──
// Default FPS for legacy .mjpeg files without header; files with the
// "CMTV" header carry their own FPS (see VideoPlayer::openFile).
#define VIDEO_FPS 15
#define VIDEO_QUALITY 8
// If video falls more than this many frames behind audio, frames are read
// and discarded (not decoded) until it catches up.
#define VIDEO_MAX_LAG_FRAMES 2
#define VIDEO_MAX_SKIP_PER_LOOP 8   // Cap per loop() so the SD mutex is not held too long
#define MJPEG_EXT ".mjpeg"
#define PCM_EXT ".pcm"

// ── Audio ──
#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_BITS 16
#define AUDIO_CHANNELS 1
#ifdef WAVESHARE_154
#define AUDIO_I2S_BCLK 9
#define AUDIO_I2S_WS 10
#define AUDIO_I2S_DOUT 12
#define AUDIO_I2S_MCLK 8
#define AUDIO_PA_CTRL 7
// Mono I2S frames (I2S_CHANNEL_FMT_ONLY_LEFT) broke audio on the Waveshare
// with the legacy S3 I2S driver, so samples are duplicated to stereo.
#define AUDIO_I2S_MONO 0
#else
#define AUDIO_I2S_BCLK 9
#define AUDIO_I2S_WS 0
#define AUDIO_I2S_DOUT 13
#endif
#define AUDIO_DMA_BUF_COUNT 8
#define AUDIO_DMA_BUF_LEN 512
#define AUDIO_READ_SIZE 8192
#ifndef AUDIO_I2S_MONO
#define AUDIO_I2S_MONO 0
#endif

// ── SD Card ──
#ifdef WAVESHARE_154
#include <SD_MMC.h>
#define SD_CLK 16
#define SD_CMD 15
#define SD_D0 17
#define SD_D1 18
#define SD_D2 13
#define SD_D3 14
#define SD_FS SD_MMC
#else
#include <SD.h>
#define SD_CS 4
#define SD_MOSI 37
#define SD_MISO 35
#define SD_SCLK 36
#define SD_SPI_FREQ 40000000
#define SD_FS SD
#endif

// ── Buttons (Waveshare) ──
#ifdef WAVESHARE_154
#define BTN_BOOT 0
#define BTN_PWR 5
#define BTN_PLUS 4
#define BTN_DEBOUNCE_MS 50
#define PWR_LONG_PRESS_MS 3000   // Hold PWR this long to power off
#endif

// ── Touch (CST816S) ──
#ifdef WAVESHARE_154
#define TOUCH_SDA 42
#define TOUCH_SCL 41
#define TOUCH_INT 48
#define TOUCH_RST 47
#define TOUCH_ADDR 0x15
#endif

// ── Battery (Waveshare) ──
#ifdef WAVESHARE_154
#define BAT_ADC_PIN  1   // GPIO1 - ADC battery voltage (divider x3)
#define BAT_CHG_STAT 3   // GPIO3 - charge status (LOW=charging)
#endif

// ── CRT Effects ──
#define CRT_SCANLINE_ALPHA 48
#define CRT_NOISE_PIXELS 16
#define CRT_CURVATURE 0
#define CRT_STATIC_FRAMES 40

// ── Display DMA (TFT_eSPI only) ──
// Sends each frame by DMA while the next one is decoded. Waveshare only: on
// the generic board the SD card may share the SPI host that initDMA() claims.
#ifndef DISPLAY_DMA
#ifdef WAVESHARE_154
#define DISPLAY_DMA 1
#else
#define DISPLAY_DMA 0
#endif
#endif

// ── Episode Playback ──
// Static list in internal RAM: ~100 bytes per episode (~25 KB for 250)
#define MAX_EPISODES 250
#define EPISODE_PATH_MAX 96   // Longer file names are skipped with a warning
#define BOOT_DELAY_MS 500

// ── TV UI ──
#define CHANNEL_OSD_MS 2000
#define LOW_BATTERY_OSD_MS 3000
#define BATTERY_OSD_MS 10000   // Battery level shown at episode start
#define STATIC_TRANSITION_MS 1000   // Static at boot, before the first episode
#define EPISODE_STATIC_MS 1500      // Static between episodes
#define CHANNEL_STATIC_MS 800       // Static on manual channel change
#define MENU_TIMEOUT_MS 10000       // Menu closes after this long without input
#define SETTINGS_SAVE_DELAY_MS 5000 // Save gesture volume/brightness once they settle

// ── Firmware info (shown under the boot logo) ──
// FW_VERSION / FW_GIT_HASH can be injected with -D at build time (HANDOFF F7.2)
#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif
#ifndef FW_GIT_HASH
#define FW_GIT_HASH ""
#endif
#define FW_REPO_URL "github.com/Martin78b/corem5s3-tv"

// ── CRT Off Animation ──
#define CRT_OFF_ANIM_MS 700

// ── Touch Zones ──
#define TOUCH_TAP_MS 300
#define TOUCH_DEBOUNCE_MS 30

// ── Gesture Debug ──
// Set to 1 to enable detailed gesture logging (may affect playback performance)
#define GESTURE_DEBUG 0
