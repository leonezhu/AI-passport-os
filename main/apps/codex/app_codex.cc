#include "apps/codex/app_codex.h"
#include "apps/codex/buddy_ui.h"
#include "apps/codex/buddy_character.h"
#include "apps/codex/buddy_ble.h"
#include "apps/codex/buddy_settings.h"
#include "apps/codex/buddy_sound_player.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include <atomic>

static const char *TAG = "app_codex";

namespace {

std::atomic<bool> s_codex_active{false};
TaskHandle_t s_codex_task_handle = nullptr;
buddy_settings_t s_settings;
lv_obj_t *s_codex_screen = nullptr;
lv_obj_t *s_status_label = nullptr;
lv_obj_t *s_mascot_canvas = nullptr;

// 绘制 BSOD 像素小精灵画布 (64x64)
constexpr uint16_t kCanvasW = 128;
constexpr uint16_t kCanvasH = 128;
lv_color_t s_canvas_buf[kCanvasW * kCanvasH];

void draw_bsod_pixel_gremlin(int frame_tick) {
    if (!s_mascot_canvas) return;

    // 清屏填充底色
    lv_canvas_fill_bg(s_mascot_canvas, lv_color_hex(0x0047AB), LV_OPA_COVER);

    // 绘制经典的 Windows 蓝屏小精灵表情
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_hex(0xFFFFFF);
    rect_dsc.bg_opa = LV_OPA_COVER;

    // 眼睛 (带有跳动的动态动画)
    int eye_y = 40 + (frame_tick % 2) * 2;
    lv_canvas_draw_rect(s_mascot_canvas, 36, eye_y, 14, 18, &rect_dsc);
    lv_canvas_draw_rect(s_mascot_canvas, 78, eye_y, 14, 18, &rect_dsc);

    // 嘴巴 (像素微笑或微张)
    lv_canvas_draw_rect(s_mascot_canvas, 48, 82, 32, 8, &rect_dsc);
    lv_canvas_draw_rect(s_mascot_canvas, 44, 76, 8, 8, &rect_dsc);
    lv_canvas_draw_rect(s_mascot_canvas, 76, 76, 8, 8, &rect_dsc);
}

void codex_render_task(void *arg) {
    ESP_LOGI(TAG, "Codex rendering task started");
    int tick = 0;

    while (s_codex_active.load()) {
        bsp_lvgl_lock(-1);
        draw_bsod_pixel_gremlin(tick++);
        bsp_lvgl_unlock();

        vTaskDelay(pdMS_TO_TICKS(150));
    }

    ESP_LOGI(TAG, "Codex rendering task exiting");
    s_codex_task_handle = nullptr;
    vTaskDelete(NULL);
}

} // namespace

void codex_app_init(void) {
    ESP_LOGI(TAG, "Initializing Codex App...");
    buddy_settings_init();
    buddy_settings_load(&s_settings);
    buddy_sound_player_init();
}

void codex_app_start(void) {
    ESP_LOGI(TAG, "Starting Codex App (BSOD gremlin & BLE)...");
    s_codex_active.store(true);

    // 1. 构建专属 UI 界面
    bsp_lvgl_lock(-1);
    s_codex_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_codex_screen, lv_color_hex(0x003366), 0);
    lv_obj_clear_flag(s_codex_screen, LV_OBJ_FLAG_SCROLLABLE);

    // 标题
    lv_obj_t *title = lv_label_create(s_codex_screen);
    lv_label_set_text(title, "CODEX BUDDY");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    // BSOD 像素画布
    s_mascot_canvas = lv_canvas_create(s_codex_screen);
    lv_canvas_set_buffer(s_mascot_canvas, s_canvas_buf, kCanvasW, kCanvasH, LV_COLOR_FORMAT_RGB565);
    lv_obj_align(s_mascot_canvas, LV_ALIGN_CENTER, 0, -10);

    // 状态提示
    s_status_label = lv_label_create(s_codex_screen);
    lv_label_set_text(s_status_label, "等待电脑 Codex 任务...");
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x80D0FF), 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_status_label, LV_ALIGN_BOTTOM_MID, 0, -32);

    lv_scr_load(s_codex_screen);
    bsp_lvgl_unlock();

    // 2. 启动 BLE 服务
    buddy_ble_ensure_initialized();
    buddy_ble_start();

    // 3. 启动小精灵动画渲染线程
    if (s_codex_task_handle == nullptr) {
        xTaskCreate(codex_render_task, "codex_anim", 3072, NULL, 5, &s_codex_task_handle);
    }
}

void codex_app_stop(void) {
    ESP_LOGI(TAG, "Stopping Codex App and releasing BLE/UI memory...");
    s_codex_active.store(false);

    // 1. 停止 BLE 广播与连接
    buddy_ble_stop();

    // 2. 等待动画线程退出
    while (s_codex_task_handle != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // 3. 清理 screen 资源
    bsp_lvgl_lock(-1);
    if (s_codex_screen) {
        lv_obj_del(s_codex_screen);
        s_codex_screen = nullptr;
        s_mascot_canvas = nullptr;
        s_status_label = nullptr;
    }
    bsp_lvgl_unlock();

    ESP_LOGI(TAG, "Codex App cleanly stopped. Free heap: %u KB",
             (unsigned)(esp_get_free_heap_size() / 1024));
}

void codex_app_on_key(uint8_t btn, app_btn_event_t event, uint16_t mv) {
    (void)mv;
    if (event != BTN_EVT_CLICK) return;

    if (btn == BSP_BTN_OK) {
        ESP_LOGI(TAG, "Codex OK key pressed - approval / confirm");
        buddy_sound_play(BUDDY_SOUND_APPROVE);
    } else if (btn == BSP_BTN_UP) {
        ESP_LOGI(TAG, "Codex UP key pressed - reject");
        buddy_sound_play(BUDDY_SOUND_REJECT);
    }
}

const passport_app_t g_codex_app = {
    .id = APP_ID_CODEX,
    .name = "Codex 陪伴精灵",
    .en_name = "CODEX",
    .desc = "像素蓝屏小精灵 · 实时任务监听 · 审批与庆祝",
    .tag = "BLE / Agent",
    .theme_color = 0x4DA6FF,
    .init = codex_app_init,
    .start = codex_app_start,
    .stop = codex_app_stop,
    .on_key = codex_app_on_key,
};
