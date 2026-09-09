/*
 * Copyright (C) 2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  patch.c
 * @brief Patching some of the .so internal functions or bridging them to native
 *        for better compatibility.
 */

#include <kubridge.h>
#include <psp2/kernel/clib.h>
#include <so_util/so_util.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

extern so_module so_mod;

static void patch_log_once(const char *label, const char *result) {
    enum { max_slots = 64, max_per_label = 6 };
    static struct {
        const char *label;
        int count;
    } slots[max_slots];
    int i;
    int slot = -1;

    for (i = 0; i < max_slots; i++) {
        if (slots[i].label == label) {
            slot = i;
            break;
        }

        if (!slots[i].label && slot < 0) {
            slot = i;
        }
    }

    if (slot >= 0 && !slots[slot].label) {
        slots[slot].label = label;
    }

    if (slot < 0 || slots[slot].count < max_per_label) {
        sceClibPrintf("[PATCH-CNGS] %s -> %s\n",
                      label ? label : "(null)",
                      result ? result : "(null)");
    }

    if (slot >= 0) {
        slots[slot].count++;
    }
}

#define CNGS_INT_STUB(fn, label, value) \
    static int fn(void) { \
        patch_log_once(label, value ? "true" : "false"); \
        return value; \
    }

#define CNGS_VOID_STUB(fn, label) \
    static void fn(void) { \
        patch_log_once(label, "skip"); \
    }

CNGS_VOID_STUB(cngs_handle_update_stub, "CNGS::HandleUpdate")
CNGS_VOID_STUB(cngs_session_tick_stub, "CNGSSession::tick")
CNGS_INT_STUB(cngs_session_key_valid_stub, "CNGSSession::isSessionKeyValid", 0)
CNGS_INT_STUB(cngs_local_user_authenticated_stub, "CNGSLocalUser::isAuthenticated", 0)
CNGS_INT_STUB(cngs_local_user_load_credentials_stub, "CNGSLocalUser::LoadCredentials", 0)
CNGS_INT_STUB(cngs_local_user_register_stub, "CNGSLocalUser::RegisterUser", 0)
CNGS_INT_STUB(cngs_local_user_validate_stub, "CNGSLocalUser::ValidateUser", 0)
CNGS_INT_STUB(cngs_local_user_associate_stub, "CNGSLocalUser::AssociateUser", 0)
CNGS_INT_STUB(cngs_local_user_update_stub, "CNGSLocalUser::UpdateUserInfo", 0)
CNGS_INT_STUB(cngs_local_credentials_exists_stub, "CNGSLocalUser::CredentialsFileExists", 0)
CNGS_INT_STUB(cngs_user_load_credentials_stub, "CNGSUser::LoadCredentials", 0)
CNGS_INT_STUB(cngs_user_credentials_exists_stub, "CNGSUser::CredentialsFileExists", 0)
CNGS_INT_STUB(cngs_user_credentials_valid_stub, "CNGSUserCredentials::isValid", 0)
CNGS_INT_STUB(cngs_user_credentials_read_stub, "CNGSUserCredentials::readFromFile", 0)
CNGS_INT_STUB(cngs_user_credentials_last_exists_stub, "CNGSUserCredentials::lastPlayerExists", 0)
CNGS_INT_STUB(cngs_user_credentials_get_last_stub, "CNGSUserCredentials::getLastPlayer", 0)
CNGS_INT_STUB(cngs_session_config_read_stub, "CNGSSessionConfig::readFromFile", 0)
CNGS_VOID_STUB(cngs_account_handle_update_stub, "CNGSAccountManager::HandleUpdate")
CNGS_INT_STUB(cngs_account_send_message_stub, "CNGSAccountManager::SendMessageToServer", 0)
CNGS_INT_STUB(cngs_account_send_object_stub, "CNGSAccountManager::SendMessageObjectToServer", 0)
CNGS_VOID_STUB(content_tracker_load_from_server_stub, "CContentTracker::LoadFromServer")
CNGS_INT_STUB(content_tracker_save_to_server_stub, "CContentTracker::SaveToServer", 0)
CNGS_INT_STUB(cngs_content_get_stub, "CNGSContentManager::GetContent", 0)
CNGS_INT_STUB(cngs_content_get_self_stub, "CNGSContentManager::GetContentSelf", 0)
CNGS_INT_STUB(cngs_content_get_friend_stub, "CNGSContentManager::GetContentFriend", 0)
CNGS_VOID_STUB(cngs_json_handle_update_stub, "CNGSJSONData::HandleUpdate")
CNGS_INT_STUB(cngs_json_load_server_stub, "CNGSJSONData::LoadFromServer", 0)
CNGS_VOID_STUB(cngs_content_handle_update_stub, "CNGSContentManager::HandleUpdate")
CNGS_INT_STUB(cngs_content_status_stub, "CNGSContentManager::getContentManagerStatus", 0)
CNGS_VOID_STUB(gunbros_update_online_status_stub, "CGunBros::UpdateOnlineStatus")
CNGS_INT_STUB(menu_greeting_offline_stub, "CMenuGreeting::IsInOfflineMode", 1)
CNGS_INT_STUB(cngs_applet_wifi_stub, "CApplet::IsUserOnWiFi", 0)
CNGS_INT_STUB(cngs_callback_wifi_stub, "GluPlatformCallbackJNI::IsUserOnWiFi", 0)

static int menu_splash_busy_stub(void *self) {
    (void)self;
    patch_log_once("CMenuSplash::IsBusy", "false");
    return 0;
}

static so_hook h_menu_stack_update;
static so_hook h_menu_stack_set_menu;
static so_hook h_menu_stack_push_menu;
static so_hook h_menu_stack_pop_menu;
static so_hook h_menu_stack_is_busy;
static so_hook h_menu_navigation_bar_hide_buttons;
static so_hook h_menu_system_is_menu_busy;
static so_hook h_menu_system_update;
static so_hook h_menu_splash_update;
static so_hook h_menu_splash_is_loaded;
static so_hook h_menu_greeting_update;
static so_hook h_menu_greeting_is_busy;
static so_hook h_menu_greeting_is_loaded;
static so_hook h_gunbros_update;
static so_hook h_gunbros_bind;
static so_hook h_gunbros_load_menus;
static so_hook h_gunbros_show_main_menu;
static so_hook h_gunbros_set_menu;
static so_hook h_gunbros_flatten_object_index_const;
static so_hook h_gunbros_unflatten_object_index_const;
static so_hook h_gunbros_get_object_count_const;
static so_hook h_gunbros_validate_game_object_3_const;
static so_hook h_gunbros_validate_game_object_ref_const;
static so_hook h_gunbros_get_game_object_flat;
static so_hook h_gunbros_get_game_object_flat_const;
static so_hook h_gunbros_get_game_object_pack;
static so_hook h_gunbros_get_game_object_pack_const;
static so_hook h_gunbros_load_game_object_req;
static so_hook h_gunbros_free_game_object_req;
static so_hook h_resource_load_next;
static so_hook h_menu_stack_load_menu;
static so_hook h_menu_splash_load;
static so_hook h_menu_greeting_load;
static so_hook h_menu_system_load;
static so_hook h_menu_system_set_menu;
static so_hook h_menu_system_push_menu;
static so_hook h_menu_system_pop_menu;
static so_hook h_res_pack_toc_init;
static so_hook h_res_pack_toc_bind;
static so_hook h_res_toc_manager_init;
static so_hook h_res_toc_manager_bind;
static so_hook h_res_toc_manager_set_pack_hash;
static so_hook h_res_toc_manager_set_pack_str;
static so_hook h_res_toc_manager_set_pack_internal;
static so_hook h_image_pool_load_image;
static so_hook h_movie_load;
static so_hook h_movie_sprite_load;
static so_hook h_movie_sound_set_load;
static so_hook h_movie_tiled_sprite_load;
static so_hook h_graphics_instr_texture;
static so_hook h_player_progress_get_experience_for_level;
static so_hook h_player_progress_get_experience_delta;
static so_hook h_player_progress_get_percent_to_next_level;
static so_hook h_sprite_player_draw_rect;
static so_hook h_game_flow_configure_brother;
static so_hook h_game_flow_get_mission;
static so_hook h_friend_data_get_avatar_progress;
static so_hook h_friend_data_get_avatar_config;
static so_hook h_game_object_pack_init_game_object;
static so_hook h_game_object_pack_get_game_object;
static so_hook h_game_object_pack_get_game_object_const;
static so_hook h_store_aggregator_is_item_level_locked;
static so_hook h_store_aggregator_can_item_be_acquired;
static so_hook h_store_aggregator_get_item_status;
static so_hook h_store_aggregator_init_filtered_list;
static so_hook h_store_aggregator_acquire_item;
static so_hook h_level_on_start;
static so_hook h_brother_spawn_construct;
static so_hook h_brother_bind_trace;
static so_hook h_challenge_update_status_data;
static so_hook h_challenge_update_from_level_session;
static so_hook h_input_pad_update_input;
static so_hook h_weapon_mastery_add_xp;
static so_hook h_brother_move_trace;
static so_hook h_brother_fire_trace;
static so_hook h_player_bind_trace;
static so_hook h_player_update_trace;

typedef struct GunBrosGameplayInputState {
    int move_active;
    int fire_active;
    float move_x;
    float move_y;
    float fire_x;
    float fire_y;
    int move_source;
    int fire_source;
    unsigned int seq;
} GunBrosGameplayInputState;

extern volatile GunBrosGameplayInputState g_gunbros_gameplay_input;

static void gameplay_control_bridge_update(int dt);

enum {
    GUNBROS_STATE_LOADING_SPLASH = 2,
    GUNBROS_MAIN_MENU_SCREEN = 0x13,
    GUNBROS_MAIN_MENU_KICK_DELAY_UPDATES = 2,
};

static int g_main_menu_kick_pending;
static int g_main_menu_kick_done;
static int g_main_menu_kick_delay = -1;
static int g_splash_loaded_seen;

static int trace_allow_count(const char *label, int first, int every, int *out_count) {
    enum { max_slots = 96 };
    static struct {
        const char *label;
        int count;
    } slots[max_slots];
    int i;
    int slot = -1;

    for (i = 0; i < max_slots; i++) {
        if (slots[i].label == label) {
            slot = i;
            break;
        }

        if (!slots[i].label && slot < 0) {
            slot = i;
        }
    }

    if (slot >= 0 && !slots[slot].label) {
        slots[slot].label = label;
    }

    if (slot < 0) {
        if (out_count) {
            *out_count = 0;
        }
        return 1;
    }

    slots[slot].count++;

    if (out_count) {
        *out_count = slots[slot].count;
    }

    if (slots[slot].count <= first) {
        return 1;
    }

    return every > 0 && (slots[slot].count % every) == 0;
}

static int trace_allow_ex(const char *label, int first, int every) {
    return trace_allow_count(label, first, every, NULL);
}

static int trace_allow(const char *label) {
    return trace_allow_ex(label, 20, 120);
}

static const char *trace_cstr(const char *text) {
    uintptr_t value = (uintptr_t)text;
    int i;

    if (!text) {
        return "(null)";
    }

    if (value < 0x81000000u || value >= 0xA0000000u) {
        return "(badptr)";
    }

    for (i = 0; i < 96; ++i) {
        if (text[i] == '\0') {
            return text;
        }
    }

    return "(unterminated)";
}

static int trace_is_game_ptr(const void *ptr) {
    uintptr_t value = (uintptr_t)ptr;

    return value >= 0x81000000u && value < 0xA0000000u;
}

static int trace_is_game_range(const void *ptr, size_t size) {
    uintptr_t start = (uintptr_t)ptr;
    uintptr_t end = start + size;

    return size > 0 &&
           start >= 0x81000000u &&
           end >= start &&
           end < 0xA0000000u;
}


static int brother_has_bound_state(const void *self);

static void trace_dump_brother_state(const char *tag, const void *self) {
    const unsigned char *base = (const unsigned char *)self;
    const void *vptr = NULL;
    const void *map = NULL;
    const void *config = NULL;
    const void *gun_runtime = NULL;
    const void *move_primary = NULL;
    const void *move_secondary = NULL;
    const void *script_base_state = NULL;
    const void *script_normal_state = NULL;
    unsigned int active = 0;
    unsigned int visible = 0;
    unsigned int shooting = 0;
    float pos_x = 0.0f;
    float pos_y = 0.0f;
    float max_health = 0.0f;
    float health = 0.0f;

    if (!trace_is_game_range(self, 0xbb0)) {
        sceClibPrintf("[DIAG-BROTHER] %s brother=%p INVALID_RANGE\n",
                      tag ? tag : "(null)", self);
        return;
    }

    vptr = *(const void * const *)(const void *)base;
    map = *(const void * const *)(const void *)(base + 0x6fc);
    config = *(const void * const *)(const void *)(base + 0x780);
    gun_runtime = *(const void * const *)(const void *)(base + 0x210);
    move_primary = *(const void * const *)(const void *)(base + 0x6f8);
    move_secondary = *(const void * const *)(const void *)(base + 0x6f4);
    script_base_state = *(const void * const *)(const void *)(base + 0x68 + 0x30);
    script_normal_state = *(const void * const *)(const void *)(base + 0x310 + 0x30);
    active = base[0x785];
    visible = base[0x786];
    shooting = base[0x788];
    pos_x = *(const float *)(const void *)(base + 0x708);
    pos_y = *(const float *)(const void *)(base + 0x70c);
    max_health = *(const float *)(const void *)(base + 0x728);
    health = *(const float *)(const void *)(base + 0x72c);

    sceClibPrintf("[DIAG-BROTHER] %s brother=%p vptr=%p map=%p cfg=%p gunCtx=%p moveA=%p moveB=%p scriptBase=%p scriptNormal=%p pos=(%.1f,%.1f) hp=%.1f/%.1f flags(active=%u visible=%u shooting=%u)\n",
                  tag ? tag : "(null)", self, vptr, map, config, gun_runtime,
                  move_primary, move_secondary, script_base_state,
                  script_normal_state, pos_x, pos_y, health, max_health,
                  active, visible, shooting);
}

static int trace_float_bad_or_dead(float value, float min_good, float max_good) {
    return !(value >= min_good && value <= max_good);
}

static void repair_brother_runtime_state(void *self, const char *tag) {
    unsigned char *base = (unsigned char *)self;
    float *max_health;
    float *health;
    int repaired = 0;

    if (!trace_is_game_range(self, 0xbb0u)) {
        return;
    }

    max_health = (float *)(void *)(base + 0x728);
    health = (float *)(void *)(base + 0x72c);

    /* Offline/profile bootstrap can leave health at 0, which makes the actor
     * look permanently dead and can suppress normal movement/damage handling. */
    if (trace_float_bad_or_dead(*max_health, 1.0f, 100000.0f)) {
        *max_health = 100.0f;
        repaired = 1;
    }
    if (trace_float_bad_or_dead(*health, 1.0f, *max_health)) {
        *health = *max_health;
        repaired = 1;
    }

    if (base[0x786] == 0) {
        base[0x786] = 1;
        repaired = 1;
    }

    if (brother_has_bound_state(self) && base[0x785] == 0) {
        base[0x785] = 1;
        repaired = 1;
    }

    if (repaired && trace_allow(tag ? tag : "CBrother/runtime_repair")) {
        sceClibPrintf("[FIX-BROTHER] runtime state repaired tag=%s brother=%p hp=%.1f/%.1f active=%u visible=%u\n",
                      tag ? tag : "(null)", self, *health, *max_health,
                      (unsigned int)base[0x785], (unsigned int)base[0x786]);
        trace_dump_brother_state("CBrother/runtime-after", self);
    }
}

static void construct_brother_if_uninitialized(void *self, const char *log_label);
static void repair_brother_vptr_preserve_state(void *self, const char *log_label, void *preferred_vptr);
static void *g_live_gunbros_self;

static int gunbros_object_registry_is_valid(const void *self) {
    const unsigned char *base = (const unsigned char *)self;
    const void *packs;
    unsigned int count;

    if (!trace_is_game_range(self, 0x140)) {
        return 0;
    }

    packs = *(const void * const *)(const void *)(base + 0x138);
    count = *(const unsigned int *)(const void *)(base + 0x13c);

    return count > 0 &&
           count <= 64 &&
           trace_is_game_range(packs, (size_t)count * 0xb8u);
}

static void remember_live_gunbros_self(void *self, const char *source) {
    if (gunbros_object_registry_is_valid(self)) {
        if (g_live_gunbros_self != self && trace_allow("CGunBros/live-registry")) {
            sceClibPrintf("[FIX-GUNBROS] live registry from %s: %p (old=%p)\n",
                          source ? source : "?", self, g_live_gunbros_self);
        }
        g_live_gunbros_self = self;
    }
}

static void *gunbros_registry_self_or_live(void *self,
                                           const char *label,
                                           unsigned int type,
                                           unsigned int id) {
    if (gunbros_object_registry_is_valid(self)) {
        return self;
    }

    if (gunbros_object_registry_is_valid(g_live_gunbros_self)) {
        if (trace_allow(label)) {
            sceClibPrintf("[FIX-GUNBROS] %s repaired self=%p -> live=%p type=%u id=%u\n",
                          label ? label : "registry", self, g_live_gunbros_self,
                          type, id);
        }
        return g_live_gunbros_self;
    }

    if (trace_allow(label)) {
        sceClibPrintf("[FIX-GUNBROS] %s no live registry self=%p type=%u id=%u -> safe miss\n",
                      label ? label : "registry", self, type, id);
    }
    return NULL;
}

static int trace_gunbros_unflatten_object_index_const(void *self,
                                                       unsigned int type,
                                                       unsigned int flat_index,
                                                       uint16_t *out_index,
                                                       unsigned char *out_pack) {
    void *safe_self = gunbros_registry_self_or_live(self,
                                                    "CGunBros::UnFlattenObjectIndex/null-self",
                                                    type,
                                                    flat_index);
    if (!safe_self) {
        if (trace_is_game_range(out_index, sizeof(*out_index))) {
            *out_index = 0;
        }
        if (trace_is_game_range(out_pack, sizeof(*out_pack))) {
            *out_pack = 0xff;
        }
        return 0;
    }

    return SO_CONTINUE(int, h_gunbros_unflatten_object_index_const,
                       safe_self, type, flat_index, out_index, out_pack);
}

