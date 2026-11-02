#include "wifi_setup.h"

#ifdef WAVESHARE_154

#include "clock.h"
#include "display_hal.h"
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <qrcode.h>

namespace WifiSetup {

static const char *NVS_NAMESPACE = "coretv";
static const char *KEY_SSID = "w_ssid";
static const char *KEY_PASS = "w_pass";
static const uint8_t DNS_PORT = 53;

enum class Step : uint8_t {
  IDLE,
  QR_AP,       // Step 1: QR to join SoftAP CoreTV-XXXX
  QR_URL,      // Step 2: QR to open http://192.168.4.1
  CONNECTING,  // Testing submitted STA credentials
  CONNECTED,   // Connected ✓ + IP shown before exiting
  FAIL,        // Connection failed; returns to QR_URL after a pause
};

static Step s_step = Step::IDLE;
// Outcome of the last credentials test, reported to the phone by /status
// (s_step goes back to QR_URL after a failure).
enum class Result : uint8_t { NONE, CONNECTING, OK, FAIL };
static Result s_result = Result::NONE;
static bool s_ntpTried = false;  // NTP sync done for this CONNECTED screen
static bool s_dirty = false;
static bool s_savedFlag = false;
static bool s_radioActive = false;
static uint32_t s_lastActivityMs = 0;
static uint32_t s_stepStartMs = 0;
static uint8_t s_lastStations = 0;

static char s_apSsid[20] = "";
static char s_apPass[12] = "";
static char s_pendingSsid[33] = "";
static char s_pendingPass[65] = "";

static char s_savedSsid[33] = "";
static char s_lastIp[16] = "";
static int8_t s_lastRssi = 0;

static DNSServer *s_dns = nullptr;
static WebServer *s_web = nullptr;

// Why the last STA attempt failed, captured from WiFi events (they run in the
// WiFi event task, hence volatile). Shown on the TV and on the phone.
static volatile uint8_t s_discReason = 0;
static volatile bool s_associated = false;  // Joined the AP, maybe no IP yet
static bool s_eventsRegistered = false;
static char s_failText[28] = "";

static void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
    s_associated = true;
  } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    uint8_t reason = info.wifi_sta_disconnected.reason;
    // ASSOC_LEAVE is our own disconnect() call, not a failure
    if (reason != WIFI_REASON_ASSOC_LEAVE) s_discReason = reason;
  }
}

static void registerEvents() {
  if (s_eventsRegistered) return;
  WiFi.onEvent(onWifiEvent, ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(onWifiEvent, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  s_eventsRegistered = true;
}

// Starts an STA connection with settings that accept any router:
//  - min security WEP: Arduino's default (WPA2) makes the driver ignore
//    WPA/TKIP-only routers, which then look like "network not found";
//  - full channel scan, strongest AP first (no channel/BSSID pinning, which
//    fails on mesh / multi-AP networks or if the router changed channel).
static void beginSta(const char *ssid, const char *pass) {
  registerEvents();
  s_discReason = 0;
  s_associated = false;
  WiFi.setMinSecurity(WIFI_AUTH_WEP);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, (pass && pass[0]) ? pass : nullptr);
}

static void describeFailure() {
  uint8_t r = s_discReason;
  const char *txt = nullptr;
  switch (r) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
      txt = "Clave incorrecta";
      break;
    case WIFI_REASON_NO_AP_FOUND:
      txt = "Red no encontrada";
      break;
    case WIFI_REASON_ASSOC_FAIL:
    case WIFI_REASON_ASSOC_TOOMANY:
      txt = "El router la rechazo";
      break;
    case WIFI_REASON_BEACON_TIMEOUT:
      txt = "Senal muy debil";
      break;
    default:
      break;
  }
  if (txt) {
    snprintf(s_failText, sizeof(s_failText), "%s", txt);
  } else if (s_associated) {
    snprintf(s_failText, sizeof(s_failText), "Sin IP del router (DHCP)");
  } else if (r) {
    snprintf(s_failText, sizeof(s_failText), "Error WiFi (motivo %u)", r);
  } else {
    snprintf(s_failText, sizeof(s_failText), "Sin respuesta del router");
  }
  log_w("WiFi connect failed: reason %u, associated %d -> %s", r,
        (int)s_associated, s_failText);
}

