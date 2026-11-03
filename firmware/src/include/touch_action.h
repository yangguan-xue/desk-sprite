#ifndef TOUCH_ACTION_H
#define TOUCH_ACTION_H

#include "sprite_types.h"
#include "alert_queue.h"
#include "action_reporter.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TOUCH_ALERT_ACT_DISMISSED_SENT = 0,    // 动作成功构建并已发出，告警本地消除
    TOUCH_ALERT_ACT_BLOCKED_UNSYNCED = 1,  // SNTP 未校时，阻止发送并保留告警以备重试 (R5-04)
    TOUCH_ALERT_ACT_BLOCKED_OFFLINE = 2,   // MQTT 离线，阻止发送并保留告警以备重试
    TOUCH_ALERT_ACT_IGNORED = 3,           // 无活跃告警或非相关手势
    TOUCH_ALERT_ACT_ERROR = -1
} touch_alert_action_result_t;

// SNTP 时钟同步状态监视器 (R6-01: 解决 ESP-IDF/lwIP 读取 COMPLETED 状态后立即重置为 RESET 导致后续动作被拦截的问题)
typedef struct {
    bool has_synced;
} sntp_sync_tracker_t;

void sntp_sync_tracker_init(sntp_sync_tracker_t* tracker);
void sntp_sync_tracker_notify_synced(sntp_sync_tracker_t* tracker);
bool sntp_sync_tracker_eval(sntp_sync_tracker_t* tracker, int sntp_sync_status, long long unix_now);

// 处理轻触告警弹窗并决定是否上报动作 (R5-04: 受时钟同步与连接状态强约束)
touch_alert_action_result_t touch_action_handle_tap(
    alert_scheduler_t* sched,
    const char* device_id,
    bool is_time_synced,
    long long unix_now,
    bool is_mqtt_connected,
    work_mode_t current_mode,
    char* out_action_json,
    size_t action_json_len
);

#ifdef __cplusplus
}
#endif

#endif // TOUCH_ACTION_H
