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
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_crt_bundle.h"
#include "mqtt_client.h"
#include "mqtt_reassembler.h"
#include "cJSON.h"

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
#include "touch_action.h"
#include "font_utf8.h"

static const char *TAG = "SPRITE_MAIN";

// 可配置宏定义 (支持 menuconfig / Kconfig.projbuild / NVS 覆盖)
#ifndef CONFIG_SPRITE_WIFI_SSID
#define CONFIG_SPRITE_WIFI_SSID "myssid"
#endif

#ifndef CONFIG_SPRITE_WIFI_PASSWORD
#define CONFIG_SPRITE_WIFI_PASSWORD "mypassword"
#endif

#ifndef CONFIG_SPRITE_MQTT_BROKER_URL
#define CONFIG_SPRITE_MQTT_BROKER_URL "mqtt://127.0.0.1:1883"
#endif

#ifndef CONFIG_SPRITE_MQTT_USERNAME
#define CONFIG_SPRITE_MQTT_USERNAME ""
#endif

#ifndef CONFIG_SPRITE_MQTT_PASSWORD
#define CONFIG_SPRITE_MQTT_PASSWORD ""
#endif

#ifndef CONFIG_SPRITE_DEVICE_ID
#define CONFIG_SPRITE_DEVICE_ID "esp32_c049ef"
#endif

#ifndef CONFIG_SPRITE_SNTP_SERVER
#define CONFIG_SPRITE_SNTP_SERVER "pool.ntp.org"
#endif

// SPEC-005 第 3.0 节 & SPEC-007: 原生电容触摸交互尚未通过实机可靠性验收，默认禁用产品入口上报行为 (R4-05)
#ifndef CONFIG_SPRITE_ENABLE_TOUCH_ACTIONS
#define CONFIG_SPRITE_ENABLE_TOUCH_ACTIONS 0
#endif

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
static mqtt_reassembler_t s_mqtt_reassembler;

// 前向声明
static void wifi_init_sta(void);
static void mqtt_app_start(void);
static void sync_time_via_sntp(void);
static bool is_time_synced(void) __attribute__((unused));

// ============================================================================
// JSON 报文解析辅助函数 (R4-02: 有界解析 SPEC-002 下行告警、指标与状态)
// ============================================================================

