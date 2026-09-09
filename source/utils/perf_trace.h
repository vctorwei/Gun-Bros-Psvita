#ifndef GUNBROS_PERF_TRACE_H
#define GUNBROS_PERF_TRACE_H

#include <psp2/kernel/clib.h>

enum GunBrosPerfScene {
    GUNBROS_PERF_SCENE_BOOT = 0,
    GUNBROS_PERF_SCENE_MENU = 1,
    GUNBROS_PERF_SCENE_SHOP = 2,
    GUNBROS_PERF_SCENE_REFINERY = 3,
    GUNBROS_PERF_SCENE_GAMEPLAY = 4,
};

#ifdef GUNBROS_ENABLE_PERF_TRACE
#define GUNBROS_PERF_LOG(...) ((sceClibPrintf)(__VA_ARGS__))
#else
#define GUNBROS_PERF_LOG(...) ((void)0)
#endif

void gunbros_perf_set_context(int scene, int screen, int branch,
                              const char *source);

extern volatile unsigned int g_gunbros_perf_sprite_draw_calls;
extern volatile unsigned int g_gunbros_perf_store_query_calls;
extern volatile unsigned int g_gunbros_perf_store_query_cache_hits;
extern volatile unsigned int g_gunbros_perf_store_touch_calls;
extern volatile unsigned int g_gunbros_perf_xp_query_calls;
extern volatile unsigned int g_gunbros_perf_refinery_refresh_calls;
extern volatile unsigned int g_gunbros_perf_refinery_refresh_runs;
extern volatile unsigned int g_gunbros_perf_resource_load_next_calls;
extern volatile unsigned int g_gunbros_perf_image_load_calls;
extern volatile unsigned int g_gunbros_perf_game_object_init_calls;
extern volatile unsigned int g_gunbros_perf_store_filtered_count_calls;
extern volatile unsigned int g_gunbros_perf_store_level_lock_calls;
extern volatile unsigned int g_gunbros_perf_store_status_calls;
extern volatile unsigned int g_gunbros_perf_store_cost_string_calls;
extern volatile unsigned int g_gunbros_perf_store_sale_string_calls;

#ifdef GUNBROS_ENABLE_PERF_TRACE
#define GUNBROS_PERF_COUNT(name) (g_gunbros_perf_##name++)
#else
#define GUNBROS_PERF_COUNT(name) ((void)0)
#endif

#endif /* GUNBROS_PERF_TRACE_H */
