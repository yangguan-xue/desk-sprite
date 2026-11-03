#ifndef SIM_MQTT_H
#define SIM_MQTT_H

#include "sprite_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*alert_callback_t)(const alert_payload_t* alert);
typedef void (*metrics_callback_t)(const metrics_payload_t* metrics);
typedef void (*state_callback_t)(work_mode_t mode);
typedef void (*dismiss_callback_t)(void);

typedef struct {
    const char* broker_host;
    int broker_port;
    const char* device_id;
    const char* username;
    const char* password;
    const char* ca_cert;
    bool tls_enabled;
    bool strict_mqtt;
    alert_callback_t on_alert;
    metrics_callback_t on_metrics;
    state_callback_t on_state;
    dismiss_callback_t on_dismiss;
} sim_mqtt_config_t;

int sim_mqtt_init(const sim_mqtt_config_t* config);
int sim_mqtt_loop(int timeout_ms);
bool sim_mqtt_is_connected(void);
int sim_mqtt_publish_action(const device_action_t* action);
int sim_mqtt_publish_status(const char* status_str);
void sim_mqtt_destroy(void);

// 供模拟器注入或解析 payload
void sim_mqtt_handle_message(const char* topic, const char* payload);

#ifdef __cplusplus
}
#endif

#endif // SIM_MQTT_H
