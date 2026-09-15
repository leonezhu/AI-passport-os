// main/ui/ui_pixel.c —— 全机共享的像素风设计系统实现。见 ui_pixel.h。
#include "ui_pixel.h"

#include <stdio.h>

static void start_blink(lv_obj_t *eye);

static lv_obj_t *block(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    return obj;
}

lv_obj_t *ui_pixel_label_ls(lv_obj_t *parent, const char *text,
                            const lv_font_t *font, uint32_t color,
                            int letter_space)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(label, letter_space, 0);
    return label;
}

lv_obj_t *ui_pixel_label(lv_obj_t *parent, const char *text,
                         const lv_font_t *font, uint32_t color)
{
    return ui_pixel_label_ls(parent, text, font, color, UI_LS_BODY);
}

// ==================== 昼夜主题(2026-09-13) ====================

// 夜间只动"环境色":天空压深、草地转暗、云染月光灰;纸卡/墨描边原样,
// 夜景靠环境色表达,卡片像贴在夜空上的纸贴画。
#define UI_SKY_NIGHT    0x0D2144
#define UI_GRASS_NIGHT 0x2A5220

static bool s_night = false;

bool ui_pixel_theme_is_night(void) { return s_night; }

bool ui_pixel_set_hour_theme(int hour)
{
    // hour<0 = 时钟未校准,保持白天
    const bool night = hour >= 0 && (hour >= 19 || hour < 6);
    if (night == s_night) return false;
    s_night = night;
    return true;
}

// ---- 背景部件登记表:theme_apply 按主题重染色块或显隐 ----
// vis: 0=两主题都显示 1=仅白天(花草) 2=仅夜间(星星/萤火虫)
#define UI_BG_PARTS_MAX 64
typedef struct {
    lv_obj_t *obj;
    uint32_t day;
    uint32_t night;
    uint8_t vis;
} ui_bg_part_t;
static ui_bg_part_t s_bg_parts[UI_BG_PARTS_MAX];
static int s_bg_nparts;
static lv_obj_t *s_bg_screen;   // 最近一次创建背景的 screen;重建即重绑

static lv_obj_t *bg_block(lv_obj_t *parent, int x, int y, int w, int h,
                          uint32_t day, uint32_t night, uint8_t vis)
{
    lv_obj_t *o = block(parent, x, y, w, h, day);
    if (s_bg_nparts < UI_BG_PARTS_MAX) {
        ui_bg_part_t *p = &s_bg_parts[s_bg_nparts++];
        p->obj = o;
        p->day = day;
        p->night = night;
        p->vis = vis;
    }
    return o;
}

static void cloud_x(void *obj, int32_t value)
{
    lv_obj_set_x((lv_obj_t *)obj, value);
}

// 一朵云 = 透明容器 + 4 个色块(夜间染月光灰)。容器交给 lv_anim 极速缓慢
// 横漂(约 1 分钟一趟),从顶栏下方露出一条白边;删屏时动画随对象一起清掉
// (lv_obj_destructor → lv_anim_delete,已核对 lv_obj.c:614)。
static void add_cloud(int x, int y, uint32_t drift_ms, uint32_t delay_ms)
{
    lv_obj_t *c = lv_obj_create(s_bg_screen);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, 45, 18);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);

    bg_block(c, 1, 7, 43, 10, UI_INK, UI_INK, 0);
    bg_block(c, 5, 4, 35, 10, 0xFFFFFF, 0xA9BDD9, 0);
    bg_block(c, 12, 0, 10, 9, 0xFFFFFF, 0xA9BDD9, 0);
    bg_block(c, 27, 1, 9, 8, 0xFFFFFF, 0xA9BDD9, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, c);
    lv_anim_set_exec_cb(&a, cloud_x);
    lv_anim_set_values(&a, -50, 290);
    lv_anim_set_duration(&a, drift_ms);
    lv_anim_set_delay(&a, delay_ms);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

// ---- 星星(仅夜间):贴着顶栏下沿,交替闪烁 ----
static lv_obj_t *s_bg_stars[5];
static int s_bg_nstars;

