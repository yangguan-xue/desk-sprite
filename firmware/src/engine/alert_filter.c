#include "alert_filter.h"
#include <string.h>

alert_ingress_decision_t alert_filter_check_ingress(
    work_mode_t current_mode,
    alert_payload_t* alert
) {
    if (!alert) return ALERT_DECISION_DROP;

    // 1. Sleep 模式: 丢弃 Low 与 Medium；仅 High 唤醒屏幕强穿透
    if (current_mode == WORK_MODE_SLEEP) {
        if (alert->priority == PRIORITY_LOW || alert->priority == PRIORITY_MEDIUM) {
            return ALERT_DECISION_DROP;
        }
        return ALERT_DECISION_ACCEPT;
    }

    // 2. Meeting 模式: 静默丢弃 Low；Medium 与 High 接收并强制硬件静音 (PA_EN=0)
    if (current_mode == WORK_MODE_MEETING) {
        if (alert->priority == PRIORITY_LOW) {
            return ALERT_DECISION_DROP;
        }
        alert->force_mute = true;
        return ALERT_DECISION_ACCEPT;
    }

    // 3. Coding 或 Idle 模式: Low 仅状态栏打点 3s；Medium 和 High 正常接收
    if (alert->priority == PRIORITY_LOW) {
        return ALERT_DECISION_STATUS_DOT;
    }

    return ALERT_DECISION_ACCEPT;
}

bool alert_filter_check_egress(
    work_mode_t current_mode,
    alert_payload_t* alert,
    long long now,
    bool* out_clear_all_medium
) {
    if (out_clear_all_medium) {
        *out_clear_all_medium = false;
    }
    if (!alert) return false;

    // 1. 过期检查 (High 告警豁免过期判定，必须手动确认)
    if (alert->priority != PRIORITY_HIGH && alert->expire_at > 0 && now > alert->expire_at) {
        return false; // 超时失效静默丢弃
    }

    // 2. 依据当前最新模式动态复核
    if (current_mode == WORK_MODE_SLEEP) {
        if (alert->priority != PRIORITY_HIGH) {
            if (out_clear_all_medium) {
                *out_clear_all_medium = true;
            }
            return false;
        }
        return true;
    }

    if (current_mode == WORK_MODE_MEETING) {
        alert->force_mute = true;
        return true;
    }

    return true;
}
