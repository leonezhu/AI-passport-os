// Voice pages: shared terminal frames, readable text and live audio meters.
#include "app_ui.h"
#include "app_fonts.h"       // buddy_font_16 / voice_font_20
#include "audio_streamer.h"  // 录音时实时音量:audio_streamer_peak(0..32767)
#include "bsp_battery.h"     // 电量
#include "bsp_display.h"
#include "time_sync.h"       // 顶栏 HH:MM
#include "ui_pixel.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// ---- 版式常量 ----
#define W            240
#define H            320
#define BAR_H        26    // 顶栏高
#define BANNER_Y     28    // 悬浮横幅起点
#define BANNER_H     24
#define CONTENT_Y    70    // 内容区起点
#define HINT_Y       292   // 底部提示行

// 顶栏元素坐标
#define ST_DOT_X     12
#define ST_DOT_Y     10
#define ST_DOT_S     6
#define ST_TXT_X     24
#define TIME_X       100

// ---- 中文文案(已包含在 voice_font_20 字符集中) ----
static const char *const T_LINK_ON    = "已连接";
static const char *const T_LINK_OFF   = "未连接";
static const char *const T_OFFLINE    = "与电脑断开，正在重连";
static const char *const T_BUSY       = "网络拥塞，正在丢弃音频帧";
static const char *const T_HINT_HOME  = "确定 进入    长按下 清空";
static const char *const T_HINT_READY = "按住 上键 说话";
static const char *const T_HINT_REC   = "松开 上键 完成";
static const char *const T_HINT_TR    = "下键 发送    上键 返回";
static const char *const T_HINT_RUN   = "请稍候";
static const char *const T_HINT_AP    = "确定 批准    上键 拒绝";
static const char *const T_RISK[APP_RISK_COUNT] = { "低风险", "中风险", "高风险" };

// ---- 页内部件索引(与 page 切换共用) ----
typedef struct {
    lv_obj_t *root;                       // 本页容器(显隐切换)
    lv_obj_t *rec_elapsed;                // LISTENING:计时(REC 00:03)
    lv_obj_t *rec_bars[5];                // LISTENING:5 根动态像素声波柱
    lv_obj_t *live_text;                  // LISTENING:实时转写文本
    lv_obj_t *run_chip;                   // AGENT_RUNNING:状态牌底
    lv_obj_t *run_chip_label;             // AGENT_RUNNING:状态名
    lv_obj_t *run_message;                // AGENT_RUNNING:消息
    lv_obj_t *ap_risk_banner;             // APPROVAL:风险牌底
    lv_obj_t *ap_risk_label;              // APPROVAL:风险文本
    lv_obj_t *ap_title;                   // APPROVAL:标题
    lv_obj_t *ap_target;                  // APPROVAL:目标
    lv_obj_t *ap_diff;                    // APPROVAL:摘要/详情
    lv_obj_t *mascot;                     // HOME:吉祥物
} page_t;

static lv_obj_t *s_chrome;                // 顶层容器(lv_layer_top)
static lv_obj_t *s_chrome_box = NULL;
static lv_obj_t *s_link_dot;              // 顶栏链路状态方块
static lv_obj_t *s_link_text;             // 顶栏链路状态文字
static ui_pixel_battery_t s_batt = {NULL, NULL};  // 共享像素电池
static lv_obj_t *s_time_label;
static lv_obj_t *s_offline_banner;
static lv_obj_t *s_offline_text;
static lv_obj_t *s_netbusy_banner;
static lv_obj_t *s_netbusy_text;
static lv_obj_t *s_toast;

static page_t s_pages[APP_ST_COUNT];
static app_stage_t s_cur_page = APP_ST_COUNT;
static bool s_last_screen_on = true;
static lv_obj_t *s_bg = NULL;             // 基底屏

// 样式脏检查
static int s_last_link_up = -1;
static int s_last_risk = -1;
static char s_last_chip[16];

static const uint32_t RISK_COLORS[APP_RISK_COUNT] = { UI_GRASS, UI_YELLOW, UI_RED };

// ---- 基础纯方角块(零圆角像素构件) ----
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

static lv_obj_t *mk_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                          uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_line_space(l, 3, 0);
    return l;
}

// 状态页容器:透明、无边框、铺满
static lv_obj_t *page_root(page_t *p)
{
    lv_obj_t *r = lv_obj_create(s_bg);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_set_size(r, W, H);
    lv_obj_set_pos(r, 0, 0);
    p->root = r;
    return r;
}

