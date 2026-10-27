#include "panda_core.h"

#include <string.h>

// ---- 参数区（全部可调） ----
#define PANDA_BASE_SPEED 38      // 初始爬升 px/s
#define PANDA_SPEED_NUM 1        // 速度无限递增：每 PANDA_SPEED_DEN ms +1 px/s
#define PANDA_SPEED_DEN 2000     // → 每秒 +0.5px/s，两分钟 ~98px/s，持续施压不封顶
#define PANDA_INVINCIBLE_MS 3000 // 星星无敌
#define PANDA_HURT_MS 1500       // 受伤闪烁保护（防连续判定）
#define PANDA_SPAWN_GAP_MIN 110
#define PANDA_SPAWN_GAP_MAX 240
#define PANDA_ENTITY_H 24
// 世界↔屏幕换算（所见即所得的关键）：
// 渲染: entity_sy = PANDA_RENDER_TOP - (e->y - scroll_y)，熊猫画在 PANDA_SCREEN_Y
// → 熊猫对应的世界 y = scroll_y + (PANDA_RENDER_TOP - PANDA_SCREEN_Y)
#define PANDA_SCREEN_Y 158
#define PANDA_RENDER_TOP 216

static uint32_t next_rand(panda_game_t *g) {
    g->rng = g->rng * 1664525u + 1013904223u;
    return (g->rng >> 8);
}

void panda_init(panda_game_t *g) {
    memset(g, 0, sizeof(*g));
    g->state = PANDA_STATE_IDLE;
    g->rng = 0x1234abcd;
}

void panda_start(panda_game_t *g) {
    memset(g->entities, 0, sizeof(g->entities));
    g->entity_count = 0;
    g->lane = 2;             // 6 槽位：从中间竿右面开始
    g->switch_anim = 0;
    g->from_lane = 2;
    g->height_m = 0;
    g->scroll_y = 0;
    g->speed = PANDA_BASE_SPEED;
    g->invincible_ms = 0;
    g->lives = 3;
    g->hurt_ms = 0;
    g->combo = 0;
    g->combo_score = 0;
    g->tick_ms = 0;
    g->next_spawn_y = 300;
    g->state = PANDA_STATE_RUNNING;
}

// ---- 6 槽位模型（原版诺基亚规则） ----
// 每根竿有左/右两面：竿0[左0|右1] 竿1[左2|右3] 竿2[左4|右5]
uint8_t panda_slot_lane(uint8_t slot) { return (uint8_t)(slot / 2); }
int8_t  panda_slot_side(uint8_t slot) { return (slot % 2) ? (int8_t)1 : (int8_t)-1; }  // -1左面 +1右面

void panda_move(panda_game_t *g, int dir) {
    if (g->state != PANDA_STATE_RUNNING || g->switch_anim) return;
    int target = (int)g->lane + dir;
    if (target < 0 || target >= PANDA_LANES * 2) return;
    g->from_lane = g->lane;
    g->lane = (uint8_t)target;
    g->switch_anim = PANDA_SWITCH_FRAMES;
}

static void spawn_entities(panda_game_t *g) {
    while (g->next_spawn_y < (int16_t)(g->scroll_y + 340)) {
        uint32_t roll = next_rand(g) % 10;
        uint8_t kind;
        if (roll < 5) kind = (next_rand(g) & 1) ? PANDA_ENTITY_SNAKE : PANDA_ENTITY_BUG;
        else if (roll < 7) kind = PANDA_ENTITY_BEE;              // 20% 移动障碍
        else if (roll < 9) kind = PANDA_ENTITY_SHOOT;
        else kind = PANDA_ENTITY_STAR;
        if (g->entity_count < PANDA_MAX_ENTITIES) {
            panda_entity_t *e = &g->entities[g->entity_count++];
            e->kind = kind;
            e->lane = (uint8_t)(next_rand(g) % (PANDA_LANES * 2));
            e->y = g->next_spawn_y;
            e->consumed = false;
        }
        uint32_t gap = PANDA_SPAWN_GAP_MIN + next_rand(g) % (PANDA_SPAWN_GAP_MAX - PANDA_SPAWN_GAP_MIN);
        uint32_t shrink = (g->speed - PANDA_BASE_SPEED);         // 越快生成越密
        if (gap > PANDA_SPAWN_GAP_MIN + shrink) gap -= shrink; else gap = PANDA_SPAWN_GAP_MIN;
        g->next_spawn_y = (int16_t)(g->next_spawn_y + (int32_t)gap);
    }
}

