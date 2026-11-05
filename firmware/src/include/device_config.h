#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char wifi_ssid[33];
    char wifi_password[65];
    char mqtt_username[64];
    char mqtt_password[128];
} device_config_t;

// Reader must reject values that do not fit, including the terminating NUL.
typedef bool (*device_config_reader_t)(void *ctx, const char *key, char *out, size_t capacity);
bool device_config_load(device_config_t *out, device_config_reader_t reader, void *ctx);

#endif
