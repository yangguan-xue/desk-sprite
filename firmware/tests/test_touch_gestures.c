#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "sprite_types.h"
#include "touch_button.h"
#include "action_reporter.h"

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

int main(void) {
    printf("Running test_touch_gestures...\n");
    test_touch_jitter_debounce();
    test_touch_tap_detection();
    test_touch_long_press_detection();
    test_action_reporter_payload_serialization();
    printf("All test_touch_gestures tests passed successfully!\n");
    return 0;
}
