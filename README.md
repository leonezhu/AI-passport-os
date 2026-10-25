<div align="center">

# AI Passport OS

**个人自用固件 · 只为自己想要的功能服务。**

基于 [manchunx7-bit/passport-os](https://github.com/manchunx7-bit/passport-os) 修改 · ESP32-C3 · 持续增删中

</div>

---

## 这是什么

一块 [FoloToy AI Passport](https://ai-passport.folotoy.cn/) 硬件上的自制固件，**纯个人自用**：在上游社区固件基础上移除了自己不需要的应用，之后会慢慢加上自己感兴趣的东西。

当前保留 6 款应用：**首页 / 收音机 / AI 随身语音 / 小智 AI / 秒表 / 番茄钟**。

已移除：Muse、你在狗叫什么、声音跳一跳、俄罗斯方块。

## 记录一下用了谁的东西

| 来源 | 用了什么 |
| --- | --- |
| [FoloToy](https://ai-passport.folotoy.cn/) | AI Passport 硬件、官方固件与刷机工具 |
| [manchunx7-bit/passport-os](https://github.com/manchunx7-bit/passport-os) | 固件本体（MIT）：整机框架与全部保留应用 |
| [manchunx7-bit/folo-ai-passport-voice](https://github.com/manchunx7-bit/folo-ai-passport-voice) | AI 随身语音的电脑端程序 |
| [小智 AI](https://xiaozhi.me/) | 「小智 AI」应用的服务 |
| [leo0183/leo-radio](https://github.com/leo0183/leo-radio) | 收音机功能的相关参考 |

许可：MIT，见 [LICENSE](LICENSE)。

## 编译

需要 ESP-IDF 5.5.3：

```bash
IDF_DIR="$HOME/esp/esp-idf" bash build.sh release
```

产物 `build-public/FoloToy-AI-Passport-full.bin` 刷入 `0x0`（完整镜像，刷完需重新配网）。

## 接下来想加的

想好了就做，没顺序承诺：晨报宠物、身体线打卡、番茄钟联动电脑端专注数据。
