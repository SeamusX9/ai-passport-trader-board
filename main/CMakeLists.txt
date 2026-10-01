idf_component_register(
    SRCS "main.c"
         "app_ui.c"
         "app_config.c"
         "app_wifi.c"
         "app_traders.c"
         "app_traders_logic.c"
         "app_fonts.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_event esp_http_client esp_http_server esp_hw_support
             esp_netif esp_timer esp_wifi nvs_flash
)

target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/app_font_16.c"
)
