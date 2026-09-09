#define BROTHER_SIZE_GUESS 0xbb0u
#define CPLAYER_SIZE_GUESS 0xce0u
#define BROTHER_GUN_SLOT_BASE 0x28u
#define BROTHER_GUN_SLOT_SIZE 0xf4u
#define BROTHER_GUN_SLOT_COUNT 2u
#define BROTHER_OFF_GUNCTX 0x210u
#define BROTHER_OFF_MOVE_B 0x6f4u
#define BROTHER_OFF_MOVE_A 0x6f8u
#define BROTHER_OFF_MAP    0x6fcu
#define BROTHER_OFF_CFG    0x780u
#define BROTHER_OFF_ACTIVE 0x785u
#define BROTHER_INTERP_BASE   0x68u
#define BROTHER_INTERP_NORMAL 0x310u
#define SCRIPT_INTERP_STATE   0x30u

static so_hook h_game_construct_complete, h_game_construct_base;
static uintptr_t g_game_reset_state_settings;

static void *trace_game_construct_complete(void *self, void *owner) {
    void *result = SO_CONTINUE(void *, h_game_construct_complete, self, owner);
    ((void (*)(void *))g_game_reset_state_settings)(self);
    return result;
}

static void *trace_game_construct_base(void *self, void *owner) {
    void *result = SO_CONTINUE(void *, h_game_construct_base, self, owner);
    ((void (*)(void *))g_game_reset_state_settings)(self);
    return result;
}

static void continue_game_update(void *self, int dt) {
    typedef void (*game_update_fn_t)(void *, int);
    game_update_fn_t original;

    if (g_original_game_update) {
        ((game_update_fn_t)g_original_game_update)(self, dt);
        return;
    }

    kuKernelCpuUnrestrictedMemcpy((void *)h_game_update.addr,
                                  h_game_update.orig_instr,
                                  sizeof(h_game_update.orig_instr));
    kuKernelFlushCaches((void *)h_game_update.addr,
                        sizeof(h_game_update.orig_instr));

    original = (game_update_fn_t)(uintptr_t)
        (h_game_update.thumb_addr ? h_game_update.thumb_addr : h_game_update.addr);
    original(self, dt);

    kuKernelCpuUnrestrictedMemcpy((void *)h_game_update.addr,
                                  h_game_update.patch_instr,
                                  sizeof(h_game_update.patch_instr));
    kuKernelFlushCaches((void *)h_game_update.addr,
                        sizeof(h_game_update.patch_instr));
}

static void trace_game_load(void *self, void *loader) {
    typedef void (*game_load_fn_t)(void *, void *);
    void *config = NULL;
    void *progress = NULL;

    vita_asset_preload_enter_gameplay(loader, "CGame::Load");
    vita_offline_profile_get_best_local_pair(&config, &progress);
    (void)progress;
    if (vita_offline_loadout_restore_config(config,
                                            "CGame::Load/pre")) {
        gameplay_request_configured_content(config,
                                            "CGame::Load/loadout");
    }

    if (g_original_game_load) {
        ((game_load_fn_t)g_original_game_load)(self, loader);
    } else {
        (void)SO_CONTINUE(int, h_game_load, self, loader);
    }
}

static void trace_game_update_scaled(void *self, int dt) {
    static double fractional_ms;
    double exact_ms;
    int bounded_dt = dt;
    int scaled_dt;

    gunbros_perf_set_context(GUNBROS_PERF_SCENE_GAMEPLAY, -1, -1,
                             "CGame::Update");

    if (bounded_dt < 0) {
        bounded_dt = 0;
    }
    if ((unsigned int)bounded_dt > g_gunbros_gameplay_max_delta_ms) {
        bounded_dt = (int)g_gunbros_gameplay_max_delta_ms;
    }

    exact_ms = (double)bounded_dt * (double)g_gunbros_gameplay_speed +
               fractional_ms;
    scaled_dt = (int)exact_ms;
    fractional_ms = exact_ms - (double)scaled_dt;
    continue_game_update(self, scaled_dt);
}

static uintptr_t brother_default_vptr(void) {
    static uintptr_t vptr;
    static int looked_up;

    if (!looked_up) {
        uintptr_t vtable = so_symbol(&so_mod, "_ZTV8CBrother");
        vptr = vtable ? vtable + 8 : 0;
        looked_up = 1;
    }
    return vptr;
}

static uintptr_t player_default_vptr(void) {
    static uintptr_t vptr;
    static int looked_up;

    if (!looked_up) {
        uintptr_t vtable = so_symbol(&so_mod, "_ZTV7CPlayer");
        vptr = vtable ? vtable + 8 : 0;
        looked_up = 1;
    }
    return vptr;
}

static uintptr_t brother_ai_default_vptr(void) {
    static uintptr_t vptr;
    static int looked_up;

    if (!looked_up) {
        uintptr_t vtable = so_symbol(&so_mod, "_ZTV10CBrotherAI");
        vptr = vtable ? vtable + 8 : 0;
        looked_up = 1;
    }
    return vptr;
}

static int brother_is_player_object(const void *self) {
    const void *vptr;
    uintptr_t pvptr = player_default_vptr();
    uintptr_t v;

    if (!trace_is_game_range(self, sizeof(void *)) || !pvptr) {
        return 0;
    }

    vptr = *(const void * const *)self;
    v = (uintptr_t)vptr;

    return v >= pvptr && v < pvptr + 0x80u;
}

static int brother_is_ai_object(const void *self) {
    const void *vptr;
    uintptr_t avptr = brother_ai_default_vptr();
    uintptr_t v;

    if (!trace_is_game_range(self, sizeof(void *)) || !avptr) {
        return 0;
    }

    vptr = *(const void * const *)self;
    v = (uintptr_t)vptr;
    return v >= avptr && v < avptr + 0x80u;
}

static int brother_vptr_is_full(const void *vptr) {
    uintptr_t value = (uintptr_t)vptr;
    return value >= 0x98000000u && value < 0x99000000u;
}

static int brother_has_bound_state(const void *self) {
    const unsigned char *base = (const unsigned char *)self;
    const void *map;
    const void *cfg;
    const void *gun;
    const void *script_base;
    const void *script_normal;

    if (!trace_is_game_range(self, BROTHER_SIZE_GUESS)) {
        return 0;
    }

    map = *(const void * const *)(const void *)(base + BROTHER_OFF_MAP);
    cfg = *(const void * const *)(const void *)(base + BROTHER_OFF_CFG);
    gun = *(const void * const *)(const void *)(base + BROTHER_OFF_GUNCTX);
    script_base = *(const void * const *)(const void *)(base + BROTHER_INTERP_BASE + SCRIPT_INTERP_STATE);
    script_normal = *(const void * const *)(const void *)(base + BROTHER_INTERP_NORMAL + SCRIPT_INTERP_STATE);

    if (trace_is_game_ptr(map) && trace_is_game_ptr(cfg)) {
        return 1;
    }
    if (trace_is_game_ptr(gun)) {
        return 1;
    }
    if (trace_is_game_ptr(script_base) || trace_is_game_ptr(script_normal)) {
        return 1;
    }

    return 0;
}

static void repair_brother_vptr_preserve_state(void *self, const char *log_label, void *preferred_vptr) {
    uintptr_t fallback_vptr = brother_default_vptr();
    void *old_vptr;
    void *new_vptr = preferred_vptr;

    if (!trace_is_game_range(self, BROTHER_SIZE_GUESS)) {
        return;
    }

    old_vptr = *(void **)self;
    if (brother_vptr_is_full(old_vptr)) {
        return;
    }

    if (!brother_vptr_is_full(new_vptr)) {
        new_vptr = (void *)fallback_vptr;
    }
    if (!brother_vptr_is_full(new_vptr)) {
        return;
    }

    *(void **)self = new_vptr;

    if (trace_allow(log_label)) {
        sceClibPrintf("[FIX-BROTHER] repaired vptr only at %p old_vptr=%p new_vptr=%p preserve_bound=%d\n",
                      self, old_vptr, new_vptr, brother_has_bound_state(self));
        trace_dump_brother_state(log_label, self);
    }
}

static int promote_bound_object_to_player(void *self, const char *source) {
    uintptr_t player_vptr = player_default_vptr();
    void *old_vptr;

    if (!player_vptr || !trace_is_game_range(self, CPLAYER_SIZE_GUESS)) {
        if (trace_allow_ex("CPlayer::Bind/player-vptr-no-storage", 8, 180)) {
            sceClibPrintf("[FIX-PLAYER] CPlayer promotion skipped source=%s self=%p vtable=%p storage_ok=0\n",
                          source ? source : "?", self, (void *)player_vptr);
        }
        return 0;
    }

    old_vptr = *(void **)self;
    if ((uintptr_t)old_vptr != player_vptr) {
        *(void **)self = (void *)player_vptr;
        if (trace_allow_ex("CPlayer::Bind/player-vptr-promote", 12, 180)) {
            sceClibPrintf("[FIX-PLAYER] promoted bound actor source=%s self=%p vptr=%p->%p\n",
                          source ? source : "?", self, old_vptr, (void *)player_vptr);
        }
    }

    return brother_is_player_object(self);
}

static void construct_brother_if_uninitialized(void *self, const char *log_label) {
    static const char ctor_symbol[] = "_ZN8CBrotherC1Ev";
    uintptr_t ctor = so_symbol(&so_mod, ctor_symbol);
    uintptr_t brother_vptr = brother_default_vptr();
    void *old_vptr;
    void *new_vptr;

    if (!brother_vptr || !trace_is_game_range(self, BROTHER_SIZE_GUESS)) {
        return;
    }

    old_vptr = *(void **)self;
    if (brother_vptr_is_full(old_vptr)) {
        return;
    }

    /* Critical fix: if Bind already populated the object, do not run the
     * constructor. Just repair the vptr word and preserve all gameplay state. */
    if (brother_has_bound_state(self)) {
        repair_brother_vptr_preserve_state(self, log_label, NULL);
        return;
    }

    memset(self, 0, BROTHER_SIZE_GUESS);

    if (ctor) {
        typedef void *(*brother_ctor_fn)(void *);
        ((brother_ctor_fn)ctor)(self);
    } else {
        *(uintptr_t *)self = brother_vptr;
    }

    new_vptr = *(void **)self;
    if (trace_allow(log_label)) {
        sceClibPrintf("[FIX-BROTHER] placement-constructed empty CBrother at %p old_vptr=%p new_vptr=%p ctor=%p\n",
                      self, old_vptr, new_vptr, (void *)ctor);
        trace_dump_brother_state(log_label, self);
    }
}

typedef void (*brother_move_fn_t)(void *self, const float *vec);
typedef void (*brother_fire_fn_t)(void *self);
typedef void (*brother_draw_fn_t)(void *self, void *camera);
typedef void (*brother_on_shoot_fn_t)(void *self, float angle);
typedef void (*brother_swap_gun_fn_t)(void *self);
typedef int (*brother_can_swap_fn_t)(void *self);
typedef void (*brother_set_gun_fn_t)(void *self, unsigned char subtype, unsigned short id, int slot);
typedef void (*brother_set_gun_slot_fn_t)(void *self, unsigned int slot);
typedef void (*gun_shoot_event_fn_t)(void *self);
typedef void (*gunbros_show_pause_menu_fn_t)(void *self, unsigned char show);

typedef struct GameplayBrotherSlot {
    void *brother;
    const void *config;
    int score;
    int is_player;
} GameplayBrotherSlot;

static GameplayBrotherSlot g_gameplay_brothers[4];
static void *g_gameplay_primary_brother;
static void *g_gameplay_player_brother;
static void *g_gameplay_companion_brother;
static unsigned int g_gameplay_last_input_seq;
static volatile unsigned int g_gameplay_swap_request_seq;
static unsigned int g_gameplay_swap_handled_seq;
static void *g_gameplay_player_model_fix_target;
static uint32_t g_gameplay_player_model_fix_signature;

static void gameplay_begin_player_binding(void *player) {
    void *old_player = g_gameplay_player_brother;

    if (!old_player || old_player == player) {
        return;
    }

    memset(g_gameplay_brothers, 0, sizeof(g_gameplay_brothers));
    g_gameplay_primary_brother = NULL;
    g_gameplay_player_brother = NULL;
    g_gameplay_companion_brother = NULL;
    g_gameplay_player_model_fix_target = NULL;
    g_gameplay_player_model_fix_signature = 0u;
    sceClibPrintf("[FIX-CONTROL] new gameplay player binding old=%p new=%p; cleared stale actor registry\n",
                  old_player, player);
}

static float gb_clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

#define GAMEPLAY_RAD_TO_DEG 57.29577951308232f

static brother_move_fn_t gameplay_brother_on_move_fn(void) {
    static brother_move_fn_t fn;
    if (!fn) {
        fn = (brother_move_fn_t)(uintptr_t)so_symbol(&so_mod, "_ZN8CBrother6OnMoveERK4vec2");
        if (!fn) {
            sceClibPrintf("[FIX-CONTROL] CBrother::OnMove symbol missing\n");
        }
    }
    return fn;
}

static brother_on_shoot_fn_t gameplay_brother_on_shoot_fn(void) {
    static brother_on_shoot_fn_t fn;
    if (!fn) {
        fn = (brother_on_shoot_fn_t)(uintptr_t)so_symbol(&so_mod, "_ZN8CBrother7OnShootEf");
        if (!fn) {
            sceClibPrintf("[FIX-CONTROL] CBrother::OnShoot symbol missing\n");
        }
    }
    return fn;
}

static brother_fire_fn_t gameplay_brother_on_shoot_stop_fn(void) {
    static brother_fire_fn_t fn;
    if (!fn) {
        fn = (brother_fire_fn_t)(uintptr_t)so_symbol(&so_mod, "_ZN8CBrother11OnShootStopEv");
        if (!fn) {
            sceClibPrintf("[REBUILD-FIRE] CBrother::OnShootStop symbol missing\n");
        }
    }
    return fn;
}

static brother_swap_gun_fn_t gameplay_player_swap_animation_fn(void) {
    static brother_swap_gun_fn_t fn;
    if (!fn) {
        fn = (brother_swap_gun_fn_t)(uintptr_t)so_symbol(&so_mod, "_ZN7CPlayer9OnSwapGunEv");
        if (!fn) {
            sceClibPrintf("[FIX-CONTROL] CPlayer::OnSwapGun symbol missing\n");
        }
    }
    return fn;
}

static brother_can_swap_fn_t gameplay_brother_can_swap_guns_fn(void) {
    static brother_can_swap_fn_t fn;
    if (!fn) {
        fn = (brother_can_swap_fn_t)(uintptr_t)so_symbol(&so_mod, "_ZN8CBrother11CanSwapGunsEv");
        if (!fn) {
            sceClibPrintf("[FIX-CONTROL] CBrother::CanSwapGuns symbol missing\n");
        }
    }
    return fn;
}

void gunbros_request_player_gun_swap(void) {
    /* Triangle is a gameplay action, never a deferred action from menus. */
    if (!trace_is_game_range(g_live_gunbros_self, 0x138) ||
        gunbros_state(g_live_gunbros_self) != 8) return;
    g_gameplay_swap_request_seq++;
    sceClibPrintf("[FIX-CONTROL] gun swap request seq=%u\n",
                  g_gameplay_swap_request_seq);
}

static gunbros_show_pause_menu_fn_t gameplay_gunbros_show_pause_menu_fn(void) {
    static gunbros_show_pause_menu_fn_t fn;
    if (!fn) {
        fn = (gunbros_show_pause_menu_fn_t)(uintptr_t)so_symbol(&so_mod, "_ZN8CGunBros13ShowPauseMenuEb");
        if (!fn) {
            sceClibPrintf("[FIX-CONTROL] CGunBros::ShowPauseMenu symbol missing\n");
        }
    }
    return fn;
}

