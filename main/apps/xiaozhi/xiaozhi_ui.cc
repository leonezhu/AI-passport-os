// Xiaozhi: shared terminal conversation view and bounded UTF-8 replies.
#include "apps/xiaozhi/xiaozhi_ui.h"
#include "bsp_display.h"
#include "ui_pixel.h"
#include "utf8_tail.h"
#include "app_fonts.h"
#include "wifi_manager.h"
#include "lvgl.h"
#include <esp_log.h>
#include <cstdio>

namespace xiaozhi {

namespace {

// 状态点颜色:与整机状态色语义一致(墨/纸/草/黄/红/天蓝)
constexpr uint32_t kTextMain   = UI_TEXT;
constexpr uint32_t kTextMuted  = UI_SUB;
constexpr uint32_t kUserBlue   = UI_SKY_DARK;
constexpr uint32_t kGreenAccent = UI_GRASS_DARK;
constexpr uint32_t kAmberAccent = UI_ORANGE;
constexpr uint32_t kRedAccent   = UI_RED;

lv_obj_t *s_screen = nullptr;
lv_obj_t *s_status_badge = nullptr;
lv_obj_t *s_status_label = nullptr;
lv_obj_t *s_msg_container = nullptr;
lv_obj_t *s_user_text_label = nullptr;
lv_obj_t *s_ai_text_label = nullptr;
lv_obj_t *s_hint_label = nullptr;

// 文本内容也只在持锁时读写，避免与 lv_label 生命周期脱节。
std::string s_user_text;
std::string s_ai_reply_buffer;

constexpr size_t kMaxTextBytes = 1200;  // up to 400 three-byte Han characters

// 滚动到消息区底部，保证最新一轮对话可见
void scroll_to_bottom_locked() {
    if (s_msg_container) {
        lv_obj_scroll_to_y(s_msg_container, LV_COORD_MAX, LV_ANIM_OFF);
    }
}

// 所有"改文案"的操作都走这里：先加锁、再加一次指针校验。
// 返回 false 表示控件已销毁（应用正在退出），调用方静默丢弃即可。
bool begin_update(uint32_t timeout_ms) {
    if (!bsp_lvgl_lock(timeout_ms)) return false;
    if (s_screen == nullptr) {                      // 加锁期间被 ui_hide 销毁
        bsp_lvgl_unlock();
        return false;
    }
    return true;
}

} // namespace

void ui_init() {
    if (!bsp_lvgl_lock(1000)) return;

    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = nullptr;
    }
    s_status_badge = nullptr;
    s_status_label = nullptr;
    s_msg_container = nullptr;
    s_user_text_label = nullptr;
    s_ai_text_label = nullptr;
    s_hint_label = nullptr;
    s_user_text.clear();
    s_ai_reply_buffer.clear();

    s_screen = lv_obj_create(nullptr);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    ui_pixel_background(s_screen);

    // 1. 标题牌 + 状态胶囊(纸底墨边,与整机一致)
    ui_pixel_title_plate(s_screen, 10, "小智AI", &ui_font_20);

