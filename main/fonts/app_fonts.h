#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

LV_FONT_DECLARE(radio_font);
LV_FONT_DECLARE(radio_font_title);

#define APP_FONT_BODY  (&radio_font)
#define APP_FONT_TITLE (&radio_font_title)

#ifdef __cplusplus
}
#endif