static int trace_gunbros_flatten_object_index_const(void *self,
                                                     unsigned int type,
                                                     unsigned int index,
                                                     unsigned int pack,
                                                     uint16_t *out_flat) {
    void *safe_self = gunbros_registry_self_or_live(self,
                                                    "CGunBros::FlattenObjectIndex/null-self",
                                                    type,
                                                    index);
    if (!safe_self) {
        if (trace_is_game_range(out_flat, sizeof(*out_flat))) {
            *out_flat = 0;
        }
        return 0;
    }

    return SO_CONTINUE(int, h_gunbros_flatten_object_index_const,
                       safe_self, type, index, pack, out_flat);
}

static unsigned int trace_gunbros_get_object_count_const(void *self, unsigned int type) {
    void *safe_self = gunbros_registry_self_or_live(self,
                                                    "CGunBros::GetObjectCount/null-self",
                                                    type,
                                                    0);
    if (!safe_self) {
        return 0;
    }

    return SO_CONTINUE(unsigned int, h_gunbros_get_object_count_const, safe_self, type);
}

static int trace_gunbros_validate_game_object_3_const(void *self,
                                                       unsigned int type,
                                                       unsigned int index,
                                                       unsigned int pack) {
    void *safe_self = gunbros_registry_self_or_live(self,
                                                    "CGunBros::ValidateGameObject/null-self",
                                                    type,
                                                    index);
    if (!safe_self) {
        return 0;
    }

    return SO_CONTINUE(int, h_gunbros_validate_game_object_3_const,
                       safe_self, type, index, pack);
}

static int trace_gunbros_validate_game_object_ref_const(void *self,
                                                         unsigned int type,
                                                         const void *ref) {
    void *safe_self = gunbros_registry_self_or_live(self,
                                                    "CGunBros::ValidateGameObjectRef/null-self",
                                                    type,
                                                    0);
    if (!safe_self || !trace_is_game_range(ref, 8)) {
        if (trace_allow("CGunBros::ValidateGameObjectRef/bad-ref")) {
            sceClibPrintf("[FIX-GUNBROS] ValidateGameObject(ref) safe miss self=%p ref=%p type=%u\n",
                          self, ref, type);
        }
        return 0;
    }

    return SO_CONTINUE(int, h_gunbros_validate_game_object_ref_const,
                       safe_self, type, ref);
}

static void *trace_gunbros_get_game_object_flat(void *self,
                                                 unsigned int type,
                                                 unsigned int flat_index) {
    void *safe_self = gunbros_registry_self_or_live(self,
                                                    "CGunBros::GetGameObject(flat)/null-self",
                                                    type,
                                                    flat_index);
    if (!safe_self) {
        return NULL;
    }

    return SO_CONTINUE(void *, h_gunbros_get_game_object_flat,
                       safe_self, type, flat_index);
}

static void *trace_gunbros_get_game_object_flat_const(void *self,
                                                       unsigned int type,
                                                       unsigned int flat_index) {
    void *safe_self = gunbros_registry_self_or_live(self,
                                                    "CGunBros::GetGameObject(flat const)/null-self",
                                                    type,
                                                    flat_index);
    if (!safe_self) {
        return NULL;
    }

    return SO_CONTINUE(void *, h_gunbros_get_game_object_flat_const,
                       safe_self, type, flat_index);
}

static void *trace_gunbros_get_game_object_pack(void *self,
                                                 unsigned int type,
                                                 unsigned int index,
                                                 unsigned int pack) {
    void *safe_self = gunbros_registry_self_or_live(self,
                                                    "CGunBros::GetGameObject(pack)/null-self",
                                                    type,
                                                    index);
    if (!safe_self) {
        return NULL;
    }

    return SO_CONTINUE(void *, h_gunbros_get_game_object_pack,
                       safe_self, type, index, pack);
}

static void *trace_gunbros_get_game_object_pack_const(void *self,
                                                       unsigned int type,
                                                       unsigned int index,
                                                       unsigned int pack) {
    void *safe_self = gunbros_registry_self_or_live(self,
                                                    "CGunBros::GetGameObject(pack const)/null-self",
                                                    type,
                                                    index);
    if (!safe_self) {
        return NULL;
    }

    return SO_CONTINUE(void *, h_gunbros_get_game_object_pack_const,
                       safe_self, type, index, pack);
}

static void trace_gunbros_load_game_object_req(void *self,
                                                unsigned int type,
                                                unsigned int flat_index,
                                                unsigned int flag) {
    void *safe_self = gunbros_registry_self_or_live(self,
                                                    "CGunBros::LoadGameObjectReq/null-self",
                                                    type,
                                                    flat_index);
    if (!safe_self) {
        return;
    }

    (void)SO_CONTINUE(int, h_gunbros_load_game_object_req,
                      safe_self, type, flat_index, flag);
}

static void trace_gunbros_free_game_object_req(void *self,
                                                unsigned int type,
                                                unsigned int flat_index,
                                                unsigned int flag) {
    void *safe_self = gunbros_registry_self_or_live(self,
                                                    "CGunBros::FreeGameObjectReq/null-self",
                                                    type,
                                                    flat_index);
    if (!safe_self) {
        return;
    }

    (void)SO_CONTINUE(int, h_gunbros_free_game_object_req,
                      safe_self, type, flat_index, flag);
}


static int challenge_manager_has_status_data(const void *self) {
    const unsigned char *base = (const unsigned char *)self;
    const void *status;

    if (!trace_is_game_range(self, 0x860)) {
        return 0;
    }

    status = *(const void * const *)(const void *)(base + 0x85c);
    return trace_is_game_range(status, 0x338);
}

static void trace_challenge_update_status_data(void *self, unsigned int player_index) {
    if (!challenge_manager_has_status_data(self)) {
        if (trace_allow("CChallengeManager::UpdateChallengeStatusData/missing_status")) {
            void *status = NULL;
            if (trace_is_game_range(self, 0x860)) {
                status = *(void **)(void *)((unsigned char *)self + 0x85c);
            }
            sceClibPrintf("[FIX-CHALLENGE] UpdateChallengeStatusData skipped self=%p player=%u status=%p\n",
                          self, player_index, status);
        }
        return;
    }

    (void)SO_CONTINUE(int, h_challenge_update_status_data, self, player_index);
}


static int trace_weapon_mastery_add_xp(void *self, void *gun, unsigned int pack, unsigned int variant, unsigned int hand, unsigned int xp, unsigned int reason) {
    const unsigned char *gun_base = (const unsigned char *)gun;
    const void *gun_xp_thresholds = NULL;
    unsigned int mastery_class = 0xffffffffu;

    if (!trace_is_game_range(self, 0x610)) {
        if (trace_allow("CWeaponMastery::AddXP/bad_self")) {
            sceClibPrintf("[FIX-MASTERY] AddXP skipped bad self=%p gun=%p pack=%u variant=%u xp=%u reason=%u\n",
                          self, gun, pack, variant, xp, reason);
        }
        return 0;
    }

    if (!trace_is_game_range(gun, 0xe0)) {
        if (trace_allow("CWeaponMastery::AddXP/bad_gun")) {
            sceClibPrintf("[FIX-MASTERY] AddXP skipped bad gun self=%p gun=%p pack=%u variant=%u xp=%u reason=%u\n",
                          self, gun, pack, variant, xp, reason);
        }
        return 0;
    }

    gun_xp_thresholds = *(const void * const *)(const void *)(gun_base + 0xa8);
    mastery_class = *(const unsigned int *)(const void *)(gun_base + 0xac);
    if (!trace_is_game_range(gun_xp_thresholds, 12)) {
        if (trace_allow("CWeaponMastery::AddXP/no_xp_thresholds")) {
            sceClibPrintf("[FIX-MASTERY] AddXP skipped missing gun XP table self=%p gun=%p table=%p mastery_class=%u pack=%u variant=%u xp=%u reason=%u\n",
                          self, gun, gun_xp_thresholds, mastery_class, pack, variant, xp, reason);
        }
        return 0;
    }

    return SO_CONTINUE(int, h_weapon_mastery_add_xp, self, gun, pack, variant, hand, xp, reason);
}

static void trace_challenge_update_from_level_session(void *self, const void *level, const void *ref, unsigned int player_index) {
    if (!challenge_manager_has_status_data(self)) {
        if (trace_allow("CChallengeManager::UpdateFromLevelSession/missing_status")) {
            void *status = NULL;
            if (trace_is_game_range(self, 0x860)) {
                status = *(void **)(void *)((unsigned char *)self + 0x85c);
            }
            sceClibPrintf("[FIX-CHALLENGE] UpdateFromLevelSession skipped self=%p level=%p ref=%p player=%u status=%p\n",
                          self, level, ref, player_index, status);
        }
        return;
    }

    (void)SO_CONTINUE(int, h_challenge_update_from_level_session, self, level, ref, player_index);
}

static int trace_input_pad_update_input(void *self, int dt) {
    int ret;
    if (trace_allow_ex("CInputPad::UpdateInput/diag", 16, 240)) {
        const unsigned char *base = (const unsigned char *)self;
        void *slot0 = NULL;
        void *slot1 = NULL;
        void *slot2 = NULL;
        void *slot3 = NULL;
        unsigned int consumed = 0xff;
        if (trace_is_game_range(self, 0x2590)) {
            slot0 = *(void **)(void *)(base + 0x2000);
            slot1 = *(void **)(void *)(base + 0x2004);
            slot2 = *(void **)(void *)(base + 0x2008);
            slot3 = *(void **)(void *)(base + 0x200c);
            consumed = base[0x258c];
        }
        sceClibPrintf("[DIAG-INPUTPAD] UpdateInput enter self=%p dt=%d slots={%p,%p,%p,%p} consumed=%u\n",
                      self, dt, slot0, slot1, slot2, slot3, consumed);
    }

    ret = SO_CONTINUE(int, h_input_pad_update_input, self, dt);

    if (trace_allow_ex("CInputPad::UpdateInput/diag-leave", 16, 240)) {
        unsigned int consumed = 0xff;
        if (trace_is_game_range(self, 0x2590)) {
            consumed = ((const unsigned char *)self)[0x258c];
        }
        sceClibPrintf("[DIAG-INPUTPAD] UpdateInput leave self=%p ret=%d consumed=%u\n",
                      self, ret, consumed);
    }
    return ret;
}

static int trace_level_on_start(void *self) {
    int log = trace_allow("CLevel::OnStart");
    int ret;
    unsigned char *base = (unsigned char *)self;
    void *current = trace_is_game_range(self, 0x46ab0u + sizeof(void *)) ?
                    *(void **)(void *)(base + 0x46ab0) : NULL;
    construct_brother_if_uninitialized(current, "CLevel::OnStart/pre-state-fix");

    if (log) {
        const void *vptr = trace_is_game_range(current, sizeof(void *)) ?
                           *(const void * const *)current : NULL;

        sceClibPrintf("[TRACE-LEVEL] enter CLevel::OnStart(this=%p, current=%p, vptr=%p)\n",
                      self, current, vptr);
    }

    ret = SO_CONTINUE(int, h_level_on_start, self);
    repair_brother_vptr_preserve_state(current, "CLevel::OnStart/current-vptr-repair", NULL);
    repair_brother_runtime_state(current, "CLevel::OnStart/current-runtime");

    if (log) {
        sceClibPrintf("[TRACE-LEVEL] leave CLevel::OnStart(this=%p) -> %d\n",
                      self, ret);
        trace_dump_brother_state("CLevel::OnStart/current-after", current);
    }

    return ret;
}

static int player_progress_has_exp_table(const void *self) {
    const unsigned char *base = (const unsigned char *)self;
    const void *table;
    unsigned int count;

    if (!trace_is_game_ptr(self)) {
        return 0;
    }

    table = *(const void * const *)(const void *)(base + 0x08);
    count = *(const unsigned int *)(const void *)(base + 0x0c);

    return trace_is_game_ptr(table) && count > 0;
}

static uint64_t trace_player_progress_get_experience_for_level(void *self) {
    if (!player_progress_has_exp_table(self)) {
        if (trace_allow("CPlayerProgress::GetExperienceForLevel/null_table")) {
            sceClibPrintf("[PATCH-PROGRESS] GetExperienceForLevel(this=%p) -> 0; XP table unavailable\n", self);
        }
        return 0;
    }

    return SO_CONTINUE(uint64_t, h_player_progress_get_experience_for_level, self);
}

static uint32_t trace_player_progress_get_experience_delta(void *self) {
    if (!player_progress_has_exp_table(self)) {
        if (trace_allow("CPlayerProgress::GetExperienceDelta/null_table")) {
            sceClibPrintf("[PATCH-PROGRESS] GetExperienceDelta(this=%p) -> 0; XP table unavailable\n", self);
        }
        return 0;
    }

    return SO_CONTINUE(uint32_t, h_player_progress_get_experience_delta, self);
}

static uint32_t trace_player_progress_get_percent_to_next_level(void *self) {
    if (!player_progress_has_exp_table(self)) {
        if (trace_allow("CPlayerProgress::GetPercentToNextLevel/null_table")) {
            sceClibPrintf("[PATCH-PROGRESS] GetPercentToNextLevel(this=%p) -> 0.0; XP table unavailable\n", self);
        }
        return 0;
    }

    return SO_CONTINUE(uint32_t, h_player_progress_get_percent_to_next_level, self);
}

static int trace_is_player_config_for_brother(const void *config) {
    return trace_is_game_range(config, 0x0c + 0x76);
}

static int trace_is_player_progress_for_brother(const void *progress) {
    return trace_is_game_range(progress, 0x50);
}


static unsigned char g_vita_default_player_config[0x80] __attribute__((aligned(4)));
static unsigned char g_vita_default_player_progress[0x60] __attribute__((aligned(4)));
static uint32_t g_vita_default_xp_deltas[8] __attribute__((aligned(4))) = {
    100, 150, 250, 400, 600, 900, 1300, 1800
};
static uint16_t g_vita_default_level_values[8] __attribute__((aligned(2))) = {
    0, 0, 0, 0, 0, 0, 0, 0
};
static int g_vita_default_profile_ready;

static void vita_default_profile_init(void) {
    if (g_vita_default_profile_ready) {
        return;
    }

    memset(g_vita_default_player_config, 0, sizeof(g_vita_default_player_config));
    memset(g_vita_default_player_progress, 0, sizeof(g_vita_default_player_progress));
    g_vita_default_player_config[0x10] = 0;      /* primary gun id lo */
    g_vita_default_player_config[0x11] = 0;      /* primary gun id hi */
    g_vita_default_player_config[0x12] = 0;      /* primary gun subtype */
    g_vita_default_player_config[0x18] = 1;      /* secondary gun id lo */
    g_vita_default_player_config[0x19] = 0;      /* secondary gun id hi */
    g_vita_default_player_config[0x1a] = 0;      /* secondary gun subtype */
    g_vita_default_player_config[0x20] = 0;      /* primary gun mastery lo */
    g_vita_default_player_config[0x21] = 0;      /* primary gun mastery hi */
    g_vita_default_player_config[0x22] = 0;      /* primary gun extra */
    g_vita_default_player_config[0x2a] = 0;
    g_vita_default_player_config[0x32] = 0xff;   /* armour slots empty */
    g_vita_default_player_config[0x3a] = 0xff;
    g_vita_default_player_config[0x42] = 0xff;
    g_vita_default_player_config[0x4a] = 0xff;
    g_vita_default_player_config[0x4c] = 0;      /* active gun: primary */
    g_vita_default_player_config[0x4d] = 0;

    *(uint32_t *)(void *)(g_vita_default_player_progress + 0x08) = (uint32_t)g_vita_default_xp_deltas;
    *(uint32_t *)(void *)(g_vita_default_player_progress + 0x0c) = 8;
    *(uint32_t *)(void *)(g_vita_default_player_progress + 0x10) = (uint32_t)g_vita_default_level_values;
    *(uint32_t *)(void *)(g_vita_default_player_progress + 0x14) = 8;
    *(uint32_t *)(void *)(g_vita_default_player_progress + 0x48) = 0; /* xp low */
    *(uint32_t *)(void *)(g_vita_default_player_progress + 0x4c) = 0; /* xp high */
    *(uint16_t *)(void *)(g_vita_default_player_progress + 0x50) = 0; /* level */

    g_vita_default_profile_ready = 1;
    sceClibPrintf("[FIX-PROFILE] synthetic offline config=%p progress=%p xpTable=%p levelTable=%p\n",
                  g_vita_default_player_config, g_vita_default_player_progress,
                  g_vita_default_xp_deltas, g_vita_default_level_values);
}

static const void *vita_default_config(void) {
    vita_default_profile_init();
    return g_vita_default_player_config;
}

static const void *vita_default_progress(void) {
    vita_default_profile_init();
    return g_vita_default_player_progress;
}

static void *trace_friend_data_get_avatar_progress(void *self, int index) {
    void *progress = SO_CONTINUE(void *, h_friend_data_get_avatar_progress, self, index);

    if (!trace_is_player_progress_for_brother(progress)) {
        const void *fallback = vita_default_progress();
        if (trace_allow("CFriendDataManager::GetFriendAvatarProgress/bad_ptr")) {
            sceClibPrintf("[FIX-PROFILE] GetFriendAvatarProgress(this=%p, index=%d) returned bad progress=%p; using synthetic=%p\n",
                          self, index, progress, fallback);
        }
        return (void *)fallback;
    }

    return progress;
}

static void *trace_friend_data_get_avatar_config(void *self, int index) {
    void *config = SO_CONTINUE(void *, h_friend_data_get_avatar_config, self, index);

    if (!trace_is_player_config_for_brother(config)) {
        const void *fallback = vita_default_config();
        if (trace_allow("CFriendDataManager::GetFriendAvatarConfig/bad_ptr")) {
            sceClibPrintf("[FIX-PROFILE] GetFriendAvatarConfig(this=%p, index=%d) returned bad config=%p; using synthetic=%p\n",
                          self, index, config, fallback);
        }
        return (void *)fallback;
    }

    return config;
}

static void trace_game_flow_configure_brother(void *self, const void *config, const void *progress) {
    const void *safe_config = config;
    const void *safe_progress = progress;

    if (!trace_is_game_range(self, 0x260)) {
        if (trace_allow("CGameFlow::ConfigureBrother/bad_self")) {
            sceClibPrintf("[PATCH-FLOW] ConfigureBrother(this=%p, config=%p, progress=%p) skipped bad gameflow\n",
                          self, config, progress);
        }
        return;
    }

    if (!trace_is_player_config_for_brother(config)) {
        safe_config = vita_default_config();
    }

    if (!trace_is_player_progress_for_brother(progress)) {
        safe_progress = vita_default_progress();
    }

    if (safe_config != config || safe_progress != progress) {
        if (trace_allow("CGameFlow::ConfigureBrother/bad_brother_data")) {
            sceClibPrintf("[FIX-PROFILE] ConfigureBrother(this=%p) repaired config=%p->%p progress=%p->%p\n",
                          self, config, safe_config, progress, safe_progress);
        }
    }

    (void)SO_CONTINUE(int, h_game_flow_configure_brother, self, safe_config, safe_progress);
}


