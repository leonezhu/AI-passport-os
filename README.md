<div align="center">

# Passport OS · Xiong 版

**把一块小硬件，折腾成每天会用的小玩意。**

基于 [manchunx7-bit/passport-os](https://github.com/manchunx7-bit/passport-os) 的个人定制版 · ESP32-C3 · 8 MB Flash · 6 款应用

**简体中文**

[看看能玩什么](#六款应用装进口袋) · [安装固件](#先用起来) · [与上游的差异](#与上游的差异) · [自己编译](#自己编译)

</div>

---

## 这个 fork 做了什么

上游 Passport OS 是一款优秀的社区固件。这个版本按个人使用需求做了减法：**移除 Muse 与三款小游戏，保留全部日用功能**，为后续自研应用腾出 Flash 空间（应用分区余量约 1 MB）。

移除清单：

- **Muse** 语音助手（含 BLE 配对、Noise 协议组件、代理配置界面与 `/api/muse` 手机端接口）
- **你在狗叫什么**（声音小游戏）
- **声音跳一跳**（声控跳跃游戏）
- **俄罗斯方块**

## 来源与致谢

这个固件站在以下项目和作者的肩膀上，在此一一记录：

| 来源 | 贡献 | 链接 |
| --- | --- | --- |
| **FoloToy** | AI Passport 硬件与官方固件、Web Flasher 刷机工具 | [ai-passport.folotoy.cn](https://ai-passport.folotoy.cn/) |
| **manchunx7-bit** | Passport OS 上游社区固件（本 fork 的直接基础）：整机框架、启动器、首页/收音机/随身语音/小智/秒表/番茄钟等全部保留功能的实现 | [manchunx7-bit/passport-os](https://github.com/manchunx7-bit/passport-os) |
| **manchunx7-bit** | AI 随身语音电脑端程序（本项目 Mac Intel 适配 PR 已提交上游） | [manchunx7-bit/folo-ai-passport-voice](https://github.com/manchunx7-bit/folo-ai-passport-voice) |
| **小智 AI (xiaozhi)** | 「小智 AI」语音对话应用的服务与生态 | [xiaozhi.me](https://xiaozhi.me/) |
| **leo0183** | AI Passport 随身城市电台思路与实现参考（收音机应用相关） | [leo0183/leo-radio](https://github.com/leo0183/leo-radio) |

 详细来源记录见 [docs/ATTRIBUTION.md](docs/ATTRIBUTION.md)，许可信息见 [LICENSE](LICENSE)（MIT，含各方版权行）。

## 六款应用，装进口袋

| 应用 | 可以做什么 | 使用条件 |
| --- | --- | --- |
| 首页 | 时钟、个人资料、头像与名片、天气和空气质量 | 天气与空气质量需要联网 |
| 收音机 | 选择城市和电台，听广播，设置睡眠计时 | Wi-Fi 与可用的网络电台 |
| AI 随身语音 | 把设备当成电脑的语音输入工具，联动快捷键 | 配套电脑端程序（Mac/Windows 均可） |
| 小智 AI | 通过麦克风和扬声器进行语音对话 | 网络与小智服务配置 |
| 秒表 | 计时、分段、暂停与继续 | 可离线使用 |
| 番茄钟 | 专注、休息和到时提醒 | 可离线使用 |

应用之外，主题、音量、亮度、自动熄屏、短按唤醒、Wi-Fi 配网、电池显示与功耗管理等整机功能全部保留。

## 与上游的差异

| 变更 | 说明 |
| --- | --- |
| 删除 4 款应用 | 见上方清单；`main/apps/{muse,game,sound_jump}` 与 `components/passport_muse` 整体移除 |
| 配网页精简 | `phone_setup.html` 移除 Muse 配置区（SDK Token / 代理设置） |
| 网络诊断页 | 移除 Muse 代理路由显示 |
| 构建校验 | `verify_firmware.py` 移除 Muse 代理默认值检查；CMakeLists 移除对应源文件 |
| 保留 | 首页 / 收音机 / 随身语音 / 小智 / 秒表 / 番茄钟，及全部整机功能 |

上游的完整文档、其他应用与 Muse 教程见 [原项目](https://github.com/manchunx7-bit/passport-os)。

## 先用起来

适用硬件：**相同硬件版本的 FoloToy AI Passport，ESP32-C3、8 MB Flash**。

从 [Releases](https://github.com/leonezhu/passport-os/releases) 下载固件，或自行编译：

| 你的情况 | 选择镜像 | 烧录地址 |
| --- | --- | --- |
| 首次安装，或从上游官方固件切换 | `*-full.bin` 完整镜像 | `0x0` |
| 已安装本固件，且确认分区布局一致 | `*-app.bin` 应用镜像 | `0x10000` |

> **注意**：完整镜像会覆盖配网信息，刷完需重新配网（连接设备热点 `Passport-WiFi-xxxx` → 浏览器打开 `http://192.168.4.1`）。

**AI 随身语音的电脑端**：上游官方程序面向 Windows；Mac（含 Intel）用户可参考 [folo-ai-passport-voice](https://github.com/manchunx7-bit/folo-ai-passport-voice) 自行编译客户端，Intel Mac 适配见 [PR #2](https://github.com/manchunx7-bit/folo-ai-passport-voice/pull/2)。

## 自己编译

准备 **Git、ESP-IDF 5.5.3** 及配套工具链。

```bash
git clone https://github.com/leonezhu/passport-os.git
cd passport-os
IDF_DIR="$HOME/esp/esp-idf" bash build.sh release
```

构建产物：

| 文件 | 用途 |
| --- | --- |
| `build-public/FoloToy-AI-Passport-full.bin` | 完整镜像，写入 `0x0` |
| `build-public/passport-os.bin` | 应用镜像，相同分区布局升级写入 `0x10000` |

静态检查与固件校验：

```bash
./tools/validate.sh --static
./tools/validate.sh --firmware
```

## 规划中的自研应用

- 晨报宠物：晨间摘要 + 健康红绿灯
- 身体线打卡伙伴：运动/饮食 streak，连续 7 天满血动画
- 番茄钟 × ActivityWatch 联动

## 许可与致谢

项目主体沿用上游 [MIT 许可证](LICENSE)，各方版权行已在 LICENSE 中记录。感谢 FoloToy 的硬件、[manchunx7-bit](https://github.com/manchunx7-bit) 的优秀上游项目、小智 AI 与 leo0183 的电台工作，以及所有社区贡献者。完整致谢见上方「来源与致谢」与 [docs/ATTRIBUTION.md](docs/ATTRIBUTION.md)。

**从一个自己想用的小玩意开始，接下来，把它改成自己的样子。**
