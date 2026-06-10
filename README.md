# CoreM5S3 TV

![CoreM5S3 TV Logo](assets/corem5s3-tv-logo.png)

Turn your M5Stack CoreS3 SE into a standalone retro TV that plays episodes with a CRT-like experience. No menus, no navigation — just power on and watch.

## Quick Start

```bash
# 1. Convert episodes
python3 tools/convert_episodes.py --input ~/Videos/Simpsons --output /Volumes/SDCARD

# 2. Build and flash firmware
cd firmware && pio run --target upload

# 3. Insert SD card, power on
```

## Photos

| Playing 1 | Playing 2 |
|-----------|-----------|
| ![Playing 1](assets/screen-playing-1.jpg) | ![Playing 2](assets/screen-playing-2.jpg) |

| Playing 3 | Playing 4 |
|-----------|-----------|
| ![Playing 3](assets/screen-playing-3.jpg) | ![Playing 4](assets/screen-playing-4.jpg) |

| Playing 5 |
|-----------|
| ![Playing 5](assets/screen-playing.jpg) |

## Hardware

| Component | Spec |
|-----------|------|
| **Board** | M5Stack CoreS3 SE |
| **SoC** | ESP32-S3, dual-core Xtensa LX7 @ 240 MHz |
| **RAM** | 512 KB SRAM + 8 MB PSRAM |
| **Flash** | 16 MB |
| **Display** | 2.0" IPS 320×240 ILI9342C (SPI) |
| **Audio** | AW88298 I2S amp + 1 W speaker |
| **Storage** | microSD (FAT32, up to 16 GB officially) |
| **Input** | Capacitive touch, power/reset buttons |

## How It Works

```
Video file (.mp4/.mkv)
         │
         ├── ffmpeg ──► .mjpeg (sequential JPEG frames with size prefix)
         │
         └── ffmpeg ──► .pcm (raw 16-bit mono PCM audio)
                                  │
SD Card ──► ESP32-S3 ──► MJPEG decode (JPEGDEC lib)
                           ├── Apply CRT effects (scanlines, noise)
                           ├── Push to ILI9342C display @ 320×240
                           └── Audio sync via I2S → AW88298 speaker
```

### File Format

**`.mjpeg`** — Custom format: `[4-byte frame size][JPEG data]...` repeated. No header, no index — just sequential frames until EOF.

**`.pcm`** — Raw PCM: 44,100 Hz, 16-bit signed, mono, little-endian. No header.

### Pipeline

| Step | Time (approx) | Core |
|------|---------------|------|
| Read JPEG from SD (SPI) | ~2 ms | 1 |
| Decode JPEG (JPEGDEC) | ~55 ms | 1 |
| Push to display (SPI) | ~5 ms | 1 |
| I2S audio DMA | 0 ms (background) | 0 |
| **Total per frame** | **~62 ms** | |
| **Sustained FPS** | **15** | |

## Storage Estimates

| Item | Per 22-min episode |
|------|-------------------|
| Video (320×240, 15 FPS, Q8 MJPEG) | ~350 MB |
| Audio (44.1 kHz, 16-bit, mono PCM) | ~116 MB |
| **Total** | **~466 MB** |

| SD card size | Episodes (approx) |
|-------------|-------------------|
| 16 GB | ~34 |
| 32 GB | ~68 |
| 64 GB | ~137 |
| 128 GB | ~275+ |

## Firmware

### Prerequisites

