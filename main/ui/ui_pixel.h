// main/ui/ui_pixel.h —— 全机共享的像素风设计系统。
//
// 2026-09-13 从 apps/voice 提升为公共模块:配色、纸面板、标题牌、提示胶囊、
// 顶栏、电池/Wi-Fi 图标全部收敛到这一份,Launcher/首页/设置/小智/收音机
// 都用它,保证整机视觉统一。
//
// 设计规范(各 App 必须遵守):
//   配色:天 UI_SKY / 纸 UI_PAPER / 墨 UI_INK / 草 UI_GRASS / 黄 UI_YELLOW /
//        橙 UI_ORANGE / 红 UI_RED;浅底上次要文字用 UI_SUB(不是 UI_MUTED,
//        后者只适用于深底)。
//   构件:卡片=纸底+墨描边(3~4px)+右下错位硬阴影;提示=纸胶囊;标题=纸牌。
//        **全套方角**(radius 0)—— 圆角/胶囊弧线是像素风大忌,勿再引入。
//   字体:静态中文标题 20px(ui_font_20 / voice_font_20),动态中文 16px
//        (buddy_font_16,4360 字全覆盖,2bpp 抗锯齿),数字/英文 Montserrat。
//   字间距:标题 UI_LS_TITLE(+2),正文/提示 UI_LS_BODY(+1) —— ui_pixel_label
//        默认带 +1,不要再手调出第三种值。
//   昼夜:19:00~05:59 自动切夜景(深蓝天+星+萤火虫,草地转暗,云染月光灰),
//        卡片保持纸色。接 ui_pixel_set_hour_theme + ui_pixel_theme_apply。
#pragma once

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_SKY        0x1689E8
#define UI_SKY_DARK   0x0872C9
#define UI_INK        0x17202A
#define UI_PAPER      0xF4F4EA
#define UI_GRASS      0x82BE2D
#define UI_GRASS_DARK 0x55951D
#define UI_YELLOW     0xFFD928
#define UI_ORANGE     0xFFB23E
#define UI_RED        0xE43B2F
#define UI_MUTED      0xD9E7EC   // 深底上的弱文字
#define UI_SUB        0x5B6B73   // 浅底上的弱文字(纸面上的次级信息)

// 字间距规范
#define UI_LS_TITLE   2
#define UI_LS_BODY    1

// 像素基底:天蓝底 + 右上云 + 底部草地(286..320)。在空 screen 上调用。
void ui_pixel_background(lv_obj_t *scr);
lv_obj_t *ui_pixel_screen_create(const char *title);
lv_obj_t *ui_pixel_panel_create(lv_obj_t *parent, int x, int y, int w, int h,
                                uint32_t color);
lv_obj_t *ui_pixel_label(lv_obj_t *parent, const char *text,
                         const lv_font_t *font, uint32_t color);
lv_obj_t *ui_pixel_label_ls(lv_obj_t *parent, const char *text,
                            const lv_font_t *font, uint32_t color,
                            int letter_space);
lv_obj_t *ui_pixel_mascot_create(lv_obj_t *parent, int x, int y);
void ui_pixel_mascot_jump(lv_obj_t *mascot);
void ui_pixel_set_selected(lv_obj_t *panel, bool selected, bool enabled);

// ---- 2026-09-13 新增共享构件 ----

// 标题牌:纸底 + 3px 墨边 + 右下硬阴影,宽度随文字自适应,水平居中于 y。
// 返回牌对象(可继续改 y 或加子对象)。
lv_obj_t *ui_pixel_title_plate(lv_obj_t *parent, int y, const char *text,
                               const lv_font_t *font);

// 底部按键说明:与小智AI同款 —— 无底框纯白文字,底部居中(y≈292)。
lv_obj_t *ui_pixel_hint_pill(lv_obj_t *parent, const char *text,
                             const lv_font_t *font);

// 26px 墨色顶栏(与 AI语音 一致)。返回 bar 容器,子元素用 LEFT_MID/RIGHT_MID
// 对齐即垂直居中。
lv_obj_t *ui_pixel_top_bar(lv_obj_t *parent);

// 像素电池:描边框 + 电量填充 + 百分比文字。
// frame_color/文字色由调用方给(顶栏深底用白,纸面浅底用墨)。
typedef struct {
    lv_obj_t *fill;
    lv_obj_t *label;
} ui_pixel_battery_t;
ui_pixel_battery_t ui_pixel_battery_create(lv_obj_t *parent, int x, int y,
                                           uint32_t frame_color,
                                           const lv_font_t *font);
void ui_pixel_battery_set(ui_pixel_battery_t *batt, int soc);  // soc<0 显示 "--"

// Wi-Fi 信号 3 格(几何条,不依赖图标字体)。
void ui_pixel_wifi_bars_create(lv_obj_t *parent, int x, int y,
                               lv_obj_t *bars[3]);
void ui_pixel_wifi_bars_set(lv_obj_t *bars[3], int level, uint32_t color);

// ---- 昼夜主题(2026-09-13) ----

// 按小时切主题:19~23 点与 0~5 点为夜景,hour<0(时钟未校准)保持白天。
// 主题真的发生变化时返回 true —— 此时对当前 screen 调 ui_pixel_theme_apply。
bool ui_pixel_set_hour_theme(int hour);
bool ui_pixel_theme_is_night(void);

// 把 scr 的背景(天空/云/草/星/花/萤火虫)重染成当前主题。
// 只对"最近一次创建过 ui_pixel_background 的 screen"生效,返回是否生效。
bool ui_pixel_theme_apply(lv_obj_t *scr);

#ifdef __cplusplus
}
#endif
