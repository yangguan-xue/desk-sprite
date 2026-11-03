#ifndef UI_METRICS_H
#define UI_METRICS_H

#include "sprite_types.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_METRIC_STYLE_FRESH = 0,
    UI_METRIC_STYLE_STALE = 1
} ui_metric_style_t;

typedef enum {
    UI_METRIC_VIEW_VALUE = 0,
    UI_METRIC_VIEW_PERCENT = 1
} ui_metric_view_mode_t;

typedef struct {
    char rendered_text[64];
    float progress_ratio; // 0.0 ~ 1.0
    ui_metric_style_t style;
    bool show_mock_badge;
    int delta_color; // 0: normal, 1: positive/green, -1: negative/red
} ui_metric_row_render_t;

typedef struct {
    int count;
    ui_metric_row_render_t rows[3];
    bool is_stale;
} ui_metrics_render_model_t;

// 更新模型数据并根据 current_time 计算渲染呈现参数 (默认数值模式)
int ui_metrics_compute_render_model(
    const metrics_payload_t* payload,
    long long current_time,
    ui_metrics_render_model_t* out_model
);

// 支持数值 / 百分比展示模式切换 (SPEC-003 第 6 节, R3-03)
int ui_metrics_compute_render_model_ex(
    const metrics_payload_t* payload,
    long long current_time,
    ui_metric_view_mode_t view_mode,
    ui_metrics_render_model_t* out_model
);

// 格式化输出 (用于调试或模拟器终端展示)
void ui_metrics_render_text(const ui_metrics_render_model_t* model, char* buffer, size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif // UI_METRICS_H
