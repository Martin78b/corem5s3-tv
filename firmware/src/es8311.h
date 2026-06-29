#ifndef ES8311_H
#define ES8311_H

#include <stdint.h>
#include <stdbool.h>

#ifdef WAVESHARE_154
bool es8311_init(int sda, int scl, uint8_t addr, int sample_rate);
bool es8311_set_volume(uint8_t vol);
bool es8311_set_mute(bool mute);
#endif

#endif
