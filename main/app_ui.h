#pragma once

#include "bsp_button.h"
#include "esp_err.h"

esp_err_t app_ui_start(void);
void app_ui_on_key(bsp_btn_t btn, bsp_btn_ev_t ev);