static void star_opa(void *obj, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)value, 0);
}

static void stars_anim_start(void)
{
    for (int i = 0; i < s_bg_nstars; ++i) {
        lv_anim_delete(s_bg_stars[i], star_opa);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_bg_stars[i]);
        lv_anim_set_exec_cb(&a, star_opa);
        lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_30);
        lv_anim_set_duration(&a, 500);
        lv_anim_set_playback_duration(&a, 500);
        lv_anim_set_repeat_delay(&a, 900 + i * 400);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&a, lv_anim_path_step);
        lv_anim_start(&a);
    }
}

static void stars_anim_stop(void)
{
    for (int i = 0; i < s_bg_nstars; ++i) {
        lv_anim_delete(s_bg_stars[i], star_opa);
        lv_obj_set_style_opa(s_bg_stars[i], LV_OPA_COVER, 0);
    }
}

// ---- 萤火虫(仅夜间):2×2 黄点沿草地低空巡游 + 呼吸明灭 ----
static lv_obj_t *s_bg_firefly;

static void fly_x(void *obj, int32_t value)
{
    lv_obj_set_x((lv_obj_t *)obj, value);
}

static void fly_glow(void *obj, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)value, 0);
}

static void firefly_anim_start(void)
{
    if (!s_bg_firefly) return;
    lv_anim_delete(s_bg_firefly, fly_x);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_bg_firefly);
    lv_anim_set_exec_cb(&a, fly_x);
    lv_anim_set_values(&a, 24, 208);
    lv_anim_set_duration(&a, 9000);
    lv_anim_set_playback_duration(&a, 9000);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);

    lv_anim_delete(s_bg_firefly, fly_glow);
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_bg_firefly);
    lv_anim_set_exec_cb(&a, fly_glow);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_40);
    lv_anim_set_duration(&a, 420);
    lv_anim_set_playback_duration(&a, 420);
    lv_anim_set_repeat_delay(&a, 600);
    lv_anim_set_path_cb(&a, lv_anim_path_step);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static void firefly_anim_stop(void)
{
    if (!s_bg_firefly) return;
    lv_anim_delete(s_bg_firefly, fly_x);
    lv_anim_delete(s_bg_firefly, fly_glow);
    lv_obj_set_style_opa(s_bg_firefly, LV_OPA_COVER, 0);
}

// ---- 小花(仅白天):十字花瓣 + 花芯 + 茎叶,贴在草地两端 ----
static void add_flower(int x, int y, uint32_t petal)
{
    bg_block(s_bg_screen, x + 3, y + 3, 3, 3, petal, petal, 1);          // 上瓣
    bg_block(s_bg_screen, x + 3, y + 9, 3, 3, petal, petal, 1);          // 下瓣
    bg_block(s_bg_screen, x, y + 6, 3, 3, petal, petal, 1);              // 左瓣
    bg_block(s_bg_screen, x + 6, y + 6, 3, 3, petal, petal, 1);          // 右瓣
    bg_block(s_bg_screen, x + 3, y + 6, 3, 3, UI_YELLOW, UI_YELLOW, 1);  // 花芯
    bg_block(s_bg_screen, x + 4, y + 12, 2, 5, 0x3E7C22, 0x3E7C22, 1);   // 茎
    bg_block(s_bg_screen, x + 6, y + 13, 3, 2, 0x55951D, 0x55951D, 1);   // 叶
}

