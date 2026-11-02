#include "menu.h"

#include "auto_off.h"
#include "clock.h"
#include "rotation.h"
#include "wifi_setup.h"

#ifdef WAVESHARE_154

// ── Layout (240x240; text size 2 = 12x16 px per char) ──
static const int BOX_X = 12;
static const int BOX_W = DISPLAY_WIDTH - 2 * BOX_X;
static const int TITLE_H = 22;
static const int ROW_H = 20;
static const int FOOTER_H = 14;
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

void SettingsMenu::addEnum(const char *label, uint8_t *value,
                           const char *const *options, uint8_t count,
                           void (*onChange)(uint8_t)) {
  if (_count >= MAX_ITEMS) return;
  _items[_count++] = {label, ItemType::ENUM, value, 0, (uint8_t)(count - 1), 0,
                      options, nullptr, onChange, Action::NONE};
}

void SettingsMenu::addAction(const char *label, Action action) {
  if (_count >= MAX_ITEMS) return;
  _items[_count++] = {label, ItemType::ACTION, nullptr, 0, 0, 0,
                      nullptr, nullptr, nullptr, action};
}

void SettingsMenu::open(Settings &settings, const Callbacks &cb) {
  _cb = cb;
  _count = 0;
  addRange("VOLUMEN", &settings.volumePct, 0, 100, 10, "%", cb.applyVolume);
  addRange("BRILLO", &settings.brightnessPct, 10, 100, 10, "%", cb.applyBrightness);
  addEnum("ROTACION", &settings.rotation, Rotation::LABELS,
          Rotation::OPTION_COUNT, cb.applyRotation);
  addEnum("APAGADO", cb.sleepOption, AutoOff::SLEEP_LABELS,
          AutoOff::SLEEP_OPTION_COUNT, cb.applySleep);
  addEnum("APAGAR A", &settings.offAtOption, Clock::OFF_AT_LABELS,
          Clock::OFF_AT_OPTION_COUNT, nullptr);
  addEnum("INACTIVIDAD", &settings.idleOffOption, AutoOff::IDLE_LABELS,
          AutoOff::IDLE_OPTION_COUNT, cb.applyIdle);
  addAction("HORA", Action::CLOCK);
  addEnum("ZONA", &settings.tzOption, Clock::TZ_LABELS, Clock::TZ_OPTION_COUNT,
          cb.applyTimezone);
  addAction("WIFI", Action::WIFI);
  addAction("MODO USB", Action::USB_MODE);
  addAction("INFO", Action::INFO);
  addAction("SALIR", Action::EXIT);

  _selected = 0;
  _scroll = 0;
  _wifiSelected = 0;
  _page = Page::MAIN;
  _open = true;
  _dirty = true;
  _lastInputMs = millis();
}