void gunbros_request_pause_menu(void) {
    gunbros_show_pause_menu_fn_t show_pause = gameplay_gunbros_show_pause_menu_fn();
    void *self = g_live_gunbros_self;

    if (!show_pause || !trace_is_game_range(self, 0x3a4u)) {
        if (trace_allow_ex("gameplay-pause/no-state", 8, 180)) {
            sceClibPrintf("[FIX-CONTROL] pause skipped self=%p show_pause=%p\n",
                          self, (void *)show_pause);
        }
        return;
    }

    show_pause(self, 1);
    if (trace_allow_ex("gameplay-pause/show", 8, 180)) {
        sceClibPrintf("[FIX-CONTROL] pause menu requested self=%p\n", self);
    }
}

static brother_set_gun_fn_t gameplay_brother_set_gun_fn(void) {
    static brother_set_gun_fn_t fn;
    if (!fn) {
        fn = (brother_set_gun_fn_t)(uintptr_t)so_symbol(&so_mod, "_ZN8CBrother6SetGunEhti");
        if (!fn) {
            sceClibPrintf("[FIX-CONTROL] CBrother::SetGun(subtype,id,slot) symbol missing\n");
        }
    }
    return fn;
}

static brother_set_gun_slot_fn_t gameplay_brother_set_gun_slot_fn(void) {
    static brother_set_gun_slot_fn_t fn;
    if (!fn) {
        fn = (brother_set_gun_slot_fn_t)(uintptr_t)so_symbol(&so_mod, "_ZN8CBrother6SetGunEj");
        if (!fn) {
            sceClibPrintf("[FIX-CONTROL] CBrother::SetGun(slot) symbol missing\n");
        }
    }
    return fn;
}


static gun_shoot_event_fn_t gameplay_gun_shoot_stop_fn(void) {
    static gun_shoot_event_fn_t fn;
    if (!fn) {
        fn = (gun_shoot_event_fn_t)(uintptr_t)so_symbol(&so_mod, "_ZN4CGun11OnShootStopEv");
        if (!fn) {
            sceClibPrintf("[REBUILD-FIRE] CGun::OnShootStop symbol missing\n");
        }
    }
    return fn;
}

static unsigned int gameplay_player_active_gun_index(void *brother);
static unsigned int gameplay_gun_slot_from_ptr(void *brother, const void *gun);

static int gameplay_gun_can_fire_direct(void *gun) {
    void *bullet_pool;
    void *bullet_template;
    if (!trace_is_game_range(gun, 0xf4u)) {
        return 0;
    }
    bullet_template = *(void **)(void *)((unsigned char *)gun + 0x2c);
    bullet_pool = *(void **)(void *)((unsigned char *)gun + 0x78);
    return trace_is_game_ptr(bullet_template) && trace_is_game_range(bullet_pool, sizeof(void *)) &&
           trace_is_game_ptr(*(void **)(void *)bullet_pool);
}

static void trace_gun_fire(void *gun) {
    typedef void *(*script_get_export_fn_t)(const void *self, unsigned char export_id);
    typedef void (*gun_fire_bullet_fn_t)(void *self, int mode, int projectile,
                                         float angle_min, float angle_max,
                                         float spread, unsigned char flags);
    static script_get_export_fn_t get_export;
    static gun_fire_bullet_fn_t fire_bullet;
    static int looked_up;
    void *export_code = NULL;
    void *script = NULL;

    if (!looked_up) {
        get_export = (script_get_export_fn_t)(uintptr_t)
            so_symbol(&so_mod, "_ZNK18CScriptInterpreter17GetExportFunctionEh");
        fire_bullet = (gun_fire_bullet_fn_t)(uintptr_t)
            so_symbol(&so_mod, "_ZN4CGun10FireBulletEiifffh");
        looked_up = 1;
    }

    if (trace_is_game_range(gun, 0xf4u)) {
        script = *(void **)(void *)((unsigned char *)gun + 0x44u);
        if (get_export) {
            export_code = get_export((unsigned char *)gun + 0x40u, 1u);
        }
    }

    if (export_code) {
        (void)SO_CONTINUE(int, h_gun_fire, gun);
        return;
    }
    if (fire_bullet && gameplay_gun_can_fire_direct(gun)) {
        fire_bullet(gun, 0, 0, 0.0f, 0.0f, 0.0f, 0u);
        if (trace_allow_ex("CGun::Fire/missing-export-fallback", 24, 180)) {
            sceClibPrintf("[FIX-FIRE] native projectile fallback gun=%p script=%p export1=%p template=%p pool=%p hand=%u\n",
                          gun, script, export_code,
                          *(void **)(void *)((unsigned char *)gun + 0x2cu),
                          *(void **)(void *)((unsigned char *)gun + 0x78u),
                          (unsigned int)*((unsigned char *)gun + 0x8cu));
        }
        return;
    }

    if (trace_allow_ex("CGun::Fire/missing-export-not-ready", 24, 180)) {
        sceClibPrintf("[FIX-FIRE] fire deferred gun=%p script=%p export1=%p direct_ready=%d fn=%p\n",
                      gun, script, export_code,
                      gameplay_gun_can_fire_direct(gun), fire_bullet);
    }
    (void)SO_CONTINUE(int, h_gun_fire, gun);
}

static void gameplay_dump_gun_state(const char *tag, void *brother, void *gun) {
    unsigned char *g = (unsigned char *)gun;
    void *bullet_template = NULL;
    void *bullet_pool = NULL;
    void *pool_first = NULL;
    int timer_a = 0;
    int timer_b = 0;
    unsigned int hand = 0xffu;
    unsigned int cfg_idx = gameplay_player_active_gun_index(brother);
    unsigned int gun_slot = gameplay_gun_slot_from_ptr(brother, gun);

    if (trace_is_game_range(gun, 0xf4u)) {
        bullet_template = *(void **)(void *)(g + 0x2c);
        bullet_pool = *(void **)(void *)(g + 0x78);
        if (trace_is_game_range(bullet_pool, sizeof(void *))) {
            pool_first = *(void **)(void *)bullet_pool;
        }
        timer_a = *(int *)(void *)(g + 0x7c);
        timer_b = *(int *)(void *)(g + 0x84);
        hand = *(unsigned char *)(void *)(g + 0x8c);
    }

    sceClibPrintf("[DIAG-FIRE] %s brother=%p gun=%p cfg_idx=%u gun_slot=%u tmpl=%p pool=%p pool0=%p timers=(%d,%d) hand=%u direct_ok=%d\n",
                  tag ? tag : "(null)", brother, gun, cfg_idx, gun_slot,
                  bullet_template, bullet_pool, pool_first, timer_a, timer_b,
                  hand, gameplay_gun_can_fire_direct(gun));
}

static uint32_t gameplay_player_model_config_signature(const unsigned char *cfg) {
    static const unsigned int armor_id_offsets[4] = { 0x30u, 0x38u, 0x40u, 0x48u };
    uint32_t sig = 2166136261u;
    unsigned int i;

    if (!trace_is_player_config_for_brother(cfg)) {
        return 0;
    }

    for (i = 0; i < 4u; ++i) {
        unsigned int off = armor_id_offsets[i];
        unsigned int id = (unsigned int)(cfg[off] | (cfg[off + 1u] << 8));
        unsigned int subtype = cfg[off + 2u];

        sig ^= (uint32_t)(id + 0x9e37u * (i + 1u));
        sig *= 16777619u;
        sig ^= (uint32_t)(subtype + 0x85ebu * (i + 1u));
        sig *= 16777619u;
    }

    return sig ? sig : 1u;
}

static void gameplay_repair_player_model(void *brother, const char *source) {
    unsigned char *cfg;
    uint32_t signature;

    if (!brother_is_player_object(brother)) {
        return;
    }

    cfg = *(unsigned char **)(void *)((unsigned char *)brother + BROTHER_OFF_CFG);
    signature = gameplay_player_model_config_signature(cfg);
    if (g_gameplay_player_model_fix_target == brother &&
        g_gameplay_player_model_fix_signature == signature) {
        return;
    }
    g_gameplay_player_model_fix_target = brother;
    g_gameplay_player_model_fix_signature = signature;
    if (trace_allow("gameplay-player-model-fix")) {
        sceClibPrintf("[FIX-RENDER] kept executable PLAYER model source=%s target=%p cfg=%p signature=%08x; no guessed SetLegs reset\n",
                      source ? source : "?", brother, cfg,
                      (unsigned int)signature);
        trace_dump_brother_state("player-model-config", brother);
        trace_dump_player_config_summary("player-model-config/config", cfg);
    }
}

static int gameplay_brother_score_candidate(void *brother, const void *config, const void *progress) {
    int score = 1;
    uintptr_t live = (uintptr_t)g_live_gunbros_self;
    uintptr_t cfg = (uintptr_t)config;
    uintptr_t prog = (uintptr_t)progress;

    if (!trace_is_game_range(brother, BROTHER_SIZE_GUESS) || !brother_has_bound_state(brother)) {
        return 0;
    }
    if (brother_is_player_object(brother)) {
        score += 100;
    }
    if (live && cfg >= live && cfg < live + 0x2000u) {
        score += 8;
    }
    if (live && prog >= live && prog < live + 0x3000u) {
        score += 8;
    }
    if (trace_is_player_config_for_brother(config)) {
        score += 4;
    }
    return score;
}

static void gameplay_register_brother(void *brother, const void *config, const void *progress, const char *source) {
    int score = gameplay_brother_score_candidate(brother, config, progress);
    int is_player = brother_is_player_object(brother);
    int i;
    int slot = -1;
    int lowest = 0;

    if (score <= 0) {
        return;
    }

    for (i = 0; i < 4; ++i) {
        if (g_gameplay_brothers[i].brother == brother) {
            slot = i;
            break;
        }
        if (!g_gameplay_brothers[i].brother && slot < 0) {
            slot = i;
        }
        if (g_gameplay_brothers[i].score < g_gameplay_brothers[lowest].score) {
            lowest = i;
        }
    }
    if (slot < 0) {
        slot = lowest;
    }

    g_gameplay_brothers[slot].brother = brother;
    g_gameplay_brothers[slot].config = config;
    g_gameplay_brothers[slot].score = score;
    g_gameplay_brothers[slot].is_player = is_player;

    if (is_player) {
        if (g_gameplay_player_brother != brother && trace_allow("gameplay-player-brother")) {
            sceClibPrintf("[FIX-CONTROL] local PLAYER from %s -> %p score=%d cfg=%p progress=%p vptr=%p\n",
                          source ? source : "?", brother, score, config, progress,
                          trace_is_game_range(brother, sizeof(void *)) ? *(void **)brother : NULL);
            trace_dump_brother_state("gameplay-player", brother);
        }
        g_gameplay_player_brother = brother;
        g_gameplay_primary_brother = brother;
        gameplay_repair_player_model(brother, source);
        return;
    }

    if (!g_gameplay_companion_brother || score >= gameplay_brother_score_candidate(g_gameplay_companion_brother, NULL, NULL)) {
        g_gameplay_companion_brother = brother;
    }

    /* Fallback only: never let a companion replace a known CPlayer target. */
    if (!g_gameplay_player_brother &&
        (!g_gameplay_primary_brother || score >= gameplay_brother_score_candidate(g_gameplay_primary_brother, NULL, NULL))) {
        if (g_gameplay_primary_brother != brother && trace_allow("gameplay-primary-brother")) {
            sceClibPrintf("[FIX-CONTROL] fallback companion from %s -> %p score=%d cfg=%p progress=%p vptr=%p\n",
                          source ? source : "?", brother, score, config, progress,
                          trace_is_game_range(brother, sizeof(void *)) ? *(void **)brother : NULL);
            trace_dump_brother_state("gameplay-fallback-companion", brother);
        }
        g_gameplay_primary_brother = brother;
    }
}

static void *gameplay_find_brother_target(void) {
    int i;

    if (trace_is_game_range(g_gameplay_player_brother, BROTHER_SIZE_GUESS) &&
        brother_has_bound_state(g_gameplay_player_brother) &&
        brother_is_player_object(g_gameplay_player_brother)) {
        return g_gameplay_player_brother;
    }

    for (i = 0; i < 4; ++i) {
        void *b = g_gameplay_brothers[i].brother;
        if (g_gameplay_brothers[i].is_player &&
            trace_is_game_range(b, BROTHER_SIZE_GUESS) &&
            brother_has_bound_state(b) &&
            brother_is_player_object(b)) {
            g_gameplay_player_brother = b;
            g_gameplay_primary_brother = b;
            return b;
        }
    }
    if (trace_allow_ex("gameplay-no-verified-player", 8, 240)) {
        sceClibPrintf("[FIX-CONTROL] waiting for verified CPlayer; candidate=%p vptr=%p\n",
                      g_gameplay_primary_brother,
                      trace_is_game_range(g_gameplay_primary_brother, sizeof(void *)) ?
                      *(void **)g_gameplay_primary_brother : NULL);
    }
    return NULL;
}

static void gameplay_apply_move(void *brother, int dt, float nx, float ny) {
    unsigned char *base = (unsigned char *)brother;
    brother_move_fn_t on_move = gameplay_brother_on_move_fn();
    float before_x;
    float before_y;
    float after_x;
    float after_y;
    float raw[2];
    float mag2;

    (void)dt;

    if (!trace_is_game_range(brother, CPLAYER_SIZE_GUESS) ||
        !brother_is_player_object(brother)) {
        return;
    }

    nx = gb_clampf(nx, -1.0f, 1.0f);
    ny = gb_clampf(ny, -1.0f, 1.0f);
    mag2 = nx * nx + ny * ny;
    if (mag2 < 0.0025f) {
        return;
    }
    if (mag2 > 1.0f) {
        float inv_mag = 1.0f / sqrtf(mag2);
        nx *= inv_mag;
        ny *= inv_mag;
    }

    repair_brother_runtime_state(brother, "gameplay-control/player-move");
    base[0x785] = 1; /* active */
    base[0x786] = 1; /* visible */

    raw[0] = nx;
    raw[1] = ny;
    before_x = *(float *)(void *)(base + 0x708);
    before_y = *(float *)(void *)(base + 0x70c);
    if (on_move) {
        on_move(brother, raw);
    } else if (trace_allow_ex("gameplay-player-move-no-handler", 8, 180)) {
        sceClibPrintf("[FIX-CONTROL] PLAYER move deferred: OnMove unavailable target=%p\n", brother);
    }

    after_x = *(float *)(void *)(base + 0x708);
    after_y = *(float *)(void *)(base + 0x70c);
    if (trace_allow_ex("gameplay-player-move", 16, 120)) {
        sceClibPrintf("[FIX-CONTROL] PLAYER OnMove target=%p input=(%.2f,%.2f) pos=(%.1f,%.1f)->(%.1f,%.1f) vptr=%p\n",
                      brother, nx, ny, before_x, before_y, after_x, after_y,
                      trace_is_game_range(brother, sizeof(void *)) ? *(void **)brother : NULL);
    }
}

static unsigned int gameplay_player_active_gun_index(void *brother) {
    void *cfg;
    if (!trace_is_game_range(brother, BROTHER_SIZE_GUESS)) {
        return 0xffu;
    }
    cfg = *(void **)(void *)((unsigned char *)brother + BROTHER_OFF_CFG);
    if (!trace_is_game_range(cfg, 0x50u)) {
        return 0xffu;
    }
    return *(unsigned char *)(void *)((unsigned char *)cfg + 0x4c);
}

