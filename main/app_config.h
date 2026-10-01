// Persistent Wi-Fi and trader API settings (NVS).
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_WIFI_SSID_MAX 32
#define APP_WIFI_PASS_MAX 64
#define APP_API_HOST_MAX  64

typedef struct {
    char wifi_ssid[APP_WIFI_SSID_MAX];
    char wifi_pass[APP_WIFI_PASS_MAX];
    char api_host[APP_API_HOST_MAX];
    uint16_t api_port;
} app_config_t;

void app_config_set_defaults(app_config_t *cfg);
esp_err_t app_config_load(app_config_t *cfg);
esp_err_t app_config_save(const app_config_t *cfg);
esp_err_t app_config_clear_wifi(void);
bool app_config_has_wifi(const app_config_t *cfg);
bool app_config_has_api(const app_config_t *cfg);
