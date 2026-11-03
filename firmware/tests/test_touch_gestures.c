#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "sprite_types.h"
#include "touch_button.h"
#include "action_reporter.h"
#include "touch_action.h"

static void test_touch_jitter_debounce() {
    touch_button_state_t state;
    touch_button_init(&state);

    // 20ms 抖动毛刺脉冲
    touch_gesture_t g1 = touch_button_update(&state, true, 100);
    assert(g1 == GESTURE_NONE);

    touch_gesture_t g2 = touch_button_update(&state, false, 120);
    assert(g2 == GESTURE_NONE); // 低于 50ms 阈值，被滤除

    printf("[PASS] test_touch_jitter_debounce\n");
}

static void test_touch_tap_detection() {
    touch_button_state_t state;
    touch_button_init(&state);

    // 150ms 标准点动
    touch_button_update(&state, true, 1000);
    touch_gesture_t g = touch_button_update(&state, false, 1150);
    assert(g == GESTURE_TAP);

    printf("[PASS] test_touch_tap_detection\n");
}

static void test_touch_long_press_detection() {
    touch_button_state_t state;
    touch_button_init(&state);

    // 按下起始
    touch_button_update(&state, true, 1000);
    // 持续按住 500ms
    touch_gesture_t g1 = touch_button_update(&state, true, 1500);
    assert(g1 == GESTURE_NONE);

    // 达到 1200ms 阈值 (2200 - 1000 = 1200ms)
    touch_gesture_t g2 = touch_button_update(&state, true, 2200);
    assert(g2 == GESTURE_LONG_PRESS);

    // 继续按住，不再重复触发
    touch_gesture_t g3 = touch_button_update(&state, true, 2300);
    assert(g3 == GESTURE_NONE);

    // 释放时不再报 TAP
    touch_gesture_t g4 = touch_button_update(&state, false, 2400);
    assert(g4 == GESTURE_NONE);

    printf("[PASS] test_touch_long_press_detection\n");
}

static void test_action_reporter_payload_serialization() {
    device_action_t act;
    int rc = action_reporter_build_action("esp32_c049ef", GESTURE_TAP, "evt_abc_123", 1700000000, &act);
    assert(rc == 0);
    assert(strcmp(act.type, "device_action") == 0);
    assert(act.version == 1);
    assert(strcmp(act.device_id, "esp32_c049ef") == 0);
    assert(strcmp(act.action, "tap") == 0);
    assert(act.created_at == 1700000000);
    assert(act.expire_at == 1700000010);
    assert(strlen(act.action_id) == 36);
    assert(strcmp(act.active_alert_id, "evt_abc_123") == 0);

    char json_buf[512];
    rc = action_reporter_serialize_json(&act, json_buf, sizeof(json_buf));
    assert(rc == 0);
    assert(strstr(json_buf, "\"device_id\":\"esp32_c049ef\"") != NULL);
    assert(strstr(json_buf, "\"action\":\"tap\"") != NULL);
    assert(strstr(json_buf, "\"created_at\":1700000000") != NULL);
    assert(strstr(json_buf, "\"expire_at\":1700000010") != NULL);
    assert(strstr(json_buf, "\"active_alert_id\":\"evt_abc_123\"") != NULL);

    // 测试无活跃告警时的 NULL 格式
    rc = action_reporter_build_action("esp32_c049ef", GESTURE_LONG_PRESS, NULL, 1700000000, &act);
    assert(rc == 0);
    rc = action_reporter_serialize_json(&act, json_buf, sizeof(json_buf));
    assert(rc == 0);
    assert(strstr(json_buf, "\"action\":\"long_press\"") != NULL);
    assert(strstr(json_buf, "\"active_alert_id\":null") != NULL);

    // 测试连续生成 UUIDv4 的唯一性 (RR02)
    device_action_t act2;
    rc = action_reporter_build_action("esp32_c049ef", GESTURE_TAP, "evt_abc_124", 1700000001, &act2);
    assert(rc == 0);
    assert(strcmp(act.action_id, act2.action_id) != 0);

    printf("[PASS] test_action_reporter_payload_serialization\n");
}

// 模拟 ESP-IDF lwIP esp_sntp 底层读取即重置行为 (R6-01)
static int s_mock_sntp_status = 0; // 0: RESET, 1: COMPLETED, 2: IN_PROGRESS
static int mock_esp_sntp_get_sync_status(void) {
    int cur = s_mock_sntp_status;
    if (s_mock_sntp_status == 1) {
        s_mock_sntp_status = 0; // 读取后立即重置为 RESET (0)
    }
    return cur;
}

