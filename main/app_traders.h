#pragma once

#include "app_config.h"
#include "app_traders_logic.h"
#include "esp_err.h"

esp_err_t app_traders_fetch_summary(const app_config_t *cfg,
                                    int profile_id,
                                    app_trader_summary_t *out,
                                    char *err, size_t err_cap);

esp_err_t app_traders_fetch_positions(const app_config_t *cfg,
                                      int profile_id,
                                      app_trader_positions_t *out,
                                      char *err, size_t err_cap);

esp_err_t app_traders_fetch_latest_trade(const app_config_t *cfg,
                                         int profile_id,
                                         app_trader_trade_t *out,
                                         char *err, size_t err_cap);
