#include "apps/radio/app_radio.h"
#include "apps/radio/radio_ui.h"
#include "apps/radio/radio_player.h"
#include "apps/radio/radio_catalog.h"
#include "wifi_manager.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include <atomic>

static const char *TAG = "app_radio";

namespace {

std::atomic<bool> s_radio_active{false};
std::atomic<bool> s_wifi_connected{false};
TaskHandle_t s_catalog_task = nullptr;

void catalog_worker(void *arg) {
    ESP_LOGI(TAG, "Starting IP-based location and station catalog search...");
    RadioLocation loc = {};
    if (locate(&loc)) {
        ESP_LOGI(TAG, "Location detected: %s (%s)", loc.city, loc.region);
        radio_ui_set_location(loc.city);
        RadioStation stations[10];
        std::size_t found = 0;
        if (query_directory(loc.city, stations, 10, &found) && found > 0) {
            ESP_LOGI(TAG, "Found %u stations for %s", (unsigned)found, loc.city);
            radio_player_replace_stations_resuming(stations, found);
        }
    }
    s_catalog_task = nullptr;
    vTaskDelete(NULL);
}

void on_wifi_event(WifiEvent event, const std::string &data) {
    if (!s_radio_active.load()) return;

    if (event == WifiEvent::StationConnected) {
        ESP_LOGI(TAG, "Wi-Fi connected: %s", data.c_str());
        s_wifi_connected.store(true);
        radio_ui_set_network(true, ("已连接: " + data).c_str());
        radio_player_set_network(true);

        if (s_catalog_task == nullptr) {
            xTaskCreate(catalog_worker, "radio_cat", 4096, NULL, 5, &s_catalog_task);
        }
    } else if (event == WifiEvent::StationDisconnected) {
        ESP_LOGI(TAG, "Wi-Fi disconnected: %s", data.c_str());
        s_wifi_connected.store(false);
        radio_ui_set_network(false, "Wi-Fi已断开");
        radio_player_set_network(false);
    }
}

} // namespace

void radio_app_init(void) {
    ESP_LOGI(TAG, "Initializing Radio App subsystems...");
    radio_ui_init();
    radio_player_init();
}

void radio_app_start(void) {
    ESP_LOGI(TAG, "Starting Radio App...");
    s_radio_active.store(true);

    // 1. 显示 Radio UI 界面
    radio_ui_show_main();

    // 2. 初始化并连接 Wi-Fi
    WifiManagerConfig cfg = {};
    cfg.auto_reconnect = true;
    cfg.reconnect_interval_ms = 5000;
    cfg.max_reconnect_attempts = 10;
    cfg.enable_ap_fallback = true;
    cfg.ap_channel = 1;
    cfg.ap_max_connections = 4;
    cfg.ap_ssid_prefix = "LEO Radio";
    cfg.ap_ip = "192.168.4.1";

    WifiManager::GetInstance().Initialize(cfg);
    WifiManager::GetInstance().SetEventCallback(on_wifi_event);
    WifiManager::GetInstance().StartStation();

    // 3. 开始播放
    radio_player_set_playing(true);
}

void radio_app_stop(void) {
    ESP_LOGI(TAG, "Stopping Radio App and releasing Wi-Fi/Audio memory...");
    s_radio_active.store(false);

    // 1. 停止播放并等待拉流线程退出
    radio_player_set_playing(false);
    radio_player_wait_idle(1000);

    // 2. 终止目录解析任务
    if (s_catalog_task != nullptr) {
        vTaskDelete(s_catalog_task);
        s_catalog_task = nullptr;
    }

    // 3. 彻底注销 Wi-Fi 协议栈，释放 ~45KB 堆内存
    WifiManager::GetInstance().Disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();

    ESP_LOGI(TAG, "Radio App cleanly stopped. Free heap: %u KB",
             (unsigned)(esp_get_free_heap_size() / 1024));
}

void radio_app_on_key(uint8_t btn, app_btn_event_t event, uint16_t mv) {
    (void)mv;
    if (event != BTN_EVT_CLICK) return;

    if (btn == BSP_BTN_UP) {
        // 上一电台
        radio_player_select_relative(-1);
    } else if (btn == BSP_BTN_DOWN) {
        // 下一电台
        radio_player_select_relative(1);
    } else if (btn == BSP_BTN_OK) {
        // 单击播放 / 暂停切换
        radio_player_toggle();
    }
}

const passport_app_t g_radio_app = {
    .id = APP_ID_RADIO,
    .name = "城市收音机",
    .en_name = "RADIO",
    .desc = "自动定位城市 · 真实FM刻度扫频 · 18段动态频谱",
    .tag = "Wi-Fi / FM",
    .theme_color = 0xFFB74D,
    .init = radio_app_init,
    .start = radio_app_start,
    .stop = radio_app_stop,
    .on_key = radio_app_on_key,
};