static void test_touch_action_sntp_sync_guard() {
    alert_scheduler_t sched;
    alert_scheduler_init(&sched);

    alert_payload_t med_alert = {
        .priority = PRIORITY_MEDIUM,
        .duration_sec = 8,
        .sound = SOUND_DING
    };
    strncpy(med_alert.event_id, "evt_test_sntp_1", sizeof(med_alert.event_id) - 1);
    strncpy(med_alert.title, "测试告警", sizeof(med_alert.title) - 1);

    int push_rc = alert_scheduler_push(&sched, &med_alert, WORK_MODE_IDLE);
    assert(push_rc > 0);
    assert(sched.has_active);
    assert(alert_scheduler_get_active(&sched) != NULL);

    char action_json[512] = {0};

    // 1. MQTT 已连接，但 SNTP 未同步 (unix_now < 1700000000 且 is_time_synced = false)
    // 预期: 拦截动作发送，不生成 JSON，保留活跃告警以备校时后重试 (R5-04)
    touch_alert_action_result_t res1 = touch_action_handle_tap(
        &sched,
        "esp32_c049ef",
        false,      /* is_time_synced */
        100LL,      /* unix_now: 开机未校时时间戳 */
        true,       /* is_mqtt_connected */
        WORK_MODE_IDLE,
        action_json,
        sizeof(action_json)
    );
    assert(res1 == TOUCH_ALERT_ACT_BLOCKED_UNSYNCED);
    assert(alert_scheduler_get_active(&sched) != NULL); // 告警必须保留，绝不提前消除
    assert(action_json[0] == '\0');

    // 2. SNTP 已同步，但 MQTT 暂时离线
    // 预期: 拦截动作发送，保留告警待重连
    touch_alert_action_result_t res2 = touch_action_handle_tap(
        &sched,
        "esp32_c049ef",
        true,          /* is_time_synced */
        1700000050LL,  /* unix_now: 合法 Unix 时间戳 */
        false,         /* is_mqtt_connected */
        WORK_MODE_IDLE,
        action_json,
        sizeof(action_json)
    );
    assert(res2 == TOUCH_ALERT_ACT_BLOCKED_OFFLINE);
    assert(alert_scheduler_get_active(&sched) != NULL); // 依然保留
    assert(action_json[0] == '\0');

    // 3. SNTP 已同步且 MQTT 在线
    // 预期: 成功生成动作 JSON，且安全消除告警
    touch_alert_action_result_t res3 = touch_action_handle_tap(
        &sched,
        "esp32_c049ef",
        true,          /* is_time_synced */
        1700000050LL,  /* unix_now */
        true,          /* is_mqtt_connected */
        WORK_MODE_IDLE,
        action_json,
        sizeof(action_json)
    );
    assert(res3 == TOUCH_ALERT_ACT_DISMISSED_SENT);
    assert(alert_scheduler_get_active(&sched) == NULL); // 已被消除
    assert(strstr(action_json, "\"action\":\"tap\"") != NULL);
    assert(strstr(action_json, "\"created_at\":1700000050") != NULL);
    assert(strstr(action_json, "\"expire_at\":1700000060") != NULL);
    assert(strstr(action_json, "\"active_alert_id\":\"evt_test_sntp_1\"") != NULL);

    printf("[PASS] test_touch_action_sntp_sync_guard\n");
}

