#include "sim_mqtt.h"
#include "action_reporter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef HAS_MOSQUITTO
#include <mosquitto.h>
static struct mosquitto* g_mosq = NULL;
#endif

static sim_mqtt_config_t g_config;
static bool g_initialized = false;

// 简单的 JSON 字段提取辅助函数
static const char* json_get_string(const char* json, const char* key, char* out, size_t max_len) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char* pos = strstr(json, pattern);
    if (!pos) return NULL;
    pos += strlen(pattern);
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') pos++;
    if (*pos == '\"') {
        pos++;
        size_t idx = 0;
        while (*pos && *pos != '\"' && idx + 1 < max_len) {
            out[idx++] = *pos++;
        }
        out[idx] = '\0';
        return out;
    }
    return NULL;
}

static long long json_get_int64(const char* json, const char* key, long long default_val) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char* pos = strstr(json, pattern);
    if (!pos) return default_val;
    pos += strlen(pattern);
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') pos++;
    return atoll(pos);
}

static double json_get_double(const char* json, const char* key, double default_val) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char* pos = strstr(json, pattern);
    if (!pos) return default_val;
    pos += strlen(pattern);
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') pos++;
    return atof(pos);
}

void sim_mqtt_handle_message(const char* topic, const char* payload) {
    if (!payload) return;

    if (strstr(topic, "/down/alert")) {
        if (g_config.on_alert) {
            alert_payload_t alert;
            memset(&alert, 0, sizeof(alert));

            json_get_string(payload, "event_id", alert.event_id, sizeof(alert.event_id));
            json_get_string(payload, "title", alert.title, sizeof(alert.title));
            json_get_string(payload, "summary", alert.summary, sizeof(alert.summary));
            json_get_string(payload, "target_url", alert.action_url, sizeof(alert.action_url));
            if (alert.action_url[0] == '\0') {
                json_get_string(payload, "url", alert.action_url, sizeof(alert.action_url));
            }

            char prio_str[16] = {0};
            json_get_string(payload, "priority", prio_str, sizeof(prio_str));
            if (strcmp(prio_str, "high") == 0) alert.priority = PRIORITY_HIGH;
            else if (strcmp(prio_str, "medium") == 0) alert.priority = PRIORITY_MEDIUM;
            else alert.priority = PRIORITY_LOW;

            char sound_str[32] = {0};
            json_get_string(payload, "sound", sound_str, sizeof(sound_str));
            if (strcmp(sound_str, "ding") == 0) alert.sound = SOUND_DING;
            else if (strcmp(sound_str, "chime") == 0) alert.sound = SOUND_CHIME;
            else if (strcmp(sound_str, "cheer") == 0) alert.sound = SOUND_CHEER;
            else if (strcmp(sound_str, "sigh") == 0) alert.sound = SOUND_SIGH;
            else if (strcmp(sound_str, "alert_beep") == 0) alert.sound = SOUND_ALERT_BEEP;
            else alert.sound = SOUND_NONE;

            char mood_str[32] = {0};
            json_get_string(payload, "pet_action", mood_str, sizeof(mood_str));
            if (strcmp(mood_str, "coding") == 0) alert.pet_action = PET_CODING;
            else if (strcmp(mood_str, "meeting") == 0) alert.pet_action = PET_MEETING;
            else if (strcmp(mood_str, "cheer") == 0) alert.pet_action = PET_CHEER;
            else if (strcmp(mood_str, "sigh") == 0) alert.pet_action = PET_SIGH;
            else if (strcmp(mood_str, "alert") == 0) alert.pet_action = PET_ALERT;
            else if (strcmp(mood_str, "sleep") == 0) alert.pet_action = PET_SLEEP;
            else alert.pet_action = PET_IDLE;

            alert.duration_sec = (int)json_get_int64(payload, "duration_sec", 10);
            alert.expire_at = json_get_int64(payload, "expire_at", 0);
            alert.created_at = json_get_int64(payload, "created_at", (long long)time(NULL));

            g_config.on_alert(&alert);
        }
    } else if (strstr(topic, "/down/state")) {
        if (g_config.on_state) {
            char mode_str[32] = {0};
            json_get_string(payload, "work_mode", mode_str, sizeof(mode_str));
            work_mode_t mode = WORK_MODE_IDLE;
            if (strcmp(mode_str, "coding") == 0) mode = WORK_MODE_CODING;
            else if (strcmp(mode_str, "meeting") == 0) mode = WORK_MODE_MEETING;
            else if (strcmp(mode_str, "sleep") == 0) mode = WORK_MODE_SLEEP;

            g_config.on_state(mode);
        }
    } else if (strstr(topic, "/down/metrics")) {
        if (g_config.on_metrics) {
            metrics_payload_t metrics;
            memset(&metrics, 0, sizeof(metrics));
            metrics.ttl_sec = (int)json_get_int64(payload, "ttl_sec", 3600);
            metrics.sampled_at = json_get_int64(payload, "sampled_at", (long long)time(NULL));

            // 解析 data.bars 数组 (R09 核心修复)
            const char* bars_pos = strstr(payload, "\"bars\":");
            if (bars_pos) {
                const char* arr_start = strchr(bars_pos, '[');
                const char* arr_end = arr_start ? strchr(arr_start, ']') : NULL;
                if (arr_start && arr_end) {
                    const char* cur = arr_start;
                    while (cur < arr_end && metrics.item_count < 3) {
                        const char* obj_start = strchr(cur, '{');
                        if (!obj_start || obj_start > arr_end) break;
                        const char* obj_end = strchr(obj_start, '}');
                        if (!obj_end || obj_end > arr_end) break;

                        size_t obj_len = (size_t)(obj_end - obj_start + 1);
                        char obj_buf[512];
                        if (obj_len < sizeof(obj_buf)) {
                            memcpy(obj_buf, obj_start, obj_len);
                            obj_buf[obj_len] = '\0';

                            metric_item_t* item = &metrics.items[metrics.item_count];
                            json_get_string(obj_buf, "id", item->key, sizeof(item->key));
                            json_get_string(obj_buf, "label", item->label, sizeof(item->label));
                            item->current = json_get_double(obj_buf, "current", 0.0);
                            item->total = json_get_double(obj_buf, "limit", 100.0);
                            json_get_string(obj_buf, "unit", item->unit, sizeof(item->unit));
                            item->percent = (int)json_get_int64(obj_buf, "percent", 0);

                            char dir_str[16] = {0};
                            json_get_string(obj_buf, "direction", dir_str, sizeof(dir_str));
                            item->direction = (strcmp(dir_str, "remaining") == 0) ? METRIC_DIR_REMAINING : METRIC_DIR_USED;

                            char status_str[16] = {0};
                            json_get_string(obj_buf, "status", status_str, sizeof(status_str));
                            if (strcmp(status_str, "stale") == 0) item->status = METRIC_STATUS_EXHAUSTED;
                            else if (strcmp(status_str, "warning") == 0) item->status = METRIC_STATUS_WARNING;
                            else item->status = METRIC_STATUS_NORMAL;

                            item->sampled_at = json_get_int64(obj_buf, "sampled_at", metrics.sampled_at);
                            item->ttl_sec = (int)json_get_int64(obj_buf, "ttl_sec", metrics.ttl_sec);

                            metrics.item_count++;
                        }
                        cur = obj_end + 1;
                    }
                }
            }
            g_config.on_metrics(&metrics);
        }
    }
}

