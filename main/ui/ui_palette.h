#pragma once

/* Passport terminal palette. No LVGL dependency: shared by host checks too.
 * Text roles are always used on dark surfaces; bright badges use ON_ACCENT.
 * Keep game-piece colours distinct and QR modules strictly black/white. */
#define UI_BG          0x03120B
#define UI_SURFACE     0x091E14
#define UI_SURFACE_ALT 0x122E20
#define UI_BORDER      0x37694B
#define UI_TEXT        0xA8EDBB
#define UI_TEXT_DIM    0x80AF90
#define UI_ACCENT      0x8CF5A6
#define UI_AMBER       0xEDBC73
#define UI_DANGER      0xFF9585
#define UI_ON_ACCENT   0x03120B

/* Compatibility names for existing pixel artwork and state colours. */
#define UI_SKY        UI_BG
#define UI_SKY_DARK   UI_ACCENT
#define UI_INK        UI_ON_ACCENT
#define UI_PAPER      UI_SURFACE
#define UI_GRASS      UI_ACCENT
#define UI_GRASS_DARK UI_ACCENT
#define UI_YELLOW     UI_AMBER
#define UI_ORANGE     UI_AMBER
#define UI_RED        UI_DANGER
#define UI_MUTED      UI_TEXT_DIM
#define UI_SUB        UI_TEXT_DIM
