#include "launcher/app_registry.h"
#include "launcher/launcher.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <vector>

static const char *TAG = "app_registry";

// 外部 App 实例前向声明
extern const passport_app_t g_radio_app;
extern const passport_app_t g_voice_app;
extern const passport_app_t g_codex_app;

namespace {

const passport_app_t *s_apps[] = {
    &g_radio_app,
    &g_voice_app,
    &g_codex_app,
};

constexpr size_t kAppCount = sizeof(s_apps) / sizeof(s_apps[0]);
passport_app_id_t s_active_id = APP_ID_LAUNCHER;

// 息屏与背光管理
bool s_screen_on = true;
int64_t s_last_activity_us = 0;
constexpr int64_t kScreenTimeoutUs = 60 * 1000 * 1000LL; // 60秒无操作熄屏

// 长按 OK 强制返回计时 (1.5s)
int64_t s_ok_press_start_us = 0;
bool s_ok_long_triggered = false;
constexpr int64_t kLongOkReturnThresholdUs = 1500 * 1000LL; // 1.5 秒

} // namespace

void app_registry_init(void) {
    ESP_LOGI(TAG, "Initializing app registry with %u apps...", (unsigned)kAppCount);
    for (size_t i = 0; i < kAppCount; ++i) {
        if (s_apps[i] && s_apps[i]->init) {
            s_apps[i]->init();
        }
    }
    s_last_activity_us = esp_timer_get_time();
    s_active_id = APP_ID_LAUNCHER;
    launcher_ui_show();
}

size_t app_registry_get_count(void) {
    return kAppCount;
}

const passport_app_t *app_registry_get_by_index(size_t index) {
    if (index >= kAppCount) return nullptr;
    return s_apps[index];
}

const passport_app_t *app_registry_get_by_id(passport_app_id_t id) {
    for (size_t i = 0; i < kAppCount; ++i) {
        if (s_apps[i] && s_apps[i]->id == id) {
            return s_apps[i];
        }
    }
    return nullptr;
}

passport_app_id_t app_registry_get_active(void) {
    return s_active_id;
}

void app_registry_switch_to(passport_app_id_t id) {
    if (s_active_id == id) return;

    ESP_LOGI(TAG, "Switching app: %d -> %d", s_active_id, id);

    // 1. 退出前一个 App 并清理资源
    if (s_active_id != APP_ID_LAUNCHER) {
        const passport_app_t *prev = app_registry_get_by_id(s_active_id);
        if (prev && prev->stop) {
            ESP_LOGI(TAG, "Stopping app: %s", prev->name);
            prev->stop();
        }
    } else {
        launcher_ui_hide();
    }

    // 2. 打印当前可用空闲内存，确认清理效果
    uint32_t free_heap = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Free heap after app stop: %u KB", (unsigned)(free_heap / 1024));

    s_active_id = id;

    // 3. 启动新目标
    if (id == APP_ID_LAUNCHER) {
        launcher_ui_show();
    } else {
        const passport_app_t *next = app_registry_get_by_id(id);
        if (next && next->start) {
            ESP_LOGI(TAG, "Starting app: %s", next->name);
            next->start();
        }
    }
}

void app_registry_dispatch_key(uint8_t btn, app_btn_event_t event, uint16_t mv) {
    int64_t now = esp_timer_get_time();

    // 1. 熄屏唤醒护栏：如果屏幕已休眠，第一按只点亮屏幕，不透传给业务
    if (!s_screen_on) {
        if (event == BTN_EVT_PRESS || event == BTN_EVT_CLICK) {
            bsp_display_backlight(100);
            s_screen_on = true;
            s_last_activity_us = now;
            ESP_LOGI(TAG, "Screen woken up by key press");
            return;
        }
    }

    s_last_activity_us = now;

    // 2. 全局拦截：在任何子 App 运行中，长按 OK 键 >= 1.5s 强制返回 Launcher
    if (btn == BSP_BTN_OK) {
        if (event == BTN_EVT_PRESS) {
            s_ok_press_start_us = now;
            s_ok_long_triggered = false;
        } else if (event == BTN_EVT_RELEASE || event == BTN_EVT_LONG_UP) {
            s_ok_press_start_us = 0;
            if (s_ok_long_triggered) {
                // 已经作为全局返回消费了，不再透传给子应用
                s_ok_long_triggered = false;
                return;
            }
        }
    }

    // 检测持续按压是否超阈值
    if (btn == BSP_BTN_OK && s_ok_press_start_us > 0 && !s_ok_long_triggered) {
        if (now - s_ok_press_start_us >= kLongOkReturnThresholdUs) {
            if (s_active_id != APP_ID_LAUNCHER) {
                s_ok_long_triggered = true;
                ESP_LOGI(TAG, "Global Long-OK detected! Returning to Launcher.");
                app_registry_switch_to(APP_ID_LAUNCHER);
                return;
            }
        }
    }

    // 3. 事件分发
    if (s_active_id == APP_ID_LAUNCHER) {
        launcher_ui_on_key(btn, event);
    } else {
        const passport_app_t *app = app_registry_get_by_id(s_active_id);
        if (app && app->on_key) {
            app->on_key(btn, event, mv);
        }
    }
}
