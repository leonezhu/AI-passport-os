// System settings: shared terminal rows and amber focus.
#include "launcher/settings.h"

#include "launcher/app_registry.h"
#include "bsp_audio.h"
#include "bsp_display.h"
#include "ui_pixel.h"
#include "app_fonts.h"
#include "esp_log.h"
#include "lvgl.h"
#include "nvs.h"

#include <algorithm>
#include <cstdio>

namespace {

constexpr char kTag[] = "settings";
constexpr char kNvsNamespace[] = "system_prefs";

constexpr uint8_t kVolumes[] = {0, 20, 40, 60, 80, 100};
constexpr uint8_t kBrightness[] = {20, 40, 60, 80, 100};
constexpr uint16_t kTimeouts[] = {0, 15, 30, 60, 120, 300};
constexpr size_t kRowCount = 4;

uint8_t s_volume = 80;
uint8_t s_brightness = 80;
uint16_t s_timeout = 60;
size_t s_selected = 0;
lv_obj_t *s_screen;
lv_obj_t *s_rows[kRowCount];
lv_obj_t *s_values[kRowCount];

template <typename T, size_t N>
T validated(const T (&values)[N], T value, T fallback) {
    for (T candidate : values) {
        if (candidate == value) return value;
    }
    return fallback;
}

template <typename T, size_t N>
T next_value(const T (&values)[N], T current) {
    for (size_t i = 0; i < N; ++i) {
        if (values[i] == current) return values[(i + 1) % N];
    }
    return values[0];
}

void save_all() {
    nvs_handle_t handle;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) return;
    esp_err_t result = nvs_set_u8(handle, "volume", s_volume);
    if (result == ESP_OK) result = nvs_set_u8(handle, "brightness", s_brightness);
    if (result == ESP_OK) result = nvs_set_u16(handle, "timeout", s_timeout);
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    if (result != ESP_OK) ESP_LOGW(kTag, "Failed to save settings: %s", esp_err_to_name(result));
}

void apply_settings() {
    bsp_audio_set_master_volume(s_volume);
    bsp_display_set_master_brightness(s_brightness);
    app_registry_set_screen_timeout(s_timeout);
}

void refresh_ui() {
    if (!s_screen) return;
    for (size_t i = 0; i < kRowCount; ++i) {
        const bool selected = i == s_selected;
        ui_pixel_set_selected(s_rows[i], selected, true);
        // 选中行染像素黄,与整机"选中=黄"语义一致
        lv_obj_set_style_bg_color(s_rows[i],
                                  lv_color_hex(selected ? UI_SURFACE_ALT : UI_SURFACE), 0);
        lv_obj_set_style_text_color(s_values[i],
                                    lv_color_hex(selected ? UI_AMBER : UI_TEXT), 0);
    }

    char value[20];
    std::snprintf(value, sizeof(value), "%u%%", static_cast<unsigned>(s_volume));
    lv_label_set_text(s_values[0], value);
    std::snprintf(value, sizeof(value), "%u%%", static_cast<unsigned>(s_brightness));
    lv_label_set_text(s_values[1], value);
    if (s_timeout == 0) {
        lv_label_set_text(s_values[2], "关闭");
    } else {
        std::snprintf(value, sizeof(value), "%u 秒", static_cast<unsigned>(s_timeout));
        lv_label_set_text(s_values[2], value);
    }
    lv_label_set_text(s_values[3], "确定");
}

