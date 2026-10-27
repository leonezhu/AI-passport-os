#include "apps/app_base.h"
#include "panda_core.h"
#include "launcher/app_registry.h"
#include "bsp_display.h"
#include "app_fonts.h"
#include "ui_pixel.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <esp_random.h>
#include <cstdio>

namespace {
panda_game_t s_game{};
lv_obj_t *s_screen;
lv_timer_t *s_timer;
lv_obj_t *s_status;      // 顶栏下的状态行
lv_obj_t *s_hint;        // 底部提示（复用 footer）
lv_obj_t *s_canvas;      // 像素游戏画布（draw 事件渲染）
uint32_t s_last_us;
uint32_t s_invincible_blink;  // 无敌闪烁计数

// 像素配色（竹绿+熊猫黑白+障碍暖色，与 OS 调色板同族）
constexpr uint32_t C_BAMBOO = 0x7FA65A;   // 竹竿
constexpr uint32_t C_BAMBOO_D = 0x5C7F40; // 竹节
constexpr uint32_t C_PANDA_W = 0xF2EFE6;  // 熊猫白
constexpr uint32_t C_PANDA_B = 0x2B2B2B;  // 熊猫黑
constexpr uint32_t C_SNAKE = 0x9BBF3C;
constexpr uint32_t C_BUG = 0xB4553A;
constexpr uint32_t C_SHOOT = 0xC9A96A;    // 竹笋
constexpr uint32_t C_STAR = 0xE8C558;     // 星星

// 像素渲染：在 draw 事件里用绝对坐标画色块（LVGL9 draw layer）
void draw_game(lv_event_t *e) {
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = LV_OPA_COVER;
    auto box = [&](int x, int y, int w, int h, uint32_t color) {
        dsc.bg_color = lv_color_hex(color);
        lv_area_t a{x, y, x + w - 1, y + h - 1};
        lv_draw_rect(layer, &dsc, &a);
    };

    const int panda_screen_y = 160;
    // 三根竹竿（杆宽 8，竹节每 24px 一节）
    for (int l = 0; l < PANDA_LANES; ++l) {
        int bx = 40 + l * 80 - 4;
        box(bx, 30, 8, 240 - 30 - 22, C_BAMBOO);
        int phase = (int)((s_game.scroll_y + (240 - 30)) % 24);
        for (int y = 30 - phase; y < 240 - 22; y += 24) {
            if (y + 3 > 30) box(bx, y, 8, 3, C_BAMBOO_D);
        }
    }

    // 实体：屏幕 y = 240 - (世界y - scroll_y) …世界越大越靠上
    for (uint16_t i = 0; i < s_game.entity_count; ++i) {
        const panda_entity_t *en = &s_game.entities[i];
        int sy = 240 - 22 - (en->y - (int)s_game.scroll_y) - 8;
        if (sy < 26 || sy > 240 - 24) continue;
        int ex = 40 + en->lane * 80 - 8;
        switch (en->kind) {
            case PANDA_ENTITY_SNAKE:  // 蛇：三节绿波
                box(ex, sy + 4, 16, 4, C_SNAKE);
                box(ex + 10, sy + 1, 6, 6, C_SNAKE);
                box(ex + 13, sy + 3, 1, 1, C_PANDA_B);
                break;
            case PANDA_ENTITY_BUG:    // 甲虫：暗红壳+触角
                box(ex + 4, sy + 3, 8, 7, C_BUG);
                box(ex + 6, sy + 1, 4, 2, C_PANDA_B);
                box(ex + 3, sy + 5, 1, 3, C_PANDA_B);
                box(ex + 12, sy + 5, 1, 3, C_PANDA_B);
                break;
            case PANDA_ENTITY_SHOOT:  // 竹笋：三角塔
                box(ex + 6, sy + 1, 4, 2, C_SHOOT);
                box(ex + 4, sy + 3, 8, 3, C_SHOOT);
                box(ex + 2, sy + 6, 12, 4, C_SHOOT);
                break;
            case PANDA_ENTITY_STAR:   // 星星：十字闪烁
                box(ex + 6, sy, 4, 12, C_STAR);
                box(ex, sy + 4, 16, 4, C_STAR);
                box(ex + 5, sy + 5, 6, 2, C_PANDA_W);
                break;
            default: break;
        }
    }

    // 熊猫（黑白像素块：耳/头/眼斑/身），切杆动画横向插值
    int from_x = 40 + s_game.from_lane * 80;
    int to_x = 40 + s_game.lane * 80;
    int px = to_x;
    if (s_game.switch_anim) {
        int t = PANDA_SWITCH_FRAMES - s_game.switch_anim;
        px = from_x + (to_x - from_x) * t / PANDA_SWITCH_FRAMES;
    }
    int py = panda_screen_y;
    // 身体
    box(px - 9, py - 2, 18, 14, C_PANDA_W);
    box(px - 9, py + 8, 18, 6, C_PANDA_B);        // 黑色四肢带
    // 头
    box(px - 7, py - 14, 14, 12, C_PANDA_W);
    box(px - 7, py - 16, 5, 3, C_PANDA_B);        // 左耳
    box(px + 2, py - 16, 5, 3, C_PANDA_B);        // 右耳
    box(px - 5, py - 11, 4, 5, C_PANDA_B);        // 左眼斑
    box(px + 1, py - 11, 4, 5, C_PANDA_B);        // 右眼斑
    box(px - 4, py - 10, 2, 2, C_PANDA_W);        // 眼白点
    box(px + 2, py - 10, 2, 2, C_PANDA_W);
    box(px - 1, py - 7, 2, 2, C_PANDA_B);         // 鼻
    // 抱杆手
    box(px - 11, py + 2, 3, 6, C_PANDA_B);
    box(px + 8, py + 2, 3, 6, C_PANDA_B);
    // 无敌闪烁：熊猫外圈星尘
    if (s_game.invincible_ms && (++s_invincible_blink & 2)) {
        box(px - 12, py - 18, 2, 2, C_STAR);
        box(px + 10, py - 18, 2, 2, C_STAR);
        box(px - 12, py + 12, 2, 2, C_STAR);
        box(px + 10, py + 12, 2, 2, C_STAR);
    }
}

void refresh_texts() {
    char buf[48];
    if (s_game.state == PANDA_STATE_IDLE) {
        lv_label_set_text(s_status, "熊猫爬竹竿");
        lv_obj_set_style_text_color(s_status, lv_color_hex(UI_ACCENT), 0);
        std::snprintf(buf, sizeof(buf), "上=左移  下=右移  确定=开始");
        lv_label_set_text(s_hint, buf);
    } else if (s_game.state == PANDA_STATE_OVER) {
        std::snprintf(buf, sizeof(buf), "爬到 %lu 米  最佳 %lu 米",
                      (unsigned long)s_game.height_m, (unsigned long)s_game.best_m);
        lv_label_set_text(s_status, buf);
        lv_obj_set_style_text_color(s_status, lv_color_hex(UI_SECONDARY), 0);
        lv_label_set_text(s_hint, "确定再来一局 · 长按返回");
    } else {
        std::snprintf(buf, sizeof(buf), "高度 %lu 米%s",
                      (unsigned long)s_game.height_m,
                      s_game.invincible_ms ? "  ★无敌" : "");
        lv_label_set_text(s_status, buf);
        lv_obj_set_style_text_color(s_status, lv_color_hex(UI_TEXT), 0);
        lv_label_set_text(s_hint, "上=左杆 下=右杆 · 长按确定返回");
    }
}

void tick(lv_timer_t *timer) {
    if (!s_screen) return;
    const bool visible = app_registry_screen_is_on();
    if (visible && s_game.state == PANDA_STATE_RUNNING) {
        uint32_t now = esp_timer_get_time();
        uint32_t dt = s_last_us ? (uint32_t)((now - s_last_us) / 1000) : 16;
        s_last_us = now;
        panda_tick(&s_game, dt);
        app_registry_keep_awake();   // 游戏中不许息屏
        if (s_game.state == PANDA_STATE_OVER) refresh_texts();
    } else {
        s_last_us = 0;
    }
    lv_timer_set_period(timer, s_game.state == PANDA_STATE_RUNNING ? 16 : 200);
    if (s_canvas) lv_obj_invalidate(s_canvas);
}

void init() { panda_init(&s_game); esp_fill_random(&s_game.rng, sizeof(s_game.rng));
            if (!s_game.rng) s_game.rng = 0x1234abcd; }

void start() {
    if (!bsp_lvgl_lock(1000)) return;
    if (s_screen) { bsp_lvgl_unlock(); return; }
    s_last_us = 0;
    s_invincible_blink = 0;
    s_screen = lv_obj_create(nullptr);
    ui_pixel_background(s_screen);
    lv_obj_t *top = ui_pixel_top_bar(s_screen);
    lv_obj_t *name = ui_pixel_label(top, "熊猫爬竹竿", &buddy_font_16, UI_TEXT);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 42, 0);
    s_status = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_status, &buddy_font_16, 0);
    lv_obj_set_pos(s_status, 12, 32);
    lv_obj_set_size(s_status, 216, 22);
    s_canvas = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_canvas);
    lv_obj_set_pos(s_canvas, 0, 26);
    lv_obj_set_size(s_canvas, 240, 240 - 26);
    lv_obj_add_event_cb(s_canvas, draw_game, LV_EVENT_DRAW_MAIN, nullptr);
    s_hint = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_hint, &buddy_font_16, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(UI_TEXT_DIM), 0);
    lv_obj_set_pos(s_hint, 8, 216);
    lv_obj_set_size(s_hint, 224, 22);
    refresh_texts();
    lv_screen_load(s_screen);
    s_timer = lv_timer_create(tick, 16, nullptr);
    ESP_LOGI("panda", "started");
    bsp_lvgl_unlock();
}