static void parse_down_alert(const char *json_str, alert_payload_t *out_alert) {
    if (!json_str || !out_alert) return;
    memset(out_alert, 0, sizeof(*out_alert));

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGW(TAG, "Failed to parse JSON in /down/alert");
        return;
    }

    cJSON *ev_id = cJSON_GetObjectItem(root, "event_id");
    if (cJSON_IsString(ev_id) && ev_id->valuestring) {
        strncpy(out_alert->event_id, ev_id->valuestring, sizeof(out_alert->event_id) - 1);
    }

    cJSON *exp = cJSON_GetObjectItem(root, "expire_at");
    if (cJSON_IsNumber(exp)) {
        out_alert->expire_at = (long long)exp->valuedouble;
    }

    cJSON *ts = cJSON_GetObjectItem(root, "timestamp");
    if (cJSON_IsNumber(ts)) {
        out_alert->created_at = (long long)ts->valuedouble;
    } else {
        out_alert->created_at = (long long)time(NULL);
    }

    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (cJSON_IsObject(data)) {
        cJSON *title = cJSON_GetObjectItem(data, "title");
        if (cJSON_IsString(title) && title->valuestring) {
            strncpy(out_alert->title, title->valuestring, sizeof(out_alert->title) - 1);
        }

        cJSON *summary = cJSON_GetObjectItem(data, "summary");
        if (cJSON_IsString(summary) && summary->valuestring) {
            strncpy(out_alert->summary, summary->valuestring, sizeof(out_alert->summary) - 1);
        }

        cJSON *prio = cJSON_GetObjectItem(data, "priority");
        if (cJSON_IsString(prio) && prio->valuestring) {
            if (strcmp(prio->valuestring, "high") == 0) out_alert->priority = PRIORITY_HIGH;
            else if (strcmp(prio->valuestring, "medium") == 0) out_alert->priority = PRIORITY_MEDIUM;
            else out_alert->priority = PRIORITY_LOW;
        } else {
            out_alert->priority = PRIORITY_MEDIUM;
        }

        cJSON *sound = cJSON_GetObjectItem(data, "sound");
        if (cJSON_IsString(sound) && sound->valuestring) {
            if (strcmp(sound->valuestring, "ding") == 0) out_alert->sound = SOUND_DING;
            else if (strcmp(sound->valuestring, "chime") == 0) out_alert->sound = SOUND_CHIME;
            else if (strcmp(sound->valuestring, "cheer") == 0) out_alert->sound = SOUND_CHEER;
            else if (strcmp(sound->valuestring, "sigh") == 0) out_alert->sound = SOUND_SIGH;
            else if (strcmp(sound->valuestring, "alert_beep") == 0) out_alert->sound = SOUND_ALERT_BEEP;
            else out_alert->sound = SOUND_NONE;
        }

        cJSON *pet_action = cJSON_GetObjectItem(data, "pet_action");
        if (cJSON_IsString(pet_action) && pet_action->valuestring) {
            if (strcmp(pet_action->valuestring, "coding") == 0) out_alert->pet_action = PET_CODING;
            else if (strcmp(pet_action->valuestring, "meeting") == 0) out_alert->pet_action = PET_MEETING;
            else if (strcmp(pet_action->valuestring, "cheer") == 0) out_alert->pet_action = PET_CHEER;
            else if (strcmp(pet_action->valuestring, "sigh") == 0) out_alert->pet_action = PET_SIGH;
            else if (strcmp(pet_action->valuestring, "alert") == 0) out_alert->pet_action = PET_ALERT;
            else if (strcmp(pet_action->valuestring, "sleep") == 0) out_alert->pet_action = PET_SLEEP;
            else out_alert->pet_action = PET_IDLE;
        }

        cJSON *dur = cJSON_GetObjectItem(data, "duration_sec");
        if (cJSON_IsNumber(dur)) {
            out_alert->duration_sec = dur->valueint;
        } else {
            out_alert->duration_sec = 8;
        }

        cJSON *interactive = cJSON_GetObjectItem(data, "interactive");
        if (cJSON_IsObject(interactive)) {
            cJSON *target_url = cJSON_GetObjectItem(interactive, "target_url");
            if (cJSON_IsString(target_url) && target_url->valuestring) {
                strncpy(out_alert->action_url, target_url->valuestring, sizeof(out_alert->action_url) - 1);
            }
        }
    }
    cJSON_Delete(root);
}

