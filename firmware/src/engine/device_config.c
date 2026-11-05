#include "device_config.h"
#include <string.h>

bool device_config_load(device_config_t *out, device_config_reader_t reader, void *ctx) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!reader ||
        !reader(ctx, "wifi_ssid", out->wifi_ssid, sizeof(out->wifi_ssid)) ||
        !reader(ctx, "wifi_password", out->wifi_password, sizeof(out->wifi_password)) ||
        !reader(ctx, "mqtt_username", out->mqtt_username, sizeof(out->mqtt_username)) ||
        !reader(ctx, "mqtt_password", out->mqtt_password, sizeof(out->mqtt_password)) ||
        !out->wifi_ssid[0] || !out->wifi_password[0] ||
        !out->mqtt_username[0] || !out->mqtt_password[0]) {
        memset(out, 0, sizeof(*out));
        return false;
    }
    return true;
}
