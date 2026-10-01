#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_TRADER_PROFILE_MIN 1
#define APP_TRADER_PROFILE_MAX 5
#define APP_TRADER_NAME_MAX    48
#define APP_TRADER_STRAT_MAX   24
#define APP_TRADER_STATUS_MAX  16
#define APP_TRADER_POS_MAX     10
#define APP_TRADER_POS_NAME_MAX 20
#define APP_TRADER_SIDE_MAX    12
#define APP_TRADER_CODE_MAX    12
#define APP_TRADER_TIME_MAX    24

typedef struct {
    int profile_id;
    char trader_id[24];
    char short_name[APP_TRADER_NAME_MAX];
    char strategy[APP_TRADER_STRAT_MAX];
    char status[APP_TRADER_STATUS_MAX];
    bool running;
    bool ok;
    double total_assets;
    double pnl;
    double pnl_pct;
    double cash;
    double position_pct;
    int holdings_count;
    double day_pnl;
    double day_pnl_pct;
    double win_rate;
} app_trader_summary_t;

typedef struct {
    int count;
    char names[APP_TRADER_POS_MAX][APP_TRADER_POS_NAME_MAX];
} app_trader_positions_t;

typedef struct {
    bool valid;
    char side[APP_TRADER_SIDE_MAX];
    char name[APP_TRADER_NAME_MAX];
    char code[APP_TRADER_CODE_MAX];
    double price;
    int shares;
    char trade_at[APP_TRADER_TIME_MAX];
} app_trader_trade_t;

void app_trader_summary_clear(app_trader_summary_t *out);
void app_trader_positions_clear(app_trader_positions_t *out);
void app_trader_trade_clear(app_trader_trade_t *out);

bool app_trader_parse_summary(const char *json, app_trader_summary_t *out);
bool app_trader_parse_positions(const char *json, app_trader_positions_t *out);
bool app_trader_parse_latest_trade(const char *json, app_trader_trade_t *out);

int app_trader_clamp_profile(int profile_id);
void app_trader_format_money(char *out, size_t cap, double value);
void app_trader_format_signed(char *out, size_t cap, double value);
void app_trader_format_pct(char *out, size_t cap, double value, bool signed_pct);