static unsigned char g_vita_dummy_mission[0x80] __attribute__((aligned(4)));
static int g_vita_dummy_mission_ready;

static void *vita_dummy_mission(void) {
    if (!g_vita_dummy_mission_ready) {
        memset(g_vita_dummy_mission, 0, sizeof(g_vita_dummy_mission));
        *(uint32_t *)(void *)(g_vita_dummy_mission + 0x3c) = 0;
        g_vita_dummy_mission_ready = 1;
        sceClibPrintf("[FIX-MISSION] synthetic dummy mission=%p field_3c=0\n", g_vita_dummy_mission);
    }
    return g_vita_dummy_mission;
}

static void *trace_game_flow_get_mission(void *self) {
    void *mission;

    if (!trace_is_game_ptr(self)) {
        if (trace_allow("CGameFlow::GetMission/bad_self")) {
            sceClibPrintf("[FIX-MISSION] GetMission(this=%p) bad self -> dummy\n", self);
        }
        return vita_dummy_mission();
    }

    mission = SO_CONTINUE(void *, h_game_flow_get_mission, self);
    if (!trace_is_game_range(mission, 0x40)) {
        if (trace_allow("CGameFlow::GetMission/null")) {
            sceClibPrintf("[FIX-MISSION] GetMission(this=%p) returned %p -> dummy\n", self, mission);
        }
        return vita_dummy_mission();
    }

    return mission;
}

typedef struct GameObjectPackSlot {
    void *items;
    uint32_t count;
} GameObjectPackSlot;

enum {
    GOBJ_PACK_TYPE_COUNT = 28,
    GOBJ_PACK_MAX_TYPES = 64,
    GOBJ_PACK_MAX_LAZY_COUNT = 512,
};

static GameObjectPackSlot *gobj_alloc_slot_vector(uint32_t count) {
    uint32_t *raw;

    if (count == 0 || count > GOBJ_PACK_MAX_TYPES) {
        return NULL;
    }

    raw = (uint32_t *)calloc(1, 8u + count * sizeof(GameObjectPackSlot));
    if (!raw) {
        return NULL;
    }

    raw[0] = sizeof(GameObjectPackSlot);
    raw[1] = count;
    return (GameObjectPackSlot *)(void *)(raw + 2);
}

static void gobj_free_slot_vector(GameObjectPackSlot *slots, uint32_t count) {
    unsigned char *raw;

    if (!slots || count == 0 || count > GOBJ_PACK_MAX_TYPES) {
        return;
    }

    raw = (unsigned char *)(void *)slots - 8;
    if (trace_is_game_range(raw, 8u + count * sizeof(GameObjectPackSlot))) {
        free(raw);
    }
}

static GameObjectPackSlot *gobj_ensure_slot_vector(void *self,
                                                   size_t slot_offset,
                                                   size_t count_offset,
                                                   unsigned int type,
                                                   const char *label) {
    unsigned char *base = (unsigned char *)self;
    GameObjectPackSlot *slots = *(GameObjectPackSlot **)(void *)(base + slot_offset);
    uint32_t count = *(uint32_t *)(void *)(base + count_offset);
    uint32_t new_count = GOBJ_PACK_TYPE_COUNT;
    GameObjectPackSlot *new_slots;

    if (type < count &&
        count <= GOBJ_PACK_MAX_TYPES &&
        trace_is_game_range(slots, count * sizeof(GameObjectPackSlot))) {
        return slots;
    }

    if (type >= new_count) {
        new_count = type + 1;
    }

    if (new_count > GOBJ_PACK_MAX_TYPES) {
        return NULL;
    }

    new_slots = gobj_alloc_slot_vector(new_count);
    if (!new_slots) {
        return NULL;
    }

    if (count > 0 &&
        count <= GOBJ_PACK_MAX_TYPES &&
        trace_is_game_range(slots, count * sizeof(GameObjectPackSlot))) {
        sceClibMemcpy(new_slots, slots, count * sizeof(GameObjectPackSlot));
        gobj_free_slot_vector(slots, count);
    }

    *(GameObjectPackSlot **)(void *)(base + slot_offset) = new_slots;
    *(uint32_t *)(void *)(base + count_offset) = new_count;

    if (trace_allow(label)) {
        sceClibPrintf("[PATCH-GOBJ] repaired %s slot vector for pack=%p type=%u count=%u\n",
                      label, self, type, (unsigned int)new_count);
    }

    return new_slots;
}

static void *gobj_resize_items(void *items, uint32_t old_count, uint32_t new_count, size_t item_size) {
    void *new_items;
    size_t old_size = 0;

    if (new_count == 0 || new_count > GOBJ_PACK_MAX_LAZY_COUNT || item_size == 0) {
        return NULL;
    }

    new_items = calloc(new_count, item_size);
    if (!new_items) {
        return NULL;
    }

    if (items && old_count > 0 && old_count <= GOBJ_PACK_MAX_LAZY_COUNT) {
        old_size = old_count * item_size;
    }

    if (old_size > 0 && trace_is_game_range(items, old_size)) {
        sceClibMemcpy(new_items, items, old_size);
        free(items);
    }

    return new_items;
}

static int gobj_ensure_type_storage(void *self, unsigned int type, unsigned int index) {
    GameObjectPackSlot *object_slots;
    GameObjectPackSlot *state_slots;
    GameObjectPackSlot *object_slot;
    GameObjectPackSlot *state_slot;
    uint32_t needed;
    uint32_t object_count;
    uint32_t state_count;
    unsigned char *base = (unsigned char *)self;

    if (!trace_is_game_range(self, 0x30) ||
        type >= GOBJ_PACK_MAX_TYPES ||
        index == 0xff ||
        index >= GOBJ_PACK_MAX_LAZY_COUNT) {
        return 0;
    }

    needed = index + 1;
    object_slots = gobj_ensure_slot_vector(self, 0x04, 0x08, type, "object");
    state_slots = gobj_ensure_slot_vector(self, 0x0c, 0x10, type, "state");
    if (!object_slots || !state_slots) {
        return 0;
    }

    object_slot = &object_slots[type];
    state_slot = &state_slots[type];
    object_count = (object_slot->count >= needed &&
                    object_slot->count <= GOBJ_PACK_MAX_LAZY_COUNT) ? object_slot->count : needed;
    state_count = (state_slot->count >= needed &&
                   state_slot->count <= GOBJ_PACK_MAX_LAZY_COUNT) ? state_slot->count : needed;

    if (object_slot->count > GOBJ_PACK_MAX_LAZY_COUNT ||
        !trace_is_game_range(object_slot->items, object_slot->count * sizeof(void *)) ||
        object_slot->count < needed) {
        void *old_items = object_slot->items;
        void *items = gobj_resize_items(object_slot->items,
                                        object_slot->count,
                                        object_count,
                                        sizeof(void *));
        if (!items) {
            return 0;
        }

        object_slot->items = items;
        object_slot->count = object_count;
        if (trace_allow("CGameObjectPack/object_slot_repair")) {
            sceClibPrintf("[PATCH-GOBJ] repaired object slots pack=%p type=%u old_items=%p new_items=%p count=%u needed=%u\n",
                          self, type, old_items, object_slot->items,
                          (unsigned int)object_count, (unsigned int)needed);
        }
    }

    if (state_slot->count > GOBJ_PACK_MAX_LAZY_COUNT ||
        !trace_is_game_range(state_slot->items, state_slot->count) ||
        state_slot->count < needed) {
        void *items = gobj_resize_items(state_slot->items,
                                        state_slot->count,
                                        state_count,
                                        1);
        if (!items) {
            return 0;
        }

        state_slot->items = items;
        state_slot->count = state_count;
        if (trace_allow("CGameObjectPack/state_slot_repair")) {
            sceClibPrintf("[PATCH-GOBJ] repaired state slots pack=%p type=%u count=%u needed=%u\n",
                          self, type, (unsigned int)state_count, (unsigned int)needed);
        }
    }

    if (type < GOBJ_PACK_TYPE_COUNT && base[0x14 + type] < needed) {
        base[0x14 + type] = (unsigned char)needed;
    }

    return 1;
}

static void *trace_game_object_pack_get_game_object(void *self, unsigned int type, unsigned int index) {
    unsigned char *base = (unsigned char *)self;
    GameObjectPackSlot *slots;
    GameObjectPackSlot *slot;
    uint32_t type_count;

    if (index == 0xff || !trace_is_game_range(self, 0x0c)) {
        return NULL;
    }

    slots = *(GameObjectPackSlot **)(void *)(base + 0x04);
    type_count = *(uint32_t *)(void *)(base + 0x08);
    if (type >= type_count ||
        type_count > GOBJ_PACK_MAX_TYPES ||
        !trace_is_game_range(slots, type_count * sizeof(GameObjectPackSlot))) {
        return NULL;
    }

    slot = &slots[type];
    if (index >= slot->count ||
        slot->count > GOBJ_PACK_MAX_LAZY_COUNT ||
        !trace_is_game_range(slot->items, slot->count * sizeof(void *))) {
        return NULL;
    }

    return ((void **)slot->items)[index];
}

static void trace_game_object_pack_init_game_object(void *self, unsigned int type, unsigned int index) {
    if (!gobj_ensure_type_storage(self, type, index)) {
        if (trace_allow("CGameObjectPack::InitGameObject/bad_storage")) {
            sceClibPrintf("[PATCH-GOBJ] InitGameObject(pack=%p, type=%u, index=%u) skipped bad storage\n",
                          self, type, index);
        }
        return;
    }

    (void)SO_CONTINUE(int, h_game_object_pack_init_game_object, self, type, index);
}

static void gameplay_force_player_gun(unsigned int subtype, unsigned int id, const char *source);

static int trace_store_aggregator_is_item_level_locked(void *self, const void *item) {
    const unsigned char *base = (const unsigned char *)self;
    const void *progress;

    if (!trace_is_game_range(item, 0x16)) {
        if (trace_allow("CStoreAggregator::IsItemLevelLocked/bad_item")) {
            sceClibPrintf("[PATCH-STORE] IsItemLevelLocked(this=%p, item=%p) -> 0; bad item\n",
                          self, item);
        }
        return 0;
    }

    if (!trace_is_game_range(self, 0x44)) {
        if (trace_allow("CStoreAggregator::IsItemLevelLocked/bad_self")) {
            sceClibPrintf("[PATCH-STORE] IsItemLevelLocked(this=%p, item=%p) -> 0; bad aggregator\n",
                          self, item);
        }
        return 0;
    }

    progress = *(const void * const *)(const void *)(base + 0x40);
    if (!trace_is_game_range(progress, 0x52)) {
        if (trace_allow("CStoreAggregator::IsItemLevelLocked/no_progress")) {
            sceClibPrintf("[PATCH-STORE] IsItemLevelLocked(this=%p, item=%p) -> 0; missing progress=%p\n",
                          self, item, progress);
        }
        return 0;
    }

    return SO_CONTINUE(int, h_store_aggregator_is_item_level_locked, self, item);
}

static int store_aggregator_has_state_for_item_queries(const void *self,
                                                       const void **out_query_object,
                                                       const void **out_config,
                                                       const void **out_progress) {
    const unsigned char *base = (const unsigned char *)self;
    const void *query_object;
    const void *query_vtable;
    const void *config;
    const void *progress;

    if (out_query_object) {
        *out_query_object = NULL;
    }

    if (out_config) {
        *out_config = NULL;
    }

    if (out_progress) {
        *out_progress = NULL;
    }

    if (!trace_is_game_range(self, 0x44)) {
        return 0;
    }

    query_object = *(const void * const *)(const void *)(base + 0x38);
    config = *(const void * const *)(const void *)(base + 0x3c);
    progress = *(const void * const *)(const void *)(base + 0x40);

    if (out_query_object) {
        *out_query_object = query_object;
    }

    if (out_config) {
        *out_config = config;
    }

    if (out_progress) {
        *out_progress = progress;
    }

    if (!trace_is_game_range(query_object, sizeof(void *)) ||
        !trace_is_game_range(config, sizeof(void *)) ||
        !trace_is_game_range(progress, 0x52)) {
        return 0;
    }

    query_vtable = *(const void * const *)query_object;
    return trace_is_game_range(query_vtable, 0x48);
}

static int trace_store_aggregator_can_item_be_acquired(void *self, const void *item) {
    const void *query_object;
    const void *config;
    const void *progress;

    if (!trace_is_game_range(item, 0x14)) {
        if (trace_allow("CStoreAggregator::CanItemBeAcquired/bad_item")) {
            sceClibPrintf("[PATCH-STORE] CanItemBeAcquired(this=%p, item=%p) -> 0; bad item\n",
                          self, item);
        }
        return 0;
    }

    if (!store_aggregator_has_state_for_item_queries(self, &query_object, &config, &progress)) {
        if (trace_allow("CStoreAggregator::CanItemBeAcquired/missing_state")) {
            sceClibPrintf("[PATCH-STORE] CanItemBeAcquired(this=%p, item=%p) -> 0; missing state query=%p config=%p progress=%p\n",
                          self, item, query_object, config, progress);
        }
        return 0;
    }

    return SO_CONTINUE(int, h_store_aggregator_can_item_be_acquired, self, item);
}

static int trace_store_aggregator_get_item_status(void *self, const void *item, unsigned int cache) {
    const void *query_object;
    const void *config;
    const void *progress;

    if (!trace_is_game_range(item, 0x14)) {
        if (trace_allow("CStoreAggregator::GetItemStatus/bad_item")) {
            sceClibPrintf("[PATCH-STORE] GetItemStatus(this=%p, item=%p, cache=%u) -> -1; bad item\n",
                          self, item, cache);
        }
        return -1;
    }

    if (!store_aggregator_has_state_for_item_queries(self, &query_object, &config, &progress)) {
        if (trace_allow("CStoreAggregator::GetItemStatus/missing_state")) {
            sceClibPrintf("[PATCH-STORE] GetItemStatus(this=%p, item=%p, cache=%u) -> 7; missing state query=%p config=%p progress=%p\n",
                          self, item, cache, query_object, config, progress);
        }
        return 7;
    }

    return SO_CONTINUE(int, h_store_aggregator_get_item_status, self, item, cache);
}

static int trace_store_aggregator_acquire_item(void *self, const void *item, unsigned int equip_now) {
    const void *query_object;
    const void *config;
    const void *progress;
    unsigned int type = 0xffu;
    unsigned int count = 0u;
    const void *refs = NULL;

    if (trace_is_game_range(item, 0x20u)) {
        type = *(const unsigned char *)(const void *)((const unsigned char *)item + 0x4);
        count = *(const uint32_t *)(const void *)((const unsigned char *)item + 0x10);
        refs = *(const void * const *)(const void *)((const unsigned char *)item + 0x0c);
    }

    if (trace_is_game_range(refs, 12u)) {
        unsigned int ref_id = *(const uint16_t *)(const void *)((const unsigned char *)refs + 0x04);
        unsigned int ref_type = *(const unsigned char *)(const void *)((const unsigned char *)refs + 0x06);
        unsigned int ref_subtype = *(const unsigned char *)(const void *)((const unsigned char *)refs + 0x08);
        if (ref_type == 6 || type == 6) {
            gameplay_force_player_gun(ref_subtype, ref_id, "AcquireItem/ref");
        }
    }

    if (!trace_is_game_range(item, 0x14u)) {
        if (trace_allow("CStoreAggregator::AcquireItem/bad_item")) {
            sceClibPrintf("[FIX-STORE] AcquireItem(this=%p, item=%p, equip=%u) -> 0 bad item\n",
                          self, item, equip_now);
        }
        return 0;
    }

    if (!store_aggregator_has_state_for_item_queries(self, &query_object, &config, &progress)) {
        if (trace_allow("CStoreAggregator::AcquireItem/missing_state")) {
            sceClibPrintf("[FIX-STORE] AcquireItem(this=%p, item=%p type=%u count=%u refs=%p equip=%u) -> 1 missing state query=%p config=%p progress=%p\n",
                          self, item, type, count, refs, equip_now, query_object, config, progress);
        }
        return 1;
    }

    if (count > 0 && !trace_is_game_range(refs, count * 12u)) {
        if (trace_allow("CStoreAggregator::AcquireItem/bad_refs")) {
            sceClibPrintf("[FIX-STORE] AcquireItem(this=%p, item=%p type=%u count=%u refs=%p equip=%u) -> 1 bad refs\n",
                          self, item, type, count, refs, equip_now);
        }
        return 1;
    }

    return SO_CONTINUE(int, h_store_aggregator_acquire_item, self, item, equip_now);
}

static void trace_store_aggregator_init_filtered_list(void *self, unsigned int type) {
    const void *query_object;
    const void *config;
    const void *progress;

    if (!store_aggregator_has_state_for_item_queries(self, &query_object, &config, &progress)) {
        if (trace_allow("CStoreAggregator::InitFilteredList/missing_state")) {
            sceClibPrintf("[PATCH-STORE] InitFilteredList(this=%p, type=%u) skipped; missing state query=%p config=%p progress=%p\n",
                          self, type, query_object, config, progress);
        }
        return;
    }

    (void)SO_CONTINUE(int, h_store_aggregator_init_filtered_list, self, type);
}

static int sprite_player_can_draw(const void *self) {
    const unsigned char *base = (const unsigned char *)self;
    const unsigned char *archetype;
    const unsigned char *animation;
    const unsigned char *frame_table;
    const unsigned char *chunk_table;
    uintptr_t chunk_ptr;
    unsigned int frame;
    unsigned int frame_count;
    unsigned int chunk_index;

    if (!trace_is_game_ptr(self)) {
        return 0;
    }

    archetype = *(const unsigned char * const *)(const void *)(base + 0x18);
    animation = *(const unsigned char * const *)(const void *)(base + 0x1c);
    if (!trace_is_game_ptr(archetype) || !trace_is_game_ptr(animation)) {
        return 0;
    }

    frame_table = *(const unsigned char * const *)(const void *)(animation + 0x04);
    chunk_table = *(const unsigned char * const *)(const void *)(archetype + 0x0c);
    frame_count = animation[0x0c];
    frame = base[0x0a];
    if (!trace_is_game_ptr(frame_table) ||
        !trace_is_game_ptr(chunk_table) ||
        frame_count == 0 ||
        frame >= frame_count) {
        return 0;
    }

    chunk_index = *(const uint16_t *)(const void *)(frame_table + frame * 4 + 2);
    chunk_ptr = (uintptr_t)(chunk_table + chunk_index * 8);
    return chunk_ptr >= 0x81000000u && chunk_ptr < 0xA0000000u;
}