void ui_pixel_background(lv_obj_t *scr)
{
    s_bg_screen = scr;
    s_bg_nparts = 0;
    s_bg_nstars = 0;
    s_bg_firefly = NULL;

    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_SKY), 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    // 星星先建,云后建 —— 云飘过时正好把星遮住
    static const int star_xy[][2] = {
        {34, 29}, {76, 28}, {148, 29}, {206, 28}, {118, 30},
    };
    const int nstars = (int)(sizeof(star_xy) / sizeof(star_xy[0]));
    for (int i = 0; i < nstars; ++i) {
        const int side = (i % 2) ? 2 : 3;
        s_bg_stars[i] = bg_block(scr, star_xy[i][0], star_xy[i][1],
                                 side, side, 0xFFE9A8, 0xFFE9A8, 2);
        s_bg_nstars++;
    }

    // 云:底沿(y26..34 一带)从顶栏下露出,慢速横漂。起点一律在屏外,
    // 带延迟的那朵到点才从左边进场,避免半路瞬移。
    add_cloud(-50, 22, 52000, 0);
    add_cloud(-50, 23, 68000, 18000);

    // 草地:亮缘 + 草簇 + 泥块
    bg_block(scr, 0, 286, 240, 34, UI_GRASS, UI_GRASS_NIGHT, 0);
    bg_block(scr, 0, 286, 240, 4, 0xA7D93E, 0x35651F, 0);
    for (int x = 0; x < 240; x += 30) {
        bg_block(scr, x, 312, 18, 8, UI_GRASS_DARK, 0x173612, 0);
        bg_block(scr, x + 18, 316, 12, 4, 0x75452E, 0x45291B, 0);
    }

    // 小花贴草地两端(白字 footer 居中占 x12..228,两端是空位);萤火虫巡草皮
    add_flower(2, 291, 0xE43B2F);
    add_flower(229, 291, 0xFFFFFF);
    s_bg_firefly = bg_block(scr, 60, 313, 2, 2, 0xFFE97A, 0xFFE97A, 2);

    // 按当前主题染一遍:夜间开机首屏直接是夜景
    ui_pixel_theme_apply(scr);
}

bool ui_pixel_theme_apply(lv_obj_t *scr)
{
    if (!scr || scr != s_bg_screen) return false;
    lv_obj_set_style_bg_color(scr,
                              lv_color_hex(s_night ? UI_SKY_NIGHT : UI_SKY), 0);
    for (int i = 0; i < s_bg_nparts; ++i) {
        ui_bg_part_t *p = &s_bg_parts[i];
        const bool hide = (p->vis == 1 && s_night) || (p->vis == 2 && !s_night);
        if (hide) {
            lv_obj_add_flag(p->obj, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(p->obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(p->obj,
                                  lv_color_hex(s_night ? p->night : p->day), 0);
    }
    if (s_night) {
        stars_anim_start();
        firefly_anim_start();
    } else {
        stars_anim_stop();
        firefly_anim_stop();
    }
    return true;
}

lv_obj_t *ui_pixel_screen_create(const char *title)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    ui_pixel_background(scr);

    block(scr, 9, 12, 151, 33, UI_INK);
    lv_obj_t *plate = block(scr, 5, 8, 151, 33, UI_PAPER);
    lv_obj_set_style_border_color(plate, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_border_width(plate, 3, 0);
    lv_obj_t *heading = ui_pixel_label(plate, title, &lv_font_montserrat_20, UI_INK);
    lv_obj_center(heading);
    return scr;
}

lv_obj_t *ui_pixel_panel_create(lv_obj_t *parent, int x, int y, int w, int h,
                                uint32_t color)
{
    block(parent, x + 5, y + 6, w, h, UI_INK);
    lv_obj_t *panel = block(parent, x, y, w, h, color);
    lv_obj_set_style_border_color(panel, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_border_width(panel, 4, 0);
    lv_obj_set_style_pad_all(panel, 7, 0);
    return panel;
}

lv_obj_t *ui_pixel_mascot_create(lv_obj_t *parent, int x, int y)
{
    lv_obj_t *m = lv_obj_create(parent);
    lv_obj_remove_flag(m, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(m, x, y);
    lv_obj_set_size(m, 38, 48);
    lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(m, 0, 0);
    lv_obj_set_style_pad_all(m, 0, 0);

    /* 原创“小电视机器人”：天线、发光屏幕脸、橙色围巾与履带脚。 */
    block(m, 18, 0, 3, 6, UI_INK);
    block(m, 16, 0, 7, 3, UI_ORANGE);
    block(m, 3, 6, 32, 24, UI_INK);
    block(m, 0, 12, 5, 10, 0x7557D9);
    block(m, 33, 12, 5, 10, 0x7557D9);
    block(m, 7, 10, 24, 16, 0xB9F3FF);
    lv_obj_t *left_eye = block(m, 11, 14, 4, 6, 0x294B7A);
    lv_obj_t *right_eye = block(m, 23, 14, 4, 6, 0x294B7A);
    block(m, 16, 22, 7, 2, 0x7557D9);
    block(m, 10, 29, 18, 4, UI_ORANGE);
    block(m, 8, 33, 22, 11, 0x7557D9);
    block(m, 3, 35, 5, 7, 0xB9F3FF);
    block(m, 30, 35, 5, 7, 0xB9F3FF);
    block(m, 8, 44, 9, 4, UI_INK);
    block(m, 21, 44, 9, 4, UI_INK);
    start_blink(left_eye);
    start_blink(right_eye);
    return m;
}

static void jump_y(void *obj, int32_t value)
{
    lv_obj_set_y((lv_obj_t *)obj, value);
}

static void blink_eye(void *obj, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)value, 0);
}

static void start_blink(lv_obj_t *eye)
{
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, eye);
    lv_anim_set_exec_cb(&anim, blink_eye);
    lv_anim_set_values(&anim, LV_OPA_COVER, LV_OPA_20);
    lv_anim_set_duration(&anim, 70);
    lv_anim_set_playback_duration(&anim, 70);
    lv_anim_set_repeat_delay(&anim, 1700);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&anim, lv_anim_path_step);
    lv_anim_start(&anim);
}

void ui_pixel_mascot_jump(lv_obj_t *mascot)
{
    if (!mascot) return;
    int y = lv_obj_get_y(mascot);
    lv_anim_delete(mascot, jump_y);
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, mascot);
    lv_anim_set_exec_cb(&anim, jump_y);
    lv_anim_set_values(&anim, y, y - 5);
    lv_anim_set_duration(&anim, 110);
    lv_anim_set_playback_duration(&anim, 140);
    lv_anim_set_path_cb(&anim, lv_anim_path_step);
    lv_anim_start(&anim);
}

