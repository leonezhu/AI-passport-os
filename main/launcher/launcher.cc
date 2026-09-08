#include "launcher/launcher.h"
#include "launcher/app_registry.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "lvgl.h"
#include <cstdio>

static const char *TAG = "launcher";

namespace {

constexpr uint32_t kBgColor      = 0x0A0F14; // 深夜墨黑
constexpr uint32_t kCardBg       = 0x121B22; // 卡片深灰蓝
constexpr uint32_t kBorderColor  = 0x22323D; // 卡片描边
constexpr uint32_t kTextWhite    = 0xF0F4F8; // 主标题白
constexpr uint32_t kTextMuted    = 0x7E92A2; // 副标题灰
constexpr uint32_t kAmberAccent  = 0xFFB74D; // 强调金黄

lv_obj_t *s_screen = nullptr;
lv_obj_t *s_card_container = nullptr;
lv_obj_t *s_app_badge = nullptr;
lv_obj_t *s_app_title = nullptr;
lv_obj_t *s_app_desc = nullptr;
lv_obj_t *s_page_dots = nullptr;
lv_obj_t *s_battery_label = nullptr;
lv_obj_t *s_memory_label = nullptr;

size_t s_selected_index = 0;

void update_card_content() {
    const passport_app_t *app = app_registry_get_by_index(s_selected_index);
    if (!app) return;

    // 更新卡片主题色描边与文本
    lv_obj_set_style_border_color(s_card_container, lv_color_hex(app->theme_color), 0);
    lv_obj_set_style_bg_color(s_app_badge, lv_color_hex(app->theme_color), 0);
    lv_label_set_text(s_app_badge, app->tag);
    lv_label_set_text(s_app_title, app->name);
    lv_label_set_text(s_app_desc, app->desc);

    // 更新指示点
    char dots[32] = "";
    size_t total = app_registry_get_count();
    for (size_t i = 0; i < total; ++i) {
        if (i == s_selected_index) {
            std::snprintf(dots + std::strlen(dots), sizeof(dots) - std::strlen(dots), "#FFB74D [ ● ]# ");
        } else {
            std::snprintf(dots + std::strlen(dots), sizeof(dots) - std::strlen(dots), "#7E92A2  ○  # ");
        }
    }
    lv_label_set_text(s_page_dots, dots);
}

} // namespace

