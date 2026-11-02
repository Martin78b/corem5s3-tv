#pragma once

#include <Arduino.h>
#include "config.h"
#include "display_hal.h"
#include "settings.h"

#ifdef WAVESHARE_154

// On-screen settings menu, styled like an old TV service menu (CRT green).
// Drawn straight on the display; the caller pauses playback while it is open.
class SettingsMenu {
public:
  enum class Input : uint8_t {
    UP,      // Swipe up
    DOWN,    // Swipe down, BOOT button
    LEFT,    // Swipe left: previous value
    RIGHT,   // Swipe right: next value
    SELECT,  // Tap, + button: next value (wraps) or run the action
    BACK,    // PWR short press: leave a sub-page, or close the menu
  };

  struct InfoData {
    const char *version;
    int episodes;
    uint8_t batteryPct;
    uint16_t batteryMv;
    bool batteryValid;
    bool charging;
    char sleep[12];  // Sleep timer: "NO", "25m", "FIN EP"
    char wifi[20];   // IP address, saved SSID, or "NO"
  };

  struct Callbacks {
    void (*applyVolume)(uint8_t pct);      // Called on every volume change
    void (*applyBrightness)(uint8_t pct);  // Called on every brightness change
    void (*applyRotation)(uint8_t option); // Settings::rotation changed
    void (*fillInfo)(InfoData &info);
    uint8_t *sleepOption;                  // AutoOff::SLEEP_LABELS index
    void (*applySleep)(uint8_t option);
    void (*applyIdle)(uint8_t option);     // Settings::idleOffOption changed
    void (*enterUsbMode)();                // Requests entering USB MSC mode
    void (*enterWifiSetup)();              // Requests entering WiFi QR setup
    bool (*testWifi)();                    // Connects to saved WiFi on demand
    void (*forgetWifi)();                  // Clears saved WiFi credentials
    void (*applyTimezone)(uint8_t option); // Settings::tzOption changed
    // Connects to the saved WiFi and sets the clock from NTP (blocking).
    // Returns nullptr on success, or the error text to show.
    const char *(*syncClock)();
    void (*enterUpload)();                 // Requests episode upload mode
  };

  void open(Settings &settings, const Callbacks &cb);
  void markDirty() { _dirty = true; }

  // Call every loop() pass while open, with the pending input or nullptr.
  // Returns false once the menu has closed (by the user or by timeout).
  bool tick(const Input *input);

private:
  enum class Page : uint8_t { MAIN, WIFI, INFO };
  enum class ItemType : uint8_t { RANGE, ENUM, ACTION };
  enum class Action : uint8_t { NONE, WIFI, USB_MODE, INFO, CLOCK, EXIT };

  struct Item {
    const char *label;
    ItemType type;
    uint8_t *value;               // RANGE / ENUM
    uint8_t min, max, step;       // RANGE
    const char *const *options;   // ENUM: `max + 1` labels, value = index
    const char *suffix;           // RANGE: appended to the value ("%")
    void (*onChange)(uint8_t);
    Action action;                // ACTION
  };

  static const int MAX_ITEMS = 12;
  static const int VISIBLE_ROWS = 9;  // The main list scrolls past this
  Item _items[MAX_ITEMS];
  int _count = 0;
  int _selected = 0;
  int _scroll = 0;       // First visible row of the main list
  int _shownMinute = -1; // Redraw when the clock's minute changes
  int _wifiSelected = 0;
  Page _page = Page::MAIN;
  bool _open = false;
  bool _dirty = true;
  uint32_t _lastInputMs = 0;
  Callbacks _cb = {};

  void addRange(const char *label, uint8_t *value, uint8_t min, uint8_t max,
                uint8_t step, const char *suffix, void (*onChange)(uint8_t));
  void addEnum(const char *label, uint8_t *value, const char *const *options,
               uint8_t count, void (*onChange)(uint8_t));
  void addAction(const char *label, Action action);
  void change(Item &item, int dir, bool wrap);
  bool activate(Item &item);  // Returns false if the menu should close
  bool tickWifiPage(Input input);
  void showMessage(const char *text, uint16_t color);
  void formatValue(const Item &item, char *buf, size_t len);
  void draw();
  void drawList();
  void drawWifi();
  void drawInfo();
};

#endif  // WAVESHARE_154
