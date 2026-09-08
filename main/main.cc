#include "launcher/launcher.h"
#include "launcher/app_registry.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

static void on_bsp_key_callback(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    int mv = bsp_button_read_mv();
    app_btn_event_t app_ev;
    switch (ev) {
    case BSP_BTN_PRESS:   app_ev = BTN_EVT_PRESS; break;
    case BSP_BTN_RELEASE: app_ev = BTN_EVT_RELEASE; break;
    case BSP_BTN_CLICK:   app_ev = BTN_EVT_CLICK; break;
    case BSP_BTN_LONG:    app_ev = BTN_EVT_LONG; break;
    case BSP_BTN_LONG_UP: app_ev = BTN_EVT_LONG_UP; break;
    default: return;
    }

    app_registry_dispatch_key(static_cast<uint8_t>(btn), app_ev,
                              static_cast<uint16_t>(mv > 0 ? mv : 0));
}

static void system_monitor_task(void *arg) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (app_registry_get_active() == APP_ID_LAUNCHER) {
            bsp_lvgl_lock(-1);
            launcher_ui_update_status();
            bsp_lvgl_unlock();
        }
    }
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== FoloToy AI Passport OS Starting ===");

    // 1. NVS 初始化
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. 硬件总线与外设初始化
    ESP_ERROR_CHECK(bsp_i2c_init());
    ESP_ERROR_CHECK(bsp_display_init());
    if (bsp_lvgl_init() == NULL) {
        ESP_LOGE(TAG, "LVGL initialization failed");
        return;
    }
    bsp_display_backlight(100);

    bsp_battery_init();
    bsp_button_init(on_bsp_key_callback, NULL);

    ESP_LOGI(TAG, "Hardware BSP initialized successfully");

    // 3. 初始化 Launcher 与应用注册表
    launcher_ui_init();
    app_registry_init();

    // 4. 启动系统监控心跳任务 (状态栏与内存遥测)
    xTaskCreate(system_monitor_task, "sys_mon", 2048, NULL, 1, NULL);

    ESP_LOGI(TAG, "Passport OS Boot Complete. Initial Free Heap: %u KB",
             (unsigned)(esp_get_free_heap_size() / 1024));
}
