[English](README.md) · 简体中文

# Passport OS

## 项目初衷

这个项目的初衷，是把 FoloToy AI Passport 做成一台我自己会长期使用、随手就能拿起来玩的“小玩意”。经过持续筛选和迭代，我把日常更常用的场景整理成了 10 款功能与应用，希望它既有实用价值，也保留一点探索和玩乐的空间。

首先感谢大家对这个项目的喜欢和支持。项目收到了很多很好的反馈，也有不少朋友提出了个性化需求。由于每个人对设备的用途和偏好都不一样，把项目开源，让大家在自己的设备上自由 DIY、增加功能或删减不需要的部分，是更合适的方式。

希望这份代码和固件能给你一个可以继续改造的起点，也希望大家都能在折腾和使用的过程中玩得开心。

面向 FoloToy AI Passport（ESP32-C3、8 MB Flash）的社区固件，包含多应用系统、
Wi-Fi 配网、小智 AI、收音机、游戏，以及 Windows 电脑端硬件麦克风与快捷键联动。

## 下载与安装

从最新 GitHub Release 下载 `FoloToy-AI-Passport-full.bin`，将完整合并镜像写入
`0x0`。烧录前必须确认设备是相同硬件版本的 ESP32-C3 / 8 MB AI Passport。
完整镜像可能覆盖设备本地的 Wi-Fi 与应用配置，需要的内容请先备份。

带有 `-app.bin` 的应用镜像写入 `0x10000`，仅适用于已经确认分区布局相同的
本项目固件升级；设备型号相同不等于分区布局相同。不要先擦除整片 Flash。
`-full.bin` 写入 `0x0`，包含空白 NVS/PHY 区域，会覆盖该范围内的 Wi-Fi、
设置和图片索引，不能当作保留资料的普通升级。

Release 工作流固定使用 ESP-IDF 5.5.3，完成编译、合并镜像校验和受保护分区布局
校验后，发布同一个二进制文件。编译通过不等于真机上的音频、按键、电池和网络
已经全部验收。

Windows 的“硬件麦克风 + 可配置语音快捷键”电脑端程序见
[folo-ai-passport-voice](https://github.com/manchunx7-bit/folo-ai-passport-voice)。

## 隐私与隔离

Release 固件只由 Git 已跟踪源码构建，不包含维护者本机的 Wi-Fi 密码、API Key
或设备本地设置。用户安装后在自己的设备上生成配置。不要把个人凭据、配置导出
或日志提交到仓库。

## 构建与验证

```bash
source <ESP-IDF-5.5.3>/export.sh
./tools/validate.sh --static
./tools/validate.sh --firmware
```

目标芯片为 `esp32c3`，完整镜像输出到
`build/FoloToy-AI-Passport-full.bin`。

本机候选使用 `bash build.sh release` 构建至 `build-public`。公开构建会检查
`PASSPORT_PUBLIC_RELEASE=ON` 与空代理默认值；CI 使用相同的校验条件。
通过对应版本验收后，可在本机生成带源码快照、manifest 和 SHA256 的候选包：

```bash
python3 tools/package_firmware.py --date YYYY-MM-DD --verification /path/to/verification-public.json
```

验收 JSON 必须对应当前固件版本且可公开，不得使用设备日志或配置导出。
打包不会创建 GitHub Release。源码 ZIP 和逐文件校验值固定候选的实际内容；
工作区有未提交修改时，旧 Git HEAD 不能代表该候选，正式标签应对应整理后的提交。

## 兼容性与风险

- 仅支持硬件版本相同的 FoloToy AI Passport：ESP32-C3、8 MB Flash。
- 外观相似但 Flash 容量、屏幕、引脚、音频编解码、电池电路或分区表不同的设备，
  不能直接刷入。
- 保留一份已知可用的恢复固件；烧录中断、端口选错或目标型号错误可能需要恢复。
- 社区用户必须使用自己的 Wi-Fi 和服务凭据。
