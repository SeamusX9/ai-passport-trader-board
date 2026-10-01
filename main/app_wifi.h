#pragma once

#include "app_config.h"
#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    APP_WIFI_IDLE = 0,
    APP_WIFI_CONNECTING,
    APP_WIFI_CONNECTED,
    APP_WIFI_FAILED,
    APP_WIFI_AP_MODE,
} app_wifi_state_t;

typedef void (*app_wifi_status_cb_t)(app_wifi_state_t state, const char *detail);

esp_err_t app_wifi_init(app_wifi_status_cb_t cb);
void app_wifi_deinit(void);

esp_err_t app_wifi_start_sta(const app_config_t *cfg);
esp_err_t app_wifi_start_softap(void);
esp_err_t app_wifi_stop_softap(void);

app_wifi_state_t app_wifi_state(void);
bool app_wifi_is_connected(void);
const char *app_wifi_ip(void);
const char *app_wifi_ap_ssid(void);
