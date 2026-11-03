#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "mqtt_client.h"

#include "st7789.h"
#include "es8311.h"
#include "ttp223.h"
#include "sprite_types.h"
#include "ui_layout.h"
#include "ui_metrics.h"
#include "alert_filter.h"
#include "alert_queue.h"
#include "touch_button.h"
#include "action_reporter.h"

static const char *TAG = "SPRITE_MAIN";

#define DEVICE_ID "esp32_c049ef"

// 全局运行时状态
static alert_scheduler_t s_alert_sched;
static touch_button_state_t s_touch_button;
static work_mode_t s_current_mode = WORK_MODE_IDLE;
static ui_metric_view_mode_t s_metric_view_mode = UI_METRIC_VIEW_VALUE;
static long long s_mute_until_sec = 0;
static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static bool s_mqtt_connected = false;
static bool s_wifi_connected = false;

static metrics_payload_t s_last_metrics;
static ui_metrics_render_model_t s_metrics_model;
static bool s_has_metrics = false;
static char s_last_played_event_id[64] = {0};

// Wi-Fi 事件处理
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
        ESP_LOGW(TAG, "Wi-Fi disconnected, reconnecting...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_wifi_connected = true;
        ESP_LOGI(TAG, "Wi-Fi connected and got IP");
        if (s_mqtt_client) {
            esp_mqtt_client_start(s_mqtt_client);
        }
    }
}

// MQTT 报文与事件处理
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            s_mqtt_connected = true;
            ESP_LOGI(TAG, "MQTT Connected to Broker");
            char sub_topic[128];
            snprintf(sub_topic, sizeof(sub_topic), "sprite/device/%s/down/#", DEVICE_ID);
            esp_mqtt_client_subscribe(s_mqtt_client, sub_topic, 1);
            ESP_LOGI(TAG, "Subscribed to %s", sub_topic);
            break;
        case MQTT_EVENT_DISCONNECTED:
            s_mqtt_connected = false;
            ESP_LOGW(TAG, "MQTT Disconnected");
            break;
        case MQTT_EVENT_DATA: {
            // 处理接收到的下行报文 (alert, state, metrics, dismiss)
            char topic[128] = {0};
            int tlen = event->topic_len < (int)sizeof(topic) - 1 ? event->topic_len : (int)sizeof(topic) - 1;
            memcpy(topic, event->topic, tlen);

            if (strstr(topic, "/down/alert")) {
                ESP_LOGI(TAG, "Received down/alert payload");
                // 构造告警实体并推入调度器
                alert_payload_t alert;
                memset(&alert, 0, sizeof(alert));
                alert.priority = PRIORITY_MEDIUM;
                alert.duration_sec = 10;
                strncpy(alert.title, "New Notification", sizeof(alert.title) - 1);
                strncpy(alert.event_id, "evt_hw", sizeof(alert.event_id) - 1);
                alert_scheduler_push(&s_alert_sched, &alert, s_current_mode);
            } else if (strstr(topic, "/down/state")) {
                if (strstr(event->data, "coding")) s_current_mode = WORK_MODE_CODING;
                else if (strstr(event->data, "meeting")) s_current_mode = WORK_MODE_MEETING;
                else if (strstr(event->data, "sleep")) {
                    s_current_mode = WORK_MODE_SLEEP;
                    alert_scheduler_clear_medium(&s_alert_sched);
                } else s_current_mode = WORK_MODE_IDLE;
                ESP_LOGI(TAG, "Updated work mode: %d", (int)s_current_mode);
            } else if (strstr(topic, "/down/dismiss")) {
                long long now = esp_timer_get_time() / 1000000;
                alert_scheduler_dismiss(&s_alert_sched, now, s_current_mode);
                ESP_LOGI(TAG, "Alert dismissed via MQTT");
            }
            break;
        }
        default:
            break;
    }
}

