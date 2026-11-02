#ifndef ACTION_REPORTER_H
#define ACTION_REPORTER_H

#include "sprite_types.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 构建 device_action 报文
int action_reporter_build_action(
    const char* device_id,
    touch_gesture_t gesture,
    const char* active_event_id,
    long long timestamp,
    device_action_t* out_action
);

// 序列化为规范 JSON 字符串
int action_reporter_serialize_json(
    const device_action_t* action,
    char* buffer,
    size_t buf_len
);

#ifdef __cplusplus
}
#endif

#endif // ACTION_REPORTER_H
