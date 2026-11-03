#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "sprite_types.h"
#include "mqtt_reassembler.h"
#include "alert_filter.h"
#include "alert_queue.h"
#include "cJSON.h"

static void parse_alert_json(const char *json_str, alert_payload_t *out_alert) {
    if (!json_str || !out_alert) return;
    memset(out_alert, 0, sizeof(*out_alert));

    cJSON *root = cJSON_Parse(json_str);
    assert(root != NULL);

    cJSON *ev_id = cJSON_GetObjectItem(root, "event_id");
    if (cJSON_IsString(ev_id) && ev_id->valuestring) {
        strncpy(out_alert->event_id, ev_id->valuestring, sizeof(out_alert->event_id) - 1);
    }

    cJSON *exp = cJSON_GetObjectItem(root, "expire_at");
    if (cJSON_IsNumber(exp)) {
        out_alert->expire_at = (long long)exp->valuedouble;
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

static void test_mqtt_single_packet() {
    mqtt_reassembler_t r;
    mqtt_reassembler_init(&r);

    const char *topic = "sprite/device/esp32_c049ef/down/alert";
    const char *payload = "{\"type\":\"alert\",\"event_id\":\"evt_1\",\"timestamp\":1700000000,\"expire_at\":1700000060,\"data\":{\"title\":\"测试\",\"summary\":\"简单告警\"}}";
    int plen = strlen(payload);

    const char *out_topic = NULL;
    const char *out_payload = NULL;
    int out_len = 0;

    mqtt_reassemble_result_t res = mqtt_reassembler_feed(
        &r, topic, strlen(topic), payload, plen, plen, 0,
        &out_topic, &out_payload, &out_len
    );

    assert(res == MQTT_REASSEMBLE_COMPLETE);
    assert(out_topic != NULL);
    assert(strcmp(out_topic, topic) == 0);
    assert(out_payload != NULL);
    assert(strcmp(out_payload, payload) == 0);
    assert(out_len == plen);

    printf("[PASS] test_mqtt_single_packet\n");
}

static void test_mqtt_fragmented_1167_bytes() {
    mqtt_reassembler_t r;
    mqtt_reassembler_init(&r);

    // 构造合法 1167 字节符合 SPEC-002 / AlertPayload Schema 的下行告警 JSON (R5-02)
    // 包含 24 个中文字符标题 (72 字节)
    const char *title_24_chars = "紧急告警测试：系统核心数据库连接超时需工程师核查";
    // 包含 48 个中文字符摘要 (144 字节)
    const char *summary_48_chars = "这是一条用于测试第五轮长报文分片解析的完整摘要文本，包含详细错误上下文以及重试策略提示信息，请工程师核查。";

    const char *full_json = "{\"type\": \"alert\", \"event_id\": \"evt_fragment_1167_test_id\", \"timestamp\": 1700000000, \"expire_at\": 1700000060, \"data\": {\"category\": \"database_alerts\", \"priority\": \"high\", \"title\": \"紧急告警测试：系统核心数据库连接超时需工程师核查\", \"summary\": \"这是一条用于测试第五轮长报文分片解析的完整摘要文本，包含详细错误上下文以及重试策略提示信息，请工程师核查。\", \"source\": \"ci_monitor\", \"sound\": \"alert_beep\", \"pet_action\": \"alert\", \"duration_sec\": 8, \"interactive\": {\"tap_action\": \"open_url\", \"target_host\": \"mac_host_1xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\", \"target_url\": \"https://example.com/alerts/detail?query=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}}}";

    int target_total = strlen(full_json);
    assert(target_total == 1167);

    // 模拟 ESP-MQTT 分片下发: 960 字节 + 207 字节
    const char *topic = "sprite/device/esp32_c049ef/down/alert";
    int chunk1_len = 960;
    int chunk2_len = 207;
    assert(chunk1_len + chunk2_len == target_total);

    const char *out_topic = NULL;
    const char *out_payload = NULL;
    int out_len = 0;

    // 1. 首片片段: offset=0, len=960, total=1167, 携带完整 topic
    mqtt_reassemble_result_t res1 = mqtt_reassembler_feed(
        &r,
        topic,
        strlen(topic),
        full_json,
        chunk1_len,
        target_total,
        0, /* offset */
        &out_topic,
        &out_payload,
        &out_len
    );
    assert(res1 == MQTT_REASSEMBLE_NEED_MORE);
    assert(out_topic == NULL);
    assert(out_payload == NULL);

    // 2. 第二片片段: offset=960, len=207, total=1167, topic 为 NULL / len=0 (ESP-MQTT 实际分片行为)
    mqtt_reassemble_result_t res2 = mqtt_reassembler_feed(
        &r,
        NULL,
        0,
        full_json + chunk1_len,
        chunk2_len,
        target_total,
        chunk1_len, /* offset=960 */
        &out_topic,
        &out_payload,
        &out_len
    );
    assert(res2 == MQTT_REASSEMBLE_COMPLETE);
    assert(out_topic != NULL);
    assert(strcmp(out_topic, topic) == 0); // 成功保留首片 topic
    assert(out_payload != NULL);
    assert(out_len == target_total);
    assert(strcmp(out_payload, full_json) == 0);

    // 3. 验证业务集成: 完整输入交由 cJSON 解析并入队调度器
    alert_payload_t alert;
    parse_alert_json(out_payload, &alert);
    assert(strcmp(alert.event_id, "evt_fragment_1167_test_id") == 0);
    assert(alert.priority == PRIORITY_HIGH);
    assert(strcmp(alert.title, title_24_chars) == 0);
    assert(strcmp(alert.summary, summary_48_chars) == 0);
    assert(strlen(alert.action_url) == 512);

    alert_scheduler_t sched;
    alert_scheduler_init(&sched);
    int admitted = alert_scheduler_push(&sched, &alert, WORK_MODE_IDLE);
    assert(admitted > 0);
    assert(sched.has_active);
    const alert_payload_t *active = alert_scheduler_get_active(&sched);
    assert(active != NULL);
    assert(strcmp(active->event_id, alert.event_id) == 0);

    printf("[PASS] test_mqtt_fragmented_1167_bytes (active=1, title/summary/url preserved)\n");
}

static void test_mqtt_edge_cases() {
    mqtt_reassembler_t r;
    mqtt_reassembler_init(&r);

    const char *topic = "sprite/device/esp32_c049ef/down/alert";
    char dummy[100];
    memset(dummy, 'x', sizeof(dummy));

    // 1. 超过 2048 字节超长报文: 必须被拒绝 (R5-02)
    mqtt_reassemble_result_t res = mqtt_reassembler_feed(
        &r, topic, strlen(topic), dummy, 100, 2049, 0, NULL, NULL, NULL
    );
    assert(res == MQTT_REASSEMBLE_ERR_OVERSIZED);

    // 2. 偏移不匹配: 期望 0 但传入 offset=50
    res = mqtt_reassembler_feed(
        &r, topic, strlen(topic), dummy, 50, 200, 50, NULL, NULL, NULL
    );
    assert(res == MQTT_REASSEMBLE_ERR_OFFSET);

    // 3. 分片乱序: 首片 100 字节，后续分片 offset=120 (跳过了 20 字节)
    res = mqtt_reassembler_feed(
        &r, topic, strlen(topic), dummy, 100, 300, 0, NULL, NULL, NULL
    );
    assert(res == MQTT_REASSEMBLE_NEED_MORE);

    res = mqtt_reassembler_feed(
        &r, NULL, 0, dummy, 50, 300, 120, NULL, NULL, NULL
    );
    assert(res == MQTT_REASSEMBLE_ERR_OFFSET);

    // 4. 断网重置
    mqtt_reassembler_reset(&r);
    assert(r.in_progress == false);
    assert(r.total_len == 0);

    printf("[PASS] test_mqtt_edge_cases\n");
}

int main(void) {
    printf("Running test_mqtt_reassembly...\n");
    test_mqtt_single_packet();
    test_mqtt_fragmented_1167_bytes();
    test_mqtt_edge_cases();
    printf("All test_mqtt_reassembly tests passed successfully!\n");
    return 0;
}