static unsigned int gameplay_gun_config_subtype(const void *cfg, unsigned int slot) {
    if (slot >= BROTHER_GUN_SLOT_COUNT || !trace_is_game_range(cfg, 0x50u)) {
        return 0xffu;
    }
    return *(const unsigned char *)(const void *)((const unsigned char *)cfg + 0x12u + slot * 8u);
}

static unsigned int gameplay_gun_config_id(const void *cfg, unsigned int slot) {
    const unsigned char *c = (const unsigned char *)cfg;

    if (slot >= BROTHER_GUN_SLOT_COUNT || !trace_is_game_range(cfg, 0x50u)) {
        return 0;
    }
    return (unsigned int)(c[0x10u + slot * 8u] |
                          (c[0x11u + slot * 8u] << 8));
}

static int gameplay_gun_config_slot_valid(const void *cfg, unsigned int slot) {
    return gameplay_gun_config_subtype(cfg, slot) != 0xffu;
}

static void *gameplay_brother_gun_slot_ptr(void *brother, unsigned int slot) {
    if (slot >= BROTHER_GUN_SLOT_COUNT || !trace_is_game_range(brother, BROTHER_SIZE_GUESS)) {
        return NULL;
    }
    return (void *)((unsigned char *)brother + BROTHER_GUN_SLOT_BASE + slot * BROTHER_GUN_SLOT_SIZE);
}

static unsigned int gameplay_gun_slot_from_ptr(void *brother, const void *gun) {
    unsigned int slot;

    if (!trace_is_game_range(brother, BROTHER_SIZE_GUESS)) {
        return 0xffu;
    }

    for (slot = 0; slot < BROTHER_GUN_SLOT_COUNT; ++slot) {
        if (gameplay_brother_gun_slot_ptr(brother, slot) == gun) {
            return slot;
        }
    }
    return 0xffu;
}

static void *gameplay_current_equipped_gun(void *brother, const char *source, unsigned int *slot_out, int repair) {
    unsigned char *base = (unsigned char *)brother;
    void *cfg;
    void *current;
    void *expected;
    unsigned int cfg_slot;
    unsigned int current_slot;

    if (slot_out) {
        *slot_out = 0xffu;
    }
    if (!trace_is_game_range(brother, BROTHER_SIZE_GUESS)) {
        return NULL;
    }

    cfg = *(void **)(void *)(base + BROTHER_OFF_CFG);
    current = *(void **)(void *)(base + BROTHER_OFF_GUNCTX);
    cfg_slot = gameplay_player_active_gun_index(brother);
    current_slot = gameplay_gun_slot_from_ptr(brother, current);

    if (cfg_slot >= BROTHER_GUN_SLOT_COUNT ||
        !gameplay_gun_config_slot_valid(cfg, cfg_slot)) {
        if (slot_out) {
            *slot_out = current_slot;
        }
        return current;
    }

    expected = gameplay_brother_gun_slot_ptr(brother, cfg_slot);
    if (slot_out) {
        *slot_out = cfg_slot;
    }

    if (current != expected && trace_is_game_range(expected, BROTHER_GUN_SLOT_SIZE)) {
        void *before = current;
        int expected_ready = gameplay_gun_can_fire_direct(expected);
        brother_set_gun_slot_fn_t set_slot =
            repair && expected_ready ? gameplay_brother_set_gun_slot_fn() : NULL;

        if (set_slot) {
            set_slot(brother, cfg_slot);
            current = *(void **)(void *)(base + BROTHER_OFF_GUNCTX);
        }
        if (repair && expected_ready && current != expected) {
            *(void **)(void *)(base + BROTHER_OFF_GUNCTX) = expected;
            current = expected;
        }

        if (trace_allow_ex("gameplay-equipped-gun-refresh", 20, 180)) {
            sceClibPrintf("[FIX-FIRE] equipped gun refresh source=%s target=%p cfg_slot=%u current_slot=%u gun=%p->%p expected=%p expected_ready=%d set_slot=%d subtypes=(%u,%u)\n",
                          source ? source : "?", brother, cfg_slot, current_slot,
                          before, current, expected, expected_ready,
                          set_slot != NULL,
                          gameplay_gun_config_subtype(cfg, 0),
                          gameplay_gun_config_subtype(cfg, 1));
        }
    }

    return current;
}

enum {
    GAMEPLAY_PENDING_CONTENT_MAX = 24,
    GAMEPLAY_CONTENT_RETRY_FRAMES = 30,
};

typedef struct GameplayPendingContent {
    unsigned int used;
    unsigned int object_type;
    unsigned int id;
    unsigned int subtype;
    uint32_t requested_frame;
} GameplayPendingContent;

static GameplayPendingContent g_gameplay_pending_content[GAMEPLAY_PENDING_CONTENT_MAX];
static unsigned int g_gameplay_pending_content_replace;

static int gameplay_content_request_due(unsigned int object_type,
                                        unsigned int id,
                                        unsigned int subtype) {
    GameplayPendingContent *entry = NULL;
    uint32_t frame = g_gunbros_render_frame;
    unsigned int i;

    for (i = 0; i < GAMEPLAY_PENDING_CONTENT_MAX; ++i) {
        GameplayPendingContent *candidate = &g_gameplay_pending_content[i];
        if (candidate->used && candidate->object_type == object_type &&
            candidate->id == id && candidate->subtype == subtype) {
            if ((uint32_t)(frame - candidate->requested_frame) <
                GAMEPLAY_CONTENT_RETRY_FRAMES) {
                return 0;
            }
            entry = candidate;
            break;
        }
        if (!candidate->used && !entry) {
            entry = candidate;
        }
    }

    if (!entry) {
        entry = &g_gameplay_pending_content[
            g_gameplay_pending_content_replace++ % GAMEPLAY_PENDING_CONTENT_MAX];
    }
    entry->used = 1u;
    entry->object_type = object_type;
    entry->id = id;
    entry->subtype = subtype;
    entry->requested_frame = frame;
    return 1;
}

static void gameplay_content_request_resolved(unsigned int object_type,
                                              unsigned int id,
                                              unsigned int subtype) {
    unsigned int i;

    for (i = 0; i < GAMEPLAY_PENDING_CONTENT_MAX; ++i) {
        GameplayPendingContent *entry = &g_gameplay_pending_content[i];
        if (entry->used && entry->object_type == object_type &&
            entry->id == id && entry->subtype == subtype) {
            entry->used = 0u;
            return;
        }
    }
}

static int gameplay_object_ref_resolves(unsigned int object_type,
                                        unsigned int id,
                                        unsigned int subtype,
                                        int reject_zero_zero,
                                        const char *source,
                                        const char *label) {
    void *safe_self;
    void *obj = NULL;
    uint16_t flat = 0;

    if (subtype == 0xffu || (reject_zero_zero && id == 0u && subtype == 0u)) {
        if (trace_allow_ex(label, 12, 180)) {
            sceClibPrintf("[FIX-GOBJ] object ref rejected source=%s type=%u id=%u subtype=%u\n",
                          source ? source : "?", object_type, id, subtype);
        }
        return 0;
    }

    safe_self = gunbros_registry_self_or_live(g_live_gunbros_self,
                                              label,
                                              object_type,
                                              id);
    if (!safe_self || !h_gunbros_get_game_object_pack_const.addr) {
        return 0;
    }

    obj = trace_gunbros_get_game_object_pack_const(safe_self,
                                                   object_type,
                                                   id,
                                                   subtype);
    if (trace_is_game_range(obj, sizeof(void *))) {
        if (object_type != GUNBROS_OBJECT_TYPE_GUN) {
            gameplay_content_request_resolved(object_type, id, subtype);
        }
        return 1;
    }

    if (h_gunbros_flatten_object_index_const.addr &&
        h_gunbros_load_game_object_req.addr &&
        gameplay_content_request_due(object_type, id, subtype) &&
        trace_gunbros_flatten_object_index_const(safe_self,
                                                 object_type,
                                                 id,
                                                 subtype,
                                                 &flat)) {
        trace_gunbros_load_game_object_req(safe_self,
                                           object_type,
                                           flat,
                                           1);
        obj = trace_gunbros_get_game_object_pack_const(safe_self,
                                                       object_type,
                                                       id,
                                                       subtype);
        if (trace_is_game_range(obj, sizeof(void *))) {
            if (object_type != GUNBROS_OBJECT_TYPE_GUN) {
                gameplay_content_request_resolved(object_type, id, subtype);
            }
            if (trace_allow_ex(label, 12, 180)) {
                sceClibPrintf("[FIX-GOBJ] resolved object after load source=%s type=%u id=%u subtype=%u flat=%u obj=%p\n",
                              source ? source : "?", object_type, id, subtype,
                              (unsigned int)flat, obj);
            }
            return 1;
        }
    }

    if (trace_allow_ex(label, 12, 180)) {
        sceClibPrintf("[FIX-GOBJ] object ref unresolved source=%s type=%u id=%u subtype=%u obj=%p live=%p\n",
                      source ? source : "?", object_type, id, subtype, obj, safe_self);
    }
    return 0;
}

static int gameplay_gun_ref_resolves(unsigned int id, unsigned int subtype, const char *source) {
    return gameplay_object_ref_resolves(GUNBROS_OBJECT_TYPE_GUN,
                                        id,
                                        subtype,
                                        0,
                                        source,
                                        "gameplay-gun-ref");
}

static void gameplay_request_gun_dependencies(unsigned int id,
                                               unsigned int subtype,
                                               const char *source) {
    void *safe_self;
    uint16_t flat = 0u;

    if (subtype == 0xffu) {
        return;
    }

    safe_self = gunbros_registry_self_or_live(g_live_gunbros_self,
                                              "gameplay-gun-dependencies",
                                              GUNBROS_OBJECT_TYPE_GUN,
                                              id);
    if (!safe_self || !h_gunbros_flatten_object_index_const.addr ||
        !h_gunbros_load_game_object_req.addr ||
        !gameplay_content_request_due(GUNBROS_OBJECT_TYPE_GUN,
                                      id, subtype) ||
        !trace_gunbros_flatten_object_index_const(safe_self,
                                                  GUNBROS_OBJECT_TYPE_GUN,
                                                  id,
                                                  subtype,
                                                  &flat)) {
        return;
    }
    trace_gunbros_load_game_object_req(safe_self,
                                       GUNBROS_OBJECT_TYPE_GUN,
                                       flat,
                                       1u);
    if (trace_allow_ex("gameplay-gun-dependencies/request", 16, 180)) {
        sceClibPrintf("[FIX-GUN] requested full gun dependencies source=%s id=%u subtype=%u flat=%u\n",
                      source ? source : "?", id, subtype,
                      (unsigned int)flat);
    }
}

static int gameplay_armor_ref_resolves(unsigned int id, unsigned int subtype, const char *source) {
    void *safe_self;
    void *templ;
    uint16_t flat = 0;

    if (subtype == 0xffu) {
        return 0;
    }

    safe_self = gunbros_registry_self_or_live(g_live_gunbros_self,
                                              "gameplay-armor-ref",
                                              GUNBROS_OBJECT_TYPE_ARMOR,
                                              id);
    if (!safe_self || !h_gunbros_get_game_object_pack_const.addr) {
        return 0;
    }

    templ = trace_gunbros_get_game_object_pack_const(
        safe_self, GUNBROS_OBJECT_TYPE_ARMOR, id, subtype);
    if (trace_is_game_range(templ, 0x170u) &&
        trace_armor_template_validate(templ)) {
        gameplay_content_request_resolved(GUNBROS_OBJECT_TYPE_ARMOR,
                                          id, subtype);
        return 1;
    }
    if (h_gunbros_flatten_object_index_const.addr &&
        h_gunbros_load_game_object_req.addr &&
        gameplay_content_request_due(GUNBROS_OBJECT_TYPE_ARMOR,
                                     id, subtype) &&
        trace_gunbros_flatten_object_index_const(safe_self,
                                                 GUNBROS_OBJECT_TYPE_ARMOR,
                                                 id,
                                                 subtype,
                                                 &flat)) {
        trace_gunbros_load_game_object_req(safe_self,
                                           GUNBROS_OBJECT_TYPE_ARMOR,
                                           flat,
                                           1u);
        templ = trace_gunbros_get_game_object_pack_const(
            safe_self, GUNBROS_OBJECT_TYPE_ARMOR, id, subtype);
    }
    return trace_is_game_range(templ, 0x170u);
}

static void gameplay_request_configured_content(const void *config,
                                                const char *source) {
    static const unsigned int armor_id_offsets[4] = {
        0x30u, 0x38u, 0x40u, 0x48u
    };
    const unsigned char *cfg = (const unsigned char *)config;
    unsigned int requested = 0u;
    unsigned int unresolved = 0u;
    unsigned int slot;

    if (!trace_is_player_config_for_brother(config)) {
        return;
    }
    for (slot = 0u; slot < BROTHER_GUN_SLOT_COUNT; ++slot) {
        unsigned int off = 0x10u + slot * 8u;
        unsigned int subtype = cfg[off + 2u];
        unsigned int id;

        if (subtype == 0xffu) {
            continue;
        }
        id = *(const uint16_t *)(const void *)(cfg + off);
        ++requested;
        gameplay_request_gun_dependencies(id, subtype, source);
        if (!gameplay_gun_ref_resolves(id, subtype, source)) {
            ++unresolved;
        }
    }

    for (slot = 0u; slot < 4u; ++slot) {
        unsigned int off = armor_id_offsets[slot];
        unsigned int subtype = cfg[off + 2u];
        unsigned int id;

        if (subtype == 0xffu) {
            continue;
        }
        id = *(const uint16_t *)(const void *)(cfg + off);
        ++requested;
        if (!gameplay_armor_ref_resolves(id, subtype, source)) {
            ++unresolved;
        }
    }

    if (trace_allow_ex("CPlayer::Bind/configured-content", 24, 180)) {
        sceClibPrintf("[FIX-GOBJ] requested configured content source=%s config=%p refs=%u unresolved=%u\n",
                      source ? source : "?", config, requested, unresolved);
    }
}
static int trace_armor_template_validate(const void *self) {
    if (!trace_is_game_range(self, 0x170u)) {
        if (trace_allow_ex("CArmor::Template::Validate/null", 24, 180)) {
            sceClibPrintf("[FIX-ARMOR] Template::Validate skipped missing template=%p\n",
                          self);
        }
        return 0;
    }

    return SO_CONTINUE(int, h_armor_template_validate, self);
}

static int gameplay_find_first_resolved_gun_ref(unsigned short *out_id,
                                                unsigned char *out_subtype,
                                                const char *source) {
    static gunbros_unflatten_object_index_fn_t unflatten_fn;
    void *safe_self;
    unsigned int count;
    unsigned int flat;

    if (out_id) {
        *out_id = 0;
    }
    if (out_subtype) {
        *out_subtype = 0xffu;
    }

    safe_self = gunbros_registry_self_or_live(g_live_gunbros_self,
                                              "gameplay-find-gun/live-registry",
                                              GUNBROS_OBJECT_TYPE_GUN,
                                              0);
    if (!safe_self ||
        !h_gunbros_get_object_count_const.addr) {
        return 0;
    }

    if (!unflatten_fn) {
        unflatten_fn = (gunbros_unflatten_object_index_fn_t)(uintptr_t)so_symbol(
            &so_mod,
            "_ZNK8CGunBros20UnFlattenObjectIndexE14GameObjectTypetRtRh");
        if (!unflatten_fn) {
            if (trace_allow("gameplay-find-gun/unflatten-symbol-missing")) {
                sceClibPrintf("[FIX-GUNBROS] gameplay gun scan cannot resolve UnFlattenObjectIndex\n");
            }
            return 0;
        }
    }

    count = trace_gunbros_get_object_count_const(safe_self, GUNBROS_OBJECT_TYPE_GUN);
    if (count > 256u) {
        count = 256u;
    }

    for (flat = 0; flat < count; ++flat) {
        uint16_t id = 0;
        unsigned char subtype = 0xffu;

        if (!unflatten_fn(safe_self,
                          GUNBROS_OBJECT_TYPE_GUN,
                          flat,
                          &id,
                          &subtype)) {
            continue;
        }
        if (gameplay_gun_ref_resolves(id, subtype, source)) {
            if (out_id) {
                *out_id = id;
            }
            if (out_subtype) {
                *out_subtype = subtype;
            }
            return 1;
        }
    }

    return 0;
}

