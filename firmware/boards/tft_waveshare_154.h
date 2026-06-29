#pragma once

#define USER_SETUP_LOADED 1
#define ST7789_DRIVER 1

#define TFT_WIDTH  240
#define TFT_HEIGHT 240

#define TFT_CS    21
#define TFT_DC    45
#define TFT_RST   40
#define TFT_MOSI  39
#define TFT_SCLK  38
#define TFT_MISO  -1
#define TFT_BL    46

#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY  20000000

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define SMOOTH_FONT