static uint16_t green() { return TFT_GREEN; }
static uint16_t dimGreen() { return Display.color565(0, 120, 40); }

static void loadSavedFromNvs() {
  s_savedSsid[0] = '\0';
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, true)) return;
  String ssid = prefs.getString(KEY_SSID, "");
  prefs.end();
  if (ssid.length() > 0 && ssid.length() < sizeof(s_savedSsid)) {
    strncpy(s_savedSsid, ssid.c_str(), sizeof(s_savedSsid) - 1);
    s_savedSsid[sizeof(s_savedSsid) - 1] = '\0';
  }
}

static void saveCredentialsToNvs(const char *ssid, const char *pass) {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, false)) {
    log_e("WifiSetup: NVS open failed");
    return;
  }
  prefs.putString(KEY_SSID, ssid);
  prefs.putString(KEY_PASS, pass ? pass : "");
  prefs.end();

  strncpy(s_savedSsid, ssid, sizeof(s_savedSsid) - 1);
  s_savedSsid[sizeof(s_savedSsid) - 1] = '\0';
  s_savedFlag = true;
  log_i("WiFi credentials saved for SSID '%s'", s_savedSsid);
}

static void generateApCredentials() {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  snprintf(s_apSsid, sizeof(s_apSsid), "CoreTV-%02X%02X", mac[4], mac[5]);

  // Same password for every attempt until the next reboot: if the phone drops
  // off the AP (or setup is retried) it can rejoin with the saved credentials.
  if (s_apPass[0]) return;
  // Unambiguous characters (no 0/O/1/I/l) so the password is easy to read
  // if typed manually instead of scanning the QR.
  static const char kAlphabet[] =
      "23456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz";
  const size_t alphaLen = sizeof(kAlphabet) - 1;
  for (int i = 0; i < 8; i++) {
    s_apPass[i] = kAlphabet[esp_random() % alphaLen];
  }
  s_apPass[8] = '\0';
}

static String htmlEscape(const String &in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += c; break;
    }
  }
  return out;
}

static void stopPortalServers() {
  if (s_web) {
    s_web->stop();
    delete s_web;
    s_web = nullptr;
  }
  if (s_dns) {
    s_dns->stop();
    delete s_dns;
    s_dns = nullptr;
  }
}

