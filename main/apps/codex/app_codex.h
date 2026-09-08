#pragma once

#include "apps/app_base.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const passport_app_t g_codex_app;

void codex_app_init(void);
void codex_app_start(void);
void codex_app_stop(void);
void codex_app_on_key(uint8_t btn, app_btn_event_t event, uint16_t mv);

#ifdef __cplusplus
}
#endif
