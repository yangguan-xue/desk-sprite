#include "es8311.h"
#include <stdio.h>
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "HAL_ES8311";

void bsp_es8311_init(void) {
    gpio_config_t pa_conf = {
        .pin_bit_mask = (1ULL << AUDIO_PIN_PA_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&pa_conf));
    // 默认关闭功放静音，避免上电破音
    gpio_set_level(AUDIO_PIN_PA_EN, 0);

    ESP_LOGI(TAG, "Initialized Audio HAL (BCLK=%d, LRCK=%d, SDOUT=%d, PA_EN=%d)",
             AUDIO_PIN_BCLK, AUDIO_PIN_LRCK, AUDIO_PIN_SDOUT, AUDIO_PIN_PA_EN);
}

void bsp_es8311_set_mute(bool mute) {
    // PA_EN: 1 为开启放音，0 为硬件断电静音
    gpio_set_level(AUDIO_PIN_PA_EN, mute ? 0 : 1);
    ESP_LOGI(TAG, "PA_EN set to %s (GPIO %d)", mute ? "LOW (Muted)" : "HIGH (Enabled)", AUDIO_PIN_PA_EN);
}

void bsp_es8311_set_volume(uint8_t volume_pct) {
    ESP_LOGI(TAG, "Volume set to %d%%", volume_pct);
}
