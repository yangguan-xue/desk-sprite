#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "sprite_types.h"
#include "ui_metrics.h"

static void test_metrics_fresh_state() {
    metrics_payload_t payload;
    memset(&payload, 0, sizeof(payload));
    payload.item_count = 2;
    payload.ttl_sec = 3600;
    payload.sampled_at = 10000;

    // Item 0: Kimi (used: 42/100)
    strncpy(payload.items[0].key, "kimi", sizeof(payload.items[0].key));
    strncpy(payload.items[0].label, "Kimi", sizeof(payload.items[0].label));
    payload.items[0].current = 42.0;
    payload.items[0].total = 100.0;
    strncpy(payload.items[0].unit, "kTok", sizeof(payload.items[0].unit));
    payload.items[0].direction = METRIC_DIR_USED;
    payload.items[0].status = METRIC_STATUS_NORMAL;
    payload.items[0].is_mock = false;
    payload.items[0].delta_direction = 1;

    // Item 1: Copilot (remaining: 18/30 天, 60%, RR07 真实语义)
    strncpy(payload.items[1].key, "copilot_sub", sizeof(payload.items[1].key));
    strncpy(payload.items[1].label, "Copilot", sizeof(payload.items[1].label));
    payload.items[1].current = 18.0;
    payload.items[1].total = 30.0;
    strncpy(payload.items[1].unit, "天", sizeof(payload.items[1].unit));
    payload.items[1].direction = METRIC_DIR_REMAINING;
    payload.items[1].status = METRIC_STATUS_NORMAL;
    payload.items[1].is_mock = true;
    payload.items[1].delta_direction = -1;

    ui_metrics_render_model_t model;
    long long now = 10000 + 1800; // age = 1800s <= 3600s
    int rc = ui_metrics_compute_render_model(&payload, now, &model);

    assert(rc == 0);
    assert(!model.is_stale);
    assert(model.count == 2);

    // 验证 Item 0
    assert(model.rows[0].style == UI_METRIC_STYLE_FRESH);
    assert(!model.rows[0].show_mock_badge);
    assert(model.rows[0].delta_color == 1);
    assert(model.rows[0].progress_ratio >= 0.419f && model.rows[0].progress_ratio <= 0.421f);
    assert(strstr(model.rows[0].rendered_text, "Kimi: 42/100 kTok") != NULL);
    assert(strstr(model.rows[0].rendered_text, "[STALE]") == NULL);

    // 验证 Item 1 (Remaining: 18/30 = 0.60 -> ratio 0.60, RR07)
    assert(model.rows[1].style == UI_METRIC_STYLE_FRESH);
    assert(model.rows[1].show_mock_badge);
    assert(model.rows[1].progress_ratio >= 0.599f && model.rows[1].progress_ratio <= 0.601f);
    assert(strstr(model.rows[1].rendered_text, "[M]") != NULL);

    printf("[PASS] test_metrics_fresh_state\n");
}

static void test_metrics_stale_transition() {
    metrics_payload_t payload;
    memset(&payload, 0, sizeof(payload));
    payload.item_count = 1;
    payload.ttl_sec = 3600;
    payload.sampled_at = 10000;

    strncpy(payload.items[0].key, "codex", sizeof(payload.items[0].key));
    strncpy(payload.items[0].label, "Codex", sizeof(payload.items[0].label));
    payload.items[0].current = 10.0;
    payload.items[0].total = 50.0;
    strncpy(payload.items[0].unit, "req", sizeof(payload.items[0].unit));
    payload.items[0].direction = METRIC_DIR_USED;

    ui_metrics_render_model_t model;

    // 1. 恰好在边界 (age == 3600) -> 依然 Fresh
    long long now_boundary = 10000 + 3600;
    ui_metrics_compute_render_model(&payload, now_boundary, &model);
    assert(!model.is_stale);
    assert(model.rows[0].style == UI_METRIC_STYLE_FRESH);

    // 2. 超出 1 秒 (age == 3601) -> 降级为 Stale
    long long now_stale = 10000 + 3601;
    ui_metrics_compute_render_model(&payload, now_stale, &model);
    assert(model.is_stale);
    assert(model.rows[0].style == UI_METRIC_STYLE_STALE);
    assert(strstr(model.rows[0].rendered_text, "[STALE]") != NULL);

    // 3. 负时差 (时钟回拨 jitter) -> 判定为 Stale
    long long now_jitter = 10000 - 10;
    ui_metrics_compute_render_model(&payload, now_jitter, &model);
    assert(model.is_stale);

    printf("[PASS] test_metrics_stale_transition\n");
}

int main(void) {
    printf("Running test_metrics_stale...\n");
    test_metrics_fresh_state();
    test_metrics_stale_transition();
    printf("All test_metrics_stale tests passed successfully!\n");
    return 0;
}