static void handlePortalRoot() {
  if (!s_web) return;
  s_lastActivityMs = millis();

  bool forceRescan = s_web->hasArg("rescan");
  int n = WiFi.scanComplete();
  if (forceRescan || n < 0) {
    WiFi.scanDelete();
    n = WiFi.scanNetworks(false, false);
  }

  // Sort indices by RSSI descending and deduplicate SSIDs
  int indices[32];
  int count = 0;
  for (int i = 0; i < n && count < 32; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;
    bool dup = false;
    for (int j = 0; j < count; j++) {
      if (WiFi.SSID(indices[j]) == ssid) {
        if (WiFi.RSSI(i) > WiFi.RSSI(indices[j])) indices[j] = i;
        dup = true;
        break;
      }
    }
    if (!dup) indices[count++] = i;
  }
  for (int i = 0; i < count - 1; i++) {
    for (int j = i + 1; j < count; j++) {
      if (WiFi.RSSI(indices[j]) > WiFi.RSSI(indices[i])) {
        int tmp = indices[i];
        indices[i] = indices[j];
        indices[j] = tmp;
      }
    }
  }

  String page;
  page.reserve(2600);
  page += "<!DOCTYPE html><html lang='es'><head><meta charset='utf-8'>"
          "<meta name='viewport' content='width=device-width,initial-scale=1'>"
          "<title>CoreTV WiFi</title><style>"
          "body{background:#080c08;color:#00ff55;font-family:monospace,sans-serif;"
          "margin:0;padding:16px;display:flex;justify-content:center}"
          ".box{width:100%;max-width:360px;border:2px solid #00ff55;padding:16px;"
          "box-sizing:border-box;background:#000}"
          "h1{font-size:20px;margin:0 0 12px;text-align:center;"
          "border-bottom:1px solid #007828;padding-bottom:8px}"
          "label{display:block;margin:12px 0 4px;font-size:13px;color:#88ffaa}"
          "select,input,button{width:100%;padding:10px;font-family:inherit;"
          "font-size:15px;box-sizing:border-box;background:#0b140b;color:#00ff55;"
          "border:1px solid #00ff55;border-radius:0}"
          "button{margin-top:16px;background:#00ff55;color:#000;font-weight:bold;"
          "cursor:pointer}"
          ".sec{background:#000;color:#00ff55;border-color:#007828;margin-top:8px;"
          "font-weight:normal;font-size:13px}"
          ".row{display:flex;align-items:center;gap:8px;margin-top:6px;font-size:12px}"
          ".row input{width:auto}"
          "</style></head><body><div class='box'><h1>CORETV WIFI</h1>"
          "<form method='POST' action='/connect'>"
          "<label>Red WiFi</label>"
          "<select id='sel' name='ssid_sel' onchange='onSel(this.value)'>";

  if (count == 0) {
    page += "<option value='__custom__'>-- Sin redes (escribir manual) --</option>";
  } else {
    for (int i = 0; i < count; i++) {
      int idx = indices[i];
      String ssid = htmlEscape(WiFi.SSID(idx));
      int rssi = WiFi.RSSI(idx);
      bool open = (WiFi.encryptionType(idx) == WIFI_AUTH_OPEN);
      page += "<option value=\"";
      page += ssid;
      page += "\">";
      page += ssid;
      page += " (";
      page += String(rssi);
      page += " dBm)";
      if (!open) page += " *";
      page += "</option>";
    }
    page += "<option value='__custom__'>-- Otra red (manual) --</option>";
  }

  page += "</select>"
          "<div id='cust' style='display:";
  page += (count == 0) ? "block" : "none";
  page += "'><label>Nombre de red (SSID)</label>"
          "<input id='c_in' name='ssid_custom' maxlength='32' placeholder='SSID'>"
          "</div>"
          "<label>Contrasena</label>"
          "<input id='pw' type='password' name='pass' maxlength='64' "
          "placeholder='Dejar vacio si es abierta'>"
          "<label class='row'><input type='checkbox' "
          "onclick=\"document.getElementById('pw').type=this.checked?'text':'password'\">"
          "Mostrar contrasena</label>"
          "<button type='submit'>CONECTAR</button>"
          "</form>"
          "<form method='GET' action='/'>"
          "<input type='hidden' name='rescan' value='1'>"
          "<button type='submit' class='sec'>REESCANEAR REDES</button>"
          "</form>"
          "<script>function onSel(v){var d=document.getElementById('cust');"
          "d.style.display=(v==='__custom__')?'block':'none';}</script>"
          "</div></body></html>";

  s_web->send(200, "text/html; charset=utf-8", page);
}

