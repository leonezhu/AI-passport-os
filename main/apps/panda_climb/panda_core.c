#include "panda_core.h"

#include <string.h>

// ---- 参数区（全部可调） ----
#define PANDA_BASE_SPEED 40      // 初始爬升 px/s
#define PANDA_SPEED_GAIN 1       // 每爬 10px 速度 +1
#define PANDA_MAX_SPEED 150
#define PANDA_INVINCIBLE_MS 3000
#define PANDA_SPAWN_GAP_MIN 110  // 实体最小生成间隔（世界 px）
#define PANDA_SPAWN_GAP_MAX 240
#define PANDA_ENTITY_H 22        // 实体碰撞盒高（放大版）

static uint32_t next_rand(panda_game_t *g) {
    g->rng = g->rng * 1664525u + 1013904223u;
    return (g->rng >> 8);
}

void panda_init(panda_game_t *g) {
    memset(g, 0, sizeof(*g));
    g->state = PANDA_STATE_IDLE;
    g->rng = 0x1234abcd;  // 每次开机相同序列也可接受；如需随机由 app 层重种子
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
    g->tick_ms = 0;
    g->next_spawn_y = 300;   // 首个实体出现在视野外上方
    g->state = PANDA_STATE_RUNNING;
}

// ---- 6 槽位模型（原版诺基亚规则） ----
// 每根竿有左/右两面：竿0[左0|右1] 竿1[左2|右3] 竿2[左4|右5]
// 熊猫贴在竿的某一面；左右键在 6 槽位间移动，同竿两面先切面再到邻竿
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
            e->lane = (uint8_t)(next_rand(g) % (PANDA_LANES * 2));   // 占某竿的某一面
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

    // 碰撞：熊猫贴当前槽位（同竿同面）；屏幕熊猫 y 固定 160 → 世界坐标 scroll_y+160
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