static void parse_down_metrics(const char *json_str, metrics_payload_t *out_metrics) {
    if (!json_str || !out_metrics) return;
    memset(out_metrics, 0, sizeof(*out_metrics));

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGW(TAG, "Failed to parse JSON in /down/metrics");
        return;
    }

    cJSON *ts = cJSON_GetObjectItem(root, "timestamp");
    if (cJSON_IsNumber(ts)) {
        out_metrics->sampled_at = (long long)ts->valuedouble;
    } else {
        out_metrics->sampled_at = (long long)time(NULL);
    }
    out_metrics->ttl_sec = 3600;

    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (cJSON_IsObject(data)) {
        cJSON *is_mock = cJSON_GetObjectItem(data, "is_mock");
        bool mock_val = cJSON_IsTrue(is_mock);

        cJSON *bars = cJSON_GetObjectItem(data, "bars");
        if (cJSON_IsArray(bars)) {
            int count = cJSON_GetArraySize(bars);
            if (count > 3) count = 3;
            out_metrics->item_count = count;

            for (int i = 0; i < count; i++) {
                cJSON *bar = cJSON_GetArrayItem(bars, i);
                if (!cJSON_IsObject(bar)) continue;

                metric_item_t *item = &out_metrics->items[i];
                item->is_mock = mock_val;

                cJSON *id = cJSON_GetObjectItem(bar, "id");
                if (cJSON_IsString(id) && id->valuestring) {
                    strncpy(item->key, id->valuestring, sizeof(item->key) - 1);
                }

                cJSON *label = cJSON_GetObjectItem(bar, "label");
                if (cJSON_IsString(label) && label->valuestring) {
                    strncpy(item->label, label->valuestring, sizeof(item->label) - 1);
                }

                cJSON *cur = cJSON_GetObjectItem(bar, "current");
                if (cJSON_IsNumber(cur)) item->current = cur->valuedouble;

                cJSON *limit = cJSON_GetObjectItem(bar, "limit");
                if (cJSON_IsNumber(limit)) item->total = limit->valuedouble;

                cJSON *unit = cJSON_GetObjectItem(bar, "unit");
                if (cJSON_IsString(unit) && unit->valuestring) {
                    strncpy(item->unit, unit->valuestring, sizeof(item->unit) - 1);
                }

                cJSON *percent = cJSON_GetObjectItem(bar, "percent");
                if (cJSON_IsNumber(percent)) item->percent = percent->valueint;

                cJSON *dir = cJSON_GetObjectItem(bar, "direction");
                if (cJSON_IsString(dir) && dir->valuestring) {
                    if (strcmp(dir->valuestring, "remaining") == 0) {
                        item->direction = METRIC_DIR_REMAINING;
                    } else {
                        item->direction = METRIC_DIR_USED;
                    }
                }

                cJSON *sampled = cJSON_GetObjectItem(bar, "sampled_at");
                if (cJSON_IsNumber(sampled)) item->sampled_at = (long long)sampled->valuedouble;
                else item->sampled_at = out_metrics->sampled_at;

                cJSON *ttl = cJSON_GetObjectItem(bar, "ttl_sec");
                if (cJSON_IsNumber(ttl)) item->ttl_sec = ttl->valueint;
                else item->ttl_sec = 3600;

                cJSON *status = cJSON_GetObjectItem(bar, "status");
                if (cJSON_IsString(status) && status->valuestring) {
                    if (strcmp(status->valuestring, "stale") == 0) item->status = METRIC_STATUS_EXHAUSTED;
                    else if (strcmp(status->valuestring, "warning") == 0) item->status = METRIC_STATUS_WARNING;
                    else item->status = METRIC_STATUS_NORMAL;
                }
            }
        }
    }
    cJSON_Delete(root);
}

static void parse_down_state(const char *json_str, work_mode_t *out_mode) {
    if (!json_str || !out_mode) return;
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return;

    cJSON *data = cJSON_GetObjectItem(root, "data");
    const char *mode_str = NULL;
    if (cJSON_IsObject(data)) {
        cJSON *wm = cJSON_GetObjectItem(data, "work_mode");
        if (cJSON_IsString(wm) && wm->valuestring) {
            mode_str = wm->valuestring;
        }
    }
    if (!mode_str) {
        cJSON *wm = cJSON_GetObjectItem(root, "work_mode");
        if (cJSON_IsString(wm) && wm->valuestring) mode_str = wm->valuestring;
    }

    if (mode_str) {
        if (strcmp(mode_str, "coding") == 0) *out_mode = WORK_MODE_CODING;
        else if (strcmp(mode_str, "meeting") == 0) *out_mode = WORK_MODE_MEETING;
        else if (strcmp(mode_str, "sleep") == 0) *out_mode = WORK_MODE_SLEEP;
        else *out_mode = WORK_MODE_IDLE;
    }
    cJSON_Delete(root);
}

// ============================================================================
// 网络与 MQTT 事件处理 (R4-01: 完整初始化 Wi-Fi、MQTT 客户端与事件订阅)
// ============================================================================

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    (void)arg;
    (void)event_data;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi started, connecting to %s...", CONFIG_SPRITE_WIFI_SSID);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
        ESP_LOGW(TAG, "Wi-Fi disconnected, reconnecting in 2s...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Wi-Fi Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_connected = true;

        // 启动 SNTP 同步真实 Unix 时间戳 (R4-03)
        sync_time_via_sntp();

        // 启动 MQTT 客户端
        if (s_mqtt_client) {
            esp_mqtt_client_start(s_mqtt_client);
        }
    }
}

static void sync_time_via_sntp(void) {
    static bool s_sntp_initialized = false;
    if (!s_sntp_initialized) {
        ESP_LOGI(TAG, "Initializing SNTP for Unix timestamp sync...");
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, CONFIG_SPRITE_SNTP_SERVER);
        esp_sntp_setservername(1, "time.apple.com");
        esp_sntp_init();
        s_sntp_initialized = true;
    }
}

