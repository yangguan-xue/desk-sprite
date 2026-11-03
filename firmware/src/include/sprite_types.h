#ifndef SPRITE_TYPES_H
#define SPRITE_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 告警优先级
typedef enum {
    PRIORITY_LOW = 0,
    PRIORITY_MEDIUM = 1,
    PRIORITY_HIGH = 2
} priority_t;

// 提示音效
typedef enum {
    SOUND_NONE = 0,
    SOUND_DING = 1,
    SOUND_CHIME = 2,
    SOUND_CHEER = 3,
    SOUND_SIGH = 4,
    SOUND_ALERT_BEEP = 5
} sound_t;

// 桌宠动作
typedef enum {
    PET_IDLE = 0,
    PET_CODING = 1,
    PET_MEETING = 2,
    PET_CHEER = 3,
    PET_SIGH = 4,
    PET_ALERT = 5,
    PET_SLEEP = 6
} pet_action_t;

// 工作模式
typedef enum {
    WORK_MODE_IDLE = 0,
    WORK_MODE_CODING = 1,
    WORK_MODE_MEETING = 2,
    WORK_MODE_SLEEP = 3
} work_mode_t;

// 触摸手势
typedef enum {
    GESTURE_NONE = 0,
    GESTURE_TAP = 1,
    GESTURE_LONG_PRESS = 2,
    GESTURE_DOUBLE_TAP = 3
} touch_gesture_t;

// 告警载荷结构体 (容纳 UTF-8 多字节中文与规范 URL, R5-02)
typedef struct {
    char event_id[64];
    priority_t priority;
    char title[128];
    char summary[256];
    sound_t sound;
    pet_action_t pet_action;
    int duration_sec;
    char action_url[516];
    long long expire_at;
    long long created_at;
    bool force_mute;
} alert_payload_t;

// 指标方向
typedef enum {
    METRIC_DIR_USED = 0,
    METRIC_DIR_REMAINING = 1
} metric_direction_t;

// 指标健康状态
typedef enum {
    METRIC_STATUS_NORMAL = 0,
    METRIC_STATUS_WARNING = 1,
    METRIC_STATUS_EXHAUSTED = 2
} metric_status_t;

// 单条度量数据项 (SPEC-002 第 3.1 节, R09)
typedef struct {
    char key[32];
    char label[32];
    double current;
    double total;
    char unit[16];
    int percent;
    metric_direction_t direction;
    metric_status_t status;
    bool is_mock;
    int delta_direction; // -1: 降低, 0: 持平, 1: 升高
    long long sampled_at;
    int ttl_sec;
} metric_item_t;

// 额度看板载荷
typedef struct {
    int item_count;
    metric_item_t items[3];
    int ttl_sec;
    long long sampled_at;
} metrics_payload_t;

// 设备上报动作 (SPEC-002 第 3.4 节, R05)
typedef struct {
    char type[16];          // "device_action"
    int version;            // 1
    char device_id[32];
    char action_id[64];     // UUIDv4 (36 chars + \0)
    char action[16];        // "tap", "long_press", "double_tap"
    char action_type[16];   // 别名兼容
    char active_alert_id[64]; // 活跃告警 ID (如 evt_...)
    char active_event_id[64]; // 别名兼容
    long long created_at;
    long long expire_at;    // 严格等于 created_at + 10
    long long timestamp;    // 兼容时间戳
} device_action_t;

#ifdef __cplusplus
}
#endif

#endif // SPRITE_TYPES_H