static void trace_sprite_player_draw_rect(void *self, const void *rect, int x, int y, int alpha) {
    if (!sprite_player_can_draw(self)) {
        if (trace_allow("CSpritePlayer::Draw/bad_metadata")) {
            const unsigned char *base = (const unsigned char *)self;
            const void *archetype = trace_is_game_ptr(self) ? *(const void * const *)(const void *)(base + 0x18) : NULL;
            const void *animation = trace_is_game_ptr(self) ? *(const void * const *)(const void *)(base + 0x1c) : NULL;

            sceClibPrintf("[PATCH-SPRITE] CSpritePlayer::Draw(this=%p, rect=%p, x=%d, y=%d, alpha=%d) skipped bad metadata arch=%p anim=%p\n",
                          self, rect, x, y, alpha, archetype, animation);
        }
        return;
    }

    (void)SO_CONTINUE(int, h_sprite_player_draw_rect, self, rect, x, y, alpha);
}


static void event_log_cur_guns_stub(void *self) {
    (void)self;
    if (trace_allow("CEventLog::logGameCurGuns/skipped")) {
        sceClibPrintf("[PATCH-EVENTLOG] logGameCurGuns skipped; current-gun inventory data unavailable during Vita boot\n");
    }
}

static void event_log_cur_armor_stub(void *self) {
    (void)self;
    if (trace_allow("CEventLog::logGameCurArmor/skipped")) {
        sceClibPrintf("[PATCH-EVENTLOG] logGameCurArmor skipped; current-armor inventory data unavailable during Vita boot\n");
    }
}

static int trace_menu_stack_update(void *self, int dt) {
    int count = 0;
    int log = trace_allow_count("CMenuStack::Update", 30, 120, &count);
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-MENU] enter CMenuStack::Update#%d(this=%p, dt=%d)\n",
                      count, self, dt);
    }

    ret = SO_CONTINUE(int, h_menu_stack_update, self, dt);

    if (log) {
        sceClibPrintf("[TRACE-MENU] leave CMenuStack::Update#%d(this=%p, dt=%d) -> %d\n",
                      count, self, dt, ret);
    }

    return ret;
}

static int trace_menu_stack_set_menu(void *self, const void *config, unsigned short menu_id, unsigned char flags, int arg) {
    int log = trace_allow("CMenuStack::SetMenu");
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-MENU] enter CMenuStack::SetMenu(this=%p, config=%p, menu=%u, flags=%u, arg=%d)\n",
                      self, config, (unsigned int)menu_id, (unsigned int)flags, arg);
    }

    ret = SO_CONTINUE(int, h_menu_stack_set_menu, self, config, menu_id, flags, arg);

    if (log) {
        sceClibPrintf("[TRACE-MENU] leave CMenuStack::SetMenu(this=%p) -> %d\n", self, ret);
    }

    return ret;
}

static int trace_menu_stack_push_menu(void *self, const void *config, unsigned short menu_id, unsigned char flags, int arg) {
    int log = trace_allow("CMenuStack::PushMenu");
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-MENU] enter CMenuStack::PushMenu(this=%p, config=%p, menu=%u, flags=%u, arg=%d)\n",
                      self, config, (unsigned int)menu_id, (unsigned int)flags, arg);
    }

    ret = SO_CONTINUE(int, h_menu_stack_push_menu, self, config, menu_id, flags, arg);

    if (log) {
        sceClibPrintf("[TRACE-MENU] leave CMenuStack::PushMenu(this=%p) -> %d\n", self, ret);
    }

    return ret;
}

static int trace_menu_stack_pop_menu(void *self, const void *config, unsigned char flags) {
    int log = trace_allow("CMenuStack::PopMenu");
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-MENU] enter CMenuStack::PopMenu(this=%p, config=%p, flags=%u)\n",
                      self, config, (unsigned int)flags);
    }

    ret = SO_CONTINUE(int, h_menu_stack_pop_menu, self, config, flags);

    if (log) {
        sceClibPrintf("[TRACE-MENU] leave CMenuStack::PopMenu(this=%p) -> %d\n", self, ret);
    }

    return ret;
}

static int trace_menu_stack_is_busy(void *self) {
    int log = trace_allow("CMenuStack::IsBusy");
    int ret;

    ret = SO_CONTINUE(int, h_menu_stack_is_busy, self);

    if (log) {
        sceClibPrintf("[TRACE-MENU] CMenuStack::IsBusy(this=%p) -> %d\n", self, ret);
    }

    return ret;
}

static int trace_menu_navigation_bar_hide_buttons(void *self, unsigned char hide) {
    const unsigned char *base = (const unsigned char *)self;
    void *movie = self ? *(void **)(void *)(base + 0x0c) : NULL;
    int log = trace_allow("CMenuNavigationBar::HideButtons");
    int ret;

    if (!movie) {
        sceClibPrintf("[PATCH-MENU] CMenuNavigationBar::HideButtons(this=%p, hide=%u) skipped null movie\n",
                      self, (unsigned int)hide);
        return 0;
    }

    if (log) {
        sceClibPrintf("[TRACE-MENU] enter CMenuNavigationBar::HideButtons(this=%p, movie=%p, hide=%u)\n",
                      self, movie, (unsigned int)hide);
    }

    ret = SO_CONTINUE(int, h_menu_navigation_bar_hide_buttons, self, hide);

    if (log) {
        sceClibPrintf("[TRACE-MENU] leave CMenuNavigationBar::HideButtons(this=%p) -> %d\n", self, ret);
    }

    return ret;
}

static int trace_menu_system_is_menu_busy(void *self) {
    int log = trace_allow("CMenuSystem::IsMenuBusy");
    int ret;

    ret = SO_CONTINUE(int, h_menu_system_is_menu_busy, self);

    if (log) {
        sceClibPrintf("[TRACE-MENU] CMenuSystem::IsMenuBusy(this=%p) -> %d\n", self, ret);
    }

    return ret;
}

static int trace_menu_system_update(void *self, int dt) {
    int count = 0;
    int log = trace_allow_count("CMenuSystem::Update", 30, 120, &count);
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-TICK] enter CMenuSystem::Update#%d(this=%p, dt=%d)\n",
                      count, self, dt);
    }

    ret = SO_CONTINUE(int, h_menu_system_update, self, dt);

    if (log) {
        sceClibPrintf("[TRACE-TICK] leave CMenuSystem::Update#%d(this=%p, dt=%d) -> %d\n",
                      count, self, dt, ret);
    }

    return ret;
}

static int trace_menu_splash_update(void *self, int dt) {
    int count = 0;
    int log = trace_allow_count("CMenuSplash::Update", 30, 120, &count);
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-MENU] enter CMenuSplash::Update#%d(this=%p, dt=%d)\n",
                      count, self, dt);
    }

    ret = SO_CONTINUE(int, h_menu_splash_update, self, dt);

    if (log) {
        sceClibPrintf("[TRACE-MENU] leave CMenuSplash::Update#%d(this=%p, dt=%d) -> %d\n",
                      count, self, dt, ret);
    }

    return ret;
}

static int trace_menu_splash_is_loaded(void *self) {
    int log = trace_allow("CMenuSplash::IsLoaded");
    int ret;

    ret = SO_CONTINUE(int, h_menu_splash_is_loaded, self);

    if (ret) {
        if (!g_splash_loaded_seen) {
            sceClibPrintf("[PATCH-MENU] CMenuSplash::IsLoaded became true; main-menu kick can settle\n");
        }
        g_splash_loaded_seen = 1;
        if (g_main_menu_kick_pending && g_main_menu_kick_delay < 0) {
            g_main_menu_kick_delay = GUNBROS_MAIN_MENU_KICK_DELAY_UPDATES;
        }
    }

    if (log) {
        sceClibPrintf("[TRACE-MENU] CMenuSplash::IsLoaded(this=%p) -> %d\n", self, ret);
    }

    return ret;
}

static int trace_menu_greeting_update(void *self, int dt) {
    int log = trace_allow("CMenuGreeting::Update");
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-MENU] enter CMenuGreeting::Update(this=%p, dt=%d)\n", self, dt);
    }

    ret = SO_CONTINUE(int, h_menu_greeting_update, self, dt);

    if (log) {
        sceClibPrintf("[TRACE-MENU] leave CMenuGreeting::Update(this=%p, dt=%d) -> %d\n", self, dt, ret);
    }

    return ret;
}

static int trace_menu_greeting_is_busy(void *self) {
    int log = trace_allow("CMenuGreeting::IsBusy");
    int ret;

    ret = SO_CONTINUE(int, h_menu_greeting_is_busy, self);

    if (log) {
        sceClibPrintf("[TRACE-MENU] CMenuGreeting::IsBusy(this=%p) -> %d\n", self, ret);
    }

    return ret;
}

static int trace_menu_greeting_is_loaded(void *self) {
    int log = trace_allow("CMenuGreeting::IsLoaded");
    int ret;

    ret = SO_CONTINUE(int, h_menu_greeting_is_loaded, self);

    if (log) {
        sceClibPrintf("[TRACE-MENU] CMenuGreeting::IsLoaded(this=%p) -> %d\n", self, ret);
    }

    return ret;
}

static int gunbros_state(const void *self) {
    const unsigned char *base = (const unsigned char *)self;

    return self ? *(const int *)(const void *)(base + 0x134) : -1;
}

static void gunbros_request_main_menu_kick(void) {
    if (g_main_menu_kick_done || g_main_menu_kick_pending) {
        return;
    }

    g_main_menu_kick_pending = 1;
    g_main_menu_kick_delay = g_splash_loaded_seen ? GUNBROS_MAIN_MENU_KICK_DELAY_UPDATES : -1;
    sceClibPrintf("[PATCH-MENU] loading complete in state=2; waiting for CMenuSplash::IsLoaded before ShowMainMenu(screen=%d)\n",
                  GUNBROS_MAIN_MENU_SCREEN);
}

static void gunbros_maybe_show_main_menu(void *self, const char *source) {
    int state;
    int menu_ret;

    if (!g_main_menu_kick_pending || g_main_menu_kick_done || !h_gunbros_show_main_menu.addr) {
        return;
    }

    state = gunbros_state(self);
    if (state != GUNBROS_STATE_LOADING_SPLASH) {
        sceClibPrintf("[PATCH-MENU] %s: cancel pending ShowMainMenu(screen=%d); state=%d\n",
                      source ? source : "update", GUNBROS_MAIN_MENU_SCREEN, state);
        g_main_menu_kick_pending = 0;
        return;
    }

    if (!g_splash_loaded_seen) {
        return;
    }

    if (g_main_menu_kick_delay < 0) {
        g_main_menu_kick_delay = GUNBROS_MAIN_MENU_KICK_DELAY_UPDATES;
    }

    if (g_main_menu_kick_delay > 0) {
        g_main_menu_kick_delay--;
        return;
    }

    sceClibPrintf("[PATCH-MENU] %s: splash loaded; ShowMainMenu(screen=%d)\n",
                  source ? source : "update", GUNBROS_MAIN_MENU_SCREEN);
    menu_ret = SO_CONTINUE(int, h_gunbros_show_main_menu, self, GUNBROS_MAIN_MENU_SCREEN);
    g_main_menu_kick_done = 1;
    g_main_menu_kick_pending = 0;
    sceClibPrintf("[PATCH-MENU] ShowMainMenu(screen=%d) -> %d state=%d\n",
                  GUNBROS_MAIN_MENU_SCREEN, menu_ret, gunbros_state(self));
}

static int trace_gunbros_bind(void *self) {
    int log = trace_allow("CGunBros::Bind");
    int state_before = gunbros_state(self);
    int ret;

    remember_live_gunbros_self(self, "CGunBros::Bind/enter");

    if (log) {
        sceClibPrintf("[TRACE-LOAD] enter CGunBros::Bind(this=%p)\n", self);
    }

    ret = SO_CONTINUE(int, h_gunbros_bind, self);
    remember_live_gunbros_self(self, "CGunBros::Bind/leave");

    if (state_before == GUNBROS_STATE_LOADING_SPLASH &&
        gunbros_state(self) == GUNBROS_STATE_LOADING_SPLASH &&
        ret == 1 &&
        h_gunbros_show_main_menu.addr) {
        gunbros_request_main_menu_kick();
    }

    if (log) {
        sceClibPrintf("[TRACE-LOAD] leave CGunBros::Bind(this=%p) -> %d\n", self, ret);
    }

    return ret;
}

static int trace_gunbros_load_menus(void *self) {
    int log = trace_allow("CGunBros::LoadMenus");
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-ASSET] enter CGunBros::LoadMenus(this=%p)\n", self);
    }

    ret = SO_CONTINUE(int, h_gunbros_load_menus, self);

    if (log) {
        sceClibPrintf("[TRACE-ASSET] leave CGunBros::LoadMenus(this=%p) -> %d\n", self, ret);
    }

    return ret;
}

static int trace_gunbros_update(void *self, int dt) {
    int count = 0;
    int log = trace_allow_count("CGunBros::Update", 30, 120, &count);
    int ret;

    remember_live_gunbros_self(self, "CGunBros::Update/enter");

    if (log) {
        sceClibPrintf("[TRACE-TICK] enter CGunBros::Update#%d(this=%p, dt=%d)\n",
                      count, self, dt);
    }

    ret = SO_CONTINUE(int, h_gunbros_update, self, dt);
    gameplay_control_bridge_update(dt);
    remember_live_gunbros_self(self, "CGunBros::Update/leave");

    gunbros_maybe_show_main_menu(self, "CGunBros::Update");

    if (log) {
        sceClibPrintf("[TRACE-TICK] leave CGunBros::Update#%d(this=%p, dt=%d) -> %d\n",
                      count, self, dt, ret);
    }

    return ret;
}

static int trace_gunbros_show_main_menu(void *self, int screen) {
    int log = trace_allow("CGunBros::ShowMainMenu");
    int ret;

    remember_live_gunbros_self(self, "CGunBros::ShowMainMenu");

    if (log) {
        sceClibPrintf("[TRACE-MENU] enter CGunBros::ShowMainMenu(this=%p, screen=%d)\n",
                      self, screen);
    }

    ret = SO_CONTINUE(int, h_gunbros_show_main_menu, self, screen);

    if (screen == GUNBROS_MAIN_MENU_SCREEN) {
        g_main_menu_kick_done = 1;
        g_main_menu_kick_pending = 0;
    }

    if (log) {
        sceClibPrintf("[TRACE-MENU] leave CGunBros::ShowMainMenu(this=%p, screen=%d) -> %d\n",
                      self, screen, ret);
    }

    return ret;
}

static int trace_gunbros_set_menu(void *self, int screen) {
    int log = trace_allow("CGunBros::SetMenu");
    int ret;

    remember_live_gunbros_self(self, "CGunBros::SetMenu");

    if (log) {
        sceClibPrintf("[TRACE-MENU] enter CGunBros::SetMenu(this=%p, screen=%d)\n",
                      self, screen);
    }

    ret = SO_CONTINUE(int, h_gunbros_set_menu, self, screen);

    if (log) {
        sceClibPrintf("[TRACE-MENU] leave CGunBros::SetMenu(this=%p, screen=%d) -> %d\n",
                      self, screen, ret);
    }

    return ret;
}

static int trace_resource_load_next(void *self) {
    int count = 0;
    int log = trace_allow_count("CResourceLoader::LoadNext", 20, 240, &count);
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-LOAD] enter CResourceLoader::LoadNext#%d(this=%p)\n",
                      count, self);
    }

    ret = SO_CONTINUE(int, h_resource_load_next, self);

    if (log) {
        sceClibPrintf("[TRACE-LOAD] leave CResourceLoader::LoadNext#%d(this=%p) -> %d\n",
                      count, self, ret);
    }

    return ret;
}

static int trace_menu_stack_load_menu(void *self) {
    int log = trace_allow("CMenuStack::LoadMenu");
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-ASSET] enter CMenuStack::LoadMenu(this=%p)\n", self);
    }

    ret = SO_CONTINUE(int, h_menu_stack_load_menu, self);

    if (log) {
        sceClibPrintf("[TRACE-ASSET] leave CMenuStack::LoadMenu(this=%p) -> %d\n", self, ret);
    }

    return ret;
}

#define TRACE_SELF_LOADER(fn, hookvar, label_text) \
static int fn(void *self, void *loader) { \
    int log = trace_allow(label_text); \
    int ret; \
    if (log) { \
        sceClibPrintf("[TRACE-ASSET] enter " label_text "(this=%p, loader=%p)\n", self, loader); \
    } \
    ret = SO_CONTINUE(int, hookvar, self, loader); \
    if (log) { \
        sceClibPrintf("[TRACE-ASSET] leave " label_text "(this=%p, loader=%p) -> %d\n", self, loader, ret); \
    } \
    return ret; \
}

static int trace_menu_splash_load(void *self, void *loader) {
    int log = trace_allow("CMenuSplash::Load");
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-ASSET] enter CMenuSplash::Load(this=%p, loader=%p)\n", self, loader);
    }

    g_splash_loaded_seen = 0;
    if (g_main_menu_kick_pending) {
        g_main_menu_kick_delay = -1;
    }

    ret = SO_CONTINUE(int, h_menu_splash_load, self, loader);

    if (log) {
        sceClibPrintf("[TRACE-ASSET] leave CMenuSplash::Load(this=%p, loader=%p) -> %d\n",
                      self, loader, ret);
    }

    return ret;
}

TRACE_SELF_LOADER(trace_menu_greeting_load, h_menu_greeting_load, "CMenuGreeting::Load")
TRACE_SELF_LOADER(trace_menu_system_load, h_menu_system_load, "CMenuSystem::Load")
TRACE_SELF_LOADER(trace_movie_load, h_movie_load, "CMovie::Load")
TRACE_SELF_LOADER(trace_movie_sprite_load, h_movie_sprite_load, "CMovieSprite::Load")
TRACE_SELF_LOADER(trace_movie_sound_set_load, h_movie_sound_set_load, "CMovieSoundSet::Load")
TRACE_SELF_LOADER(trace_movie_tiled_sprite_load, h_movie_tiled_sprite_load, "CMovieTiledSprite::Load")