static bool __attribute__((unused)) is_time_synced(void) {
    time_t now = time(NULL);
    if (now < 1700000000LL) {
        return false;
    }
    return (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED);
}

static void wifi_init_sta(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, CONFIG_SPRITE_WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, CONFIG_SPRITE_WIFI_PASSWORD, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Wi-Fi STA mode initialized.");
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    (void)handler_args;
    (void)base;
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED: {
            s_mqtt_connected = true;
            ESP_LOGI(TAG, "MQTT Connected to Broker: %s", CONFIG_SPRITE_MQTT_BROKER_URL);

            // 订阅下行通道
            char sub_topic[128];
            snprintf(sub_topic, sizeof(sub_topic), "sprite/device/%s/down/#", CONFIG_SPRITE_DEVICE_ID);
            int sub_msg_id = esp_mqtt_client_subscribe(s_mqtt_client, sub_topic, 1);
            ESP_LOGI(TAG, "Subscribed to %s (msg_id=%d)", sub_topic, sub_msg_id);

            // 发布上线通知 (SPEC-002 第 3.7 节)
            char status_topic[128];
            snprintf(status_topic, sizeof(status_topic), "sprite/device/%s/up/status", CONFIG_SPRITE_DEVICE_ID);
            time_t now = time(NULL);
            char online_payload[192];
            snprintf(online_payload, sizeof(online_payload),
                     "{\"type\":\"status_lifecycle\",\"version\":1,\"status\":\"online\",\"client_version\":\"1.4.0\",\"timestamp\":%lld}",
                     (long long)now);
            esp_mqtt_client_publish(s_mqtt_client, status_topic, online_payload, 0, 1, 1);
            break;
        }
        case MQTT_EVENT_DISCONNECTED:
            s_mqtt_connected = false;
            mqtt_reassembler_reset(&s_mqtt_reassembler);
            ESP_LOGW(TAG, "MQTT Disconnected");
            break;
        case MQTT_EVENT_DATA: {
            const char *full_topic = NULL;
            const char *full_payload = NULL;
            int full_payload_len = 0;

            // 基于报文声明总长度进行有界重组，支持 ESP-MQTT 跨包分片报文 (R5-02)
            mqtt_reassemble_result_t res = mqtt_reassembler_feed(
                &s_mqtt_reassembler,
                event->topic,
                event->topic_len,
                event->data,
                event->data_len,
                event->total_data_len,
                event->current_data_offset,
                &full_topic,
                &full_payload,
                &full_payload_len
            );

            if (res == MQTT_REASSEMBLE_NEED_MORE) {
                ESP_LOGD(TAG, "MQTT fragmented payload chunk received: offset=%d len=%d total=%d",
                         event->current_data_offset, event->data_len, event->total_data_len);
                break;
            } else if (res != MQTT_REASSEMBLE_COMPLETE) {
                ESP_LOGW(TAG, "MQTT payload reassembly rejected: res=%d, offset=%d, len=%d, total=%d",
                         res, event->current_data_offset, event->data_len, event->total_data_len);
                break;
            }

            // 完整数据包已拼装完毕，执行真实下行业务解析
            if (strstr(full_topic, "/down/alert")) {
                ESP_LOGI(TAG, "Received complete down/alert payload (%d bytes)", full_payload_len);
                alert_payload_t alert;
                parse_down_alert(full_payload, &alert);
                if (alert.event_id[0] != '\0') {
                    int admitted = alert_scheduler_push(&s_alert_sched, &alert, s_current_mode);
                    ESP_LOGI(TAG, "Alert %s (prio=%d, dur=%ds) admitted to scheduler (res=%d)",
                             alert.event_id, alert.priority, alert.duration_sec, admitted);
                }
            } else if (strstr(full_topic, "/down/metrics")) {
                ESP_LOGI(TAG, "Received complete down/metrics payload (%d bytes)", full_payload_len);
                metrics_payload_t metrics;
                parse_down_metrics(full_payload, &metrics);
                if (metrics.item_count > 0) {
                    s_last_metrics = metrics;
                    s_has_metrics = true;
                    time_t now_sec = time(NULL);
                    ui_metrics_compute_render_model_ex(&s_last_metrics, (long long)now_sec, s_metric_view_mode, &s_metrics_model);
                    ESP_LOGI(TAG, "Updated metrics model with %d bars", metrics.item_count);
                }
            } else if (strstr(full_topic, "/down/state")) {
                ESP_LOGI(TAG, "Received complete down/state payload: %s", full_payload);
                work_mode_t new_mode = s_current_mode;
                parse_down_state(full_payload, &new_mode);
                if (new_mode == WORK_MODE_SLEEP && s_current_mode != WORK_MODE_SLEEP) {
                    alert_scheduler_clear_medium(&s_alert_sched);
                }
                s_current_mode = new_mode;
                ESP_LOGI(TAG, "Updated work mode: %d", (int)s_current_mode);
            } else if (strstr(full_topic, "/down/dismiss")) {
                time_t now_sec = time(NULL);
                alert_scheduler_dismiss(&s_alert_sched, (long long)now_sec, s_current_mode);
                ESP_LOGI(TAG, "Alert dismissed via MQTT");
            }
            break;
        }
        default:
            break;
    }
}