// ---- 微插画 1:精致复古金属麦克风(中心对齐 cx, cy) ----
static void mic_icon(lv_obj_t *parent, int cx, int cy)
{
    // 话筒网罩(墨框 + 银灰网面 + 细条纹)
    block(parent, cx - 10, cy - 22, 20, 24, UI_BORDER);
    block(parent, cx - 8, cy - 20, 16, 20, UI_ACCENT);
    block(parent, cx - 8, cy - 14, 16, 2, UI_BORDER);
    block(parent, cx - 8, cy - 8, 16, 2, UI_BORDER);
    block(parent, cx - 2, cy - 20, 4, 20, UI_BORDER);

    // U 型金属托架
    block(parent, cx - 15, cy - 8, 3, 16, UI_BORDER);
    block(parent, cx + 12, cy - 8, 3, 16, UI_BORDER);
    block(parent, cx - 15, cy + 8, 30, 3, UI_BORDER);

    // 立杆与底座
    block(parent, cx - 2, cy + 11, 4, 8, UI_BORDER);
    block(parent, cx - 12, cy + 19, 24, 4, UI_BORDER);

    // 双侧扩散声波微弧(左/右各 2 弧)
    block(parent, cx - 21, cy - 15, 3, 10, UI_SKY_DARK);
    block(parent, cx - 27, cy - 19, 3, 18, UI_TEXT_DIM);
    block(parent, cx + 18, cy - 15, 3, 10, UI_SKY_DARK);
    block(parent, cx + 24, cy - 19, 3, 18, UI_TEXT_DIM);
}

// ---- 微插画 2:像素双打勾(成功送达图标, cx, cy 为右下交界) ----
static void check_icon(lv_obj_t *parent, int cx, int cy)
{
    // 墨色阴影底
    block(parent, cx - 15, cy + 3, 5, 5, UI_BORDER);
    block(parent, cx - 11, cy + 7, 5, 5, UI_BORDER);
    block(parent, cx - 7,  cy + 3, 5, 5, UI_BORDER);
    block(parent, cx - 3,  cy - 2, 5, 5, UI_BORDER);
    block(parent, cx + 1,  cy - 7, 5, 5, UI_BORDER);
    block(parent, cx + 5,  cy - 12, 5, 5, UI_BORDER);

    // 鲜绿色第 1 勾
    block(parent, cx - 16, cy + 2, 4, 4, UI_GRASS);
    block(parent, cx - 12, cy + 6, 4, 4, UI_GRASS);
    block(parent, cx - 8,  cy + 2, 4, 4, UI_GRASS);
    block(parent, cx - 4,  cy - 3, 4, 4, UI_GRASS);
    block(parent, cx,      cy - 8, 4, 4, UI_GRASS);
    block(parent, cx + 4,  cy - 13, 4, 4, UI_GRASS);

    // 鲜黄色第 2 勾(右错位 8px, 增强层次与完成感)
    block(parent, cx - 8,  cy + 2, 4, 4, UI_YELLOW);
    block(parent, cx - 4,  cy + 6, 4, 4, UI_YELLOW);
    block(parent, cx,      cy + 2, 4, 4, UI_YELLOW);
    block(parent, cx + 4,  cy - 3, 4, 4, UI_YELLOW);
    block(parent, cx + 8,  cy - 8, 4, 4, UI_YELLOW);
    block(parent, cx + 12, cy - 13, 4, 4, UI_YELLOW);
}

// ---- 基底屏构建 ----
static void build_background(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    s_bg = scr;
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    ui_pixel_background(scr);
}

