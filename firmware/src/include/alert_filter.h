#ifndef ALERT_FILTER_H
#define ALERT_FILTER_H

#include "sprite_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ALERT_DECISION_DROP = 0,        // 静默丢弃
    ALERT_DECISION_STATUS_DOT = 1,  // 状态栏打点 3 秒 (不进队列、不弹窗)
    ALERT_DECISION_ACCEPT = 2       // 接收入队 / 弹窗展示
} alert_ingress_decision_t;

// 第一阶段：接收时入队过滤 (Ingress Filter)
alert_ingress_decision_t alert_filter_check_ingress(
    work_mode_t current_mode,
    alert_payload_t* alert
);

// 第二阶段：出队复核 (Egress Re-check)
// 返回 true 表示可展示，false 表示应当丢弃
bool alert_filter_check_egress(
    work_mode_t current_mode,
    alert_payload_t* alert,
    long long now,
    bool* out_clear_all_medium
);

#ifdef __cplusplus
}
#endif

#endif // ALERT_FILTER_H
