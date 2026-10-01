#include "app_config.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "app_config";
static const char *NS = "trader_dash";

void app_config_set_defaults(app_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->api_port = 8766;
}

esp_err_t app_config_load(app_config_t *cfg)
{
    app_config_set_defaults(cfg);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    size_t len = sizeof(cfg->wifi_ssid);
    (void)nvs_get_str(h, "wifi_ssid", cfg->wifi_ssid, &len);
    len = sizeof(cfg->wifi_pass);
    (void)nvs_get_str(h, "wifi_pass", cfg->wifi_pass, &len);
    len = sizeof(cfg->api_host);
    (void)nvs_get_str(h, "api_host", cfg->api_host, &len);

    uint16_t port = 0;
    if (nvs_get_u16(h, "api_port", &port) == ESP_OK && port != 0) {
        cfg->api_port = port;
    }

    nvs_close(h);
    ESP_LOGI(TAG, "loaded api=%s:%u",
             cfg->api_host[0] ? cfg->api_host : "(empty)",
             cfg->api_port);
    return ESP_OK;
}

esp_err_t app_config_save(const app_config_t *cfg)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(h, "wifi_ssid", cfg->wifi_ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(h, "wifi_pass", cfg->wifi_pass);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(h, "api_host", cfg->api_host);
    }
    if (err == ESP_OK) {
        err = nvs_set_u16(h, "api_port", cfg->api_port);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t app_config_clear_wifi(void)
{
    app_config_t cfg;
    esp_err_t err = app_config_load(&cfg);
    if (err != ESP_OK) {
        return err;
    }
    cfg.wifi_ssid[0] = '\0';
    cfg.wifi_pass[0] = '\0';
    return app_config_save(&cfg);
}

bool app_config_has_wifi(const app_config_t *cfg)
{
    return cfg && cfg->wifi_ssid[0] != '\0';
}

bool app_config_has_api(const app_config_t *cfg)
{
    return cfg && cfg->api_host[0] != '\0' && cfg->api_port != 0;
}
