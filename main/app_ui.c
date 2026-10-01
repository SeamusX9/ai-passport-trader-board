#include "app_ui.h"

#include "app_config.h"
#include "app_fonts.h"
#include "app_traders.h"
#include "app_traders_logic.h"
#include "app_wifi.h"

#include "bsp_battery.h"
#include "bsp_display.h"
#include "lvgl.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "app_ui";

#define COL_BG     0x0A1218
#define COL_PANEL  0x142028
#define COL_LINE   0x3D8BFF
#define COL_TEXT   0xFFFFFF
#define COL_MUTED  0x7A8B9A
#define COL_SELECT 0x1A2E3C
#define COL_POS    0xE74C3C  /* A-share: positive = red */
#define COL_NEG    0x2ECC71  /* A-share: negative = green */
#define COL_WARN   0xF0A020

typedef enum {
    PAGE_HOME = 0,
    PAGE_BOARD,
    PAGE_SETUP,
} page_t;

typedef enum {
    CMD_START_AP = 1,
    CMD_START_STA,
    CMD_FETCH_SUMMARY,
    CMD_FETCH_POSITIONS,
    CMD_FETCH_TRADE,
} worker_cmd_t;

typedef struct {
    worker_cmd_t cmd;
    int profile_id;
} worker_msg_t;

typedef enum {
    EVT_WIFI = 1,
    EVT_PHASE,
    EVT_SUMMARY,
    EVT_POSITIONS,
    EVT_TRADE,
    EVT_FAIL,
} ui_evt_t;

typedef enum {
    BOARD_VIEW_SUMMARY = 0,
    BOARD_VIEW_POSITIONS,
    BOARD_VIEW_TRADE,
} board_view_t;

typedef struct {
    ui_evt_t type;
    app_wifi_state_t wifi_state;
    app_trader_summary_t summary;
    app_trader_positions_t positions;
    app_trader_trade_t trade;
    char text[128];
} ui_evt_msg_t;

static page_t s_page = PAGE_HOME;
static int s_home_sel;
static int s_profile = APP_TRADER_PROFILE_MIN;
static bool s_busy;
static board_view_t s_board_view = BOARD_VIEW_SUMMARY;
static app_trader_summary_t s_summary;
static bool s_has_summary;
static app_trader_positions_t s_positions;
static bool s_has_positions;
static app_trader_trade_t s_trade;
static bool s_has_trade;

static lv_obj_t *s_scr;
static lv_obj_t *s_brand;
static lv_obj_t *s_sub;
static lv_obj_t *s_status;
static lv_obj_t *s_battery;
static lv_obj_t *s_body;
static lv_obj_t *s_hint;
static lv_obj_t *s_board_title;
static lv_obj_t *s_row_assets;
static lv_obj_t *s_row_pnl;
static lv_obj_t *s_row_day;
static lv_obj_t *s_row_pos;
static lv_obj_t *s_row_cash;
static lv_obj_t *s_row_hold;
static lv_obj_t *s_board_hint;

static app_config_t s_cfg;
static QueueHandle_t s_worker_q;
static QueueHandle_t s_ui_q;
static lv_timer_t *s_poll;

static void style_label(lv_obj_t *obj, const lv_font_t *font, uint32_t color)
{
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
}

static void apply_cjk_font(lv_obj_t *obj)
{
    style_label(obj, app_font(), COL_TEXT);
}

static void wifi_status_cb(app_wifi_state_t state, const char *detail)
{
    if (!s_ui_q) {
        return;
    }
    ui_evt_msg_t msg = { .type = EVT_WIFI, .wifi_state = state };
    if (detail) {
        strncpy(msg.text, detail, sizeof(msg.text) - 1);
    }
    (void)xQueueSend(s_ui_q, &msg, 0);
}

static void post_phase(const char *phase)
{
    if (!s_ui_q || !phase) {
        return;
    }
    ui_evt_msg_t msg = { .type = EVT_PHASE };
    strncpy(msg.text, phase, sizeof(msg.text) - 1);
    (void)xQueueSend(s_ui_q, &msg, 0);
}