// ---- 常驻顶栏 / 悬浮纸条 / Toast ----
static void build_chrome(void)
{
    lv_obj_t *layer_top = lv_display_get_layer_top(lv_display_get_default());
    s_chrome_box = lv_obj_create(layer_top);
    lv_obj_remove_style_all(s_chrome_box);
    lv_obj_set_size(s_chrome_box, W, H);
    lv_obj_remove_flag(s_chrome_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_chrome_box, 0, 0);
    s_chrome = s_chrome_box;

    // 26px 墨色顶栏
    lv_obj_t *bar = ui_pixel_top_bar(s_chrome);

    // 左:链路状态(6x6 像素方块 + 中文)
    s_link_dot = block(bar, ST_DOT_X, ST_DOT_Y, ST_DOT_S, ST_DOT_S, UI_GRASS);
    s_link_text = mk_label(bar, T_LINK_ON, &buddy_font_16, UI_TEXT);
    lv_obj_align(s_link_text, LV_ALIGN_LEFT_MID, ST_TXT_X, 0);

    // 右:共享像素电池 + 本地时间
    s_batt = ui_pixel_battery_create(bar, 206, 6, UI_TEXT, &lv_font_montserrat_14);
    s_time_label = mk_label(bar, "--:--", &lv_font_montserrat_14, UI_TEXT);
    lv_obj_align(s_time_label, LV_ALIGN_LEFT_MID, TIME_X, 0);

    // 悬浮断链提示纸条(浅红底 + 墨框 + 居中)
    s_offline_banner = block(s_chrome, 16, BANNER_Y, 208, BANNER_H, UI_SURFACE_ALT);
    lv_obj_set_style_border_width(s_offline_banner, 2, 0);
    lv_obj_set_style_border_color(s_offline_banner, lv_color_hex(UI_BORDER), 0);
    s_offline_text = mk_label(s_offline_banner, T_OFFLINE, &buddy_font_16, UI_RED);
    lv_obj_center(s_offline_text);

    // 悬浮拥塞提示纸条(浅黄底 + 墨框)
    s_netbusy_banner = block(s_chrome, 16, BANNER_Y, 208, BANNER_H, UI_SURFACE_ALT);
    lv_obj_set_style_border_width(s_netbusy_banner, 2, 0);
    lv_obj_set_style_border_color(s_netbusy_banner, lv_color_hex(UI_BORDER), 0);
    s_netbusy_text = mk_label(s_netbusy_banner, T_BUSY, &buddy_font_16, UI_TEXT);
    lv_obj_center(s_netbusy_text);

    // Toast 浮动提示
    lv_obj_t *tbg = block(s_chrome, 16, 246, 208, 30, UI_PAPER);
    lv_obj_set_style_border_width(tbg, 2, 0);
    lv_obj_set_style_border_color(tbg, lv_color_hex(UI_BORDER), 0);
    s_toast = mk_label(tbg, "", &buddy_font_16, UI_RED);
    lv_obj_center(s_toast);
}

// ---- 各状态页 ----

static void build_home(void)
{
    page_t *p = &s_pages[APP_ST_HOME];
    lv_obj_t *r = page_root(p);

    ui_pixel_title_plate(r, 30, "随身 AI 语音", &voice_font_20);

    lv_obj_t *card = ui_pixel_panel_create(r, 16, CONTENT_Y, 208, 180, UI_PAPER);

    mic_icon(card, 104, 38);

    lv_obj_t *t_main = mk_label(card, "按住上键说话", &buddy_font_16, UI_TEXT);
    lv_obj_align(t_main, LV_ALIGN_TOP_MID, 0, 72);

    lv_obj_t *t_sub = mk_label(card, "文字实时飞入电脑光标", &buddy_font_16, UI_SUB);
    lv_obj_align(t_sub, LV_ALIGN_TOP_MID, 0, 102);

    lv_obj_t *btn = block(card, 24, 134, 160, 30, UI_YELLOW);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(UI_BORDER), 0);
    lv_obj_t *start = mk_label(btn, "按 确定 进入", &buddy_font_16, UI_ON_ACCENT);
    lv_obj_center(start);

    ui_pixel_hint_pill(r, T_HINT_HOME, &buddy_font_16);
}

static void build_ready(void)
{
    page_t *p = &s_pages[APP_ST_READY];
    lv_obj_t *r = page_root(p);

    ui_pixel_title_plate(r, 30, "准备就绪", &voice_font_20);

    lv_obj_t *card = ui_pixel_panel_create(r, 16, CONTENT_Y, 208, 180, UI_PAPER);

    mic_icon(card, 104, 38);

    lv_obj_t *t_main = mk_label(card, "按住 上键 说话", &buddy_font_16, UI_TEXT);
    lv_obj_align(t_main, LV_ALIGN_TOP_MID, 0, 74);

    lv_obj_t *t_sub = mk_label(card, "松开上键即完成输入", &buddy_font_16, UI_SUB);
    lv_obj_align(t_sub, LV_ALIGN_TOP_MID, 0, 104);

    lv_obj_t *badge = block(card, 34, 134, 140, 26, UI_SURFACE_ALT);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(badge, lv_color_hex(UI_BORDER), 0);
    lv_obj_t *b_txt = mk_label(badge, "随时说话就绪", &buddy_font_16, UI_GRASS_DARK);
    lv_obj_center(b_txt);

    ui_pixel_hint_pill(r, T_HINT_READY, &buddy_font_16);
}