void stop() {
    if (!bsp_lvgl_lock(-1)) return;
    if (s_timer) { lv_timer_delete(s_timer); s_timer = nullptr; }
    if (s_screen) { lv_obj_delete(s_screen); s_screen = nullptr; }
    s_canvas = nullptr;
    bsp_lvgl_unlock();
}

void on_key(uint8_t btn, app_btn_event_t event, uint16_t) {
    if (event != BTN_EVT_CLICK || !bsp_lvgl_lock(500)) return;
    if (!s_screen) { bsp_lvgl_unlock(); return; }
    if (btn == BSP_BTN_OK) {
        if (s_game.state == PANDA_STATE_RUNNING) {
            // 游戏中单击 OK：暂停暂不做状态机，简化为不响应（避免误触）
        } else {
            esp_fill_random(&s_game.rng, sizeof(s_game.rng));
            if (!s_game.rng) s_game.rng = 0x1234abcd;
            panda_start(&s_game);
            s_last_us = 0;
        }
    } else if (btn == BSP_BTN_UP) {
        panda_move(&s_game, -1);
    } else if (btn == BSP_BTN_DOWN) {
        panda_move(&s_game, +1);
    }
    refresh_texts();
    bsp_lvgl_unlock();
}
} // namespace

extern const passport_app_t g_panda_app = {
    .id = APP_ID_PANDA,
    .name = "熊猫爬竹竿",
    .en_name = "PANDA CLIMB",
    .desc = "诺基亚经典复刻\n三杆切换躲避虫蛇",
    .tag = "GAME",
    .theme_color = 0x7FA65A,
    .init = init,
    .start = start,
    .stop = stop,
    .on_key = on_key,
};
