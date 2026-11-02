#ifndef UI_LAYOUT_H
#define UI_LAYOUT_H

#include "sprite_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 240

#define STATUS_BAR_HEIGHT 24
#define STAGE_HEIGHT 144
#define METRICS_BAR_HEIGHT 72
#define METRIC_ROW_HEIGHT 24

typedef struct {
    bool wifi_connected;
    bool mqtt_connected;
    work_mode_t current_mode;
    char clock_str[8]; // e.g. "18:30"
    pet_action_t current_pet_action;
    bool alert_active;
    alert_payload_t active_alert;
    int alert_remaining_sec;
} ui_state_t;

void ui_layout_init(void);
void ui_layout_update_status_bar(bool wifi, bool mqtt, work_mode_t mode, const char* clock_str);
void ui_layout_set_pet_action(pet_action_t action);
void ui_layout_show_alert(const alert_payload_t* alert);
void ui_layout_hide_alert(void);
const ui_state_t* ui_layout_get_state(void);

#ifdef __cplusplus
}
#endif

#endif // UI_LAYOUT_H
