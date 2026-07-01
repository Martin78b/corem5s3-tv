# Waveshare ESP32-S3-Touch-LCD-1.54 — Hardware Reference

**SKU:** 33869 | **Part No.:** ESP32-S3-Touch-LCD-1.54
**Docs:** https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.54
**Repo:** https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.54
**Product:** https://www.waveshare.com/esp32-s3-lcd-1.54.htm?sku=33869

---

## Specs

| Component | Detail |
|-----------|--------|
| SoC | ESP32-S3R8, dual-core Xtensa LX7 @ 240 MHz |
| PSRAM | 8 MB (octal SPI, stacked) |
| Flash | 16 MB NOR-Flash |
| SRAM | 512 KB (+ 384 KB ROM) |
| Display | 1.54" IPS, 240×240, 262K colors, ST7789V2, 4-wire SPI |
| Touch | CST816T capacitive (I2C `0x15`) — Touch version only |
| Audio DAC | ES8311 (I2C `0x18`) + NS4150B amplifier, mono 3W max |
| Audio ADC | ES7210 (I2C `0x40`) — dual mic, echo cancellation |
| IMU | QMI8658 (6-axis: accel + gyro, I2C `0x6B`) |
| RTC | PCF85063 (I2C `0x51`) |
| PMU | AXP2101 — power management, battery charging, power-on control |
| Battery | MX1.25 2-pin, 3.7V LiPo, with charger management |
| Speaker | MX1.25 2-pin header (non-polarized) |
| USB | Type-C (native ESP32-S3 USB) |
| WiFi | 2.4 GHz 802.11 b/g/n |
| Bluetooth | Bluetooth 5 (LE) |
| Buttons | BOOT/- (GPIO0), PWR (GPIO5), +/Key (GPIO4) |
| SD Card | microSD via SD_MMC (4-bit mode) |
| Camera | FPC connector for OV2640/OV5640 cameras |

---

## Pin Map

### Display (ST7789V2) — 4-wire SPI

| Signal | GPIO | Notes |
|--------|------|-------|
| TFT_CS | 21 | Chip select, active low |
| TFT_DC | 45 | Data/Command (HIGH=data, LOW=command) |
| TFT_RST | 40 | Reset, active low |
| TFT_MOSI | 39 | SPI data (master out) |
| TFT_SCLK | 38 | SPI clock |
| TFT_MISO | — | Not connected |
| TFT_BL | 46 | Backlight PWM |

**SPI:** Mode 0 (CPOL=0, CPHA=0), MSB first.
**Display:** `Arduino_ESP32SPI(45 /* DC */, 21 /* CS */, 38 /* SCK */, 39 /* MOSI */)` with `Arduino_ST7789(bus, 40 /* RST */, 0 /* rotation */, true /* IPS */, 240, 240)`.
**IPS invert:** `invert_colors: true` (ST7789V2 on this panel requires color inversion).

### Touch (CST816T) — I2C

| Signal | GPIO | Notes |
|--------|------|-------|
| TP_SDA | 42 | I2C Data (shared bus) |
| TP_SCL | 41 | I2C Clock (shared bus) |
| TP_INT | 48 | Interrupt (active low) |
| TP_RST | 47 | Reset (active low) |

**I2C address:** `0x15`

### Audio — I2S (ES8311 DAC + NS4150B amp)

| Signal | GPIO | Notes |
|--------|------|-------|
| I2S_MCLK | 8 | Master clock |
| I2S_BCLK | 9 | Bit clock |
| I2S_LRC/WS | 10 | Word select / LR clock |
| I2S_DOUT | 12 | Data out to DAC |
| PA_CTRL | 7 | Amplifier enable (HIGH=on) |

### Audio Codec Config (ES8311)

- **I2C:** SDA=GPIO42, SCL=GPIO41, addr=`0x18`
- **I2C bus:** Shared with CST816T, QMI8658, ES7210 and PCF85063

### Audio ADC (ES7210)

| Signal | GPIO |
|--------|------|
| I2S_DIN | 11 | Data in from microphones |

**I2C address:** `0x40`

### SD Card — SD_MMC (4-bit mode)

| Signal | GPIO |
|--------|------|
| SD_CLK | 16 |
| SD_CMD | 15 |
| SD_D0 | 17 |
| SD_D1 | 18 |
| SD_D2 | 13 |
| SD_D3 | 14 |

