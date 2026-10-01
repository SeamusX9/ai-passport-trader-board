#pragma once

#include "lvgl.h"

LV_FONT_DECLARE(app_font_16);

/* Writable descriptor: Chinese+ASCII primary, Montserrat fallback for symbols. */
const lv_font_t *app_font(void);
void app_fonts_init(void);
