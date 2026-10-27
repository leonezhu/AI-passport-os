# AI Passport OS 开发备忘（自用）

## 编译（本机 Mac）

```bash
cd ~/Documents/GitHub/AI-passport-os
env -u PYTHONPATH bash -c '
export PATH="/usr/local/bin:$PATH"   # 关键：idf.py 需要 python3.13（/usr/local/bin/python3.13.7），系统默认 3.14 的 venv 不存在
. "$HOME/esp/esp-idf/export.sh"
idf.py -B build build        # 日常开发构建
IDF_DIR="$HOME/esp/esp-idf" bash build.sh release   # 发布构建（build-public）
'
```

## 已踩坑

1. **idf.py: command not found**：export.sh 默认找 py3.14 的 venv（不存在）→ 必须 PATH 前置 /usr/local/bin 让 python3 指到 3.13.7
2. **esp_random/esp_fill_random 未声明**：用 `#include <esp_random.h>`（尖括号）；esp_random() 在部分头文件组合下未声明，优先 esp_fill_random（见 home_upload.cc 注释）
3. **PYTHONPATH 污染**：Hermes 环境的 PYTHONPATH 会破坏 idf 工具链，编译必须 env -u PYTHONPATH

## 新增 App 清单（照 apps/stopwatch 模板）

1. `main/apps/<name>/` 新建 app 代码（纯 C 逻辑层 + .cc UI 壳）
2. `main/apps/app_base.h` 枚举加 APP_ID_XXX
3. `main/launcher/app_registry.cc` 三处：extern 声明 / s_apps[] 数组 / feedback_allowed()
4. `main/CMakeLists.txt` 源文件列表
5. README 应用列表

## 已有游戏

- **熊猫爬竹竿**（apps/panda_climb/，2026-10-28）：诺基亚经典复刻。三杆自动爬升，
  UP=左切杆 DOWN=右切杆 OK=开始；障碍蛇/甲虫、道具竹笋(+5米)/星星(无敌3秒)；
  速度随高度递增；开机最佳记录。逻辑层 panda_core.c 纯 C 无 LVGL 依赖可单测；
  渲染走 LVGL draw 事件 + lv_draw_rect 色块（无 canvas/图片解码，复用 pixel_scene 思路）
