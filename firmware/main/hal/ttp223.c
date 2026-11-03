#include "ttp223.h"
#include <stdio.h>
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "HAL_TTP223";

void bsp_ttp223_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << TOUCH_PIN_KEY),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    ESP_LOGI(TAG, "Configured touch button input on GPIO %d", TOUCH_PIN_KEY);
}

bool bsp_ttp223_read(void) {
    return gpio_get_level(TOUCH_PIN_KEY) == 1;
}
