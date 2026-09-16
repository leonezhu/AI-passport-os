[English](README.md) · 简体中文

# Passport OS

面向 FoloToy AI Passport（ESP32-C3、8 MB Flash）的社区固件，包含多应用系统、
Wi-Fi 配网、小智 AI、收音机、游戏，以及 Windows 电脑端硬件麦克风与快捷键联动。

## 下载与安装

从最新 GitHub Release 下载 `FoloToy-AI-Passport-full.bin`，将完整合并镜像写入
`0x0`。烧录前必须确认设备是相同硬件版本的 ESP32-C3 / 8 MB AI Passport。
完整镜像可能覆盖设备本地的 Wi-Fi 与应用配置，需要的内容请先备份。

Release 工作流固定使用 ESP-IDF 5.5.3，完成编译、合并镜像校验和受保护分区布局
校验后，发布同一个二进制文件。编译通过不等于真机上的音频、按键、电池和网络
已经全部验收。

Windows 的“硬件麦克风 + 右 Shift/Enter”电脑端程序见
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

## 兼容性与风险

- 仅支持硬件版本相同的 FoloToy AI Passport：ESP32-C3、8 MB Flash。
- 外观相似但 Flash 容量、屏幕、引脚、音频编解码、电池电路或分区表不同的设备，
  不能直接刷入。
- 保留一份已知可用的恢复固件；烧录中断、端口选错或目标型号错误可能需要恢复。
- 社区用户必须使用自己的 Wi-Fi 和服务凭据。