static void handlePortalConnect() {
  if (!s_web) return;
  s_lastActivityMs = millis();

  String ssid = s_web->arg("ssid_sel");
  if (ssid == "__custom__" || ssid.length() == 0) {
    ssid = s_web->arg("ssid_custom");
    ssid.trim();  // Typed by hand; scanned SSIDs may really have spaces
  }
  String pass = s_web->arg("pass");

  if (ssid.length() == 0 || ssid.length() >= sizeof(s_pendingSsid)) {
    s_web->sendHeader("Location", "http://192.168.4.1/", true);
    s_web->send(302, "text/plain", "");
    return;
  }

  strncpy(s_pendingSsid, ssid.c_str(), sizeof(s_pendingSsid) - 1);
  s_pendingSsid[sizeof(s_pendingSsid) - 1] = '\0';
  strncpy(s_pendingPass, pass.c_str(), sizeof(s_pendingPass) - 1);
  s_pendingPass[sizeof(s_pendingPass) - 1] = '\0';

  String escSsid = htmlEscape(ssid);
  String resp =
      "<!DOCTYPE html><html lang='es'><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Conectando...</title><style>"
      "body{background:#080c08;color:#00ff55;font-family:monospace,sans-serif;"
      "margin:0;padding:16px;display:flex;justify-content:center}"
      ".box{width:100%;max-width:360px;border:2px solid #00ff55;padding:16px;"
      "background:#000;text-align:center}"
      "</style></head><body><div class='box'>"
      "<h2>PROBANDO CONEXION</h2><p>Red: <b>" +
      escSsid +
      "</b></p><p id='st'>Conectando... (hasta " +
      String(WIFI_CONNECT_TIMEOUT_MS / 1000) +
      " s)</p><p style='font-size:12px;color:#88ffaa'>Si el telefono se "
      "desconecta de " +
      String(s_apSsid) +
      ", vuelve a unirte: la clave no cambia. El resultado tambien se ve en el "
      "televisor.</p></div><script>"
      "var e=document.getElementById('st');"
      "function p(){fetch('/status',{cache:'no-store'}).then(function(r){"
      "return r.json();}).then(function(j){"
      "if(j.s==='ok'){e.innerHTML='CONECTADO<br>IP '+j.ip;return;}"
      "if(j.s==='fail'){e.innerHTML='NO SE PUDO CONECTAR:<br><b>'+j.r+"
      "'</b><br><br><a style=\"color:#00ff55\" href=\"/\">VOLVER A "
      "INTENTAR</a>';return;}"
      "setTimeout(p,1500);}).catch(function(){"
      "e.innerHTML='Esperando al televisor...';setTimeout(p,2000);});}"
      "setTimeout(p,1500);</script></body></html>";
  s_web->send(200, "text/html; charset=utf-8", resp);
  delay(50);

  beginSta(s_pendingSsid, s_pendingPass);
  s_failText[0] = '\0';
  s_result = Result::CONNECTING;
  s_step = Step::CONNECTING;
  s_stepStartMs = millis();
  s_dirty = true;
}

// Polled by the "PROBANDO CONEXION" page to show the result on the phone.
static void handlePortalStatus() {
  if (!s_web) return;
  s_lastActivityMs = millis();
  const char *st = "none";
  if (s_result == Result::CONNECTING) st = "connecting";
  else if (s_result == Result::OK) st = "ok";
  else if (s_result == Result::FAIL) st = "fail";
  String json = String("{\"s\":\"") + st + "\",\"ip\":\"" + s_lastIp +
                "\",\"r\":\"" + s_failText + "\"}";
  s_web->sendHeader("Cache-Control", "no-store");
  s_web->send(200, "application/json", json);
}

static void handleCaptiveRedirect() {
  if (!s_web) return;
  s_lastActivityMs = millis();
  s_web->sendHeader("Location", "http://192.168.4.1/", true);
  s_web->send(302, "text/plain", "");
}

// Callback used by esp_qrcode_generate() to draw modules onto Display.
static void renderQrCallback(esp_qrcode_handle_t qrcode) {
  int qrSize = esp_qrcode_get_size(qrcode);
  if (qrSize <= 0) return;

  // Target ~124-136 px for the QR matrix + 6 px white quiet zone on each side
  int scale = 132 / qrSize;
  if (scale < 2) scale = 2;
  if (scale > 5) scale = 5;

  const int quiet = 6;
  int totalSide = qrSize * scale + 2 * quiet;
  int boxX = (DISPLAY_WIDTH - totalSide) / 2;
  int boxY = 30 + (142 - totalSide) / 2;

  Display.fillRect(boxX, boxY, totalSide, totalSide, TFT_WHITE);
  int qrX = boxX + quiet;
  int qrY = boxY + quiet;
  for (int y = 0; y < qrSize; y++) {
    for (int x = 0; x < qrSize; x++) {
      if (esp_qrcode_get_module(qrcode, x, y)) {
        Display.fillRect(qrX + x * scale, qrY + y * scale, scale, scale,
                         TFT_BLACK);
      }
    }
  }
}