static void gameplay_rehydrate_brother_active_gun(void *brother, const char *source) {
    unsigned char *base = (unsigned char *)brother;
    unsigned char *cfg;
    unsigned int slot;
    unsigned int active_slot;
    unsigned int runtime_slot;
    unsigned int first_bound_slot = BROTHER_GUN_SLOT_COUNT;
    unsigned int bound_count = 0u;
    int is_player;
    int config_changed = 0;
    int active_resolved = 0;
    void *before_gun;
    void *after_gun;
    brother_set_gun_fn_t set_gun;
    brother_set_gun_slot_fn_t set_slot;

    if (!trace_is_game_range(brother, BROTHER_SIZE_GUESS)) {
        return;
    }

    cfg = *(unsigned char **)(void *)(base + BROTHER_OFF_CFG);
    if (!trace_is_game_range(cfg, 0x50u)) {
        return;
    }

    is_player = brother_is_player_object(brother);
    if (is_player) {
        config_changed = player_config_repair_loadout_defaults(cfg, source);
    }
    active_slot = cfg[0x4cu] < BROTHER_GUN_SLOT_COUNT ? cfg[0x4cu] : 0u;

    set_gun = gameplay_brother_set_gun_fn();
    if (!set_gun) {
        return;
    }

    before_gun = *(void **)(void *)(base + BROTHER_OFF_GUNCTX);
    for (slot = 0u; slot < BROTHER_GUN_SLOT_COUNT; ++slot) {
        unsigned int bind_slot = slot;
        unsigned int id = gameplay_gun_config_id(cfg, bind_slot);
        unsigned int subtype = gameplay_gun_config_subtype(cfg, bind_slot);
        if (subtype == 0xffu ||
            !gameplay_gun_ref_resolves(id, subtype, source)) {
            continue;
        }
        set_gun(brother, (unsigned char)subtype, (unsigned short)id,
                (int)bind_slot);
        if (gameplay_gun_can_fire_direct(
                gameplay_brother_gun_slot_ptr(brother, bind_slot))) {
            gameplay_content_request_resolved(GUNBROS_OBJECT_TYPE_GUN,
                                              id, subtype);
        }
        if (bind_slot == active_slot) {
            active_resolved = 1;
        }
        if (first_bound_slot >= BROTHER_GUN_SLOT_COUNT) {
            first_bound_slot = bind_slot;
        }
        bound_count++;
    }

    if (first_bound_slot >= BROTHER_GUN_SLOT_COUNT) {
        if (is_player && config_changed) {
            vita_offline_profile_request_save();
        }
        if (trace_allow_ex("gameplay-rehydrate-gun/no-ref", 12, 180)) {
            sceClibPrintf("[FIX-FIRE] gun rehydrate skipped source=%s brother=%p cfg=%p; no configured gun resolved\n",
                          source ? source : "?", brother, cfg);
        }
        return;
    }
    runtime_slot = active_resolved ? active_slot : first_bound_slot;

    set_slot = gameplay_brother_set_gun_slot_fn();
    if (set_slot) {
        set_slot(brother, runtime_slot);
        if (is_player && !active_resolved) {
            /* CBrother::SetGun(slot) also writes config+0x4c. A temporary
             * starter-gun fallback must never become the saved selection. */
            cfg[0x4cu] = (unsigned char)active_slot;
        }
    }
    if (is_player && config_changed) {
        vita_offline_profile_request_save();
    }

    after_gun = active_resolved ?
        gameplay_current_equipped_gun(brother, "rehydrate-active-gun",
                                      NULL, 1) :
        *(void **)(void *)(base + BROTHER_OFF_GUNCTX);
    if (trace_allow_ex("gameplay-rehydrate-gun", 24, 180)) {
        sceClibPrintf("[FIX-FIRE] rehydrated guns source=%s brother=%p saved_active=%u runtime_active=%u deferred=%d bound=%u gun=%p->%p direct_ok=%d cfg=%p\n",
                      source ? source : "?", brother, active_slot,
                      runtime_slot, !active_resolved, bound_count,
                      before_gun, after_gun,
                      gameplay_gun_can_fire_direct(after_gun), cfg);
        trace_dump_player_config_summary("rehydrate-gun/config", cfg);
        gameplay_dump_gun_state("rehydrate-active-gun", brother, after_gun);
    }
}

static void gameplay_force_player_gun(unsigned int subtype, unsigned int id, const char *source) {
    void *brother = gameplay_find_brother_target();
    void *cfg;
    void *before_gun;
    void *after_gun;
    unsigned int slot;
    brother_set_gun_fn_t set_gun = gameplay_brother_set_gun_fn();
    brother_set_gun_slot_fn_t set_slot = gameplay_brother_set_gun_slot_fn();

    if (subtype == 0xffu) {
        sceClibPrintf("[FIX-CONTROL] force gun skipped source=%s id=%u subtype=none\n",
                      source ? source : "?", id);
        return;
    }

    if (!trace_is_game_range(brother, BROTHER_SIZE_GUESS)) {
        sceClibPrintf("[FIX-CONTROL] force gun skipped source=%s id=%u subtype=%u no player\n",
                      source ? source : "?", id, subtype);
        return;
    }

    if (!gameplay_gun_ref_resolves(id, subtype, source)) {
        sceClibPrintf("[FIX-CONTROL] force gun skipped source=%s id=%u subtype=%u unresolved object\n",
                      source ? source : "?", id, subtype);
        return;
    }

    cfg = *(void **)(void *)((unsigned char *)brother + BROTHER_OFF_CFG);
    slot = 0u;

    if (trace_is_game_range(cfg, 0x50u)) {
        unsigned char *c = (unsigned char *)cfg;
        unsigned int off;
        (void)player_config_repair_loadout_defaults(c, source);
        slot = c[0x4cu] < BROTHER_GUN_SLOT_COUNT ? c[0x4cu] : 0u;
        off = 0x10u + slot * 8u;
        c[off + 0] = (unsigned char)(id & 0xffu);
        c[off + 1] = (unsigned char)((id >> 8) & 0xffu);
        c[off + 2] = (unsigned char)(subtype & 0xffu);
        c[0x4c] = (unsigned char)slot;
        (void)vita_offline_loadout_save(c,
                                        source ? source : "force-gun");
        vita_offline_profile_request_save();
    }

    before_gun = *(void **)(void *)((unsigned char *)brother + 0x210);
    if (set_gun) {
        set_gun(brother, (unsigned char)subtype, (unsigned short)id, (int)slot);
    }
    if (set_slot) {
        set_slot(brother, slot);
    }
    after_gun = gameplay_current_equipped_gun(brother, "force-gun", NULL, 1);
    repair_brother_runtime_state(brother, "gameplay-control/force-gun");
    sceClibPrintf("[FIX-CONTROL] force active gun source=%s target=%p slot=%u id=%u subtype=%u gun=%p->%p cfg=%p\n",
                  source ? source : "?", brother, slot, id, subtype, before_gun, after_gun, cfg);
}

static int gameplay_install_store_gun_ref(const unsigned char *ref,
                                          unsigned int id,
                                          unsigned int subtype,
                                          unsigned int equip_now,
                                          const char *source) {
    void *brother;
    unsigned char *cfg;
    unsigned int slot;
    void *before_gun;
    void *after_gun;
    brother_set_gun_fn_t set_gun = gameplay_brother_set_gun_fn();
    brother_set_gun_slot_fn_t set_slot = gameplay_brother_set_gun_slot_fn();

    if (subtype == 0xffu || !trace_is_game_range(ref, 8u)) {
        return 0;
    }
    if (!equip_now) {
        if (trace_allow_ex("gameplay-install-gun/not-equip", 12, 180)) {
            sceClibPrintf("[FIX-STORE] install gun skipped live target source=%s id=%u subtype=%u equip=0\n",
                          source ? source : "?", id, subtype);
        }
        return 0;
    }

    brother = gameplay_find_brother_target();
    if (!trace_is_game_range(brother, BROTHER_SIZE_GUESS)) {
        sceClibPrintf("[FIX-STORE] install gun skipped source=%s id=%u subtype=%u no gameplay target\n",
                      source ? source : "?", id, subtype);
        return 0;
    }
    if (!brother_is_player_object(brother)) {
        sceClibPrintf("[FIX-STORE] install gun skipped source=%s target=%p id=%u subtype=%u not player vptr=%p\n",
                      source ? source : "?", brother, id, subtype,
                      trace_is_game_range(brother, sizeof(void *)) ? *(void **)brother : NULL);
        return 0;
    }
    if (!gameplay_gun_ref_resolves(id, subtype, source)) {
        sceClibPrintf("[FIX-STORE] install gun skipped source=%s id=%u subtype=%u unresolved object\n",
                      source ? source : "?", id, subtype);
        return 0;
    }

    cfg = *(unsigned char **)(void *)((unsigned char *)brother + BROTHER_OFF_CFG);
    if (!trace_is_player_config_for_brother(cfg)) {
        sceClibPrintf("[FIX-STORE] install gun skipped source=%s target=%p id=%u subtype=%u bad cfg=%p\n",
                      source ? source : "?", brother, id, subtype, cfg);
        return 0;
    }

    player_config_install_gun_store_ref(cfg, ref, id, subtype, equip_now, &slot);
    if (slot >= BROTHER_GUN_SLOT_COUNT) {
        sceClibPrintf("[FIX-STORE] install gun skipped source=%s target=%p id=%u subtype=%u no config slot\n",
                      source ? source : "?", brother, id, subtype);
        return 0;
    }

    before_gun = *(void **)(void *)((unsigned char *)brother + BROTHER_OFF_GUNCTX);
    if (set_gun) {
        set_gun(brother, (unsigned char)subtype, (unsigned short)id, (int)slot);
    }
    if (equip_now && set_slot) {
        set_slot(brother, slot);
    }

    repair_brother_runtime_state(brother, "store-acquire/install-gun");
    after_gun = gameplay_current_equipped_gun(brother, "store-acquire/install-gun", NULL, 1);
    sceClibPrintf("[FIX-STORE] installed gameplay gun source=%s target=%p slot=%u id=%u subtype=%u equip=%u gun=%p->%p direct_ok=%d cfg=%p\n",
                  source ? source : "?", brother, slot, id, subtype,
                  equip_now, before_gun, after_gun,
                  gameplay_gun_can_fire_direct(after_gun), cfg);
    trace_dump_player_config_summary("store-acquire/gameplay-config", cfg);
    return 1;
}

static int gameplay_apply_configured_player_gun(void *config, const char *source) {
    void *brother = gameplay_find_brother_target();
    unsigned char *cfg = (unsigned char *)config;
    unsigned int slot;
    unsigned int id = 0u;
    unsigned int subtype = 0xffu;
    unsigned int bind_slot;
    void *before_gun;
    void *after_gun;
    brother_set_gun_fn_t set_gun = gameplay_brother_set_gun_fn();
    brother_set_gun_slot_fn_t set_slot = gameplay_brother_set_gun_slot_fn();

    if (!trace_is_player_config_for_brother(config)) {
        return 0;
    }

    (void)player_config_repair_loadout_defaults(config, source);
    slot = cfg[0x4cu] < BROTHER_GUN_SLOT_COUNT ? cfg[0x4cu] : 0u;
    id = gameplay_gun_config_id(cfg, slot);
    subtype = gameplay_gun_config_subtype(cfg, slot);
    if (subtype == 0xffu || !gameplay_gun_ref_resolves(id, subtype, source)) {
        return 0;
    }
    if (!trace_is_game_range(brother, BROTHER_SIZE_GUESS) ||
        *(void **)(void *)((unsigned char *)brother + BROTHER_OFF_CFG) != config ||
        !brother_is_player_object(brother)) {
        if (trace_allow_ex("gameplay-apply-config-gun/deferred", 16, 180)) {
            sceClibPrintf("[FIX-STORE] configured gun deferred source=%s config=%p slot=%u ref=%u:%u live=%p live_cfg=%p\n",
                          source ? source : "?", config, slot, id, subtype,
                          brother,
                          trace_is_game_range(brother, BROTHER_SIZE_GUESS) ?
                          *(void **)(void *)((unsigned char *)brother + BROTHER_OFF_CFG) : NULL);
        }
        return 0;
    }
    if (!set_gun || !set_slot) {
        return 0;
    }

    before_gun = *(void **)(void *)((unsigned char *)brother + BROTHER_OFF_GUNCTX);
    for (bind_slot = 0u; bind_slot < BROTHER_GUN_SLOT_COUNT; ++bind_slot) {
        unsigned int bind_id = gameplay_gun_config_id(cfg, bind_slot);
        unsigned int bind_subtype = gameplay_gun_config_subtype(cfg, bind_slot);
        if (bind_subtype != 0xffu &&
            gameplay_gun_ref_resolves(bind_id, bind_subtype, source)) {
            set_gun(brother, (unsigned char)bind_subtype,
                    (unsigned short)bind_id, (int)bind_slot);
        }
    }
    set_slot(brother, slot);
    after_gun = gameplay_current_equipped_gun(brother, "store-equip/apply", NULL, 1);
    repair_brother_runtime_state(brother, "store-equip/apply");

    sceClibPrintf("[FIX-STORE] applied configured gun source=%s target=%p config=%p slot=%u ref=%u:%u gun=%p->%p direct_ok=%d\n",
                  source ? source : "?", brother, config, slot, id, subtype,
                  before_gun, after_gun, gameplay_gun_can_fire_direct(after_gun));
    return after_gun == gameplay_brother_gun_slot_ptr(brother, slot);
}

static int gameplay_config_slot_resolves(const void *cfg, unsigned int slot, const char *source) {
    unsigned int id;
    unsigned int subtype;

    if (!trace_is_game_range(cfg, 0x50u) || slot >= BROTHER_GUN_SLOT_COUNT) {
        return 0;
    }

    id = gameplay_gun_config_id(cfg, slot);
    subtype = gameplay_gun_config_subtype(cfg, slot);
    if (subtype == 0xffu) {
        return 0;
    }

    return gameplay_gun_ref_resolves(id, subtype, source);
}

#define GUNBROS_POSTGAME_GUNREF_BASE 0x160u
#define GUNBROS_POSTGAME_GUNREF_SIZE 8u
#define GUNBROS_INLINE_PLAYER_CONFIG_OFF 0x258u

static unsigned int gunbros_postgame_gun_ref_id(const void *state, unsigned int slot) {
    const unsigned char *s = (const unsigned char *)state;
    unsigned int off = GUNBROS_POSTGAME_GUNREF_BASE + slot * GUNBROS_POSTGAME_GUNREF_SIZE;

    if (slot >= BROTHER_GUN_SLOT_COUNT || !trace_is_game_range(state, 0x170u)) {
        return 0;
    }
    return (unsigned int)(s[off + 0] | (s[off + 1] << 8));
}

