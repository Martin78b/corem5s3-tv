# SD Card Preparation

## Requirements
- microSD card (up to 16 GB officially supported; 32–128 GB may work)
- FAT32 formatted (MBR partition table)
- Class 10 / U1 or faster recommended
- For the Waveshare board, SD_MMC 4-bit mode is used (no SPI pin conflicts)

## Formatting

### macOS
```bash
diskutil list                    # Find your SD card (e.g., /dev/disk2)
diskutil eraseDisk FAT32 COREM5S3TV MBR /dev/disk2
```

### Windows
1. Insert SD card
2. Open "This PC", right-click the SD card → Format
3. File system: FAT32, Allocation unit size: 32 KB
4. Volume label: COREM5S3TV

### Linux
```bash
sudo mkfs.vfat -F 32 -n COREM5S3TV /dev/sdX1
```

## File Layout

Copy the `.mjpeg` and `.pcm` files to the **root** of the SD card:

```
SD card root (/) →
├── S01E01_The_Simpsons.mjpeg
├── S01E01_The_Simpsons.pcm
├── S01E03_Homer_at_the_Bat.mjpeg
├── S01E03_Homer_at_the_Bat.pcm
├── S03E01.mjpeg
├── S03E01.pcm
...
```

### Naming Convention

Files are auto-detected by the `.mjpeg` extension. The `.pcm` file with the same base name is used for audio. If no `.pcm` file exists, the episode plays silently.

Valid patterns:
- `S01E01.mjpeg` + `S01E01.pcm`
- `S03E01_Homer_at_the_Bat.mjpeg` + `S03E01_Homer_at_the_Bat.pcm`
- `EPISODE_001.mjpeg` + `EPISODE_001.pcm`

### Storage Calculations

| SD Size | Est. Episodes (320×240) | Est. Episodes (240×240) | Notes |
|---------|------------------------|------------------------|-------|
| 16 GB | ~35 | ~51 | Official max |
| 32 GB | ~70 | ~103 | May work |
| 64 GB | ~150 | ~207 | May need exFAT format |
| 128 GB | ~300+ | ~415+ | May need exFAT format |

> Note: Cards larger than 32 GB are often factory-formatted as exFAT. The ESP32's FAT32 driver does not support exFAT. You must reformat larger cards to FAT32 using a third-party tool.