static void mqtt_app_start(void) {
    char lwt_topic[128];
    snprintf(lwt_topic, sizeof(lwt_topic), "sprite/device/%s/up/status", CONFIG_SPRITE_DEVICE_ID);
    const char *lwt_msg = "{\"type\":\"status_lifecycle\",\"version\":1,\"status\":\"offline\",\"reason\":\"connection_lost\",\"timestamp\":0}";

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_SPRITE_MQTT_BROKER_URL,
        .credentials.client_id = "dev_" CONFIG_SPRITE_DEVICE_ID,
        .session.last_will.topic = lwt_topic,
        .session.last_will.msg = lwt_msg,
        .session.last_will.msg_len = strlen(lwt_msg),
        .session.last_will.qos = 1,
        .session.last_will.retain = 1,
        .session.keepalive = 60,
        .network.reconnect_timeout_ms = 2000,
        .network.disable_auto_reconnect = false,
        .buffer.size = 2048,
    };

#ifdef CONFIG_SPRITE_MQTT_CA_CERT
    if (strlen(CONFIG_SPRITE_MQTT_CA_CERT) > 0) {
        mqtt_cfg.broker.verification.certificate = CONFIG_SPRITE_MQTT_CA_CERT;
        mqtt_cfg.broker.verification.certificate_len = strlen(CONFIG_SPRITE_MQTT_CA_CERT) + 1;
        ESP_LOGI(TAG, "Configured custom CA certificate for MQTT TLS verification");
    } else {
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
        ESP_LOGI(TAG, "Attached ESP x509 certificate bundle for MQTT TLS verification");
#endif
    }
#elif CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
    mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    ESP_LOGI(TAG, "Attached ESP x509 certificate bundle for MQTT TLS verification");
#endif

#ifdef CONFIG_SPRITE_MQTT_BROKER_COMMON_NAME
    if (strlen(CONFIG_SPRITE_MQTT_BROKER_COMMON_NAME) > 0) {
        mqtt_cfg.broker.verification.common_name = CONFIG_SPRITE_MQTT_BROKER_COMMON_NAME;
    }
#endif

    // 严禁关闭服务端验证，严格校验主机名与证书有效性 (R5-01)
    mqtt_cfg.broker.verification.skip_cert_common_name_check = false;

    if (strlen(CONFIG_SPRITE_MQTT_USERNAME) > 0) {
        mqtt_cfg.credentials.username = CONFIG_SPRITE_MQTT_USERNAME;
    }
    if (strlen(CONFIG_SPRITE_MQTT_PASSWORD) > 0) {
        mqtt_cfg.credentials.authentication.password = CONFIG_SPRITE_MQTT_PASSWORD;
    }

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    ESP_LOGI(TAG, "MQTT client registered for %s", CONFIG_SPRITE_MQTT_BROKER_URL);
}

