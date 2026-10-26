#include <stdio.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_oled_ssd1315.h"
// #include "esp_log_args.h"
// #include "soc/clk_tree_defs.h"

#define I2C_SDA_PIN_NUM 10
#define I2C_SCL_PIN_NUM 9

#define I2C_TAG "I2C"
#define LCD_IO_TAG "LCD_IO"
#define SSD1315_TAG "SSD1315"

static i2c_master_bus_handle_t i2c_bus = NULL;

void i2c_init(void) {
  ESP_LOGI(I2C_TAG, "Initializing i2c bus");

  i2c_master_bus_config_t i2c_bus_cfg = {
      .clk_source = I2C_CLK_SRC_DEFAULT,
      // NOTE: SDA/SCL hardware filter; ignores pulses shorter than N
      // source-clock cycles (APB 80 MHz on ESP32 -> 7 = ~87.5 ns). 0 disables
      // it, 7 is the ESP32 max (3-bit field).
      .glitch_ignore_cnt = 7,
      .i2c_port = -1,
      .sda_io_num = I2C_SDA_PIN_NUM,
      .scl_io_num = I2C_SCL_PIN_NUM,

      // WARN: better use external pullups
      .flags.enable_internal_pullup = true};

  ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus));
}

void panel_init() {

  ESP_LOGI(LCD_IO_TAG, "Install panel IO");

  esp_lcd_panel_io_handle_t io_handle = NULL;
  esp_lcd_panel_io_i2c_config_t io_config = {
      .dev_addr = 0x3C,
      .scl_speed_hz = 100000,
      .control_phase_bytes = 1, // According to SSD1315 datasheet
      // NOTE: according to SSD1315 datasheet, but same as SSD1306. Strange if
      //       not 8
      .lcd_cmd_bits = 8,
      .lcd_param_bits = 8,
      .dc_bit_offset = 6,

  };
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &io_config, &io_handle));

  ESP_LOGI(SSD1315_TAG, "Install SSD1315 panel driver");
  esp_lcd_panel_handle_t panel_handle = NULL;
  esp_lcd_panel_dev_config_t panel_config = {
      .bits_per_pixel = 1,
      .reset_gpio_num = -1,
  };

  esp_lcd_panel_ssd1315_config_t ssd1315_config = {
      .height = 64,
  };
  panel_config.vendor_config = &ssd1315_config;
  ESP_ERROR_CHECK(
      esp_lcd_new_panel_ssd1315(io_handle, &panel_config, &panel_handle));

  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
}

void app_main(void) {
  i2c_init();
  panel_init();
}