// 触摸轮询任务 (GPIO 5 原生电容触摸 / TTP223, 10ms 周期)
static void touch_poll_task(void *pvParameters) {
    (void)pvParameters;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        long long now_ms = esp_timer_get_time() / 1000;
        bool pin_high = bsp_ttp223_read();
        touch_gesture_t gesture = touch_button_update(&s_touch_button, pin_high, now_ms);

        long long now_sec = now_ms / 1000;
        const alert_payload_t* active = alert_scheduler_get_active(&s_alert_sched);

        if (gesture == GESTURE_TAP) {
            ESP_LOGI(TAG, "[Touch] GESTURE_TAP detected");
            if (active) {
                // 弹窗状态下 Tap: 即刻确认并上报云端打开 URL
                device_action_t act;
                action_reporter_build_action(DEVICE_ID, GESTURE_TAP, active->event_id, now_sec, &act);
                if (s_mqtt_client && s_mqtt_connected) {
                    char up_topic[128];
                    snprintf(up_topic, sizeof(up_topic), "sprite/device/%s/up/action", DEVICE_ID);
                    esp_mqtt_client_publish(s_mqtt_client, up_topic, act.action_id, 0, 1, 0);
                }
                alert_scheduler_dismiss(&s_alert_sched, now_sec, s_current_mode);
            } else {
                // 主屏状态下 Tap: 切换三行度量指标数值 / 百分比模式 (SPEC-003 第 6 节)
                s_metric_view_mode = (s_metric_view_mode == UI_METRIC_VIEW_VALUE) ? UI_METRIC_VIEW_PERCENT : UI_METRIC_VIEW_VALUE;
                ESP_LOGI(TAG, "[Touch] Toggled metric view mode to %s", s_metric_view_mode == UI_METRIC_VIEW_PERCENT ? "PERCENT" : "VALUE");
                if (s_has_metrics) {
                    ui_metrics_compute_render_model_ex(&s_last_metrics, now_sec, s_metric_view_mode, &s_metrics_model);
                }
            }
        } else if (gesture == GESTURE_LONG_PRESS) {
            ESP_LOGI(TAG, "[Touch] GESTURE_LONG_PRESS detected");
            if (active) {
                // 弹窗状态下长按: 静默关闭，不上报动作 (SPEC-003 第 6 节)
                alert_scheduler_dismiss(&s_alert_sched, now_sec, s_current_mode);
            } else {
                // 主屏状态下长按: 切换 30 分钟本地临时静音
                if (now_sec < s_mute_until_sec) {
                    s_mute_until_sec = 0;
                    ESP_LOGI(TAG, "[Touch] Temporary mute disabled");
                } else {
                    s_mute_until_sec = now_sec + 1800;
                    ESP_LOGI(TAG, "[Touch] Temporary mute enabled for 30 min (until %lld)", s_mute_until_sec);
                }
            }
        }
    }
}

// UI 渲染与告警视听调度任务 (50ms 周期)
static void ui_and_alert_task(void *pvParameters) {
    (void)pvParameters;
    long long last_tick_sec = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(50));
        long long now_sec = esp_timer_get_time() / 1000000;

        // 每秒驱动一次调度器与倒计时
        if (now_sec != last_tick_sec) {
            last_tick_sec = now_sec;
            alert_scheduler_tick(&s_alert_sched, now_sec, s_current_mode);
            if (s_has_metrics) {
                ui_metrics_compute_render_model_ex(&s_last_metrics, now_sec, s_metric_view_mode, &s_metrics_model);
            }
        }

        const alert_payload_t* active = alert_scheduler_get_active(&s_alert_sched);
        if (active) {
            // 新到达告警触发清脆提示音 (物理 ES8311 视听联动闭环)
            if (strcmp(s_last_played_event_id, active->event_id) != 0) {
                strncpy(s_last_played_event_id, active->event_id, sizeof(s_last_played_event_id) - 1);
                bool mute = active->force_mute;
                if (!mute && active->priority == PRIORITY_MEDIUM && now_sec < s_mute_until_sec) {
                    mute = true;
                }
                if (!mute) {
                    bsp_es8311_play_tone(880, 250); // 880Hz 清脆提示音
                }
            }

            // 渲染告警弹窗全屏覆盖 (橙色或高优先级红色)
            uint16_t alert_bg = (active->priority == PRIORITY_HIGH) ? 0xF800 : 0xFD20;
            bsp_st7789_fill(alert_bg);
        } else {
            s_last_played_event_id[0] = '\0';
            // 正常待机/工作状态下绘制背景与指标
            uint16_t bg_color = (s_current_mode == WORK_MODE_SLEEP) ? 0x0000 : 0x0410;
            bsp_st7789_fill(bg_color);
        }
    }
}

void app_main(void) {
    printf("===========================================\n");
    printf("  Desk Sprite ESP32-S3 Firmware v1.4.0    \n");
    printf("===========================================\n");

    // 1. 初始化 NVS 存储
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. 初始化板级硬件外设 (按已核验引脚 board-profile.json)
    bsp_st7789_init();
    bsp_es8311_init();
    bsp_ttp223_init();

    // 3. 初始化核心状态机与调度器
    ui_layout_init();
    alert_scheduler_init(&s_alert_sched);
    touch_button_init(&s_touch_button);
    bsp_st7789_set_backlight(80);

    // 播报开机提示音
    bsp_es8311_play_tone(880, 150);

    ESP_LOGI(TAG, "Desk Sprite ESP32-S3 firmware initialized successfully.");

    // 4. 创建 FreeRTOS 任务
    xTaskCreate(touch_poll_task, "touch_task", 4096, NULL, 5, NULL);
    xTaskCreate(ui_and_alert_task, "ui_alert_task", 8192, NULL, 4, NULL);

    ESP_LOGI(TAG, "FreeRTOS tasks created. System running.");
}
