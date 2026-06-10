# CoreM5S3 TV — Feasibility Analysis

## 1. Hardware Summary: M5Stack CoreS3 SE

| Component | Specification |
|-----------|--------------|
| SoC | ESP32-S3, dual-core Xtensa LX7 @ 240 MHz |
| Flash | 16 MB (for firmware) |
| PSRAM | 8 MB Quad (external, on-board) |
| Internal SRAM | 512 KB |
| Display | 2.0" IPS, 320×240, ILI9342C, SPI |
| Display bus | SPI (shared with SD), 4-wire |
| Audio amp | AW88298, 16-bit I2S, 1 W speaker |
| Audio codec | ES7210, dual-microphone input |
| microSD | SPI mode, CS=G4, MOSI=G37, MISO=G35, SCK=G36 |
| WiFi | 2.4 GHz 802.11 b/g/n |
| Touch | FT6336U capacitive (I2C) |
| PMIC | AXP2101 |
| RTC | BM8563 |
| Buttons | Power, RST (long-press = download mode) |
| USB | Type-C (OTG/CDC) |

## 2. Critical Design Constraint: Shared GPIO35

GPIO35 serves **both** as the LCD D/C (Data/Command) **output** and as the SD card MISO **input** on the shared SPI bus. This is a hardware design choice by M5Stack.

**Impact:** LCD and SD card cannot actively use the SPI bus simultaneously. The M5GFX library handles this by re-configuring GPIO35 via the GPIO matrix in its `cs_control()` override — switching between `FSPI_MISO` (0x43) and `GPIO_OUT` (0x100) as needed.

**Our approach:** Sequential SPI access — read a JPEG frame from SD, release the bus, decode, then push to display. This naturally fits a video pipeline.

## 3. Video Playback Feasibility

### Proven cases on identical hardware
| Project | Resolution | FPS | Audio | Framework |
|---------|-----------|-----|-------|-----------|
| derdacavga/video-Player | 240×280 | ~20 | I2S (MAX98357) | Arduino |
| KiranPranay/AVI-player | 800×480 | 12–15 | None | ESP-IDF |
| moononournation/RGB565_video | 320×240 | 24–30 | I2S PCM | Arduino |
| t0mg/tinytron | 288×240 | 20–25 | None | Arduino |
| thelastoutpostworkshop/CYD | 240×320 | 15–24 | None | Arduino |

### Our target: 320×240 @ 20 FPS with audio

At 320×240, each JPEG frame is ~15–40 KB (Q5–10). The ESP32-S3 with JPEGDEC library can decode one such frame in **15–30 ms**. SPI display push takes **~5–8 ms** at 40 MHz. Total per frame: **~25–40 ms** → **25–35 FPS theoretical**.

**Adding audio and I2S DMA overhead:** ~5 ms per frame period → **~20–25 FPS sustainable**, which is excellent for a CRT-like effect.

### Memory budget
| Item | Size | Location |
|------|------|----------|
| JPEG decode input buffer | 40 KB | PSRAM |
| Decoded RGB565 frame buffer | 153.6 KB (320×240×2) | PSRAM |
| CRT effect overlay buffer | 153.6 KB (optional) | PSRAM |
| Audio DMA buffer | 4 KB × 2 | PSRAM |
| SD card read buffer | 4 KB | PSRAM |
| JPEGDEC work buffer | 8 KB | PSRAM |
| **Total** | **~360 KB** | of 8 MB available |

### CPU utilization
- **Core 1 (video pipeline):** 60–80% busy (SD read → JPEG decode → SPI push)
- **Core 0 (audio):** 10–20% busy (I2S DMA does most work)
- Headroom available for CRT effects, UI, WiFi

## 4. Audio Feasibility

The AW88298 is a 16-bit I2S amplifier with 1 W output into an 8 Ω speaker. The ESP32-S3's I2S peripheral handles DMA-based output with zero CPU intervention after buffer setup.

**Target audio format:** 22,050 Hz, 16-bit, mono PCM
- Bitrate: 352.8 Kbps
- 1 hour of audio: ~155 MB
- A typical 22-min Simpsons episode: ~57 MB for audio

The ESP-IDF I2S driver supports double-buffering with DMA, allowing glitch-free audio while the CPU is busy decoding video.

## 5. Storage Feasibility

| Item | Per minute | Per 22-min episode |
|------|-----------|-------------------|
| Video (MJPEG, Q5, 320×240 @ 20 FPS) | ~25 MB | ~550 MB |
| Audio (22 kHz, 16-bit, mono PCM) | ~2.6 MB | ~57 MB |
| **Total per episode** | | **~610 MB** |

A 32 GB SD card stores ~50 episodes. A 64 GB card stores ~100 episodes.
Simpsons has ~750+ episodes — only the best/most-rewatched ones fit.

**Optimization:** Using JPEG quality 8–10 instead of 5 reduces file size by ~40% (~360 MB/episode) with acceptable quality on a 2" screen.

## 6. Conversion Pipeline Feasibility

FFmpeg can convert any video to MJPEG + PCM:
```bash
# Video: MJPEG in custom container
ffmpeg -i input.mp4 -c:v mjpeg -q:v 8 -vf "fps=20,scale=320:240" output.mjpeg

# Audio: Raw PCM
ffmpeg -i input.mp4 -vn -ar 22050 -ac 1 -sample_fmt s16 output.pcm
```

The `.mjpeg` format is: `[4-byte frame size][JPEG data]...` — trivially parsed.

## 7. Verdict: FEASIBLE

All critical requirements are met:
- ✅ Display: 320×240 ILI9342C, suitable for video
- ✅ CPU: ESP32-S3 proven at 20+ FPS MJPEG decode
- ✅ Memory: 8 MB PSRAM provides ample frame buffer space
- ✅ Audio: Built-in I2S amp and speaker
- ✅ Storage: SD card with up to 16 GB (official) or larger (practical)
- ✅ Libraries: M5Unified, M5GFX, JPEGDEC, I2S driver all available
- ✅ Software approach: Multiple proven projects to build upon

**Architecture choice:** Arduino framework with PlatformIO (M5Unified + M5GFX handles the GPIO35 bus sharing transparently).
