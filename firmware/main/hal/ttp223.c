#include "ttp223.h"
#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/touch_pad.h"

static const char *TAG = "HAL_TOUCH_PAD";
static uint32_t s_touch_baseline = 0;
static uint32_t s_touch_threshold = 2000;
static bool s_is_calibrated = false;

void bsp_touch_init(void) {
    ESP_LOGI(TAG, "Initializing native capacitive touch sensor on GPIO 5 (TOUCH_PAD_NUM5)...");
    esp_err_t ret = touch_pad_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "touch_pad_init failed: %d", ret);
        return;
    }
    ret = touch_pad_config(TOUCH_PAD_NUM5);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "touch_pad_config failed: %d", ret);
        return;
    }
    touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER);
    touch_pad_fsm_start();

    // 延时等待 FSM 启动并采集初始基线
    vTaskDelay(pdMS_TO_TICKS(100));
    uint32_t sum = 0;
    int valid_count = 0;
    for (int i = 0; i < 5; i++) {
        uint32_t raw = 0;
        if (touch_pad_read_raw_data(TOUCH_PAD_NUM5, &raw) == ESP_OK && raw > 0) {
            sum += raw;
            valid_count++;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (valid_count > 0) {
        s_touch_baseline = sum / valid_count;
        s_touch_threshold = (s_touch_baseline * 15) / 100;
        s_is_calibrated = true;
        ESP_LOGI(TAG, "Native touch pad calibrated: baseline=%lu, threshold=%lu",
                 (unsigned long)s_touch_baseline, (unsigned long)s_touch_threshold);
    } else {
        ESP_LOGW(TAG, "Touch pad baseline sampling uncalibrated");
    }
}

uint32_t bsp_touch_get_raw(void) {
    uint32_t raw = 0;
    touch_pad_read_raw_data(TOUCH_PAD_NUM5, &raw);
    return raw;
}

bool bsp_touch_read(void) {
    if (!s_is_calibrated) return false;
    uint32_t raw = 0;
    if (touch_pad_read_raw_data(TOUCH_PAD_NUM5, &raw) != ESP_OK) {
        return false;
    }
    if (raw > s_touch_baseline + s_touch_threshold ||
        (s_touch_baseline > s_touch_threshold && raw < s_touch_baseline - s_touch_threshold)) {
        return true;
    }
    return false;
}
