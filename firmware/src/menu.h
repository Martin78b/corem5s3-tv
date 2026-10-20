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
  };

  struct Callbacks {
    void (*applyVolume)(uint8_t pct);      // Called on every volume change
    void (*applyBrightness)(uint8_t pct);  // Called on every brightness change
    void (*fillInfo)(InfoData &info);
  };

  void open(Settings &settings, const Callbacks &cb);

  // Call every loop() pass while open, with the pending input or nullptr.
  // Returns false once the menu has closed (by the user or by timeout).
  bool tick(const Input *input);

private:
  enum class ItemType : uint8_t { RANGE, ENUM, ACTION };
  enum class Action : uint8_t { NONE, INFO, EXIT };

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

  static const int MAX_ITEMS = 8;
  Item _items[MAX_ITEMS];
  int _count = 0;
  int _selected = 0;
  bool _infoPage = false;
  bool _open = false;
  bool _dirty = true;
  uint32_t _lastInputMs = 0;
  Callbacks _cb = {};

  void addRange(const char *label, uint8_t *value, uint8_t min, uint8_t max,
                uint8_t step, const char *suffix, void (*onChange)(uint8_t));
  void addAction(const char *label, Action action);
  void change(Item &item, int dir, bool wrap);
  bool activate(Item &item);  // Returns false if the menu should close
  void formatValue(const Item &item, char *buf, size_t len);
  void draw();
  void drawList();
  void drawInfo();
};

#endif  // WAVESHARE_154
