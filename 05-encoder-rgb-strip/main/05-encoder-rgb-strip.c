#include "encoder.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
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

void app_main(void) {

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
      ESP_LOGI("ENCODER", "Acceleration enabled");
      break;
    case RE_ET_BTN_LONG_PRESSED:
      ESP_LOGI("ENCODER", "Looooong pressed button");
      // rotary_encoder_disable_acceleration(re);
      ESP_LOGI("ENCODER", "Acceleration disabled");
      break;
    case RE_ET_CHANGED:
      val += e.diff;
      ESP_LOGI("ENCODER", "Value = %" PRIi32, val);
      break;
    default:
      break;
    }
  }
}