// ============================================================================
// 触摸轮询任务 (R4-05: 原生电容触摸接入，根据验收门禁配置上报行为)
// ============================================================================

static void touch_poll_task(void *pvParameters) {
    (void)pvParameters;
    while (1) {
#if CONFIG_SPRITE_ENABLE_TOUCH_ACTIONS
        vTaskDelay(pdMS_TO_TICKS(10));
        long long now_ms = esp_timer_get_time() / 1000;
        bool pin_high = bsp_touch_read();
        touch_gesture_t gesture = touch_button_update(&s_touch_button, pin_high, now_ms);

        const alert_payload_t* active = alert_scheduler_get_active(&s_alert_sched);

        if (gesture == GESTURE_TAP) {
            ESP_LOGI(TAG, "[Touch] GESTURE_TAP detected");
            const alert_payload_t* active = alert_scheduler_get_active(&s_alert_sched);
            if (active) {
                // 协议动作受时钟同步与网络在线状态强约束 (R5-04)
                time_t unix_now = time(NULL);
                bool time_synced = is_time_synced();
                char action_json[512] = {0};
                touch_alert_action_result_t act_res = touch_action_handle_tap(
                    &s_alert_sched,
                    CONFIG_SPRITE_DEVICE_ID,
                    time_synced,
                    (long long)unix_now,
                    s_mqtt_connected,
                    s_current_mode,
                    action_json,
                    sizeof(action_json)
                );

                if (act_res == TOUCH_ALERT_ACT_DISMISSED_SENT) {
                    if (s_mqtt_client) {
                        char up_topic[128];
                        snprintf(up_topic, sizeof(up_topic), "sprite/device/%s/up/action", CONFIG_SPRITE_DEVICE_ID);
                        int pub_msg_id = esp_mqtt_client_publish(s_mqtt_client, up_topic, action_json, 0, 1, 0);
                        ESP_LOGI(TAG, "Published action JSON (msg_id=%d) to %s: %s", pub_msg_id, up_topic, action_json);
                    }
                } else if (act_res == TOUCH_ALERT_ACT_BLOCKED_UNSYNCED) {
                    ESP_LOGW(TAG, "Touch action blocked: SNTP clock not synced yet (unix_now=%lld); alert retained for retry", (long long)unix_now);
                } else if (act_res == TOUCH_ALERT_ACT_BLOCKED_OFFLINE) {
                    ESP_LOGW(TAG, "Touch action blocked: MQTT disconnected; alert retained for retry");
                }
            } else {
                // 主屏状态下 Tap: 切换三行度量指标数值 / 百分比模式 (SPEC-003 第 6 节)
                s_metric_view_mode = (s_metric_view_mode == UI_METRIC_VIEW_VALUE) ? UI_METRIC_VIEW_PERCENT : UI_METRIC_VIEW_VALUE;
                ESP_LOGI(TAG, "[Touch] Toggled metric view mode to %s", s_metric_view_mode == UI_METRIC_VIEW_PERCENT ? "PERCENT" : "VALUE");
                if (s_has_metrics) {
                    time_t unix_now = time(NULL);
                    ui_metrics_compute_render_model_ex(&s_last_metrics, (long long)unix_now, s_metric_view_mode, &s_metrics_model);
                }
            }
        } else if (gesture == GESTURE_LONG_PRESS) {
            ESP_LOGI(TAG, "[Touch] GESTURE_LONG_PRESS detected");
            time_t unix_now = time(NULL);
            if (active) {
                // 弹窗状态下长按: 静默关闭，不上报动作 (SPEC-003 第 6 节)
                alert_scheduler_dismiss(&s_alert_sched, (long long)unix_now, s_current_mode);
            } else {
                // 主屏状态下长按: 切换 30 分钟本地临时静音
                if (unix_now < s_mute_until_sec) {
                    s_mute_until_sec = 0;
                    ESP_LOGI(TAG, "[Touch] Temporary mute disabled");
                } else {
                    s_mute_until_sec = unix_now + 1800;
                    ESP_LOGI(TAG, "[Touch] Temporary mute enabled for 30 min (until %lld)", s_mute_until_sec);
                }
            }
        }
#else
        // SPEC-005 第 3.0 节 & SPEC-007: 产品入口在触摸交互验收通过前保持禁用，周期记录传感器原始电容读数
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint32_t raw_cap = bsp_touch_get_raw();
        ESP_LOGD(TAG, "[Touch] Native capacitive raw=%lu (action reporting disabled pending calibration)", (unsigned long)raw_cap);
#endif
    }
}

