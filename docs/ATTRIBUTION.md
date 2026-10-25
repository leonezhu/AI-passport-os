# 来源与致谢（Attribution）

本项目（leonezhu/passport-os，"Xiong fork"）基于以下项目构建，按使用层次记录如下。

## 硬件与官方生态

- **FoloToy AI Passport**：ESP32-C3 / 8MB Flash / ST7789P3 屏 / ES8311 音频 / 三键交互
  - 官方站：https://ai-passport.folotoy.cn/
  - 开发指南与 Web Flasher 均出自官方文档

## 固件上游

- **manchunx7-bit/passport-os**（MIT）：本仓库的直接基础
  - https://github.com/manchunx7-bit/passport-os
  - 保留：整机框架、启动器、首页、收音机、AI 随身语音、小智、秒表、番茄钟
  - 移除：Muse、你在狗叫什么、声音跳一跳、俄罗斯方块

## 功能组件来源

- **小智 AI**（「小智 AI」应用的后端服务与协议）
  - https://xiaozhi.me/
- **leo0183/leo-radio**（AI Passport 随身城市电台，收音机功能的相关参考）
  - https://github.com/leo0183/leo-radio
- **manchunx7-bit/folo-ai-passport-voice**（AI 随身语音电脑端，本仓库贡献了 Intel Mac 适配）
  - https://github.com/manchunx7-bit/folo-ai-passport-voice

## 许可

主体 MIT，见根目录 LICENSE（含 FoloToy / manchunx7-bit / leonezhu 版权行）。
第三方组件遵循其目录内的 LICENSE / NOTICE。