static void build_listening(void)
{
    page_t *p = &s_pages[APP_ST_LISTENING];
    lv_obj_t *r = page_root(p);

    ui_pixel_title_plate(r, 30, "正在聆听...", &voice_font_20);

    lv_obj_t *card = ui_pixel_panel_create(r, 16, CONTENT_Y, 208, 184, UI_PAPER);

    // 录音状态微徽章:REC 00:00
    lv_obj_t *rec_badge = block(card, 54, 8, 100, 22, UI_INK);
    block(rec_badge, 8, 8, 6, 6, UI_RED);
    p->rec_elapsed = mk_label(rec_badge, "REC 00:00", &lv_font_montserrat_14, UI_TEXT);
    lv_obj_align(p->rec_elapsed, LV_ALIGN_LEFT_MID, 22, 0);

    // 5 柱动态跳动像素声波柱(横向居中对齐,各宽 18px, 间距 8px)
    for (int i = 0; i < 5; i++) {
        lv_obj_t *bar = lv_bar_create(card);
        lv_obj_set_size(bar, 18, 38);
        lv_obj_set_pos(bar, 43 + i * 26, 38);
        lv_obj_set_style_radius(bar, 0, 0);
        lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_border_color(bar, lv_color_hex(UI_BORDER), 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(UI_SURFACE_ALT), 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(UI_GRASS), LV_PART_INDICATOR);
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, 10, LV_ANIM_OFF);
        p->rec_bars[i] = bar;
    }

    // 分割线
    block(card, 14, 84, 180, 2, UI_BORDER);

    // 实时转写文字区
    p->live_text = mk_label(card, "对准麦克风说话...", &buddy_font_16, UI_SUB);
    lv_obj_set_width(p->live_text, 180);
    lv_obj_set_pos(p->live_text, 14, 94);
    lv_label_set_long_mode(p->live_text, LV_LABEL_LONG_WRAP);

    ui_pixel_hint_pill(r, T_HINT_REC, &buddy_font_16);
}

