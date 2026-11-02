#include "alert_queue.h"
#include <string.h>

void alert_scheduler_init(alert_scheduler_t* sched) {
    if (!sched) return;
    memset(sched, 0, sizeof(alert_scheduler_t));
}

void alert_scheduler_clear_medium(alert_scheduler_t* sched) {
    if (!sched) return;
    int write_idx = 0;
    for (int i = 0; i < sched->count; i++) {
        if (sched->queue[i].priority == PRIORITY_HIGH) {
            sched->queue[write_idx++] = sched->queue[i];
        }
    }
    sched->count = write_idx;
}

int alert_scheduler_push(alert_scheduler_t* sched, const alert_payload_t* alert, work_mode_t current_mode) {
    if (!sched || !alert) return 0;

    alert_payload_t item = *alert;
    alert_ingress_decision_t decision = alert_filter_check_ingress(current_mode, &item);

    if (decision == ALERT_DECISION_DROP) {
        return 0; // 静默丢弃
    }
    if (decision == ALERT_DECISION_STATUS_DOT) {
        return 1; // 仅状态栏打点
    }

    // 若当前屏幕空闲，直接弹出
    if (!sched->has_active) {
        sched->has_active = true;
        sched->active_alert = item;
        sched->remaining_sec = item.duration_sec;
        return 2; // 立即弹出
    }

    // 若当前正在展示 Medium
    if (sched->active_alert.priority == PRIORITY_MEDIUM) {
        if (item.priority == PRIORITY_HIGH) {
            // High 立即打断抢占 Medium 弹窗
            sched->active_alert = item;
            sched->remaining_sec = item.duration_sec;
            return 3; // 抢占
        } else {
            // 新到达的 Medium 进入排队队列
            if (sched->count >= ALERT_QUEUE_CAPACITY) {
                // 队列已满，丢弃最老的一条 Medium
                for (int i = 0; i < ALERT_QUEUE_CAPACITY - 1; i++) {
                    sched->queue[i] = sched->queue[i + 1];
                }
                sched->count = ALERT_QUEUE_CAPACITY - 1;
            }
            sched->queue[sched->count++] = item;
            return 4; // 排队
        }
    }

    // 若当前正在展示 High
    if (sched->active_alert.priority == PRIORITY_HIGH) {
        if (item.priority == PRIORITY_HIGH) {
            if (strcmp(sched->active_alert.event_id, item.event_id) == 0) {
                return 0; // 相同 event_id 幂等去重
            } else {
                // 不同 event_id 替换展示并重置
                sched->active_alert = item;
                sched->remaining_sec = item.duration_sec;
                return 5; // 替换
            }
        } else {
            // 新到达的 Medium 进入排队
            if (sched->count >= ALERT_QUEUE_CAPACITY) {
                for (int i = 0; i < ALERT_QUEUE_CAPACITY - 1; i++) {
                    sched->queue[i] = sched->queue[i + 1];
                }
                sched->count = ALERT_QUEUE_CAPACITY - 1;
            }
            sched->queue[sched->count++] = item;
            return 4; // 排队
        }
    }

    return 0;
}

bool alert_scheduler_dismiss(alert_scheduler_t* sched, long long now, work_mode_t current_mode) {
    if (!sched) return false;

    sched->has_active = false;
    memset(&sched->active_alert, 0, sizeof(alert_payload_t));
    sched->remaining_sec = 0;

    while (sched->count > 0) {
        // 出队队首元素
        alert_payload_t next = sched->queue[0];
        for (int i = 0; i < sched->count - 1; i++) {
            sched->queue[i] = sched->queue[i + 1];
        }
        sched->count--;

        bool clear_all_med = false;
        bool valid = alert_filter_check_egress(current_mode, &next, now, &clear_all_med);

        if (clear_all_med) {
            alert_scheduler_clear_medium(sched);
            return false;
        }

        if (valid) {
            sched->has_active = true;
            sched->active_alert = next;
            sched->remaining_sec = next.duration_sec;
            return true;
        }
    }

    return false;
}

void alert_scheduler_tick(alert_scheduler_t* sched, long long now, work_mode_t current_mode) {
    if (!sched || !sched->has_active) return;

    // High 告警绝不受倒计时自动关闭影响，必须物理确认
    if (sched->active_alert.priority == PRIORITY_HIGH) {
        return;
    }

    if (sched->remaining_sec > 0) {
        sched->remaining_sec--;
        if (sched->remaining_sec <= 0) {
            alert_scheduler_dismiss(sched, now, current_mode);
        }
    }
}

const alert_payload_t* alert_scheduler_get_active(const alert_scheduler_t* sched) {
    if (!sched || !sched->has_active) return NULL;
    return &sched->active_alert;
}
