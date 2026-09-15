#pragma once

/* Passport travel-instrument palette. No LVGL dependency: shared by host checks too.
 * Text roles are always used on dark surfaces; bright badges use ON_ACCENT.
 * Keep game-piece colours distinct and QR modules strictly black/white. */
#define UI_BG          0x181B1C
#define UI_SURFACE     0x202526
#define UI_SURFACE_ALT 0x303738
#define UI_BORDER      0x59605D
#define UI_TEXT        0xEEE7D6
#define UI_TEXT_DIM    0xB3B2A5
#define UI_ACCENT      0xDDB36C
#define UI_AMBER       0xDDB36C
#define UI_DANGER      0xF09B87
#define UI_ON_ACCENT   0x181B1C

/* Compatibility names for existing pixel artwork and state colours. */
#define UI_SKY        UI_BG
#define UI_SKY_DARK   UI_ACCENT
#define UI_INK        UI_ON_ACCENT
#define UI_PAPER      UI_SURFACE
#define UI_SUCCESS    0x9DB89A
#define UI_GRASS      UI_SUCCESS
#define UI_GRASS_DARK UI_SUCCESS
#define UI_YELLOW     UI_AMBER
#define UI_ORANGE     UI_AMBER
#define UI_RED        UI_DANGER
#define UI_MUTED      UI_TEXT_DIM
#define UI_SUB        UI_TEXT_DIM