void drawQrScreen(const char *header, const char *qrPayload,
                  const char *line1, const char *line2, uint16_t line2Color,
                  const char *footer) {
  Display.fillScreen(TFT_BLACK);
  const int bx = 6, by = 4, bw = DISPLAY_WIDTH - 12, bh = DISPLAY_HEIGHT - 8;
  Display.drawRect(bx, by, bw, bh, green());
  Display.drawRect(bx + 1, by + 1, bw - 2, bh - 2, green());

  Display.setTextDatum(TC_DATUM);
  Display.setTextSize(1);
  Display.setTextColor(green(), TFT_BLACK);
  Display.drawString(header, DISPLAY_WIDTH / 2, by + 7);
  Display.drawFastHLine(bx + 6, by + 19, bw - 12, dimGreen());

  esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
  cfg.display_func = renderQrCallback;
  cfg.max_qrcode_version = 6;
  cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
  esp_qrcode_generate(&cfg, qrPayload);

  Display.setTextSize(1);
  Display.setTextColor(TFT_WHITE, TFT_BLACK);
  Display.drawString(line1, DISPLAY_WIDTH / 2, 180);
  Display.setTextColor(line2Color, TFT_BLACK);
  Display.drawString(line2, DISPLAY_WIDTH / 2, 194);

  Display.setTextColor(dimGreen(), TFT_BLACK);
  Display.drawString(footer, DISPLAY_WIDTH / 2, by + bh - 12);
  Display.setTextDatum(TL_DATUM);
}

static void drawStatusBox(const char *title, const char *line1,
                          const char *line2, uint16_t accent,
                          bool drawCheckmark) {
  Display.fillScreen(TFT_BLACK);
  const int bx = 12, by = 36, bw = DISPLAY_WIDTH - 24, bh = 168;
  Display.drawRect(bx, by, bw, bh, accent);
  Display.drawRect(bx + 1, by + 1, bw - 2, bh - 2, accent);

  Display.setTextDatum(TC_DATUM);
  Display.setTextSize(2);
  Display.setTextColor(accent, TFT_BLACK);
  Display.drawString(title, DISPLAY_WIDTH / 2, by + 14);
  Display.drawFastHLine(bx + 6, by + 36, bw - 12, dimGreen());

  if (drawCheckmark) {
    // Crisp check mark icon in the center
    int cx = DISPLAY_WIDTH / 2;
    int cy = by + 70;
    for (int t = -1; t <= 1; t++) {
      Display.drawLine(cx - 14, cy + t, cx - 4, cy + 10 + t, green());
      Display.drawLine(cx - 4, cy + 10 + t, cx + 16, cy - 10 + t, green());
    }
  }

  Display.setTextSize(1);
  Display.setTextColor(TFT_WHITE, TFT_BLACK);
  if (line1 && line1[0]) {
    Display.drawString(line1, DISPLAY_WIDTH / 2, by + 100);
  }
  Display.setTextSize(2);
  Display.setTextColor(accent, TFT_BLACK);
  if (line2 && line2[0]) {
    Display.drawString(line2, DISPLAY_WIDTH / 2, by + 122);
  }
  Display.setTextDatum(TL_DATUM);
}

static const char *const kQrFooter = "+:otro QR   PWR:salir";

