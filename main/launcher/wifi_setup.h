#pragma once

#include "apps/app_base.h"

#ifdef __cplusplus
extern "C" {
#endif

void wifi_setup_init(void);
void wifi_setup_start(void);
void wifi_setup_stop(void);
void wifi_setup_on_key(uint8_t btn, app_btn_event_t event, uint16_t mv);

extern const passport_app_t g_wifi_app;

#ifdef __cplusplus
}
#endif
