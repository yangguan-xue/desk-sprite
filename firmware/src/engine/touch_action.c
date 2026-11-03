#include "touch_action.h"
#include <string.h>

touch_alert_action_result_t touch_action_handle_tap(
    alert_scheduler_t* sched,
    const char* device_id,
    bool is_time_synced,
    long long unix_now,
    bool is_mqtt_connected,
    work_mode_t current_mode,
    char* out_action_json,
    size_t action_json_len
) {
    if (!sched || !device_id) return TOUCH_ALERT_ACT_ERROR;

    const alert_payload_t* active = alert_scheduler_get_active(sched);
    if (!active) {
        return TOUCH_ALERT_ACT_IGNORED;
    }

    // 1. SNTP 尚未校时或时间戳无效: 绝对不可发送未校准动作导致云端判过期，保留告警以备重试 (R5-04)
    if (!is_time_synced || unix_now < 1700000000LL) {
        return TOUCH_ALERT_ACT_BLOCKED_UNSYNCED;
    }

    // 2. MQTT 离线: 保留告警以备联网后重试，不进行无效本地清除
    if (!is_mqtt_connected) {
        return TOUCH_ALERT_ACT_BLOCKED_OFFLINE;
    }

    // 3. 构建规范 device_action 报文
    device_action_t act;
    if (action_reporter_build_action(device_id, GESTURE_TAP, active->event_id, unix_now, &act) != 0) {
        return TOUCH_ALERT_ACT_ERROR;
    }

    // 4. 序列化为规范 JSON
    if (action_reporter_serialize_json(&act, out_action_json, action_json_len) != 0) {
        return TOUCH_ALERT_ACT_ERROR;
    }

    // 5. 动作构建与序列化成功，消除本地告警弹窗
    alert_scheduler_dismiss(sched, unix_now, current_mode);
    return TOUCH_ALERT_ACT_DISMISSED_SENT;
}
