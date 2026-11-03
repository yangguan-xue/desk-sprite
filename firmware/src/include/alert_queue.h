#ifndef ALERT_QUEUE_H
#define ALERT_QUEUE_H

#include "sprite_types.h"
#include "alert_filter.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ALERT_QUEUE_CAPACITY 5

typedef struct {
    alert_payload_t queue[ALERT_QUEUE_CAPACITY];
    int count;
    bool has_active;
    alert_payload_t active_alert;
    int remaining_sec;
} alert_scheduler_t;

void alert_scheduler_init(alert_scheduler_t* sched);

// 投递新告警 (结合两阶段过滤与优先级抢占)
int alert_scheduler_push(alert_scheduler_t* sched, const alert_payload_t* alert, work_mode_t current_mode);

// 每秒定时时钟驱动
void alert_scheduler_tick(alert_scheduler_t* sched, long long now, work_mode_t current_mode);

// 用户手动消除或倒计时结束
bool alert_scheduler_dismiss(alert_scheduler_t* sched, long long now, work_mode_t current_mode);

// 获取当前展示中的活跃告警
const alert_payload_t* alert_scheduler_get_active(const alert_scheduler_t* sched);

// 获取当前活跃告警在调度器中的展示剩余秒数 (R3-07)
int alert_scheduler_get_remaining_sec(const alert_scheduler_t* sched);

// 清空等待队列中的所有 Medium 告警
void alert_scheduler_clear_medium(alert_scheduler_t* sched);

#ifdef __cplusplus
}
#endif

#endif // ALERT_QUEUE_H