void ui_pixel_set_selected(lv_obj_t *panel, bool selected, bool enabled)
{
    uint32_t color = !enabled ? 0x78909C : (selected ? UI_YELLOW : UI_PAPER);
    lv_obj_set_style_bg_color(panel, lv_color_hex(color), 0);
    lv_obj_set_style_border_color(panel,
        lv_color_hex(selected ? 0xFFFFFF : UI_INK), 0);
}

// ==================== 2026-09-13 新增共享构件 ====================

// 文字像素宽 = 标签布局后的真实宽度(字号/混排都不用手算)
static int text_w(lv_obj_t *label)
{
    lv_obj_update_layout(label);
    return (int)lv_obj_get_width(label);
}

lv_obj_t *ui_pixel_title_plate(lv_obj_t *parent, int y, const char *text,
                               const lv_font_t *font)
{
    // 先建牌+字,量出字宽再定牌宽;阴影块同理后补尺寸。
    lv_obj_t *shadow = block(parent, 0, y + 5, 10, 10, UI_INK);
    lv_obj_t *plate = block(parent, 0, y, 10, 10, UI_PAPER);
    lv_obj_set_style_border_width(plate, 3, 0);
    lv_obj_set_style_border_color(plate, lv_color_hex(UI_INK), 0);
    lv_obj_t *t = ui_pixel_label_ls(plate, text, font, UI_INK, UI_LS_TITLE);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 12, 0);

    const int w = text_w(t) + 24;              // 左右各 12px 内边距
    const int h = (int)lv_font_get_line_height(font) + 14;
    lv_obj_set_size(plate, w, h);
    lv_obj_set_pos(plate, (240 - w) / 2, y);
    lv_obj_set_size(shadow, w, h);
    lv_obj_set_pos(shadow, (240 - w) / 2 + 4, y + 5);
    return plate;
}

