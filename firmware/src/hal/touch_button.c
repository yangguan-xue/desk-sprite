#include "touch_button.h"
#include <string.h>

void touch_button_init(touch_button_state_t* state) {
    if (!state) return;
    memset(state, 0, sizeof(touch_button_state_t));
}

touch_gesture_t touch_button_update(touch_button_state_t* state, bool pin_high, long long now_ms) {
    if (!state) return GESTURE_NONE;

    if (pin_high) {
        if (!state->is_pressed) {
            // 按下起始
            state->is_pressed = true;
            state->press_start_ms = now_ms;
            state->long_press_fired = false;
            return GESTURE_NONE;
        } else {
            // 持续按住，检测长按阈值 (>= 1200ms)
            if (!state->long_press_fired && (now_ms - state->press_start_ms >= TOUCH_LONG_PRESS_MS)) {
                state->long_press_fired = true;
                return GESTURE_LONG_PRESS;
            }
            return GESTURE_NONE;
        }
    } else {
        // 电平释放
        if (state->is_pressed) {
            long long duration = now_ms - state->press_start_ms;
            state->is_pressed = false;

            if (state->long_press_fired) {
                // 长按已触发，释放时不再重复触发
                return GESTURE_NONE;
            }

            // 单击时间窗: 50ms ~ 300ms
            if (duration >= TOUCH_DEBOUNCE_MS && duration <= TOUCH_TAP_MAX_MS) {
                return GESTURE_TAP;
            }
        }
        return GESTURE_NONE;
    }
}
