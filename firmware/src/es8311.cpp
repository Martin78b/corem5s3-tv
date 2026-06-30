#include "es8311.h"
#ifdef WAVESHARE_154
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

#define I2C_MASTER_FREQ_HZ 100000
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

  // Try both I2C buses (I2C_NUM_0 and I2C_NUM_1)
  i2c_port_t buses[] = {I2C_NUM_0, I2C_NUM_1};
  bool found = false;

  for (int b = 0; b < 2 && !found; b++) {
    i2c_port_t bus = buses[b];
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = (gpio_num_t)sda;
    conf.scl_io_num = (gpio_num_t)scl;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_MASTER_FREQ_HZ;
    esp_err_t err = i2c_param_config(bus, &conf);
    if (err != ESP_OK) { ESP_LOGE(TAG, "i2c_param_config bus %d failed: %d", bus, err); continue; }
    err = i2c_driver_install(bus, I2C_MODE_MASTER, 0, 0, 0);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "I2C_NUM_%d driver installed", bus);
    } else {
      ESP_LOGW(TAG, "I2C_NUM_%d driver install: %d (might be already initialized — continuing)", bus, err);
    }

    // Try multiple addresses
    uint8_t addrs[] = {addr, (uint8_t)(addr ^ 0x08), 0x10, 0x11, 0x18, 0x19, 0x1A, 0x1B};
    for (int i = 0; i < 8 && !found; i++) {
      _addr = addrs[i];
      // Quick probe: write 0 bytes to check if device acks
      i2c_cmd_handle_t cmd = i2c_cmd_link_create();
      i2c_master_start(cmd);
      i2c_master_write_byte(cmd, (_addr << 1) | I2C_MASTER_WRITE, true);
      i2c_master_stop(cmd);
      err = i2c_master_cmd_begin(bus, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
      i2c_cmd_link_delete(cmd);
      if (err == ESP_OK) {
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
          ESP_LOGI(TAG, "Chip ID at 0x%02X = 0x%02X", _addr, id);
        } else {
          ESP_LOGW(TAG, "Chip ID read at 0x%02X: err=%d id=0x%02X — proceeding anyway", _addr, err, id);
        }
        found = true;
        _bus = bus;
        ESP_LOGI(TAG, "Device found at 0x%02X on I2C_NUM_%d", _addr, _bus);
        break;
      }
    }
    if (!found) {
      ESP_LOGI(TAG, "ES8311 not found on I2C_NUM_%d", bus);
    }
  }

  if (!found) {
    ESP_LOGE(TAG, "ES8311 not found on any I2C bus or address");
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

  write_reg(ES8311_SDPIN_REG09, 0x04);   // I2S standard, 16-bit
  write_reg(ES8311_SDPOUT_REG0A, 0x04);  // I2S standard, 16-bit

  write_reg(ES8311_ADC_REG17, 0xBF);
  write_reg(ES8311_SYSTEM_REG0E, 0x02);
  write_reg(ES8311_SYSTEM_REG12, 0x00);
  write_reg(ES8311_SYSTEM_REG14, 0x1A);
  write_reg(ES8311_SYSTEM_REG0D, 0x01);
  write_reg(ES8311_ADC_REG15, 0x40);
  write_reg(ES8311_DAC_REG37, 0x10);
  write_reg(ES8311_GP_REG45, 0x00);

  write_reg(ES8311_DAC_REG32, 0xFF);
  read_reg(ES8311_DAC_REG31, &regv);
  regv &= 0x9F;
  write_reg(ES8311_DAC_REG31, regv);

  _initialized = true;
  ESP_LOGI(TAG, "ES8311 initialized at %dHz", sample_rate);
  return true;
}

bool es8311_set_volume(uint8_t vol) {
  uint8_t reg = (uint8_t)((uint32_t)vol * 0xFF / 128);
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
