#include "app_traders.h"

#include "esp_http_client.h"
#include "esp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "app_traders";

#define RESP_CAP 12288

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
} resp_acc_t;

static esp_err_t http_event(esp_http_client_event_t *evt)
{
    resp_acc_t *acc = evt->user_data;
    if (evt->event_id != HTTP_EVENT_ON_DATA || !acc || evt->data_len <= 0) {
        return ESP_OK;
    }
    size_t copy = (size_t)evt->data_len;
    if (acc->len + copy >= acc->cap) {
        copy = acc->cap > acc->len + 1 ? acc->cap - acc->len - 1 : 0;
    }
    if (copy > 0) {
        memcpy(acc->buf + acc->len, evt->data, copy);
        acc->len += copy;
        acc->buf[acc->len] = '\0';
    }
    return ESP_OK;
}

static void set_err(char *err, size_t err_cap, const char *msg)
{
    if (err && err_cap > 0 && msg) {
        strncpy(err, msg, err_cap - 1);
        err[err_cap - 1] = '\0';
    }
}

static esp_err_t http_get(const app_config_t *cfg,
                          const char *url,
                          char **resp_out,
                          size_t *len_out,
                          char *err, size_t err_cap)
{
    *resp_out = NULL;
    if (len_out) {
        *len_out = 0;
    }

    char *resp = calloc(1, RESP_CAP);
    if (!resp) {
        set_err(err, err_cap, "内存不足");
        return ESP_ERR_NO_MEM;
    }
    resp_acc_t acc = { .buf = resp, .cap = RESP_CAP, .len = 0 };

    esp_http_client_config_t http_cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 15000,
        .event_handler = http_event,
        .user_data = &acc,
        .buffer_size = 1024,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        free(resp);
        set_err(err, err_cap, "HTTP初始化失败");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "GET %s", url);
    esp_err_t ret = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "http err=%s status=%d bytes=%u",
             esp_err_to_name(ret), status, (unsigned)acc.len);

    if (ret != ESP_OK) {
        if (err && err_cap) {
            if (ret == ESP_ERR_HTTP_CONNECT) {
                snprintf(err, err_cap, "连不上 %s:%u",
                         cfg->api_host, (unsigned)cfg->api_port);
            } else {
                snprintf(err, err_cap, "网络错误:%s", esp_err_to_name(ret));
            }
        }
        esp_http_client_cleanup(client);
        free(resp);
        return ret;
    }

    if (status < 200 || status >= 300) {
        if (err && err_cap) {
            snprintf(err, err_cap, "HTTP错误:%d", status);
        }
        esp_http_client_cleanup(client);
        free(resp);
        return ESP_FAIL;
    }

    esp_http_client_cleanup(client);
    *resp_out = resp;
    if (len_out) {
        *len_out = acc.len;
    }
    return ESP_OK;
}

esp_err_t app_traders_fetch_summary(const app_config_t *cfg,
                                    int profile_id,
                                    app_trader_summary_t *out,
                                    char *err, size_t err_cap)
{
    if (!cfg || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!app_config_has_api(cfg)) {
        set_err(err, err_cap, "请先配置服务器");
        return ESP_ERR_INVALID_STATE;
    }

    profile_id = app_trader_clamp_profile(profile_id);
    app_trader_summary_clear(out);

    char url[192];
    snprintf(url, sizeof(url),
             "http://%s:%u/api/traders/profile%d/summary",
             cfg->api_host, (unsigned)cfg->api_port, profile_id);

    char *resp = NULL;
    esp_err_t ret = http_get(cfg, url, &resp, NULL, err, err_cap);
    if (ret != ESP_OK) {
        return ret;
    }

    if (!app_trader_parse_summary(resp, out)) {
        set_err(err, err_cap, "解析失败");
        ESP_LOGE(TAG, "parse summary fail preview=%.120s", resp);
        free(resp);
        return ESP_ERR_NOT_FOUND;
    }
    out->profile_id = profile_id;
    free(resp);
    return ESP_OK;
}

esp_err_t app_traders_fetch_positions(const app_config_t *cfg,
                                      int profile_id,
                                      app_trader_positions_t *out,
                                      char *err, size_t err_cap)
{
    if (!cfg || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!app_config_has_api(cfg)) {
        set_err(err, err_cap, "请先配置服务器");
        return ESP_ERR_INVALID_STATE;
    }

    profile_id = app_trader_clamp_profile(profile_id);
    app_trader_positions_clear(out);

    char url[192];
    snprintf(url, sizeof(url),
             "http://%s:%u/api/traders/profile%d?fields=positions",
             cfg->api_host, (unsigned)cfg->api_port, profile_id);

    char *resp = NULL;
    esp_err_t ret = http_get(cfg, url, &resp, NULL, err, err_cap);
    if (ret != ESP_OK) {
        return ret;
    }

    if (!app_trader_parse_positions(resp, out)) {
        set_err(err, err_cap, "持仓解析失败");
        ESP_LOGE(TAG, "parse positions fail preview=%.120s", resp);
        free(resp);
        return ESP_ERR_NOT_FOUND;
    }
    free(resp);
    return ESP_OK;
}

esp_err_t app_traders_fetch_latest_trade(const app_config_t *cfg,
                                         int profile_id,
                                         app_trader_trade_t *out,
                                         char *err, size_t err_cap)
{
    if (!cfg || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!app_config_has_api(cfg)) {
        set_err(err, err_cap, "请先配置服务器");
        return ESP_ERR_INVALID_STATE;
    }

    profile_id = app_trader_clamp_profile(profile_id);
    app_trader_trade_clear(out);

    char url[192];
    snprintf(url, sizeof(url),
             "http://%s:%u/api/traders/profile%d?fields=trades",
             cfg->api_host, (unsigned)cfg->api_port, profile_id);

    char *resp = NULL;
    esp_err_t ret = http_get(cfg, url, &resp, NULL, err, err_cap);
    if (ret != ESP_OK) {
        return ret;
    }

    if (!app_trader_parse_latest_trade(resp, out)) {
        set_err(err, err_cap, "成交解析失败");
        ESP_LOGE(TAG, "parse trade fail preview=%.120s", resp);
        free(resp);
        return ESP_ERR_NOT_FOUND;
    }
    free(resp);
    return ESP_OK;
}
