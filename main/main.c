/*
 * Smart lastvakt for Waveshare ESP32-S3-Touch-LCD-4.3B-BOX
 *
 * This starts from Waveshare's official LVGL v8 hardware example, but leaves
 * flashing as an explicit, separate step.
 */

#include <assert.h>

#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "waveshare_rgb_lcd_port.h"

#include "app_time.h"
#include "app_config.h"
#include "calendar_source.h"
#include "network_setup.h"
#include "phase_source.h"
#include "ui.h"

static const char *TAG = "lastvakt";

void app_main(void)
{
    const esp_lv_adapter_rotation_t rotation = ESP_LV_ADAPTER_ROTATE_0;
    const esp_lv_adapter_tear_avoid_mode_t tear_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_RGB;
    const uint8_t frame_buffer_count = esp_lv_adapter_get_required_frame_buffer_count(tear_mode, rotation);

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_touch_handle_t touch_handle = NULL;

    ESP_ERROR_CHECK(waveshare_esp32_s3_rgb_lcd_init(
        frame_buffer_count,
        &panel_handle,
        &touch_handle));
    ESP_ERROR_CHECK(waveshare_rgb_lcd_backlight_on());

    ESP_ERROR_CHECK(app_config_init());
    app_time_init();
    ESP_ERROR_CHECK(network_setup_start());
    phase_source_init();
    calendar_source_init();

    esp_lv_adapter_config_t adapter_config = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_config.task_stack_size = 12 * 1024;
    adapter_config.stack_in_psram = true;
    ESP_ERROR_CHECK(esp_lv_adapter_init(&adapter_config));

    esp_lv_adapter_display_config_t disp_config = ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
        panel_handle,
        NULL,
        EXAMPLE_LCD_H_RES,
        EXAMPLE_LCD_V_RES,
        rotation);
    /* Keep tear-free rendering while leaving internal DMA headroom for concurrent TLS. */
    disp_config.profile.buffer_height = 30;
    disp_config.profile.use_psram = true;

    lv_display_t *display = esp_lv_adapter_register_display(&disp_config);
    assert(display != NULL);

    if (touch_handle != NULL) {
        esp_lv_adapter_touch_config_t touch_config = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(display, touch_handle);
        lv_indev_t *touch = esp_lv_adapter_register_touch(&touch_config);
        assert(touch != NULL);
    }

    ESP_ERROR_CHECK(esp_lv_adapter_start());

    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        ui_create();
        esp_lv_adapter_unlock();
    }

    app_time_start_network_sync();
    ESP_LOGI(TAG, "Clock, calendar, setup page and Tibber phase view started");
}