    lv_obj_t *top_bar = lv_obj_create(s_screen);
    lv_obj_set_size(top_bar, 216, 30);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(top_bar, 0, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_border_color(top_bar, lv_color_hex(UI_BORDER), 0);
    lv_obj_set_style_pad_hor(top_bar, 10, 0);
    lv_obj_set_style_pad_ver(top_bar, 0, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_status_badge = lv_obj_create(top_bar);
    lv_obj_set_size(s_status_badge, 9, 9);
    lv_obj_align(s_status_badge, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(s_status_badge, 0, 0);
    lv_obj_set_style_bg_color(s_status_badge, lv_color_hex(kAmberAccent), 0);
    lv_obj_set_style_border_width(s_status_badge, 0, 0);

    s_status_label = lv_label_create(top_bar);
    lv_obj_set_style_text_font(s_status_label, &buddy_font_16, 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(kTextMain), 0);
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(s_status_label, 172);
    lv_label_set_text(s_status_label, "准备中...");
    lv_obj_align(s_status_label, LV_ALIGN_LEFT_MID, 15, 0);

    // 2. 可滚动的消息区（长回复不再被裁掉）——纸面板+墨描边+硬阴影
    s_msg_container = lv_obj_create(s_screen);
    lv_obj_set_size(s_msg_container, 216, 186);
    lv_obj_set_pos(s_msg_container, 12, 88);
    lv_obj_set_style_bg_color(s_msg_container, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(s_msg_container, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_msg_container, 0, 0);
    lv_obj_set_style_border_width(s_msg_container, 1, 0);
    lv_obj_set_style_border_side(s_msg_container, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(s_msg_container, lv_color_hex(UI_BORDER), 0);
    lv_obj_set_style_pad_all(s_msg_container, 8, 0);
    lv_obj_set_style_pad_row(s_msg_container, 12, 0);
    lv_obj_set_style_text_line_space(s_msg_container, 4, 0);
    lv_obj_set_flex_flow(s_msg_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_msg_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(s_msg_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_msg_container, LV_SCROLLBAR_MODE_AUTO);

    s_user_text_label = lv_label_create(s_msg_container);
    lv_obj_set_width(s_user_text_label, 196);
    lv_obj_set_style_text_font(s_user_text_label, &buddy_font_16, 0);
    lv_obj_set_style_text_color(s_user_text_label, lv_color_hex(kUserBlue), 0);
    lv_label_set_long_mode(s_user_text_label, LV_LABEL_LONG_WRAP);
    s_user_text = "你说: ...";
    lv_label_set_text(s_user_text_label, s_user_text.c_str());

    s_ai_text_label = lv_label_create(s_msg_container);
    lv_obj_set_width(s_ai_text_label, 196);
    lv_obj_set_style_text_font(s_ai_text_label, &buddy_font_16, 0);
    lv_obj_set_style_text_color(s_ai_text_label, lv_color_hex(kTextMain), 0);
    lv_label_set_long_mode(s_ai_text_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_ai_text_label, "小智: 短按 OK 说话，再按一下结束。");

    // 3. 底部按键指引(共享胶囊样式)
    s_hint_label = ui_pixel_hint_pill(s_screen, "OK:说话/打断 长按:返回", &buddy_font_16);

    bsp_lvgl_unlock();
}

void ui_show() {
    if (!bsp_lvgl_lock(1000)) return;
    if (s_screen == nullptr) {
        bsp_lvgl_unlock();
        ui_init();
        if (!bsp_lvgl_lock(1000)) return;
    }
    if (s_screen) lv_screen_load(s_screen);
    bsp_lvgl_unlock();
}

void ui_hide() {
    if (!bsp_lvgl_lock(1000)) return;
    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = nullptr;
        s_status_badge = nullptr;
        s_status_label = nullptr;
        s_msg_container = nullptr;
        s_user_text_label = nullptr;
        s_ai_text_label = nullptr;
        s_hint_label = nullptr;
    }
    s_user_text.clear();
    s_ai_reply_buffer.clear();
    bsp_lvgl_unlock();
}

void ui_set_state(State state) {
    if (!begin_update(200)) return;

    switch (state) {
    case State::Disconnected:
        lv_obj_set_style_bg_color(s_status_badge, lv_color_hex(kRedAccent), 0);
        if (!WifiManager::GetInstance().IsConnected()) {
            lv_label_set_text(s_status_label, "Wi-Fi 未连接(请配网)");
        } else {
            lv_label_set_text(s_status_label, "未连接小智(按OK重试)");
        }
        break;
    case State::Connecting:
        lv_obj_set_style_bg_color(s_status_badge, lv_color_hex(kAmberAccent), 0);
        lv_label_set_text(s_status_label, "连接小智服务中...");
        break;
    case State::Idle:
        lv_obj_set_style_bg_color(s_status_badge, lv_color_hex(kGreenAccent), 0);
        lv_label_set_text(s_status_label, "小智待命中(按OK说话)");
        break;
    case State::Listening:
        lv_obj_set_style_bg_color(s_status_badge, lv_color_hex(kUserBlue), 0);
        lv_label_set_text(s_status_label, "正在聆听中...");
        break;
    case State::Thinking:
        lv_obj_set_style_bg_color(s_status_badge, lv_color_hex(kAmberAccent), 0);
        lv_label_set_text(s_status_label, "小智思考中...");
        break;
    case State::Speaking:
        lv_obj_set_style_bg_color(s_status_badge, lv_color_hex(kGreenAccent), 0);
        lv_label_set_text(s_status_label, "小智回复中(OK打断)");
        break;
    case State::Error:
        lv_obj_set_style_bg_color(s_status_badge, lv_color_hex(kRedAccent), 0);
        lv_label_set_text(s_status_label, "服务异常(按OK重试)");
        break;
    }

    bsp_lvgl_unlock();
}

void ui_set_status_text(const std::string &text) {
    if (!begin_update(200)) return;
    lv_label_set_text(s_status_label, text.c_str());
    bsp_lvgl_unlock();
}

void ui_set_user_text(const std::string &text) {
    if (!begin_update(200)) return;
    s_user_text = "你说: ";
    passport::append_utf8_tail(s_user_text, text, kMaxTextBytes);
    lv_label_set_text(s_user_text_label, s_user_text.c_str());
    scroll_to_bottom_locked();
    bsp_lvgl_unlock();
}

void ui_set_ai_text(const std::string &text) {
    if (!begin_update(200)) return;
    const auto tail = passport::utf8_tail(text, kMaxTextBytes);
    s_ai_reply_buffer.assign(tail.data(), tail.size());
    std::string display = "小智: " + s_ai_reply_buffer;
    lv_label_set_text(s_ai_text_label, display.c_str());
    scroll_to_bottom_locked();
    bsp_lvgl_unlock();
}

void ui_append_ai_text(const std::string &text) {
    if (!begin_update(200)) return;

    passport::append_utf8_tail(s_ai_reply_buffer, text, kMaxTextBytes);

    std::string display = "小智: " + s_ai_reply_buffer;
    lv_label_set_text(s_ai_text_label, display.c_str());
    scroll_to_bottom_locked();
    bsp_lvgl_unlock();
}

void ui_set_hint(const std::string &text) {
    if (!begin_update(200)) return;
    lv_label_set_text(s_hint_label, text.c_str());
    bsp_lvgl_unlock();
}

void ui_clear_text() {
    if (!begin_update(200)) return;
    s_ai_reply_buffer.clear();
    s_user_text = "你说: ...";
    lv_label_set_text(s_user_text_label, s_user_text.c_str());
    lv_label_set_text(s_ai_text_label, "小智: ...");
    bsp_lvgl_unlock();
}

} // namespace xiaozhi