static unsigned int gunbros_postgame_gun_ref_subtype(const void *state, unsigned int slot) {
    const unsigned char *s = (const unsigned char *)state;
    unsigned int off = GUNBROS_POSTGAME_GUNREF_BASE + slot * GUNBROS_POSTGAME_GUNREF_SIZE;

    if (slot >= BROTHER_GUN_SLOT_COUNT || !trace_is_game_range(state, 0x170u)) {
        return 0xffu;
    }
    return (unsigned int)s[off + 2];
}

static void gunbros_postgame_set_gun_ref(void *state,
                                         unsigned int slot,
                                         unsigned int id,
                                         unsigned int subtype) {
    unsigned char *s = (unsigned char *)state;
    unsigned int off = GUNBROS_POSTGAME_GUNREF_BASE + slot * GUNBROS_POSTGAME_GUNREF_SIZE;

    if (slot >= BROTHER_GUN_SLOT_COUNT || !trace_is_game_range(state, 0x170u)) {
        return;
    }

    s[off + 0] = (unsigned char)(id & 0xffu);
    s[off + 1] = (unsigned char)((id >> 8) & 0xffu);
    s[off + 2] = (unsigned char)(subtype & 0xffu);
}

static int gunbros_postgame_ref_resolves(const void *state,
                                         unsigned int slot,
                                         unsigned short *out_id,
                                         unsigned char *out_subtype,
                                         const char *source) {
    unsigned int id = gunbros_postgame_gun_ref_id(state, slot);
    unsigned int subtype = gunbros_postgame_gun_ref_subtype(state, slot);

    if (out_id) {
        *out_id = (unsigned short)id;
    }
    if (out_subtype) {
        *out_subtype = (unsigned char)subtype;
    }
    if (subtype == 0xffu) {
        return 0;
    }
    return gameplay_object_ref_resolves(GUNBROS_OBJECT_TYPE_GUN,
                                        id,
                                        subtype,
                                        0,
                                        source,
                                        "postgame-mastery-gun-ref");
}

static int gunbros_postgame_try_config_ref(const void *cfg,
                                           unsigned int slot,
                                           unsigned short *out_id,
                                           unsigned char *out_subtype,
                                           const char *source) {
    unsigned int id;
    unsigned int subtype;

    if (!trace_is_game_range(cfg, 0x50u) || slot >= BROTHER_GUN_SLOT_COUNT) {
        return 0;
    }

    id = gameplay_gun_config_id(cfg, slot);
    subtype = gameplay_gun_config_subtype(cfg, slot);
    if (subtype == 0xffu ||
        !gameplay_object_ref_resolves(GUNBROS_OBJECT_TYPE_GUN,
                                      id,
                                      subtype,
                                      0,
                                      source,
                                      "postgame-config-gun-ref")) {
        return 0;
    }
    if (out_id) {
        *out_id = (unsigned short)id;
    }
    if (out_subtype) {
        *out_subtype = (unsigned char)subtype;
    }
    return 1;
}

static int gunbros_repair_postgame_mastery_gun_refs(const char *source) {
    unsigned char *state = (unsigned char *)g_live_gunbros_self;
    unsigned short id[2] = { 0, 0 };
    unsigned char subtype[2] = { 0xffu, 0xffu };
    int valid[2] = { 0, 0 };
    int changed = 0;
    void *brother;
    const void *cfg = NULL;
    void *local_config = NULL;
    void *local_progress = NULL;
    unsigned int slot;

    if (!trace_is_game_range(state, 0x170u)) {
        if (trace_allow_ex("CMenuUpgradePopup::ShowForGuns/no-state", 8, 180)) {
            sceClibPrintf("[FIX-POSTGAME] ShowForGuns skipped source=%s no live CGunBros state=%p\n",
                          source ? source : "?", state);
        }
        return 0;
    }

    for (slot = 0; slot < BROTHER_GUN_SLOT_COUNT; ++slot) {
        valid[slot] = gunbros_postgame_ref_resolves(state, slot,
                                                    &id[slot],
                                                    &subtype[slot],
                                                    source);
    }
    vita_offline_profile_get_best_local_pair(&local_config, &local_progress);
    (void)local_progress;
    if (trace_is_player_config_for_brother(local_config)) {
        (void)vita_offline_loadout_restore_config(
            local_config, "postgame/current-loadout");
        gameplay_request_configured_content(local_config,
                                             "postgame/current-loadout");
        cfg = local_config;
    }

    if (!trace_is_game_range(cfg, 0x50u)) {
        brother = gameplay_find_brother_target();
        if (trace_is_game_range(brother, BROTHER_SIZE_GUESS)) {
            gameplay_rehydrate_brother_active_gun(brother, source);
            cfg = *(const void **)(void *)((unsigned char *)brother + BROTHER_OFF_CFG);
        }
        if (!trace_is_game_range(cfg, 0x50u) &&
            trace_is_game_range(state + GUNBROS_INLINE_PLAYER_CONFIG_OFF, 0x50u)) {
            cfg = state + GUNBROS_INLINE_PLAYER_CONFIG_OFF;
        }

    }

    for (slot = 0; slot < BROTHER_GUN_SLOT_COUNT; ++slot) {
        unsigned short current_id;
        unsigned char current_subtype;

        if (gunbros_postgame_try_config_ref(cfg, slot,
                                            &current_id,
                                            &current_subtype,
                                            source)) {
            if (!valid[slot] || id[slot] != current_id ||
                subtype[slot] != current_subtype) {
                changed = 1;
            }
            id[slot] = current_id;
            subtype[slot] = current_subtype;
            valid[slot] = 1;
        }
    }

    if (!valid[0] && !valid[1]) {
        if (gameplay_find_first_resolved_gun_ref(&id[0], &subtype[0], source)) {
            id[1] = id[0];
            subtype[1] = subtype[0];
            valid[0] = 1;
            valid[1] = 1;
            changed = 1;
        }
    } else if (valid[0] && !valid[1]) {
        id[1] = id[0];
        subtype[1] = subtype[0];
        valid[1] = 1;
        changed = 1;
    } else if (!valid[0] && valid[1]) {
        id[0] = id[1];
        subtype[0] = subtype[1];
        valid[0] = 1;
        changed = 1;
    }

    if (!valid[0] || !valid[1]) {
        if (trace_allow_ex("CMenuUpgradePopup::ShowForGuns/no-gun", 8, 180)) {
            sceClibPrintf("[FIX-POSTGAME] ShowForGuns skipped source=%s refs=[%u:%u,%u:%u] cfg=%p; no resolved gun template\n",
                          source ? source : "?", (unsigned int)id[0], (unsigned int)subtype[0],
                          (unsigned int)id[1], (unsigned int)subtype[1], cfg);
        }
        return 0;
    }

    for (slot = 0; slot < BROTHER_GUN_SLOT_COUNT; ++slot) {
        if (gunbros_postgame_gun_ref_id(state, slot) != id[slot] ||
            gunbros_postgame_gun_ref_subtype(state, slot) != subtype[slot]) {
            gunbros_postgame_set_gun_ref(state, slot, id[slot], subtype[slot]);
            changed = 1;
        }
    }

    if (changed && trace_allow_ex("CMenuUpgradePopup::ShowForGuns/repair", 16, 180)) {
        sceClibPrintf("[FIX-POSTGAME] repaired mastery gun refs source=%s state=%p refs=[%u:%u,%u:%u] cfg=%p\n",
                      source ? source : "?", state,
                      (unsigned int)id[0], (unsigned int)subtype[0],
                      (unsigned int)id[1], (unsigned int)subtype[1], cfg);
    }
    return 1;
}

static void trace_menu_upgrade_popup_show_for_guns(unsigned char mode) {
    if (!gunbros_repair_postgame_mastery_gun_refs("CMenuUpgradePopup::ShowForGuns")) {
        return;
    }
    (void)SO_CONTINUE(int, h_menu_upgrade_popup_show_for_guns, mode);
}

static int gameplay_force_swap_using_config(void *brother, const char *source) {
    unsigned char *cfg;
    unsigned int before_idx;
    unsigned int next_idx;
    unsigned int id;
    unsigned int subtype;
    void *before_gun;
    void *after_gun;
    brother_set_gun_fn_t set_gun = gameplay_brother_set_gun_fn();
    brother_set_gun_slot_fn_t set_slot = gameplay_brother_set_gun_slot_fn();

    if (!trace_is_game_range(brother, BROTHER_SIZE_GUESS)) {
        return 0;
    }

    cfg = *(unsigned char **)(void *)((unsigned char *)brother + BROTHER_OFF_CFG);
    if (!trace_is_game_range(cfg, 0x50u)) {
        return 0;
    }

    before_idx = gameplay_player_active_gun_index(brother);
    if (before_idx >= BROTHER_GUN_SLOT_COUNT) {
        before_idx = 0;
    }
    next_idx = before_idx ^ 1u;
    if (!gameplay_config_slot_resolves(cfg, next_idx, source)) {
        return 0;
    }

    id = gameplay_gun_config_id(cfg, next_idx);
    subtype = gameplay_gun_config_subtype(cfg, next_idx);
    before_gun = gameplay_current_equipped_gun(brother, "swap-force-before", NULL, 1);

    cfg[0x4c] = (unsigned char)next_idx;
    if (set_gun) {
        set_gun(brother, (unsigned char)subtype, (unsigned short)id, (int)next_idx);
    }
    if (set_slot) {
        set_slot(brother, next_idx);
    }

    repair_brother_runtime_state(brother, "gameplay-control/gun-swap-force");
    after_gun = gameplay_current_equipped_gun(brother, "swap-force-after", NULL, 1);
    (void)vita_offline_loadout_save(cfg, "gameplay-force-swap");
    vita_offline_profile_request_save();
    sceClibPrintf("[FIX-CONTROL] gun swap repaired source=%s target=%p cfg_idx=%u->%u ref=%u:%u gun=%p->%p direct_ok=%d subtypes=(%u,%u)\n",
                  source ? source : "?", brother, before_idx, next_idx,
                  id, subtype, before_gun, after_gun,
                  gameplay_gun_can_fire_direct(after_gun),
                  gameplay_gun_config_subtype(cfg, 0),
                  gameplay_gun_config_subtype(cfg, 1));
    return 1;
}

static void gameplay_apply_pending_swap(void *brother) {
    unsigned int req=g_gameplay_swap_request_seq;
    if(req==g_gameplay_swap_handled_seq) return;
    g_gameplay_swap_handled_seq=req;
    if(!trace_is_game_range(g_live_gunbros_self,0x138) ||
       gunbros_state(g_live_gunbros_self)!=8 ||
       !trace_is_game_range(brother,CPLAYER_SIZE_GUESS) ||
       !brother_is_player_object(brother)) return;
    brother_swap_gun_fn_t animate=gameplay_player_swap_animation_fn();
    brother_can_swap_fn_t can_swap=gameplay_brother_can_swap_guns_fn();
    if(animate && can_swap && can_swap(brother)) animate(brother);
}

static void gameplay_apply_fire_stop(void *brother) {
    unsigned char *base = (unsigned char *)brother;
    void *gun;
    int was_shooting;

    if (!trace_is_game_range(brother, BROTHER_SIZE_GUESS)) {
        return;
    }

    gun = gameplay_current_equipped_gun(brother, "fire-stop", NULL, 1);
    was_shooting = base[0x788] || base[0x73c];

    if (was_shooting) {
        brother_fire_fn_t brother_stop = gameplay_brother_on_shoot_stop_fn();
        gun_shoot_event_fn_t gun_stop = gameplay_gun_shoot_stop_fn();

        if (base[0x788] && brother_stop) {
            brother_stop(brother);
        } else if (trace_is_game_range(gun, 0xf4u) && gun_stop) {
            gun_stop(gun);
        }

        base[0x788] = 0;
        base[0x73c] = 0;
        *(uint32_t *)(void *)(base + 0x724) = 0;

        if (trace_allow_ex("gameplay-fire-stop", 12, 180)) {
            sceClibPrintf("[REBUILD-FIRE] stop target=%p gun=%p flags(shoot=%u aux=%u)\n",
                          brother, gun, (unsigned int)base[0x788], (unsigned int)base[0x73c]);
        }
    } else {
        base[0x788] = 0;
        base[0x73c] = 0;
    }

}

static void gameplay_apply_fire(void *brother, int dt, float ax, float ay) {
    unsigned char *base = (unsigned char *)brother;
    brother_on_shoot_fn_t on_shoot = gameplay_brother_on_shoot_fn();
    void *gun;
    float angle;
    float angle_deg;
    float game_ay;
    float mag;
    unsigned int active_idx;

    (void)dt;

    if (!trace_is_game_range(brother, CPLAYER_SIZE_GUESS) ||
        !brother_is_player_object(brother)) {
        return;
    }

    ax = gb_clampf(ax, -1.0f, 1.0f);
    ay = gb_clampf(ay, -1.0f, 1.0f);
    mag = ax * ax + ay * ay;
    if (mag < 0.0025f) {
        ax = 1.0f;
        ay = 0.0f;
    } else if (mag > 1.0f) {
        float inv_mag = 1.0f / sqrtf(mag);
        ax *= inv_mag;
        ay *= inv_mag;
    }

    gun = gameplay_current_equipped_gun(brother, "fire", &active_idx, 1);
    if (!trace_is_game_range(gun, 0xf4u)) {
        gameplay_rehydrate_brother_active_gun(brother, "fire/no-current-gun");
        gun = gameplay_current_equipped_gun(brother, "fire-after-rehydrate", &active_idx, 1);
    }
    if (!trace_is_game_range(gun, 0xf4u)) {
        if (trace_allow_ex("gameplay-fire-no-gun", 8, 180)) {
            sceClibPrintf("[REBUILD-FIRE] deferred no-gun target=%p gun=%p vptr=%p\n",
                          brother, gun,
                          trace_is_game_range(brother, sizeof(void *)) ? *(void **)brother : NULL);
            gameplay_dump_gun_state("no-gun", brother, gun);
        }
        return;
    }

    repair_brother_runtime_state(brother, "gameplay-control/player-fire");
    base[0x785] = 1;
    base[0x786] = 1;

    game_ay = -ay;
    angle = atan2f(game_ay, ax);
    angle_deg = angle * GAMEPLAY_RAD_TO_DEG;
    if (on_shoot) {
        on_shoot(brother, angle);
    } else {
        *(float *)(void *)(base + 0x718) = angle;
        if (trace_allow_ex("gameplay-player-fire-no-handler", 8, 180)) {
            sceClibPrintf("[REBUILD-FIRE] PLAYER fire deferred: OnShoot unavailable target=%p gun=%p\n",
                          brother, gun);
        }
        return;
    }

    if (trace_allow_ex("gameplay-player-fire", 30, 90)) {
        sceClibPrintf("[REBUILD-FIRE] PLAYER OnShoot target=%p gun=%p cfg_idx=%u aim=(%.2f,%.2f) angle=%.3f deg=%.1f flags(shoot=%u aux=%u) vptr=%p\n",
                      brother, gun, active_idx, ax, game_ay, angle, angle_deg,
                      (unsigned int)base[0x788], (unsigned int)base[0x73c],
                      trace_is_game_range(brother, sizeof(void *)) ? *(void **)brother : NULL);
        gameplay_dump_gun_state("after-on-shoot", brother, gun);
    }
}


