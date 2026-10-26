#include <stdio.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_oled_ssd1315.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#define I2C_SDA_PIN_NUM 10
#define I2C_SCL_PIN_NUM 9

#define OLED_H_RES 128
#define OLED_V_RES 64

// NOTE: LVGL reserves 2 x 4 bytes at the start of an I1 draw buffer for the
//       palette (index 0 and index 1 as ARGB8888). Pixel data starts after it.
#define LVGL_PALETTE_SIZE 8

#define I2C_TAG "I2C"
#define LCD_IO_TAG "LCD_IO"
#define SSD1315_TAG "SSD1315"

static i2c_master_bus_handle_t i2c_bus = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;

// Frame buffer in the SSD1315's native page layout: one byte holds 8
// vertically stacked pixels (bit 0 = top row of the page), pages run
// top-to-bottom, columns left-to-right within a page.
static uint8_t oled_buffer[OLED_H_RES * OLED_V_RES / 8];

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
  esp_lcd_panel_dev_config_t panel_config = {
      .bits_per_pixel = 1,
      .reset_gpio_num = -1,
  };

  esp_lcd_panel_ssd1315_config_t ssd1315_config = {
      .height = OLED_V_RES,
  };
  panel_config.vendor_config = &ssd1315_config;
  ESP_ERROR_CHECK(
      esp_lcd_new_panel_ssd1315(io_handle, &panel_config, &panel_handle));

  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
}

// LVGL renders in I1: row-major, 1 bit per pixel, MSB = leftmost pixel.
// The panel wants page layout (see oled_buffer), so repack before sending.
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area,
                          uint8_t *px_map) {
  esp_lcd_panel_handle_t panel = lv_display_get_user_data(disp);

  px_map += LVGL_PALETTE_SIZE;

  uint16_t hor_res = lv_display_get_physical_horizontal_resolution(disp);
  int x1 = area->x1;
  int x2 = area->x2;
  int y1 = area->y1;
  int y2 = area->y2;

  for (int y = y1; y <= y2; y++) {
    for (int x = x1; x <= x2; x++) {
      // NOTE: LVGL maps bright colors to 1 and dark colors to 0. The default
      //       theme draws black text on a white background, so a 0 bit is
      //       "ink". Light those pixels so text shows bright on black.
      bool is_bright = px_map[(hor_res >> 3) * y + (x >> 3)] & (1 << (7 - x % 8));

      uint8_t *buf = oled_buffer + hor_res * (y >> 3) + x;
      if (is_bright) {
        *buf &= ~(1 << (y % 8));
      } else {
        *buf |= (1 << (y % 8));
      }
    }
  }

  // NOTE: with LV_DISPLAY_RENDER_MODE_FULL the area is always the whole
  //       screen, so sending oled_buffer from its start is correct.
  ESP_ERROR_CHECK(
      esp_lcd_panel_draw_bitmap(panel, x1, y1, x2 + 1, y2 + 1, oled_buffer));

  // NOTE: the I2C transfer is synchronous, so the buffer is free again here.
  lv_display_flush_ready(disp);
}

static uint32_t lvgl_tick_get_cb(void) {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

void app_main(void) {
  i2c_init();
  panel_init();

  lv_init();
  lv_tick_set_cb(lvgl_tick_get_cb);

  lv_display_t *display = lv_display_create(OLED_H_RES, OLED_V_RES);
  lv_display_set_user_data(display, panel_handle);

  size_t draw_buffer_sz = OLED_H_RES * OLED_V_RES / 8 + LVGL_PALETTE_SIZE;
  void *buf = heap_caps_calloc(1, draw_buffer_sz,
                               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  assert(buf);

  // LVGL9 supports a packed monochrome format.
  lv_display_set_color_format(display, LV_COLOR_FORMAT_I1);
  lv_display_set_buffers(display, buf, NULL, draw_buffer_sz,
                         LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display, lvgl_flush_cb);

  lv_obj_t *scr = lv_display_get_screen_active(display);
  lv_obj_t *label = lv_label_create(scr);
  lv_label_set_text(label, "Hello world");
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

  while (1) {
    // Runs LVGL's timers: refreshes the screen and calls lvgl_flush_cb when
    // something changed. Returns how long we can sleep until the next timer.
    uint32_t sleep_ms = lv_timer_handler();
    if (sleep_ms > 500) {
      sleep_ms = 500;
    } else if (sleep_ms < 1) {
      sleep_ms = 1;
    }
    vTaskDelay(pdMS_TO_TICKS(sleep_ms));
  }
}
