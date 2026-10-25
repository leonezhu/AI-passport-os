# 开发指南备忘（防忘）

> 官方仓库 [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport) 有完整的开发文档体系。本文件提炼要点并留好入口，忘了怎么开发就看这里，再不够就去读官方原文。

## 官方文档地图

| 想知道什么 | 去哪看（官方仓库内） |
| --- | --- |
| AI 编程助手如何在本仓库工作 | `docs/development/ai-guide.zh_CN.md` |
| 构建、测试、验证命令 | `docs/development/engineering/build-and-test.zh_CN.md` |
| 代码风格 / 中文字体 / LVGL | `docs/development/engineering/coding-conventions.zh_CN.md`、`lvgl-chinese-fonts.zh_CN.md` |
| 分区布局与合并产物 | `docs/development/engineering/firmware-layout.zh_CN.md` |
| 硬件事实（引脚/外设/排障） | `docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md` |
| Wi-Fi 蓝牙配网参考 | `docs/development/engineering/wifi-provisioning.zh_CN.md` |
| fork 工作流（分支模型） | `docs/fork-guide.zh_CN.md` |
| 分阶段开发技能（含构建/调试/真机测试） | `skills/passport-develop/SKILL.zh_CN.md` 等 |

一键抓取官方文档更新：

```bash
# 例如更新 ai-guide
curl -sL https://raw.githubusercontent.com/FoloToy/ai-passport/main/docs/development/ai-guide.zh_CN.md -o /tmp/ai-guide.md
```

## 硬件事实（事实来源优先级）

冲突时按此优先级取真相：

```
产品规格 / 实机测量
  > components/bsp/include/bsp_pins.h   ← 引脚与硬件参数唯一权威
  > BSP 公开头文件与实现
  > 官方硬件开发指南
  > README 与示例应用
```

本板：ESP32-C3 / 8MB Flash / 无 PSRAM / ST7789P3 240×320 SPI 屏 / 三键（GPIO0 ADC 分压）/ ES8311 I2S 音频 / CW2017 电量计。**不要拿别的 ESP32-C3 板的参数猜本板**——查 `bsp_pins.h` 或问人。

## 应用与 BSP 的边界

```
main/                        应用层：页面、状态机、动画、任务
  └─ components/bsp/include/  稳定板级 API（应用只 import 这层）
      └─ components/bsp/src/  GPIO、总线、驱动细节（一般不动）
          └─ bsp_pins.h       引脚唯一事实源
```

新应用接入点（在本仓库已验证过的路径）：

1. `main/apps/app_base.h` 加 `APP_ID` 枚举
2. `main/launcher/app_registry.cc` 注册（extern + 注册项 + heap_need + power_save + heavy_app 白名单）
3. `main/launcher/launcher.cc` 图标与主题色
4. `main/apps/<你的应用>/` 实现应用（参照现有 6 款，如 `stopwatch` 最简单）
5. `main/CMakeLists.txt` 加源文件
6. 删应用则反向执行全部 6 步 + 配网页 + 串口命令 + verify 校验（Muse 移除踩过的 7 处清单）

## 硬约束（官方规约，本仓库同样适用）

- **禁止给离线需求擅自加 Wi-Fi、云服务、付费依赖**
- 应用状态和任务放 `main/`，硬件操作一律走 BSP API，不绕过
- 遵守 LVGL 锁、回调、退出清理规则；注意 RAM 预算（release 构建会校验 SRAM，当前余量见编译输出）
- 二次开发应用要有自己的页面和交互，改名换色不算重新设计
- 涉及屏幕、按键、音频、电池、时序的结论，必须真机验收才算数
- 交付前跑 `./tools/validate.sh`；纯逻辑测试无硬件可跑（`tests/`）

## 本仓库编译（已验证路径）

```bash
# 本机（Intel Mac, macOS 13.7）：ESP-IDF 5.5.3 已装在 ~/esp/esp-idf
unset PYTHONPATH                # 关键！否则子进程 pip 被污染
export PATH=/usr/local/bin:$PATH  # 用 python3.13（系统 3.14 太新）
export IDF_DIR=$HOME/esp/esp-idf
source ~/esp/esp-idf/export.sh
bash build.sh release
# 产物 build-public/FoloToy-AI-Passport-full.bin → Web Flasher 刷 0x0
```

网络相关（clone/fetch 上游）注意代理端口以 `scutil --proxy` 实测为准。

## 上游与同步

- 上游社区固件（本仓库的直系基础）：`manchunx7-bit/passport-os`
- 官方仓库（文档与 BSP 的上游）：`FoloToy/ai-passport`（本仓库 fork 自 manchunx7-bit，其又源自官方）
- 想拉上游更新：`git remote add upstream https://github.com/manchunx7-bit/passport-os.git && git fetch upstream`，按需 cherry-pick / merge，不要盲目 sync（会覆盖删改）