bool SettingsMenu::tick(const Input *input) {
  if (!_open) return false;

  if (input) {
    _lastInputMs = millis();
    _dirty = true;
    if (_page == Page::INFO) {
      // Any input leaves the info page; BACK too (it does not close the menu)
      _page = Page::MAIN;
    } else if (_page == Page::WIFI) {
      _open = tickWifiPage(*input);
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

  // Keep the clock shown in HORA / INFO current
  struct tm t;
  int minute = Clock::localTime(t) ? t.tm_hour * 60 + t.tm_min : -1;
  if (minute != _shownMinute) {
    _shownMinute = minute;
    if (_page != Page::WIFI) _dirty = true;
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
    case Action::WIFI:
      _page = Page::WIFI;
      _wifiSelected = 0;
      return true;
    case Action::USB_MODE:
      if (_cb.enterUsbMode) _cb.enterUsbMode();
      return false;
    case Action::INFO:
      _page = Page::INFO;
      return true;
    case Action::CLOCK: {
      // HORA: set the clock from NTP over the saved WiFi
      if (!_cb.syncClock) return true;
      showMessage("SINCRONIZANDO...", TFT_CYAN);
      const char *err = _cb.syncClock();
      if (err) {
        showMessage(err, TFT_RED);
      } else {
        char msg[20];
        Clock::format(msg, sizeof(msg));
        showMessage(msg, green());
      }
      delay(1500);
      _lastInputMs = millis();
      return true;
    }
    case Action::EXIT:
      return false;
    default:
      return true;
  }
}

// One-line notice over the menu (the next draw() replaces it)
void SettingsMenu::showMessage(const char *text, uint16_t color) {
  Display.fillRect(BOX_X + 4, 104, BOX_W - 8, 30, TFT_BLACK);
  Display.drawRect(BOX_X + 4, 104, BOX_W - 8, 30, color);
  Display.setTextDatum(MC_DATUM);
  Display.setTextSize(2);
  Display.setTextColor(color, TFT_BLACK);
  Display.drawString(text, DISPLAY_WIDTH / 2, 119);
  Display.setTextDatum(TL_DATUM);
}

bool SettingsMenu::tickWifiPage(Input input) {
  // CONECTAR..., ESTADO, SUBIR EPISODIOS, OLVIDAR RED, VOLVER
  static const int WIFI_ITEM_COUNT = 5;
  switch (input) {
    case Input::UP:
      _wifiSelected = (_wifiSelected + WIFI_ITEM_COUNT - 1) % WIFI_ITEM_COUNT;
      return true;
    case Input::DOWN:
      _wifiSelected = (_wifiSelected + 1) % WIFI_ITEM_COUNT;
      return true;
    case Input::BACK:
      _page = Page::MAIN;
      return true;
    case Input::SELECT:
      if (_wifiSelected == 0) {
        // CONECTAR... -> enter SoftAP + QR + captive portal setup
        if (_cb.enterWifiSetup) _cb.enterWifiSetup();
        return false;
      } else if (_wifiSelected == 1) {
        // ESTADO -> connect on demand to saved network and refresh IP / RSSI
        if (_cb.testWifi && WifiSetup::hasSavedNetwork()) {
          Display.fillRect(BOX_X + 4, 80, BOX_W - 8, 28, TFT_BLACK);
          Display.setTextDatum(MC_DATUM);
          Display.setTextSize(2);
          Display.setTextColor(TFT_CYAN, TFT_BLACK);
          Display.drawString("CONECTANDO...", DISPLAY_WIDTH / 2, 94);
          Display.setTextDatum(TL_DATUM);
          _cb.testWifi();
          _lastInputMs = millis();
        }
        return true;
      } else if (_wifiSelected == 2) {
        // SUBIR EPISODIOS -> web upload over the saved network (F5.3)
        if (!WifiSetup::hasSavedNetwork()) {
          showMessage("SIN RED WIFI", TFT_RED);
          delay(1500);
          return true;
        }
        if (_cb.enterUpload) _cb.enterUpload();
        return false;
      } else if (_wifiSelected == 3) {
        // OLVIDAR RED -> clear saved credentials
        if (_cb.forgetWifi) _cb.forgetWifi();
        return true;
      } else {
        // VOLVER
        _page = Page::MAIN;
        return true;
      }
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
  } else if (item.action == Action::CLOCK) {
    Clock::format(buf, len);
  }
}

static int drawFrame(int h, const char *title) {
  int boxY = (DISPLAY_HEIGHT - h) / 2;
  Display.fillRect(BOX_X, boxY, BOX_W, h, TFT_BLACK);
  Display.drawRect(BOX_X, boxY, BOX_W, h, green());
  Display.drawRect(BOX_X + 1, boxY + 1, BOX_W - 2, h - 2, green());

  Display.setTextSize(2);
  Display.setTextColor(green(), TFT_BLACK);
  Display.setTextDatum(TC_DATUM);
  Display.drawString(title, DISPLAY_WIDTH / 2, boxY + 4);
  Display.drawFastHLine(BOX_X + 6, boxY + TITLE_H, BOX_W - 12, dimGreen());
  Display.setTextDatum(TL_DATUM);
  return boxY;
}

static void drawFooter(int boxY, int boxH, const char *text) {
  Display.setTextSize(1);
  Display.setTextColor(dimGreen(), TFT_BLACK);
  Display.setTextDatum(TC_DATUM);
  Display.drawString(text, DISPLAY_WIDTH / 2, boxY + boxH - FOOTER_H + 3);
  Display.setTextDatum(TL_DATUM);
}

void SettingsMenu::draw() {
  if (_page == Page::INFO) {
    drawInfo();
  } else if (_page == Page::WIFI) {
    drawWifi();
  } else {
    drawList();
  }
}

void SettingsMenu::drawList() {
  int rows = min(_count, VISIBLE_ROWS);
  if (_selected < _scroll) _scroll = _selected;
  if (_selected >= _scroll + rows) _scroll = _selected - rows + 1;

  int h = TITLE_H + 4 + rows * ROW_H + FOOTER_H + 4;
  int boxY = drawFrame(h, "CONFIGURACION");

  // Scroll hints at the right of the title
  Display.setTextSize(1);
  Display.setTextColor(dimGreen(), TFT_BLACK);
  if (_scroll > 0) Display.drawString("^", BOX_X + BOX_W - 12, boxY + 4);
  if (_scroll + rows < _count) {
    Display.drawString("v", BOX_X + BOX_W - 12, boxY + 12);
  }

  int y = boxY + TITLE_H + 4;
  for (int i = _scroll; i < _scroll + rows; i++, y += ROW_H) {
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
      bool arrows = sel && item.type != ItemType::ACTION;
      snprintf(shown, sizeof(shown), arrows ? "<%s>" : "%s", value);
      Display.setTextDatum(MR_DATUM);
      Display.drawString(shown, BOX_X + BOX_W - PAD, y + ROW_H / 2);
    }
    Display.setTextDatum(TL_DATUM);
  }

  drawFooter(boxY, h, "BOOT:bajar  +:cambiar  PWR:salir");
}

void SettingsMenu::drawWifi() {
  WifiSetup::StatusInfo st;
  WifiSetup::getStatus(st);

  static const int kCount = 5;
  static const char *const kLabels[kCount] = {
      "CONECTAR...",
      "ESTADO",
      "SUBIR EPISODIOS",
      "OLVIDAR RED",
      "VOLVER",
  };
  const int statusH = 44;
  int h = TITLE_H + 4 + statusH + 6 + kCount * ROW_H + FOOTER_H + 4;
  int boxY = drawFrame(h, "WIFI");

  // Status block: SSID, IP, RSSI
  int sy = boxY + TITLE_H + 6;
  Display.setTextSize(1);
  Display.setTextColor(dimGreen(), TFT_BLACK);
  Display.setTextDatum(TL_DATUM);
  Display.drawString("RED:", BOX_X + PAD, sy);
  Display.setTextColor(TFT_WHITE, TFT_BLACK);
  Display.setTextDatum(TR_DATUM);
  Display.drawString(st.hasSaved ? st.ssid : "SIN RED", BOX_X + BOX_W - PAD, sy);

  sy += 14;
  Display.setTextColor(dimGreen(), TFT_BLACK);
  Display.setTextDatum(TL_DATUM);
  Display.drawString("IP:", BOX_X + PAD, sy);
  Display.setTextColor(st.connected ? green() : TFT_WHITE, TFT_BLACK);
  Display.setTextDatum(TR_DATUM);
  if (st.ip[0]) {
    Display.drawString(st.ip, BOX_X + BOX_W - PAD, sy);
  } else {
    Display.drawString(st.hasSaved ? "GUARDADA" : "--", BOX_X + BOX_W - PAD, sy);
  }

  sy += 14;
  Display.setTextColor(dimGreen(), TFT_BLACK);
  Display.setTextDatum(TL_DATUM);
  Display.drawString("RSSI:", BOX_X + PAD, sy);
  Display.setTextColor(TFT_WHITE, TFT_BLACK);
  Display.setTextDatum(TR_DATUM);
  if (st.connected || st.rssi != 0) {
    char rssiBuf[16];
    snprintf(rssiBuf, sizeof(rssiBuf), "%d dBm", (int)st.rssi);
    Display.drawString(rssiBuf, BOX_X + BOX_W - PAD, sy);
  } else {
    Display.drawString("--", BOX_X + BOX_W - PAD, sy);
  }

  int divY = boxY + TITLE_H + 4 + statusH + 2;
  Display.drawFastHLine(BOX_X + 6, divY, BOX_W - 12, dimGreen());

  int y = divY + 4;
  for (int i = 0; i < kCount; i++, y += ROW_H) {
    bool sel = (i == _wifiSelected);
    uint16_t fg = sel ? TFT_BLACK : green();
    uint16_t bg = sel ? green() : TFT_BLACK;
    Display.fillRect(BOX_X + 4, y, BOX_W - 8, ROW_H - 2, bg);

    Display.setTextSize(2);
    Display.setTextColor(fg, bg);
    Display.setTextDatum(ML_DATUM);
    Display.drawString(kLabels[i], BOX_X + PAD, y + ROW_H / 2);
  }

  Display.setTextDatum(TL_DATUM);
  drawFooter(boxY, h, "BOOT:bajar  +:elegir  PWR:volver");
}

void SettingsMenu::drawInfo() {
  InfoData info = {};
  if (_cb.fillInfo) _cb.fillInfo(info);

  const int lines = 7;
  int h = TITLE_H + 4 + lines * ROW_H + FOOTER_H + 4;
  int boxY = drawFrame(h, "INFO");

  int y = boxY + TITLE_H + 4 + ROW_H / 2;
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
  y += ROW_H;

  Display.setTextDatum(ML_DATUM);
  Display.drawString("HORA", BOX_X + PAD, y);
  Clock::format(buf, sizeof(buf));
  Display.setTextDatum(MR_DATUM);
  Display.drawString(buf, BOX_X + BOX_W - PAD, y);
  y += ROW_H;

  Display.setTextDatum(ML_DATUM);
  Display.drawString("APAGADO", BOX_X + PAD, y);
  Display.setTextDatum(MR_DATUM);
  Display.drawString(info.sleep, BOX_X + BOX_W - PAD, y);
  y += ROW_H;

  Display.setTextDatum(ML_DATUM);
  Display.drawString("WIFI", BOX_X + PAD, y);
  Display.setTextSize(1);
  Display.setTextDatum(MR_DATUM);
  Display.drawString(info.wifi[0] ? info.wifi : "NO", BOX_X + BOX_W - PAD, y);

  Display.setTextDatum(TL_DATUM);
  drawFooter(boxY, h, "Toca o pulsa para volver");
}

#endif  // WAVESHARE_154
