#include "ui_metrics.h"
#include <stdio.h>
#include <string.h>

int ui_metrics_compute_render_model(
    const metrics_payload_t* payload,
    long long current_time,
    ui_metrics_render_model_t* out_model
) {
    if (!payload || !out_model) return -1;

    memset(out_model, 0, sizeof(ui_metrics_render_model_t));
    out_model->count = payload->item_count > 3 ? 3 : payload->item_count;

    bool has_any_stale = false;

    for (int i = 0; i < out_model->count; i++) {
        const metric_item_t* src = &payload->items[i];
        ui_metric_row_render_t* dst = &out_model->rows[i];

        long long item_sampled = src->sampled_at > 0 ? src->sampled_at : payload->sampled_at;
        int item_ttl = src->ttl_sec > 0 ? src->ttl_sec : payload->ttl_sec;
        long long age = current_time - item_sampled;

        bool row_stale = (item_ttl > 0 && age > item_ttl) || (src->status == METRIC_STATUS_EXHAUSTED) || (age < 0);
        if (row_stale) {
            has_any_stale = true;
        }

        dst->style = row_stale ? UI_METRIC_STYLE_STALE : UI_METRIC_STYLE_FRESH;
        dst->show_mock_badge = src->is_mock;
        dst->delta_color = src->delta_direction;

        // 计算进度比例
        float ratio = 0.0f;
        if (src->total > 0.00001) {
            if (src->direction == METRIC_DIR_USED) {
                ratio = (float)(src->current / src->total);
            } else {
                ratio = (float)((src->total - src->current) / src->total);
            }
        }
        if (ratio < 0.0f) ratio = 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        dst->progress_ratio = ratio;

        // 格式化文本
        if (row_stale) {
            snprintf(dst->rendered_text, sizeof(dst->rendered_text),
                     "%s: %.0f/%.0f %s [STALE]",
                     src->label[0] ? src->label : src->key,
                     src->current, src->total, src->unit);
        } else {
            snprintf(dst->rendered_text, sizeof(dst->rendered_text),
                     "%s: %.0f/%.0f %s%s",
                     src->label[0] ? src->label : src->key,
                     src->current, src->total, src->unit,
                     src->is_mock ? " [M]" : "");
        }
    }

    out_model->is_stale = has_any_stale;
    return 0;
}

void ui_metrics_render_text(const ui_metrics_render_model_t* model, char* buffer, size_t buf_len) {
    if (!model || !buffer || buf_len == 0) return;
    buffer[0] = '\0';

    size_t offset = 0;
    for (int i = 0; i < model->count; i++) {
        int written = snprintf(buffer + offset, buf_len - offset,
                               "[%d] %s (ratio: %.2f, %s)\n",
                               i + 1,
                               model->rows[i].rendered_text,
                               model->rows[i].progress_ratio,
                               model->rows[i].style == UI_METRIC_STYLE_STALE ? "STALE" : "FRESH");
        if (written < 0 || (size_t)written >= buf_len - offset) break;
        offset += written;
    }
}
