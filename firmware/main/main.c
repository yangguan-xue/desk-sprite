#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "st7789.h"
#include "es8311.h"
#include "ttp223.h"
#include "ui_layout.h"
#include "alert_queue.h"
#include "touch_button.h"
#include "action_reporter.h"

void app_main(void) {
    printf("===========================================\n");
    printf("  Desk Sprite ESP32-S3 Firmware v1.4.0    \n");
    printf("===========================================\n");

    // 1. 初始化板级硬件外设
    bsp_st7789_init();
    bsp_es8311_init();
    bsp_ttp223_init();

    // 2. 初始化核心引擎与 UI 状态
    ui_layout_init();
    bsp_st7789_set_backlight(80);

    printf("[System] Desk Sprite ESP32-S3 firmware initialized successfully.\n");

    // 3. 运行主循环与心跳
    int tick = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        tick++;
        if (tick % 5 == 0) {
            printf("[System Heartbeat] Desk Sprite running, uptime: %d s\n", tick);
        }
    }
}