static void gameplay_control_bridge_update(int dt) {
    GunBrosGameplayInputState st = g_gunbros_gameplay_input;
    void *brother;

    if (!st.move_active && !st.fire_active) {
        void *b = g_gameplay_player_brother;
        if (g_gameplay_swap_request_seq != g_gameplay_swap_handled_seq) {
            b = gameplay_find_brother_target();
        }
        if (trace_is_game_range(b, BROTHER_SIZE_GUESS)) {
            gameplay_apply_pending_swap(b);
            gameplay_apply_fire_stop(b);
        }
        return;
    }

    brother = gameplay_find_brother_target();
    if (!brother) {
        if (trace_allow_ex("gameplay-no-brother", 12, 120)) {
            sceClibPrintf("[FIX-CONTROL] no bound brother for input move=%d fire=%d seq=%u\n",
                          st.move_active, st.fire_active, st.seq);
        }
        return;
    }

    gameplay_apply_pending_swap(brother);

    if (st.seq != g_gameplay_last_input_seq && trace_allow_ex("gameplay-input-state", 16, 120)) {
        sceClibPrintf("[FIX-CONTROL] input state target=%p is_player=%d companion=%p move=%d(%.2f,%.2f src=%d) fire=%d(%.2f,%.2f src=%d) seq=%u\n",
                      brother, brother_is_player_object(brother), g_gameplay_companion_brother,
                      st.move_active, st.move_x, st.move_y, st.move_source,
                      st.fire_active, st.fire_x, st.fire_y, st.fire_source, st.seq);
        g_gameplay_last_input_seq = st.seq;
    }

    if (st.move_active) {
        gameplay_apply_move(brother, dt, st.move_x, st.move_y);
    }
    if (st.fire_active) {
        gameplay_apply_fire(brother, dt, st.fire_x, st.fire_y);
    } else {
        gameplay_apply_fire_stop(brother);
    }
}

static int gameplay_is_registered_brother(void *brother) {
    int i;

    if (brother == g_gameplay_player_brother || brother == g_gameplay_companion_brother) {
        return 1;
    }

    for (i = 0; i < 4; ++i) {
        if (g_gameplay_brothers[i].brother == brother && g_gameplay_brothers[i].score > 0) {
            return 1;
        }
    }
    return 0;
}

static int gameplay_should_skip_ghost_brother_draw(void *self) {
    if (!trace_is_game_range(self, BROTHER_SIZE_GUESS)) {
        return 0;
    }

    if (brother_is_player_object(self) || brother_is_ai_object(self) || gameplay_is_registered_brother(self)) {
        return 0;
    }

    return 1;
}

static void continue_brother_draw_trace(void *self, void *camera) {
    brother_draw_fn_t fn;

    kuKernelCpuUnrestrictedMemcpy((void *)h_brother_draw_trace.addr,
                                  h_brother_draw_trace.orig_instr,
                                  sizeof(h_brother_draw_trace.orig_instr));
    kuKernelFlushCaches((void *)h_brother_draw_trace.addr,
                        sizeof(h_brother_draw_trace.orig_instr));
    fn = (brother_draw_fn_t)(uintptr_t)(h_brother_draw_trace.thumb_addr ?
                                        h_brother_draw_trace.thumb_addr :
                                        h_brother_draw_trace.addr);
    fn(self, camera);
    kuKernelCpuUnrestrictedMemcpy((void *)h_brother_draw_trace.addr,
                                  h_brother_draw_trace.patch_instr,
                                  sizeof(h_brother_draw_trace.patch_instr));
    kuKernelFlushCaches((void *)h_brother_draw_trace.addr,
                        sizeof(h_brother_draw_trace.patch_instr));
}

static void trace_brother_draw_trace(void *self, void *camera) {
    if (gameplay_should_skip_ghost_brother_draw(self)) {
        if (trace_allow_ex("CBrother::Draw/ghost-skip", 20, 180)) {
            sceClibPrintf("[FIX-RENDER] skipped unregistered plain CBrother draw self=%p camera=%p vptr=%p player=%p companion=%p\n",
                          self, camera,
                          trace_is_game_range(self, sizeof(void *)) ? *(void **)self : NULL,
                          g_gameplay_player_brother, g_gameplay_companion_brother);
            trace_dump_brother_state("ghost-draw-skip", self);
        }
        return;
    }

    if (trace_allow_ex("CBrother::Draw/pass", 8, 300)) {
        sceClibPrintf("[DIAG-RENDER] CBrother::Draw pass self=%p camera=%p vptr=%p player=%d ai=%d registered=%d\n",
                      self, camera,
                      trace_is_game_range(self, sizeof(void *)) ? *(void **)self : NULL,
                      brother_is_player_object(self), brother_is_ai_object(self),
                      gameplay_is_registered_brother(self));
    }
    continue_brother_draw_trace(self, camera);
}


static int trace_player_bind_trace(void *self, void *map, const void *templ, const void *config, const void *progress) {
    const void *safe_config = config;
    const void *safe_progress = progress;
    void *local_config = NULL;
    void *local_progress = NULL;
    int ret;
    void *vptr_before = trace_is_game_range(self, sizeof(void *)) ? *(void **)self : NULL;

    gameplay_begin_player_binding(self);
    vita_offline_profile_get_best_local_pair(&local_config, &local_progress);
    if (!trace_is_player_config_for_brother(safe_config)) {
        safe_config = local_config;
    }
    if (!trace_is_player_progress_for_brother(safe_progress)) {
        safe_progress = local_progress;
    }
    vita_offline_profile_adopt_active((void *)safe_config,
                                      (void *)safe_progress,
                                      "CPlayer::Bind");
    (void)vita_offline_loadout_restore_config((void *)safe_config,
                                              "CPlayer::Bind/journal");
    (void)player_progress_ensure_native_tables((void *)safe_progress,
                                                "CPlayer::Bind");
    (void)player_config_repair_armor_slots((void *)safe_config,
                                           "CPlayer::Bind/pre");
    (void)player_config_repair_loadout_defaults((void *)safe_config,
                                                 "CPlayer::Bind/pre");
    gameplay_request_configured_content(safe_config,
                                        "CPlayer::Bind/pre");

    if (trace_allow("CPlayer::Bind/diag-enter")) {
        sceClibPrintf("[DIAG-PLAYER] enter CPlayer::Bind player=%p map=%p templ=%p config=%p->%p progress=%p->%p vptr_before=%p\n",
                      self, map, templ, config, safe_config, progress, safe_progress, vptr_before);
        trace_dump_player_config_summary("CPlayer::Bind/input", safe_config);
    }
    (void)promote_bound_object_to_player(self, "CPlayer::Bind/pre");
    ret = SO_CONTINUE(int, h_player_bind_trace, self, map, templ, safe_config, safe_progress);
    (void)promote_bound_object_to_player(self, "CPlayer::Bind/post");
    (void)vita_offline_loadout_restore_config((void *)safe_config,
                                              "CPlayer::Bind/post-journal");
    repair_brother_runtime_state(self, "CPlayer::Bind/runtime");
    gameplay_register_brother(self, safe_config, safe_progress, "CPlayer::Bind");
    gameplay_rehydrate_brother_active_gun(self, "CPlayer::Bind");
    gameplay_repair_player_model(self, "CPlayer::Bind");

    if (trace_allow("CPlayer::Bind/diag-leave")) {
        trace_dump_brother_state("CPlayer::Bind/after", self);
    }
    return ret;
}

static int trace_player_update_trace(void *self, int dt) {
    typedef int (*player_update_fn_t)(void *, int);
    int ret;

    if (promote_bound_object_to_player(self, "CPlayer::Update/pre") &&
        trace_is_game_range(self, CPLAYER_SIZE_GUESS)) {
        gameplay_register_brother(self,
                                  *(void **)(void *)((unsigned char *)self + BROTHER_OFF_CFG),
                                  NULL,
                                  "CPlayer::Update");
        gameplay_apply_pending_swap(self);
    }

    unsigned int slot_before=gameplay_player_active_gun_index(self);
    if (g_original_player_update_trace) {
        ret = ((player_update_fn_t)g_original_player_update_trace)(self, dt);
    } else {
        ret = SO_CONTINUE(int, h_player_update_trace, self, dt);
    }
    if (trace_is_game_range(self, CPLAYER_SIZE_GUESS)) {
        unsigned char *base = (unsigned char *)self;
        unsigned char *cfg = *(unsigned char **)(void *)(base + BROTHER_OFF_CFG);
        if(trace_is_game_range(cfg,0x50u) && cfg[0x4c]<2 &&
           slot_before<2 && cfg[0x4c]!=slot_before) {
            (void)vita_offline_loadout_save(cfg,"gameplay-animated-swap");
            vita_offline_profile_request_save();
        }

        if ((g_gunbros_render_frame % GAMEPLAY_CONTENT_RETRY_FRAMES) == 0u &&
            trace_is_game_range(cfg, 0x50u) && cfg[0x4cu] < 2u) {
            unsigned int saved_slot = cfg[0x4cu];
            void *expected = gameplay_brother_gun_slot_ptr(self, saved_slot);
            void *current = *(void **)(void *)(base + BROTHER_OFF_GUNCTX);

            if (current != expected ||
                !gameplay_gun_can_fire_direct(expected)) {
                gameplay_request_configured_content(
                    cfg, "CPlayer::Update/loadout-retry");
                gameplay_rehydrate_brother_active_gun(
                    self, "CPlayer::Update/loadout-retry");
            }
        }

    }
    if (trace_allow_ex("CPlayer::Update/diag", 12, 240)) {
        trace_dump_brother_state("CPlayer::Update/after", self);
    }
    return ret;
}

static int trace_brother_bind_trace(void *self, void *map, const void *templ, const void *config, const void *progress) {
    const void *safe_config = config;
    const void *safe_progress = progress;
    void *vptr_before = NULL;
    int restore_player_loadout;
    int ret;

    if (trace_is_game_range(self, BROTHER_SIZE_GUESS)) {
        vptr_before = *(void **)self;
    }

    construct_brother_if_uninitialized(self, "CBrother::Bind/pre-ctor");

    if (!trace_is_player_config_for_brother(safe_config)) {
        safe_config = vita_default_config();
    }
    restore_player_loadout = brother_is_player_object(self) ||
                             self == g_gameplay_player_brother;
    if (restore_player_loadout) {
        (void)vita_offline_loadout_restore_config((void *)safe_config,
                                                  "CBrother::Bind/journal");
        (void)player_config_repair_loadout_defaults((void *)safe_config,
                                                     "CBrother::Bind");
    }
    if (!trace_is_player_progress_for_brother(safe_progress)) {
        safe_progress = vita_default_progress();
    }
    (void)player_progress_ensure_native_tables((void *)safe_progress,
                                                "CBrother::Bind");

    if (trace_allow("CBrother::Bind/diag-enter")) {
        sceClibPrintf("[DIAG-BIND] enter brother=%p map=%p templ=%p config=%p->%p progress=%p->%p vptr_before=%p\n",
                      self, map, templ, config, safe_config, progress, safe_progress, vptr_before);
        trace_dump_brother_state("CBrother::Bind/before", self);
    }

    ret = SO_CONTINUE(int, h_brother_bind_trace, self, map, templ, safe_config, safe_progress);

    if (restore_player_loadout) {
        (void)vita_offline_loadout_restore_config(
            (void *)safe_config, "CBrother::Bind/post-journal");
    }

    (void)player_model_apply_empty_slot_textures(
        self, safe_config, "CBrother::Bind/post");

    repair_brother_vptr_preserve_state(self, "CBrother::Bind/vptr-repair", vptr_before);
    repair_brother_runtime_state(self, "CBrother::Bind/runtime");
    gameplay_register_brother(self, safe_config, safe_progress, "CBrother::Bind");
    gameplay_rehydrate_brother_active_gun(self, "CBrother::Bind");

    if (trace_allow("CBrother::Bind/diag-leave")) {
        trace_dump_brother_state("CBrother::Bind/after", self);
    }

    return ret;
}

static int trace_brother_spawn_construct(void *self, const void *spawn_data) {
    int ret;
    construct_brother_if_uninitialized(self, "CBrother::Spawn/pre-state-fix");
    if (trace_allow("CBrother::Spawn/diag")) {
        sceClibPrintf("[DIAG-SPAWN] brother=%p spawn_data=%p\n", self, spawn_data);
        trace_dump_brother_state("CBrother::Spawn/before", self);
    }
    ret = SO_CONTINUE(int, h_brother_spawn_construct, self, spawn_data);
    repair_brother_vptr_preserve_state(self, "CBrother::Spawn/post-vptr-repair", NULL);
    repair_brother_runtime_state(self, "CBrother::Spawn/runtime");
    if (trace_is_game_range(self, BROTHER_SIZE_GUESS)) {
        gameplay_register_brother(self, *(void **)(void *)((unsigned char *)self + 0x780), NULL, "CBrother::Spawn");
    }
    if (trace_allow("CBrother::Spawn/diag-after")) {
        trace_dump_brother_state("CBrother::Spawn/after", self);
    }
    return ret;
}

static int trace_brother_respawn_trace(void *self, const void *respawn_data) {
    unsigned char *cfg_before = NULL;
    unsigned int saved_active_slot = BROTHER_GUN_SLOT_COUNT;
    int restore_player_loadout = 0;
    int ret;

    if (trace_is_game_range(self, BROTHER_SIZE_GUESS)) {
        cfg_before = *(unsigned char **)(void *)
            ((unsigned char *)self + BROTHER_OFF_CFG);
        restore_player_loadout =
            (brother_is_player_object(self) || self == g_gameplay_player_brother) &&
            trace_is_game_range(cfg_before, 0x50u) &&
            cfg_before[0x4cu] < BROTHER_GUN_SLOT_COUNT;
        if (restore_player_loadout) {
            saved_active_slot = cfg_before[0x4cu];
            gameplay_request_configured_content(cfg_before,
                                                "CBrother::Respawn/pre");
        }
    }

    ret = SO_CONTINUE(int, h_brother_respawn_trace, self, respawn_data);

    if (restore_player_loadout &&
        trace_is_game_range(self, BROTHER_SIZE_GUESS)) {
        unsigned char *cfg_after = *(unsigned char **)(void *)
            ((unsigned char *)self + BROTHER_OFF_CFG);

        if (trace_is_game_range(cfg_after, 0x50u)) {
            unsigned int native_slot = cfg_after[0x4cu];
            cfg_after[0x4cu] = (unsigned char)saved_active_slot;
            gameplay_request_configured_content(cfg_after,
                                                "CBrother::Respawn/post");
            gameplay_rehydrate_brother_active_gun(self,
                                                  "CBrother::Respawn/post");
            if (trace_allow_ex("CBrother::Respawn/loadout-restore", 16, 180)) {
                sceClibPrintf("[FIX-GUN] respawn restored equipped slot brother=%p cfg=%p->%p native=%u saved=%u data=%p\n",
                              self, cfg_before, cfg_after, native_slot,
                              saved_active_slot, respawn_data);
            }
        }
    }

    return ret;
}

