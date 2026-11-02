#pragma once

#include <Arduino.h>
#include "config.h"

#ifdef WAVESHARE_154

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Upload episodes to the SD card from a browser on the home network
// (HANDOFF F5.3). Needs the WiFi STA already connected; playback must be
// stopped (no files open) while it runs. Only .mjpeg / .pcm files in the SD
// root can be uploaded, replaced or deleted.
namespace WebUpload {

// Starts the HTTP server on port 80 and draws the screen (QR with the URL).
// `sdMutex` guards SD access (shared with audioTask).
bool start(SemaphoreHandle_t sdMutex);
void stop();

// Forces a full redraw (e.g. after rotation).
void markDirty();

// One loop() pass. Returns true when the mode should end: `cancel` (PWR
// short), "TERMINAR" in the browser, WiFi lost, or UPLOAD_IDLE_TIMEOUT_MS
// without requests.
bool tick(bool cancel);

// True if any file was uploaded or deleted since start().
bool filesChanged();

}  // namespace WebUpload

#endif  // WAVESHARE_154