Initialized with: `SD_MMC.setPins(16, 15, 17, 18, 13, 14)`.

### Buttons

| Button | GPIO | Notes |
|--------|------|-------|
| BOOT/- | 0 | Internal pull-up, LOW = pressed. Hold + power cycle for download mode |
| PWR | 5 | Power control, connected to AXP2101 PMU |
| +/Key | 4 | Customizable button |

### Power & Battery

| Signal | GPIO | Notes |
|--------|------|-------|
| BAT_EN | 2 | Power latch — keep HIGH to stay powered on battery |
| BAT_ADC | 1 | ADC for battery voltage measurement (divider 200k+100k → ×3) |
| CHG_STAT | 3 | Charge status (LOW = charging, open-drain) |

### I2C Bus (shared: GPIO42=SDA, GPIO41=SCL)

| Device | Address |
|--------|---------|
| ES8311 (audio DAC) | `0x18` |
| CST816T (touch) | `0x15` |
| QMI8658 (IMU) | `0x6B` |
| ES7210 (audio ADC) | `0x40` |
| PCF85063 (RTC) | `0x51` |

### External Headers

| Interface | GPIOs |
|-----------|-------|
| I2C | SDA=42, SCL=41 |
| UART | TX=GPIO43, RX=GPIO44 |
| USB | D-=GPIO19, D+=GPIO20 |

### Additional Board Features

| Feature | Description |
|---------|-------------|
| Camera FPC | 24-pin connector for OV2640/OV5640 cameras |
| Antenna | Onboard PCB antenna + IPEX 1 connector (switchable via resistor) |
| RTC Battery | SH1.0 2-pin connector for rechargeable RTC battery |
| Charging LED | Red LED (D2) — lit during charging |
| Power LED | Green LED (D1) — lit when powered |

---

## Display FPC Connector — 12 pins

| Pin | Signal |
|-----|-------|
| 1 | VCC (3.3V) |
| 2 | GND |
| 3 | MOSI (SPI data) |
| 4 | SCLK (SPI clock) |
| 5 | LCD_CS |
| 6 | LCD_DC |
| 7 | LCD_RST |
| 8 | LCD_BL |
| 9 | TP_SDA (touch I2C) |
| 10 | TP_SCL (touch I2C) |
| 11 | TP_RST (touch reset) |
| 12 | TP_INT (touch interrupt) |

---

## Build Configuration

### PlatformIO (`platformio.ini`)

```ini
[env:waveshare-s3-touch-lcd-154]
platform = espressif32@6.7.0
board = esp32-s3-devkitc-1
framework = arduino
board_build.flash_mode = qio
board_build.f_flash = 80000000L
board_build.partitions = default_16MB.csv
board_build.psram.enable = true
upload_speed = 460800
monitor_speed = 115200

build_flags =
    -DWAVESHARE_154
    -DBOARD_HAS_PSRAM
    -DCORE_DEBUG_LEVEL=0
    -mfix-esp32-psram-cache-issue
    -O2
    -DUSE_FSPI_PORT
    -DUSER_SETUP_LOADED=1
    -DST7789_DRIVER=1
    -DTFT_WIDTH=240
    -DTFT_HEIGHT=240
    -DTFT_CS=21
    -DTFT_DC=45
    -DTFT_RST=40
    -DTFT_MOSI=39
    -DTFT_SCLK=38
    -DTFT_MISO=-1
    -DTFT_BL=46
    -DSPI_FREQUENCY=40000000
    -DSPI_READ_FREQUENCY=20000000

lib_deps =
    bodmer/TFT_eSPI @ ^2.5.43
    bitbank2/JPEGDEC @ ^1.3.0
```

### Arduino IDE

- Board: **ESP32S3 Dev Module**
- Flash Size: **16MB**
- Partition Scheme: **Default 16MB**
- USB CDC On Boot: **Enabled**
- PSRAM: **Octal 80MHz** (if used)

---

## Sources

The information in this document comes from:

1. **Waveshare official docs:** https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.54
2. **Official Arduino examples** (from the Waveshare repository) — `04_gfx_helloworld`, `06_gfx_u8g2_font`, `07_sd_card_test`, `01_audio_out`
3. **Product page:** https://www.waveshare.com/esp32-s3-lcd-1.54.htm?sku=33869
4. **Community documentation:** ESPHome config verified on real hardware (SKU 33869)