static void draw() {
  if (s_step == Step::QR_AP) {
    char qr[96];
    snprintf(qr, sizeof(qr), "WIFI:T:WPA;S:%s;P:%s;;", s_apSsid, s_apPass);
    char l1[36];
    char l2[36];
    snprintf(l1, sizeof(l1), "Red: %s", s_apSsid);
    snprintf(l2, sizeof(l2), "Clave: %s", s_apPass);
    drawQrScreen("1/2 ESCANEA PARA UNIRTE AL WIFI", qr, l1, l2, TFT_YELLOW,
                 kQrFooter);
  } else if (s_step == Step::QR_URL) {
    drawQrScreen("2/2 ABRE EL PORTAL DE REDES", "http://192.168.4.1",
                 "Si el portal no abrio solo:", "http://192.168.4.1",
                 TFT_CYAN, kQrFooter);
  } else if (s_step == Step::CONNECTING) {
    drawStatusBox("CONECTANDO", "Probando red WiFi...", s_pendingSsid,
                  TFT_CYAN, false);
  } else if (s_step == Step::CONNECTED) {
    char ipLine[24];
    snprintf(ipLine, sizeof(ipLine), "IP %s", s_lastIp);
    char line1[48];
    if (s_ntpTried && Clock::valid()) {
      char hhmm[8];
      Clock::format(hhmm, sizeof(hhmm));
      snprintf(line1, sizeof(line1), "%s  -  HORA %s", s_savedSsid, hhmm);
    } else {
      snprintf(line1, sizeof(line1), "%s", s_savedSsid);
    }
    drawStatusBox("CONECTADO", line1, ipLine, green(), true);
  } else if (s_step == Step::FAIL) {
    drawStatusBox("ERROR WIFI", s_failText, s_pendingSsid, TFT_RED, false);
  }
}

void init() {
  // Do not touch WiFi.mode() at boot: calling WiFi.mode(WIFI_OFF) before WiFi
  // has ever been started initializes the ESP-NETIF / event loop in internal RAM.
  loadSavedFromNvs();
}

bool hasSavedNetwork() { return s_savedSsid[0] != '\0'; }

void getStatus(StatusInfo &out) {
  memset(&out, 0, sizeof(out));
  out.hasSaved = (s_savedSsid[0] != '\0');
  if (s_radioActive && WiFi.getMode() != WIFI_OFF &&
      WiFi.status() == WL_CONNECTED) {
    out.connected = true;
    strncpy(out.ssid, WiFi.SSID().c_str(), sizeof(out.ssid) - 1);
    IPAddress ip = WiFi.localIP();
    snprintf(out.ip, sizeof(out.ip), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    out.rssi = (int8_t)WiFi.RSSI();
    strncpy(s_lastIp, out.ip, sizeof(s_lastIp) - 1);
    s_lastRssi = out.rssi;
  } else {
    out.connected = false;
    strncpy(out.ssid, s_savedSsid, sizeof(out.ssid) - 1);
    strncpy(out.ip, s_lastIp, sizeof(out.ip) - 1);
    out.rssi = s_lastRssi;
  }
}

bool connectSaved(uint32_t timeoutMs) {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, true)) return false;
  String ssid = prefs.getString(KEY_SSID, "");
  String pass = prefs.getString(KEY_PASS, "");
  prefs.end();

  if (ssid.length() == 0) return false;

  s_radioActive = true;
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  beginSta(ssid.c_str(), pass.c_str());

  uint32_t startMs = millis();
  while (millis() - startMs < timeoutMs) {
    wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
      IPAddress ip = WiFi.localIP();
      snprintf(s_lastIp, sizeof(s_lastIp), "%u.%u.%u.%u", ip[0], ip[1], ip[2],
               ip[3]);
      s_lastRssi = (int8_t)WiFi.RSSI();
      strncpy(s_savedSsid, ssid.c_str(), sizeof(s_savedSsid) - 1);
      s_savedSsid[sizeof(s_savedSsid) - 1] = '\0';
      return true;
    }
    // Same rule as the portal: WL_CONNECT_FAILED can be left over from an
    // earlier attempt, and Arduino only sets it after its own retry.
    if (st == WL_CONNECT_FAILED && millis() - startMs >= WIFI_CONNECT_MIN_MS) {
      break;
    }
    delay(50);
  }

  stopRadio();
  return false;
}

