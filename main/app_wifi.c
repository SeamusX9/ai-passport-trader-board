#include "app_wifi.h"

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *AP_SSID = "Passport-Board";

static app_wifi_status_cb_t s_cb;
static app_wifi_state_t s_state = APP_WIFI_IDLE;
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static httpd_handle_t s_httpd;
static esp_timer_handle_t s_sta_timer;
static app_config_t s_pending_cfg;
static char s_ip[16];
static bool s_wifi_started;
static bool s_handlers;
static bool s_pending_sta;

static void stop_httpd(void);
static void sta_timer_cb(void *arg);

static void notify(app_wifi_state_t st, const char *detail)
{
    s_state = st;
    if (s_cb) {
        s_cb(st, detail ? detail : "");
    }
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    if (id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        s_ip[0] = '\0';
        if (s_state != APP_WIFI_AP_MODE) {
            notify(APP_WIFI_FAILED, "disconnected");
        }
    } else if (id == WIFI_EVENT_AP_START) {
        notify(APP_WIFI_AP_MODE, AP_SSID);
    }
}

static void ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id != IP_EVENT_STA_GOT_IP) {
        return;
    }
    ip_event_got_ip_t *event = data;
    snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
    notify(APP_WIFI_CONNECTED, s_ip);
}

static esp_err_t url_decode_inplace(char *s)
{
    char *src = s;
    char *dst = s;
    while (*src) {
        if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else if (*src == '%' && src[1] && src[2]) {
            char hex[3] = { src[1], src[2], 0 };
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    return ESP_OK;
}

static bool form_get(const char *body, const char *key, char *out, size_t out_len)
{
    size_t klen = strlen(key);
    const char *p = body;
    while (p && *p) {
        if ((p == body || p[-1] == '&') &&
            strncmp(p, key, klen) == 0 && p[klen] == '=') {
            p += klen + 1;
            size_t i = 0;
            while (*p && *p != '&' && i + 1 < out_len) {
                out[i++] = *p++;
            }
            out[i] = '\0';
            url_decode_inplace(out);
            return true;
        }
        p = strchr(p, '&');
        if (p) {
            p++;
        }
    }
    out[0] = '\0';
    return false;
}

static esp_err_t root_get(httpd_req_t *req)
{
    static const char page[] =
        "<!DOCTYPE html><html><head><meta charset=utf-8>"
        "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
        "<title>Passport Trader Board</title>"
        "<style>body{font-family:sans-serif;max-width:420px;margin:24px auto;padding:0 12px}"
        "label{display:block;margin-top:12px}input{width:100%;padding:8px;box-sizing:border-box}"
        "button{margin-top:16px;padding:10px 16px;width:100%}</style></head><body>"
        "<h2>WiFi / Trader API</h2>"
        "<form method=POST action=/save>"
        "<label>WiFi SSID</label><input name=wifi_ssid required maxlength=31>"
        "<label>WiFi Password</label><input name=wifi_pass type=password maxlength=63>"
        "<label>API Host (PC LAN IP)</label><input name=api_host required "
        "placeholder=192.168.1.10 maxlength=63>"
        "<label>API Port</label><input name=api_port value=8766 maxlength=5>"
        "<button type=submit>Save & Connect</button></form>"
        "<p>Device AP: Passport-Board · http://192.168.4.1</p>"
        "<p>API example: http://HOST:8766/api/traders/profile2/summary</p>"
        "<p>PC firewall must allow TCP 8766 on the LAN.</p>"
        "</body></html>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t save_post(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > 512) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body too large");
        return ESP_FAIL;
    }

    char body[513];
    int received = 0;
    while (received < req->content_len) {
        int r = httpd_req_recv(req, body + received, req->content_len - received);
        if (r <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv fail");
            return ESP_FAIL;
        }
        received += r;
    }
    body[received] = '\0';

    app_config_t cfg;
    app_config_load(&cfg);
    form_get(body, "wifi_ssid", cfg.wifi_ssid, sizeof(cfg.wifi_ssid));
    form_get(body, "wifi_pass", cfg.wifi_pass, sizeof(cfg.wifi_pass));
    form_get(body, "api_host", cfg.api_host, sizeof(cfg.api_host));

    char port_s[8] = {0};
    if (form_get(body, "api_port", port_s, sizeof(port_s))) {
        int port = atoi(port_s);
        if (port > 0 && port < 65536) {
            cfg.api_port = (uint16_t)port;
        }
    }

    if (!app_config_has_wifi(&cfg) || !app_config_has_api(&cfg)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing fields");
        return ESP_FAIL;
    }

    esp_err_t err = app_config_save(&cfg);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "nvs fail");
        return err;
    }

    static const char ok[] =
        "<!DOCTYPE html><html><body><h3>Saved</h3>"
        "<p>Connecting to WiFi... return to the device screen.</p></body></html>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, ok, HTTPD_RESP_USE_STRLEN);

    /* Defer STA switch: stopping httpd inside its own handler is unsafe. */
    s_pending_cfg = cfg;
    s_pending_sta = true;
    if (s_sta_timer) {
        esp_timer_stop(s_sta_timer);
        esp_timer_start_once(s_sta_timer, 500000);
    }
    return ESP_OK;
}

