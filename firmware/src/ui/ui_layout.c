#include "ui_layout.h"
#include <string.h>
#include <stdio.h>

static ui_state_t g_ui_state;

void ui_layout_init(void) {
    memset(&g_ui_state, 0, sizeof(ui_state_t));
    g_ui_state.current_mode = WORK_MODE_IDLE;
    g_ui_state.current_pet_action = PET_IDLE;
    snprintf(g_ui_state.clock_str, sizeof(g_ui_state.clock_str), "12:00");
}

void ui_layout_update_status_bar(bool wifi, bool mqtt, work_mode_t mode, const char* clock_str) {
    g_ui_state.wifi_connected = wifi;
    g_ui_state.mqtt_connected = mqtt;
    g_ui_state.current_mode = mode;
    if (clock_str) {
        strncpy(g_ui_state.clock_str, clock_str, sizeof(g_ui_state.clock_str) - 1);
        g_ui_state.clock_str[sizeof(g_ui_state.clock_str) - 1] = '\0';
    }
}

void ui_layout_set_pet_action(pet_action_t action) {
    g_ui_state.current_pet_action = action;
}

void ui_layout_show_alert(const alert_payload_t* alert) {
    if (!alert) return;
    g_ui_state.alert_active = true;
    memcpy(&g_ui_state.active_alert, alert, sizeof(alert_payload_t));
    g_ui_state.alert_remaining_sec = alert->duration_sec;
    g_ui_state.current_pet_action = alert->pet_action;
}

void ui_layout_hide_alert(void) {
    g_ui_state.alert_active = false;
    memset(&g_ui_state.active_alert, 0, sizeof(alert_payload_t));
    g_ui_state.alert_remaining_sec = 0;
    // 恢复为当前工作模式对应的桌宠状态
    switch (g_ui_state.current_mode) {
        case WORK_MODE_CODING:
            g_ui_state.current_pet_action = PET_CODING;
            break;
        case WORK_MODE_MEETING:
            g_ui_state.current_pet_action = PET_MEETING;
            break;
        case WORK_MODE_SLEEP:
            g_ui_state.current_pet_action = PET_SLEEP;
            break;
        case WORK_MODE_IDLE:
        default:
            g_ui_state.current_pet_action = PET_IDLE;
            break;
    }
}

const ui_state_t* ui_layout_get_state(void) {
    return &g_ui_state;
}