lv_obj_t *ui_pixel_hint_pill(lv_obj_t *parent, const char *text,
                             const lv_font_t *font)
{
    // 与小智AI的底部按键说明同款,但压在草地上纯白字对比度不足 ——
    // 先铺一份墨色错位(+1,+1)字,再叠白字,得到像素风的"字投影"。
    // 调用方只在创建时设一次文字(launcher/settings),阴影不用跟随更新;
    // 若将来要动态改字,请两份一起改,或把本函数改为返回结构体。
    lv_obj_t *shadow = ui_pixel_label_ls(parent, text, font, UI_INK, UI_LS_BODY);
    lv_obj_align(shadow, LV_ALIGN_BOTTOM_MID, 1, -7);
    lv_obj_t *t = ui_pixel_label(parent, text, font, 0xFFFFFF);
    lv_obj_align(t, LV_ALIGN_BOTTOM_MID, 0, -8);
    return t;
}

lv_obj_t *ui_pixel_top_bar(lv_obj_t *parent)
{
    return block(parent, 0, 0, 240, 26, UI_INK);
}

ui_pixel_battery_t ui_pixel_battery_create(lv_obj_t *parent, int x, int y,
                                           uint32_t frame_color,
                                           const lv_font_t *font)
{
    ui_pixel_battery_t b = { NULL, NULL };
    lv_obj_t *frame = block(parent, x, y, 24, 12, UI_INK);
    lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(frame, 2, 0);
    lv_obj_set_style_border_color(frame, lv_color_hex(frame_color), 0);
    lv_obj_set_style_radius(frame, 3, 0);
    block(parent, x + 24, y + 3, 3, 6, frame_color);      // 右缘触点

    b.fill = block(frame, 2, 2, 2, 6, frame_color);
    lv_obj_set_style_radius(b.fill, 1, 0);

    // 宽度按最宽内容 "100%"(约35px) 取 38,窄了会折行把百分比挤到下一行
    b.label = ui_pixel_label(parent, "--", font, frame_color);
    lv_obj_set_pos(b.label, x - 42, y - 2);
    lv_obj_set_width(b.label, 38);
    lv_obj_set_style_text_align(b.label, LV_TEXT_ALIGN_RIGHT, 0);
    return b;
}

void ui_pixel_battery_set(ui_pixel_battery_t *batt, int soc)
{
    if (!batt || !batt->fill || !batt->label) return;
    char text[8];
    uint32_t color;
    if (soc >= 0 && soc <= 100) {
        // 满电绿、中段黄、低电红 —— 与整机"状态色"语义一致。
        color = soc <= 15 ? (uint32_t)UI_RED
                          : (soc <= 35 ? (uint32_t)UI_ORANGE : (uint32_t)UI_GRASS);
        snprintf(text, sizeof(text), "%d%%", soc);
        lv_obj_set_width(batt->fill, 1 + (17 * soc) / 100);
    } else {
        color = 0x8A97A0;
        snprintf(text, sizeof(text), "--");
        lv_obj_set_width(batt->fill, 1);
    }
    lv_obj_set_style_bg_color(batt->fill, lv_color_hex(color), 0);
    lv_label_set_text(batt->label, text);
}

void ui_pixel_wifi_bars_create(lv_obj_t *parent, int x, int y, lv_obj_t *bars[3])
{
    // y 为最低一条的底边;三条逐级升高,占位 16×13。
    for (int i = 0; i < 3; ++i) {
        const int h = 5 + i * 4;
        bars[i] = block(parent, x + i * 6, y - h, 4, h, 0x8A97A0);
        lv_obj_set_style_radius(bars[i], 1, 0);
    }
}

void ui_pixel_wifi_bars_set(lv_obj_t *bars[3], int level, uint32_t color)
{
    for (int i = 0; i < 3; ++i) {
        if (!bars[i]) continue;
        lv_obj_set_style_bg_color(bars[i], lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(bars[i], i < level ? LV_OPA_COVER : LV_OPA_30, 0);
    }
}
