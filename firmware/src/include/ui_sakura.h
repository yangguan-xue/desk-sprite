#ifndef UI_SAKURA_H
#define UI_SAKURA_H
#include "sprite_types.h"
#include <stddef.h>
#define SAKURA_WIDTH 240
#define SAKURA_HEIGHT 240
#define SAKURA_STRIP_HEIGHT 16
#define SAKURA_BUFFER_PIXELS (SAKURA_WIDTH * SAKURA_STRIP_HEIGHT)
typedef struct {
    bool available, stale, mock;
    float remaining;
    int item_index;
} sakura_quota_t;
typedef struct {
    work_mode_t mode;
    const char *clock;
    bool wifi, mqtt, muted, touch_enabled, show_value;
    uint32_t elapsed_ms;
    long long now;
    const metrics_payload_t *metrics;
    const alert_payload_t *alert;
    int alert_remaining_sec;
} sakura_scene_t;
/* Native-endian RGB565. Callback must consume synchronously before returning.
 * Caller owns the strip buffer; separate callers may render independently. */
typedef void (*sakura_flush_t)(void *ctx, int y, int height, const uint16_t *pixels);
sakura_quota_t ui_sakura_quota(const metrics_payload_t *metrics, long long now);
void ui_sakura_render(const sakura_scene_t *scene, uint16_t *strip, sakura_flush_t flush, void *ctx);
#endif