static void patch_brother_refresh_sequence_null_state(void) {
    static const char refresh_symbol[] = "_ZN8CBrother15RefreshSequenceEv";
    uintptr_t refresh = so_symbol(&so_mod, refresh_symbol);
    uintptr_t patch_at;
    uintptr_t resume_at;
    uintptr_t early_return_at;
    uintptr_t cave;
    uint32_t code[9];

    if (!refresh) {
        sceClibPrintf("[PATCH] CBrother::RefreshSequence symbol not found\n");
        return;
    }

    patch_at = refresh + 0x4;         /* add r5, r0, #0x310 */
    resume_at = refresh + 0xc;        /* mov r1, r5 */
    early_return_at = refresh + 0x20; /* pop {r4, r5, r6, pc} */
    cave = alloc_patch_cave(sizeof(code));
    if (!cave) {
        return;
    }

    code[0] = 0xe5903318;          /* ldr r3, [r0, #0x318] (peek CScriptState*) */
    code[1] = 0xe3530000;          /* cmp r3, #0 */
    code[2] = 0x1a000001;          /* bne normal */
    code[3] = 0xe51ff004;          /* ldr pc, [pc, #-4] */
    code[4] = (uint32_t)early_return_at;
    code[5] = 0xe2805e31;          /* normal: add r5, r0, #0x310 (orig +0x4) */
    code[6] = 0xe1a04000;          /* mov r4, r0 (orig +0x8) */
    code[7] = 0xe51ff004;          /* ldr pc, [pc, #-4] */
    code[8] = (uint32_t)resume_at;

    kuKernelCpuUnrestrictedMemcpy((void *)cave, code, sizeof(code));
    kuKernelFlushCaches((void *)cave, sizeof(code));
    hook_arm(patch_at, cave);

    sceClibPrintf("[PATCH] CBrother::RefreshSequence null script-state guard at %p -> %p\n",
                  (void *)patch_at, (void *)cave);
}

static void patch_guard_inline_vcall(const char *symbol_name, uintptr_t crash_offset, const char *label) {
    uintptr_t func = so_symbol(&so_mod, symbol_name);
    uintptr_t patch_at;
    uintptr_t resume_at;
    uintptr_t cave;
    uint32_t mov_instr;
    uint32_t ldr_instr;
    uint32_t target_ldr_instr;
    uint32_t code[13];

    if (!func) {
        sceClibPrintf("[PATCH] %s symbol not found\n", symbol_name);
        return;
    }
    patch_at = func + crash_offset - 4;   /* mov lr, pc */
    resume_at = func + crash_offset + 4;  /* instruction right after the ldr */

    mov_instr = *(volatile uint32_t *)patch_at;
    ldr_instr = *(volatile uint32_t *)(patch_at + 4);

    if (mov_instr != 0xe1a0e00f) {
        sceClibPrintf("[PATCH] %s vtable guard (%s) skipped; unexpected instruction 0x%08x at %p\n",
                      symbol_name, label ? label : "", mov_instr, (void *)patch_at);
        return;
    }

    if ((ldr_instr & 0xff7ff000) != 0xe513f000) {
        sceClibPrintf("[PATCH] %s vtable guard (%s) skipped; unexpected ldr-pc instruction 0x%08x at %p\n",
                      symbol_name, label ? label : "", ldr_instr, (void *)(patch_at + 4));
        return;
    }

    target_ldr_instr = (ldr_instr & ~0x0000f000u) | 0x0000c000u; /* ldr ip, [r3, #imm] */

    cave = alloc_patch_cave(sizeof(code));
    if (!cave) {
        return;
    }

    code[0]  = 0xe3530498;          /* cmp r3, #0x98000000 */
    code[1]  = 0x3a000008;          /* blo skip */
    code[2]  = 0xe3530499;          /* cmp r3, #0x99000000 */
    code[3]  = 0x2a000006;          /* bhs skip */
    code[4]  = target_ldr_instr;    /* ldr ip, [r3, #imm] (same slot as original) */
    code[5]  = 0xe35c0498;          /* cmp ip, #0x98000000 */
    code[6]  = 0x3a000003;          /* blo skip */
    code[7]  = 0xe35c0499;          /* cmp ip, #0x99000000 */
    code[8]  = 0x2a000001;          /* bhs skip */
    code[9]  = 0xe59fe004;          /* ldr lr, [pc, #4] -> resume_at */
    code[10] = 0xe1a0f00c;          /* mov pc, ip */
    code[11] = 0xe51ff004;          /* skip: ldr pc, [pc, #-4] */
    code[12] = (uint32_t)resume_at;

    kuKernelCpuUnrestrictedMemcpy((void *)cave, code, sizeof(code));
    kuKernelFlushCaches((void *)cave, sizeof(code));
    hook_arm(patch_at, cave);

    sceClibPrintf("[PATCH] %s vtable guard (%s) at %p -> %p (ldr=0x%08x slotldr=0x%08x)\n",
                  symbol_name, label ? label : "", (void *)patch_at, (void *)cave,
                  ldr_instr, target_ldr_instr);
}

static void patch_guard_all_inline_vcalls_in_range(const char *symbol_name, uintptr_t size, const char *label) {
    uintptr_t func = so_symbol(&so_mod, symbol_name);
    uintptr_t i;
    int found = 0;

    if (!func) {
        sceClibPrintf("[PATCH] %s symbol not found\n", symbol_name);
        return;
    }

    for (i = 0; i + 8 <= size; i += 4) {
        uintptr_t addr = func + i;
        uint32_t w0 = *(volatile uint32_t *)addr;
        uint32_t w1 = *(volatile uint32_t *)(addr + 4);

        if (w0 == 0xe1a0e00f && (w1 & 0xff7ff000) == 0xe513f000) {
            patch_guard_inline_vcall(symbol_name, i + 4, label);
            found++;
        }
    }

    sceClibPrintf("[PATCH] %s: guarded %d inline vcall site(s) (%s)\n",
                  symbol_name, found, label ? label : "");
}

static void patch_level_onstart_bad_vtable_call(void) {
    patch_guard_all_inline_vcalls_in_range("_ZN6CLevel7OnStartEv", 0x740, "current-brother-vcall");
}

static void patch_render_queue_add_bad_vtable_call(void) {
    patch_guard_all_inline_vcalls_in_range("_ZN12CRenderQueue3AddEP9IDrawable", 0xf0, "embedded-brother-drawable-vcall");
}

static void patch_render_sort_compare_bad_vtable_call(void) {
    patch_guard_all_inline_vcalls_in_range("_Z7ComparePKvS0_", 0x84, "embedded-brother-sort-compare-vcall");
}

static void patch_render_queue_draw_bad_vtable_call(void) {
    patch_guard_all_inline_vcalls_in_range("_ZN12CRenderQueue4DrawEv", 0x3c0, "current-brother-draw-vcall");
}

static void patch_level_draw_enemy_health_bars_bad_vtable_call(void) {
    patch_guard_all_inline_vcalls_in_range("_ZN6CLevel19DrawEnemyHealthBarsEv", 0x348, "current-brother-healthbar-vcall");
}

static void patch_level_draw_brother_health_bar_bad_vtable_call(void) {
    patch_guard_all_inline_vcalls_in_range("_ZN6CLevel20DrawBrotherHealthBarEv", 0x23c, "current-brother-healthbar-vcall");
}

static void patch_input_pad_base_draw_bad_vtable_call(void) {
    patch_guard_all_inline_vcalls_in_range("_ZN9CInputPad4Base4DrawEv", 0x178, "input-pad-base-draw-vcall");
}

static void patch_level_add_object_bad_vtable(void) {
    static const char add_object_symbol[] =
        "_ZN6CLevel9AddObjectEP12ILevelObject";
    uintptr_t add_object = so_symbol(&so_mod, add_object_symbol);
    uintptr_t patch_at;
    uintptr_t resume_at;
    uintptr_t cave;
    uint32_t first_call_code[8];
    uint32_t second_call_code[9];
    uint32_t third_call_code[9];

    if (!add_object) {
        sceClibPrintf("[PATCH] CLevel::AddObject symbol not found\n");
        return;
    }

    patch_at = add_object + 0x14;  /* mov lr, pc; ldr pc, [r3, #0x24] */
    resume_at = add_object + 0x1c; /* mov r3, #0x47000 */
    cave = alloc_patch_cave(sizeof(first_call_code));
    if (!cave) {
        return;
    }

    first_call_code[0] = 0xe3530498;       /* cmp r3, #0x98000000 */
    first_call_code[1] = 0x3a000003;       /* blo skip */
    first_call_code[2] = 0xe3530499;       /* cmp r3, #0x99000000 */
    first_call_code[3] = 0x2a000001;       /* bhs skip */
    first_call_code[4] = 0xe59fe004;       /* ldr lr, [pc, #4] */
    first_call_code[5] = 0xe593f024;       /* ldr pc, [r3, #0x24] */
    first_call_code[6] = 0xe51ff004;       /* skip: ldr pc, [pc, #-4] */
    first_call_code[7] = (uint32_t)resume_at;

    kuKernelCpuUnrestrictedMemcpy((void *)cave, first_call_code, sizeof(first_call_code));
    kuKernelFlushCaches((void *)cave, sizeof(first_call_code));
    hook_arm(patch_at, cave);

    sceClibPrintf("[PATCH] CLevel::AddObject vtable guard #1 at %p -> %p\n",
                  (void *)patch_at, (void *)cave);

    patch_at = add_object + 0x48;  /* mov lr, pc; ldr pc, [r3, #0x74] */
    resume_at = add_object + 0x50; /* cmp r0, #0 */
    cave = alloc_patch_cave(sizeof(second_call_code));
    if (!cave) {
        return;
    }

    second_call_code[0] = 0xe3530498;      /* cmp r3, #0x98000000 */
    second_call_code[1] = 0x3a000003;      /* blo null_result */
    second_call_code[2] = 0xe3530499;      /* cmp r3, #0x99000000 */
    second_call_code[3] = 0x2a000001;      /* bhs null_result */
    second_call_code[4] = 0xe59fe008;      /* ldr lr, [pc, #8] */
    second_call_code[5] = 0xe593f074;      /* ldr pc, [r3, #0x74] */
    second_call_code[6] = 0xe3a00000;      /* null_result: mov r0, #0 */
    second_call_code[7] = 0xe51ff004;      /* ldr pc, [pc, #-4] */
    second_call_code[8] = (uint32_t)resume_at;

    kuKernelCpuUnrestrictedMemcpy((void *)cave, second_call_code, sizeof(second_call_code));
    kuKernelFlushCaches((void *)cave, sizeof(second_call_code));
    hook_arm(patch_at, cave);

    sceClibPrintf("[PATCH] CLevel::AddObject vtable guard #2 at %p -> %p\n",
                  (void *)patch_at, (void *)cave);

    patch_at = add_object + 0xc8;  /* mov lr, pc; ldr pc, [r3, #0x7c] */
    resume_at = add_object + 0xd0; /* cmp r0, #1 */
    cave = alloc_patch_cave(sizeof(third_call_code));
    if (!cave) {
        return;
    }

    third_call_code[0] = 0xe3530498;       /* cmp r3, #0x98000000 */
    third_call_code[1] = 0x3a000003;       /* blo null_result */
    third_call_code[2] = 0xe3530499;       /* cmp r3, #0x99000000 */
    third_call_code[3] = 0x2a000001;       /* bhs null_result */
    third_call_code[4] = 0xe59fe008;       /* ldr lr, [pc, #8] */
    third_call_code[5] = 0xe593f07c;       /* ldr pc, [r3, #0x7c] */
    third_call_code[6] = 0xe3a00000;       /* null_result: mov r0, #0 */
    third_call_code[7] = 0xe51ff004;       /* ldr pc, [pc, #-4] */
    third_call_code[8] = (uint32_t)resume_at;

    kuKernelCpuUnrestrictedMemcpy((void *)cave, third_call_code, sizeof(third_call_code));
    kuKernelFlushCaches((void *)cave, sizeof(third_call_code));
    hook_arm(patch_at, cave);

    sceClibPrintf("[PATCH] CLevel::AddObject vtable guard #3 at %p -> %p\n",
                  (void *)patch_at, (void *)cave);
}




static void patch_level_on_enemy_killed_missing_mission(void) {
    static const char enemy_killed_symbol[] = "_ZN6CLevel13OnEnemyKilledEPK6CEnemyPK12ILevelObject";
    uintptr_t fn = so_symbol(&so_mod, enemy_killed_symbol);
    uintptr_t patch_at;
    uintptr_t resume_at;
    uintptr_t cave;
    uint32_t code[7];
    uint32_t first;
    uint32_t second;

    if (!fn) {
        sceClibPrintf("[PATCH] CLevel::OnEnemyKilled mission-null guard: symbol not found\n");
        return;
    }

    patch_at = fn + 0x8b8;  /* ldr r3, [r0, #0x3c] after CGameFlow::GetMission */
    resume_at = fn + 0x8c0; /* first instruction after the 8-byte hook */
    first = *(volatile uint32_t *)patch_at;
    second = *(volatile uint32_t *)(patch_at + 4);
    if (first != 0xe590303cu || second != 0xe3530002u) {
        sceClibPrintf("[PATCH] CLevel::OnEnemyKilled mission-null guard skipped; unexpected words at %p: %08x %08x\n",
                      (void *)patch_at, first, second);
        return;
    }

    cave = alloc_patch_cave(sizeof(code));
    if (!cave) {
        return;
    }

    code[0] = 0xe3500000u;       /* cmp r0, #0 */
    code[1] = 0x1590303cu;       /* ldrne r3, [r0, #0x3c] */
    code[2] = 0x03a03000u;       /* moveq r3, #0 */
    code[3] = 0xe3530002u;       /* cmp r3, #2 (displaced +0x8bc) */
    code[4] = 0xe51ff004u;       /* ldr pc, [pc, #-4] */
    code[5] = (uint32_t)resume_at;
    code[6] = 0xe1a00000u;

    kuKernelCpuUnrestrictedMemcpy((void *)cave, code, sizeof(code));
    kuKernelFlushCaches((void *)cave, sizeof(code));
    hook_arm(patch_at, cave);
    kuKernelFlushCaches((void *)patch_at, 8);

    sceClibPrintf("[PATCH] CLevel::OnEnemyKilled mission-null guard at %p -> %p resume=%p\n",
                  (void *)patch_at, (void *)cave, (void *)resume_at);
}
static void patch_script_refresh_null_state(void) {
    static const char refresh_symbol[] = "_ZN18CScriptInterpreter7RefreshEv";
    uintptr_t refresh = so_symbol(&so_mod, refresh_symbol);
    uintptr_t patch_at;
    uintptr_t resume_at;
    uintptr_t cave;
    uint32_t code[11];
    uint32_t first;
    uint32_t second;

    if (!refresh) {
        sceClibPrintf("[PATCH] CScriptInterpreter::Refresh null-state guard: symbol not found\n");
        return;
    }

    patch_at = refresh + 0x04;
    resume_at = refresh + 0x10;
    first = *(volatile uint32_t *)patch_at;
    second = *(volatile uint32_t *)(patch_at + 4);
    if (first != 0xe5903030u || second != 0xe1a04000u) {
        sceClibPrintf("[PATCH] CScriptInterpreter::Refresh null-state guard skipped; unexpected words at %p: %08x %08x\n",
                      (void *)patch_at, first, second);
        return;
    }

    cave = alloc_patch_cave(sizeof(code));
    if (!cave) {
        return;
    }

    code[0] = 0xe3500000u;       /* cmp r0, #0 */
    code[1] = 0x0a000006u;       /* beq return_from_refresh */
    code[2] = 0xe5903030u;       /* ldr r3, [r0, #0x30] */
    code[3] = 0xe3530000u;       /* cmp r3, #0 */
    code[4] = 0x0a000003u;       /* beq return_from_refresh */
    code[5] = 0xe1a04000u;       /* mov r4, r0 */
    code[6] = 0xe1a00003u;       /* mov r0, r3 */
    code[7] = 0xe51ff004u;       /* ldr pc, [pc, #-4] */
    code[8] = (uint32_t)resume_at;
    code[9] = 0xe8bd8070u;       /* return_from_refresh: pop {r4,r5,r6,pc} */
    code[10] = 0xe1a00000u;      /* padding */

    kuKernelCpuUnrestrictedMemcpy((void *)cave, code, sizeof(code));
    kuKernelFlushCaches((void *)cave, sizeof(code));
    hook_arm(patch_at, cave);
    kuKernelFlushCaches((void *)patch_at, 8);

    sceClibPrintf("[PATCH] CScriptInterpreter::Refresh null-state guard at %p -> %p; words=%08x %08x\n",
                  (void *)patch_at, (void *)cave,
                  *(volatile uint32_t *)patch_at,
                  *(volatile uint32_t *)(patch_at + 4));
}