- [PlatformIO](https://platformio.org/) (`pip install platformio`)
- Python 3.7+
- ffmpeg (`brew install ffmpeg`)

### Build & Flash

```bash
cd firmware
pio run              # Build only
pio run --target upload   # Build + upload via USB
pio run --target monitor  # Serial monitor (115200 baud)
```

### First Boot

1. Format microSD card as **FAT32** (MBR, not GPT)
2. Copy `.mjpeg` and `.pcm` files to the **root** of the SD card
3. Insert into CoreS3 SE
4. Connect USB-C, flash firmware
5. Device reboots into TV mode automatically

No SD card? The display shows "NO SD CARD". No `.mjpeg` files? It shows "NO .mjpeg FILES ON SD CARD".

### Behavior

- **Power on** → Boot animation → TV static → Episode starts
- **Between episodes** → TV static for 2 seconds → next episode
- **Channel OSD** → "CH XX" in top-right corner for 2 seconds per episode
- **Episode order** → Random shuffle on each boot
- **Loop** → Plays continuously until power-off

### CRT Effects

- **Scanlines**: Every other row dimmed by 25% — classic CRT look
- **Noise**: Random pixel noise at low opacity
- **TV Static**: Full random grayscale between episodes

## Conversion Tool

### Basic usage

```bash
# Convert a directory of episode files
python3 tools/convert_episodes.py -i ~/Videos/Simpsons -o /Volumes/SDCARD

# Convert a single file
python3 tools/convert_episodes.py -s "The.Simpsons.S03E01.mkv" -o ./output

# Custom quality settings
python3 tools/convert_episodes.py -i ./videos -o ./output --fps 15 --quality 10

# Estimate storage without converting
python3 tools/convert_episodes.py -i ./videos --dry-run
```

### Filename Recognition

The tool automatically extracts season/episode numbers from filenames:

- `S01E01` / `s01e01` → `S01E01`
- `1x01` → `S01E01`
- `Season.1.Episode.1` → `S01E01`
- Unrecognized → `EPISODE_001`

### Options

| Flag | Default | Description |
|------|---------|-------------|
| `--input` / `-i` | — | Input directory |
| `--single` / `-s` | — | Single video file |
| `--output` / `-o` | `./output` | Output directory |
| `--fps` | 15 | Target frames per second |
| `--quality` / `-q` | 8 | JPEG quality (1–31, lower = better quality, larger files) |
| `--width` | 320 | Output width in pixels |
| `--height` | 240 | Output height in pixels |
| `--audio-rate` | 44100 | Audio sample rate in Hz |
| `--dry-run` / `-n` | false | Estimate only, don't convert |
| `--start-index` | 0 | Starting index for unnamed files |

### Tuning Guide

The most important parameters for playback quality vs. storage:

- **FPS**: 15 is the target. The decoder sustains ~16–18 FPS.
- **Quality**: 8 is a good balance. 5 is near-transparent (larger files). 12+ starts to show artifacts.
- **Resolution**: 320×240 matches the display exactly. Don't change unless you're sure.
- **Audio rate**: 44100 Hz gives good fidelity. 22050 Hz saves ~50% audio space.

## Project Structure

```
CoreM5S3-TV/
├── FEASIBILITY.md                 # Hardware analysis and feasibility
├── README.md                      # This file
├── firmware/                      # PlatformIO firmware project
│   ├── platformio.ini             # Build configuration
│   └── src/
│       ├── main.cpp               # Entry point, TV loop, UI
│       ├── config.h               # All tunable parameters
│       ├── video_player.h/.cpp    # MJPEG decoder + display pipeline
│       ├── audio_player.h/.cpp    # I2S audio playback
│       └── crt_effects.h/.cpp     # Scanlines, noise, static
├── tools/
│   └── convert_episodes.py        # Episode conversion script
└── sd_card/
    └── README.md                  # SD card preparation guide
```

## Memory Map

```
PSRAM (8 MB total):
├── JPEG decode buffer         64 KB
├── RGB565 framebuffer        154 KB
├── JPEGDEC work buffer        ~8 KB
├── SD read buffer              4 KB
├── I2S DMA buffers             ~8 KB
├── CRT effect scratch          ~4 KB
├── Remaining for stack/etc.  ~7.5 MB
└── Audio data buffer           2 KB

Internal SRAM (512 KB):
├── FreeRTOS kernel + tasks   ~200 KB
├── Arduino core               ~100 KB
├── WiFi/LWIP stack            ~60 KB
├── M5GFX + M5Unified         ~50 KB
├── Remaining for stack       ~100 KB
```

## Performance

| Resolution | FPS | Quality | CPU Core 0 | CPU Core 1 |
|-----------|-----|---------|-----------|-----------|
| 320×240 | 15 | 8 | ~5% (audio) | ~90% |

## Stretch Goals

- [ ] Random episode mode (✓ — default behavior)
- [x] Channel up/down (touch or hardware buttons — needs wiring)
- [ ] 1990s TV UI with OSD menus
- [ ] Simpsons intro boot animation (✓ — basic animation present)
- [ ] Volume control (via touch)
- [ ] Sleep timer
- [ ] Favorites list
- [ ] Shuffle mode (✓ — default)
- [ ] Support for Futurama + other shows
- [ ] WiFi streaming from NAS/Jellyfin
- [ ] OTA firmware updates

## Troubleshooting

**"NO SD CARD" on boot:**
- Check SD card is FAT32 formatted
- Try a different SD card (Class 10 recommended)
- Re-insert the card firmly

**"NO .mjpeg FILES" on boot:**
- Run the conversion tool
- Copy files to SD card root (not in a subfolder)
- Check file extensions are lowercase `.mjpeg`

**Video stuttering:**
- Lower FPS to 15 in config.h or conversion settings
- Increase JPEG quality value (e.g., 10 → 12 = smaller frames)
- Use a faster SD card (Class 10 / U1)

**No audio:**
- Audio must be in a separate `.pcm` file with matching base name
- Check audio is 22050 Hz, 16-bit, mono
- Hardware may need the I2S pins verified (see config.h)

**Display corruption:**
- The SD card and display share SPI bus and GPIO35 — this is normal
- M5GFX handles the pin sharing, but if you see corruption, try lowering SD_SPI_FREQ in config.h