#ifdef HAS_MOSQUITTO
static void on_mosq_message(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* msg) {
    (void)mosq;
    (void)userdata;
    if (msg && msg->payload) {
        sim_mqtt_handle_message(msg->topic, (const char*)msg->payload);
    }
}
#endif

int sim_mqtt_init(const sim_mqtt_config_t* config) {
    if (!config) return -1;
    memcpy(&g_config, config, sizeof(sim_mqtt_config_t));
    g_initialized = true;

#ifdef HAS_MOSQUITTO
    mosquitto_lib_init();
    char client_id[64];
    snprintf(client_id, sizeof(client_id), "cid_device_%s", config->device_id ? config->device_id : "esp32_dev");
    g_mosq = mosquitto_new(client_id, true, NULL);
    if (!g_mosq) return -1;

    if (config->username && config->password) {
        mosquitto_username_pw_set(g_mosq, config->username, config->password);
    }

    mosquitto_message_callback_set(g_mosq, on_mosq_message);

    int rc = mosquitto_connect(g_mosq, config->broker_host, config->broker_port, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "[Sim MQTT] Connect failed: %s\n", mosquitto_strerror(rc));
        return -1;
    }

    char sub_topic[128];
    snprintf(sub_topic, sizeof(sub_topic), "sprite/device/%s/down/#", config->device_id);
    mosquitto_subscribe(g_mosq, NULL, sub_topic, 1);
    printf("[Sim MQTT] Subscribed to %s\n", sub_topic);
#else
    printf("[Sim MQTT] Initialized in headless/standalone mode for %s\n",
           config->device_id ? config->device_id : "unknown");
#endif
    return 0;
}

int sim_mqtt_loop(int timeout_ms) {
#ifdef HAS_MOSQUITTO
    if (g_mosq) {
        return mosquitto_loop(g_mosq, timeout_ms, 1);
    }
#else
    (void)timeout_ms;
#endif
    return 0;
}

int sim_mqtt_publish_action(const device_action_t* action) {
    if (!action) return -1;
    char topic[128];
    snprintf(topic, sizeof(topic), "sprite/device/%s/up/action", action->device_id);

    char payload[512];
    if (action_reporter_serialize_json(action, payload, sizeof(payload)) != 0) {
        return -1;
    }

#ifdef HAS_MOSQUITTO
    if (g_mosq) {
        return mosquitto_publish(g_mosq, NULL, topic, strlen(payload), payload, 1, false);
    }
#else
    printf("[Sim MQTT Publish] %s -> %s\n", topic, payload);
#endif
    return 0;
}

int sim_mqtt_publish_status(const char* status_str) {
    char topic[128];
    snprintf(topic, sizeof(topic), "sprite/device/%s/up/status", g_config.device_id);

    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"type\":\"status_lifecycle\",\"version\":1,\"status\":\"%s\",\"client_version\":\"1.4.0\",\"timestamp\":%lld}",
             status_str, (long long)time(NULL));

#ifdef HAS_MOSQUITTO
    if (g_mosq) {
        return mosquitto_publish(g_mosq, NULL, topic, strlen(payload), payload, 1, true);
    }
#else
    printf("[Sim MQTT Publish] %s -> %s\n", topic, payload);
#endif
    return 0;
}

void sim_mqtt_destroy(void) {
#ifdef HAS_MOSQUITTO
    if (g_mosq) {
        mosquitto_disconnect(g_mosq);
        mosquitto_destroy(g_mosq);
        g_mosq = NULL;
    }
    mosquitto_lib_cleanup();
#endif
    g_initialized = false;
}