static const char *wifi_state_text(app_wifi_state_t st)
{
    switch (st) {
    case APP_WIFI_CONNECTED: return "网络已连接";
    case APP_WIFI_CONNECTING: return "正在连接网络";
    case APP_WIFI_AP_MODE: return "配网热点已开";
    case APP_WIFI_FAILED: return "网络未连接";
    default: return "网络空闲";
    }
}

static void refresh_status_locked(void)
{
    if (!s_status) {
        return;
    }
    char line[96];
    const char *net = wifi_state_text(app_wifi_state());
    const char *ip = app_wifi_ip();
    if (ip && ip[0]) {
        snprintf(line, sizeof(line), "%s  %s", net, ip);
    } else if (app_wifi_state() == APP_WIFI_AP_MODE) {
        snprintf(line, sizeof(line), "%s  %s", net, app_wifi_ap_ssid());
    } else {
        snprintf(line, sizeof(line), "%s", net);
    }
    lv_label_set_text(s_status, line);

    int soc = bsp_battery_soc();
    if (s_battery) {
        if (soc < 0) {
            lv_label_set_text(s_battery, "");
        } else {
            lv_label_set_text_fmt(s_battery, "%d%%", soc);
        }
    }
}

static void clear_body(void)
{
    if (s_body) {
        lv_obj_clean(s_body);
    }
    s_board_title = NULL;
    s_row_assets = NULL;
    s_row_pnl = NULL;
    s_row_day = NULL;
    s_row_pos = NULL;
    s_row_cash = NULL;
    s_row_hold = NULL;
    s_board_hint = NULL;
}

