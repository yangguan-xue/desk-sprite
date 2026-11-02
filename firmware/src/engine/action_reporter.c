#include "action_reporter.h"
#include <stdio.h>
#include <string.h>

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

    static unsigned int s_counter = 0;
    snprintf(out_action->action_id, sizeof(out_action->action_id),
             "act_%lld_%u", timestamp, ++s_counter);

    switch (gesture) {
        case GESTURE_TAP:
            strncpy(out_action->action_type, "tap", sizeof(out_action->action_type) - 1);
            break;
        case GESTURE_LONG_PRESS:
            strncpy(out_action->action_type, "long_press", sizeof(out_action->action_type) - 1);
            break;
        case GESTURE_DOUBLE_TAP:
            strncpy(out_action->action_type, "double_tap", sizeof(out_action->action_type) - 1);
            break;
        default:
            return -1;
    }

    if (active_event_id && active_event_id[0] != '\0') {
        strncpy(out_action->active_event_id, active_event_id, sizeof(out_action->active_event_id) - 1);
    } else {
        out_action->active_event_id[0] = '\0';
    }

    out_action->timestamp = timestamp;
    return 0;
}

int action_reporter_serialize_json(
    const device_action_t* action,
    char* buffer,
    size_t buf_len
) {
    if (!action || !buffer || buf_len == 0) return -1;

    int written;
    if (action->active_event_id[0] != '\0') {
        written = snprintf(buffer, buf_len,
            "{\"type\":\"%s\",\"version\":%d,\"device_id\":\"%s\",\"action_id\":\"%s\","
            "\"action_type\":\"%s\",\"active_event_id\":\"%s\",\"timestamp\":%lld}",
            action->type, action->version, action->device_id, action->action_id,
            action->action_type, action->active_event_id, action->timestamp);
    } else {
        written = snprintf(buffer, buf_len,
            "{\"type\":\"%s\",\"version\":%d,\"device_id\":\"%s\",\"action_id\":\"%s\","
            "\"action_type\":\"%s\",\"active_event_id\":null,\"timestamp\":%lld}",
            action->type, action->version, action->device_id, action->action_id,
            action->action_type, action->timestamp);
    }

    if (written < 0 || (size_t)written >= buf_len) {
        return -1;
    }
    return 0;
}