static void test_touch_action_sntp_continuous_alerts_and_retry() {
    sntp_sync_tracker_t tracker;
    sntp_sync_tracker_init(&tracker);

    alert_scheduler_t sched;
    alert_scheduler_init(&sched);

    char action_json[512] = {0};

    // -------------------------------------------------------------
    // 步骤 1: 开机未校时阶段 (unix_now=50s, 底层状态 RESET=0)
    // -------------------------------------------------------------
    s_mock_sntp_status = 0;
    bool synced_boot = sntp_sync_tracker_eval(&tracker, mock_esp_sntp_get_sync_status(), 50LL);
    assert(!synced_boot);

    alert_payload_t alert1 = { .priority = PRIORITY_HIGH, .duration_sec = 10, .sound = SOUND_DING };
    strncpy(alert1.event_id, "evt_high_1", sizeof(alert1.event_id) - 1);
    assert(alert_scheduler_push(&sched, &alert1, WORK_MODE_IDLE) > 0);

    touch_alert_action_result_t res_boot = touch_action_handle_tap(
        &sched, "esp32_c049ef", synced_boot, 50LL, true, WORK_MODE_IDLE, action_json, sizeof(action_json)
    );
    assert(res_boot == TOUCH_ALERT_ACT_BLOCKED_UNSYNCED);
    assert(alert_scheduler_get_active(&sched) != NULL); // 告警必须保留

    // -------------------------------------------------------------
    // 步骤 2: SNTP 异步校时成功一次 (COMPLETED=1, unix_now=1700000050)
    // -------------------------------------------------------------
    s_mock_sntp_status = 1; // 底层进入 COMPLETED
    long long now_t1 = 1700000050LL;
    bool synced_t1 = sntp_sync_tracker_eval(&tracker, mock_esp_sntp_get_sync_status(), now_t1);
    assert(synced_t1); // 成功识别校时
    assert(s_mock_sntp_status == 0); // 确认底层读取后已重置为 RESET (0)

    // 点击第一条告警 -> 动作成功发出并消除告警 1
    action_json[0] = '\0';
    touch_alert_action_result_t res_act1 = touch_action_handle_tap(
        &sched, "esp32_c049ef", synced_t1, now_t1, true, WORK_MODE_IDLE, action_json, sizeof(action_json)
    );
    assert(res_act1 == TOUCH_ALERT_ACT_DISMISSED_SENT);
    assert(alert_scheduler_get_active(&sched) == NULL);
    assert(strstr(action_json, "\"active_alert_id\":\"evt_high_1\"") != NULL);

    // -------------------------------------------------------------
    // 步骤 3: 连续第二条告警到达 (R6-01: 底层状态为 RESET=0，但时钟依然校准有效)
    // -------------------------------------------------------------
    alert_payload_t alert2 = { .priority = PRIORITY_HIGH, .duration_sec = 10, .sound = SOUND_DING };
    strncpy(alert2.event_id, "evt_high_2", sizeof(alert2.event_id) - 1);
    assert(alert_scheduler_push(&sched, &alert2, WORK_MODE_IDLE) > 0);

    long long now_t2 = 1700000100LL;
    // 此时 mock_esp_sntp_get_sync_status() 依然返回 RESET (0)，且无新同步
    bool synced_t2 = sntp_sync_tracker_eval(&tracker, mock_esp_sntp_get_sync_status(), now_t2);
    assert(synced_t2); // 必须依托 tracker 缓存持久判定为有效，绝不可返回 false (R6-01)

    action_json[0] = '\0';
    touch_alert_action_result_t res_act2 = touch_action_handle_tap(
        &sched, "esp32_c049ef", synced_t2, now_t2, true, WORK_MODE_IDLE, action_json, sizeof(action_json)
    );
    assert(res_act2 == TOUCH_ALERT_ACT_DISMISSED_SENT); // 连续点击必须成功放行
    assert(alert_scheduler_get_active(&sched) == NULL);
    assert(strstr(action_json, "\"active_alert_id\":\"evt_high_2\"") != NULL);

    // -------------------------------------------------------------
    // 步骤 4: 离线点击后恢复连接再重试 (R6-01: 离线先拦截保留，联网后重试成功)
    // -------------------------------------------------------------
    alert_payload_t alert3 = { .priority = PRIORITY_HIGH, .duration_sec = 10, .sound = SOUND_DING };
    strncpy(alert3.event_id, "evt_high_3", sizeof(alert3.event_id) - 1);
    assert(alert_scheduler_push(&sched, &alert3, WORK_MODE_IDLE) > 0);

    long long now_t3 = 1700000200LL;
    bool synced_t3 = sntp_sync_tracker_eval(&tracker, mock_esp_sntp_get_sync_status(), now_t3);
    assert(synced_t3);

    // 4.1 处于离线状态下点击
    action_json[0] = '\0';
    touch_alert_action_result_t res_offline = touch_action_handle_tap(
        &sched, "esp32_c049ef", synced_t3, now_t3, false /* mqtt offline */, WORK_MODE_IDLE, action_json, sizeof(action_json)
    );
    assert(res_offline == TOUCH_ALERT_ACT_BLOCKED_OFFLINE);
    assert(alert_scheduler_get_active(&sched) != NULL); // 告警 3 依然保留

    // 4.2 网络恢复后再次点击重试
    long long now_t4 = 1700000205LL;
    bool synced_t4 = sntp_sync_tracker_eval(&tracker, mock_esp_sntp_get_sync_status(), now_t4);
    assert(synced_t4);

    action_json[0] = '\0';
    touch_alert_action_result_t res_retry = touch_action_handle_tap(
        &sched, "esp32_c049ef", synced_t4, now_t4, true /* mqtt reconnected */, WORK_MODE_IDLE, action_json, sizeof(action_json)
    );
    assert(res_retry == TOUCH_ALERT_ACT_DISMISSED_SENT); // 重试放行
    assert(alert_scheduler_get_active(&sched) == NULL);
    assert(strstr(action_json, "\"active_alert_id\":\"evt_high_3\"") != NULL);

    // -------------------------------------------------------------
    // 步骤 5: 验证通知回调触发路径 (time_sync_notification_cb 机制)
    // -------------------------------------------------------------
    sntp_sync_tracker_t tracker_cb;
    sntp_sync_tracker_init(&tracker_cb);
    assert(!sntp_sync_tracker_eval(&tracker_cb, 0, 1700000300LL));
    sntp_sync_tracker_notify_synced(&tracker_cb);
    assert(sntp_sync_tracker_eval(&tracker_cb, 0, 1700000300LL));

    printf("[PASS] test_touch_action_sntp_continuous_alerts_and_retry\n");
}

int main(void) {
    printf("Running test_touch_gestures...\n");
    test_touch_jitter_debounce();
    test_touch_tap_detection();
    test_touch_long_press_detection();
    test_action_reporter_payload_serialization();
    test_touch_action_sntp_sync_guard();
    test_touch_action_sntp_continuous_alerts_and_retry();
    printf("All test_touch_gestures tests passed successfully!\n");
    return 0;
}
