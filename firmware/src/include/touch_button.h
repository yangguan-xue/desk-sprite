#ifndef TOUCH_BUTTON_H
#define TOUCH_BUTTON_H

#include "sprite_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TOUCH_DEBOUNCE_MS 50
#define TOUCH_TAP_MAX_MS 300
#define TOUCH_LONG_PRESS_MS 1200

typedef struct {
    bool is_pressed;
    long long press_start_ms;
    bool long_press_fired;
} touch_button_state_t;

void touch_button_init(touch_button_state_t* state);

// 输入当前引脚高低电平与毫秒时间戳，返回检测到的手势
touch_gesture_t touch_button_update(touch_button_state_t* state, bool pin_high, long long now_ms);

#ifdef __cplusplus
}
#endif

#endif // TOUCH_BUTTON_H
