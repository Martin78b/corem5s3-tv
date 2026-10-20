#include "menu.h"

#ifdef WAVESHARE_154

// ── Layout (240x240; text size 2 = 12x16 px per char) ──
static const int BOX_X = 12;
static const int BOX_W = DISPLAY_WIDTH - 2 * BOX_X;
static const int BOX_Y = 24;
static const int TITLE_H = 28;
static const int ROW_H = 26;
static const int FOOTER_H = 18;
static const int PAD = 10;

static uint16_t green() { return TFT_GREEN; }
static uint16_t dimGreen() { return Display.color565(0, 120, 40); }

void SettingsMenu::addRange(const char *label, uint8_t *value, uint8_t min,
                            uint8_t max, uint8_t step, const char *suffix,
                            void (*onChange)(uint8_t)) {
  if (_count >= MAX_ITEMS) return;
  _items[_count++] = {label, ItemType::RANGE, value, min, max, step,
                      nullptr, suffix, onChange, Action::NONE};
}

void SettingsMenu::addAction(const char *label, Action action) {
  if (_count >= MAX_ITEMS) return;
  _items[_count++] = {label, ItemType::ACTION, nullptr, 0, 0, 0,
                      nullptr, nullptr, nullptr, action};
}

void SettingsMenu::open(Settings &settings, const Callbacks &cb) {
  _cb = cb;
  _count = 0;
  // Items for F2 (sleep / idle off), F3 (rotation) and F4 (WiFi) are added
  // here when those features exist; Settings already stores their values.
  addRange("VOLUMEN", &settings.volumePct, 0, 100, 10, "%", cb.applyVolume);
  addRange("BRILLO", &settings.brightnessPct, 10, 100, 10, "%", cb.applyBrightness);
  addAction("INFO", Action::INFO);
  addAction("SALIR", Action::EXIT);

  _selected = 0;
  _infoPage = false;
  _open = true;
  _dirty = true;
  _lastInputMs = millis();
}

bool SettingsMenu::tick(const Input *input) {
  if (!_open) return false;

  if (input) {
    _lastInputMs = millis();
    _dirty = true;
    if (_infoPage) {
      // Any input leaves the info page; BACK too (it does not close the menu)
      _infoPage = false;
    } else {
      Item &item = _items[_selected];
      switch (*input) {
        case Input::UP:
          _selected = (_selected + _count - 1) % _count;
          break;
        case Input::DOWN:
          _selected = (_selected + 1) % _count;
          break;
        case Input::LEFT:
          change(item, -1, false);
          break;
        case Input::RIGHT:
          change(item, +1, false);
          break;
        case Input::SELECT:
          if (item.type == ItemType::ACTION) {
            _open = activate(item);
          } else {
            change(item, +1, true);
          }
          break;
        case Input::BACK:
          _open = false;
          break;
      }
    }
  } else if (millis() - _lastInputMs >= MENU_TIMEOUT_MS) {
    _open = false;
  }

  if (_open && _dirty) {
    draw();
    _dirty = false;
  }
  return _open;
}

void SettingsMenu::change(Item &item, int dir, bool wrap) {
  if (!item.value) return;
  int v = *item.value;
  if (item.type == ItemType::RANGE) {
    if (dir > 0) {
      v = (v >= item.max) ? (wrap ? item.min : item.max) : v + item.step;
      if (v > item.max) v = item.max;
    } else {
      v = (v <= item.min) ? item.min : v - item.step;
      if (v < item.min) v = item.min;
    }
  } else if (item.type == ItemType::ENUM) {
    int n = item.max + 1;
    v = wrap ? (v + dir + n) % n : constrain(v + dir, 0, item.max);
  }
  if (v != *item.value) {
    *item.value = (uint8_t)v;
    if (item.onChange) item.onChange(*item.value);
  }
}

bool SettingsMenu::activate(Item &item) {
  switch (item.action) {
    case Action::INFO:
      _infoPage = true;
      return true;
    case Action::EXIT:
      return false;
    default:
      return true;
  }
}

void SettingsMenu::formatValue(const Item &item, char *buf, size_t len) {
  buf[0] = '\0';
  if (item.type == ItemType::RANGE) {
    snprintf(buf, len, "%u%s", *item.value, item.suffix ? item.suffix : "");
  } else if (item.type == ItemType::ENUM && item.options) {
    snprintf(buf, len, "%s", item.options[*item.value]);
  }
}