static void patch_script_export_missing_tables(void) {
    static const char get_export_symbol[] =
        "_ZNK18CScriptInterpreter17GetExportFunctionEh";
    static const char call_export_symbol[] =
        "_ZN18CScriptInterpreter18CallExportFunctionEh";
    static const char call_export_args_symbol[] =
        "_ZN18CScriptInterpreter18CallExportFunctionEhsss";
    uintptr_t get_export = so_symbol(&so_mod, get_export_symbol);
    uintptr_t call_export = so_symbol(&so_mod, call_export_symbol);
    uintptr_t call_export_args = so_symbol(&so_mod, call_export_args_symbol);
    uintptr_t patch_at;
    uintptr_t resume_at;
    uintptr_t epilogue_at;
    uintptr_t cave;
    uint32_t base_table_code[11];
    uint32_t lookup_code[20];
    uint32_t caller_code[12];

    if (!get_export) {
        sceClibPrintf("[PATCH] CScriptInterpreter::GetExportFunction symbol not found\n");
        return;
    }

    patch_at = get_export + 0x68;    /* ldr ip, [r3, #0x28] */
    resume_at = get_export + 0x70;   /* ldr r2, [r3, #0x10] */
    epilogue_at = get_export + 0x94; /* pop {r4, r5, r6, r7}; bx lr */
    cave = alloc_patch_cave(sizeof(base_table_code));
    if (!cave) {
        return;
    }

    base_table_code[0] = 0xe3530481;       /* cmp r3, #0x81000000 */
    base_table_code[1] = 0x3a000005;       /* blo invalid_return */
    base_table_code[2] = 0xe35304a0;       /* cmp r3, #0xa0000000 */
    base_table_code[3] = 0x2a000003;       /* bhs invalid_return */
    base_table_code[4] = 0xe593c028;       /* ldr ip, [r3, #0x28] (orig +0x68) */
    base_table_code[5] = 0xe5930024;       /* ldr r0, [r3, #0x24] (orig +0x6c) */
    base_table_code[6] = 0xe51ff004;       /* ldr pc, [pc, #-4] */
    base_table_code[7] = (uint32_t)resume_at;
    base_table_code[8] = 0xe3a00000;       /* invalid_return: mov r0, #0 */
    base_table_code[9] = 0xe51ff004;       /* ldr pc, [pc, #-4] */
    base_table_code[10] = (uint32_t)epilogue_at;

    kuKernelCpuUnrestrictedMemcpy((void *)cave, base_table_code, sizeof(base_table_code));
    kuKernelFlushCaches((void *)cave, sizeof(base_table_code));
    hook_arm(patch_at, cave);

    sceClibPrintf("[PATCH] CScriptInterpreter::GetExportFunction base-table range guard at %p -> %p\n",
                  (void *)patch_at, (void *)cave);

    patch_at = get_export + 0x7c;  /* ldrb r0, [r0, r1] */
    cave = alloc_patch_cave(sizeof(lookup_code));
    if (!cave) {
        return;
    }

    lookup_code[0] = 0xe3500481;       /* cmp r0, #0x81000000 */
    lookup_code[1] = 0x3a00000e;       /* blo invalid_return */
    lookup_code[2] = 0xe35004a0;       /* cmp r0, #0xa0000000 */
    lookup_code[3] = 0x2a00000c;       /* bhs invalid_return */
    lookup_code[4] = 0xe7d00001;       /* ldrb r0, [r0, r1] */
    lookup_code[5] = 0xe593c00c;       /* ldr ip, [r3, #0xc] */
    lookup_code[6] = 0xe35c0481;       /* cmp ip, #0x81000000 */
    lookup_code[7] = 0x3a000008;       /* blo invalid_return */
    lookup_code[8] = 0xe35c04a0;       /* cmp ip, #0xa0000000 */
    lookup_code[9] = 0x2a000006;       /* bhs invalid_return */
    lookup_code[10] = 0xe1500002;      /* cmp r0, r2 */
    lookup_code[11] = 0x23a00000;      /* movhs r0, #0 */
    lookup_code[12] = 0x31a00180;      /* movlo r0, r0, lsl #3 */
    lookup_code[13] = 0xe08c0000;      /* add r0, ip, r0 */
    lookup_code[14] = 0xe8bd00f0;      /* pop {r4, r5, r6, r7} */
    lookup_code[15] = 0xe12fff1e;      /* bx lr */
    lookup_code[16] = 0xe1a00000;      /* branch landing padding */
    lookup_code[17] = 0xe3a00000;      /* invalid_return: mov r0, #0 */
    lookup_code[18] = 0xe8bd00f0;      /* pop {r4, r5, r6, r7} */
    lookup_code[19] = 0xe12fff1e;      /* bx lr */

    kuKernelCpuUnrestrictedMemcpy((void *)cave, lookup_code, sizeof(lookup_code));
    kuKernelFlushCaches((void *)cave, sizeof(lookup_code));
    hook_arm(patch_at, cave);

    sceClibPrintf("[PATCH] CScriptInterpreter::GetExportFunction table range guard at %p -> %p\n",
                  (void *)patch_at, (void *)cave);

#define PATCH_EXPORT_CALLER(addr, resume, label) do { \
    cave = alloc_patch_cave(sizeof(caller_code)); \
    if (!cave) { \
        return; \
    } \
    caller_code[0] = 0xe3500481;       /* cmp r0, #0x81000000 */ \
    caller_code[1] = 0x3a000007;       /* blo invalid_return */ \
    caller_code[2] = 0xe35004a0;       /* cmp r0, #0xa0000000 */ \
    caller_code[3] = 0x2a000005;       /* bhs invalid_return */ \
    caller_code[4] = 0xe3100003;       /* tst r0, #3 */ \
    caller_code[5] = 0x1a000003;       /* bne invalid_return */ \
    caller_code[6] = 0xe1a01004;       /* mov r1, r4 */ \
    caller_code[7] = 0xe8bd4010;       /* pop {r4, lr} */ \
    caller_code[8] = 0xe51ff004;       /* ldr pc, [pc, #-4] */ \
    caller_code[9] = (uint32_t)(resume); \
    caller_code[10] = 0xe3a00000;      /* invalid_return: mov r0, #0 */ \
    caller_code[11] = 0xe8bd8010;      /* pop {r4, pc} */ \
    kuKernelCpuUnrestrictedMemcpy((void *)cave, caller_code, sizeof(caller_code)); \
    kuKernelFlushCaches((void *)cave, sizeof(caller_code)); \
    hook_arm((addr), cave); \
    sceClibPrintf("[PATCH] %s export range guard at %p -> %p\n", \
                  (label), (void *)(addr), (void *)cave); \
} while (0)

    if (call_export_args) {
        patch_at = call_export_args + 0x24;  /* mov r1, r4 */
        resume_at = call_export_args + 0x2c; /* branch to CScriptCode::Execute */
        PATCH_EXPORT_CALLER(patch_at, resume_at, "CScriptInterpreter::CallExportFunctionEhsss");
    } else {
        sceClibPrintf("[PATCH] CScriptInterpreter::CallExportFunctionEhsss symbol not found\n");
    }

    if (call_export) {
        patch_at = call_export + 0x24;  /* mov r1, r4 */
        resume_at = call_export + 0x2c; /* branch to CScriptCode::Execute */
        PATCH_EXPORT_CALLER(patch_at, resume_at, "CScriptInterpreter::CallExportFunctionEh");
    } else {
        sceClibPrintf("[PATCH] CScriptInterpreter::CallExportFunctionEh symbol not found\n");
    }

#undef PATCH_EXPORT_CALLER
}




static void patch_brother_update_normal_missing_current_gun(void) {
    static const char normal_symbol[] = "_ZN8CBrother12UpdateNormalEi";
    uintptr_t normal = so_symbol(&so_mod, normal_symbol);
    uintptr_t patch_at;
    uintptr_t resume_at;
    uintptr_t exit_at;
    uintptr_t cave;
    uint32_t code[10];
    uint32_t first;
    uint32_t second;

    if (!normal) {
        sceClibPrintf("[PATCH] CBrother::UpdateNormal current-gun guard: symbol not found\n");
        return;
    }

    patch_at = normal + 0x1b8;   /* ldr r3, [r0, #0x2c] */
    resume_at = normal + 0x1c0;  /* ldr r6, [r3, #0x80] */
    exit_at = normal + 0x204;    /* add r0, r4, #0x310; pop; b Refresh */

    first = *(volatile uint32_t *)patch_at;
    second = *(volatile uint32_t *)(patch_at + 4);
    if (first != 0xe590302cu || second != 0xe3520000u) {
        sceClibPrintf("[PATCH] CBrother::UpdateNormal current-gun guard skipped; unexpected words at %p: %08x %08x\n",
                      (void *)patch_at, first, second);
        return;
    }

    cave = alloc_patch_cave(sizeof(code));
    if (!cave) {
        return;
    }

    code[0] = 0xe3500000u;       /* cmp r0, #0 */
    code[1] = 0x0a000005u;       /* beq skip_weapon_block */
    code[2] = 0xe590302cu;       /* ldr r3, [r0, #0x2c] */
    code[3] = 0xe3530000u;       /* cmp r3, #0 */
    code[4] = 0x0a000002u;       /* beq skip_weapon_block */
    code[5] = 0xe3520000u;       /* cmp r2, #0 (overwritten original) */
    code[6] = 0xe51ff004u;       /* ldr pc, [pc, #-4] */
    code[7] = (uint32_t)resume_at;
    code[8] = 0xe51ff004u;       /* skip_weapon_block: ldr pc, [pc, #-4] */
    code[9] = (uint32_t)exit_at;

    kuKernelCpuUnrestrictedMemcpy((void *)cave, code, sizeof(code));
    kuKernelFlushCaches((void *)cave, sizeof(code));
    hook_arm(patch_at, cave);
    kuKernelFlushCaches((void *)patch_at, 8);

    sceClibPrintf("[PATCH] CBrother::UpdateNormal current-gun null guard at %p -> %p; words=%08x %08x\n",
                  (void *)patch_at, (void *)cave,
                  *(volatile uint32_t *)patch_at,
                  *(volatile uint32_t *)(patch_at + 4));
}

static void patch_brother_update_animation_missing_movesets(void) {
    static const char anim_symbol[] = "_ZN8CBrother15UpdateAnimationEi";
    uintptr_t anim = so_symbol(&so_mod, anim_symbol);
    uintptr_t patch_at;
    uintptr_t resume_at;
    uintptr_t cave;
    uint32_t code_primary[8];
    uint32_t code_secondary[8];
    uint32_t first;
    uint32_t second;

    if (!anim) {
        sceClibPrintf("[PATCH] CBrother::UpdateAnimation missing moveset guard: symbol not found\n");
        return;
    }

    patch_at = anim + 0x18;   /* ldr r3, [r4, #0x6f8] */
    resume_at = anim + 0x20;  /* ldr r0, [r4, #0x758] */
    first = *(volatile uint32_t *)patch_at;
    second = *(volatile uint32_t *)(patch_at + 4);
    if (first != 0xe59436f8u || second != 0xe1a06000u) {
        sceClibPrintf("[PATCH] CBrother::UpdateAnimation primary moveset guard skipped; unexpected words at %p: %08x %08x\n",
                      (void *)patch_at, first, second);
    } else {
        cave = alloc_patch_cave(sizeof(code_primary));
        if (!cave) {
            return;
        }

        code_primary[0] = 0xe59436f8u;       /* ldr r3, [r4, #0x6f8] */
        code_primary[1] = 0xe3530000u;       /* cmp r3, #0 */
        code_primary[2] = 0x0a000002u;       /* beq skip_update_animation */
        code_primary[3] = 0xe1a06000u;       /* mov r6, r0 */
        code_primary[4] = 0xe51ff004u;       /* ldr pc, [pc, #-4] */
        code_primary[5] = (uint32_t)resume_at;
        code_primary[6] = 0xe28dd00cu;       /* skip: add sp, sp, #12 */
        code_primary[7] = 0xe8bd85f0u;       /* pop {r4,r5,r6,r7,r8,r10,pc} */

        kuKernelCpuUnrestrictedMemcpy((void *)cave, code_primary, sizeof(code_primary));
        kuKernelFlushCaches((void *)cave, sizeof(code_primary));
        hook_arm(patch_at, cave);
        kuKernelFlushCaches((void *)patch_at, 8);

        sceClibPrintf("[PATCH] CBrother::UpdateAnimation primary moveset null guard at %p -> %p; words=%08x %08x\n",
                      (void *)patch_at, (void *)cave,
                      *(volatile uint32_t *)patch_at,
                      *(volatile uint32_t *)(patch_at + 4));
    }

    patch_at = anim + 0x3c;   /* ldr r3, [r4, #0x6f4] */
    resume_at = anim + 0x44;  /* ldr r0, [r4, #0x754] */
    first = *(volatile uint32_t *)patch_at;
    second = *(volatile uint32_t *)(patch_at + 4);
    if (first != 0xe59436f4u || second != 0xe1a08000u) {
        sceClibPrintf("[PATCH] CBrother::UpdateAnimation secondary moveset guard skipped; unexpected words at %p: %08x %08x\n",
                      (void *)patch_at, first, second);
    } else {
        cave = alloc_patch_cave(sizeof(code_secondary));
        if (!cave) {
            return;
        }

        code_secondary[0] = 0xe59436f4u;     /* ldr r3, [r4, #0x6f4] */
        code_secondary[1] = 0xe3530000u;     /* cmp r3, #0 */
        code_secondary[2] = 0x0a000002u;     /* beq skip_update_animation */
        code_secondary[3] = 0xe1a08000u;     /* mov r8, r0 */
        code_secondary[4] = 0xe51ff004u;     /* ldr pc, [pc, #-4] */
        code_secondary[5] = (uint32_t)resume_at;
        code_secondary[6] = 0xe28dd00cu;     /* skip: add sp, sp, #12 */
        code_secondary[7] = 0xe8bd85f0u;     /* pop {r4,r5,r6,r7,r8,r10,pc} */

        kuKernelCpuUnrestrictedMemcpy((void *)cave, code_secondary, sizeof(code_secondary));
        kuKernelFlushCaches((void *)cave, sizeof(code_secondary));
        hook_arm(patch_at, cave);
        kuKernelFlushCaches((void *)patch_at, 8);

        sceClibPrintf("[PATCH] CBrother::UpdateAnimation secondary moveset null guard at %p -> %p; words=%08x %08x\n",
                      (void *)patch_at, (void *)cave,
                      *(volatile uint32_t *)patch_at,
                      *(volatile uint32_t *)(patch_at + 4));
    }
}

static void patch_eventlog_current_equipment_stubs(void) {
    hook_symbol("CEventLog::logGameCurGuns",
                "_ZN9CEventLog14logGameCurGunsEv",
                (uintptr_t)event_log_cur_guns_stub);
    hook_symbol("CEventLog::logGameCurArmor",
                "_ZN9CEventLog15logGameCurArmorEv",
                (uintptr_t)event_log_cur_armor_stub);
}
