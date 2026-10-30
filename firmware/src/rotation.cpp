#include "rotation.h"

#ifdef WAVESHARE_154

#include <atomic>
#include <cstdlib>
#include <driver/i2c.h>

namespace Rotation {

const char *const LABELS[OPTION_COUNT] = {"0", "90", "180", "270", "AUTO"};

// QMI8658 registers
static constexpr uint8_t REG_WHO_AM_I = 0x00;
static constexpr uint8_t REG_REVISION = 0x01;
static constexpr uint8_t REG_CTRL1    = 0x02;
static constexpr uint8_t REG_CTRL2    = 0x03;
static constexpr uint8_t REG_CTRL5    = 0x06;
static constexpr uint8_t REG_CTRL7    = 0x08;
static constexpr uint8_t REG_AX_L     = 0x35;  // 6 bytes: AX_L..AZ_H
static constexpr uint8_t WHO_AM_I_VAL = 0x05;

// At +/-4g full scale, 1g = 8192 LSB.
// Ignore auto-rotation when the board lies nearly flat (in-plane < ~0.45g
// or |gz| clearly dominates), or when tilted near a 45 deg diagonal.
// tan(35 deg) ~= 0.70 -> dominant * 7 >= secondary * 10.
static constexpr int32_t MIN_TILT_LSB = 3686;  // ~0.45g

static uint8_t s_addr = IMU_ADDR_PRIMARY;
static std::atomic<bool> s_imuFound{false};
static std::atomic<uint8_t> s_mode{ROT_DEFAULT};
static std::atomic<uint8_t> s_currentRot{0};
static std::atomic<uint8_t> s_targetRot{0};

// State owned by touchTask (core 0) for auto-rotation hysteresis
static uint32_t s_lastPollMs = 0;
static uint8_t s_pendingRot = 0;
static uint32_t s_pendingSinceMs = 0;

static bool writeReg(uint8_t addr, uint8_t reg, uint8_t val) {
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, reg, true);
  i2c_master_write_byte(cmd, val, true);
  i2c_master_stop(cmd);
  esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_1, cmd, pdMS_TO_TICKS(50));
  i2c_cmd_link_delete(cmd);
  return ret == ESP_OK;
}

static bool readBytes(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len) {
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, reg, true);
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
  for (size_t i = 0; i < len; i++) {
    i2c_master_read_byte(cmd, &buf[i],
                         (i == len - 1) ? I2C_MASTER_NACK : I2C_MASTER_ACK);
  }
  i2c_master_stop(cmd);
  esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_1, cmd, pdMS_TO_TICKS(50));
  i2c_cmd_link_delete(cmd);
  return ret == ESP_OK;
}

bool init() {
  const uint8_t candidates[] = {IMU_ADDR_PRIMARY, IMU_ADDR_ALT};
  uint8_t who = 0;
  bool found = false;

  for (uint8_t addr : candidates) {
    if (readBytes(addr, REG_WHO_AM_I, &who, 1) && who == WHO_AM_I_VAL) {
      s_addr = addr;
      found = true;
      break;
    }
  }

  if (!found) {
    Serial.println("[IMU] QMI8658 not found on I2C_NUM_1");
    s_imuFound = false;
    return false;
  }

  uint8_t rev = 0;
  readBytes(s_addr, REG_REVISION, &rev, 1);

  // CTRL1 (0x02) = 0x40: address auto-increment enabled, little-endian data
  // CTRL2 (0x03) = 0x17: accelerometer +/-4g (aFS=001), 62.5 Hz ODR (aODR=0111)
  // CTRL5 (0x06) = 0x01: enable accelerometer low-pass filter
  // CTRL7 (0x08) = 0x01: enable accelerometer only (aEN=1, gEN=0)
  bool ok = writeReg(s_addr, REG_CTRL1, 0x40) &&
            writeReg(s_addr, REG_CTRL2, 0x17) &&
            writeReg(s_addr, REG_CTRL5, 0x01) &&
            writeReg(s_addr, REG_CTRL7, 0x01);

  s_imuFound = ok;
  Serial.printf("[IMU] QMI8658 at 0x%02X (WHO_AM_I=0x%02X rev=0x%02X): %s\n",
                s_addr, who, rev, ok ? "OK" : "config failed");
  return ok;
}

