#pragma once

#define USER_SETUP_LOADED 1
#define ST7789_DRIVER 1

#define TFT_WIDTH  240
#define TFT_HEIGHT 240

#define TFT_CS   10
#define TFT_DC    9
#define TFT_RST   8
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_MISO -1

#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY  20000000

#define TFT_BL   45

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define SMOOTH_FONT