void forgetNetwork() {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.remove(KEY_SSID);
    prefs.remove(KEY_PASS);
    prefs.end();
  }
  s_savedSsid[0] = '\0';
  s_lastIp[0] = '\0';
  s_lastRssi = 0;
  stopRadio();
  log_i("Saved WiFi credentials cleared");
}

void stopRadio() {
  if (s_step != Step::IDLE || !s_radioActive) return;
  if (WiFi.getMode() != WIFI_OFF) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  s_radioActive = false;
}

bool start() {
  stop();
  generateApCredentials();

  s_radioActive = true;
  s_result = Result::NONE;
  drawStatusBox("WIFI", "Buscando redes...", "", TFT_CYAN, false);

  // Scan before starting the AP, and put the AP on the channel of the
  // strongest network (most likely the one the user will pick). The ESP32 has
  // one radio, so when the STA joins a network on another channel the AP has
  // to follow it and the phone gets dropped mid-setup.
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks(false, false);
  int32_t apChannel = 1;
  int bestRssi = -1000;
  for (int i = 0; i < n; i++) {
    if (WiFi.RSSI(i) > bestRssi) {
      bestRssi = WiFi.RSSI(i);
      apChannel = WiFi.channel(i);
    }
  }

  WiFi.mode(WIFI_AP_STA);
  IPAddress apIp(192, 168, 4, 1);
  IPAddress netMask(255, 255, 255, 0);
  WiFi.softAPConfig(apIp, apIp, netMask);
  if (!WiFi.softAP(s_apSsid, s_apPass, apChannel, 0, 4)) {
    log_e("Failed to start SoftAP");
    WiFi.mode(WIFI_OFF);
    s_radioActive = false;
    return false;
  }

  s_dns = new DNSServer();
  s_dns->setErrorReplyCode(DNSReplyCode::NoError);
  s_dns->start(DNS_PORT, "*", apIp);

  s_web = new WebServer(80);
  s_web->on("/", HTTP_GET, handlePortalRoot);
  s_web->on("/connect", HTTP_POST, handlePortalConnect);
  s_web->on("/status", HTTP_GET, handlePortalStatus);
  // OS captive-portal probe endpoints
  s_web->on("/generate_204", HTTP_ANY, handleCaptiveRedirect);
  s_web->on("/gen_204", HTTP_ANY, handleCaptiveRedirect);
  s_web->on("/hotspot-detect.html", HTTP_ANY, handleCaptiveRedirect);
  s_web->on("/canonical.html", HTTP_ANY, handleCaptiveRedirect);
  s_web->on("/ncsi.txt", HTTP_ANY, handleCaptiveRedirect);
  s_web->on("/connecttest.txt", HTTP_ANY, handleCaptiveRedirect);
  s_web->on("/redirect", HTTP_ANY, handleCaptiveRedirect);
  s_web->on("/success.txt", HTTP_ANY, handleCaptiveRedirect);
  s_web->onNotFound(handleCaptiveRedirect);
  s_web->begin();

  s_step = Step::QR_AP;
  s_lastStations = 0;
  s_lastActivityMs = millis();
  s_stepStartMs = s_lastActivityMs;
  s_dirty = true;
  log_i("WiFi setup AP started: SSID=%s PASS=%s", s_apSsid, s_apPass);
  return true;
}

void stop() {
  stopPortalServers();
  if (s_radioActive) {
    WiFi.scanDelete();
    if (WiFi.getMode() != WIFI_OFF) {
      WiFi.softAPdisconnect(true);
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
    }
    s_radioActive = false;
  }
  s_step = Step::IDLE;
  s_dirty = false;
}

