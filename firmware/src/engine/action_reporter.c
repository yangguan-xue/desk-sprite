#include "action_reporter.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static void generate_uuid_v4(char* out_uuid) {
    static uint32_t seed = 0x5a17e001;
    uint8_t bytes[16];
    for (int i = 0; i < 16; i++) {
        seed = seed * 1664525 + 1013904223;
        bytes[i] = (uint8_t)(seed >> 16);
    }
    // UUIDv4 规范: 第 7 字节的高 4 位为 0100 (4), 第 9 字节的高 2 位为 10 (variant 1)
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    bytes[8] = (bytes[8] & 0x3F) | 0x80;

    snprintf(out_uuid, 37,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0], bytes[1], bytes[2], bytes[3],
        bytes[4], bytes[5],
        bytes[6], bytes[7],
        bytes[8], bytes[9],
        bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]
    );
}

int action_reporter_build_action(
    const char* device_id,
    touch_gesture_t gesture,
    const char* active_event_id,
    long long timestamp,
    device_action_t* out_action
) {
    if (!device_id || !out_action) return -1;
    if (gesture == GESTURE_NONE) return -1;

    memset(out_action, 0, sizeof(device_action_t));
    strncpy(out_action->type, "device_action", sizeof(out_action->type) - 1);
    out_action->version = 1;
    strncpy(out_action->device_id, device_id, sizeof(out_action->device_id) - 1);

    // 标准 36 字符小写规范 UUIDv4 (R05)
    generate_uuid_v4(out_action->action_id);

    out_action->created_at = timestamp;
    out_action->expire_at = timestamp + 10;
    out_action->timestamp = timestamp;

    const char* gesture_str = "tap";
    switch (gesture) {
        case GESTURE_TAP:
            gesture_str = "tap";
            break;
        case GESTURE_LONG_PRESS:
            gesture_str = "long_press";
            break;
        case GESTURE_DOUBLE_TAP:
            gesture_str = "double_tap";
            break;
        default:
            return -1;
    }
    strncpy(out_action->action, gesture_str, sizeof(out_action->action) - 1);
    strncpy(out_action->action_type, gesture_str, sizeof(out_action->action_type) - 1);

    if (active_event_id && active_event_id[0] != '\0') {
        strncpy(out_action->active_alert_id, active_event_id, sizeof(out_action->active_alert_id) - 1);
        strncpy(out_action->active_event_id, active_event_id, sizeof(out_action->active_event_id) - 1);
    } else {
        out_action->active_alert_id[0] = '\0';
        out_action->active_event_id[0] = '\0';
    }

    return 0;
}

int action_reporter_serialize_json(
    const device_action_t* action,
    char* buffer,
    size_t buf_len
) {
    if (!action || !buffer || buf_len == 0) return -1;

    int written;
    const char* act_name = action->action[0] ? action->action : action->action_type;
    const char* alert_id = action->active_alert_id[0] ? action->active_alert_id : action->active_event_id;

    if (alert_id && alert_id[0] != '\0') {
        written = snprintf(buffer, buf_len,
            "{\"type\":\"%s\",\"version\":%d,\"device_id\":\"%s\",\"action_id\":\"%s\","
            "\"created_at\":%lld,\"expire_at\":%lld,\"data\":{\"action\":\"%s\",\"active_alert_id\":\"%s\"}}",
            action->type, action->version, action->device_id, action->action_id,
            action->created_at > 0 ? action->created_at : action->timestamp,
            action->expire_at > 0 ? action->expire_at : (action->timestamp + 10),
            act_name, alert_id);
    } else {
        written = snprintf(buffer, buf_len,
            "{\"type\":\"%s\",\"version\":%d,\"device_id\":\"%s\",\"action_id\":\"%s\","
            "\"created_at\":%lld,\"expire_at\":%lld,\"data\":{\"action\":\"%s\",\"active_alert_id\":null}}",
            action->type, action->version, action->device_id, action->action_id,
            action->created_at > 0 ? action->created_at : action->timestamp,
            action->expire_at > 0 ? action->expire_at : (action->timestamp + 10),
            act_name);
    }

    if (written < 0 || (size_t)written >= buf_len) {
        return -1;
    }
    return 0;
}