static void paint_home(void)
{
    clear_body();
    lv_label_set_text(s_brand, "AI Passport");
    lv_label_set_text(s_sub, "交易看板");
    lv_label_set_text(s_hint, "上/下切换  确定进入");

    static const char *labels[2] = { "打开看板", "打开配网" };
    for (int i = 0; i < 2; ++i) {
        lv_obj_t *row = lv_obj_create(s_body);
        lv_obj_set_size(row, 216, 44);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_border_width(row, 2, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(i == s_home_sel ? COL_LINE : COL_PANEL), 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(i == s_home_sel ? COL_SELECT : COL_PANEL), 0);
        lv_obj_set_style_pad_all(row, 8, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lab = lv_label_create(row);
        apply_cjk_font(lab);
        lv_label_set_text(lab, labels[i]);
        lv_obj_center(lab);
    }

    lv_obj_t *meta = lv_label_create(s_body);
    apply_cjk_font(meta);
    style_label(meta, lv_obj_get_style_text_font(meta, 0), COL_MUTED);
    char info[160];
    if (app_config_has_api(&s_cfg)) {
        snprintf(info, sizeof(info),
                 "服务器\n%s:%u\n档位 profile%d",
                 s_cfg.api_host, s_cfg.api_port, s_profile);
    } else {
        snprintf(info, sizeof(info), "尚未配置服务器\n请先打开配网");
    }
    lv_label_set_text(meta, info);
    lv_label_set_long_mode(meta, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(meta, 216);
}

static uint32_t signed_color(double v)
{
    if (v > 0.0005) {
        return COL_POS;
    }
    if (v < -0.0005) {
        return COL_NEG;
    }
    return COL_TEXT;
}

static void set_metric_row(lv_obj_t *lab, const char *name, const char *value, uint32_t value_color)
{
    if (!lab) {
        return;
    }
    char line[128];
    snprintf(line, sizeof(line), "%s  #%06x %s#", name,
             (unsigned)(value_color & 0xFFFFFFu), value);
    lv_label_set_recolor(lab, true);
    style_label(lab, app_font(), COL_TEXT);
    lv_label_set_text(lab, line);
}

static void hide_metric_rows(void)
{
    if (s_row_assets) {
        lv_obj_add_flag(s_row_assets, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_row_pnl) {
        lv_obj_add_flag(s_row_pnl, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_row_day) {
        lv_obj_add_flag(s_row_day, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_row_pos) {
        lv_obj_add_flag(s_row_pos, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_row_cash) {
        lv_obj_add_flag(s_row_cash, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_row_hold) {
        lv_obj_add_flag(s_row_hold, LV_OBJ_FLAG_HIDDEN);
    }
}

static void show_board_message(const char *msg, uint32_t color)
{
    hide_metric_rows();
    if (s_board_hint) {
        lv_obj_clear_flag(s_board_hint, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_long_mode(s_board_hint, LV_LABEL_LONG_WRAP);
        lv_label_set_text(s_board_hint, msg ? msg : "");
        style_label(s_board_hint, app_font(), color);
    }
}

static void fill_board_labels(void)
{
    if (!s_board_title) {
        return;
    }

    char title[64];
    if (s_has_summary && s_summary.short_name[0]) {
        snprintf(title, sizeof(title), "%s", s_summary.short_name);
    } else {
        snprintf(title, sizeof(title), "档位 %d", s_profile);
    }
    lv_label_set_text(s_board_title, title);
    style_label(s_board_title, app_font(), COL_TEXT);

    if (s_hint) {
        if (s_board_view == BOARD_VIEW_SUMMARY) {
            lv_label_set_text(s_hint, "确定看持仓  上/下换档  长按返回");
        } else if (s_board_view == BOARD_VIEW_POSITIONS) {
            lv_label_set_text(s_hint, "确定看成交  长按回资产");
        } else {
            lv_label_set_text(s_hint, "确定回资产  长按回资产");
        }
    }

    if (s_board_view == BOARD_VIEW_POSITIONS) {
        if (!s_has_positions) {
            show_board_message("暂无持仓数据", COL_MUTED);
            return;
        }
        if (s_positions.count <= 0) {
            show_board_message("当前无持仓", COL_MUTED);
            return;
        }
        char body[220];
        size_t used = 0;
        body[0] = '\0';
        for (int i = 0; i < s_positions.count; ++i) {
            int n = snprintf(body + used, sizeof(body) - used, "%s%s",
                             (i == 0) ? "" : "\n", s_positions.names[i]);
            if (n < 0 || (size_t)n >= sizeof(body) - used) {
                break;
            }
            used += (size_t)n;
        }
        show_board_message(body, COL_TEXT);
        if (s_sub) {
            lv_label_set_text(s_sub, "持仓股票");
        }
        return;
    }

    if (s_board_view == BOARD_VIEW_TRADE) {
        if (!s_has_trade || !s_trade.valid) {
            show_board_message("暂无成交记录", COL_MUTED);
            return;
        }
        char price[24];
        app_trader_format_money(price, sizeof(price), s_trade.price);
        char body[220];
        snprintf(body, sizeof(body),
                 "%s\n"
                 "%s\n"
                 "价格  %s\n"
                 "数量  %d\n"
                 "%s",
                 s_trade.side[0] ? s_trade.side : "-",
                 s_trade.name[0] ? s_trade.name : (s_trade.code[0] ? s_trade.code : "-"),
                 price,
                 s_trade.shares,
                 s_trade.trade_at[0] ? s_trade.trade_at : "");
        uint32_t color = COL_TEXT;
        if (strstr(s_trade.side, "买") || strstr(s_trade.side, "BUY") ||
            strstr(s_trade.side, "Buy")) {
            color = COL_POS;
        } else if (strstr(s_trade.side, "卖") || strstr(s_trade.side, "SELL") ||
                   strstr(s_trade.side, "Sell")) {
            color = COL_NEG;
        }
        show_board_message(body, color);
        if (s_sub) {
            lv_label_set_text(s_sub, "最近成交");
        }
        return;
    }

    /* summary */
    if (!s_has_summary) {
        show_board_message("按确定拉取远程数据", COL_MUTED);
        return;
    }

    if (s_board_hint) {
        lv_obj_add_flag(s_board_hint, LV_OBJ_FLAG_HIDDEN);
    }

    char money[24], pnl[48], day[48], pos[16], cash[24], hold[16];
    char pnl_n[24], pnl_p[16], day_n[24], day_p[16];

    app_trader_format_money(money, sizeof(money), s_summary.total_assets);
    app_trader_format_signed(pnl_n, sizeof(pnl_n), s_summary.pnl);
    app_trader_format_pct(pnl_p, sizeof(pnl_p), s_summary.pnl_pct, true);
    snprintf(pnl, sizeof(pnl), "%s (%s)", pnl_n, pnl_p);

    app_trader_format_signed(day_n, sizeof(day_n), s_summary.day_pnl);
    app_trader_format_pct(day_p, sizeof(day_p), s_summary.day_pnl_pct, true);
    snprintf(day, sizeof(day), "%s (%s)", day_n, day_p);

    app_trader_format_pct(pos, sizeof(pos), s_summary.position_pct, false);
    app_trader_format_money(cash, sizeof(cash), s_summary.cash);
    snprintf(hold, sizeof(hold), "%d", s_summary.holdings_count);

    if (s_row_assets) {
        lv_obj_clear_flag(s_row_assets, LV_OBJ_FLAG_HIDDEN);
        set_metric_row(s_row_assets, "总资产", money, COL_TEXT);
    }
    if (s_row_pnl) {
        lv_obj_clear_flag(s_row_pnl, LV_OBJ_FLAG_HIDDEN);
        set_metric_row(s_row_pnl, "盈亏", pnl, signed_color(s_summary.pnl));
    }
    if (s_row_day) {
        lv_obj_clear_flag(s_row_day, LV_OBJ_FLAG_HIDDEN);
        set_metric_row(s_row_day, "今日", day, signed_color(s_summary.day_pnl));
    }
    if (s_row_pos) {
        lv_obj_clear_flag(s_row_pos, LV_OBJ_FLAG_HIDDEN);
        set_metric_row(s_row_pos, "仓位", pos, COL_TEXT);
    }
    if (s_row_cash) {
        lv_obj_clear_flag(s_row_cash, LV_OBJ_FLAG_HIDDEN);
        set_metric_row(s_row_cash, "现金", cash, COL_TEXT);
    }
    if (s_row_hold) {
        lv_obj_clear_flag(s_row_hold, LV_OBJ_FLAG_HIDDEN);
        set_metric_row(s_row_hold, "持仓", hold, COL_TEXT);
    }
}

static lv_obj_t *make_metric_label(lv_obj_t *parent)
{
    lv_obj_t *lab = lv_label_create(parent);
    apply_cjk_font(lab);
    style_label(lab, app_font(), COL_TEXT);
    lv_label_set_recolor(lab, true);
    lv_obj_set_width(lab, 196);
    lv_label_set_long_mode(lab, LV_LABEL_LONG_CLIP);
    return lab;
}

static void paint_board(void)
{
    clear_body();
    lv_label_set_text(s_brand, "操盘看板");
    lv_label_set_text(s_sub, s_busy ? "查询中…" : "动态查询");
    lv_label_set_text(s_hint, "确定看持仓  上/下换档  长按返回");

    lv_obj_t *card = lv_obj_create(s_body);
    lv_obj_set_size(card, 216, 176);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COL_LINE), 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 4, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_AUTO);

    s_board_title = lv_label_create(card);
    apply_cjk_font(s_board_title);
    style_label(s_board_title, app_font(), COL_TEXT);
    lv_obj_set_width(s_board_title, 196);
    lv_label_set_long_mode(s_board_title, LV_LABEL_LONG_CLIP);

    lv_obj_t *rule = lv_obj_create(card);
    lv_obj_set_size(rule, 196, 2);
    lv_obj_set_style_bg_color(rule, lv_color_hex(0x243848), 0);
    lv_obj_set_style_border_width(rule, 0, 0);
    lv_obj_set_style_radius(rule, 0, 0);
    lv_obj_clear_flag(rule, LV_OBJ_FLAG_SCROLLABLE);

    s_board_hint = make_metric_label(card);
    s_row_assets = make_metric_label(card);
    s_row_pnl = make_metric_label(card);
    s_row_day = make_metric_label(card);
    s_row_pos = make_metric_label(card);
    s_row_cash = make_metric_label(card);
    s_row_hold = make_metric_label(card);

    fill_board_labels();
}

static void paint_setup(void)
{
    clear_body();
    lv_label_set_text(s_brand, "配网");
    lv_label_set_text(s_sub, "手机连接热点填写");
    lv_label_set_text(s_hint, "确定开热点  长按返回");

    lv_obj_t *lab = lv_label_create(s_body);
    apply_cjk_font(lab);
    lv_obj_set_width(lab, 216);
    lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);
    char text[320];
    snprintf(text, sizeof(text),
             "1. 确定开启热点 %s\n"
             "2. 手机打开 http://192.168.4.1\n"
             "3. 填 WiFi 与服务器地址\n"
             "4. 端口默认 8766\n"
             "5. 保存后设备自动联网",
             app_wifi_ap_ssid());
    lv_label_set_text(lab, text);
}

static void rebuild_page(void)
{
    if (!bsp_lvgl_lock(500)) {
        return;
    }
    refresh_status_locked();
    if (s_page == PAGE_HOME) {
        paint_home();
    } else if (s_page == PAGE_BOARD) {
        paint_board();
    } else {
        paint_setup();
    }
    bsp_lvgl_unlock();
}

static void queue_worker(worker_cmd_t cmd)
{
    worker_msg_t msg = { .cmd = cmd, .profile_id = s_profile };
    if (xQueueSend(s_worker_q, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "worker queue full");
        if (cmd == CMD_FETCH_SUMMARY || cmd == CMD_FETCH_POSITIONS ||
            cmd == CMD_FETCH_TRADE) {
            s_busy = false;
        }
    }
}

static bool prepare_net(ui_evt_msg_t *ev)
{
    if (!app_wifi_is_connected()) {
        ev->type = EVT_FAIL;
        strncpy(ev->text, "请先联网", sizeof(ev->text) - 1);
        return false;
    }
    if (!app_config_has_api(&s_cfg)) {
        ev->type = EVT_FAIL;
        strncpy(ev->text, "请配网填写服务器", sizeof(ev->text) - 1);
        return false;
    }
    return true;
}

static void request_fetch_cmd(worker_cmd_t cmd, const char *phase)
{
    if (s_busy) {
        return;
    }
    s_busy = true;
    post_phase(phase ? phase : "查询中…");
    queue_worker(cmd);
}

static void request_fetch_summary(void)
{
    request_fetch_cmd(CMD_FETCH_SUMMARY, "查询中…");
}

static void reset_board_cache(void)
{
    s_board_view = BOARD_VIEW_SUMMARY;
    s_has_summary = false;
    s_has_positions = false;
    s_has_trade = false;
    app_trader_summary_clear(&s_summary);
    app_trader_positions_clear(&s_positions);
    app_trader_trade_clear(&s_trade);
}

static void run_fetch_summary(int profile_id)
{
    ui_evt_msg_t ev = {0};
    if (!prepare_net(&ev)) {
        (void)xQueueSend(s_ui_q, &ev, portMAX_DELAY);
        return;
    }
    post_phase("拉取资产…");
    app_trader_summary_t summary;
    char err[96] = {0};
    esp_err_t ret = app_traders_fetch_summary(&s_cfg, profile_id, &summary, err, sizeof(err));
    if (ret != ESP_OK) {
        ev.type = EVT_FAIL;
        strncpy(ev.text, err[0] ? err : "查询失败", sizeof(ev.text) - 1);
        (void)xQueueSend(s_ui_q, &ev, portMAX_DELAY);
        return;
    }
    ev.type = EVT_SUMMARY;
    ev.summary = summary;
    (void)xQueueSend(s_ui_q, &ev, portMAX_DELAY);
}

static void run_fetch_positions(int profile_id)
{
    ui_evt_msg_t ev = {0};
    if (!prepare_net(&ev)) {
        (void)xQueueSend(s_ui_q, &ev, portMAX_DELAY);
        return;
    }
    post_phase("拉取持仓…");
    app_trader_positions_t positions;
    char err[96] = {0};
    esp_err_t ret = app_traders_fetch_positions(&s_cfg, profile_id, &positions, err, sizeof(err));
    if (ret != ESP_OK) {
        ev.type = EVT_FAIL;
        strncpy(ev.text, err[0] ? err : "持仓查询失败", sizeof(ev.text) - 1);
        (void)xQueueSend(s_ui_q, &ev, portMAX_DELAY);
        return;
    }
    ev.type = EVT_POSITIONS;
    ev.positions = positions;
    (void)xQueueSend(s_ui_q, &ev, portMAX_DELAY);
}

static void run_fetch_trade(int profile_id)
{
    ui_evt_msg_t ev = {0};
    if (!prepare_net(&ev)) {
        (void)xQueueSend(s_ui_q, &ev, portMAX_DELAY);
        return;
    }
    post_phase("拉取成交…");
    app_trader_trade_t trade;
    char err[96] = {0};
    esp_err_t ret = app_traders_fetch_latest_trade(&s_cfg, profile_id, &trade, err, sizeof(err));
    if (ret != ESP_OK) {
        ev.type = EVT_FAIL;
        strncpy(ev.text, err[0] ? err : "成交查询失败", sizeof(ev.text) - 1);
        (void)xQueueSend(s_ui_q, &ev, portMAX_DELAY);
        return;
    }
    ev.type = EVT_TRADE;
    ev.trade = trade;
    (void)xQueueSend(s_ui_q, &ev, portMAX_DELAY);
}

static void worker_task(void *arg)
{
    (void)arg;
    worker_msg_t msg;
    for (;;) {
        if (xQueueReceive(s_worker_q, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (msg.cmd == CMD_START_AP) {
            esp_err_t err = app_wifi_start_softap();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "softap fail %s", esp_err_to_name(err));
            }
        } else if (msg.cmd == CMD_START_STA) {
            if (app_config_has_wifi(&s_cfg)) {
                (void)app_wifi_start_sta(&s_cfg);
            }
        } else if (msg.cmd == CMD_FETCH_SUMMARY) {
            run_fetch_summary(msg.profile_id);
        } else if (msg.cmd == CMD_FETCH_POSITIONS) {
            run_fetch_positions(msg.profile_id);
        } else if (msg.cmd == CMD_FETCH_TRADE) {
            run_fetch_trade(msg.profile_id);
        }
    }
}

static void ui_event_task(void *arg)
{
    (void)arg;
    ui_evt_msg_t ev;
    for (;;) {
        if (xQueueReceive(s_ui_q, &ev, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!bsp_lvgl_lock(500)) {
            continue;
        }
        if (ev.type == EVT_WIFI) {
            if (ev.wifi_state == APP_WIFI_CONNECTED) {
                app_config_load(&s_cfg);
                if (s_page == PAGE_BOARD && !s_busy) {
                    bsp_lvgl_unlock();
                    request_fetch_summary();
                    continue;
                }
            }
            refresh_status_locked();
        } else if (ev.type == EVT_PHASE) {
            if (s_page == PAGE_BOARD && s_sub) {
                lv_label_set_text(s_sub, ev.text);
            }
        } else if (ev.type == EVT_SUMMARY) {
            s_busy = false;
            s_summary = ev.summary;
            s_has_summary = true;
            s_board_view = BOARD_VIEW_SUMMARY;
            s_profile = app_trader_clamp_profile(ev.summary.profile_id);
            if (s_page == PAGE_BOARD) {
                if (s_sub) {
                    lv_label_set_text(s_sub, "资产仓位");
                }
                fill_board_labels();
            }
            refresh_status_locked();
        } else if (ev.type == EVT_POSITIONS) {
            s_busy = false;
            s_positions = ev.positions;
            s_has_positions = true;
            s_board_view = BOARD_VIEW_POSITIONS;
            if (s_page == PAGE_BOARD) {
                fill_board_labels();
            }
            refresh_status_locked();
        } else if (ev.type == EVT_TRADE) {
            s_busy = false;
            s_trade = ev.trade;
            s_has_trade = true;
            s_board_view = BOARD_VIEW_TRADE;
            if (s_page == PAGE_BOARD) {
                fill_board_labels();
            }
            refresh_status_locked();
        } else if (ev.type == EVT_FAIL) {
            s_busy = false;
            if (s_page == PAGE_BOARD) {
                if (s_sub) {
                    lv_label_set_text(s_sub, "查询失败");
                }
                show_board_message(ev.text, COL_WARN);
            }
            refresh_status_locked();
        }
        bsp_lvgl_unlock();
    }
}

static void battery_timer(lv_timer_t *t)
{
    (void)t;
    refresh_status_locked();
}

static void build_shell(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_grad_color(s_scr, lv_color_hex(0x132436), 0);
    lv_obj_set_style_bg_grad_dir(s_scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *stripe = lv_obj_create(s_scr);
    lv_obj_set_size(stripe, 240, 4);
    lv_obj_align(stripe, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(stripe, lv_color_hex(COL_LINE), 0);
    lv_obj_set_style_border_width(stripe, 0, 0);
    lv_obj_set_style_radius(stripe, 0, 0);
    lv_obj_clear_flag(stripe, LV_OBJ_FLAG_SCROLLABLE);

    s_brand = lv_label_create(s_scr);
    style_label(s_brand, app_font(), COL_TEXT);
    lv_obj_align(s_brand, LV_ALIGN_TOP_LEFT, 12, 14);

    s_battery = lv_label_create(s_scr);
    style_label(s_battery, app_font(), COL_MUTED);
    lv_obj_align(s_battery, LV_ALIGN_TOP_RIGHT, -12, 16);

    s_sub = lv_label_create(s_scr);
    apply_cjk_font(s_sub);
    style_label(s_sub, lv_obj_get_style_text_font(s_sub, 0), COL_LINE);
    lv_obj_align(s_sub, LV_ALIGN_TOP_LEFT, 12, 42);

    s_status = lv_label_create(s_scr);
    apply_cjk_font(s_status);
    style_label(s_status, lv_obj_get_style_text_font(s_status, 0), COL_MUTED);
    lv_obj_set_width(s_status, 216);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_CLIP);
    lv_obj_align(s_status, LV_ALIGN_TOP_LEFT, 12, 66);

    s_body = lv_obj_create(s_scr);
    lv_obj_set_size(s_body, 228, 200);
    lv_obj_align(s_body, LV_ALIGN_TOP_MID, 0, 92);
    lv_obj_set_style_bg_opa(s_body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_body, 0, 0);
    lv_obj_set_style_pad_all(s_body, 4, 0);
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_body, 6, 0);
    lv_obj_set_scrollbar_mode(s_body, LV_SCROLLBAR_MODE_OFF);

    s_hint = lv_label_create(s_scr);
    apply_cjk_font(s_hint);
    style_label(s_hint, lv_obj_get_style_text_font(s_hint, 0), COL_MUTED);
    lv_obj_set_width(s_hint, 216);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -10);

    lv_screen_load(s_scr);
}

void app_ui_on_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK && ev != BSP_BTN_LONG) {
        return;
    }

    if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
        if (s_page == PAGE_BOARD && s_board_view != BOARD_VIEW_SUMMARY) {
            s_board_view = BOARD_VIEW_SUMMARY;
            s_busy = false;
            if (!bsp_lvgl_lock(500)) {
                return;
            }
            if (s_sub) {
                lv_label_set_text(s_sub, "资产仓位");
            }
            fill_board_labels();
            bsp_lvgl_unlock();
            return;
        }
        if (s_page != PAGE_HOME) {
            s_page = PAGE_HOME;
            s_busy = false;
            s_board_view = BOARD_VIEW_SUMMARY;
            rebuild_page();
        }
        return;
    }
    if (ev != BSP_BTN_CLICK) {
        return;
    }

    if (s_page == PAGE_HOME) {
        if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
            s_home_sel = (s_home_sel + 1) % 2;
            rebuild_page();
        } else if (btn == BSP_BTN_OK) {
            if (s_home_sel == 0) {
                s_page = PAGE_BOARD;
                reset_board_cache();
                rebuild_page();
                request_fetch_summary();
            } else {
                s_page = PAGE_SETUP;
                rebuild_page();
            }
        }
        return;
    }

    if (s_page == PAGE_BOARD) {
        if (btn == BSP_BTN_UP) {
            s_profile = (s_profile <= APP_TRADER_PROFILE_MIN)
                            ? APP_TRADER_PROFILE_MAX
                            : (s_profile - 1);
            reset_board_cache();
            rebuild_page();
            request_fetch_summary();
        } else if (btn == BSP_BTN_DOWN) {
            s_profile = (s_profile >= APP_TRADER_PROFILE_MAX)
                            ? APP_TRADER_PROFILE_MIN
                            : (s_profile + 1);
            reset_board_cache();
            rebuild_page();
            request_fetch_summary();
        } else if (btn == BSP_BTN_OK) {
            if (s_busy) {
                return;
            }
            if (s_board_view == BOARD_VIEW_SUMMARY) {
                request_fetch_cmd(CMD_FETCH_POSITIONS, "拉取持仓…");
            } else if (s_board_view == BOARD_VIEW_POSITIONS) {
                request_fetch_cmd(CMD_FETCH_TRADE, "拉取成交…");
            } else {
                s_board_view = BOARD_VIEW_SUMMARY;
                if (!bsp_lvgl_lock(500)) {
                    return;
                }
                if (s_sub) {
                    lv_label_set_text(s_sub, "资产仓位");
                }
                fill_board_labels();
                bsp_lvgl_unlock();
            }
        }
        return;
    }

    if (s_page == PAGE_SETUP && btn == BSP_BTN_OK) {
        queue_worker(CMD_START_AP);
        if (!bsp_lvgl_lock(500)) {
            return;
        }
        refresh_status_locked();
        bsp_lvgl_unlock();
    }
}

esp_err_t app_ui_start(void)
{
    esp_err_t err = app_config_load(&s_cfg);
    if (err != ESP_OK) {
        return err;
    }
    reset_board_cache();

    s_worker_q = xQueueCreate(4, sizeof(worker_msg_t));
    s_ui_q = xQueueCreate(8, sizeof(ui_evt_msg_t));
    if (!s_worker_q || !s_ui_q) {
        return ESP_ERR_NO_MEM;
    }

    err = app_wifi_init(wifi_status_cb);
    if (err != ESP_OK) {
        return err;
    }

    if (xTaskCreate(worker_task, "net_worker", 10240, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(ui_event_task, "ui_evt", 4096, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    if (!bsp_lvgl_lock(1000)) {
        return ESP_ERR_TIMEOUT;
    }
    app_fonts_init();
    build_shell();
    paint_home();
    refresh_status_locked();
    s_poll = lv_timer_create(battery_timer, 5000, NULL);
    (void)s_poll;
    bsp_lvgl_unlock();

    if (app_config_has_wifi(&s_cfg)) {
        queue_worker(CMD_START_STA);
    }

    ESP_LOGI(TAG, "UI ready (trader board)");
    return ESP_OK;
}
