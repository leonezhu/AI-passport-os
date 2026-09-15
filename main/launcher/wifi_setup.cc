#include "launcher/wifi_setup.h"
#include "launcher/app_registry.h"
#include "apps/radio/radio_ui.h"
#include "wifi_manager.h"
#include "wifi_station.h"
#include "ssid_manager.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <string>

static const char *TAG = "wifi_setup";

namespace {

enum class SetupState {
    List,
    Keyboard,
    Connecting,
};

constexpr std::size_t kMaxVisibleNetworks = 16;

std::atomic<SetupState> s_state{SetupState::List};
std::atomic<bool> s_active{false};
std::recursive_mutex s_mutex;

std::array<WifiNetworkView, kMaxVisibleNetworks> s_networks = {};
std::size_t s_network_count = 0;
std::size_t s_network_selected = 0;

std::string s_selected_ssid;
std::string s_password;
WifiKeyboardPage s_kb_page = WifiKeyboardPage::Lower;
std::size_t s_key_selected = 0;

// 任务句柄改为原子:原来由 worker 写 NULL、由 start/stop 读,是典型的多任务
// 裸共享(见审查 A2/D3)。用 atomic 消除"读到半截指针"的可能。
std::atomic<TaskHandle_t> s_scan_task{nullptr};
std::atomic<TaskHandle_t> s_connect_task{nullptr};

// UI 存活开关:stop() 里先置 false,worker 再晚一步醒来时也不会去碰已经
// 隐藏的 radio_ui(否则会出现"退出后还往已销毁界面写文本")。
std::atomic<bool> s_ui_alive{false};

void update_list_display(bool scanning) {
    if (!s_ui_alive.load()) return;
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    radio_ui_show_wifi_list(s_networks.data(), s_network_count,
                            s_network_selected, scanning,
                            "Passport-WiFi", true);
}

void update_keyboard_display(const char *status = nullptr) {
    if (!s_ui_alive.load()) return;
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    radio_ui_show_wifi_keyboard(s_selected_ssid.c_str(), s_password.size(),
                                s_kb_page, s_key_selected, status);
}

void scan_worker(void *arg) {
    (void)arg;
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // creator has published the handle
    ESP_LOGI(TAG, "Starting Wi-Fi scan worker...");
    // 触发一次即时 Wi-Fi 扫描
    esp_wifi_scan_start(nullptr, false);

    for (int attempt = 0; attempt < 15 && s_active.load(); ++attempt) {
        auto aps = WifiManager::GetInstance().GetConfigAccessPoints();
        if (!aps.empty()) {
            std::sort(aps.begin(), aps.end(), [](const wifi_ap_record_t &a, const wifi_ap_record_t &b) {
                return a.rssi > b.rssi;
            });

            std::array<WifiNetworkView, kMaxVisibleNetworks> views = {};
            std::size_t count = 0;
            for (const auto &ap : aps) {
                const char *ssid = reinterpret_cast<const char *>(ap.ssid);
                if (!ssid[0]) continue;
                bool dup = false;
                for (std::size_t i = 0; i < count; ++i) {
                    if (std::strncmp(views[i].ssid, ssid, sizeof(views[i].ssid)) == 0) {
                        dup = true;
                        break;
                    }
                }
                if (dup || count >= views.size()) continue;

                std::snprintf(views[count].ssid, sizeof(views[count].ssid), "%.*s", 32, ssid);
                views[count].rssi = ap.rssi;
                views[count].secure = ap.authmode != WIFI_AUTH_OPEN;
                ++count;
            }

            if (count > 0) {
                {
                    std::lock_guard<std::recursive_mutex> lock(s_mutex);
                    s_networks = views;
                    s_network_count = count;
                    if (s_network_selected >= count) s_network_selected = 0;
                }
                update_list_display(false);
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(600));
    }

    if (s_active.load()) {
        update_list_display(false);
    }
    s_scan_task = nullptr;
    vTaskDelete(NULL);
}

void start_scan() {
    if (s_scan_task.load() != nullptr) return;
    update_list_display(true);
    // xTaskCreate 要的是 TaskHandle_t*,原子变量不能直接取址 —— 先落本地再存,
    // 避免"任务已跑起来但句柄还没写回"的窗口。
    TaskHandle_t h = nullptr;
    if (xTaskCreate(scan_worker, "wf_scan", 3584, NULL, 4, &h) != pdPASS) {
        ESP_LOGW(TAG, "scan task create failed");
        update_list_display(false);
        return;
    }
    s_scan_task.store(h);
    xTaskNotifyGive(h);
}

void connect_worker(void *arg) {
    (void)arg;
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // creator has published the handle
    std::string ssid;
    std::string pass;
    {
        std::lock_guard<std::recursive_mutex> lock(s_mutex);
        ssid = s_selected_ssid;
        pass = s_password;
    }

    ESP_LOGI(TAG, "Connecting to SSID: %s...", ssid.c_str());
    bool ok = WifiManager::GetInstance().ConfigureNetwork(ssid, pass);
    if (!s_active.load()) {
        s_connect_task = nullptr;
        vTaskDelete(NULL);
        return;
    }

    if (ok) {
        ESP_LOGI(TAG, "Wi-Fi connected successfully!");
        if (s_ui_alive.load()) radio_ui_show_wifi_connecting(ssid.c_str(), "连接成功！");
        vTaskDelay(pdMS_TO_TICKS(1000));
        WifiManager::GetInstance().StopConfigAp();
        s_connect_task = nullptr;
        if (s_active.load() && app_registry_get_active() == APP_ID_WIFI)
            app_registry_switch_to(APP_ID_LAUNCHER);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGW(TAG, "Wi-Fi connection failed.");
    {
        std::lock_guard<std::recursive_mutex> lock(s_mutex);
        s_state = SetupState::Keyboard;
    }
    update_keyboard_display("连接失败，请重试");
    s_connect_task = nullptr;
    vTaskDelete(NULL);
}

void start_connect() {
    if (s_connect_task.load() != nullptr) return;
    std::string ssid;
    {
        std::lock_guard<std::recursive_mutex> lock(s_mutex);
        if (s_selected_ssid.empty()) return;
        s_state = SetupState::Connecting;
        ssid = s_selected_ssid;
    }
    if (s_ui_alive.load()) radio_ui_show_wifi_connecting(ssid.c_str(), "正在连接网络...");
    TaskHandle_t h = nullptr;
    if (xTaskCreate(connect_worker, "wf_conn", 4096, NULL, 5, &h) != pdPASS) {
        ESP_LOGW(TAG, "connect task create failed");
        s_state = SetupState::Keyboard;
        update_keyboard_display("内存不足，请重试");
        return;
    }
    s_connect_task.store(h);
    xTaskNotifyGive(h);
}

} // namespace

void wifi_setup_init(void) {
    ESP_LOGI(TAG, "Initializing Wi-Fi setup UI subsystems...");
    radio_ui_init();
}

void wifi_setup_start(void) {
    ESP_LOGI(TAG, "Starting Wi-Fi setup app...");
    s_active.store(true);
    s_ui_alive.store(true);
    s_state = SetupState::List;

    // 1. 初始化并启动 Web AP 门户 "Passport-WiFi" (192.168.4.1)
    WifiManagerConfig cfg = {};
    cfg.ssid_prefix = "Passport-WiFi";
    cfg.language = "zh-CN";
    if (!WifiManager::GetInstance().Initialize(cfg)) {
        ESP_LOGE(TAG, "WifiManager initialize failed (out of memory?)");
        update_list_display(false);
        return;
    }
    WifiManager::GetInstance().StartConfigAp();

    // 2. 启动热点扫描与列表展示
    start_scan();
}

void wifi_setup_stop(void) {
    ESP_LOGI(TAG, "Stopping Wi-Fi setup app...");
    s_active.store(false);

    // 2026-09-13 修复(A1):这里原来是"等 750ms 后 vTaskDelete 强杀"。
    // scan_worker 最长要跑 15×600ms=9s,几乎必然在持着 LVGL 递归锁时被杀,
    // 锁永不释放 → 此后所有任务的 UI 调用永久阻塞 → 整机冻死只能断电。
    // 现在的做法是:先关 UI 开关、置 s_active,然后**有上界地等任务自己退出**
    // (两个 worker 的循环都会检查 s_active),超时只记日志,绝不 vTaskDelete。
    s_ui_alive.store(false);
    TaskHandle_t cur = xTaskGetCurrentTaskHandle();
    int wait_cycles = 0;
    while ((s_scan_task.load() != nullptr ||
            (s_connect_task.load() != nullptr && s_connect_task.load() != cur)) &&
           wait_cycles < 100) {          // 100 × 50ms = 5s 上界
        vTaskDelay(pdMS_TO_TICKS(50));
        wait_cycles++;
    }
    if (s_scan_task.load() != nullptr ||
        (s_connect_task.load() != nullptr && s_connect_task.load() != cur)) {
        // 仍存活:不删它(可能正持有 LVGL 锁),只告警。它一旦走到 s_active
        // 检查点就会自行退出;s_ui_alive 已为 false,不会再碰已隐藏的界面。
        ESP_LOGW(TAG, "worker still running after 5s; left to exit by itself (no vTaskDelete)");
    }

    WifiManager::GetInstance().StopConfigAp();
    if (!SsidManager::GetInstance().GetSsidList().empty() && !WifiManager::GetInstance().IsConnected()) {
        ESP_LOGI(TAG, "Resuming station mode after exiting Wi-Fi setup...");
        WifiManager::GetInstance().StartStation();
    }
    radio_ui_hide();
    ESP_LOGI(TAG, "Wi-Fi setup stopped cleanly.");
}

void wifi_setup_on_key(uint8_t btn, app_btn_event_t event, uint16_t mv) {
    (void)mv;

    if (s_state == SetupState::Connecting) return;

    if (s_state == SetupState::List) {
        if (event == BTN_EVT_CLICK) {
            if (btn == BSP_BTN_UP) {
                // "+" 往前选择下一个热点
                std::lock_guard<std::recursive_mutex> lock(s_mutex);
                if (s_network_count > 0) {
                    s_network_selected = (s_network_selected + 1) % s_network_count;
                }
                update_list_display(false);
            } else if (btn == BSP_BTN_DOWN) {
                // "-" 往后选择上一个热点
                std::lock_guard<std::recursive_mutex> lock(s_mutex);
                if (s_network_count > 0) {
                    s_network_selected = (s_network_selected + s_network_count - 1) % s_network_count;
                }
                update_list_display(false);
            } else if (btn == BSP_BTN_OK) {
                std::lock_guard<std::recursive_mutex> lock(s_mutex);
                if (s_network_count > 0) {
                    s_selected_ssid = s_networks[s_network_selected].ssid;
                    s_password.clear();
                    s_kb_page = WifiKeyboardPage::Lower;
                    s_key_selected = 0;
                    s_state = SetupState::Keyboard;
                    ESP_LOGI(TAG, "Selected SSID '%s' (row %u), opening keyboard...",
                             s_selected_ssid.c_str(), (unsigned)s_network_selected);
                    update_keyboard_display(nullptr);
                }
            }
        } else if (event == BTN_EVT_LONG && btn == BSP_BTN_OK) {
            start_scan();
        }
        return;
    }

    if (s_state == SetupState::Keyboard) {
        // 2026-09-13 修复(A5):整个键盘分支都在改 s_password / s_state / s_kb_page,
        // 而 connect_worker 会持同一把锁读 s_selected_ssid / s_password。
        // std::string 的并发读写会直接破坏堆(表现为随机重启),所以这里必须加锁。
        // s_mutex 是 recursive_mutex,与 update_keyboard_display/start_connect
        // 内部的加锁嵌套是安全的。
        std::lock_guard<std::recursive_mutex> lock(s_mutex);
        const std::size_t count = radio_ui_wifi_key_count(s_kb_page);
        if (count == 0) return;

        if (event == BTN_EVT_CLICK) {
            if (btn == BSP_BTN_UP) {
                // "+" 往前跳一格
                s_key_selected = (s_key_selected + 1) % count;
                update_keyboard_display(nullptr);
            } else if (btn == BSP_BTN_DOWN) {
                // "-" 往后跳一格
                s_key_selected = (s_key_selected + count - 1) % count;
                update_keyboard_display(nullptr);
            } else if (btn == BSP_BTN_OK) {
                const char *key = radio_ui_wifi_key(s_kb_page, s_key_selected);
                if (std::strcmp(key, "ABC") == 0) {
                    s_kb_page = WifiKeyboardPage::Upper;
                    s_key_selected = 0;
                    update_keyboard_display(nullptr);
                } else if (std::strcmp(key, "abc") == 0) {
                    s_kb_page = WifiKeyboardPage::Lower;
                    s_key_selected = 0;
                    update_keyboard_display(nullptr);
                } else if (std::strcmp(key, "123") == 0) {
                    s_kb_page = WifiKeyboardPage::Symbols;
                    s_key_selected = 0;
                    update_keyboard_display(nullptr);
                } else if (std::strcmp(key, "<") == 0) {
                    if (!s_password.empty()) {
                        s_password.pop_back();
                        update_keyboard_display(nullptr);
                    } else {
                        // 密码已空时再次按退格，返回热点列表
                        s_state = SetupState::List;
                        update_list_display(false);
                    }
                } else if (std::strcmp(key, "GO") == 0) {
                    start_connect();
                } else if (s_password.size() < 63) {
                    s_password += key;
                    update_keyboard_display(nullptr);
                }
            }
        } else if (event == BTN_EVT_LONG) {
            if (btn == BSP_BTN_UP) {
                // "+" 长按：跨行向前跳跃 6 个按键
                s_key_selected = (s_key_selected + std::min<std::size_t>(6, count)) % count;
                update_keyboard_display(nullptr);
            } else if (btn == BSP_BTN_DOWN) {
                // "-" 长按：跨行向后跳跃 6 个按键
                s_key_selected = (s_key_selected + count - std::min<std::size_t>(6, count)) % count;
                update_keyboard_display(nullptr);
            } else if (btn == BSP_BTN_OK) {
                // 长按 OK 由全局拦截返回桌面 Launcher
            }
        }
    }
}

const passport_app_t g_wifi_app = {
    .id = APP_ID_WIFI,
    .name = "无线网络",
    .en_name = "WLAN SETUP",
    .desc = "扫描附近热点 · 小键盘输入密码 · 支持手机配网",
    .tag = "Wi-Fi / AP",
    .theme_color = 0x4ED39A,
    .init = wifi_setup_init,
    .start = wifi_setup_start,
    .stop = wifi_setup_stop,
    .on_key = wifi_setup_on_key,
};