static void build_transcribing(void)
{
    page_t *p = &s_pages[APP_ST_TRANSCRIBING];
    lv_obj_t *r = page_root(p);

    ui_pixel_title_plate(r, 30, "语音完成", &voice_font_20);

    lv_obj_t *card = ui_pixel_panel_create(r, 16, CONTENT_Y, 208, 180, UI_PAPER);

    check_icon(card, 104, 32);

    lv_obj_t *done = mk_label(card, "文字已送达电脑", &buddy_font_16, UI_TEXT);
    lv_obj_align(done, LV_ALIGN_TOP_MID, 0, 62);

    block(card, 14, 96, 180, 2, UI_BORDER);

    lv_obj_t *opt_box = block(card, 14, 106, 180, 56, UI_SURFACE_ALT);
    lv_obj_set_style_border_width(opt_box, 0, 0);
    lv_obj_set_style_bg_opa(opt_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(opt_box, lv_color_hex(UI_BORDER), 0);

    lv_obj_t *t1 = mk_label(opt_box, "下键: 确认发送", &buddy_font_16, UI_TEXT);
    lv_obj_align(t1, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *t2 = mk_label(opt_box, "按上键: 再次语音输入", &buddy_font_16, UI_SUB);
    lv_obj_align(t2, LV_ALIGN_TOP_MID, 0, 30);

    ui_pixel_hint_pill(r, T_HINT_TR, &buddy_font_16);
}

static void build_running(void)
{
    page_t *p = &s_pages[APP_ST_AGENT_RUNNING];
    lv_obj_t *r = page_root(p);

    ui_pixel_title_plate(r, 30, "正在执行", &voice_font_20);

    lv_obj_t *card = ui_pixel_panel_create(r, 16, CONTENT_Y, 208, 180, UI_PAPER);

    p->run_chip = block(card, 44, 16, 120, 32, UI_ORANGE);
    lv_obj_set_style_border_width(p->run_chip, 2, 0);
    lv_obj_set_style_border_color(p->run_chip, lv_color_hex(UI_BORDER), 0);
    p->run_chip_label = mk_label(p->run_chip, "执行中", &buddy_font_16, UI_ON_ACCENT);
    lv_obj_center(p->run_chip_label);

    block(card, 14, 60, 180, 2, UI_BORDER);

    p->run_message = mk_label(card, "", &buddy_font_16, UI_TEXT);
    lv_obj_set_width(p->run_message, 180);
    lv_label_set_long_mode(p->run_message, LV_LABEL_LONG_WRAP);
    lv_obj_align(p->run_message, LV_ALIGN_TOP_MID, 0, 72);

    ui_pixel_hint_pill(r, T_HINT_RUN, &buddy_font_16);
}

static void build_approval(void)
{
    page_t *p = &s_pages[APP_ST_APPROVAL];
    lv_obj_t *r = page_root(p);

    ui_pixel_title_plate(r, 30, "请求审批", &voice_font_20);

    lv_obj_t *card = ui_pixel_panel_create(r, 16, CONTENT_Y, 208, 180, UI_PAPER);

    p->ap_risk_banner = block(card, 44, 12, 120, 30, UI_YELLOW);
    lv_obj_set_style_border_width(p->ap_risk_banner, 2, 0);
    lv_obj_set_style_border_color(p->ap_risk_banner, lv_color_hex(UI_BORDER), 0);
    p->ap_risk_label = mk_label(p->ap_risk_banner, "审批", &buddy_font_16, UI_ON_ACCENT);
    lv_obj_center(p->ap_risk_label);

    p->ap_title = mk_label(card, "", &buddy_font_16, UI_TEXT);
    lv_obj_set_width(p->ap_title, 180);
    lv_label_set_long_mode(p->ap_title, LV_LABEL_LONG_WRAP);
    lv_obj_align(p->ap_title, LV_ALIGN_TOP_MID, 0, 50);

    block(card, 14, 88, 180, 2, UI_BORDER);

    p->ap_target = mk_label(card, "", &buddy_font_16, UI_SKY_DARK);
    lv_obj_set_width(p->ap_target, 180);
    lv_label_set_long_mode(p->ap_target, LV_LABEL_LONG_DOT);
    lv_obj_align(p->ap_target, LV_ALIGN_TOP_LEFT, 14, 96);

    p->ap_diff = mk_label(card, "", &buddy_font_16, UI_SUB);
    lv_obj_set_width(p->ap_diff, 180);
    lv_label_set_long_mode(p->ap_diff, LV_LABEL_LONG_WRAP);
    lv_obj_align(p->ap_diff, LV_ALIGN_TOP_LEFT, 14, 122);

    ui_pixel_hint_pill(r, T_HINT_AP, &buddy_font_16);
}

static void set_hidden(lv_obj_t *o, bool hidden)
{
    if (!o) return;
    if (hidden) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void label_set_if_changed(lv_obj_t *l, const char *text)
{
    if (!l || !text) return;
    const char *cur = lv_label_get_text(l);
    if (cur && strcmp(cur, text) == 0) return;
    lv_label_set_text(l, text);
}

static void label_set_fmt_if_changed(lv_obj_t *l, const char *fmt, ...)
{
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    label_set_if_changed(l, buf);
}

static void set_agent_message(lv_obj_t *label_obj, const char *msg, bool preview)
{
    char buf[APP_AGENT_MSG_MAX + 2];
    if (preview && msg && msg[0]) {
        snprintf(buf, sizeof(buf), "%s_", msg);
        label_set_if_changed(label_obj, buf);
    } else {
        label_set_if_changed(label_obj, msg ? msg : "");
    }
}

static const char *agent_state_zh(const char *en)
{
    if (!en || !en[0]) return "执行中";
    if (strcmp(en, "ready") == 0)    return "就绪";
    if (strcmp(en, "thinking") == 0) return "思考中";
    if (strcmp(en, "running") == 0)  return "执行中";
    if (strcmp(en, "error") == 0)    return "出错";
    if (strcmp(en, "done") == 0)     return "完成";
    return en;
}

static void agent_chip_style(const char *en, uint32_t *bg, uint32_t *fg)
{
    if (en && strcmp(en, "running") == 0)      { *bg = UI_SKY;    *fg = UI_TEXT; }
    else if (en && strcmp(en, "error") == 0)   { *bg = UI_RED;    *fg = UI_ON_ACCENT; }
    else if (en && strcmp(en, "done") == 0)    { *bg = UI_GRASS;  *fg = UI_INK; }
    else if (en && strcmp(en, "thinking") == 0){ *bg = UI_YELLOW; *fg = UI_INK; }
    else                                       { *bg = UI_ORANGE; *fg = UI_INK; }
}

// ---- 页切换 ----
static void build_page(app_stage_t st)
{
    switch (st) {
    case APP_ST_HOME: build_home(); break;
    case APP_ST_READY: build_ready(); break;
    case APP_ST_LISTENING: build_listening(); break;
    case APP_ST_TRANSCRIBING: build_transcribing(); break;
    case APP_ST_AGENT_RUNNING: build_running(); break;
    case APP_ST_APPROVAL: build_approval(); break;
    default: break;
    }
}

static void show_page(app_stage_t st)
{
    if (st == s_cur_page) return;
    if (s_cur_page < APP_ST_COUNT && s_pages[s_cur_page].root) {
        lv_obj_delete(s_pages[s_cur_page].root);
        memset(&s_pages[s_cur_page], 0, sizeof(s_pages[s_cur_page]));
    }
    if (st < APP_ST_COUNT && !s_pages[st].root) build_page(st);
    if (st < APP_ST_COUNT && s_pages[st].root) {
        lv_obj_remove_flag(s_pages[st].root, LV_OBJ_FLAG_HIDDEN);
        s_last_risk = -1;
        s_last_chip[0] = '\0';
    }
    s_cur_page = st;
}

esp_err_t app_ui_init(void)
{
    if (s_bg != NULL) return ESP_OK;
    build_background();
    vTaskDelay(1);
    build_chrome();
    vTaskDelay(1);
    build_home();
    lv_obj_add_flag(s_pages[APP_ST_HOME].root, LV_OBJ_FLAG_HIDDEN);
    show_page(APP_ST_HOME);
    app_ui_hide();
    return ESP_OK;
}

void app_ui_show(void)
{
    if (s_bg) {
        lv_screen_load(s_bg);
    }
    if (s_chrome_box) lv_obj_remove_flag(s_chrome_box, LV_OBJ_FLAG_HIDDEN);
}

void app_ui_hide(void)
{
    if (s_chrome_box) lv_obj_add_flag(s_chrome_box, LV_OBJ_FLAG_HIDDEN);
}

void app_ui_deinit(void)
{
    app_ui_hide();
    if (s_chrome_box) lv_obj_delete(s_chrome_box);
    if (s_bg) lv_obj_delete(s_bg);
    memset(s_pages, 0, sizeof(s_pages));
    s_chrome = NULL;
    s_chrome_box = NULL;
    s_link_dot = NULL;
    s_link_text = NULL;
    s_batt = (ui_pixel_battery_t){NULL, NULL};
    s_time_label = NULL;
    s_offline_banner = NULL;
    s_offline_text = NULL;
    s_netbusy_banner = NULL;
    s_netbusy_text = NULL;
    s_toast = NULL;
    s_bg = NULL;
    s_cur_page = APP_ST_COUNT;
}

// ---- 渲染 ----
void app_ui_render(const app_ui_snapshot_t *snap)
{
    if (snap->screen_on != s_last_screen_on) {
        bsp_display_backlight(snap->screen_on ? 100 : 0);
        s_last_screen_on = snap->screen_on;
    }
    if (!snap->screen_on) return;

    // ---- chrome ----
    if ((int)snap->link_up != s_last_link_up) {
        s_last_link_up = (int)snap->link_up;
        lv_obj_set_style_bg_color(s_link_dot,
                                  lv_color_hex(snap->link_up ? UI_GRASS : UI_RED), 0);
    }
    label_set_if_changed(s_link_text, snap->link_up ? T_LINK_ON : T_LINK_OFF);

    ui_pixel_battery_set(&s_batt, snap->battery_available ? snap->battery_soc : -1);
    {
        char t[16];
        time_sync_format_local(t, sizeof(t));
        label_set_if_changed(s_time_label, t);
    }

    set_hidden(s_offline_banner, snap->link_up);
    set_hidden(s_netbusy_banner, !snap->net_busy || !snap->link_up);

    if (snap->toast[0]) {
        label_set_if_changed(s_toast, snap->toast);
        set_hidden(lv_obj_get_parent(s_toast), false);
    } else {
        set_hidden(lv_obj_get_parent(s_toast), true);
    }

    // ---- 页内容 ----
    show_page(snap->state);
    switch (snap->state) {
    case APP_ST_LISTENING: {
        // 1. 紧凑录音计时
        uint32_t sec = snap->elapsed_ms / 1000;
        char time_str[24];
        snprintf(time_str, sizeof(time_str), "REC %02u:%02u",
                 (unsigned)(sec / 60), (unsigned)(sec % 60));
        label_set_if_changed(s_pages[APP_ST_LISTENING].rec_elapsed, time_str);

        // 2. 5 柱跳动像素声波柱
        const uint16_t peak = audio_streamer_peak();
        uint32_t sq = (uint32_t)peak * (uint32_t)peak;
        uint8_t base_pct = (sq > 32768u * 100u) ? 100u : (uint8_t)(sq / 32768u);
        static const uint8_t weights[5] = { 45, 80, 100, 80, 45 };
        for (int i = 0; i < 5; i++) {
            if (s_pages[APP_ST_LISTENING].rec_bars[i]) {
                uint8_t val = (uint8_t)((base_pct * weights[i]) / 100);
                if (val < 10) val = 10;
                lv_bar_set_value(s_pages[APP_ST_LISTENING].rec_bars[i], val, LV_ANIM_OFF);
                uint32_t col = (val > 70) ? UI_YELLOW : UI_GRASS;
                lv_obj_set_style_bg_color(s_pages[APP_ST_LISTENING].rec_bars[i],
                                          lv_color_hex(col), LV_PART_INDICATOR);
            }
        }

        // 3. 实时转写
        const char *live = snap->agent_message;
        bool empty = (live == NULL || live[0] == '\0');
        if (empty) {
            label_set_if_changed(s_pages[APP_ST_LISTENING].live_text, "对准麦克风说话...");
            lv_obj_set_style_text_color(s_pages[APP_ST_LISTENING].live_text,
                                        lv_color_hex(UI_SUB), 0);
        } else {
            set_agent_message(s_pages[APP_ST_LISTENING].live_text, live,
                              !snap->transcript_final);
            lv_obj_set_style_text_color(s_pages[APP_ST_LISTENING].live_text,
                                        lv_color_hex(UI_TEXT), 0);
        }
        break;
    }
    case APP_ST_TRANSCRIBING:
        break;
    case APP_ST_AGENT_RUNNING: {
        const char *st = snap->agent_state_name;
        if (strcmp(st, s_last_chip) != 0) {
            snprintf(s_last_chip, sizeof(s_last_chip), "%s", st);
            uint32_t bg, fg;
            agent_chip_style(st, &bg, &fg);
            lv_obj_set_style_bg_color(s_pages[APP_ST_AGENT_RUNNING].run_chip,
                                      lv_color_hex(bg), 0);
            lv_obj_set_style_text_color(s_pages[APP_ST_AGENT_RUNNING].run_chip_label,
                                        lv_color_hex(fg), 0);
            label_set_if_changed(s_pages[APP_ST_AGENT_RUNNING].run_chip_label,
                                 agent_state_zh(st));
        }
        set_agent_message(s_pages[APP_ST_AGENT_RUNNING].run_message,
                          snap->agent_message, !snap->transcript_final);
        break;
    }
    case APP_ST_APPROVAL: {
        uint8_t r = snap->approval_risk < APP_RISK_COUNT ? snap->approval_risk : APP_RISK_MEDIUM;
        if ((int)r != s_last_risk) {
            s_last_risk = (int)r;
            lv_obj_set_style_bg_color(s_pages[APP_ST_APPROVAL].ap_risk_banner,
                                      lv_color_hex(RISK_COLORS[r]), 0);
            lv_obj_set_style_text_color(s_pages[APP_ST_APPROVAL].ap_risk_label,
                                        lv_color_hex(UI_ON_ACCENT), 0);
            label_set_if_changed(s_pages[APP_ST_APPROVAL].ap_risk_label, T_RISK[r]);
        }
        label_set_if_changed(s_pages[APP_ST_APPROVAL].ap_title, snap->approval_title);
        label_set_fmt_if_changed(s_pages[APP_ST_APPROVAL].ap_target, "目标:%s",
                                 snap->approval_target);
        label_set_if_changed(s_pages[APP_ST_APPROVAL].ap_diff, snap->approval_diff);
        break;
    }
    default:
        break;
    }
}