void markDirty() {
  if (s_step != Step::IDLE) {
    s_dirty = true;
  }
}

bool tick(bool toggleQr, bool cancel) {
  if (s_step == Step::IDLE) return true;
  if (cancel) {
    stop();
    return true;
  }

  if (s_dns) s_dns->processNextRequest();
  if (s_web) s_web->handleClient();
  // Read the clock after the handlers: they set s_stepStartMs and
  // s_lastActivityMs to millis(), and a `now` taken before them makes
  // `now - start` wrap around to ~4e9, so every timeout fired at once
  // (the connect test failed instantly, a page load closed the AP).
  uint32_t now = millis();

  if (s_step == Step::QR_AP || s_step == Step::QR_URL) {
    uint8_t stations = WiFi.softAPgetStationNum();
    if (stations > s_lastStations && s_step == Step::QR_AP) {
      // Phone just joined the AP: switch to QR 2 (http://192.168.4.1) in case
      // the OS captive portal sheet does not pop up on its own.
      s_step = Step::QR_URL;
      s_lastActivityMs = now;
      s_dirty = true;
    }
    s_lastStations = stations;
    if (stations > 0) s_lastActivityMs = now;  // Someone is on the portal

    if (toggleQr) {
      s_step = (s_step == Step::QR_AP) ? Step::QR_URL : Step::QR_AP;
      s_lastActivityMs = now;
      s_dirty = true;
    }

    if (now - s_lastActivityMs >= WIFI_SETUP_TIMEOUT_MS) {
      log_i("WiFi setup timed out (no activity)");
      stop();
      return true;
    }
  } else if (s_step == Step::CONNECTING) {
    wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
      IPAddress ip = WiFi.localIP();
      snprintf(s_lastIp, sizeof(s_lastIp), "%u.%u.%u.%u", ip[0], ip[1], ip[2],
               ip[3]);
      s_lastRssi = (int8_t)WiFi.RSSI();
      saveCredentialsToNvs(s_pendingSsid, s_pendingPass);
      s_result = Result::OK;

      // F4.1 step 6: the AP is turned off by stop() once the success screen
      // ends, so the phone's status poll can show the result first.
      s_step = Step::CONNECTED;
      s_stepStartMs = now;
      s_ntpTried = false;
      s_dirty = true;
    } else if (now - s_stepStartMs >= WIFI_CONNECT_TIMEOUT_MS ||
               (st == WL_CONNECT_FAILED &&
                now - s_stepStartMs >= WIFI_CONNECT_MIN_MS)) {
      // Arduino sets WL_CONNECT_FAILED (wrong password) only after its own
      // retry has failed too; anything else keeps retrying until the timeout.
      describeFailure();
      WiFi.disconnect(false);
      s_result = Result::FAIL;
      s_step = Step::FAIL;
      s_stepStartMs = now;
      s_lastActivityMs = now;
      s_dirty = true;
    }
  } else if (s_step == Step::CONNECTED) {
    if (!s_ntpTried) {
      // F5.1: the "CONECTADO" screen is already up; set the clock while the
      // STA is connected, then show the time and restart the display time.
      s_ntpTried = true;
      Clock::syncNtp();
      s_stepStartMs = millis();
      s_dirty = true;
    } else if (toggleQr || (now - s_stepStartMs >= WIFI_SUCCESS_DISPLAY_MS)) {
      stop();
      return true;
    }
  } else if (s_step == Step::FAIL) {
    if (toggleQr || (now - s_stepStartMs >= WIFI_FAIL_DISPLAY_MS)) {
      s_step = Step::QR_URL;
      s_lastActivityMs = now;
      s_dirty = true;
    }
  }

  if (s_dirty) {
    draw();
    s_dirty = false;
  }
  return false;
}

bool consumeSavedFlag() {
  if (!s_savedFlag) return false;
  s_savedFlag = false;
  return true;
}

}  // namespace WifiSetup

#endif  // WAVESHARE_154