bool hasImu() { return s_imuFound.load(); }

void setMode(uint8_t option) {
  if (option >= OPTION_COUNT) option = ROT_DEFAULT;
  s_mode = option;
  if (option < ROT_AUTO) {
    s_targetRot = option;
    s_pendingRot = option;
  } else {
    // Force an immediate sample on the next touchTask pass
    s_lastPollMs = 0;
    s_pendingRot = s_targetRot.load();
    s_pendingSinceMs = millis();
  }
}

uint8_t mode() { return s_mode.load(); }

void pollImu(uint32_t nowMs) {
  if (s_mode.load() != ROT_AUTO || !s_imuFound.load()) return;
  if (s_lastPollMs != 0 && (nowMs - s_lastPollMs < IMU_POLL_MS)) return;
  s_lastPollMs = nowMs ? nowMs : 1;

  uint8_t raw[6];
  if (!readBytes(s_addr, REG_AX_L, raw, sizeof(raw))) return;

  int16_t rawAx = (int16_t)(((uint16_t)raw[1] << 8) | raw[0]);
  int16_t rawAy = (int16_t)(((uint16_t)raw[3] << 8) | raw[2]);
  int16_t rawAz = (int16_t)(((uint16_t)raw[5] << 8) | raw[4]);

  int32_t gx = IMU_SWAP_XY ? rawAy : rawAx;
  int32_t gy = IMU_SWAP_XY ? rawAx : rawAy;
  if (IMU_INVERT_X) gx = -gx;
  if (IMU_INVERT_Y) gy = -gy;
  int32_t gz = rawAz;

  int32_t absX = std::abs(gx);
  int32_t absY = std::abs(gy);
  int32_t absZ = std::abs(gz);
  int32_t dom = (absX > absY) ? absX : absY;
  int32_t sec = (absX > absY) ? absY : absX;

  // Hold current orientation if nearly flat (Z dominant) or near 45 deg diagonal
  if (dom < MIN_TILT_LSB || dom < absZ || dom * 7 < sec * 10) {
    s_pendingRot = s_targetRot.load();
    s_pendingSinceMs = nowMs;
    return;
  }

  // Candidate TFT_eSPI rotation (0..3 = 0 / 90 / 180 / 270 deg)
  uint8_t cand;
  if (absY >= absX) {
    cand = (gy >= 0) ? ROT_0 : ROT_180;
  } else {
    cand = (gx >= 0) ? ROT_90 : ROT_270;
  }

  uint8_t active = s_targetRot.load();
  if (cand == active) {
    s_pendingRot = cand;
    s_pendingSinceMs = nowMs;
    return;
  }

  if (cand != s_pendingRot) {
    s_pendingRot = cand;
    s_pendingSinceMs = nowMs;
    return;
  }

  if (nowMs - s_pendingSinceMs >= IMU_STABLE_MS) {
    s_targetRot = cand;
  }
}

uint8_t current() { return s_currentRot.load(); }

uint8_t target() { return s_targetRot.load(); }

bool consumeChange(uint8_t &outRotation) {
  uint8_t tgt = s_targetRot.load();
  uint8_t cur = s_currentRot.load();
  if (tgt == cur) return false;
  s_currentRot = tgt;
  outRotation = tgt;
  return true;
}

void mapTouch(uint16_t rawX, uint16_t rawY, int16_t &outX, int16_t &outY) {
  int16_t rx = (rawX < DISPLAY_WIDTH) ? (int16_t)rawX : DISPLAY_WIDTH - 1;
  int16_t ry = (rawY < DISPLAY_HEIGHT) ? (int16_t)rawY : DISPLAY_HEIGHT - 1;

  switch (s_currentRot.load() & 3) {
    case 0:  // 0 deg
      outX = rx;
      outY = ry;
      break;
    case 1:  // 90 deg CW
      outX = ry;
      outY = (DISPLAY_WIDTH - 1) - rx;
      break;
    case 2:  // 180 deg
      outX = (DISPLAY_WIDTH - 1) - rx;
      outY = (DISPLAY_HEIGHT - 1) - ry;
      break;
    case 3:  // 270 deg CW
      outX = (DISPLAY_HEIGHT - 1) - ry;
      outY = rx;
      break;
  }
}

}  // namespace Rotation

#endif  // WAVESHARE_154