static esp_err_t start_httpd(void)
{
    if (s_httpd) {
        return ESP_OK;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_open_sockets = 3;
    config.lru_purge_enable = true;
    config.stack_size = 6144;
    esp_err_t err = httpd_start(&s_httpd, &config);
    if (err != ESP_OK) {
        return err;
    }
    const httpd_uri_t root = {
        .uri = "/", .method = HTTP_GET, .handler = root_get
    };
    const httpd_uri_t save = {
        .uri = "/save", .method = HTTP_POST, .handler = save_post
    };
    httpd_register_uri_handler(s_httpd, &root);
    httpd_register_uri_handler(s_httpd, &save);
    return ESP_OK;
}

static void stop_httpd(void)
{
    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }
}

static void sta_timer_cb(void *arg)
{
    (void)arg;
    if (!s_pending_sta) {
        return;
    }
    s_pending_sta = false;
    stop_httpd();
    (void)app_wifi_start_sta(&s_pending_cfg);
}

esp_err_t app_wifi_init(app_wifi_status_cb_t cb)
{
    s_cb = cb;
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    if (!s_handlers) {
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                   &wifi_event, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                   &ip_event, NULL));
        s_handlers = true;
    }
    if (!s_sta_netif) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
    }
    if (!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }

    if (!s_sta_timer) {
        const esp_timer_create_args_t args = {
            .callback = &sta_timer_cb,
            .name = "sta_switch",
        };
        ESP_ERROR_CHECK(esp_timer_create(&args, &s_sta_timer));
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    return ESP_OK;
}

void app_wifi_deinit(void)
{
    stop_httpd();
    if (s_wifi_started) {
        esp_wifi_stop();
        s_wifi_started = false;
    }
}

esp_err_t app_wifi_start_sta(const app_config_t *cfg)
{
    if (!cfg || !app_config_has_wifi(cfg)) {
        return ESP_ERR_INVALID_ARG;
    }

    stop_httpd();
    wifi_config_t wcfg = {0};
    strncpy((char *)wcfg.sta.ssid, cfg->wifi_ssid, sizeof(wcfg.sta.ssid) - 1);
    strncpy((char *)wcfg.sta.password, cfg->wifi_pass, sizeof(wcfg.sta.password) - 1);
    wcfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_STA, &wcfg);
    if (err != ESP_OK) {
        return err;
    }
    if (!s_wifi_started) {
        err = esp_wifi_start();
        if (err != ESP_OK) {
            return err;
        }
        s_wifi_started = true;
    } else {
        esp_wifi_disconnect();
        esp_wifi_connect();
    }
    /* Modem sleep often breaks large HTTP body uploads (STT PCM). */
    (void)esp_wifi_set_ps(WIFI_PS_NONE);
    notify(APP_WIFI_CONNECTING, cfg->wifi_ssid);
    return ESP_OK;
}

esp_err_t app_wifi_start_softap(void)
{
    stop_httpd();
    wifi_config_t wcfg = {0};
    strncpy((char *)wcfg.ap.ssid, AP_SSID, sizeof(wcfg.ap.ssid) - 1);
    wcfg.ap.ssid_len = strlen(AP_SSID);
    wcfg.ap.max_connection = 1;
    wcfg.ap.authmode = WIFI_AUTH_OPEN;
    wcfg.ap.channel = 6;

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_AP, &wcfg);
    if (err != ESP_OK) {
        return err;
    }
    if (!s_wifi_started) {
        err = esp_wifi_start();
        if (err != ESP_OK) {
            return err;
        }
        s_wifi_started = true;
    }
    err = start_httpd();
    if (err != ESP_OK) {
        return err;
    }
    notify(APP_WIFI_AP_MODE, AP_SSID);
    return ESP_OK;
}

esp_err_t app_wifi_stop_softap(void)
{
    stop_httpd();
    return ESP_OK;
}

app_wifi_state_t app_wifi_state(void)
{
    return s_state;
}

bool app_wifi_is_connected(void)
{
    return (s_state == APP_WIFI_CONNECTED && s_ip[0] != '\0');
}

const char *app_wifi_ip(void)
{
    return s_ip;
}

const char *app_wifi_ap_ssid(void)
{
    return AP_SSID;
}
