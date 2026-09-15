// 首页配置服务:按需启停的 HTTP 服务器(仅上传模式期间存在)。
// 手机连同一 Wi-Fi 打开设备 IP,即可设置姓名/签名/城市并上传头像。
// 图片缩放与 RGB565 编码全部在浏览器端完成,设备端零图片解码开销。
#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// 启动配置服务(幂等)。需要 Wi-Fi 已连接;返回后可用 home_upload_url() 取地址。
esp_err_t home_upload_start(void);

// 停止服务并回收 httpd 内存(退出上传模式/离开首页时必须调用)。
void home_upload_stop(void);

bool home_upload_active(void);

// "http://192.168.x.x"(仅服务运行期间有效)
const char *home_upload_url(void);

#ifdef __cplusplus
}
#endif