static void drawFrame(int h, const char *title) {
  Display.fillRect(BOX_X, BOX_Y, BOX_W, h, TFT_BLACK);
  Display.drawRect(BOX_X, BOX_Y, BOX_W, h, green());
  Display.drawRect(BOX_X + 1, BOX_Y + 1, BOX_W - 2, h - 2, green());

  Display.setTextSize(2);
  Display.setTextColor(green(), TFT_BLACK);
  Display.setTextDatum(TC_DATUM);
  Display.drawString(title, DISPLAY_WIDTH / 2, BOX_Y + 7);
  Display.drawFastHLine(BOX_X + 6, BOX_Y + TITLE_H, BOX_W - 12, dimGreen());
  Display.setTextDatum(TL_DATUM);
}

static void drawFooter(int boxH, const char *text) {
  Display.setTextSize(1);
  Display.setTextColor(dimGreen(), TFT_BLACK);
  Display.setTextDatum(TC_DATUM);
  Display.drawString(text, DISPLAY_WIDTH / 2, BOX_Y + boxH - FOOTER_H + 4);
  Display.setTextDatum(TL_DATUM);
}

void SettingsMenu::draw() {
  if (_infoPage) {
    drawInfo();
  } else {
    drawList();
  }
}

void SettingsMenu::drawList() {
  int h = TITLE_H + 6 + _count * ROW_H + FOOTER_H + 4;
  drawFrame(h, "CONFIGURACION");

  int y = BOX_Y + TITLE_H + 6;
  for (int i = 0; i < _count; i++, y += ROW_H) {
    const Item &item = _items[i];
    bool sel = (i == _selected);
    uint16_t fg = sel ? TFT_BLACK : green();
    uint16_t bg = sel ? green() : TFT_BLACK;
    Display.fillRect(BOX_X + 4, y, BOX_W - 8, ROW_H - 2, bg);

    Display.setTextSize(2);
    Display.setTextColor(fg, bg);
    Display.setTextDatum(ML_DATUM);
    Display.drawString(item.label, BOX_X + PAD, y + ROW_H / 2);

    char value[16];
    formatValue(item, value, sizeof(value));
    if (value[0]) {
      char shown[20];
      // Arrows on the selected row hint that left/right change it
      snprintf(shown, sizeof(shown), sel ? "<%s>" : "%s", value);
      Display.setTextDatum(MR_DATUM);
      Display.drawString(shown, BOX_X + BOX_W - PAD, y + ROW_H / 2);
    }
    Display.setTextDatum(TL_DATUM);
  }

  drawFooter(h, "BOOT:bajar  +:cambiar  PWR:salir");
}

void SettingsMenu::drawInfo() {
  InfoData info = {};
  if (_cb.fillInfo) _cb.fillInfo(info);

  const int lines = 4;
  int h = TITLE_H + 6 + lines * ROW_H + FOOTER_H + 4;
  drawFrame(h, "INFO");

  int y = BOX_Y + TITLE_H + 6 + ROW_H / 2;
  char buf[32];
  Display.setTextColor(green(), TFT_BLACK);

  // Version strings can be long (tag-count-hash-dirty): small font
  Display.setTextSize(2);
  Display.setTextDatum(ML_DATUM);
  Display.drawString("VERSION", BOX_X + PAD, y);
  Display.setTextSize(1);
  Display.setTextDatum(MR_DATUM);
  Display.drawString(info.version ? info.version : "?", BOX_X + BOX_W - PAD, y);
  y += ROW_H;

  Display.setTextSize(2);
  Display.setTextDatum(ML_DATUM);
  Display.drawString("BATERIA", BOX_X + PAD, y);
  if (info.batteryValid) {
    snprintf(buf, sizeof(buf), "%u%%%s", info.batteryPct, info.charging ? " C" : "");
  } else {
    snprintf(buf, sizeof(buf), "--");
  }
  Display.setTextDatum(MR_DATUM);
  Display.drawString(buf, BOX_X + BOX_W - PAD, y);
  y += ROW_H;

  Display.setTextDatum(ML_DATUM);
  Display.drawString("VOLTAJE", BOX_X + PAD, y);
  if (info.batteryValid) {
    snprintf(buf, sizeof(buf), "%u.%02uV", info.batteryMv / 1000,
             (info.batteryMv % 1000) / 10);
  } else {
    snprintf(buf, sizeof(buf), "--");
  }
  Display.setTextDatum(MR_DATUM);
  Display.drawString(buf, BOX_X + BOX_W - PAD, y);
  y += ROW_H;

  Display.setTextDatum(ML_DATUM);
  Display.drawString("EPISODIOS", BOX_X + PAD, y);
  snprintf(buf, sizeof(buf), "%d", info.episodes);
  Display.setTextDatum(MR_DATUM);
  Display.drawString(buf, BOX_X + BOX_W - PAD, y);

  Display.setTextDatum(TL_DATUM);
  drawFooter(h, "Toca o pulsa para volver");
}

#endif  // WAVESHARE_154
