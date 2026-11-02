#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "sprite_types.h"
#include "alert_filter.h"
#include "alert_queue.h"

static void test_ingress_filtering() {
    alert_scheduler_t sched;
    alert_scheduler_init(&sched);

    alert_payload_t low_alert = {.priority = PRIORITY_LOW, .duration_sec = 10};
    strncpy(low_alert.event_id, "evt_low", sizeof(low_alert.event_id));

    alert_payload_t med_alert = {.priority = PRIORITY_MEDIUM, .duration_sec = 10, .sound = SOUND_DING};
    strncpy(med_alert.event_id, "evt_med", sizeof(med_alert.event_id));

    alert_payload_t high_alert = {.priority = PRIORITY_HIGH, .duration_sec = 0, .sound = SOUND_ALERT_BEEP};
    strncpy(high_alert.event_id, "evt_high", sizeof(high_alert.event_id));

    // 1. Sleep 模式: Low 和 Medium 被静默丢弃
    int rc = alert_scheduler_push(&sched, &low_alert, WORK_MODE_SLEEP);
    assert(rc == 0);
    assert(!sched.has_active);

    rc = alert_scheduler_push(&sched, &med_alert, WORK_MODE_SLEEP);
    assert(rc == 0);
    assert(!sched.has_active);

    // Sleep 模式: High 强穿透
    rc = alert_scheduler_push(&sched, &high_alert, WORK_MODE_SLEEP);
    assert(rc == 2);
    assert(sched.has_active);
    assert(sched.active_alert.priority == PRIORITY_HIGH);

    alert_scheduler_init(&sched);

    // 2. Meeting 模式: Low 被丢弃，Medium 和 High 被静音接收
    rc = alert_scheduler_push(&sched, &low_alert, WORK_MODE_MEETING);
    assert(rc == 0);

    rc = alert_scheduler_push(&sched, &med_alert, WORK_MODE_MEETING);
    assert(rc == 2);
    assert(sched.has_active);
    assert(sched.active_alert.force_mute == true);

    alert_scheduler_init(&sched);

    // 3. Coding 模式: Low 仅打点 (返回 1，不进队列)
    rc = alert_scheduler_push(&sched, &low_alert, WORK_MODE_CODING);
    assert(rc == 1);
    assert(!sched.has_active);

    printf("[PASS] test_ingress_filtering\n");
}

static void test_high_preemption_and_idempotency() {
    alert_scheduler_t sched;
    alert_scheduler_init(&sched);

    alert_payload_t med = {.priority = PRIORITY_MEDIUM, .duration_sec = 10};
    strncpy(med.event_id, "evt_med_1", sizeof(med.event_id));

    alert_payload_t high_1 = {.priority = PRIORITY_HIGH, .duration_sec = 0};
    strncpy(high_1.event_id, "evt_high_1", sizeof(high_1.event_id));

    alert_payload_t high_2 = {.priority = PRIORITY_HIGH, .duration_sec = 0};
    strncpy(high_2.event_id, "evt_high_2", sizeof(high_2.event_id));

    // 弹出 Medium
    alert_scheduler_push(&sched, &med, WORK_MODE_CODING);
    assert(sched.has_active);
    assert(strcmp(sched.active_alert.event_id, "evt_med_1") == 0);

    // High 到达 -> 立即打断抢占 Medium
    int rc = alert_scheduler_push(&sched, &high_1, WORK_MODE_CODING);
    assert(rc == 3); // 抢占
    assert(strcmp(sched.active_alert.event_id, "evt_high_1") == 0);

    // 相同 event_id 的 High 再次到达 -> 幂等去重丢弃
    rc = alert_scheduler_push(&sched, &high_1, WORK_MODE_CODING);
    assert(rc == 0);
    assert(strcmp(sched.active_alert.event_id, "evt_high_1") == 0);

    // 不同 event_id 的 High 到达 -> 替换展示
    rc = alert_scheduler_push(&sched, &high_2, WORK_MODE_CODING);
    assert(rc == 5); // 替换
    assert(strcmp(sched.active_alert.event_id, "evt_high_2") == 0);

    printf("[PASS] test_high_preemption_and_idempotency\n");
}

static void test_egress_recheck_sleep_clears_queue() {
    alert_scheduler_t sched;
    alert_scheduler_init(&sched);

    alert_payload_t med_active = {.priority = PRIORITY_MEDIUM, .duration_sec = 2};
    strncpy(med_active.event_id, "med_active", sizeof(med_active.event_id));

    alert_payload_t med_queued = {.priority = PRIORITY_MEDIUM, .duration_sec = 10};
    strncpy(med_queued.event_id, "med_queued", sizeof(med_queued.event_id));

    // 1. Coding 模式下激活一条 Medium，排队一条 Medium
    alert_scheduler_push(&sched, &med_active, WORK_MODE_CODING);
    alert_scheduler_push(&sched, &med_queued, WORK_MODE_CODING);
    assert(sched.count == 1);

    // 2. 模拟排队期间 Mac 锁屏，当前模式突变为 Sleep
    // 3. active 告警结束 (或者用户消除)
    long long now = 1000;
    bool has_next = alert_scheduler_dismiss(&sched, now, WORK_MODE_SLEEP);

    // 动态复核检测到 Sleep，清空所有排队 Medium，绝不唤醒屏幕
    assert(!has_next);
    assert(!sched.has_active);
    assert(sched.count == 0);

    printf("[PASS] test_egress_recheck_sleep_clears_queue\n");
}

static void test_egress_recheck_expiration() {
    alert_scheduler_t sched;
    alert_scheduler_init(&sched);

    alert_payload_t med_active = {.priority = PRIORITY_MEDIUM, .duration_sec = 2, .expire_at = 2000};
    strncpy(med_active.event_id, "med_active", sizeof(med_active.event_id));

    alert_payload_t med_expired = {.priority = PRIORITY_MEDIUM, .duration_sec = 10, .expire_at = 900};
    strncpy(med_expired.event_id, "med_expired", sizeof(med_expired.event_id));

    alert_payload_t med_valid = {.priority = PRIORITY_MEDIUM, .duration_sec = 10, .expire_at = 2000};
    strncpy(med_valid.event_id, "med_valid", sizeof(med_valid.event_id));

    alert_scheduler_push(&sched, &med_active, WORK_MODE_CODING);
    alert_scheduler_push(&sched, &med_expired, WORK_MODE_CODING);
    alert_scheduler_push(&sched, &med_valid, WORK_MODE_CODING);
    assert(sched.count == 2);

    long long now = 1000; // now(1000) > med_expired.expire_at(900)
    bool has_next = alert_scheduler_dismiss(&sched, now, WORK_MODE_CODING);

    // med_expired 被静默跳过，成功拉取 med_valid
    assert(has_next);
    assert(sched.has_active);
    assert(strcmp(sched.active_alert.event_id, "med_valid") == 0);
    assert(sched.count == 0);

    printf("[PASS] test_egress_recheck_expiration\n");
}

int main(void) {
    printf("Running test_alert_scheduler...\n");
    test_ingress_filtering();
    test_high_preemption_and_idempotency();
    test_egress_recheck_sleep_clears_queue();
    test_egress_recheck_expiration();
    printf("All test_alert_scheduler tests passed successfully!\n");
    return 0;
}
