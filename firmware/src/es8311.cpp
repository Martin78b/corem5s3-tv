#include "es8311.h"
#ifdef WAVESHARE_154
#include <Arduino.h>
#include <esp_log.h>
#include <driver/i2c.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char* TAG = "ES8311";

#define ES8311_RESET_REG00       0x00
#define ES8311_CLK_MANAGER_REG01 0x01
#define ES8311_CLK_MANAGER_REG02 0x02
#define ES8311_CLK_MANAGER_REG03 0x03
#define ES8311_CLK_MANAGER_REG04 0x04
#define ES8311_CLK_MANAGER_REG05 0x05
#define ES8311_CLK_MANAGER_REG06 0x06
#define ES8311_CLK_MANAGER_REG07 0x07
#define ES8311_CLK_MANAGER_REG08 0x08
#define ES8311_SDPIN_REG09       0x09
#define ES8311_SDPOUT_REG0A      0x0A
#define ES8311_SYSTEM_REG0B      0x0B
#define ES8311_SYSTEM_REG0C      0x0C
#define ES8311_SYSTEM_REG0D      0x0D
#define ES8311_SYSTEM_REG0E      0x0E
#define ES8311_SYSTEM_REG0F      0x0F
#define ES8311_SYSTEM_REG10      0x10
#define ES8311_SYSTEM_REG11      0x11
#define ES8311_SYSTEM_REG12      0x12
#define ES8311_SYSTEM_REG13      0x13
#define ES8311_SYSTEM_REG14      0x14
#define ES8311_ADC_REG15         0x15
#define ES8311_ADC_REG16         0x16
#define ES8311_ADC_REG17         0x17
#define ES8311_ADC_REG18         0x18
#define ES8311_ADC_REG19         0x19
#define ES8311_ADC_REG1A         0x1A
#define ES8311_ADC_REG1B         0x1B
#define ES8311_ADC_REG1C         0x1C
#define ES8311_DAC_REG31         0x31
#define ES8311_DAC_REG32         0x32
#define ES8311_DAC_REG37         0x37
#define ES8311_GPIO_REG44        0x44
#define ES8311_GP_REG45          0x45
#define ES8311_CHIP_ID           0xFD

#define I2C_MASTER_FREQ_HZ 400000
#define I2C_TIMEOUT_MS 100

static uint8_t _addr = 0x18;
static i2c_port_t _bus = I2C_NUM_1;
static bool _initialized = false;