static int trace_menu_system_set_menu(void *self, int screen, unsigned short arg, int branch) {
    int log = trace_allow("CMenuSystem::SetMenu");
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-MENU] enter CMenuSystem::SetMenu(this=%p, screen=%d, arg=%u, branch=%d)\n",
                      self, screen, (unsigned int)arg, branch);
    }

    ret = SO_CONTINUE(int, h_menu_system_set_menu, self, screen, arg, branch);

    if (log) {
        sceClibPrintf("[TRACE-MENU] leave CMenuSystem::SetMenu(this=%p, screen=%d, arg=%u, branch=%d) -> %d\n",
                      self, screen, (unsigned int)arg, branch, ret);
    }

    return ret;
}

static int trace_menu_system_push_menu(void *self, int screen, unsigned short arg, int branch) {
    int log = trace_allow("CMenuSystem::PushMenu");
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-MENU] enter CMenuSystem::PushMenu(this=%p, screen=%d, arg=%u, branch=%d)\n",
                      self, screen, (unsigned int)arg, branch);
    }

    ret = SO_CONTINUE(int, h_menu_system_push_menu, self, screen, arg, branch);

    if (log) {
        sceClibPrintf("[TRACE-MENU] leave CMenuSystem::PushMenu(this=%p, screen=%d, arg=%u, branch=%d) -> %d\n",
                      self, screen, (unsigned int)arg, branch, ret);
    }

    return ret;
}

static int trace_menu_system_pop_menu(void *self, int branch) {
    int log = trace_allow("CMenuSystem::PopMenu");
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-MENU] enter CMenuSystem::PopMenu(this=%p, branch=%d)\n", self, branch);
    }

    ret = SO_CONTINUE(int, h_menu_system_pop_menu, self, branch);

    if (log) {
        sceClibPrintf("[TRACE-MENU] leave CMenuSystem::PopMenu(this=%p, branch=%d) -> %d\n",
                      self, branch, ret);
    }

    return ret;
}

static int trace_res_pack_toc_init(void *self) {
    int log = trace_allow("CResPackTOC::Init");
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-ASSET] enter CResPackTOC::Init(this=%p)\n", self);
    }

    ret = SO_CONTINUE(int, h_res_pack_toc_init, self);

    if (log) {
        sceClibPrintf("[TRACE-ASSET] leave CResPackTOC::Init(this=%p) -> %d\n", self, ret);
    }

    return ret;
}

static int trace_res_pack_toc_bind(void *self, unsigned short pack_idx) {
    int log = trace_allow("CResPackTOC::Bind");
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-ASSET] enter CResPackTOC::Bind(this=%p, pack=%u)\n",
                      self, (unsigned int)pack_idx);
    }

    ret = SO_CONTINUE(int, h_res_pack_toc_bind, self, pack_idx);

    if (log) {
        sceClibPrintf("[TRACE-ASSET] leave CResPackTOC::Bind(this=%p, pack=%u) -> %d\n",
                      self, (unsigned int)pack_idx, ret);
    }

    return ret;
}

static int trace_res_toc_manager_init(void *self) {
    int log = trace_allow("CResTOCManager::Init");
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-ASSET] enter CResTOCManager::Init(this=%p)\n", self);
    }

    ret = SO_CONTINUE(int, h_res_toc_manager_init, self);

    if (log) {
        sceClibPrintf("[TRACE-ASSET] leave CResTOCManager::Init(this=%p) -> %d\n", self, ret);
    }

    return ret;
}

static int trace_res_toc_manager_bind(void *self) {
    int log = trace_allow("CResTOCManager::Bind");
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-ASSET] enter CResTOCManager::Bind(this=%p)\n", self);
    }

    ret = SO_CONTINUE(int, h_res_toc_manager_bind, self);

    if (log) {
        sceClibPrintf("[TRACE-ASSET] leave CResTOCManager::Bind(this=%p) -> %d\n", self, ret);
    }

    return ret;
}

static int trace_res_toc_manager_set_pack_hash(void *self, unsigned int pack_hash) {
    int log = trace_allow("CResTOCManager::SetTargetResPackHash");
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-ASSET] enter CResTOCManager::SetTargetResPackHash(this=%p, hash=0x%08x)\n",
                      self, pack_hash);
    }

    ret = SO_CONTINUE(int, h_res_toc_manager_set_pack_hash, self, pack_hash);

    if (log) {
        sceClibPrintf("[TRACE-ASSET] leave CResTOCManager::SetTargetResPackHash(this=%p, hash=0x%08x) -> %d\n",
                      self, pack_hash, ret);
    }

    return ret;
}

static int trace_res_toc_manager_set_pack_str(void *self, const char *pack_name) {
    int log = trace_allow("CResTOCManager::SetTargetResPackStr");
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-ASSET] enter CResTOCManager::SetTargetResPackStr(this=%p, str=%p '%s')\n",
                      self, pack_name, trace_cstr(pack_name));
    }

    ret = SO_CONTINUE(int, h_res_toc_manager_set_pack_str, self, pack_name);

    if (log) {
        sceClibPrintf("[TRACE-ASSET] leave CResTOCManager::SetTargetResPackStr(this=%p, str=%p '%s') -> %d\n",
                      self, pack_name, trace_cstr(pack_name), ret);
    }

    return ret;
}

static int trace_res_toc_manager_set_pack_internal(void *self, unsigned short pack_idx) {
    int log = trace_allow("CResTOCManager::SetTargetResPackInternal");
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-ASSET] enter CResTOCManager::SetTargetResPackInternal(this=%p, pack=%u)\n",
                      self, (unsigned int)pack_idx);
    }

    ret = SO_CONTINUE(int, h_res_toc_manager_set_pack_internal, self, pack_idx);

    if (log) {
        sceClibPrintf("[TRACE-ASSET] leave CResTOCManager::SetTargetResPackInternal(this=%p, pack=%u) -> %d\n",
                      self, (unsigned int)pack_idx, ret);
    }

    return ret;
}

static int trace_image_pool_load_image(void *self, int format, int image_id,
                                       unsigned short pack_idx, unsigned char arg3,
                                       int arg4, unsigned char arg5, unsigned char arg6) {
    int log = trace_allow("CImagePool::LoadImage");
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-ASSET] enter CImagePool::LoadImage(this=%p, fmt=%d, image=%d, pack=%u, a3=%u, a4=%d, a5=%u, a6=%u)\n",
                      self, format, image_id, (unsigned int)pack_idx,
                      (unsigned int)arg3, arg4, (unsigned int)arg5, (unsigned int)arg6);
    }

    ret = SO_CONTINUE(int, h_image_pool_load_image, self, format, image_id,
                      pack_idx, arg3, arg4, arg5, arg6);

    if (log) {
        sceClibPrintf("[TRACE-ASSET] leave CImagePool::LoadImage(this=%p, fmt=%d, image=%d, pack=%u) -> %d\n",
                      self, format, image_id, (unsigned int)pack_idx, ret);
    }

    return ret;
}

static const void *g_last_good_texture_surface;

static int trace_texture_instr_record_is_valid(const unsigned char *record) {
    const unsigned char *surface;
    int texture_count;
    const void *texture_table;
    unsigned int texture_index;

    surface = *(const unsigned char * const *)(const void *)(record + 4);
    if (!trace_is_game_range(surface, 0x24)) {
        return 0;
    }

    texture_count = *(const int *)(const void *)(surface + 0x1c);
    if (texture_count <= 1) {
        return 1;
    }

    if (texture_count > 4096) {
        return 0;
    }

    texture_index = *(const unsigned int *)(const void *)(record + 8);
    texture_table = *(const void * const *)(const void *)(surface + 0x20);
    return texture_index < (unsigned int)texture_count &&
           trace_is_game_range(texture_table, (size_t)texture_count * sizeof(uint32_t));
}

static int trace_graphics_instr_texture(void *self, unsigned char *stream) {
    uint32_t count;
    uint32_t read_index;
    uint32_t write_index = 0;
    int dropped = 0;

    if (!trace_is_game_range(stream, 0x14)) {
        if (trace_allow("InstrTexure/bad_stream")) {
            sceClibPrintf("[PATCH-GFX] InstrTexure(self=%p, stream=%p) skipped bad stream\n",
                          self, stream);
        }
        return 0;
    }

    count = *(uint32_t *)(void *)(stream + 0x10);
    if (count > 128 ||
        !trace_is_game_range(stream + 0x14, (size_t)count * 12u)) {
        if (trace_allow("InstrTexure/bad_count")) {
            sceClibPrintf("[PATCH-GFX] InstrTexure(self=%p, stream=%p) clamped bad count=%u\n",
                          self, stream, (unsigned int)count);
        }
        *(uint32_t *)(void *)(stream + 0x10) = 0;
        return 0;
    }

    for (read_index = 0; read_index < count; ++read_index) {
        unsigned char *record = stream + 0x14 + read_index * 12u;

        if (!trace_texture_instr_record_is_valid(record)) {
            const void *surface = *(const void * const *)(const void *)(record + 4);
            unsigned int tex_index = *(const unsigned int *)(const void *)(record + 8);

            if (!trace_is_game_range(surface, 0x24) &&
                trace_is_game_range(g_last_good_texture_surface, 0x24)) {
                *(const void **)(void *)(record + 4) = g_last_good_texture_surface;
                if (!trace_texture_instr_record_is_valid(record)) {
                    *(unsigned int *)(void *)(record + 8) = 0;
                }
                if (trace_texture_instr_record_is_valid(record)) {
                    if (trace_allow("InstrTexure/repair_null_surface")) {
                        sceClibPrintf("[FIX-GFX] InstrTexure repaired null surface index=%u old_surface=%p fallback=%p old_tex=%u new_tex=%u\n",
                                      (unsigned int)read_index, surface, g_last_good_texture_surface,
                                      tex_index, *(unsigned int *)(void *)(record + 8));
                    }
                    goto texture_record_valid;
                }
            } else if (trace_is_game_range(surface, 0x24)) {
                *(unsigned int *)(void *)(record + 8) = 0;
                if (trace_texture_instr_record_is_valid(record)) {
                    if (trace_allow("InstrTexure/repair_bad_index")) {
                        sceClibPrintf("[FIX-GFX] InstrTexure clamped texture index=%u old_tex=%u surface=%p\n",
                                      (unsigned int)read_index, tex_index, surface);
                    }
                    goto texture_record_valid;
                }
            }

            if (trace_allow("InstrTexure/drop_null_surface")) {
                sceClibPrintf("[PATCH-GFX] InstrTexure dropped record index=%u surface=%p tex_index=%u fallback=%p\n",
                              (unsigned int)read_index,
                              surface, tex_index, g_last_good_texture_surface);
            }
            dropped = 1;
            continue;
        }

texture_record_valid:
        g_last_good_texture_surface = *(const void * const *)(const void *)(record + 4);

        if (write_index != read_index) {
            sceClibMemcpy(stream + 0x14 + write_index * 12u, record, 12);
        }
        write_index++;
    }

    if (dropped) {
        *(uint32_t *)(void *)(stream + 0x10) = write_index;
    }

    return SO_CONTINUE(int, h_graphics_instr_texture, self, stream);
}

static void hook_symbol(const char *label, const char *symbol, uintptr_t replacement) {
    uintptr_t addr = so_symbol(&so_mod, symbol);

    if (!addr) {
        sceClibPrintf("[PATCH-CNGS] missing %s (%s)\n",
                      label ? label : "(null)",
                      symbol ? symbol : "(null)");
        return;
    }

    hook_addr(addr, replacement);
    sceClibPrintf("[PATCH-CNGS] hook %s at %p -> %p\n",
                  label ? label : "(null)",
                  (void *)addr,
                  (void *)replacement);
}

static void hook_symbol_store(const char *label, const char *symbol, uintptr_t replacement, so_hook *out) {
    uintptr_t addr = so_symbol(&so_mod, symbol);

    if (!addr) {
        sceClibPrintf("[TRACE-MENU] missing %s (%s)\n",
                      label ? label : "(null)",
                      symbol ? symbol : "(null)");
        return;
    }

    *out = hook_addr(addr, replacement);
    sceClibPrintf("[TRACE-MENU] hook %s at %p -> %p\n",
                  label ? label : "(null)",
                  (void *)addr,
                  (void *)replacement);
}

static uintptr_t alloc_patch_cave(size_t size) {
    uintptr_t cave = ALIGN_MEM(so_mod.patch_head, 4);
    uintptr_t end = cave + ALIGN_MEM(size, 4);

    if (end > so_mod.patch_base + so_mod.patch_size) {
        sceClibPrintf("[PATCH] no patch cave space for %u bytes\n", (unsigned int)size);
        return 0;
    }

    so_mod.patch_head = end;
    return cave;
}

static void patch_graphics2d_draw_null_surface(void) {
    static const char draw_symbol[] =
        "_ZN3com3glu8platform8graphics17CGraphics2d_OGLES4DrawERNS2_15ICRenderSurfaceEPKNS1_10components10CRectangleIsEEPKNS6_10Color_RGBAENS2_12ICGraphics2d4FlipE";
    uintptr_t draw = so_symbol(&so_mod, draw_symbol);
    uintptr_t patch_at;
    uintptr_t resume_at;
    uintptr_t bail_at;
    uintptr_t cave;
    uint32_t code[8];

    if (!draw) {
        sceClibPrintf("[PATCH] CGraphics2d_OGLES::Draw symbol not found\n");
        return;
    }

    patch_at = draw + 0xf4;   /* ldrh r1, [r3, #0x12] */
    resume_at = draw + 0xfc;  /* mov ip, #0 */
    bail_at = draw + 0x19c;   /* add sp, sp, #0x174; pop {..., pc} */
    cave = alloc_patch_cave(sizeof(code));
    if (!cave) {
        return;
    }

    code[0] = 0xe3530000;          /* cmp r3, #0 */
    code[1] = 0x1a000001;          /* bne normal */
    code[2] = 0xe51ff004;          /* ldr pc, [pc, #-4] */
    code[3] = (uint32_t)bail_at;
    code[4] = 0xe1d311b2;          /* ldrh r1, [r3, #0x12] */
    code[5] = 0xe59d00a8;          /* ldr r0, [sp, #0xa8] */
    code[6] = 0xe51ff004;          /* ldr pc, [pc, #-4] */
    code[7] = (uint32_t)resume_at;

    kuKernelCpuUnrestrictedMemcpy((void *)cave, code, sizeof(code));
    kuKernelFlushCaches((void *)cave, sizeof(code));
    hook_arm(patch_at, cave);

    sceClibPrintf("[PATCH] CGraphics2d_OGLES::Draw null surface guard at %p -> %p\n",
                  (void *)patch_at, (void *)cave);
}

static void patch_brother_bind_missing_progress_table(void) {
    static const char bind_symbol[] =
        "_ZN8CBrother4BindEP4CMapPKNS_8TemplateEP20CPlayerConfigurationRK15CPlayerProgress";
    uintptr_t bind = so_symbol(&so_mod, bind_symbol);
    uintptr_t patch_at;
    uintptr_t resume_at;
    uintptr_t cave;
    uint32_t code[8];
    uint32_t clear_code[11];

    if (!bind) {
        sceClibPrintf("[PATCH] CBrother::Bind symbol not found\n");
        return;
    }

    patch_at = bind + 0x90;   /* ldrsh r0, [r2, sl] */
    resume_at = bind + 0x94;  /* __aeabi_i2f(r0) */
    cave = alloc_patch_cave(sizeof(code));
    if (!cave) {
        return;
    }

    code[0] = 0xe3520000;          /* cmp r2, #0 */
    code[1] = 0x1a000002;          /* bne normal */
    code[2] = 0xe3a00064;          /* mov r0, #100 */
    code[3] = 0xe51ff004;          /* ldr pc, [pc, #-4] */
    code[4] = (uint32_t)resume_at;
    code[5] = 0xe19200fa;          /* ldrsh r0, [r2, sl] */
    code[6] = 0xe51ff004;          /* ldr pc, [pc, #-4] */
    code[7] = (uint32_t)resume_at;

    kuKernelCpuUnrestrictedMemcpy((void *)cave, code, sizeof(code));
    kuKernelFlushCaches((void *)cave, sizeof(code));
    hook_arm(patch_at, cave);

    sceClibPrintf("[PATCH] CBrother::Bind missing progress table guard at %p -> %p\n",
                  (void *)patch_at, (void *)cave);

    patch_at = bind + 0x114;   /* ldr r2, [r1, r2] */
    resume_at = bind + 0x118;  /* mov r3, #0x730 */
    cave = alloc_patch_cave(sizeof(code));
    if (!cave) {
        return;
    }

    code[0] = 0xe3510000;          /* cmp r1, #0 */
    code[1] = 0x1a000002;          /* bne normal */
    code[2] = 0xe3a02064;          /* mov r2, #100 */
    code[3] = 0xe51ff004;          /* ldr pc, [pc, #-4] */
    code[4] = (uint32_t)resume_at;
    code[5] = 0xe7912002;          /* ldr r2, [r1, r2] */
    code[6] = 0xe51ff004;          /* ldr pc, [pc, #-4] */
    code[7] = (uint32_t)resume_at;

    kuKernelCpuUnrestrictedMemcpy((void *)cave, code, sizeof(code));
    kuKernelFlushCaches((void *)cave, sizeof(code));
    hook_arm(patch_at, cave);

    sceClibPrintf("[PATCH] CBrother::Bind missing XP delta table guard at %p -> %p\n",
                  (void *)patch_at, (void *)cave);

    patch_at = bind + 0x148;   /* bl np_memset */
    resume_at = bind + 0x150;  /* mov r2, r1 */
    cave = alloc_patch_cave(sizeof(clear_code));
    if (!cave) {
        return;
    }

    clear_code[0] = 0xe3500a01;       /* cmp r0, #0x1000 */
    clear_code[1] = 0x3a000005;       /* blo done */
    clear_code[2] = 0xe3a01000;       /* mov r1, #0 */
    clear_code[3] = 0xe3a02058;       /* mov r2, #0x58 */
    clear_code[4] = 0xe1a03000;       /* mov r3, r0 */
    clear_code[5] = 0xe4c31001;       /* loop: strb r1, [r3], #1 */
    clear_code[6] = 0xe2522001;       /* subs r2, r2, #1 */
    clear_code[7] = 0x1afffffc;       /* bne loop */
    clear_code[8] = 0xe3a01e73;       /* done: mov r1, #0x730 */
    clear_code[9] = 0xe51ff004;       /* ldr pc, [pc, #-4] */
    clear_code[10] = (uint32_t)resume_at;

    kuKernelCpuUnrestrictedMemcpy((void *)cave, clear_code, sizeof(clear_code));
    kuKernelFlushCaches((void *)cave, sizeof(clear_code));
    hook_arm(patch_at, cave);

    sceClibPrintf("[PATCH] CBrother::Bind local clear guard at %p -> %p\n",
                  (void *)patch_at, (void *)cave);
}

