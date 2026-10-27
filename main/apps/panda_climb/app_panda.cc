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
#include <cstring>

namespace {
panda_game_t s_game{};
lv_obj_t *s_screen;
lv_timer_t *s_timer;
lv_obj_t *s_canvas;      // 全屏像素游戏画布（draw 事件渲染，文字直接画进去）
uint32_t s_last_us;
uint32_t s_blink;        // 通用闪烁计数
// 与 panda_core.c 保持一致的世界↔屏幕换算（渲染用）
constexpr int kRenderTop = 216;
constexpr int kPandaScreenY = 158;

// 像素配色（竹绿+熊猫黑白+障碍暖色，与 OS 调色板同族）
constexpr uint32_t C_BG = 0xEFEAD8;       // 画布底（米色，全屏）
constexpr uint32_t C_BAMBOO = 0x7FA65A;
constexpr uint32_t C_BAMBOO_D = 0x5C7F40;
constexpr uint32_t C_PANDA_W = 0xF2EFE6;
constexpr uint32_t C_PANDA_B = 0x2B2B2B;
constexpr uint32_t C_SNAKE = 0x7A9B2E;
constexpr uint32_t C_BUG = 0xB4553A;
constexpr uint32_t C_BEE = 0xD9A514;      // 蜜蜂黄
constexpr uint32_t C_SHOOT = 0xC9A96A;
constexpr uint32_t C_STAR = 0xE8C558;
constexpr uint32_t C_RED = 0xC0392B;      // 受伤/生命
constexpr uint32_t C_UI = 0x4A4438;       // 浮层文字

// 像素渲染：在 draw 事件里画色块（LVGL9 draw layer）
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
    auto text4x = [&](int x, int y, const char *s, uint32_t color) {
        // 3x5 微型像素字体，每字符 4px 宽
        static const uint8_t glyph[36][5] = {
            {0x7,0x5,0x5,0x5,0x7}, {0x2,0x6,0x2,0x2,0x7}, {0x7,0x1,0x7,0x4,0x7},
            {0x7,0x1,0x7,0x1,0x7}, {0x5,0x5,0x7,0x1,0x1}, {0x7,0x4,0x7,0x1,0x7},
            {0x7,0x4,0x7,0x5,0x7}, {0x7,0x1,0x2,0x2,0x2}, {0x7,0x5,0x7,0x5,0x7},
            {0x7,0x5,0x7,0x1,0x7}, {0x2,0x5,0x7,0x5,0x5}, {0x6,0x5,0x6,0x5,0x6},
            {0x3,0x4,0x4,0x4,0x3}, {0x6,0x5,0x5,0x5,0x6}, {0x7,0x4,0x7,0x4,0x7},
            {0x7,0x4,0x7,0x4,0x4}, {0x7,0x5,0x5,0x7,0x1}, {0x5,0x5,0x7,0x5,0x5},
            {0x7,0x2,0x2,0x2,0x7}, {0x1,0x1,0x1,0x5,0x7}, {0x5,0x5,0x7,0x5,0x5},
            {0x4,0x4,0x4,0x4,0x7}, {0x5,0x7,0x7,0x5,0x5}, {0x5,0x7,0x7,0x5,0x5},
            {0x7,0x5,0x5,0x5,0x7}, {0x7,0x5,0x7,0x4,0x4}, {0x7,0x5,0x5,0x7,0x3},
            {0x7,0x4,0x7,0x1,0x1}, {0x7,0x2,0x7,0x2,0x7}, {0x7,0x2,0x2,0x2,0x2},
            {0x5,0x5,0x5,0x5,0x7}, {0x5,0x5,0x5,0x5,0x7}, {0x5,0x5,0x7,0x5,0x5},
            {0x5,0x5,0x2,0x5,0x5}, {0x5,0x5,0x2,0x2,0x2}, {0x7,0x1,0x7,0x4,0x7},
        };
        dsc.bg_color = lv_color_hex(color);
        for (const char *q = s; *q; ++q) {
            char ch = *q;
            if (ch >= '0' && ch <= '9') ch = (char)(ch - '0');
            else if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 10);
            else if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 10);
            else { continue; }  // 微型字体只画数字/字母
            if (ch < 0 || ch > 35) continue;
            for (int row = 0; row < 5; ++row) {
                uint8_t bits = glyph[(int)ch][row];
                for (int col = 0; col < 3; ++col)
                    if (bits & (4 >> col)) box(x + col, y + row, 1, 1, color);
            }
            x += 4;
        }
    };

    const bool flash_hurt = s_game.hurt_ms && (++s_blink & 2);

    // 背景
    box(0, 0, 240, 240, C_BG);

    // 三根竹竿：通顶全高（竹节每 24px，随滚动移动）
    for (int l = 0; l < PANDA_LANES; ++l) {
        int bx = 40 + l * 80 - 4;
        box(bx, 0, 8, 240, C_BAMBOO);
        int phase = (int)((s_game.scroll_y) % 24);
        for (int y = -phase; y < 240; y += 24) box(bx, y, 8, 3, C_BAMBOO_D);
    }

    // 实体：6 槽位贴面；sy 与碰撞公式严格同源（PANDA_RENDER_TOP）
    for (uint16_t i = 0; i < s_game.entity_count; ++i) {
        const panda_entity_t *en = &s_game.entities[i];
        int sy = kRenderTop - (en->y - (int)s_game.scroll_y) - 12;
        if (sy < -12 || sy > 240) continue;
        uint8_t st_lane = panda_slot_lane(en->lane);
        int8_t side = panda_slot_side(en->lane);
        int pole_x = 40 + st_lane * 80;
        int ex = pole_x + side * 10 - (side > 0 ? 0 : 24);
        switch (en->kind) {
            case PANDA_ENTITY_SNAKE:
                box(ex, sy + 6, 24, 6, C_SNAKE);
                box(ex + 4, sy + 2, 8, 5, C_SNAKE);
                box(ex + 14, sy, 10, 8, C_SNAKE);
                box(ex + 20, sy + 2, 3, 2, C_PANDA_B);
                box(ex + 23, sy + 4, 3, 2, C_BUG);
                box(ex + 2, sy + 12, 4, 3, C_BAMBOO_D);
                break;
            case PANDA_ENTITY_BUG:
                box(ex + 4, sy + 4, 16, 13, C_BUG);
                box(ex + 8, sy + 1, 8, 4, C_PANDA_B);
                box(ex + 2, sy + 6, 3, 2, C_PANDA_B);
                box(ex + 19, sy + 6, 3, 2, C_PANDA_B);
                box(ex + 11, sy + 6, 2, 11, C_PANDA_B);
                box(ex, sy + 8, 4, 2, C_PANDA_B);
                box(ex + 20, sy + 8, 4, 2, C_PANDA_B);
                box(ex + 2, sy + 14, 4, 2, C_PANDA_B);
                box(ex + 18, sy + 14, 4, 2, C_PANDA_B);
                break;
            case PANDA_ENTITY_BEE: {   // 蜜蜂：左右摆动的移动障碍
                int sway = (int)((s_game.tick_ms / 120) % 2) * 8 - 4;
                int bx = ex + 4 + sway;
                box(bx, sy + 4, 16, 10, C_BEE);
                box(bx + 3, sy + 4, 2, 10, C_PANDA_B);   // 纹
                box(bx + 9, sy + 4, 2, 10, C_PANDA_B);
                box(bx - 2, sy + 2, 6, 4, C_PANDA_W);    // 翅
                box(bx + 14, sy + 5, 5, 6, C_PANDA_B);   // 头
                break;
            }
            case PANDA_ENTITY_SHOOT:
                box(ex + 8, sy, 8, 4, C_SHOOT);
                box(ex + 5, sy + 4, 14, 5, C_SHOOT);
                box(ex + 2, sy + 9, 20, 6, C_SHOOT);
                box(ex + 5, sy + 6, 2, 2, C_BAMBOO_D);
                box(ex + 17, sy + 6, 2, 2, C_BAMBOO_D);
                break;
            case PANDA_ENTITY_STAR:
                box(ex + 9, sy, 6, 24, C_STAR);
                box(ex, sy + 9, 24, 6, C_STAR);
                box(ex + 9, sy + 9, 6, 6, C_PANDA_W);
                break;
            default: break;
        }
    }

    // 熊猫（受伤闪烁 = 半透明闪隐；切槽动画横向插值）
    int from_x = 40 + panda_slot_lane(s_game.from_lane) * 80 + panda_slot_side(s_game.from_lane) * 12;
    int to_x = 40 + panda_slot_lane(s_game.lane) * 80 + panda_slot_side(s_game.lane) * 12;
    int px = to_x;
    if (s_game.switch_anim) {
        int t = PANDA_SWITCH_FRAMES - s_game.switch_anim;
        px = from_x + (to_x - from_x) * t / PANDA_SWITCH_FRAMES;
    }
    const bool visible_panda = !flash_hurt || ((++s_blink & 4) != 0);
    if (s_game.state != PANDA_STATE_OVER && visible_panda) {
        const int py = kPandaScreenY;
        box(px - 9, py - 2, 18, 14, C_PANDA_W);
        box(px - 9, py + 8, 18, 6, C_PANDA_B);
        box(px - 7, py - 14, 14, 12, C_PANDA_W);
        box(px - 7, py - 16, 5, 3, C_PANDA_B);
        box(px + 2, py - 16, 5, 3, C_PANDA_B);
        box(px - 5, py - 11, 4, 5, C_PANDA_B);
        box(px + 1, py - 11, 4, 5, C_PANDA_B);
        box(px - 4, py - 10, 2, 2, C_PANDA_W);
        box(px + 2, py - 10, 2, 2, C_PANDA_W);
        box(px - 1, py - 7, 2, 2, C_PANDA_B);
        {
            int toward = panda_slot_side(s_game.lane);
            box(toward < 0 ? px - 13 : px + 10, py + 2, 3, 6, C_PANDA_B);
        }
        if (s_game.invincible_ms && (++s_blink & 2)) {
            box(px - 12, py - 18, 2, 2, C_STAR);
            box(px + 10, py - 18, 2, 2, C_STAR);
            box(px - 12, py + 12, 2, 2, C_STAR);
            box(px + 10, py + 12, 2, 2, C_STAR);
        }
    }

    // ---- 全屏浮层 HUD（画在游戏画面之上） ----
    // 顶栏: 生命(熊猫头×N) + 高度
    for (int i = 0; i < 3; ++i) {
        int hx = 8 + i * 16;
        if (i < (int)s_game.lives) {
            box(hx, 6, 12, 10, C_PANDA_W);   // 迷你熊猫头
            box(hx, 4, 4, 3, C_PANDA_B);
            box(hx + 8, 4, 4, 3, C_PANDA_B);
            box(hx + 2, 8, 3, 4, C_PANDA_B);
            box(hx + 7, 8, 3, 4, C_PANDA_B);
        } else {
            box(hx, 6, 12, 10, 0xD8D2C0);    // 已失去：暗格
        }
    }
    {
        char hbuf[16];
        std::snprintf(hbuf, sizeof(hbuf), "%luM", (unsigned long)s_game.height_m);
        text4x(240 - 8 - 4 * (int)strlen(hbuf), 8, hbuf, C_UI);
    }
    // 连击浮标：×N（≥2 时显示）
    if (s_game.state == PANDA_STATE_RUNNING && s_game.combo >= 2) {
        char cbuf[12];
        std::snprintf(cbuf, sizeof(cbuf), "X%lu", (unsigned long)(s_game.combo > 4 ? 4 : s_game.combo));
        text4x(8, 224, cbuf, C_STAR);
    }
    // 受伤红闪边框
    if (flash_hurt) {
        box(0, 0, 240, 3, C_RED);
        box(0, 237, 240, 3, C_RED);
        box(0, 0, 3, 240, C_RED);
        box(237, 0, 3, 240, C_RED);
    }
    // IDLE / OVER 全屏大字
    if (s_game.state == PANDA_STATE_IDLE) {
        box(30, 92, 180, 44, C_PANDA_W);
        box(32, 94, 176, 40, C_PANDA_B);
        text4x(52, 100, "PANDA", C_STAR);
        text4x(52, 114, "CLIMB", C_PANDA_W);
        // 按确定开始
        char ibuf[24];
        (void)ibuf;
    } else if (s_game.state == PANDA_STATE_OVER) {
        box(24, 86, 192, 56, C_PANDA_W);
        box(26, 88, 188, 52, C_BUG);
        text4x(52, 94, "GAME", C_PANDA_W);
        text4x(52, 108, "OVER", C_PANDA_W);
        char sbuf[20];
        std::snprintf(sbuf, sizeof(sbuf), "%luM BEST%luM",
                      (unsigned long)s_game.height_m, (unsigned long)s_game.best_m);
        text4x(40, 126, sbuf, C_PANDA_W);
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
    s_blink = 0;
    s_screen = lv_obj_create(nullptr);
    ui_pixel_background(s_screen);
    s_canvas = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_canvas);
    lv_obj_set_pos(s_canvas, 0, 0);
    lv_obj_set_size(s_canvas, 240, 240);   // 全屏游戏区
    lv_obj_add_event_cb(s_canvas, draw_game, LV_EVENT_DRAW_MAIN, nullptr);
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
        if (s_game.state != PANDA_STATE_RUNNING) {
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
    bsp_lvgl_unlock();
}
} // namespace

extern const passport_app_t g_panda_app = {
    .id = APP_ID_PANDA,
    .name = "熊猫爬竹竿",
    .en_name = "PANDA CLIMB",
    .desc = "诺基亚经典复刻\n三杆六面躲避虫蛇",
    .tag = "GAME",
    .theme_color = 0x7FA65A,
    .init = init,
    .start = start,
    .stop = stop,
    .on_key = on_key,
};
