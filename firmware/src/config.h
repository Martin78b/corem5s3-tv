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
#define VIDEO_FPS 15
#define VIDEO_QUALITY 8
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
#else
#define AUDIO_I2S_BCLK 9
#define AUDIO_I2S_WS 0
#define AUDIO_I2S_DOUT 13
#endif
#define AUDIO_DMA_BUF_COUNT 8
#define AUDIO_DMA_BUF_LEN 512
#define AUDIO_READ_SIZE 8192

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
#endif

// ── Touch (CST816T) ──
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

// ── Episode Playback ──
#define MAX_EPISODES 100
#define BOOT_DELAY_MS 500

// ── TV UI ──
#define CHANNEL_OSD_MS 2000
#define STATIC_TRANSITION_MS 1000

// ── CRT Off Animation ──
#define CRT_OFF_ANIM_MS 700

// ── Touch Zones ──
#define TOUCH_TAP_MS 300
#define TOUCH_DEBOUNCE_MS 500
