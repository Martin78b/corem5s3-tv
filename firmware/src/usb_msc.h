#pragma once

#include <Arduino.h>
#include "config.h"

#ifdef WAVESHARE_154

// USB Mass Storage mode (HANDOFF F6): exposes the SD card to a PC over
// TinyUSB MSC while playback is stopped and SD_MMC (FATFS) is unmounted.
namespace UsbMsc {

void init();

// True while a USB host (PC) has enumerated the device (chargers do not).
bool pcConnected();

// Returns true once each time a PC connects, so the caller can show the
// on-screen prompt.
bool consumePromptRequest();

// Raw SDMMC init + expose LUN to the host. Call only after closing every
// open File and calling SD_MMC.end().
bool enter();

// Hide LUN from the host and deinit raw SDMMC, waiting for any in-flight
// sector read/write to finish. Caller can then call SD_MMC.begin().
void leave();

// Call from loop() while in USB mode. Returns true when the session should
// end (host ejected the drive or the USB cable was unplugged).
bool tick();

// True when no sector read/write has happened in the last second.
bool isIdle();

// Card capacity in MiB while in USB mode (for the on-screen status).
uint32_t cardSizeMB();

}  // namespace UsbMsc

#endif  // WAVESHARE_154