// ============================================================================
// ST7789 UI 渲染任务 (R4-04: 将共享状态与指标真实绘制上屏)
// ============================================================================

static void render_screen_st7789(const alert_payload_t *active_alert, time_t now_sec) {
    // 1. 顶部状态栏 (y: 0..23)
    bsp_st7789_fill_rect(0, 0, LCD_WIDTH, 24, COLOR_RGB565_BG_DARK);

    // 状态指示圆点
    uint16_t dot_color = COLOR_RGB565_GRAY;
    const char *mode_tag = "IDLE";
    if (s_current_mode == WORK_MODE_CODING) {
        dot_color = COLOR_RGB565_GREEN;
        mode_tag = "CODING";
    } else if (s_current_mode == WORK_MODE_MEETING) {
        dot_color = COLOR_RGB565_DARKORANGE;
        mode_tag = "MEETING";
    } else if (s_current_mode == WORK_MODE_SLEEP) {
        dot_color = COLOR_RGB565_GRAY;
        mode_tag = "SLEEP";
    }

    bsp_st7789_fill_rect(16, 8, 12, 12, dot_color);
    bsp_st7789_draw_string(36, 8, mode_tag, COLOR_RGB565_GRAY, COLOR_RGB565_BG_DARK);

    // Wi-Fi / MQTT 在线状态
    if (s_mqtt_connected) {
        bsp_st7789_draw_string(105, 8, "[OK]", COLOR_RGB565_GREEN, COLOR_RGB565_BG_DARK);
    } else if (s_wifi_connected) {
        bsp_st7789_draw_string(105, 8, "[WIFI]", COLOR_RGB565_YELLOW, COLOR_RGB565_BG_DARK);
    } else {
        bsp_st7789_draw_string(105, 8, "[OFF]", COLOR_RGB565_RED, COLOR_RGB565_BG_DARK);
    }

    // 静音标志
    if (now_sec < s_mute_until_sec) {
        bsp_st7789_draw_string(155, 8, "[MUTE]", COLOR_RGB565_YELLOW, COLOR_RGB565_BG_DARK);
    }

    // 2. 舞台区域 (y: 24..167)
    if (active_alert) {
        // 告警弹窗覆盖层 (RR08, R4-04: 渲染标题、摘要、倒计时与手动确认)
        uint16_t alert_bg = (active_alert->priority == PRIORITY_HIGH) ? COLOR_RGB565_DARKRED : COLOR_RGB565_DARKORANGE;
        bsp_st7789_fill_rect(12, 30, 216, 132, alert_bg);

        // 绘制标题
        bsp_st7789_draw_string(20, 38, active_alert->title, COLOR_RGB565_WHITE, alert_bg);

        // 绘制摘要 (最多两行，自动换行)
        bsp_st7789_draw_string_wrapped(20, 52, 200, 24, active_alert->summary, COLOR_RGB565_WHITE, alert_bg);

        // 倒计时与交互提示 (R3-07, R4-04)
        if (active_alert->priority == PRIORITY_HIGH) {
            // High 优先级必须手动确认，不显倒计时
            bsp_st7789_draw_string(20, 78, "Action: Manual Ack Required", COLOR_RGB565_WHITE, alert_bg);
        } else {
            // Medium 优先级显示调度器剩余展示时长
            char cd_buf[32];
            int rem_sec = alert_scheduler_get_remaining_sec(&s_alert_sched);
            if (rem_sec < 0) rem_sec = 0;
            snprintf(cd_buf, sizeof(cd_buf), "Duration: %ds", rem_sec);
            bsp_st7789_draw_string(20, 78, cd_buf, COLOR_RGB565_GRAY, alert_bg);
        }

        // 底部按键提示
        bsp_st7789_draw_string(20, 142, "Tap:Open | Hold:Dismiss", COLOR_RGB565_WHITE, alert_bg);
    } else {
        // 正常待机/工作状态桌面宠物动画区
        bsp_st7789_fill_rect(0, 24, LCD_WIDTH, 144, COLOR_RGB565_BLACK);
        bsp_st7789_fill_rect(56, 32, 128, 128, COLOR_RGB565_DARKGRAY);
        bsp_st7789_draw_string(90, 88, "( ^_^ )", COLOR_RGB565_WHITE, COLOR_RGB565_DARKGRAY);
    }

    // 3. 底部三行度量指标看板 (y: 168..239, R4-04: 将 s_metrics_model 渲染至屏幕)
    bsp_st7789_fill_rect(0, 168, LCD_WIDTH, 72, COLOR_RGB565_BG_DARK);

    for (int i = 0; i < 3; i++) {
        int y_pos = 172 + i * 23;
        // 进度条轨道背景
        bsp_st7789_fill_rect(10, y_pos, 220, 18, COLOR_RGB565_DARKGRAY);

        if (i < s_metrics_model.count) {
            const ui_metric_row_render_t* row = &s_metrics_model.rows[i];
            int bar_w = (int)(row->progress_ratio * 220.0f);
            if (bar_w > 220) bar_w = 220;
            if (bar_w < 0) bar_w = 0;

            uint16_t bar_color;
            if (row->style == UI_METRIC_STYLE_STALE) {
                bar_color = COLOR_RGB565_GRAY; // Stale 灰显
            } else if (i == 0) {
                bar_color = COLOR_RGB565_GREEN;
            } else if (i == 1) {
                bar_color = COLOR_RGB565_BLUE;
            } else {
                bar_color = COLOR_RGB565_PURPLE;
            }

            if (bar_w > 0) {
                bsp_st7789_fill_rect(10, y_pos, bar_w, 18, bar_color);
            }

            // 绘制指标文本 (包含 UTF-8 中文与单位 ¥/天)
            uint16_t text_col = (row->style == UI_METRIC_STYLE_STALE) ? COLOR_RGB565_GRAY : COLOR_RGB565_WHITE;
            uint16_t text_bg = (bar_w > 10) ? bar_color : COLOR_RGB565_DARKGRAY;
            bsp_st7789_draw_string(14, y_pos + 5, row->rendered_text, text_col, text_bg);
        }
    }
}

