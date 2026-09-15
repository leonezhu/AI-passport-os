// 首页个人资料:姓名/签名/城市坐标存 NVS,人像照片(96×120 1bpp 打包)存 SPIFFS。
// 照片由上传网页在浏览器端完成裁剪、亮度阈值和 1bit 打包,设备端不做任何图片
// 处理,只做流式落盘 —— 这是小内存约束下的关键取舍。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HOME_PORTRAIT_W 96 /* 竖幅人像宽(与首页人像框同尺寸) */
#define HOME_PORTRAIT_H 150
/* 1bpp 按行打包(每字节 8 像素,MSB 在前),共 1440 字节 */
#define HOME_AVATAR_BYTES (HOME_PORTRAIT_W * HOME_PORTRAIT_H / 8)
#define HOME_NAME_MAX 32      /* UTF-8 字节数,含结尾 0(约 9 个汉字) */
#define HOME_SIGNATURE_MAX 96 /* UTF-8 字节数,含结尾 0(约 31 个汉字) */
#define HOME_CITY_MAX 48      /* UTF-8 字节数,含结尾 0 */
#define HOME_COORD_MAX 16     /* "39.9075" 字符串 */

typedef struct {
    char name[HOME_NAME_MAX];
    char signature[HOME_SIGNATURE_MAX];
    char city[HOME_CITY_MAX];
    char lat[HOME_COORD_MAX];
    char lon[HOME_COORD_MAX];
} home_profile_t;

// 读 NVS 资料。头像文件不在此加载,SPIFFS 惰性挂载。
void home_profile_init(void);

// 线程安全拷贝当前资料(httpd 任务与首页任务都会调用)。
void home_profile_get(home_profile_t *out);

// 任一资料(含头像)发生变化时递增;UI 据此免轮询字段刷新。
uint32_t home_profile_version(void);

// 整体写入姓名/签名/城市/坐标(任一指针可为 NULL 表示不改)。
// 内容超长会拒绝并返回 ESP_ERR_INVALID_ARG,绝不静默截断用户输入。
esp_err_t home_profile_set_text(const home_profile_t *in);

bool home_profile_has_avatar(void);

// 把 SPIFFS 里的头像读进 dst(需 HOME_AVATAR_BYTES 字节)。调用方不得持有
// LVGL 锁(文件读取可达几十毫秒)。首次调用会挂载 storage 分区。
esp_err_t home_profile_read_avatar(uint8_t *dst, size_t cap);

// 流式写头像:参数由 /api/avatar 处理器按收包顺序给出。数据先写 tmp 文件,
// 收满 HOME_AVATAR_BYTES 后原子改名为正式头像,中断的上传不影响旧头像。
esp_err_t home_profile_write_avatar_chunk(const uint8_t *data, size_t len,
                                          bool first, bool last);

#ifdef __cplusplus
}
#endif
