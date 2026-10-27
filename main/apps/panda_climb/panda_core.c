#include "panda_core.h"

#include <string.h>

// ---- 参数区（全部可调） ----
#define PANDA_BASE_SPEED 40      // 初始爬升 px/s
#define PANDA_SPEED_GAIN 1       // 每爬 10px 速度 +1
#define PANDA_MAX_SPEED 160
#define PANDA_INVINCIBLE_MS 3000
#define PANDA_SPAWN_GAP_MIN 90   // 实体最小生成间隔（世界 px）
#define PANDA_SPAWN_GAP_MAX 220
#define PANDA_ENTITY_H 16        // 实体碰撞盒高

static uint32_t next_rand(panda_game_t *g) {
    g->rng = g->rng * 1664525u + 1013904223u;
    return (g->rng >> 8);
}

static int16_t lane_x(uint8_t lane) { return (int16_t)(40 + lane * 80); }

void panda_init(panda_game_t *g) {
    memset(g, 0, sizeof(*g));
    g->state = PANDA_STATE_IDLE;
    g->rng = 0x1234abcd;  // 每次开机相同序列也可接受；如需随机由 app 层重种子
}

void panda_start(panda_game_t *g) {
    panda_state_t prev = g->state;
    memset(g->entities, 0, sizeof(g->entities));
    g->entity_count = 0;
    g->lane = 1;             // 从中间杆开始
    g->switch_anim = 0;
    g->from_lane = 1;
    g->height_m = 0;
    g->scroll_y = 0;
    g->speed = PANDA_BASE_SPEED;
    g->invincible_ms = 0;
    g->tick_ms = 0;
    g->next_spawn_y = 300;   // 首个实体出现在视野外上方
    g->state = PANDA_STATE_RUNNING;
    if (prev == PANDA_STATE_OVER) { /* best 已在结束时记录 */ }
}

static void spawn_entities(panda_game_t *g) {
    while (g->next_spawn_y < (int16_t)(g->scroll_y + 320)) {  // 视野上方 320px 内预生成
        // 70% 障碍 / 20% 竹笋 / 10% 星星
        uint32_t roll = next_rand(g) % 10;
        uint8_t kind;
        if (roll < 7) kind = (next_rand(g) & 1) ? PANDA_ENTITY_SNAKE : PANDA_ENTITY_BUG;
        else if (roll < 9) kind = PANDA_ENTITY_SHOOT;
        else kind = PANDA_ENTITY_STAR;
        if (g->entity_count < PANDA_MAX_ENTITIES) {
            panda_entity_t *e = &g->entities[g->entity_count++];
            e->kind = kind;
            e->lane = (uint8_t)(next_rand(g) % PANDA_LANES);
            e->y = g->next_spawn_y;
            e->consumed = false;
        }
        uint32_t gap = PANDA_SPAWN_GAP_MIN + next_rand(g) % (PANDA_SPAWN_GAP_MAX - PANDA_SPAWN_GAP_MIN);
        // 高度越高生成越密
        uint32_t shrink = (g->speed - PANDA_BASE_SPEED) * 2;
        if (gap > PANDA_SPAWN_GAP_MIN + shrink) gap -= shrink; else gap = PANDA_SPAWN_GAP_MIN;
        g->next_spawn_y = (int16_t)(g->next_spawn_y + (int32_t)gap);
    }
}

static void cull_entities(panda_game_t *g) {
    int16_t floor_y = (int16_t)(g->scroll_y > 40 ? g->scroll_y - 40 : 0);
    uint16_t w = 0;
    for (uint16_t i = 0; i < g->entity_count; ++i) {
        panda_entity_t *e = &g->entities[i];
        if (e->consumed || e->y < floor_y) continue;   // 吃掉或滚出屏幕底部即回收
        g->entities[w++] = *e;
    }
    g->entity_count = w;
}

void panda_move(panda_game_t *g, int dir) {
    if (g->state != PANDA_STATE_RUNNING || g->switch_anim) return;
    int target = (int)g->lane + dir;
    if (target < 0 || target >= PANDA_LANES) return;
    g->from_lane = g->lane;
    g->lane = (uint8_t)target;
    g->switch_anim = PANDA_SWITCH_FRAMES;
}

void panda_tick(panda_game_t *g, uint32_t dt_ms) {
    if (g->state != PANDA_STATE_RUNNING) return;
    if (dt_ms > 200) dt_ms = 200;   // 掉帧保护（长按/卡顿不瞬移）
    g->tick_ms += dt_ms;

    // 爬升
    g->scroll_y = (uint16_t)(g->scroll_y + (uint32_t)g->speed * dt_ms / 1000);
    g->height_m = g->scroll_y / 2;   // 2px = 1 米
    uint16_t want = (uint16_t)(PANDA_BASE_SPEED + g->height_m / 10 * PANDA_SPEED_GAIN);
    if (want > PANDA_MAX_SPEED) want = PANDA_MAX_SPEED;
    g->speed = want;

    if (g->switch_anim) g->switch_anim--;
    if (g->invincible_ms) g->invincible_ms -= (dt_ms < g->invincible_ms ? dt_ms : g->invincible_ms);

    spawn_entities(g);
    cull_entities(g);

    // 碰撞：熊猫占当前 lane，世界 y ≈ scroll_y + PANDA_OFFSET（熊猫在屏幕下 1/3）
    // 屏幕熊猫 y 固定 160；世界坐标 = scroll_y + 160
    int16_t panda_wy = (int16_t)(g->scroll_y + 160);
    for (uint16_t i = 0; i < g->entity_count; ++i) {
        panda_entity_t *e = &g->entities[i];
        if (e->consumed || e->lane != g->lane) continue;
        if (panda_wy + PANDA_ENTITY_H / 2 < e->y || panda_wy - PANDA_ENTITY_H / 2 > e->y + PANDA_ENTITY_H) continue;
        if (e->kind == PANDA_ENTITY_SNAKE || e->kind == PANDA_ENTITY_BUG) {
            if (g->invincible_ms) { e->consumed = true; continue; }  // 无敌撞碎障碍
            g->state = PANDA_STATE_OVER;
            if (g->height_m > g->best_m) g->best_m = g->height_m;
            return;
        } else if (e->kind == PANDA_ENTITY_SHOOT) {
            e->consumed = true;
            g->height_m += 5;
        } else if (e->kind == PANDA_ENTITY_STAR) {
            e->consumed = true;
            g->invincible_ms = PANDA_INVINCIBLE_MS;
        }
    }
}

bool panda_is_running(const panda_game_t *g) { return g->state == PANDA_STATE_RUNNING; }