static esp_err_t write_reg(uint8_t reg, uint8_t val) {
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (_addr << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, reg, true);
  i2c_master_write_byte(cmd, val, true);
  i2c_master_stop(cmd);
  esp_err_t ret = i2c_master_cmd_begin(_bus, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
  i2c_cmd_link_delete(cmd);
  return ret;
}

static esp_err_t read_reg(uint8_t reg, uint8_t *val) {
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (_addr << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, reg, true);
  i2c_master_stop(cmd);
  esp_err_t ret = i2c_master_cmd_begin(_bus, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
  i2c_cmd_link_delete(cmd);
  if (ret != ESP_OK) return ret;

  cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (_addr << 1) | I2C_MASTER_READ, true);
  i2c_master_read_byte(cmd, val, I2C_MASTER_NACK);
  i2c_master_stop(cmd);
  ret = i2c_master_cmd_begin(_bus, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
  i2c_cmd_link_delete(cmd);
  return ret;
}

bool es8311_init(int sda, int scl, uint8_t addr, int sample_rate) {
  if (_initialized) return true;
  _addr = addr;

  Serial.printf("[ES8311] Init start, addr=0x%02X\n", addr);

  // main.cpp already installed + configured I2C_NUM_1 for GPIO42/41 at 400kHz.
  // Do NOT call i2c_driver_install here — the touch task is already using the
  // bus and the install will fail with ESP_ERR_INVALID_STATE (-1).
  i2c_port_t bus = I2C_NUM_1;
  Serial.println("[ES8311] Using I2C_NUM_1 (already initialized by main.cpp)");

  esp_err_t err = ESP_OK;

  // Try multiple addresses
    uint8_t addrs[] = {addr, (uint8_t)(addr ^ 0x08), 0x10, 0x11, 0x18, 0x19, 0x1A, 0x1B};
    bool found = false;
    for (int i = 0; i < 8 && !found; i++) {
      _addr = addrs[i];
      Serial.printf("[ES8311] Probing 0x%02X... ", _addr);
      i2c_cmd_handle_t cmd = i2c_cmd_link_create();
      i2c_master_start(cmd);
      i2c_master_write_byte(cmd, (_addr << 1) | I2C_MASTER_WRITE, true);
      i2c_master_stop(cmd);
      err = i2c_master_cmd_begin(bus, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
      i2c_cmd_link_delete(cmd);
      if (err == ESP_OK) {
        Serial.print("ACK");
        uint8_t id = 0;
        // Read chip ID register (0xFD) using bus directly
        i2c_cmd_handle_t cmd2 = i2c_cmd_link_create();
        i2c_master_start(cmd2);
        i2c_master_write_byte(cmd2, (_addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(cmd2, ES8311_CHIP_ID, true);
        i2c_master_stop(cmd2);
        i2c_master_cmd_begin(bus, cmd2, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
        i2c_cmd_link_delete(cmd2);
        cmd2 = i2c_cmd_link_create();
        i2c_master_start(cmd2);
        i2c_master_write_byte(cmd2, (_addr << 1) | I2C_MASTER_READ, true);
        i2c_master_read_byte(cmd2, &id, I2C_MASTER_NACK);
        i2c_master_stop(cmd2);
        err = i2c_master_cmd_begin(bus, cmd2, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
        i2c_cmd_link_delete(cmd2);
        if (err == ESP_OK && id != 0xFF) {
          Serial.printf("[ES8311] Chip ID at 0x%02X = 0x%02X\n", _addr, id);
        } else {
          Serial.printf("[ES8311] Chip ID read at 0x%02X: err=%d id=0x%02X — proceeding anyway\n", _addr, err, id);
        }
        Serial.printf(" chipID=0x%02X\n", id);
        found = true;
        _bus = bus;
        Serial.printf("[ES8311] Found at 0x%02X\n", _addr);
      } else {
        Serial.println(" NAK");
      }
    }

  if (!found) {
    Serial.println("[ES8311] NOT found on I2C_NUM_1");
    return false;
  }

  write_reg(ES8311_SYSTEM_REG0D, 0xFA);
  vTaskDelay(pdMS_TO_TICKS(5));

  write_reg(ES8311_GPIO_REG44, 0x08);
  write_reg(ES8311_GPIO_REG44, 0x08);

  write_reg(ES8311_CLK_MANAGER_REG01, 0x30);
  write_reg(ES8311_CLK_MANAGER_REG02, 0x00);
  write_reg(ES8311_CLK_MANAGER_REG03, 0x10);
  write_reg(ES8311_ADC_REG16, 0x24);
  write_reg(ES8311_CLK_MANAGER_REG04, 0x10);
  write_reg(ES8311_CLK_MANAGER_REG05, 0x00);
  write_reg(ES8311_SYSTEM_REG0B, 0x00);
  write_reg(ES8311_SYSTEM_REG0C, 0x00);
  write_reg(ES8311_SYSTEM_REG10, 0x1F);
  write_reg(ES8311_SYSTEM_REG11, 0x7F);
  write_reg(ES8311_RESET_REG00, 0x80);
  vTaskDelay(pdMS_TO_TICKS(5));

  uint8_t regv = 0;
  read_reg(ES8311_RESET_REG00, &regv);
  regv &= 0xBF;
  write_reg(ES8311_RESET_REG00, regv);

  write_reg(ES8311_CLK_MANAGER_REG01, 0x3F);

  write_reg(ES8311_SYSTEM_REG13, 0x10);
  write_reg(ES8311_ADC_REG1B, 0x0A);
  write_reg(ES8311_ADC_REG1C, 0x6A);
  write_reg(ES8311_GPIO_REG44, 0x08);

  write_reg(ES8311_CLK_MANAGER_REG02, 0);
  write_reg(ES8311_CLK_MANAGER_REG05, 0);
  write_reg(ES8311_CLK_MANAGER_REG03, 0x10);
  write_reg(ES8311_CLK_MANAGER_REG04, 0x10);
  write_reg(ES8311_CLK_MANAGER_REG07, 0);
  write_reg(ES8311_CLK_MANAGER_REG08, 0xFF);
  write_reg(ES8311_CLK_MANAGER_REG06, 0x03);

  write_reg(ES8311_SDPIN_REG09, 0x0C);   // 16-bit SDP word length
  write_reg(ES8311_SDPOUT_REG0A, 0x0C);  // 16-bit SDP word length

  write_reg(ES8311_ADC_REG17, 0xBF);
  write_reg(ES8311_SYSTEM_REG0E, 0x02);
  write_reg(ES8311_SYSTEM_REG12, 0x00);
  write_reg(ES8311_SYSTEM_REG14, 0x1A);
  write_reg(ES8311_SYSTEM_REG0D, 0x01);
  write_reg(ES8311_ADC_REG15, 0x40);
  write_reg(ES8311_DAC_REG37, 0x08);
  write_reg(ES8311_GP_REG45, 0x00);

  write_reg(ES8311_DAC_REG32, 0xBF);  // 0dB (no digital gain/attenuation)
  read_reg(ES8311_DAC_REG31, &regv);
  regv &= 0x9F;
  write_reg(ES8311_DAC_REG31, regv);

  _initialized = true;
  Serial.printf("[ES8311] Initialized successfully at %dHz, addr=0x%02X\n", sample_rate, _addr);
  return true;
}

bool es8311_set_volume(uint8_t vol) {
  // Map 0-255 to register range 0x00 (-95.5dB) to 0xBF (0dB)
  // Avoids the +32dB digital gain range (0xC0-0xFF) which causes clipping
  uint8_t reg = (uint8_t)((uint32_t)vol * 0xBF / 255);
  return write_reg(ES8311_DAC_REG32, reg) == ESP_OK;
}

bool es8311_set_mute(bool mute) {
  uint8_t regv = 0;
  read_reg(ES8311_DAC_REG31, &regv);
  regv &= 0x9F;
  if (mute) regv |= 0x60;
  return write_reg(ES8311_DAC_REG31, regv) == ESP_OK;
}
#endif