// UI 渲染与告警视听调度任务 (50ms 周期)
static void ui_and_alert_task(void *pvParameters) {
    (void)pvParameters;
    time_t last_tick_sec = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(50));
        time_t now_sec = time(NULL);

        // 每秒驱动一次调度器与倒计时
        if (now_sec != last_tick_sec) {
            last_tick_sec = now_sec;
            alert_scheduler_tick(&s_alert_sched, (long long)now_sec, s_current_mode);
            if (s_has_metrics) {
                ui_metrics_compute_render_model_ex(&s_last_metrics, (long long)now_sec, s_metric_view_mode, &s_metrics_model);
            }

            const alert_payload_t* active = alert_scheduler_get_active(&s_alert_sched);
            render_screen_st7789(active, now_sec);
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
        } else {
            s_last_played_event_id[0] = '\0';
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
    bsp_touch_init();

    // 3. 初始化核心状态机与调度器
    ui_layout_init();
    alert_scheduler_init(&s_alert_sched);
    touch_button_init(&s_touch_button);
    mqtt_reassembler_init(&s_mqtt_reassembler);
    bsp_st7789_set_backlight(80);

    // 播报开机提示音
    bsp_es8311_play_tone(880, 150);

    // 4. 启动网络与 MQTT 协议栈 (R4-01)
    wifi_init_sta();
    mqtt_app_start();

    ESP_LOGI(TAG, "Desk Sprite ESP32-S3 firmware initialized successfully.");

    // 5. 创建 FreeRTOS 任务
    xTaskCreate(touch_poll_task, "touch_task", 4096, NULL, 5, NULL);
    xTaskCreate(ui_and_alert_task, "ui_alert_task", 8192, NULL, 4, NULL);

    ESP_LOGI(TAG, "FreeRTOS tasks created. System running.");
}
