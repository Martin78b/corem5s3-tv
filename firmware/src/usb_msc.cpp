#include "usb_msc.h"

#ifdef WAVESHARE_154

#include <SD_MMC.h>
#include <USB.h>
#include <USBMSC.h>
#include <atomic>
#include <cstring>
#include <driver/sdmmc_host.h>
#include <esp_heap_caps.h>
#include <ff.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <sdmmc_cmd.h>
#include <tusb.h>

namespace UsbMsc {

// Accessor for SD_MMC's protected _card pointer so we can perform raw sector
// I/O without deinitializing the SDMMC host controller or resetting the card
// between TV playback and USB MSC mode.
struct SdMmcAccessor : public fs::SDMMCFS {
  static sdmmc_card_t *card() {
    return static_cast<SdMmcAccessor &>(SD_MMC)._card;
  }
};

// Constructed before app_main() so TinyUSB registers the MSC interface
// alongside CDC when USB.begin() runs at boot.
static USBMSC s_msc;

static SemaphoreHandle_t s_ioMutex = NULL;
static sdmmc_card_t *s_card = nullptr;
static FATFS *s_fatfs = nullptr;
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

// Ping-pong DMA staging buffers (allocated in internal DMA RAM only while
// USB MSC mode is active). TinyUSB's MSC buffer is fixed at 4 KB (8 sectors)
// in the precompiled Arduino SDK; coalescing contiguous 4 KB chunks into
// 32-64 KB multi-block SDMMC DMA writes and overlapping SD card programming
// with the next USB transfer eliminates per-4KB CMD12/busy stalls.
static constexpr uint32_t IDLE_FLUSH_MS = 60;
static uint8_t *s_dmaBuf[2] = {nullptr, nullptr};
static size_t s_bufSectors = 0;
static uint8_t s_activeBuf = 0;
static uint32_t s_stagingLba = 0;
static size_t s_stagingCount = 0;

// Read-ahead state (reuses s_dmaBuf[0] when no write is pending).
static uint32_t s_readCacheLba = 0;
static size_t s_readCacheCount = 0;
static uint32_t s_lastReadEndLba = UINT32_MAX;

// Background SDMMC writer task state.
static TaskHandle_t s_writerTask = nullptr;
static SemaphoreHandle_t s_bgStartSem = nullptr;
static SemaphoreHandle_t s_bgDoneSem = nullptr;
static uint8_t *s_bgBuf = nullptr;
static uint32_t s_bgLba = 0;
static size_t s_bgCount = 0;
static bool s_bgInFlight = false;
static std::atomic<bool> s_bgOk{true};
static std::atomic<bool> s_hasPendingWrite{false};

static void writerTaskFn(void *arg) {
  (void)arg;
  for (;;) {
    if (xSemaphoreTake(s_bgStartSem, portMAX_DELAY) == pdTRUE) {
      esp_err_t err = ESP_FAIL;
      if (s_cardReady && s_card && s_bgBuf && s_bgCount > 0) {
        err = sdmmc_write_sectors(s_card, s_bgBuf, s_bgLba, s_bgCount);
      }
      if (err != ESP_OK) {
        log_e("async sdmmc_write_sectors(lba=%lu, cnt=%u) failed: 0x%x",
              (unsigned long)s_bgLba, (unsigned)s_bgCount, err);
        s_bgOk = false;
      }
      s_lastIoMs = millis();
      xSemaphoreGive(s_bgDoneSem);
    }
  }
}

// Must be called with s_ioMutex held.
static bool waitBgFlushLocked() {
  if (s_bgInFlight) {
    xSemaphoreTake(s_bgDoneSem, portMAX_DELAY);
    s_bgInFlight = false;
  }
  bool ok = s_bgOk.load();
  s_bgOk = true;
  return ok;
}

// Hands the currently staged sectors in s_dmaBuf[s_activeBuf] to the
// background writer task and switches s_activeBuf to the other buffer.
// Must be called with s_ioMutex held.
static bool queueActiveBufferLocked() {
  if (s_stagingCount == 0) return true;
  if (!waitBgFlushLocked()) {
    s_stagingCount = 0;
    return false;
  }
  s_bgBuf = s_dmaBuf[s_activeBuf];
  s_bgLba = s_stagingLba;
  s_bgCount = s_stagingCount;
  s_bgInFlight = true;
  s_activeBuf ^= 1;
  s_stagingCount = 0;
  xSemaphoreGive(s_bgStartSem);
  return true;
}

// Flushes both the active staging buffer and any in-flight background write
// to the SD card. Must be called with s_ioMutex held.
static bool flushAllWritesLocked() {
  bool ok = true;
  if (s_stagingCount > 0) {
    if (!queueActiveBufferLocked()) ok = false;
  }
  if (!waitBgFlushLocked()) ok = false;
  s_hasPendingWrite = false;
  return ok;
}

static void allocateStagingBuffers(uint32_t sectorSize) {
  static const size_t kCandidateSectors[] = {128, 64, 32};  // 64 KB, 32 KB, 16 KB
  s_dmaBuf[0] = nullptr;
  s_dmaBuf[1] = nullptr;
  s_bufSectors = 0;

  for (size_t sectors : kCandidateSectors) {
    size_t bytes = sectors * sectorSize;
    uint8_t *b0 = (uint8_t *)heap_caps_malloc(
        bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    uint8_t *b1 = (uint8_t *)heap_caps_malloc(
        bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (b0 && b1) {
      s_dmaBuf[0] = b0;
      s_dmaBuf[1] = b1;
      s_bufSectors = sectors;
      log_i("USB MSC allocated 2x %u KB DMA staging buffers",
            (unsigned)(bytes / 1024));
      return;
    }
    if (b0) heap_caps_free(b0);
    if (b1) heap_caps_free(b1);
  }
  log_w("USB MSC could not allocate DMA staging buffers; using direct I/O");
}

static void freeStagingBuffers() {
  for (int i = 0; i < 2; ++i) {
    if (s_dmaBuf[i]) {
      heap_caps_free(s_dmaBuf[i]);
      s_dmaBuf[i] = nullptr;
    }
  }
  s_bufSectors = 0;
  s_activeBuf = 0;
  s_stagingLba = 0;
  s_stagingCount = 0;
  s_readCacheLba = 0;
  s_readCacheCount = 0;
  s_lastReadEndLba = UINT32_MAX;
}

static int32_t onRead(uint32_t lba, uint32_t offset, void *buffer,
                      uint32_t bufsize) {
  if (!s_ioMutex ||
      xSemaphoreTake(s_ioMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
    return -1;
  }
  int32_t ret = -1;
  if (s_cardReady && s_card && s_card->csd.sector_size > 0) {
    uint32_t secSize = (uint32_t)s_card->csd.sector_size;
    if (offset == 0 && (bufsize % secSize) == 0 &&
        lba + (bufsize / secSize) <= (uint32_t)s_card->csd.capacity) {
      size_t count = bufsize / secSize;
      if (flushAllWritesLocked()) {
        if (s_dmaBuf[0] && count <= s_bufSectors) {
          // Cache miss: fill s_dmaBuf[0]. Use full read-ahead on sequential
          // reads, and exact-size DMA read on random probes.
          if (s_readCacheCount == 0 || lba < s_readCacheLba ||
              lba + count > s_readCacheLba + s_readCacheCount) {
            size_t toRead = count;
            if (lba == s_lastReadEndLba) {
              size_t maxAvail = (size_t)s_card->csd.capacity - lba;
              toRead = s_bufSectors < maxAvail ? s_bufSectors : maxAvail;
              if (toRead < count) toRead = count;
            }
            if (sdmmc_read_sectors(s_card, s_dmaBuf[0], lba, toRead) ==
                ESP_OK) {
              s_readCacheLba = lba;
              s_readCacheCount = toRead;
            } else {
              s_readCacheCount = 0;
            }
          }
          if (s_readCacheCount > 0 && lba >= s_readCacheLba &&
              lba + count <= s_readCacheLba + s_readCacheCount) {
            memcpy(buffer,
                   s_dmaBuf[0] + (lba - s_readCacheLba) * secSize,
                   bufsize);
            s_lastReadEndLba = lba + count;
            s_readCount++;
            s_lastIoMs = millis();
            ret = (int32_t)bufsize;
          }
        } else if (sdmmc_read_sectors(s_card, buffer, lba, count) == ESP_OK) {
          s_lastReadEndLba = lba + count;
          s_readCount++;
          s_lastIoMs = millis();
          ret = (int32_t)bufsize;
        }
      }
    }
  }
  xSemaphoreGive(s_ioMutex);
  return ret;
}

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer,
                       uint32_t bufsize) {
  if (!s_ioMutex ||
      xSemaphoreTake(s_ioMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
    return -1;
  }
  int32_t ret = -1;
  if (s_cardReady && s_card && s_card->csd.sector_size > 0) {
    uint32_t secSize = (uint32_t)s_card->csd.sector_size;
    if (offset == 0 && (bufsize % secSize) == 0 &&
        lba + (bufsize / secSize) <= (uint32_t)s_card->csd.capacity) {
      size_t count = bufsize / secSize;
      s_readCacheCount = 0;
      s_lastReadEndLba = UINT32_MAX;

      if (s_dmaBuf[0] && s_dmaBuf[1] && count <= s_bufSectors && s_writerTask) {
        bool ok = true;
        if (s_stagingCount > 0 &&
            (lba != s_stagingLba + s_stagingCount ||
             s_stagingCount + count > s_bufSectors)) {
          ok = queueActiveBufferLocked();
        }
        if (ok) {
          if (s_stagingCount == 0) {
            s_stagingLba = lba;
          }
          memcpy(s_dmaBuf[s_activeBuf] + s_stagingCount * secSize, buffer,
                 bufsize);
          s_stagingCount += count;
          s_hasPendingWrite = true;
          s_lastIoMs = millis();
          if (s_stagingCount >= s_bufSectors) {
            ok = queueActiveBufferLocked();
          }
          if (ok) {
            ret = (int32_t)bufsize;
          }
        }
      } else {
        if (flushAllWritesLocked() &&
            sdmmc_write_sectors(s_card, buffer, lba, count) == ESP_OK) {
          s_lastIoMs = millis();
          ret = (int32_t)bufsize;
        }
      }
    }
  }
  xSemaphoreGive(s_ioMutex);
  return ret;
}

static bool onStartStop(uint8_t power_condition, bool start, bool load_eject) {
  (void)power_condition;
  if (load_eject && !start) {
    if (s_ioMutex &&
        xSemaphoreTake(s_ioMutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
      flushAllWritesLocked();
      xSemaphoreGive(s_ioMutex);
    }
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
  if (!s_bgStartSem) {
    s_bgStartSem = xSemaphoreCreateBinary();
  }
  if (!s_bgDoneSem) {
    s_bgDoneSem = xSemaphoreCreateBinary();
  }
  if (!s_writerTask && s_bgStartSem && s_bgDoneSem) {
    xTaskCreatePinnedToCore(writerTaskFn, "msc_writer", 4096, nullptr, 5,
                            &s_writerTask, 0);
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
  if (tud_mounted() && !tud_suspended()) {
    s_mounted = true;
    s_suspended = false;
    return true;
  }
  if (!s_mounted) return false;
  if (!s_suspended) {
    s_suspended = true;
    s_suspendAtMs = millis();
  }
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

  sdmmc_card_t *card = SdMmcAccessor::card();
  if (!card || card->csd.sector_size == 0 || card->csd.capacity == 0) {
    log_e("SD_MMC card not initialized for USB MSC");
    return false;
  }

  // Grab the FatFs volume pointer while the VFS is still clean so leave()
  // can force FatFs to re-read the boot sector, FAT, and root directory
  // after the PC modifies sectors on the card.
  FF_DIR dir;
  if (!s_fatfs && f_opendir(&dir, "0:/") == FR_OK) {
    s_fatfs = dir.obj.fs;
    f_closedir(&dir);
  }

  xSemaphoreTake(s_ioMutex, portMAX_DELAY);
  s_card = card;
  allocateStagingBuffers((uint32_t)s_card->csd.sector_size);
  s_bgOk = true;
  s_hasPendingWrite = false;
  s_cardReady = true;
  s_ejected = false;
  s_active = true;
  s_readCount = 0;
  s_reattachTried = false;
  s_enterMs = millis();
  s_lastIoMs = millis();
  s_seenHost = pcConnected();
  s_msc.begin((uint32_t)s_card->csd.capacity, (uint16_t)s_card->csd.sector_size);
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
    flushAllWritesLocked();
    s_cardReady = false;
  }
  freeStagingBuffers();
  // Invalidate FatFs's in-RAM sector window and remount the FAT volume in
  // place on the existing SDMMC card handle, avoiding sdmmc_host_deinit() /
  // CMD0 re-probing (which fails on some SDXC cards without a power cycle).
  if (s_fatfs) {
    s_fatfs->winsect = (DWORD)-1;
    s_fatfs->wflag = 0;
    f_mount(s_fatfs, "0:", 1);
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
  if (s_hasPendingWrite && (now - s_lastIoMs >= IDLE_FLUSH_MS)) {
    if (xSemaphoreTake(s_ioMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      if (s_stagingCount > 0 && (millis() - s_lastIoMs >= IDLE_FLUSH_MS)) {
        flushAllWritesLocked();
      } else if (!s_bgInFlight && s_stagingCount == 0) {
        s_hasPendingWrite = false;
      }
      xSemaphoreGive(s_ioMutex);
    }
  }

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
  return !s_hasPendingWrite && (millis() - s_lastIoMs) >= 1000;
}

uint32_t cardSizeMB() {
  if (!s_cardReady || !s_card) return 0;
  return (uint32_t)(((uint64_t)s_card->csd.capacity * s_card->csd.sector_size) /
                    (1024ULL * 1024ULL));
}

}  // namespace UsbMsc

#endif  // WAVESHARE_154