static void patch_brother_function_resolver_bad_vtable(void) {
    static const char resolver_symbol[] =
        "_ZN8CBrother16FunctionResolverEP14IScriptContextaPsj";
    uintptr_t resolver = so_symbol(&so_mod, resolver_symbol);
    uintptr_t patch_at;
    uintptr_t resume_at;
    uintptr_t cave;
    uint32_t code[8];

    if (!resolver) {
        sceClibPrintf("[PATCH] CBrother::FunctionResolver symbol not found\n");
        return;
    }

    patch_at = resolver + 0x1e0;   /* mov lr, pc; ldr pc, [r3, #0x11c] */
    resume_at = resolver + 0x1e8;  /* mov r0, #0 */
    cave = alloc_patch_cave(sizeof(code));
    if (!cave) {
        return;
    }

    code[0] = 0xe3530498;          /* cmp r3, #0x98000000 */
    code[1] = 0x3a000003;          /* blo skip */
    code[2] = 0xe3530499;          /* cmp r3, #0x99000000 */
    code[3] = 0x2a000001;          /* bhs skip */
    code[4] = 0xe59fe004;          /* ldr lr, [pc, #4] */
    code[5] = 0xe593f11c;          /* ldr pc, [r3, #0x11c] */
    code[6] = 0xe51ff004;          /* skip: ldr pc, [pc, #-4] */
    code[7] = (uint32_t)resume_at;

    kuKernelCpuUnrestrictedMemcpy((void *)cave, code, sizeof(code));
    kuKernelFlushCaches((void *)cave, sizeof(code));
    hook_arm(patch_at, cave);

    sceClibPrintf("[PATCH] CBrother::FunctionResolver vtable guard at %p -> %p\n",
                  (void *)patch_at, (void *)cave);
}

static void patch_brother_spawn_bad_vtable(void) {
    static const char spawn_symbol[] =
        "_ZN8CBrother5SpawnEPKNS_9SpawnDataE";
    uintptr_t spawn = so_symbol(&so_mod, spawn_symbol);
    uintptr_t patch_at;
    uintptr_t resume_at;
    uintptr_t cave;
    uint32_t code[8];

    if (!spawn) {
        sceClibPrintf("[PATCH] CBrother::Spawn symbol not found\n");
        return;
    }

    patch_at = spawn + 0x44;   /* mov lr, pc; ldr pc, [r3, #0x110] */
    resume_at = spawn + 0x4c;  /* ldrh r0, [r5] */
    cave = alloc_patch_cave(sizeof(code));
    if (!cave) {
        return;
    }

    code[0] = 0xe3530498;          /* cmp r3, #0x98000000 */
    code[1] = 0x3a000003;          /* blo skip */
    code[2] = 0xe3530499;          /* cmp r3, #0x99000000 */
    code[3] = 0x2a000001;          /* bhs skip */
    code[4] = 0xe59fe004;          /* ldr lr, [pc, #4] */
    code[5] = 0xe593f110;          /* ldr pc, [r3, #0x110] */
    code[6] = 0xe51ff004;          /* skip: ldr pc, [pc, #-4] */
    code[7] = (uint32_t)resume_at;

    kuKernelCpuUnrestrictedMemcpy((void *)cave, code, sizeof(code));
    kuKernelFlushCaches((void *)cave, sizeof(code));
    hook_arm(patch_at, cave);

    sceClibPrintf("[PATCH] CBrother::Spawn vtable guard at %p -> %p\n",
                  (void *)patch_at, (void *)cave);
}
#define BROTHER_SIZE_GUESS 0xbb0u
#define BROTHER_OFF_GUNCTX 0x210u
#define BROTHER_OFF_MOVE_B 0x6f4u
#define BROTHER_OFF_MOVE_A 0x6f8u
#define BROTHER_OFF_MAP    0x6fcu
#define BROTHER_OFF_CFG    0x780u
#define BROTHER_OFF_ACTIVE 0x785u
#define BROTHER_INTERP_BASE   0x68u
#define BROTHER_INTERP_NORMAL 0x310u
#define SCRIPT_INTERP_STATE   0x30u

static uintptr_t brother_default_vptr(void) {
    uintptr_t vtable = so_symbol(&so_mod, "_ZTV8CBrother");
    return vtable ? vtable + 8 : 0;
}

