#include "usb_msc.h"

#ifdef WAVESHARE_154

#include <USB.h>
#include <USBMSC.h>
#include <atomic>
#include <cstring>
#include <driver/sdmmc_host.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <sdmmc_cmd.h>
#include <tusb.h>

namespace UsbMsc {

// Constructed before app_main() so TinyUSB registers the MSC interface
// alongside CDC when USB.begin() runs at boot.
static USBMSC s_msc;

static SemaphoreHandle_t s_ioMutex = NULL;
static sdmmc_card_t s_card;
static std::atomic<bool> s_cardReady{false};
static std::atomic<bool> s_active{false};
static std::atomic<bool> s_ejected{false};
static std::atomic<bool> s_wasEjected{false};
static std::atomic<bool> s_mounted{false};
static std::atomic<bool> s_suspended{false};
static std::atomic<uint32_t> s_suspendAtMs{0};
static std::atomic<bool> s_promptPending{false};
static std::atomic<bool> s_suppressNextPrompt{false};
static std::atomic<uint32_t> s_readCount{0};
static std::atomic<uint32_t> s_lastIoMs{0};
static uint32_t s_enterMs = 0;
static bool s_seenHost = false;
static bool s_reattachTried = false;

static int32_t onRead(uint32_t lba, uint32_t offset, void *buffer,
                      uint32_t bufsize) {
  if (!s_ioMutex ||
      xSemaphoreTake(s_ioMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
    return -1;
  }
  int32_t ret = -1;
  if (s_cardReady && s_card.csd.sector_size > 0) {
    uint32_t secSize = (uint32_t)s_card.csd.sector_size;
    if (offset == 0 && (bufsize % secSize) == 0 &&
        lba + (bufsize / secSize) <= (uint32_t)s_card.csd.capacity) {
      size_t count = bufsize / secSize;
      if (sdmmc_read_sectors(&s_card, buffer, lba, count) == ESP_OK) {
        s_readCount++;
        s_lastIoMs = millis();
        ret = (int32_t)bufsize;
      }
    }
  }
  xSemaphoreGive(s_ioMutex);
  return ret;
}

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer,
                       uint32_t bufsize) {
  if (!s_ioMutex ||
      xSemaphoreTake(s_ioMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
    return -1;
  }
  int32_t ret = -1;
  if (s_cardReady && s_card.csd.sector_size > 0) {
    uint32_t secSize = (uint32_t)s_card.csd.sector_size;
    if (offset == 0 && (bufsize % secSize) == 0 &&
        lba + (bufsize / secSize) <= (uint32_t)s_card.csd.capacity) {
      size_t count = bufsize / secSize;
      if (sdmmc_write_sectors(&s_card, buffer, lba, count) == ESP_OK) {
        s_lastIoMs = millis();
        ret = (int32_t)bufsize;
      }
    }
  }
  xSemaphoreGive(s_ioMutex);
  return ret;
}

static bool onStartStop(uint8_t power_condition, bool start, bool load_eject) {
  (void)power_condition;
  if (load_eject && !start) {
    // Host ejected the drive: hide the medium right away so the next
    // TEST_UNIT_READY sees "Medium Not Present", and notify loop().
    s_msc.mediaPresent(false);
    s_wasEjected = true;
    s_ejected = true;
  }
  return true;
}

// Configure inquiry strings and callbacks before app_main() calls USB.begin().
struct EarlyInit {
  EarlyInit() {
    s_msc.vendorID("CoreTV");
    s_msc.productID("SD Card");
    s_msc.productRevision("1.0");
    s_msc.onRead(onRead);
    s_msc.onWrite(onWrite);
    s_msc.onStartStop(onStartStop);
    s_msc.mediaPresent(false);
  }
} s_earlyInit;

static void usbEventCallback(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data) {
  (void)arg;
  (void)event_data;
  if (event_base != ARDUINO_USB_EVENTS) return;

  switch (event_id) {
    case ARDUINO_USB_STARTED_EVENT:
      s_mounted = true;
      s_suspended = false;
      if (!s_active && !s_suppressNextPrompt) {
        s_promptPending = true;
      }
      s_suppressNextPrompt = false;
      break;
    case ARDUINO_USB_STOPPED_EVENT:
      s_mounted = false;
      s_suspended = false;
      s_wasEjected = false;
      break;
    case ARDUINO_USB_SUSPEND_EVENT:
      s_suspended = true;
      s_suspendAtMs = millis();
      break;
    case ARDUINO_USB_RESUME_EVENT:
      s_suspended = false;
      break;
    default:
      break;
  }
}

void init() {
  if (!s_ioMutex) {
    s_ioMutex = xSemaphoreCreateMutex();
  }
  USB.onEvent(usbEventCallback);
  // USB.begin() runs in app_main() before setup(), so the host may have
  // enumerated the board during the boot animation.
  if (tud_mounted() && !tud_suspended()) {
    s_mounted = true;
    s_suspended = false;
    s_promptPending = true;
  }
}

bool pcConnected() {
  if (!s_mounted) return false;
  if (!s_suspended) return true;
  return (millis() - s_suspendAtMs) < USB_DISCONNECT_MS;
}

bool consumePromptRequest() {
  if (!s_promptPending) return false;
  s_promptPending = false;
  return pcConnected();
}

bool enter() {
  if (s_active) return true;
  if (!s_ioMutex) return false;

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.flags = SDMMC_HOST_FLAG_4BIT;
  host.slot = SDMMC_HOST_SLOT_1;
  host.max_freq_khz = BOARD_MAX_SDMMC_FREQ;

  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.clk = (gpio_num_t)SD_CLK;
  slot.cmd = (gpio_num_t)SD_CMD;
  slot.d0 = (gpio_num_t)SD_D0;
  slot.d1 = (gpio_num_t)SD_D1;
  slot.d2 = (gpio_num_t)SD_D2;
  slot.d3 = (gpio_num_t)SD_D3;
  slot.width = 4;

  esp_err_t err = sdmmc_host_init();
  if (err != ESP_OK) {
    log_e("sdmmc_host_init failed: 0x%x", err);
    return false;
  }
  err = sdmmc_host_init_slot(SDMMC_HOST_SLOT_1, &slot);
  if (err != ESP_OK) {
    log_e("sdmmc_host_init_slot failed: 0x%x", err);
    sdmmc_host_deinit();
    return false;
  }
  memset(&s_card, 0, sizeof(s_card));
  err = sdmmc_card_init(&host, &s_card);
  if (err != ESP_OK) {
    log_e("sdmmc_card_init failed: 0x%x", err);
    sdmmc_host_deinit();
    return false;
  }

  xSemaphoreTake(s_ioMutex, portMAX_DELAY);
  s_cardReady = true;
  s_ejected = false;
  s_active = true;
  s_readCount = 0;
  s_reattachTried = false;
  s_enterMs = millis();
  s_lastIoMs = millis();
  s_seenHost = pcConnected();
  s_msc.begin((uint32_t)s_card.csd.capacity, (uint16_t)s_card.csd.sector_size);
  s_msc.mediaPresent(true);
  xSemaphoreGive(s_ioMutex);

  // If the drive was already ejected earlier during this USB connection,
  // re-attach on the bus so the OS re-probes the LUN immediately.
  if (s_wasEjected && s_mounted) {
    s_wasEjected = false;
    s_reattachTried = true;
    s_suppressNextPrompt = true;
    s_seenHost = false;
    tud_disconnect();
    delay(250);
    tud_connect();
    s_enterMs = millis();
  }

  log_i("USB MSC active (%lu MB)", (unsigned long)cardSizeMB());
  return true;
}

void leave() {
  if (!s_active) return;

  xSemaphoreTake(s_ioMutex, portMAX_DELAY);
  s_msc.mediaPresent(false);
  if (s_cardReady) {
    s_cardReady = false;
    sdmmc_host_deinit();
  }
  s_active = false;
  s_ejected = false;
  xSemaphoreGive(s_ioMutex);

  log_i("USB MSC stopped");
}

bool tick() {
  if (!s_active) return true;
  if (s_ejected) return true;

  uint32_t now = millis();
  if (pcConnected()) {
    s_seenHost = true;
    // Some hosts (e.g. macOS) stop polling an empty removable LUN after boot.
    // If no sector has been read 2.5 s after exposing the medium, re-attach
    // once so the host enumerates with mediaPresent already true.
    if (!s_reattachTried && s_readCount == 0 && (now - s_enterMs >= 2500)) {
      s_reattachTried = true;
      s_suppressNextPrompt = true;
      s_seenHost = false;
      tud_disconnect();
      delay(250);
      tud_connect();
      s_enterMs = millis();
      return false;
    }
  } else if (s_seenHost && (now - s_enterMs >= USB_DISCONNECT_MS)) {
    return true;
  }
  return false;
}

bool isIdle() {
  return (millis() - s_lastIoMs) >= 1000;
}

uint32_t cardSizeMB() {
  if (!s_cardReady) return 0;
  return (uint32_t)(((uint64_t)s_card.csd.capacity * s_card.csd.sector_size) /
                    (1024ULL * 1024ULL));
}

}  // namespace UsbMsc

#endif  // WAVESHARE_154

