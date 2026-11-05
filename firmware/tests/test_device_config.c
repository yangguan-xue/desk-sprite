#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "device_config.h"

typedef struct { const char *ssid, *wifi_password, *mqtt_username, *mqtt_password; } values_t;

static bool read_value(void *ctx, const char *key, char *out, size_t capacity) {
    values_t *values = ctx;
    const char *value = NULL;
    if (!strcmp(key, "wifi_ssid")) value = values->ssid;
    if (!strcmp(key, "wifi_password")) value = values->wifi_password;
    if (!strcmp(key, "mqtt_username")) value = values->mqtt_username;
    if (!strcmp(key, "mqtt_password")) value = values->mqtt_password;
    if (!value || strlen(value) >= capacity) return false;
    strcpy(out, value);
    return true;
}

int main(void) {
    device_config_t config;
    values_t values = {"test-network", "test-wifi-password", "dev_test", "test-mqtt-password"};
    assert(device_config_load(&config, read_value, &values));
    assert(!strcmp(config.wifi_ssid, values.ssid));
    assert(!strcmp(config.mqtt_password, values.mqtt_password));
    values.mqtt_password = NULL;
    assert(!device_config_load(&config, read_value, &values));
    device_config_t cleared = {0};
    assert(!memcmp(&config, &cleared, sizeof(config)));
    values.mqtt_password = "";
    assert(!device_config_load(&config, read_value, &values));
    values.mqtt_password = "test-mqtt-password";
    values.ssid = "12345678901234567890123456789012345";
    assert(!device_config_load(&config, read_value, &values));
    values.ssid = "12345678901234567890123456789012";
    assert(device_config_load(&config, read_value, &values));
    assert(!device_config_load(NULL, read_value, &values));
    puts("PASS device configuration bounds, missing credentials and clearing");
}
