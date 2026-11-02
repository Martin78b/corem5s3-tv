#include "web_upload.h"

#ifdef WAVESHARE_154

#include "display_hal.h"
#include "wifi_setup.h"
#include <WebServer.h>
#include <WiFi.h>

namespace WebUpload {

// Written first, renamed when complete: a cut upload never replaces a good
// episode, and the name does not end in .mjpeg so scanEpisodes() skips it.
static const char *TMP_PATH = "/.upload.tmp";
static const uint32_t WIFI_LOST_MS = 10000;  // STA down this long -> exit
static const uint32_t STATUS_REDRAW_MS = 400;

static WebServer *s_web = nullptr;
static SemaphoreHandle_t s_sdMutex = nullptr;
static char s_url[32] = "";
static bool s_dirty = false;
static bool s_changed = false;
static bool s_doneRequested = false;
static uint32_t s_lastRequestMs = 0;
static uint32_t s_wifiLostMs = 0;

// Current upload
static File s_file;
static bool s_active = false;
static uint8_t *s_buf = nullptr;
static size_t s_bufLen = 0;
static uint32_t s_received = 0;
static uint32_t s_expected = 0;  // Request Content-Length (approx. file size)
static uint32_t s_lastStatusMs = 0;
static char s_target[EPISODE_PATH_MAX] = "";
static char s_error[48] = "";
static char s_status[48] = "Esperando archivos...";
static uint16_t s_statusColor = TFT_WHITE;

static uint16_t dimGreen() { return Display.color565(0, 120, 40); }

static void lockSd() { xSemaphoreTake(s_sdMutex, portMAX_DELAY); }
static void unlockSd() { xSemaphoreGive(s_sdMutex); }

// ── Screen ──

static void drawStatus() {
  Display.fillRect(10, 204, DISPLAY_WIDTH - 20, 12, TFT_BLACK);
  Display.setTextDatum(TC_DATUM);
  Display.setTextSize(1);
  Display.setTextColor(s_statusColor, TFT_BLACK);
  Display.drawString(s_status, DISPLAY_WIDTH / 2, 206);
  Display.setTextDatum(TL_DATUM);
}

static void setStatus(uint16_t color, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(s_status, sizeof(s_status), fmt, args);
  va_end(args);
  s_statusColor = color;
  drawStatus();
}

static void drawScreen() {
  WifiSetup::drawQrScreen("SUBIR EPISODIOS", s_url, "Abre en el navegador:",
                          s_url, TFT_CYAN, "PWR:terminar");
  drawStatus();
}

// ── Helpers ──

static bool hasExt(const String &name, const char *ext) {
  size_t n = name.length(), e = strlen(ext);
  return n > e && strcasecmp(name.c_str() + n - e, ext) == 0;
}

static bool allowedExt(const String &name) {
  return hasExt(name, ".mjpeg") || hasExt(name, ".pcm");
}

// Keeps the base name only and rejects anything that is not a plain
// .mjpeg / .pcm file name that scanEpisodes() can handle.
static bool sanitizeName(const String &raw, String &out) {
  String n = raw;
  int slash = max(n.lastIndexOf('/'), n.lastIndexOf('\\'));
  if (slash >= 0) n = n.substring(slash + 1);
  if (n.length() == 0 || n[0] == '.') return false;  // Also macOS "._x"
  for (size_t i = 0; i < n.length(); i++) {
    char c = n[i];
    if ((uint8_t)c < 0x20 || strchr("\\/:*?\"<>|", c)) return false;
  }
  if (!allowedExt(n)) return false;
  if (n.length() + 2 > EPISODE_PATH_MAX) return false;  // "/" + name + NUL
  out = n;
  return true;
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

static String formatMB(uint64_t bytes) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
  return String(buf);
}

static uint64_t freeBytes() {
  lockSd();
  uint64_t total = SD_FS.totalBytes();
  uint64_t used = SD_FS.usedBytes();
  unlockSd();
  return total > used ? total - used : 0;
}

// ── Upload ──

static bool flushBuffer() {
  if (s_bufLen == 0) return true;
  lockSd();
  size_t written = s_file.write(s_buf, s_bufLen);
  unlockSd();
  bool ok = (written == s_bufLen);
  s_bufLen = 0;
  return ok;
}

static void discardFile() {
  lockSd();
  if (s_file) s_file.close();
  SD_FS.remove(TMP_PATH);
  unlockSd();
  s_active = false;
}

static void fail(const char *msg) {
  snprintf(s_error, sizeof(s_error), "%s", msg);
  if (s_active) discardFile();
  setStatus(TFT_RED, "%s", msg);
  log_w("Upload failed: %s", msg);
}

static void beginFile(const String &rawName) {
  s_error[0] = '\0';
  s_active = false;
  s_bufLen = 0;
  s_received = 0;

  String name;
  if (!sanitizeName(rawName, name)) {
    fail("Solo archivos .mjpeg o .pcm");
    return;
  }
  snprintf(s_target, sizeof(s_target), "/%s", name.c_str());

  int cl = s_web->clientContentLength();
  s_expected = cl > 0 ? (uint32_t)cl : 0;
  if (s_expected && s_expected > freeBytes()) {
    fail("No hay espacio en la SD");
    return;
  }

  lockSd();
  SD_FS.remove(TMP_PATH);  // Leftover from a cut upload
  s_file = SD_FS.open(TMP_PATH, FILE_WRITE);
  unlockSd();
  if (!s_file) {
    fail("No se pudo crear el archivo");
    return;
  }
  s_active = true;
  s_lastStatusMs = 0;
  setStatus(TFT_CYAN, "Recibiendo %s", name.c_str());
  log_i("Upload start: %s (%u bytes)", s_target, (unsigned)s_expected);
}

static void appendData(const uint8_t *data, size_t len) {
  if (!s_active) return;
  while (len > 0) {
    size_t n = min(len, (size_t)UPLOAD_WRITE_BUF - s_bufLen);
    memcpy(s_buf + s_bufLen, data, n);
    s_bufLen += n;
    data += n;
    len -= n;
    s_received += n;
    if (s_bufLen == UPLOAD_WRITE_BUF && !flushBuffer()) {
      fail("Error al escribir la SD");
      return;
    }
  }

  // The whole request runs inside handleClient(): show progress from here
  uint32_t now = millis();
  if (now - s_lastStatusMs >= STATUS_REDRAW_MS) {
    s_lastStatusMs = now;
    s_lastRequestMs = now;
    if (s_expected) {
      unsigned pct = (unsigned)min<uint64_t>(99, (uint64_t)s_received * 100 / s_expected);
      setStatus(TFT_CYAN, "%s  %u%%", formatMB(s_received).c_str(), pct);
    } else {
      setStatus(TFT_CYAN, "%s", formatMB(s_received).c_str());
    }
  }
}

static void finishFile() {
  if (!s_active) return;
  if (!flushBuffer()) {
    fail("Error al escribir la SD");
    return;
  }
  lockSd();
  s_file.close();
  if (SD_FS.exists(s_target)) SD_FS.remove(s_target);  // Replace
  bool ok = SD_FS.rename(TMP_PATH, s_target);
  unlockSd();
  s_active = false;
  if (!ok) {
    fail("No se pudo guardar el archivo");
    return;
  }
  s_changed = true;
  setStatus(TFT_GREEN, "Guardado: %s", s_target + 1);
  log_i("Upload done: %s (%u bytes)", s_target, (unsigned)s_received);
}

static void handleUpload() {
  HTTPUpload &up = s_web->upload();
  s_lastRequestMs = millis();
  switch (up.status) {
    case UPLOAD_FILE_START:
      beginFile(up.filename);
      break;
    case UPLOAD_FILE_WRITE:
      appendData(up.buf, up.currentSize);
      break;
    case UPLOAD_FILE_END:
      finishFile();
      break;
    case UPLOAD_FILE_ABORTED:
      if (s_active) discardFile();
      setStatus(TFT_YELLOW, "Subida cancelada");
      break;
  }
}

static void handleUploadDone() {
  if (s_error[0]) {
    s_web->send(500, "text/plain; charset=utf-8", s_error);
  } else {
    s_web->send(200, "text/plain", "OK");
  }
}

// ── Pages ──

static void handleRoot() {
  s_lastRequestMs = millis();

  String rows;
  int count = 0;
  lockSd();
  File root = SD_FS.open("/");
  if (root) {
    while (true) {
      File e = root.openNextFile();
      if (!e) break;
      String name = e.name();
      int slash = name.lastIndexOf('/');
      if (slash >= 0) name = name.substring(slash + 1);
      if (!e.isDirectory() && name.length() && name[0] != '.' &&
          allowedExt(name)) {
        String esc = htmlEscape(name);
        rows += "<tr><td>" + esc + "</td><td class='r'>" + formatMB(e.size()) +
                "</td><td><button class='del' data-n=\"" + esc +
                "\" onclick='del(this.dataset.n)'>X</button></td></tr>";
        count++;
      }
      e.close();
    }
    root.close();
  }
  unlockSd();
  String freeText = formatMB(freeBytes());

  String page;
  page.reserve(3200 + rows.length());
  page += "<!DOCTYPE html><html lang='es'><head><meta charset='utf-8'>"
          "<meta name='viewport' content='width=device-width,initial-scale=1'>"
          "<title>CoreTV - Episodios</title><style>"
          "body{background:#080c08;color:#00ff55;font-family:monospace,sans-serif;"
          "margin:0;padding:16px;display:flex;justify-content:center}"
          ".box{width:100%;max-width:520px;border:2px solid #00ff55;padding:16px;"
          "box-sizing:border-box;background:#000}"
          "h1{font-size:20px;margin:0 0 12px;text-align:center;"
          "border-bottom:1px solid #007828;padding-bottom:8px}"
          "p{font-size:13px;color:#88ffaa}"
          "table{width:100%;border-collapse:collapse;font-size:13px}"
          "td{padding:4px;border-bottom:1px solid #003814;word-break:break-all}"
          ".r{text-align:right;white-space:nowrap}"
          "input,button{font-family:inherit;font-size:15px}"
          "button{width:100%;padding:10px;margin-top:10px;background:#00ff55;"
          "color:#000;font-weight:bold;border:0;cursor:pointer}"
          ".del{width:auto;margin:0;padding:2px 8px;background:#000;color:#ff5555;"
          "border:1px solid #ff5555}"
          ".sec{background:#000;color:#00ff55;border:1px solid #007828}"
          "progress{width:100%;height:16px;margin-top:10px}"
          "</style></head><body><div class='box'><h1>CORETV EPISODIOS</h1>"
          "<p>Sube el <b>.mjpeg</b> y su <b>.pcm</b> (mismo nombre) generados "
          "con convert_episodes.py. Un archivo con el mismo nombre se reemplaza.</p>"
          "<input id='f' type='file' multiple accept='.mjpeg,.pcm'>"
          "<button onclick='up()'>SUBIR</button>"
          "<progress id='pb' value='0' max='1'></progress>"
          "<p id='st'>Libre en la SD: ";
  page += freeText;
  page += "</p><table>";
  page += rows.length() ? rows
                        : String("<tr><td>No hay episodios en la SD</td></tr>");
  page += "</table><p>";
  page += String(count);
  page += " archivo(s)</p>"
          "<button class='sec' onclick='done()'>TERMINAR Y VOLVER A LA TV</button>"
          "</div><script>"
          "var q=[],st=document.getElementById('st'),pb=document.getElementById('pb');"
          "function up(){var f=document.getElementById('f').files;"
          "if(!f.length){st.textContent='Elige uno o mas archivos';return;}"
          "q=Array.prototype.slice.call(f);next();}"
          "function next(){if(!q.length){st.textContent='Listo';"
          "setTimeout(function(){location.reload();},800);return;}"
          "var file=q.shift(),fd=new FormData(),x=new XMLHttpRequest();"
          "fd.append('file',file,file.name);x.open('POST','/upload');"
          "x.upload.onprogress=function(e){if(e.lengthComputable){"
          "pb.value=e.loaded/e.total;st.textContent=file.name+': '+"
          "Math.round(e.loaded*100/e.total)+'%';}};"
          "x.onload=function(){if(x.status==200){next();}else{"
          "st.textContent='Error en '+file.name+': '+x.responseText;q=[];}};"
          "x.onerror=function(){st.textContent='Error de red en '+file.name;q=[];};"
          "x.send(fd);}"
          "function del(n){if(!confirm('Borrar '+n+'?'))return;"
          "var x=new XMLHttpRequest();x.open('POST','/delete');"
          "x.setRequestHeader('Content-Type','application/x-www-form-urlencoded');"
          "x.onload=function(){location.reload();};"
          "x.send('name='+encodeURIComponent(n));}"
          "function done(){fetch('/done',{method:'POST'}).then(function(){"
          "document.querySelector('.box').innerHTML='<h1>LISTO</h1>"
          "<p>El televisor vuelve a reproducir.</p>';});}"
          "</script></body></html>";
  s_web->send(200, "text/html; charset=utf-8", page);
}

static void handleDelete() {
  s_lastRequestMs = millis();
  String name;
  if (!sanitizeName(s_web->arg("name"), name)) {
    s_web->send(400, "text/plain", "Nombre no valido");
    return;
  }
  String path = "/" + name;
  lockSd();
  bool ok = SD_FS.exists(path) && SD_FS.remove(path);
  unlockSd();
  if (ok) {
    s_changed = true;
    setStatus(TFT_YELLOW, "Borrado: %s", name.c_str());
  }
  s_web->send(ok ? 200 : 404, "text/plain", ok ? "OK" : "No existe");
}

static void handleDone() {
  s_doneRequested = true;
  s_web->send(200, "text/plain", "OK");
}

static void handleNotFound() {
  s_web->sendHeader("Location", "/", true);
  s_web->send(302, "text/plain", "");
}

// ── Public API ──

bool start(SemaphoreHandle_t sdMutex) {
  stop();
  if (WiFi.status() != WL_CONNECTED) return false;
  s_sdMutex = sdMutex;
  s_buf = (uint8_t *)malloc(UPLOAD_WRITE_BUF);
  if (!s_buf) return false;

  IPAddress ip = WiFi.localIP();
  snprintf(s_url, sizeof(s_url), "http://%u.%u.%u.%u/", ip[0], ip[1], ip[2],
           ip[3]);
  WiFi.setSleep(false);  // Modem sleep halves the upload speed

  s_web = new WebServer(80);
  s_web->on("/", HTTP_GET, handleRoot);
  s_web->on("/upload", HTTP_POST, handleUploadDone, handleUpload);
  s_web->on("/delete", HTTP_POST, handleDelete);
  s_web->on("/done", HTTP_POST, handleDone);
  s_web->onNotFound(handleNotFound);
  s_web->begin();

  s_changed = false;
  s_doneRequested = false;
  s_active = false;
  s_wifiLostMs = 0;
  s_lastRequestMs = millis();
  s_statusColor = TFT_WHITE;
  snprintf(s_status, sizeof(s_status), "Esperando archivos...");
  drawScreen();
  log_i("Upload server at %s", s_url);
  return true;
}

void stop() {
  if (s_active) discardFile();
  if (s_web) {
    s_web->stop();
    delete s_web;
    s_web = nullptr;
  }
  free(s_buf);
  s_buf = nullptr;
  s_bufLen = 0;
}

void markDirty() { s_dirty = true; }

bool tick(bool cancel) {
  if (!s_web || cancel) return true;
  s_web->handleClient();
  uint32_t now = millis();  // After the handlers, which update the times

  if (s_doneRequested) return true;
  if (WiFi.status() != WL_CONNECTED) {
    if (!s_wifiLostMs) s_wifiLostMs = now ? now : 1;
    if (now - s_wifiLostMs >= WIFI_LOST_MS) {
      log_w("Upload mode: WiFi lost");
      return true;
    }
  } else {
    s_wifiLostMs = 0;
  }
  if (now - s_lastRequestMs >= UPLOAD_IDLE_TIMEOUT_MS) {
    log_i("Upload mode: idle timeout");
    return true;
  }
  if (s_dirty) {
    s_dirty = false;
    drawScreen();
  }
  return false;
}

bool filesChanged() { return s_changed; }

}  // namespace WebUpload

#endif  // WAVESHARE_154