static void cull_entities(panda_game_t *g) {
    int16_t floor_y = (int16_t)(g->scroll_y > 40 ? g->scroll_y - 40 : 0);
    uint16_t w = 0;
    for (uint16_t i = 0; i < g->entity_count; ++i) {
        panda_entity_t *e = &g->entities[i];
        if (e->consumed || e->y < floor_y) continue;
        g->entities[w++] = *e;
    }
    g->entity_count = w;
}

void panda_tick(panda_game_t *g, uint32_t dt_ms) {
    if (g->state != PANDA_STATE_RUNNING) return;
    if (dt_ms > 200) dt_ms = 200;
    g->tick_ms += dt_ms;

    // 爬升 + 速度无限递增（tick_ms 累计驱动，每 2s +1px/s，永不封顶）
    g->scroll_y = (uint16_t)(g->scroll_y + (uint32_t)g->speed * dt_ms / 1000);
    g->height_m = g->scroll_y / 2;   // 2px = 1 米
    if (g->tick_ms / PANDA_SPEED_DEN > (g->tick_ms - dt_ms) / PANDA_SPEED_DEN) {
        g->speed++;
    }

    if (g->switch_anim) g->switch_anim--;
    if (g->invincible_ms) g->invincible_ms -= (dt_ms < g->invincible_ms ? dt_ms : g->invincible_ms);
    if (g->hurt_ms) g->hurt_ms -= (dt_ms < g->hurt_ms ? dt_ms : g->hurt_ms);

    spawn_entities(g);
    cull_entities(g);

    // 碰撞：所见即所得——熊猫世界 y = scroll_y + (RENDER_TOP - SCREEN_Y)
    int16_t panda_wy = (int16_t)(g->scroll_y + (PANDA_RENDER_TOP - PANDA_SCREEN_Y));
    for (uint16_t i = 0; i < g->entity_count; ++i) {
        panda_entity_t *e = &g->entities[i];
        if (e->consumed || e->lane != g->lane) continue;
        if (panda_wy + PANDA_ENTITY_H / 2 < e->y || panda_wy - PANDA_ENTITY_H / 2 > e->y + PANDA_ENTITY_H) continue;
        if (e->kind == PANDA_ENTITY_SNAKE || e->kind == PANDA_ENTITY_BUG || e->kind == PANDA_ENTITY_BEE) {
            if (g->invincible_ms || g->hurt_ms) { e->consumed = true; continue; }   // 无敌撞碎/受伤保护
            e->consumed = true;
            if (g->lives > 0) g->lives--;
            g->combo = 0;                    // 撞障碍连击清零
            g->combo_score = 0;
            if (g->lives == 0) {
                g->state = PANDA_STATE_OVER;
                if (g->height_m > g->best_m) g->best_m = g->height_m;
                return;
            }
            g->hurt_ms = PANDA_HURT_MS;
        } else if (e->kind == PANDA_ENTITY_SHOOT) {
            e->consumed = true;
            g->combo++;
            uint32_t mult = g->combo > 4 ? 4 : g->combo;   // 连击倍率 ×1..×4
            uint32_t gain = 5u * mult;
            g->height_m += gain;
            g->combo_score += gain;
        } else if (e->kind == PANDA_ENTITY_STAR) {
            e->consumed = true;
            g->invincible_ms = PANDA_INVINCIBLE_MS;
        }
    }
}

bool panda_is_running(const panda_game_t *g) { return g->state == PANDA_STATE_RUNNING; }
