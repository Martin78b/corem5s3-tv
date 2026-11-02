#pragma once

#include <Arduino.h>
#include "config.h"

#ifdef WAVESHARE_154

// On-demand WiFi provisioning via SoftAP + QR code + captive portal (HANDOFF F4).
// WiFi stays completely off during normal video playback to save ~60 KB of
// internal SRAM and battery power.
namespace WifiSetup {

struct StatusInfo {
  bool hasSaved;     // Credentials stored in NVS
  bool connected;    // STA currently connected
  char ssid[33];     // Saved or connected SSID
  char ip[16];       // Current IP (or last IP obtained in this session)
  int8_t rssi;       // Signal strength in dBm when connected, 0 otherwise
};

// Loads saved SSID metadata from NVS and ensures the WiFi radio is off.
void init();

// True if a network SSID is saved in NVS.
bool hasSavedNetwork();

// Populates `out` with the current WiFi state, saved SSID, IP, and RSSI.
void getStatus(StatusInfo &out);

// Connects STA to the saved network in NVS (blocking up to `timeoutMs`) to
// verify credentials and refresh IP / RSSI while inside the settings menu.
bool connectSaved(uint32_t timeoutMs = WIFI_CONNECT_TIMEOUT_MS);

// Clears saved WiFi credentials from NVS and turns off the radio.
void forgetNetwork();

// Turns off WiFi STA/AP radio to free RAM and power before resuming playback.
void stopRadio();

// Starts SoftAP `CoreTV-XXXX` with a random password, DNS captive portal,
// HTTP server, and draws QR 1 on the display.
bool start();

// Stops the captive portal, SoftAP, and WiFi radio.
void stop();

// Forces a full redraw of the current WiFi setup screen (e.g. after rotation).
void markDirty();

// Runs one iteration of the captive portal / setup state machine.
// `toggleQr`: user pressed + / tapped / swiped to switch between QR 1 and QR 2.
// `cancel`: user pressed PWR short to exit setup mode.
// Returns true when setup mode should exit back to playback/menu.
bool tick(bool toggleQr, bool cancel);

// True once a new network was successfully connected and saved during the
// current/last setup session.
bool consumeSavedFlag();

// Full-screen QR page (CRT frame, header, QR, two text lines, footer).
// Shared with the episode upload screen (F5.3).
void drawQrScreen(const char *header, const char *qrPayload,
                  const char *line1, const char *line2, uint16_t line2Color,
                  const char *footer);

}  // namespace WifiSetup

#endif  // WAVESHARE_154