static uintptr_t player_default_vptr(void) {
    uintptr_t vtable = so_symbol(&so_mod, "_ZTV7CPlayer");
    return vtable ? vtable + 8 : 0;
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
typedef void (*brother_on_shoot_fn_t)(void *self, float angle);
typedef void (*brother_swap_gun_fn_t)(void *self);
typedef void (*brother_set_gun_fn_t)(void *self, unsigned char subtype, unsigned short id, int slot);
typedef void (*gun_fire_direct_fn_t)(void *self, int a, int b, float c, float d, float e, unsigned char h);
typedef void (*gun_shoot_event_fn_t)(void *self);

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
static int g_gameplay_fire_accum_ms;
static volatile unsigned int g_gameplay_swap_request_seq;
static unsigned int g_gameplay_swap_handled_seq;

static float gb_absf(float v) {
    return v < 0.0f ? -v : v;
}

static float gb_clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static brother_move_fn_t gameplay_brother_move_fn(void) {
    static brother_move_fn_t fn;
    if (!fn) {
        fn = (brother_move_fn_t)(uintptr_t)so_symbol(&so_mod, "_ZN8CBrother4MoveERK4vec2");
        if (!fn) {
            sceClibPrintf("[FIX-CONTROL] CBrother::Move symbol missing\n");
        }
    }
    return fn;
}

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

static brother_fire_fn_t gameplay_brother_fire_fn(void) {
    static brother_fire_fn_t fn;
    if (!fn) {
        fn = (brother_fire_fn_t)(uintptr_t)so_symbol(&so_mod, "_ZN8CBrother10FireBulletEv");
        if (!fn) {
            sceClibPrintf("[FIX-CONTROL] CBrother::FireBullet symbol missing\n");
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

static brother_swap_gun_fn_t gameplay_brother_swap_gun_fn(void) {
    static brother_swap_gun_fn_t fn;
    if (!fn) {
        fn = (brother_swap_gun_fn_t)(uintptr_t)so_symbol(&so_mod, "_ZN8CBrother7SwapGunEv");
        if (!fn) {
            sceClibPrintf("[FIX-CONTROL] CBrother::SwapGun symbol missing\n");
        }
    }
    return fn;
}

static brother_swap_gun_fn_t gameplay_player_on_swap_gun_fn(void) {
    static brother_swap_gun_fn_t fn;
    if (!fn) {
        fn = (brother_swap_gun_fn_t)(uintptr_t)so_symbol(&so_mod, "_ZN7CPlayer9OnSwapGunEv");
        if (!fn) {
            sceClibPrintf("[FIX-CONTROL] CPlayer::OnSwapGun symbol missing\n");
        }
    }
    return fn;
}

void gunbros_request_player_gun_swap(void) {
    g_gameplay_swap_request_seq++;
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

static brother_move_fn_t gameplay_player_move_fn(void) {
    static brother_move_fn_t fn;
    if (!fn) {
        fn = (brother_move_fn_t)(uintptr_t)so_symbol(&so_mod, "_ZN7CPlayer4MoveERK4vec2");
        if (!fn) {
            sceClibPrintf("[FIX-CONTROL] CPlayer::Move symbol missing\n");
        }
    }
    return fn;
}

static brother_fire_fn_t gameplay_player_fire_fn(void) {
    static brother_fire_fn_t fn;
    if (!fn) {
        fn = (brother_fire_fn_t)(uintptr_t)so_symbol(&so_mod, "_ZN7CPlayer10FireBulletEv");
        if (!fn) {
            sceClibPrintf("[FIX-CONTROL] CPlayer::FireBullet symbol missing\n");
        }
    }
    return fn;
}

static gun_fire_direct_fn_t gameplay_gun_fire_direct_fn(void) {
    static gun_fire_direct_fn_t fn;
    if (!fn) {
        fn = (gun_fire_direct_fn_t)(uintptr_t)so_symbol(&so_mod, "_ZN4CGun10FireBulletEiifffh");
        if (!fn) {
            sceClibPrintf("[FIX-CONTROL] CGun::FireBullet direct symbol missing\n");
        }
    }
    return fn;
}

static gun_shoot_event_fn_t gameplay_gun_shoot_start_fn(void) {
    static gun_shoot_event_fn_t fn;
    if (!fn) {
        fn = (gun_shoot_event_fn_t)(uintptr_t)so_symbol(&so_mod, "_ZN4CGun12OnShootStartEv");
        if (!fn) {
            sceClibPrintf("[FIX-CONTROL] CGun::OnShootStart symbol missing\n");
        }
    }
    return fn;
}

static int gameplay_gun_can_fire_direct(void *gun) {
    void *bullet_pool;
    void *bullet_template;
    if (!trace_is_game_range(gun, 0xf4u)) {
        return 0;
    }
    bullet_template = *(void **)(void *)((unsigned char *)gun + 0x2c);
    bullet_pool = *(void **)(void *)((unsigned char *)gun + 0x78);
    return trace_is_game_ptr(bullet_template) && trace_is_game_ptr(bullet_pool) &&
           trace_is_game_ptr(*(void **)(void *)bullet_pool);
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
        return;
    }

    if (!g_gameplay_companion_brother || score >= gameplay_brother_score_candidate(g_gameplay_companion_brother, NULL, NULL)) {
        g_gameplay_companion_brother = brother;
    }
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
    if (trace_is_game_range(g_gameplay_primary_brother, BROTHER_SIZE_GUESS) && brother_has_bound_state(g_gameplay_primary_brother)) {
        if (trace_allow_ex("gameplay-using-fallback-not-player", 8, 240)) {
            sceClibPrintf("[FIX-CONTROL] WARNING using non-player fallback target=%p vptr=%p; player target not captured yet\n",
                          g_gameplay_primary_brother,
                          trace_is_game_range(g_gameplay_primary_brother, sizeof(void *)) ? *(void **)g_gameplay_primary_brother : NULL);
        }
        return g_gameplay_primary_brother;
    }
    return NULL;
}

static void gameplay_apply_move(void *brother, int dt, float nx, float ny) {
    unsigned char *base = (unsigned char *)brother;
    int is_player = brother_is_player_object(brother);
    brother_move_fn_t player_move = gameplay_player_move_fn();
    brother_move_fn_t on_move = gameplay_brother_on_move_fn();
    brother_move_fn_t brother_move = gameplay_brother_move_fn();
    float before_x;
    float before_y;
    float after_x;
    float after_y;
    float raw[2];
    float step[2];
    float speed;

    if (!trace_is_game_range(brother, BROTHER_SIZE_GUESS)) {
        return;
    }

    nx = gb_clampf(nx, -1.0f, 1.0f);
    ny = gb_clampf(ny, -1.0f, 1.0f);
    if (gb_absf(nx) < 0.05f && gb_absf(ny) < 0.05f) {
        return;
    }

    repair_brother_runtime_state(brother, is_player ? "gameplay-control/player-move" : "gameplay-control/nonplayer-move");
    base[0x785] = 1; /* active */
    base[0x786] = 1; /* visible */

    raw[0] = nx;
    raw[1] = ny;
    before_x = *(float *)(void *)(base + 0x708);
    before_y = *(float *)(void *)(base + 0x70c);
    if (is_player && player_move) {
        speed = (float)dt * 0.62f; 
        step[0] = nx * speed;
        step[1] = ny * speed;
        player_move(brother, step);
    } else {
        if (on_move) {
            on_move(brother, raw);
        }

        after_x = *(float *)(void *)(base + 0x708);
        after_y = *(float *)(void *)(base + 0x70c);
        if (brother_move && gb_absf(after_x - before_x) < 0.001f && gb_absf(after_y - before_y) < 0.001f) {
            speed = (float)dt * 0.62f;
            step[0] = nx * speed;
            step[1] = ny * speed;
            brother_move(brother, step);
        }
    }

    after_x = *(float *)(void *)(base + 0x708);
    after_y = *(float *)(void *)(base + 0x70c);
    if (trace_allow_ex(is_player ? "gameplay-player-move" : "gameplay-nonplayer-move", 16, 120)) {
        sceClibPrintf("[FIX-CONTROL] %s Move target=%p input=(%.2f,%.2f) pos=(%.1f,%.1f)->(%.1f,%.1f) vptr=%p\n",
                      is_player ? "PLAYER" : "NONPLAYER", brother, nx, ny,
                      before_x, before_y, after_x, after_y,
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

static void gameplay_force_player_gun(unsigned int subtype, unsigned int id, const char *source) {
    void *brother = gameplay_find_brother_target();
    void *cfg;
    void *before_gun;
    void *after_gun;
    unsigned int slot;
    brother_set_gun_fn_t set_gun = gameplay_brother_set_gun_fn();

    if (!trace_is_game_range(brother, BROTHER_SIZE_GUESS)) {
        sceClibPrintf("[FIX-CONTROL] force gun skipped source=%s id=%u subtype=%u no player\n",
                      source ? source : "?", id, subtype);
        return;
    }

    cfg = *(void **)(void *)((unsigned char *)brother + BROTHER_OFF_CFG);
    slot = gameplay_player_active_gun_index(brother);
    if (slot > 1) {
        slot = 0;
    }

    if (trace_is_game_range(cfg, 0x50u)) {
        unsigned char *c = (unsigned char *)cfg;
        unsigned int off = 0x10u + slot * 8u;
        c[off + 0] = (unsigned char)(id & 0xffu);
        c[off + 1] = (unsigned char)((id >> 8) & 0xffu);
        c[off + 2] = (unsigned char)(subtype & 0xffu);
        c[0x4c] = (unsigned char)slot;
    }

    before_gun = *(void **)(void *)((unsigned char *)brother + 0x210);
    if (set_gun) {
        set_gun(brother, (unsigned char)subtype, (unsigned short)id, (int)slot);
    }
    after_gun = *(void **)(void *)((unsigned char *)brother + 0x210);
    repair_brother_runtime_state(brother, "gameplay-control/force-gun");
    sceClibPrintf("[FIX-CONTROL] force active gun source=%s target=%p slot=%u id=%u subtype=%u gun=%p->%p cfg=%p\n",
                  source ? source : "?", brother, slot, id, subtype, before_gun, after_gun, cfg);
}

static void gameplay_apply_pending_swap(void *brother) {
    unsigned int req = g_gameplay_swap_request_seq;
    unsigned int before_idx;
    unsigned int after_idx;
    void *before_gun;
    void *after_gun;
    brother_swap_gun_fn_t player_on_swap = gameplay_player_on_swap_gun_fn();
    brother_swap_gun_fn_t swap_gun = gameplay_brother_swap_gun_fn();
    int is_player;

    if (req == g_gameplay_swap_handled_seq) {
        return;
    }
    g_gameplay_swap_handled_seq = req;

    if (!trace_is_game_range(brother, BROTHER_SIZE_GUESS)) {
        sceClibPrintf("[FIX-CONTROL] gun swap skipped: no valid player target req=%u\n", req);
        return;
    }

    is_player = brother_is_player_object(brother);
    before_idx = gameplay_player_active_gun_index(brother);
    before_gun = *(void **)(void *)((unsigned char *)brother + 0x210);

    if (is_player && player_on_swap) {
        player_on_swap(brother);
    }
    if (swap_gun) {
        swap_gun(brother);
    }

    repair_brother_runtime_state(brother, "gameplay-control/gun-swap");
    after_idx = gameplay_player_active_gun_index(brother);
    after_gun = *(void **)(void *)((unsigned char *)brother + 0x210);
    sceClibPrintf("[FIX-CONTROL] gun swap target=%p is_player=%d cfg_idx=%u->%u gun=%p->%p req=%u\n",
                  brother, is_player, before_idx, after_idx, before_gun, after_gun, req);
}

static void gameplay_apply_fire(void *brother, int dt, float ax, float ay) {
    unsigned char *base = (unsigned char *)brother;
    int is_player = brother_is_player_object(brother);
    brother_fire_fn_t fire = is_player ? gameplay_player_fire_fn() : gameplay_brother_fire_fn();
    brother_on_shoot_fn_t on_shoot = gameplay_brother_on_shoot_fn();
    gun_shoot_event_fn_t gun_shoot_start = gameplay_gun_shoot_start_fn();
    void *gun;
    float angle;
    float mag;
    unsigned int active_idx;

    if (!trace_is_game_range(brother, BROTHER_SIZE_GUESS)) {
        return;
    }

    ax = gb_clampf(ax, -1.0f, 1.0f);
    ay = gb_clampf(ay, -1.0f, 1.0f);
    mag = ax * ax + ay * ay;
    if (mag < 0.01f) {
        ax = 1.0f;
        ay = 0.0f;
    }

    gun = *(void **)(void *)(base + 0x210);
    if (!trace_is_game_range(gun, 0xf4u)) {
        if (trace_allow_ex("gameplay-fire-no-gun", 8, 180)) {
            sceClibPrintf("[FIX-CONTROL] fire skipped target=%p is_player=%d gun=%p vptr=%p\n",
                          brother, is_player, gun,
                          trace_is_game_range(brother, sizeof(void *)) ? *(void **)brother : NULL);
        }
        return;
    }

    repair_brother_runtime_state(brother, is_player ? "gameplay-control/player-fire" : "gameplay-control/nonplayer-fire");
    base[0x785] = 1;
    base[0x786] = 1;
    angle = atan2f(ay, ax);
    *(float *)(void *)(base + 0x718) = angle;
    if (on_shoot) {
        on_shoot(brother, angle);
    } else {
        base[0x788] = 1;
        base[0x73c] = 1;
    }
    *(int *)(void *)((unsigned char *)gun + 0x7c) = 0;
    *(int *)(void *)((unsigned char *)gun + 0x84) = 0;
    if (gun_shoot_start) {
        gun_shoot_start(gun);
    }

    g_gameplay_fire_accum_ms += dt;
    if (g_gameplay_fire_accum_ms >= 65) {
        g_gameplay_fire_accum_ms = 0;
        if (fire) {
            fire(brother);
        }
        active_idx = gameplay_player_active_gun_index(brother);
        if (trace_allow_ex(is_player ? "gameplay-player-fire" : "gameplay-nonplayer-fire", 20, 90)) {
            sceClibPrintf("[FIX-CONTROL] %s native Fire target=%p gun=%p cfg_idx=%u aim=(%.2f,%.2f) angle=%.3f shooting=%u vptr=%p\n",
                          is_player ? "PLAYER" : "NONPLAYER", brother, gun, active_idx,
                          ax, ay, angle, base[0x788],
                          trace_is_game_range(brother, sizeof(void *)) ? *(void **)brother : NULL);
        }
    }
}



static void gameplay_control_bridge_update(int dt) {
    GunBrosGameplayInputState st = g_gunbros_gameplay_input;
    void *brother;

    if (!st.move_active && !st.fire_active) {
        void *b = gameplay_find_brother_target();
        if (trace_is_game_range(b, BROTHER_SIZE_GUESS)) {
            gameplay_apply_pending_swap(b);
            ((unsigned char *)b)[0x788] = 0;
        }
        g_gameplay_fire_accum_ms = 120;
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
        ((unsigned char *)brother)[0x788] = 0;
        g_gameplay_fire_accum_ms = 120;
    }
}


static int trace_player_bind_trace(void *self, void *map, const void *templ, const void *config, const void *progress) {
    int ret;
    void *vptr_before = trace_is_game_range(self, sizeof(void *)) ? *(void **)self : NULL;

    if (trace_allow("CPlayer::Bind/diag-enter")) {
        sceClibPrintf("[DIAG-PLAYER] enter CPlayer::Bind player=%p map=%p templ=%p config=%p progress=%p vptr_before=%p\n",
                      self, map, templ, config, progress, vptr_before);
    }

    ret = SO_CONTINUE(int, h_player_bind_trace, self, map, templ, config, progress);
    repair_brother_vptr_preserve_state(self, "CPlayer::Bind/vptr-repair", vptr_before);
    repair_brother_runtime_state(self, "CPlayer::Bind/runtime");
    gameplay_register_brother(self, config, progress, "CPlayer::Bind");

    if (trace_allow("CPlayer::Bind/diag-leave")) {
        trace_dump_brother_state("CPlayer::Bind/after", self);
    }
    return ret;
}

static int trace_player_update_trace(void *self, int dt) {
    int ret;

    if (trace_is_game_range(self, BROTHER_SIZE_GUESS)) {
        gameplay_register_brother(self,
                                  *(void **)(void *)((unsigned char *)self + BROTHER_OFF_CFG),
                                  NULL,
                                  "CPlayer::Update");
    }

    ret = SO_CONTINUE(int, h_player_update_trace, self, dt);
    if (trace_allow_ex("CPlayer::Update/diag", 12, 240)) {
        trace_dump_brother_state("CPlayer::Update/after", self);
    }
    return ret;
}

static int trace_brother_bind_trace(void *self, void *map, const void *templ, const void *config, const void *progress) {
    const void *safe_config = config;
    const void *safe_progress = progress;
    void *vptr_before = NULL;
    int ret;

    if (trace_is_game_range(self, BROTHER_SIZE_GUESS)) {
        vptr_before = *(void **)self;
    }

    construct_brother_if_uninitialized(self, "CBrother::Bind/pre-ctor");

    if (!trace_is_player_config_for_brother(safe_config)) {
        safe_config = vita_default_config();
    }
    if (!trace_is_player_progress_for_brother(safe_progress)) {
        safe_progress = vita_default_progress();
    }

    if (trace_allow("CBrother::Bind/diag-enter")) {
        sceClibPrintf("[DIAG-BIND] enter brother=%p map=%p templ=%p config=%p->%p progress=%p->%p vptr_before=%p\n",
                      self, map, templ, config, safe_config, progress, safe_progress, vptr_before);
        trace_dump_brother_state("CBrother::Bind/before", self);
    }

    ret = SO_CONTINUE(int, h_brother_bind_trace, self, map, templ, safe_config, safe_progress);
    repair_brother_vptr_preserve_state(self, "CBrother::Bind/vptr-repair", vptr_before);
    repair_brother_runtime_state(self, "CBrother::Bind/runtime");
    gameplay_register_brother(self, safe_config, safe_progress, "CBrother::Bind");

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
    uint32_t word;

    if (!fn) {
        sceClibPrintf("[PATCH] CLevel::OnEnemyKilled mission-null guard: symbol not found\n");
        return;
    }

    patch_at = fn + 0x8b8;  /* ldr r3, [r0, #0x3c] after CGameFlow::GetMission */
    resume_at = fn + 0x8bc; /* cmp r3, #2 */
    word = *(volatile uint32_t *)patch_at;
    if (word != 0xe590303cu) {
        sceClibPrintf("[PATCH] CLevel::OnEnemyKilled mission-null guard skipped; unexpected word at %p: %08x\n",
                      (void *)patch_at, word);
        return;
    }

    cave = alloc_patch_cave(sizeof(code));
    if (!cave) {
        return;
    }

    code[0] = 0xe3500000u;       /* cmp r0, #0 */
    code[1] = 0x1590303cu;       /* ldrne r3, [r0, #0x3c] */
    code[2] = 0x03a03000u;       /* moveq r3, #0 */
    code[3] = 0xe51ff004u;       /* ldr pc, [pc, #-4] */
    code[4] = (uint32_t)resume_at;
    code[5] = 0xe1a00000u;
    code[6] = 0xe1a00000u;

    kuKernelCpuUnrestrictedMemcpy((void *)cave, code, sizeof(code));
    kuKernelFlushCaches((void *)cave, sizeof(code));
    hook_arm(patch_at, cave);
    kuKernelFlushCaches((void *)patch_at, 8);

    sceClibPrintf("[PATCH] CLevel::OnEnemyKilled mission-null guard at %p -> %p; word=%08x\n",
                  (void *)patch_at, (void *)cave, *(volatile uint32_t *)patch_at);
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
    uint32_t base_table_code[9];
    uint32_t lookup_code[15];
    uint32_t caller_code[8];

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

    base_table_code[0] = 0xe3530000;       /* cmp r3, #0 */
    base_table_code[1] = 0x1a000002;       /* bne normal */
    base_table_code[2] = 0xe3a00000;       /* mov r0, #0 */
    base_table_code[3] = 0xe51ff004;       /* ldr pc, [pc, #-4] */
    base_table_code[4] = (uint32_t)epilogue_at;
    base_table_code[5] = 0xe593c028;       /* normal: ldr ip, [r3, #0x28] (orig +0x68) */
    base_table_code[6] = 0xe5930024;       /* ldr r0, [r3, #0x24] (orig +0x6c) */
    base_table_code[7] = 0xe51ff004;       /* ldr pc, [pc, #-4] */
    base_table_code[8] = (uint32_t)resume_at;

    kuKernelCpuUnrestrictedMemcpy((void *)cave, base_table_code, sizeof(base_table_code));
    kuKernelFlushCaches((void *)cave, sizeof(base_table_code));
    hook_arm(patch_at, cave);

    sceClibPrintf("[PATCH] CScriptInterpreter::GetExportFunction null base-table guard at %p -> %p\n",
                  (void *)patch_at, (void *)cave);

    patch_at = get_export + 0x7c;  /* ldrb r0, [r0, r1] */
    cave = alloc_patch_cave(sizeof(lookup_code));
    if (!cave) {
        return;
    }

    lookup_code[0] = 0xe3500000;       /* cmp r0, #0 */
    lookup_code[1] = 0x0a000009;       /* beq null_return */
    lookup_code[2] = 0xe7d00001;       /* ldrb r0, [r0, r1] */
    lookup_code[3] = 0xe593c00c;       /* ldr ip, [r3, #0xc] */
    lookup_code[4] = 0xe35c0000;       /* cmp ip, #0 */
    lookup_code[5] = 0x0a000005;       /* beq null_return */
    lookup_code[6] = 0xe1500002;       /* cmp r0, r2 */
    lookup_code[7] = 0x23a00000;       /* movhs r0, #0 */
    lookup_code[8] = 0x31a00180;       /* movlo r0, r0, lsl #3 */
    lookup_code[9] = 0xe08c0000;       /* add r0, ip, r0 */
    lookup_code[10] = 0xe8bd00f0;      /* pop {r4, r5, r6, r7} */
    lookup_code[11] = 0xe12fff1e;      /* bx lr */
    lookup_code[12] = 0xe3a00000;      /* null_return: mov r0, #0 */
    lookup_code[13] = 0xe8bd00f0;      /* pop {r4, r5, r6, r7} */
    lookup_code[14] = 0xe12fff1e;      /* bx lr */

    kuKernelCpuUnrestrictedMemcpy((void *)cave, lookup_code, sizeof(lookup_code));
    kuKernelFlushCaches((void *)cave, sizeof(lookup_code));
    hook_arm(patch_at, cave);

    sceClibPrintf("[PATCH] CScriptInterpreter::GetExportFunction null-table guard at %p -> %p\n",
                  (void *)patch_at, (void *)cave);

#define PATCH_EXPORT_CALLER(addr, resume, label) do { \
    cave = alloc_patch_cave(sizeof(caller_code)); \
    if (!cave) { \
        return; \
    } \
    caller_code[0] = 0xe3500000;       /* cmp r0, #0 */ \
    caller_code[1] = 0x0a000003;       /* beq null_return */ \
    caller_code[2] = 0xe1a01004;       /* mov r1, r4 */ \
    caller_code[3] = 0xe8bd4010;       /* pop {r4, lr} */ \
    caller_code[4] = 0xe51ff004;       /* ldr pc, [pc, #-4] */ \
    caller_code[5] = (uint32_t)(resume); \
    caller_code[6] = 0xe3a00000;       /* null_return: mov r0, #0 */ \
    caller_code[7] = 0xe8bd8010;       /* pop {r4, pc} */ \
    kuKernelCpuUnrestrictedMemcpy((void *)cave, caller_code, sizeof(caller_code)); \
    kuKernelFlushCaches((void *)cave, sizeof(caller_code)); \
    hook_arm((addr), cave); \
    sceClibPrintf("[PATCH] %s null-export guard at %p -> %p\n", \
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

static void patch_cngs_online_stubs(void) {
    hook_symbol("CNGS::HandleUpdate",
                "_ZN4CNGS12HandleUpdateEi",
                (uintptr_t)cngs_handle_update_stub);
    hook_symbol("CNGSSession::tick",
                "_ZN11CNGSSession4tickEj",
                (uintptr_t)cngs_session_tick_stub);
    hook_symbol("CNGSSession::isSessionKeyValid",
                "_ZN11CNGSSession17isSessionKeyValidEv",
                (uintptr_t)cngs_session_key_valid_stub);
    hook_symbol("CNGSLocalUser::isAuthenticated",
                "_ZN13CNGSLocalUser15isAuthenticatedEi",
                (uintptr_t)cngs_local_user_authenticated_stub);
    hook_symbol("CNGSLocalUser::LoadCredentials",
                "_ZN13CNGSLocalUser15LoadCredentialsEv",
                (uintptr_t)cngs_local_user_load_credentials_stub);
    hook_symbol("CNGSLocalUser::RegisterUser",
                "_ZN13CNGSLocalUser12RegisterUserEi",
                (uintptr_t)cngs_local_user_register_stub);
    hook_symbol("CNGSLocalUser::ValidateUser",
                "_ZN13CNGSLocalUser12ValidateUserEi",
                (uintptr_t)cngs_local_user_validate_stub);
    hook_symbol("CNGSLocalUser::AssociateUser",
                "_ZN13CNGSLocalUser13AssociateUserEi",
                (uintptr_t)cngs_local_user_associate_stub);
    hook_symbol("CNGSLocalUser::UpdateUserInfo",
                "_ZN13CNGSLocalUser14UpdateUserInfoEi",
                (uintptr_t)cngs_local_user_update_stub);
    hook_symbol("CNGSLocalUser::CredentialsFileExists",
                "_ZN13CNGSLocalUser21CredentialsFileExistsEv",
                (uintptr_t)cngs_local_credentials_exists_stub);
    hook_symbol("CNGSUser::LoadCredentials",
                "_ZN8CNGSUser15LoadCredentialsEPKw",
                (uintptr_t)cngs_user_load_credentials_stub);
    hook_symbol("CNGSUser::CredentialsFileExists",
                "_ZN8CNGSUser21CredentialsFileExistsEPKw",
                (uintptr_t)cngs_user_credentials_exists_stub);
    hook_symbol("CNGSUserCredentials::isValid",
                "_ZNK19CNGSUserCredentials7isValidEv",
                (uintptr_t)cngs_user_credentials_valid_stub);
    hook_symbol("CNGSUserCredentials::readFromFile",
                "_ZN19CNGSUserCredentials12readFromFileERN3com3glu8platform10components9CStrWCharEb",
                (uintptr_t)cngs_user_credentials_read_stub);
    hook_symbol("CNGSUserCredentials::lastPlayerExists",
                "_ZN19CNGSUserCredentials16lastPlayerExistsEv",
                (uintptr_t)cngs_user_credentials_last_exists_stub);
    hook_symbol("CNGSUserCredentials::getLastPlayer",
                "_ZN19CNGSUserCredentials13getLastPlayerERS_",
                (uintptr_t)cngs_user_credentials_get_last_stub);
    hook_symbol("CNGSSessionConfig::readFromFile",
                "_ZN17CNGSSessionConfig12readFromFileERN3com3glu8platform10components9CStrWCharES5_b",
                (uintptr_t)cngs_session_config_read_stub);
    hook_symbol("CNGSAccountManager::HandleUpdate",
                "_ZN18CNGSAccountManager12HandleUpdateEi",
                (uintptr_t)cngs_account_handle_update_stub);
    hook_symbol("CNGSAccountManager::SendMessageToServer",
                "_ZN18CNGSAccountManager19SendMessageToServerEP10CObjectMapPKcMS_FvS1_P25CNGSAccountManagerFunctorEPv",
                (uintptr_t)cngs_account_send_message_stub);
    hook_symbol("CNGSAccountManager::SendMessageObjectToServer",
                "_ZN18CNGSAccountManager25SendMessageObjectToServerEP16CObjectMapObjectPKcMS_FvP10CObjectMapP25CNGSAccountManagerFunctorEPv",
                (uintptr_t)cngs_account_send_object_stub);
    hook_symbol("CContentTracker::LoadFromServer",
                "_ZN15CContentTracker14LoadFromServerEPN3com3glu8platform10components5CHashE",
                (uintptr_t)content_tracker_load_from_server_stub);
    hook_symbol("CContentTracker::SaveToServer",
                "_ZN15CContentTracker12SaveToServerERN3com3glu8platform4core7CVectorIP13CNGSAttributeEE",
                (uintptr_t)content_tracker_save_to_server_stub);
    hook_symbol("CNGSContentManager::GetContent",
                "_ZN18CNGSContentManager10GetContentEP10CObjectMapP25CNGSContentRequestFunctor",
                (uintptr_t)cngs_content_get_stub);
    hook_symbol("CNGSContentManager::GetContentSelf",
                "_ZN18CNGSContentManager14GetContentSelfEPKc",
                (uintptr_t)cngs_content_get_self_stub);
    hook_symbol("CNGSContentManager::GetContentFriend",
                "_ZN18CNGSContentManager16GetContentFriendEiPKc",
                (uintptr_t)cngs_content_get_friend_stub);
    hook_symbol("CNGSContentManager::HandleUpdate",
                "_ZN18CNGSContentManager12HandleUpdateEi",
                (uintptr_t)cngs_content_handle_update_stub);
    hook_symbol("CNGSContentManager::getContentManagerStatus",
                "_ZN18CNGSContentManager23getContentManagerStatusEv",
                (uintptr_t)cngs_content_status_stub);
    hook_symbol("CNGSJSONData::HandleUpdate",
                "_ZN12CNGSJSONData12HandleUpdateEi",
                (uintptr_t)cngs_json_handle_update_stub);
    hook_symbol("CNGSJSONData::LoadFromServer(paths)",
                "_ZN12CNGSJSONData14LoadFromServerERKN3com3glu8platform10components9CStrWCharES6_S6_b",
                (uintptr_t)cngs_json_load_server_stub);
    hook_symbol("CNGSJSONData::LoadFromServer(path)",
                "_ZN12CNGSJSONData14LoadFromServerERKN3com3glu8platform10components9CStrWCharEb",
                (uintptr_t)cngs_json_load_server_stub);
    hook_symbol("CNGSJSONData::LoadFromServer",
                "_ZN12CNGSJSONData14LoadFromServerEb",
                (uintptr_t)cngs_json_load_server_stub);
    hook_symbol("CApplet::IsUserOnWiFi",
                "_ZN7CApplet12IsUserOnWiFiEv",
                (uintptr_t)cngs_applet_wifi_stub);
    hook_symbol("GluPlatformCallbackJNI::IsUserOnWiFi",
                "_ZN22GluPlatformCallbackJNI12IsUserOnWiFiEv",
                (uintptr_t)cngs_callback_wifi_stub);
    hook_symbol("CGunBros::UpdateOnlineStatus",
                "_ZN8CGunBros18UpdateOnlineStatusEv",
                (uintptr_t)gunbros_update_online_status_stub);
    hook_symbol("CMenuGreeting::IsInOfflineMode",
                "_ZNK13CMenuGreeting15IsInOfflineModeEv",
                (uintptr_t)menu_greeting_offline_stub);
}

static void patch_menu_trace_hooks(void) {
    hook_symbol_store("CResourceLoader::LoadNext",
                      "_ZN15CResourceLoader8LoadNextEv",
                      (uintptr_t)trace_resource_load_next,
                      &h_resource_load_next);
    hook_symbol_store("CGunBros::Update",
                      "_ZN8CGunBros6UpdateEi",
                      (uintptr_t)trace_gunbros_update,
                      &h_gunbros_update);
    hook_symbol_store("CGunBros::Bind",
                      "_ZN8CGunBros4BindEv",
                      (uintptr_t)trace_gunbros_bind,
                      &h_gunbros_bind);
    hook_symbol_store("CBrother::Bind/diagnostic",
                      "_ZN8CBrother4BindEP4CMapPKNS_8TemplateEP20CPlayerConfigurationRK15CPlayerProgress",
                      (uintptr_t)trace_brother_bind_trace,
                      &h_brother_bind_trace);
    hook_symbol_store("CPlayer::Bind/diagnostic",
                      "_ZN7CPlayer4BindEP4CMapPKN8CBrother8TemplateEP20CPlayerConfigurationRK15CPlayerProgress",
                      (uintptr_t)trace_player_bind_trace,
                      &h_player_bind_trace);
    hook_symbol_store("CPlayer::Update/diagnostic",
                      "_ZN7CPlayer6UpdateEi",
                      (uintptr_t)trace_player_update_trace,
                      &h_player_update_trace);
    hook_symbol_store("CLevel::OnStart",
                      "_ZN6CLevel7OnStartEv",
                      (uintptr_t)trace_level_on_start,
                      &h_level_on_start);
    hook_symbol_store("CGunBros::LoadMenus",
                      "_ZN8CGunBros9LoadMenusEv",
                      (uintptr_t)trace_gunbros_load_menus,
                      &h_gunbros_load_menus);
    hook_symbol_store("CGunBros::ShowMainMenu",
                      "_ZN8CGunBros12ShowMainMenuE10MenuScreen",
                      (uintptr_t)trace_gunbros_show_main_menu,
                      &h_gunbros_show_main_menu);
    hook_symbol_store("CGunBros::SetMenu",
                      "_ZN8CGunBros7SetMenuE10MenuScreen",
                      (uintptr_t)trace_gunbros_set_menu,
                      &h_gunbros_set_menu);
    hook_symbol_store("CGunBros::FlattenObjectIndex const",
                      "_ZNK8CGunBros18FlattenObjectIndexE14GameObjectTypethRt",
                      (uintptr_t)trace_gunbros_flatten_object_index_const,
                      &h_gunbros_flatten_object_index_const);
    hook_symbol_store("CGunBros::UnFlattenObjectIndex const",
                      "_ZNK8CGunBros20UnFlattenObjectIndexE14GameObjectTypetRtRh",
                      (uintptr_t)trace_gunbros_unflatten_object_index_const,
                      &h_gunbros_unflatten_object_index_const);
    hook_symbol_store("CGunBros::GetObjectCount const",
                      "_ZNK8CGunBros14GetObjectCountE14GameObjectType",
                      (uintptr_t)trace_gunbros_get_object_count_const,
                      &h_gunbros_get_object_count_const);
    hook_symbol_store("CGunBros::ValidateGameObject const",
                      "_ZNK8CGunBros18ValidateGameObjectE14GameObjectTypeth",
                      (uintptr_t)trace_gunbros_validate_game_object_3_const,
                      &h_gunbros_validate_game_object_3_const);
    hook_symbol_store("CGunBros::ValidateGameObject(ref) const",
                      "_ZNK8CGunBros18ValidateGameObjectE14GameObjectTypeRKN11IGameObject13GameObjectRefE",
                      (uintptr_t)trace_gunbros_validate_game_object_ref_const,
                      &h_gunbros_validate_game_object_ref_const);
    hook_symbol_store("CGunBros::GetGameObject(flat)",
                      "_ZN8CGunBros13GetGameObjectE14GameObjectTypet",
                      (uintptr_t)trace_gunbros_get_game_object_flat,
                      &h_gunbros_get_game_object_flat);
    hook_symbol_store("CGunBros::GetGameObject(flat) const",
                      "_ZNK8CGunBros13GetGameObjectE14GameObjectTypet",
                      (uintptr_t)trace_gunbros_get_game_object_flat_const,
                      &h_gunbros_get_game_object_flat_const);
    hook_symbol_store("CGunBros::GetGameObject(pack)",
                      "_ZN8CGunBros13GetGameObjectE14GameObjectTypeth",
                      (uintptr_t)trace_gunbros_get_game_object_pack,
                      &h_gunbros_get_game_object_pack);
    hook_symbol_store("CGunBros::GetGameObject(pack) const",
                      "_ZNK8CGunBros13GetGameObjectE14GameObjectTypeth",
                      (uintptr_t)trace_gunbros_get_game_object_pack_const,
                      &h_gunbros_get_game_object_pack_const);
    hook_symbol_store("CGunBros::LoadGameObjectReq",
                      "_ZN8CGunBros17LoadGameObjectReqE14GameObjectTypeth",
                      (uintptr_t)trace_gunbros_load_game_object_req,
                      &h_gunbros_load_game_object_req);
    hook_symbol_store("CGunBros::FreeGameObjectReq",
                      "_ZN8CGunBros17FreeGameObjectReqE14GameObjectTypeth",
                      (uintptr_t)trace_gunbros_free_game_object_req,
                      &h_gunbros_free_game_object_req);
    hook_symbol_store("CBrother::Spawn (construct)",
                      "_ZN8CBrother5SpawnEPKNS_9SpawnDataE",
                      (uintptr_t)trace_brother_spawn_construct,
                      &h_brother_spawn_construct);
    hook_symbol_store("CWeaponMastery::AddXP",
                      "_ZN14CWeaponMastery5AddXPEP4CGunthhjj",
                      (uintptr_t)trace_weapon_mastery_add_xp,
                      &h_weapon_mastery_add_xp);
    hook_symbol_store("CChallengeManager::UpdateChallengeStatusData",
                      "_ZN17CChallengeManager25UpdateChallengeStatusDataEh",
                      (uintptr_t)trace_challenge_update_status_data,
                      &h_challenge_update_status_data);
    hook_symbol_store("CChallengeManager::UpdateFromLevelSession",
                      "_ZN17CChallengeManager22UpdateFromLevelSessionEPK6CLevelRKN11IGameObject13GameObjectRefEh",
                      (uintptr_t)trace_challenge_update_from_level_session,
                      &h_challenge_update_from_level_session);
    hook_symbol_store("CInputPad::UpdateInput",
                      "_ZN9CInputPad11UpdateInputEi",
                      (uintptr_t)trace_input_pad_update_input,
                      &h_input_pad_update_input);
    hook_symbol_store("CMenuStack::LoadMenu",
                      "_ZN10CMenuStack8LoadMenuEv",
                      (uintptr_t)trace_menu_stack_load_menu,
                      &h_menu_stack_load_menu);
    hook_symbol_store("CMenuStack::Update",
                      "_ZN10CMenuStack6UpdateEi",
                      (uintptr_t)trace_menu_stack_update,
                      &h_menu_stack_update);
    hook_symbol_store("CMenuStack::SetMenu",
                      "_ZN10CMenuStack7SetMenuEPK10MenuConfigthi",
                      (uintptr_t)trace_menu_stack_set_menu,
                      &h_menu_stack_set_menu);
    hook_symbol_store("CMenuStack::PushMenu",
                      "_ZN10CMenuStack8PushMenuEPK10MenuConfigthi",
                      (uintptr_t)trace_menu_stack_push_menu,
                      &h_menu_stack_push_menu);
    hook_symbol_store("CMenuStack::PopMenu",
                      "_ZN10CMenuStack7PopMenuEPK10MenuConfigh",
                      (uintptr_t)trace_menu_stack_pop_menu,
                      &h_menu_stack_pop_menu);
    hook_symbol_store("CMenuStack::IsBusy",
                      "_ZNK10CMenuStack6IsBusyEv",
                      (uintptr_t)trace_menu_stack_is_busy,
                      &h_menu_stack_is_busy);
    hook_symbol_store("CMenuNavigationBar::HideButtons",
                      "_ZN18CMenuNavigationBar11HideButtonsEh",
                      (uintptr_t)trace_menu_navigation_bar_hide_buttons,
                      &h_menu_navigation_bar_hide_buttons);
    hook_symbol_store("CPlayerProgress::GetExperienceForLevel",
                      "_ZNK15CPlayerProgress21GetExperienceForLevelEv",
                      (uintptr_t)trace_player_progress_get_experience_for_level,
                      &h_player_progress_get_experience_for_level);
    hook_symbol_store("CPlayerProgress::GetExperienceDelta",
                      "_ZNK15CPlayerProgress18GetExperienceDeltaEv",
                      (uintptr_t)trace_player_progress_get_experience_delta,
                      &h_player_progress_get_experience_delta);
    hook_symbol_store("CPlayerProgress::GetPercentToNextLevel",
                      "_ZNK15CPlayerProgress21GetPercentToNextLevelEv",
                      (uintptr_t)trace_player_progress_get_percent_to_next_level,
                      &h_player_progress_get_percent_to_next_level);
    hook_symbol_store("CFriendDataManager::GetFriendAvatarProgress",
                      "_ZN18CFriendDataManager23GetFriendAvatarProgressEi",
                      (uintptr_t)trace_friend_data_get_avatar_progress,
                      &h_friend_data_get_avatar_progress);
    hook_symbol_store("CFriendDataManager::GetFriendAvatarConfig",
                      "_ZNK18CFriendDataManager21GetFriendAvatarConfigEi",
                      (uintptr_t)trace_friend_data_get_avatar_config,
                      &h_friend_data_get_avatar_config);
    hook_symbol_store("CGameFlow::GetMission",
                      "_ZNK9CGameFlow10GetMissionEv",
                      (uintptr_t)trace_game_flow_get_mission,
                      &h_game_flow_get_mission);
    hook_symbol_store("CGameFlow::ConfigureBrother",
                      "_ZN9CGameFlow16ConfigureBrotherEPK20CPlayerConfigurationPK15CPlayerProgress",
                      (uintptr_t)trace_game_flow_configure_brother,
                      &h_game_flow_configure_brother);
    hook_symbol_store("CGameObjectPack::GetGameObject",
                      "_ZN15CGameObjectPack13GetGameObjectE14GameObjectTypeh",
                      (uintptr_t)trace_game_object_pack_get_game_object,
                      &h_game_object_pack_get_game_object);
    hook_symbol_store("CGameObjectPack::GetGameObject const",
                      "_ZNK15CGameObjectPack13GetGameObjectE14GameObjectTypeh",
                      (uintptr_t)trace_game_object_pack_get_game_object,
                      &h_game_object_pack_get_game_object_const);
    hook_symbol_store("CGameObjectPack::InitGameObject",
                      "_ZN15CGameObjectPack14InitGameObjectE14GameObjectTypeh",
                      (uintptr_t)trace_game_object_pack_init_game_object,
                      &h_game_object_pack_init_game_object);
    hook_symbol_store("CStoreAggregator::InitFilteredList",
                      "_ZN16CStoreAggregator16InitFilteredListEh",
                      (uintptr_t)trace_store_aggregator_init_filtered_list,
                      &h_store_aggregator_init_filtered_list);
    hook_symbol_store("CStoreAggregator::AcquireItem",
                      "_ZN16CStoreAggregator11AcquireItemEPK10CStoreItemh",
                      (uintptr_t)trace_store_aggregator_acquire_item,
                      &h_store_aggregator_acquire_item);
    hook_symbol_store("CStoreAggregator::GetItemStatus",
                      "_ZN16CStoreAggregator13GetItemStatusEPK10CStoreItemh",
                      (uintptr_t)trace_store_aggregator_get_item_status,
                      &h_store_aggregator_get_item_status);
    hook_symbol_store("CStoreAggregator::IsItemLevelLocked",
                      "_ZNK16CStoreAggregator17IsItemLevelLockedEPK10CStoreItem",
                      (uintptr_t)trace_store_aggregator_is_item_level_locked,
                      &h_store_aggregator_is_item_level_locked);
    hook_symbol_store("CStoreAggregator::CanItemBeAcquired",
                      "_ZNK16CStoreAggregator17CanItemBeAcquiredEPK10CStoreItem",
                      (uintptr_t)trace_store_aggregator_can_item_be_acquired,
                      &h_store_aggregator_can_item_be_acquired);
    hook_symbol_store("CSpritePlayer::Draw(rect)",
                      "_ZN13CSpritePlayer4DrawEPK4Rectssh",
                      (uintptr_t)trace_sprite_player_draw_rect,
                      &h_sprite_player_draw_rect);
    hook_symbol_store("CMenuSystem::IsMenuBusy",
                      "_ZNK11CMenuSystem10IsMenuBusyEv",
                      (uintptr_t)trace_menu_system_is_menu_busy,
                      &h_menu_system_is_menu_busy);
    hook_symbol_store("CMenuSystem::Update",
                      "_ZN11CMenuSystem6UpdateEi",
                      (uintptr_t)trace_menu_system_update,
                      &h_menu_system_update);
    hook_symbol_store("CMenuSplash::Update",
                      "_ZN11CMenuSplash6UpdateEi",
                      (uintptr_t)trace_menu_splash_update,
                      &h_menu_splash_update);
    hook_symbol("CMenuSplash::IsBusy",
                "_ZNK11CMenuSplash6IsBusyEv",
                (uintptr_t)menu_splash_busy_stub);
    hook_symbol_store("CMenuSplash::IsLoaded",
                      "_ZNK11CMenuSplash8IsLoadedEv",
                      (uintptr_t)trace_menu_splash_is_loaded,
                      &h_menu_splash_is_loaded);
    hook_symbol_store("CMenuSplash::Load",
                      "_ZN11CMenuSplash4LoadEP15CResourceLoader",
                      (uintptr_t)trace_menu_splash_load,
                      &h_menu_splash_load);
    hook_symbol_store("CMenuGreeting::Update",
                      "_ZN13CMenuGreeting6UpdateEi",
                      (uintptr_t)trace_menu_greeting_update,
                      &h_menu_greeting_update);
    hook_symbol_store("CMenuGreeting::IsBusy",
                      "_ZNK13CMenuGreeting6IsBusyEv",
                      (uintptr_t)trace_menu_greeting_is_busy,
                      &h_menu_greeting_is_busy);
    hook_symbol_store("CMenuGreeting::IsLoaded",
                      "_ZNK13CMenuGreeting8IsLoadedEv",
                      (uintptr_t)trace_menu_greeting_is_loaded,
                      &h_menu_greeting_is_loaded);
    hook_symbol_store("CMenuGreeting::Load",
                      "_ZN13CMenuGreeting4LoadEP15CResourceLoader",
                      (uintptr_t)trace_menu_greeting_load,
                      &h_menu_greeting_load);
    hook_symbol_store("CMenuSystem::Load",
                      "_ZN11CMenuSystem4LoadEP15CResourceLoader",
                      (uintptr_t)trace_menu_system_load,
                      &h_menu_system_load);
    hook_symbol_store("CMenuSystem::SetMenu",
                      "_ZN11CMenuSystem7SetMenuE10MenuScreent10MenuBranch",
                      (uintptr_t)trace_menu_system_set_menu,
                      &h_menu_system_set_menu);
    hook_symbol_store("CMenuSystem::PushMenu",
                      "_ZN11CMenuSystem8PushMenuE10MenuScreent10MenuBranch",
                      (uintptr_t)trace_menu_system_push_menu,
                      &h_menu_system_push_menu);
    hook_symbol_store("CMenuSystem::PopMenu",
                      "_ZN11CMenuSystem7PopMenuE10MenuBranch",
                      (uintptr_t)trace_menu_system_pop_menu,
                      &h_menu_system_pop_menu);
    hook_symbol_store("CResPackTOC::Init",
                      "_ZN11CResPackTOC4InitEv",
                      (uintptr_t)trace_res_pack_toc_init,
                      &h_res_pack_toc_init);
    hook_symbol_store("CResPackTOC::Bind",
                      "_ZN11CResPackTOC4BindEt",
                      (uintptr_t)trace_res_pack_toc_bind,
                      &h_res_pack_toc_bind);
    hook_symbol_store("CResTOCManager::Init",
                      "_ZN14CResTOCManager4InitEv",
                      (uintptr_t)trace_res_toc_manager_init,
                      &h_res_toc_manager_init);
    hook_symbol_store("CResTOCManager::Bind",
                      "_ZN14CResTOCManager4BindEv",
                      (uintptr_t)trace_res_toc_manager_bind,
                      &h_res_toc_manager_bind);
    hook_symbol_store("CResTOCManager::SetTargetResPackHash",
                      "_ZN14CResTOCManager20SetTargetResPackHashEj",
                      (uintptr_t)trace_res_toc_manager_set_pack_hash,
                      &h_res_toc_manager_set_pack_hash);
    hook_symbol_store("CResTOCManager::SetTargetResPackStr",
                      "_ZN14CResTOCManager19SetTargetResPackStrEPKc",
                      (uintptr_t)trace_res_toc_manager_set_pack_str,
                      &h_res_toc_manager_set_pack_str);
    hook_symbol_store("CResTOCManager::SetTargetResPackInternal",
                      "_ZN14CResTOCManager24SetTargetResPackInternalEt",
                      (uintptr_t)trace_res_toc_manager_set_pack_internal,
                      &h_res_toc_manager_set_pack_internal);
    hook_symbol_store("CImagePool::LoadImage",
                      "_ZN10CImagePool9LoadImageE11ImageFormatithihh",
                      (uintptr_t)trace_image_pool_load_image,
                      &h_image_pool_load_image);
    hook_symbol_store("InstrTexure",
                      "_ZN3com3glu8platform8graphics11InstrTexureEPvPh",
                      (uintptr_t)trace_graphics_instr_texture,
                      &h_graphics_instr_texture);
    hook_symbol_store("CMovie::Load",
                      "_ZN6CMovie4LoadEP15CResourceLoader",
                      (uintptr_t)trace_movie_load,
                      &h_movie_load);
    hook_symbol_store("CMovieSprite::Load",
                      "_ZN12CMovieSprite4LoadEP15CResourceLoader",
                      (uintptr_t)trace_movie_sprite_load,
                      &h_movie_sprite_load);
    hook_symbol_store("CMovieSoundSet::Load",
                      "_ZN14CMovieSoundSet4LoadEP15CResourceLoader",
                      (uintptr_t)trace_movie_sound_set_load,
                      &h_movie_sound_set_load);
    hook_symbol_store("CMovieTiledSprite::Load",
                      "_ZN17CMovieTiledSprite4LoadEP15CResourceLoader",
                      (uintptr_t)trace_movie_tiled_sprite_load,
                      &h_movie_tiled_sprite_load);
}

void so_patch(void) {
    // Sample hook
    //hook_addr((uintptr_t)so_symbol(&so_mod, "_ZN6glitch2os7Printer5printEPKcz"), (uintptr_t)&hookedFunction);
    patch_graphics2d_draw_null_surface();
    patch_brother_bind_missing_progress_table();
    patch_brother_function_resolver_bad_vtable();
    patch_brother_spawn_bad_vtable();
    patch_brother_refresh_sequence_null_state();
    patch_level_add_object_bad_vtable();
    patch_level_onstart_bad_vtable_call();
    patch_render_queue_add_bad_vtable_call();
    patch_render_sort_compare_bad_vtable_call();
    patch_render_queue_draw_bad_vtable_call();
    patch_level_draw_enemy_health_bars_bad_vtable_call();
    patch_level_draw_brother_health_bar_bad_vtable_call();
    patch_input_pad_base_draw_bad_vtable_call();
    patch_eventlog_current_equipment_stubs();
    patch_brother_update_animation_missing_movesets();
    patch_brother_update_normal_missing_current_gun();
    patch_level_on_enemy_killed_missing_mission();
    patch_script_refresh_null_state();
    patch_script_export_missing_tables();
    patch_cngs_online_stubs();
    patch_menu_trace_hooks();
}
