#include "encoder.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_log_level.h"
#include "freertos/idf_additions.h"
#include "led_strip.h"
#include "portmacro.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stdio.h>

#define ROTARY_ENCODER_GPIO_A 16
#define ROTARY_ENCODER_GPIO_B 17
#define ROTARY_ENCODER_GPIO_BTN 5

static rotary_encoder_handle_t rotary_encoder;

void encoder_event_handler(const rotary_encoder_event_t *event, void *ctx) {
  QueueHandle_t queue = (QueueHandle_t)ctx;
  xQueueSendToBack(queue, event, 0);
}

QueueHandle_t create_rotary_encoder() {

  rotary_encoder_config_t encoder_cfg = ROTARY_ENCODER_DEFAULT_CONFIG();

  QueueHandle_t encoder_event_queue =
      xQueueCreate(5, sizeof(rotary_encoder_event_t));

  encoder_cfg.pin_a = ROTARY_ENCODER_GPIO_A;
  encoder_cfg.pin_b = ROTARY_ENCODER_GPIO_B;
  encoder_cfg.pin_btn = ROTARY_ENCODER_GPIO_BTN;
  encoder_cfg.callback = encoder_event_handler;
  encoder_cfg.callback_ctx = encoder_event_queue;
  ESP_ERROR_CHECK(rotary_encoder_create(&encoder_cfg, &rotary_encoder));

  return encoder_event_queue;
}

static led_strip_handle_t led_strip;
static bool led_on = false;
static uint32_t led_red = 16;

void configure_led() {
  led_strip_config_t strip_config = {
      .strip_gpio_num = 21,
      .max_leds = 1, // at least one LED on board
  };

  led_strip_rmt_config_t rmt_config = {
      .resolution_hz = 10 * 1000 * 1000, // 10MHz
      .flags.with_dma = false,
  };
  ESP_ERROR_CHECK(
      led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
}

void blink_led(void *arg) {
  configure_led();

  while (1) {
    /* If the addressable LED is enabled */
    if (led_on) {
      // ESP_LOGI("LED", "RED %" PRIi32, led_red);
      /* Set the LED pixel using RGB from 0 (0%) to 255 (100%) for each color */
      led_strip_set_pixel(led_strip, 0, led_red, 16, 16);
      /* Refresh the strip to send data */
      led_strip_refresh(led_strip);
    } else {
      /* Set all LED off to clear all pixels */
      led_strip_clear(led_strip);
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void app_main(void) {

  xTaskCreate(blink_led, "BLINK LED", 4 * 1024, NULL, 2, NULL);

  QueueHandle_t encoder_event_queue = create_rotary_encoder();

  rotary_encoder_event_t e;
  int32_t val = 0;

  ESP_LOGI("ENCODER", "Initial value: %" PRIi32, val);

  while (1) {
    xQueueReceive(encoder_event_queue, &e, portMAX_DELAY);

    switch (e.type) {
    case RE_ET_BTN_PRESSED:
      ESP_LOGI("ENCODER", "Button pressed");
      break;
    case RE_ET_BTN_RELEASED:
      ESP_LOGI("ENCODER", "Button released");
      break;
    case RE_ET_BTN_CLICKED:
      ESP_LOGI("ENCODER", "Button clicked");
      // rotary_encoder_enable_acceleration(re, 100);
      // ESP_LOGI("ENCODER", "Acceleration enabled");
      break;
    case RE_ET_BTN_LONG_PRESSED:
      ESP_LOGI("ENCODER", "Looooong pressed button");
      led_on = !led_on;
      // rotary_encoder_disable_acceleration(re);
      // ESP_LOGI("ENCODER", "Acceleration disabled");
      break;
    case RE_ET_CHANGED:
      val += e.diff;

      int32_t dd = e.diff * 4;

      if (dd < 0 && abs(dd) > led_red) {
        led_red = 0;
      } else if (dd > 0 && (led_red + dd) > 255) {
        led_red = 255;
      } else {
        led_red += dd;
      }
      ESP_LOGI("ENCODER", "Value = %" PRIi32, val);
      break;
    default:
      break;
    }
  }
}
