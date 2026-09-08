#include "apps/voice/app_voice.h"
#include "apps/voice/app_events.h"
#include "apps/voice/app_state.h"
#include "apps/voice/app_sound.h"
#include "apps/voice/app_ui.h"
#include "apps/voice/app_protocol.h"
#include "apps/voice/audio_streamer.h"
#include "apps/voice/ble_audio.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>

static const char *TAG = "app_voice";

namespace {

std::atomic<bool> s_voice_active{false};
TaskHandle_t s_voice_task_handle = nullptr;
app_state_t s_voice_state;
uint32_t s_last_drop_count = 0;

void run_voice_actions(const app_action_t *acts, uint8_t n) {
    for (uint8_t i = 0; i < n; i++) {
        const app_action_t *a = &acts[i];
        char buf[256];
        size_t len = 0;

        switch (a->type) {
        case APP_ACT_NONE:
        case APP_ACT_UI_REFRESH:
        case APP_ACT_UI_SCREEN_OFF:
        case APP_ACT_UI_SCREEN_ON:
        case APP_ACT_UI_PANEL_OFF:
        case APP_ACT_UI_PANEL_ON:
            break;
        case APP_ACT_SEND_VOICE_START:
            len = app_protocol_voice_start(buf, sizeof(buf), "ima_adpcm");
            ble_audio_notify_event_blocking(buf, len, 100);
            break;
        case APP_ACT_SEND_VOICE_END:
            audio_streamer_drain(500);
            s_last_drop_count = audio_streamer_take_drops();
            len = app_protocol_voice_end(buf, sizeof(buf));
            ble_audio_notify_event_blocking(buf, len, 100);
            len = app_protocol_device_status(buf, sizeof(buf), s_last_drop_count);
            ble_audio_notify_event(buf, len);
            break;
        case APP_ACT_SEND_KEY_ACTION:
            len = app_protocol_key_action(buf, sizeof(buf),
                                          (app_key_action_t)a->u.key_action.action);
            ble_audio_notify_event(buf, len);
            break;
        case APP_ACT_SEND_AGENT_ACTION:
            len = app_protocol_agent_action(buf, sizeof(buf),
                                            a->u.agent_action.task_id,
                                            a->u.agent_action.decision);
            ble_audio_notify_event(buf, len);
            break;
        case APP_ACT_STREAM_START:
            s_last_drop_count = 0;
            audio_streamer_set_sender(ble_audio_notify_audio);
            audio_streamer_set_compressed(true);
            if (audio_streamer_start() != ESP_OK) {
                app_event_t ev = { .type = APP_EV_AUDIO_ERROR };
                app_event_post(&ev);
            }
            break;
        case APP_ACT_STREAM_STOP:
            audio_streamer_stop();
            break;
        case APP_ACT_STREAM_CANCEL:
            audio_streamer_cancel();
            break;
        case APP_ACT_PLAY_TONE:
            app_sound_play((app_tone_t)a->u.tone.id);
            break;
        case APP_ACT_BLE_DISCONNECT:
            ble_audio_disconnect();
            break;
        default:
            break;
        }
    }
}

void voice_worker_task(void *arg) {
    ESP_LOGI(TAG, "Voice event dispatch task started");
    app_action_t acts[8];
    uint8_t act_n = 0;

    while (s_voice_active.load()) {
        app_event_t ev;
        if (app_event_poll(&ev, pdMS_TO_TICKS(50))) {
            uint64_t now_ms = esp_timer_get_time() / 1000;
            app_state_reduce(&s_voice_state, &ev, now_ms, acts, &act_n);
            if (act_n > 0) {
                run_voice_actions(acts, act_n);
            }
        }

        // 渲染状态页快照
        uint64_t now_ms = esp_timer_get_time() / 1000;
        app_ui_snapshot_t snap;
        app_state_snapshot(&s_voice_state, now_ms, &snap);

        bsp_lvgl_lock(-1);
        app_ui_render(&snap);
        bsp_lvgl_unlock();
    }

    ESP_LOGI(TAG, "Voice event dispatch task exiting");
    s_voice_task_handle = nullptr;
    vTaskDelete(NULL);
}

} // namespace

void voice_app_init(void) {
    ESP_LOGI(TAG, "Initializing Voice App components...");
    app_events_init();
    app_sound_init();
    app_state_init(&s_voice_state);
    app_ui_init();
}

void voice_app_start(void) {
    ESP_LOGI(TAG, "Starting Voice App...");
    s_voice_active.store(true);

    // 1. 启动 BLE 音频/事件服务广播 "AI Passport"
    ble_audio_init();

    // 2. 初始化音频流管线 (静态4KB环)
    audio_streamer_init();

    // 3. 启动主状态事件消费任务
    if (s_voice_task_handle == nullptr) {
        xTaskCreate(voice_worker_task, "voice_task", 4096, NULL, 5, &s_voice_task_handle);
    }
}

void voice_app_stop(void) {
    ESP_LOGI(TAG, "Stopping Voice App and releasing BLE/Audio streamer...");
    s_voice_active.store(false);

    // 1. 取消音频流
    audio_streamer_cancel();

    // 2. 断开并注销 BLE
    ble_audio_disconnect();

    // 3. 等待主任务退出
    while (s_voice_task_handle != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "Voice App cleanly stopped. Free heap: %u KB",
             (unsigned)(esp_get_free_heap_size() / 1024));
}

void voice_app_on_key(uint8_t btn, app_btn_event_t event, uint16_t mv) {
    app_event_t ev = {};
    ev.key.btn = btn;
    ev.key.mv = mv;

    switch (event) {
    case BTN_EVT_PRESS:
        ev.type = APP_EV_KEY_PRESS;
        break;
    case BTN_EVT_RELEASE:
        ev.type = APP_EV_KEY_RELEASE;
        break;
    case BTN_EVT_CLICK:
        ev.type = APP_EV_KEY_CLICK;
        break;
    case BTN_EVT_LONG:
        ev.type = APP_EV_KEY_LONG;
        break;
    case BTN_EVT_LONG_UP:
        ev.type = APP_EV_KEY_LONG_UP;
        break;
    default:
        return;
    }

    app_event_post(&ev);
}

const passport_app_t g_voice_app = {
    .id = APP_ID_VOICE,
    .name = "随身 AI 语音",
    .en_name = "VOICE",
    .desc = "按住说话 · 微信式桌面悬浮窗 · 物理审批手刹",
    .tag = "BLE / USB",
    .theme_color = 0x4ED39A,
    .init = voice_app_init,
    .start = voice_app_start,
    .stop = voice_app_stop,
    .on_key = voice_app_on_key,
};