void create_ui() {
    s_screen = lv_obj_create(nullptr);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    ui_pixel_background(s_screen);

    ui_pixel_title_plate(s_screen, 30, "设置", &ui_font_20);

    static const char *labels[kRowCount] = {
        "音量", "屏幕亮度", "熄屏", "重新设置"
    };
    for (size_t i = 0; i < kRowCount; ++i) {
        const int y = 84 + static_cast<int>(i) * 46;
        s_rows[i] = lv_obj_create(s_screen);
        lv_obj_set_size(s_rows[i], 200, 38);
        lv_obj_set_pos(s_rows[i], 20, y);
        lv_obj_set_style_radius(s_rows[i], 0, 0);
        lv_obj_set_style_bg_color(s_rows[i], lv_color_hex(UI_PAPER), 0);
        lv_obj_set_style_bg_opa(s_rows[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_rows[i], 1, 0);
        lv_obj_set_style_border_color(s_rows[i], lv_color_hex(UI_BORDER), 0);
        lv_obj_set_style_pad_hor(s_rows[i], 12, 0);
        lv_obj_set_style_pad_ver(s_rows[i], 0, 0);
        lv_obj_clear_flag(s_rows[i], LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *name = ui_pixel_label(s_rows[i], labels[i], &buddy_font_16, UI_TEXT);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);

        s_values[i] = ui_pixel_label(s_rows[i], "", &buddy_font_16, UI_SKY_DARK);
        lv_obj_align(s_values[i], LV_ALIGN_RIGHT_MID, 0, 0);
    }

    ui_pixel_hint_pill(s_screen, "上下选择  确定设置", &buddy_font_16);

    refresh_ui();
}

void settings_init() {
    // 启动配置已由 settings_bootstrap 恢复；这里不创建 UI，保持惰性加载。
}

void settings_start() {
    bsp_lvgl_lock(-1);
    if (!s_screen) create_ui();
    refresh_ui();
    lv_scr_load(s_screen);
    bsp_lvgl_unlock();
}

void settings_stop() {
    bsp_lvgl_lock(-1);
    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = nullptr;
        for (size_t i = 0; i < kRowCount; ++i) {
            s_rows[i] = nullptr;
            s_values[i] = nullptr;
        }
    }
    bsp_lvgl_unlock();
}

void settings_on_key(uint8_t btn, app_btn_event_t event, uint16_t) {
    if (event != BTN_EVT_CLICK) return;
    if (btn == BSP_BTN_UP) {
        s_selected = (s_selected + kRowCount - 1) % kRowCount;
    } else if (btn == BSP_BTN_DOWN) {
        s_selected = (s_selected + 1) % kRowCount;
    } else if (btn == BSP_BTN_OK) {
        switch (s_selected) {
            case 0: s_volume = next_value(kVolumes, s_volume); break;
            case 1: s_brightness = next_value(kBrightness, s_brightness); break;
            case 2: s_timeout = next_value(kTimeouts, s_timeout); break;
            case 3:
                s_volume = 80;
                s_brightness = 80;
                s_timeout = 60;
                break;
        }
        apply_settings();
        save_all();
    } else {
        return;
    }

    bsp_lvgl_lock(-1);
    refresh_ui();
    bsp_lvgl_unlock();
}

}  // namespace

void settings_bootstrap(void) {
    nvs_handle_t handle;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u8(handle, "volume", &s_volume);
        nvs_get_u8(handle, "brightness", &s_brightness);
        nvs_get_u16(handle, "timeout", &s_timeout);
        nvs_close(handle);
    }
    s_volume = validated(kVolumes, s_volume, static_cast<uint8_t>(80));
    s_brightness = validated(kBrightness, s_brightness, static_cast<uint8_t>(80));
    s_timeout = validated(kTimeouts, s_timeout, static_cast<uint16_t>(60));
    apply_settings();
    ESP_LOGI(kTag, "Loaded volume=%u brightness=%u timeout=%u",
             static_cast<unsigned>(s_volume), static_cast<unsigned>(s_brightness),
             static_cast<unsigned>(s_timeout));
}

const passport_app_t g_settings_app = {
    .id = APP_ID_SETTINGS,
    .name = "设置",
    .en_name = "SETTINGS",
    .desc = "System controls",
    .tag = "DEVICE",
    .theme_color = UI_INK,
    .init = settings_init,
    .start = settings_start,
    .stop = settings_stop,
    .on_key = settings_on_key,
};
