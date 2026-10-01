#include "app_fonts.h"

static lv_font_t s_app_font;

void app_fonts_init(void)
{
    s_app_font = app_font_16;
    s_app_font.fallback = &lv_font_montserrat_14;
}

const lv_font_t *app_font(void)
{
    return &s_app_font;
}