void launcher_ui_init(void) {
    bsp_lvgl_lock(-1);

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(kBgColor), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    // 1. 顶部状态栏
    lv_obj_t *top_bar = lv_obj_create(s_screen);
    lv_obj_set_size(top_bar, 240, 32);
    lv_obj_set_pos(top_bar, 0, 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_pad_all(top_bar, 6, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *brand_label = lv_label_create(top_bar);
    lv_label_set_text(brand_label, "PASSPORT OS");
    lv_obj_set_style_text_color(brand_label, lv_color_hex(kAmberAccent), 0);
    lv_obj_set_style_text_font(brand_label, &lv_font_montserrat_14, 0);
    lv_obj_align(brand_label, LV_ALIGN_LEFT_MID, 4, 0);

    s_battery_label = lv_label_create(top_bar);
    lv_label_set_text(s_battery_label, "--%");
    lv_obj_set_style_text_color(s_battery_label, lv_color_hex(kTextMuted), 0);
    lv_obj_set_style_text_font(s_battery_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_battery_label, LV_ALIGN_RIGHT_MID, -4, 0);

    // 2. 主卡片容器 (居中精致圆角方框)
    s_card_container = lv_obj_create(s_screen);
    lv_obj_set_size(s_card_container, 216, 210);
    lv_obj_align(s_card_container, LV_ALIGN_TOP_MID, 0, 38);
    lv_obj_set_style_bg_color(s_card_container, lv_color_hex(kCardBg), 0);
    lv_obj_set_style_bg_opa(s_card_container, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_card_container, 14, 0);
    lv_obj_set_style_border_width(s_card_container, 2, 0);
    lv_obj_set_style_border_color(s_card_container, lv_color_hex(kBorderColor), 0);
    lv_obj_set_style_pad_all(s_card_container, 16, 0);
    lv_obj_clear_flag(s_card_container, LV_OBJ_FLAG_SCROLLABLE);

    // 协议特性 Badge (如 "Wi-Fi", "BLE")
    s_app_badge = lv_label_create(s_card_container);
    lv_obj_set_style_radius(s_app_badge, 4, 0);
    lv_obj_set_style_bg_opa(s_app_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s_app_badge, lv_color_hex(kBgColor), 0);
    lv_obj_set_style_text_font(s_app_badge, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_hor(s_app_badge, 6, 0);
    lv_obj_set_style_pad_ver(s_app_badge, 2, 0);
    lv_obj_align(s_app_badge, LV_ALIGN_TOP_LEFT, 0, 4);

    // App 主名称
    s_app_title = lv_label_create(s_card_container);
    lv_obj_set_style_text_color(s_app_title, lv_color_hex(kTextWhite), 0);
    lv_obj_set_style_text_font(s_app_title, &lv_font_montserrat_20, 0);
    lv_obj_align(s_app_title, LV_ALIGN_TOP_LEFT, 0, 36);

    // App 详细描述
    s_app_desc = lv_label_create(s_card_container);
    lv_obj_set_width(s_app_desc, 184);
    lv_label_set_long_mode(s_app_desc, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_app_desc, lv_color_hex(kTextMuted), 0);
    lv_obj_set_style_text_font(s_app_desc, &lv_font_montserrat_14, 0);
    lv_obj_align(s_app_desc, LV_ALIGN_TOP_LEFT, 0, 80);

    // 内存剩余提示
    s_memory_label = lv_label_create(s_card_container);
    lv_obj_set_style_text_color(s_memory_label, lv_color_hex(kTextMuted), 0);
    lv_obj_set_style_text_font(s_memory_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_memory_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    // 3. 页面指示点
    s_page_dots = lv_label_create(s_screen);
    lv_label_set_recolor(s_page_dots, true);
    lv_obj_align(s_page_dots, LV_ALIGN_TOP_MID, 0, 258);

    // 4. 底部按键提示
    lv_obj_t *hint_label = lv_label_create(s_screen);
    lv_label_set_text(hint_label, "UP/DOWN 选择   OK 启动");
    lv_obj_set_style_text_color(hint_label, lv_color_hex(kTextMuted), 0);
    lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_14, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -12);

    update_card_content();
    launcher_ui_update_status();

    bsp_lvgl_unlock();
    ESP_LOGI(TAG, "Launcher UI initialized");
}

void launcher_ui_show(void) {
    bsp_lvgl_lock(-1);
    if (s_screen) {
        update_card_content();
        launcher_ui_update_status();
        lv_scr_load(s_screen);
    }
    bsp_lvgl_unlock();
}

void launcher_ui_hide(void) {
    // 当切换到具体 App 时，各 App 会自行加载其专属 screen
}

void launcher_ui_update_status(void) {
    if (!s_battery_label) return;

    int soc = bsp_battery_get_soc();
    uint32_t mv = bsp_battery_get_voltage();
    char buf[32];
    if (soc >= 0 && soc <= 100) {
        std::snprintf(buf, sizeof(buf), "%d%%", soc);
    } else {
        std::snprintf(buf, sizeof(buf), "%u.%02uV", (unsigned)(mv / 1000), (unsigned)((mv % 1000) / 10));
    }
    lv_label_set_text(s_battery_label, buf);

    // 实时显示系统可用空闲堆内存
    uint32_t free_kb = esp_get_free_heap_size() / 1024;
    char mem_buf[32];
    std::snprintf(mem_buf, sizeof(mem_buf), "FREE RAM: %u KB", (unsigned)free_kb);
    if (s_memory_label) {
        lv_label_set_text(s_memory_label, mem_buf);
    }
}

void launcher_ui_on_key(uint8_t btn, app_btn_event_t event) {
    if (event != BTN_EVT_CLICK) return;

    size_t count = app_registry_get_count();
    if (count == 0) return;

    if (btn == BSP_BTN_UP) {
        s_selected_index = (s_selected_index + count - 1) % count;
        bsp_lvgl_lock(-1);
        update_card_content();
        launcher_ui_update_status();
        bsp_lvgl_unlock();
    } else if (btn == BSP_BTN_DOWN) {
        s_selected_index = (s_selected_index + 1) % count;
        bsp_lvgl_lock(-1);
        update_card_content();
        launcher_ui_update_status();
        bsp_lvgl_unlock();
    } else if (btn == BSP_BTN_OK) {
        const passport_app_t *app = app_registry_get_by_index(s_selected_index);
        if (app) {
            ESP_LOGI(TAG, "User launched: %s (ID: %d)", app->name, app->id);
            app_registry_switch_to(app->id);
        }
    }
}
