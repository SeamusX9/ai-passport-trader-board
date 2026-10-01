#include "app_ui.h"

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "main";

#define INPUT_QUEUE_DEPTH 8

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} input_event_t;

static QueueHandle_t s_input_queue;
static volatile bool s_input_ready;

static void input_task(void *arg)
{
    (void)arg;
    input_event_t input;
    for (;;) {
        if (xQueueReceive(s_input_queue, &input, portMAX_DELAY) == pdTRUE) {
            app_ui_on_key(input.btn, input.event);
        }
    }
}

static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    if (!s_input_ready || !s_input_queue) {
        return;
    }
    const input_event_t input = { .btn = btn, .event = ev };
    (void)xQueueSend(s_input_queue, &input, 0);
}

void app_main(void)
{
    ESP_LOGI(TAG, "AI Passport trader board starting");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(bsp_i2c_init());
    ESP_ERROR_CHECK(bsp_display_init());
    if (!bsp_lvgl_init()) {
        ESP_LOGE(TAG, "LVGL init failed");
        return;
    }
    bsp_display_backlight(80);

    /* Defer audio — SoftAP/Wi-Fi need contiguous heap (see softap budget note). */
    (void)bsp_battery_init();

    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (!s_input_queue) {
        ESP_LOGE(TAG, "input queue alloc failed");
        return;
    }
    if (xTaskCreate(input_task, "input", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "input task failed");
        return;
    }
    ESP_ERROR_CHECK(bsp_button_init(on_key, NULL));
    s_input_ready = true;

    err = app_ui_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UI start failed: %s", esp_err_to_name(err));
    }
}
