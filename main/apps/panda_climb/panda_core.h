#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 熊猫爬竹竿 —— 游戏逻辑层（纯 C，无 LVGL 依赖，可单测）
// 复刻诺基亚熊猫爬竹竿：三根竹竿各有左右两面(6槽位)，熊猫贴面攀爬，
// 左右键切面/切竿躲避迎面的小动物，越爬越快。

#define PANDA_LANES 3
#define PANDA_SWITCH_FRAMES 4    // 切杆动画帧数
#define PANDA_MAX_ENTITIES 8   // 同屏实体上限（障碍+道具）

typedef enum {
    PANDA_STATE_IDLE = 0,   // 待机（显示开始提示）
    PANDA_STATE_RUNNING,    // 游戏中
    PANDA_STATE_OVER,       // 游戏结束
} panda_state_t;

typedef enum {
    PANDA_ENTITY_NONE = 0,
    PANDA_ENTITY_SNAKE,     // 蛇：碰到结束
    PANDA_ENTITY_BUG,       // 甲虫：碰到结束
    PANDA_ENTITY_SHOOT,     // 竹笋：+5 分
    PANDA_ENTITY_STAR,      // 星星：无敌 3 秒
} panda_entity_kind_t;

typedef struct {
    uint8_t kind;       // panda_entity_kind_t
    uint8_t lane;       // 槽位 0..5（竿*2+面；0=竿0左面 1=竿0右面 …）
    int16_t y;          // 世界坐标（越大越靠上，随爬升相对下移）
    bool consumed;
} panda_entity_t;

typedef struct {
    panda_state_t state;
    uint8_t lane;           // 熊猫当前槽位 0..5（竿*2+面）
    int16_t switch_anim;    // 切杆动画帧倒计时（0=在杆上）
    int16_t from_lane;      // 切杆起点（动画插值用）
    uint32_t height_m;      // 当前高度（分）
    uint32_t best_m;        // 本次开机最佳
    uint16_t scroll_y;      // 世界滚动偏移（熊猫固定在屏幕 2/3 高度处）
    uint16_t speed;         // 爬升速度（px/s），随高度增加
    uint16_t invincible_ms; // 无敌剩余时间
    uint32_t tick_ms;       // 累计游戏时间
    panda_entity_t entities[PANDA_MAX_ENTITIES];
    uint16_t entity_count;
    int16_t next_spawn_y;   // 下一个生成实体的世界 y
    uint32_t rng;           // 简易 LCG 状态
} panda_game_t;

void panda_init(panda_game_t *g);
void panda_start(panda_game_t *g);
// 推进一帧；dt_ms 为距上次调用的毫秒数。返回 false 表示游戏结束。
void panda_tick(panda_game_t *g, uint32_t dt_ms);
// UP=左移一槽 DOWN=右移一槽（0..5 间夹紧，同竿两面→邻竿）；游戏中才生效
void panda_move(panda_game_t *g, int dir);
// 槽位→(竿号, 面)：面 -1=竿左侧 +1=竿右侧（UI 定位用）
uint8_t panda_slot_lane(uint8_t slot);
int8_t  panda_slot_side(uint8_t slot);
// 碰撞检测结果由内部处理：吃道具加分/无敌，撞障碍转 PANDA_STATE_OVER
// 供 UI 查询
bool panda_is_running(const panda_game_t *g);

#ifdef __cplusplus
}
#endif
