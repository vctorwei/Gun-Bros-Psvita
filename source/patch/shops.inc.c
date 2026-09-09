/* Shop and offline equipment/store compatibility patches. */
/* Included textually by source/patch.c to keep the existing static-hook ABI. */

static int store_aggregator_query_object_is_valid(const void *query_object);
static const void *store_offline_purchases_query_shim(void);
static int store_query_is_real_purchases(const void *query_object);

#define STORE_AGG_OFF_PURCHASES 0x38u
#define STORE_AGG_OFF_CONFIG   0x3cu
#define STORE_AGG_OFF_PROGRESS 0x40u
#define STORE_ITEM_REF_SIZE 12u

enum {
    MENU_STORE_OFF_GUN_SWAP_MOVIE = 0x14cu,
    MENU_STORE_OFF_GUN_SLOT = 0x171u,
    MENU_STORE_SIZE_GUESS = 0x2e2u,
    /* The Android constructor keeps only twenty type-22 store objects.  A
     * single armor filter walks substantially more entries, so the circular
     * cache evicts objects which the same OnShow immediately asks for again.
     * The cache itself is only an array of pointers; the game still owns and
     * frees every object through its original CStoreAggregator lifecycle. */
    /* The first broad scan touched 290 store records, while later category
     * passes caused as many as 621 nested game-object initializations.  The
     * native cache is only a pointer array, so 1024 costs 4 KiB and leaves
     * enough headroom for the complete menu/store working set without a
     * circular-cache eviction during one category construction. */
    VITA_STORE_ITEM_CACHE_CAPACITY = 1024,
    VITA_OFFLINE_PURCHASES_SIZE = 0x1010,
    VITA_OFFLINE_PURCHASES_PAYLOAD_OFFSET = 0x08,
    VITA_OFFLINE_PURCHASES_PAYLOAD_SIZE = 0x1008,
    VITA_OFFLINE_PURCHASES_COUNT_OFFSET = 0x1008,
    VITA_OFFLINE_PURCHASES_CAPACITY = 512,
    VITA_CONSUMABLE_PURCHASE_MAX = 99,
};

#define VITA_OFFLINE_PURCHASES_MAGIC 0x50554247u /* "GBUP" */
#define VITA_OFFLINE_PURCHASES_VERSION 1u

typedef union VitaOfflinePurchasesObject {
    uint64_t alignment;
    unsigned char bytes[VITA_OFFLINE_PURCHASES_SIZE];
} VitaOfflinePurchasesObject;

typedef struct VitaOfflinePurchasesDisk {
    uint32_t magic;
    uint32_t version;
    uint32_t payload_size;
    uint32_t checksum;
    unsigned char payload[VITA_OFFLINE_PURCHASES_PAYLOAD_SIZE];
} VitaOfflinePurchasesDisk;

static VitaOfflinePurchasesObject g_store_offline_purchases;
static uint32_t g_store_offline_purchases_last_checksum;
static int g_store_offline_purchases_initialized;
static int g_store_offline_purchases_has_state;
static void *g_store_offline_purchases_live_owner;

static unsigned int g_store_cache_filter_build_depth;

typedef struct VitaStoreFilteredListCache {
    void *store;
    const void *query_object;
    const void *config;
    const void *progress;
    unsigned int profile_generation;
    uint32_t category_masks[4];
    uint32_t filter_mask;
    uint32_t root_category;
    uint32_t game_type_mask;
    unsigned short count;
    unsigned char type;
    unsigned char valid;
} VitaStoreFilteredListCache;

/* InitFilteredList owns one current eight-byte-ref vector at store +0x20.
 * Cache its exact construction key so Bind followed by OnShow, or reopening
 * the same unchanged shop category, can reuse that resident vector instead
 * of scanning, initializing and sorting the complete type-22 catalogue. */
static VitaStoreFilteredListCache g_store_filtered_list_cache;
static unsigned int g_store_list_revision;

static void store_filtered_list_invalidate(void *self) {
    ++g_store_list_revision;
    if (!self || g_store_filtered_list_cache.store == self) {
        g_store_filtered_list_cache.valid = 0u;
    }
}

static int store_filtered_list_matches(void *self, unsigned int type,
                                       const void *query_object,
                                       const void *config,
                                       const void *progress) {
    const unsigned char *store = (const unsigned char *)self;
    const VitaStoreFilteredListCache *cache =
        &g_store_filtered_list_cache;
    const void *list;
    const void *items;
    unsigned int capacity;

    if (!cache->valid || cache->store != self ||
        cache->type != (unsigned char)type ||
        cache->query_object != query_object || cache->config != config ||
        cache->progress != progress ||
        cache->profile_generation != vita_offline_profile_generation() ||
        !trace_is_game_range(self, 0x44u)) {
        return 0;
    }
    if (memcmp(cache->category_masks, store + 0x04u,
               sizeof(cache->category_masks)) != 0 ||
        cache->filter_mask !=
            *(const uint32_t *)(const void *)(store + 0x2cu) ||
        cache->root_category !=
            *(const uint32_t *)(const void *)(store + 0x30u) ||
        cache->game_type_mask !=
            *(const uint32_t *)(const void *)(store + 0x34u) ||
        cache->count !=
            *(const unsigned short *)(const void *)(store + 0x28u)) {
        return 0;
    }

    list = *(const void * const *)(const void *)(store + 0x20u);
    if (cache->count != 0u &&
        !trace_is_game_range(list, (size_t)cache->count * 8u)) {
        return 0;
    }
    capacity = *(const uint32_t *)(const void *)(store + 0x18u);
    items = *(const void * const *)(const void *)(store + 0x14u);
    return capacity >= VITA_STORE_ITEM_CACHE_CAPACITY &&
           capacity <= 0x10000u &&
           trace_is_game_range(items, (size_t)capacity * sizeof(void *));
}

static void store_filtered_list_remember(void *self, unsigned int type,
                                         const void *query_object,
                                         const void *config,
                                         const void *progress) {
    const unsigned char *store = (const unsigned char *)self;
    VitaStoreFilteredListCache *cache = &g_store_filtered_list_cache;
    const void *list;
    unsigned short count;

    ++g_store_list_revision;
    cache->valid = 0u;
    if (!trace_is_game_range(self, 0x44u)) {
        return;
    }
    count = *(const unsigned short *)(const void *)(store + 0x28u);
    list = *(const void * const *)(const void *)(store + 0x20u);
    if (count != 0u && !trace_is_game_range(list, (size_t)count * 8u)) {
        return;
    }
    cache->store = self;
    cache->query_object = query_object;
    cache->config = config;
    cache->progress = progress;
    cache->profile_generation = vita_offline_profile_generation();
    memcpy(cache->category_masks, store + 0x04u,
           sizeof(cache->category_masks));
    cache->filter_mask =
        *(const uint32_t *)(const void *)(store + 0x2cu);
    cache->root_category =
        *(const uint32_t *)(const void *)(store + 0x30u);
    cache->game_type_mask =
        *(const uint32_t *)(const void *)(store + 0x34u);
    cache->count = count;
    cache->type = (unsigned char)type;
    cache->valid = 1u;
}

static void trace_store_aggregator_clear_cached_content(void *self) {
    typedef void (*clear_cached_content_fn_t)(void *);
    typedef void (*clear_status_cache_fn_t)(void *);
    static clear_status_cache_fn_t clear_status_cache;
    static int looked_up;
    const unsigned char *store = (const unsigned char *)self;
    void * const *items;
    unsigned int capacity;
    unsigned int retained = 0u;
    unsigned int i;

    if (g_store_cache_filter_build_depth != 0u &&
        trace_is_game_range(self, 0x20u)) {
        capacity = *(const unsigned int *)(const void *)(store + 0x18u);
        items = *(void * const * const *)(const void *)(store + 0x14u);
        if (capacity >= VITA_STORE_ITEM_CACHE_CAPACITY &&
            capacity <= 0x10000u &&
            trace_is_game_range(items, (size_t)capacity * sizeof(*items))) {
            if (!looked_up) {
                clear_status_cache = (clear_status_cache_fn_t)(uintptr_t)
                    so_symbol(&so_mod,
                              "_ZN16CStoreAggregator16ClearStatusCacheEv");
                looked_up = 1;
            }
            if (clear_status_cache) {
                for (i = 0; i < capacity; ++i) {
                    retained += items[i] != NULL;
                }
                /* InitFilteredList immediately rebuilds status/sort/filter
                 * state, but native ClearCachedContent also zeros the object
                 * pointer ring and cursor. Preserve that ring only for this
                 * nested call so category changes form a resident union.
                 * Menu cleanup and both destructors still call the unmodified
                 * clear path outside this guarded build window. */
                clear_status_cache(self);
                GUNBROS_PERF_LOG("[PERF-STORE-CACHE] retained filter-union store=%p capacity=%u objects=%u cursor=%u\n",
                                 self, capacity, retained,
                                 (unsigned int)*(const unsigned short *)
                                     (const void *)(store + 0x1cu));
                return;
            }
        }
    }

    store_filtered_list_invalidate(self);
    if (g_original_store_aggregator_clear_cached_content) {
        ((clear_cached_content_fn_t)
            g_original_store_aggregator_clear_cached_content)(self);
    } else {
        (void)SO_CONTINUE(int, h_store_aggregator_clear_cached_content, self);
    }
}

static void store_aggregator_expand_item_cache(void *self,
                                               const char *source) {
    typedef void (*set_item_cache_size_fn_t)(void *, unsigned short,
                                              unsigned char);
    static set_item_cache_size_fn_t set_item_cache_size;
    static int looked_up;
    unsigned char *store = (unsigned char *)self;
    static void *last_ready_store;
    uint32_t old_capacity;
#ifdef GUNBROS_ENABLE_PERF_TRACE
    uint64_t begin_us;
    uint64_t end_us;
#endif

    if (!trace_is_game_range(self, 0x2cu)) {
        return;
    }
    old_capacity = *(const uint32_t *)(const void *)(store + 0x18u);
    if (old_capacity >= VITA_STORE_ITEM_CACHE_CAPACITY) {
        if (last_ready_store != self) {
            GUNBROS_PERF_LOG("[PERF-STORE-CACHE] ready source=%s store=%p capacity=%u\n",
                             source ? source : "?", self, old_capacity);
            last_ready_store = self;
        }
        return;
    }
    if (!looked_up) {
        set_item_cache_size = (set_item_cache_size_fn_t)(uintptr_t)
            so_symbol(&so_mod,
                      "_ZN16CStoreAggregator16SetItemCacheSizeEth");
        looked_up = 1;
    }
    if (!set_item_cache_size) {
        GUNBROS_PERF_LOG("[PERF-STORE-CACHE] unavailable source=%s store=%p old=%u requested=%u\n",
                         source ? source : "?", self, old_capacity,
                         (unsigned int)VITA_STORE_ITEM_CACHE_CAPACITY);
        return;
    }

#ifdef GUNBROS_ENABLE_PERF_TRACE
    begin_us = sceKernelGetProcessTimeWide();
#endif
    store_filtered_list_invalidate(self);
    set_item_cache_size(self,
                        (unsigned short)VITA_STORE_ITEM_CACHE_CAPACITY,
                        (unsigned char)0u);
#ifdef GUNBROS_ENABLE_PERF_TRACE
    end_us = sceKernelGetProcessTimeWide();
    GUNBROS_PERF_LOG("[PERF-STORE-CACHE] expanded source=%s store=%p old=%u new=%u elapsed_us=%llu\n",
                     source ? source : "?", self, old_capacity,
                     *(const uint32_t *)(const void *)(store + 0x18u),
                     (unsigned long long)(end_us >= begin_us ?
                         end_us - begin_us : 0u));
#endif
    last_ready_store = self;
}

static void continue_menu_store_void(so_hook *hook, void *self) {
    void (*original)(void *);

    if (!hook || !hook->addr) {
        return;
    }
    kuKernelCpuUnrestrictedMemcpy((void *)hook->addr,
                                  hook->orig_instr,
                                  sizeof(hook->orig_instr));
    kuKernelFlushCaches((void *)hook->addr, sizeof(hook->orig_instr));
    original = (void (*)(void *))(hook->thumb_addr ? hook->thumb_addr : hook->addr);
    original(self);
    kuKernelCpuUnrestrictedMemcpy((void *)hook->addr,
                                  hook->patch_instr,
                                  sizeof(hook->patch_instr));
    kuKernelFlushCaches((void *)hook->addr, sizeof(hook->patch_instr));
}

static void menu_store_set_slot_visual(void *self, unsigned int selected_slot) {
    typedef void (*movie_set_reverse_fn_t)(void *, unsigned char);
    typedef void (*movie_clear_chapter_fn_t)(void *);
    typedef void (*movie_set_loop_chapter_fn_t)(void *, int);
    unsigned char *menu = (unsigned char *)self;
    unsigned int old_slot;
    void *movie;

    if (!trace_is_game_range(self, MENU_STORE_SIZE_GUESS)) {
        return;
    }
    selected_slot &= 1u;
    old_slot = menu[MENU_STORE_OFF_GUN_SLOT] & 1u;
    if (old_slot == selected_slot) {
        return;
    }

    movie = *(void **)(void *)(menu + MENU_STORE_OFF_GUN_SWAP_MOVIE);
    if (trace_is_game_ptr(movie)) {
        movie_set_reverse_fn_t set_reverse = (movie_set_reverse_fn_t)(uintptr_t)
            so_symbol(&so_mod, "_ZN6CMovie10SetReverseEh");
        movie_clear_chapter_fn_t clear_chapter = (movie_clear_chapter_fn_t)(uintptr_t)
            so_symbol(&so_mod, "_ZN6CMovie20ClearChapterPlaybackEv");
        movie_set_loop_chapter_fn_t set_loop = (movie_set_loop_chapter_fn_t)(uintptr_t)
            so_symbol(&so_mod, "_ZN6CMovie14SetLoopChapterEi");
        if (set_reverse && clear_chapter && set_loop) {
            set_reverse(movie, (unsigned char)old_slot);
            clear_chapter(movie);
            set_loop(movie, 1);
        }
    }
    menu[MENU_STORE_OFF_GUN_SLOT] = (unsigned char)selected_slot;
}

static void trace_menu_store_on_show(void *self) {
    void *config = NULL;
    void *progress = NULL;
#ifdef GUNBROS_ENABLE_PERF_TRACE
    uint64_t begin_us = sceKernelGetProcessTimeWide();
    uint64_t end_us;

    gunbros_perf_set_context(GUNBROS_PERF_SCENE_SHOP, -1, -1,
                             "CMenuStore::OnShow");
#endif
    vita_offline_profile_get_best_local_pair(&config, &progress);
    (void)progress;
    if (trace_is_player_config_for_brother(config)) {
        unsigned char *cfg = (unsigned char *)config;
        /* Native OnShow immediately builds preview guns from this pair. Repair
         * the end-of-match starter fallback before the preview or its cached
         * recommendation list can observe it. */
        (void)vita_offline_loadout_restore_config(
            config, "CMenuStore::OnShow/pre");
        (void)player_config_repair_armor_slots(config,
                                               "CMenuStore::OnShow");
        (void)player_config_repair_loadout_defaults(config,
                                                    "CMenuStore::OnShow");
        menu_store_set_slot_visual(self, cfg[0x4cu]);
    }
    if (g_original_menu_store_on_show) {
        ((void (*)(void *))g_original_menu_store_on_show)(self);
    } else {
        continue_menu_store_void(&h_menu_store_on_show, self);
    }
    if (trace_is_player_config_for_brother(config)) {
        unsigned char *cfg = (unsigned char *)config;
        (void)vita_offline_loadout_restore_config(
            config, "CMenuStore::OnShow/post");
        menu_store_set_slot_visual(self, cfg[0x4cu]);
    }
#ifdef GUNBROS_ENABLE_PERF_TRACE
    end_us = sceKernelGetProcessTimeWide();
    if (end_us >= begin_us && end_us - begin_us >= 100000u) {
        GUNBROS_PERF_LOG("[PERF-STORE-LOAD] stage=OnShow elapsed_us=%llu menu=%p\n",
                         (unsigned long long)(end_us - begin_us), self);
    }
#endif
}

static void trace_menu_store_handle_touch_input(void *self) {
    typedef void (*menu_store_touch_fn_t)(void *);
    unsigned int before_slot;
    unsigned int after_slot;
    void *config = NULL;
    void *progress = NULL;

    GUNBROS_PERF_COUNT(store_touch_calls);

    if (!trace_is_game_range(self, MENU_STORE_SIZE_GUESS)) {
        if (g_original_menu_store_handle_touch_input) {
            ((menu_store_touch_fn_t)g_original_menu_store_handle_touch_input)(self);
        } else {
            continue_menu_store_void(&h_menu_store_handle_touch_input, self);
        }
        return;
    }
    before_slot = ((unsigned char *)self)[MENU_STORE_OFF_GUN_SLOT] & 1u;
    if (g_original_menu_store_handle_touch_input) {
        ((menu_store_touch_fn_t)g_original_menu_store_handle_touch_input)(self);
    } else {
        continue_menu_store_void(&h_menu_store_handle_touch_input, self);
    }
    after_slot = ((unsigned char *)self)[MENU_STORE_OFF_GUN_SLOT] & 1u;
    if (after_slot == before_slot) {
        return;
    }
    store_filtered_list_invalidate(NULL);

    vita_offline_profile_get_best_local_pair(&config, &progress);
    (void)progress;
    if (trace_is_player_config_for_brother(config)) {
        unsigned char *cfg = (unsigned char *)config;
        (void)player_config_repair_armor_slots(
            config, "CMenuStore::HandleTouchInput");
        (void)player_config_repair_loadout_defaults(config,
                                                    "CMenuStore::HandleTouchInput");
        cfg[0x4cu] = (unsigned char)after_slot;
        vita_offline_owned_capture_equipped(config);
        (void)vita_offline_loadout_save(
            config, "CMenuStore::HandleTouchInput/slot");
        vita_offline_profile_request_save();
    }
}

static int store_aggregator_repair_profile_state(void *self,
                                                 const char *source,
                                                 const void **out_query_object,
                                                 const void **out_config,
                                                 const void **out_progress) {
    typedef struct StoreProfilePointerCache {
        void *store;
        const void *query_object;
        const void *config;
        const void *progress;
        unsigned int profile_generation;
        int valid;
    } StoreProfilePointerCache;
    static StoreProfilePointerCache pointer_cache;
    unsigned char *base = (unsigned char *)self;
    const void *query_object;
    const void *config;
    const void *progress;
    const void *orig_query_object;
    const void *orig_config;
    const void *orig_progress;
    void *native_config = NULL;
    void *native_progress = NULL;
    void *local_config = NULL;
    void *local_progress = NULL;

    GUNBROS_PERF_COUNT(store_query_calls);

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

    query_object = *(const void * const *)(const void *)(base + STORE_AGG_OFF_PURCHASES);
    config = *(const void * const *)(const void *)(base + STORE_AGG_OFF_CONFIG);
    progress = *(const void * const *)(const void *)(base + STORE_AGG_OFF_PROGRESS);
    orig_query_object = query_object;
    orig_config = config;
    orig_progress = progress;

    /* Store ownership/price calls read config/progress contents live; only
     * their owner pointers need repair. Keep that verified tuple across
     * frames and invalidate it only when profile adoption changes owner
     * generation or the aggregator itself replaces one of the pointers. */
    if (pointer_cache.valid && pointer_cache.store == self &&
        pointer_cache.profile_generation ==
            vita_offline_profile_generation() &&
        pointer_cache.query_object == query_object &&
        pointer_cache.config == config && pointer_cache.progress == progress &&
        store_aggregator_query_object_is_valid(query_object) &&
        trace_is_player_config_for_brother(config) &&
        trace_is_player_progress_for_brother(progress)) {
        GUNBROS_PERF_COUNT(store_query_cache_hits);
        if (out_query_object) {
            *out_query_object = query_object;
        }
        if (out_config) {
            *out_config = config;
        }
        if (out_progress) {
            *out_progress = progress;
        }
        return 1;
    }

    if (!store_aggregator_query_object_is_valid(query_object)) {
        const void *shim = store_offline_purchases_query_shim();
        if (shim && store_aggregator_query_object_is_valid(shim)) {
            query_object = shim;
            *(const void **)(void *)(base + STORE_AGG_OFF_PURCHASES) = query_object;
        }
    }

    (void)vita_offline_profile_discover_native_owner(
        source ? source : "store-repair");
    if (vita_offline_profile_get_native_pair(&native_config,
                                             &native_progress)) {
        /* Returning from gameplay can expose the same native owner address
         * with newer data in the outgoing gameplay copy.  Adopt first so the
         * shop never reads the owner's previous wallet/equipment generation. */
        vita_offline_profile_adopt_active(native_config, native_progress,
                                          source ? source : "store-repair");
        config = native_config;
        progress = native_progress;
        *(const void **)(void *)(base + STORE_AGG_OFF_CONFIG) = config;
        *(const void **)(void *)(base + STORE_AGG_OFF_PROGRESS) = progress;
    } else {
        vita_offline_profile_get_best_local_pair(&local_config,
                                                 &local_progress);
        if (!trace_is_player_config_for_brother(config)) {
            config = local_config;
            *(const void **)(void *)(base + STORE_AGG_OFF_CONFIG) = config;
        }
        if (!trace_is_player_progress_for_brother(progress)) {
            progress = local_progress;
            *(const void **)(void *)(base + STORE_AGG_OFF_PROGRESS) = progress;
        }
    }
    (void)player_config_repair_loadout_defaults((void *)config, source);
    (void)player_progress_ensure_native_tables((void *)progress, source);

    if ((query_object != orig_query_object || config != orig_config || progress != orig_progress) &&
        trace_allow_ex("CStoreAggregator/profile-repair", 16, 180)) {
        sceClibPrintf("[FIX-STORE] %s repaired profile state store=%p query=%p->%p config=%p->%p progress=%p->%p\n",
                      source ? source : "store",
                      self, orig_query_object, query_object, orig_config, config, orig_progress, progress);
        trace_dump_player_config_summary("store-profile", config);
    }

    if (out_query_object) {
        *out_query_object = query_object;
    }
    if (out_config) {
        *out_config = config;
    }
    if (out_progress) {
        *out_progress = progress;
    }
    pointer_cache.store = self;
    pointer_cache.query_object = query_object;
    pointer_cache.config = config;
    pointer_cache.progress = progress;
    pointer_cache.profile_generation =
        vita_offline_profile_generation();
    pointer_cache.valid =
        store_aggregator_query_object_is_valid(query_object) &&
        trace_is_player_config_for_brother(config) &&
        trace_is_player_progress_for_brother(progress);

    return pointer_cache.valid;
}
static int store_aggregator_has_state_for_item_queries(void *self,
                                                       const char *source,
                                                       const void **out_query_object,
                                                       const void **out_config,
                                                       const void **out_progress) {
    const void *query_object = NULL;

    if (!store_aggregator_repair_profile_state(self, source,
                                               &query_object,
                                               out_config,
                                               out_progress)) {
        if (out_query_object) {
            *out_query_object = query_object;
        }
        return 0;
    }

    if (out_query_object) {
        *out_query_object = query_object;
    }

    return 1;
}

static int store_aggregator_query_object_is_valid(const void *query_object) {
    const void *query_vtable;

    if (!trace_is_game_range(query_object, sizeof(void *))) {
        return 0;
    }

    query_vtable = *(const void * const *)query_object;
    return trace_is_game_range(query_vtable, 0x48);
}

static uint32_t store_offline_purchases_disk_checksum(VitaOfflinePurchasesDisk *disk) {
    uint32_t checksum;

    disk->checksum = 0u;
    checksum = vita_profile_checksum(disk, sizeof(*disk));
    disk->checksum = checksum;
    return checksum;
}

static void store_offline_purchases_load(void) {
    static const char path[] = DATA_PATH "vita_purchases_v1.dat";
    VitaOfflinePurchasesDisk disk;
    uint32_t expected;
    uint32_t stored;
    uint32_t count;
    FILE *fp;
    size_t read;

    fp = fopen(path, "rb");
    if (!fp) {
        return;
    }
    read = fread(&disk, 1, sizeof(disk), fp);
    (void)fclose(fp);
    if (read != sizeof(disk) ||
        disk.magic != VITA_OFFLINE_PURCHASES_MAGIC ||
        disk.version != VITA_OFFLINE_PURCHASES_VERSION ||
        disk.payload_size != VITA_OFFLINE_PURCHASES_PAYLOAD_SIZE) {
        return;
    }

    stored = disk.checksum;
    expected = store_offline_purchases_disk_checksum(&disk);
    count = *(const uint32_t *)(const void *)(
        disk.payload + (VITA_OFFLINE_PURCHASES_COUNT_OFFSET -
                        VITA_OFFLINE_PURCHASES_PAYLOAD_OFFSET));
    if (stored != expected || count > VITA_OFFLINE_PURCHASES_CAPACITY) {
        return;
    }

    sceClibMemcpy(g_store_offline_purchases.bytes +
                      VITA_OFFLINE_PURCHASES_PAYLOAD_OFFSET,
                  disk.payload, sizeof(disk.payload));
    g_store_offline_purchases_last_checksum = stored;
    g_store_offline_purchases_has_state = 1;
}

static int store_purchases_object_has_full_state(const void *purchases) {
    const void *vtable;
    uint32_t count;

    if (!store_aggregator_query_object_is_valid(purchases) ||
        !trace_is_game_range(purchases, VITA_OFFLINE_PURCHASES_SIZE)) {
        return 0;
    }
    vtable = *(const void * const *)purchases;
    if (!trace_is_game_range(vtable, 0x4cu)) {
        return 0;
    }
    count = *(const uint32_t *)(const void *)
        ((const unsigned char *)purchases +
         VITA_OFFLINE_PURCHASES_COUNT_OFFSET);
    return count <= VITA_OFFLINE_PURCHASES_CAPACITY;
}

static void store_offline_purchases_capture_live_owner(void) {
    void *shim = (void *)g_store_offline_purchases.bytes;
    void *live = g_store_offline_purchases_live_owner;

    if (g_store_offline_purchases_initialized && live != shim &&
        store_purchases_object_has_full_state(live)) {
        sceClibMemcpy(g_store_offline_purchases.bytes +
                          VITA_OFFLINE_PURCHASES_PAYLOAD_OFFSET,
                      (const unsigned char *)live +
                          VITA_OFFLINE_PURCHASES_PAYLOAD_OFFSET,
                      VITA_OFFLINE_PURCHASES_PAYLOAD_SIZE);
        g_store_offline_purchases_has_state = 1;
    }
}

static int store_offline_purchases_save(int force) {
    static const char final_path[] = DATA_PATH "vita_purchases_v1.dat";
    static const char temp_path[] = DATA_PATH "vita_purchases_v1.tmp";
    VitaOfflinePurchasesDisk disk;
    uint32_t checksum;
    FILE *fp;
    size_t written;

    if (!g_store_offline_purchases_initialized) {
        return 1;
    }

    /* CPowerUpSelector and CBrother may use/remove quantities directly from a
     * native CPurchases object. Mirror that live payload before checksumming
     * so using a grenade is persisted as reliably as collecting one. */
    store_offline_purchases_capture_live_owner();

    memset(&disk, 0, sizeof(disk));
    disk.magic = VITA_OFFLINE_PURCHASES_MAGIC;
    disk.version = VITA_OFFLINE_PURCHASES_VERSION;
    disk.payload_size = VITA_OFFLINE_PURCHASES_PAYLOAD_SIZE;
    sceClibMemcpy(disk.payload,
                  g_store_offline_purchases.bytes +
                      VITA_OFFLINE_PURCHASES_PAYLOAD_OFFSET,
                  sizeof(disk.payload));
    checksum = store_offline_purchases_disk_checksum(&disk);
    if (!force && checksum == g_store_offline_purchases_last_checksum) {
        return 1;
    }

    fp = fopen(temp_path, "wb");
    if (!fp) {
        return 0;
    }
    written = fwrite(&disk, 1, sizeof(disk), fp);
    (void)fflush(fp);
    if (fclose(fp) != 0 || written != sizeof(disk)) {
        (void)remove(temp_path);
        return 0;
    }
    if (rename(temp_path, final_path) != 0) {
        (void)remove(final_path);
        if (rename(temp_path, final_path) != 0) {
            (void)remove(temp_path);
            return 0;
        }
    }

    g_store_offline_purchases_last_checksum = checksum;
    g_store_offline_purchases_has_state = 1;
    return 1;
}

static void store_offline_purchases_publish_to_live_owner(void *shim_object) {
    unsigned char *gunbros = (unsigned char *)g_live_gunbros_self;
    void *current;

    if (!shim_object || !trace_is_game_range(gunbros, 0x18u)) {
        return;
    }
    current = *(void **)(void *)(gunbros + 0x14u);
    if (!store_aggregator_query_object_is_valid(current)) {
        store_offline_purchases_capture_live_owner();
        *(void **)(void *)(gunbros + 0x14u) = shim_object;
        g_store_offline_purchases_live_owner = shim_object;
        return;
    }

    if (current == shim_object) {
        g_store_offline_purchases_live_owner = shim_object;
        return;
    }

    if (store_purchases_object_has_full_state(current) &&
        g_store_offline_purchases_live_owner != current) {
        /* ReInit/level loading can replace the native CPurchases instance.
         * Capture the outgoing instance first, then hydrate the new one from
         * the resident sidecar object before the HUD asks for its counts. */
        store_offline_purchases_capture_live_owner();
        if (g_store_offline_purchases_has_state) {
            sceClibMemcpy((unsigned char *)current +
                              VITA_OFFLINE_PURCHASES_PAYLOAD_OFFSET,
                          g_store_offline_purchases.bytes +
                              VITA_OFFLINE_PURCHASES_PAYLOAD_OFFSET,
                          VITA_OFFLINE_PURCHASES_PAYLOAD_SIZE);
        } else {
            sceClibMemcpy(g_store_offline_purchases.bytes +
                              VITA_OFFLINE_PURCHASES_PAYLOAD_OFFSET,
                          (const unsigned char *)current +
                              VITA_OFFLINE_PURCHASES_PAYLOAD_OFFSET,
                          VITA_OFFLINE_PURCHASES_PAYLOAD_SIZE);
            g_store_offline_purchases_has_state = 1;
        }
        g_store_offline_purchases_live_owner = current;
    }
}

static const void *store_offline_purchases_query_shim(void) {
    uintptr_t native_vtable;
    void *shim_object = (void *)g_store_offline_purchases.bytes;

    if (!g_store_offline_purchases_initialized) {
        native_vtable = so_symbol(&so_mod, "_ZTV10CPurchases");
        if (!native_vtable ||
            !trace_is_game_range((const void *)native_vtable, 0x58u)) {
            return NULL;
        }

        memset(&g_store_offline_purchases, 0,
               sizeof(g_store_offline_purchases));
        *(uintptr_t *)(void *)g_store_offline_purchases.bytes =
            native_vtable + 2u * sizeof(uintptr_t);
        g_store_offline_purchases_initialized = 1;
        store_offline_purchases_load();
    }

    store_offline_purchases_publish_to_live_owner(shim_object);
    return shim_object;
}

static unsigned int store_purchase_count_on(const void *purchases,
                                            unsigned int type,
                                            unsigned int id,
                                            unsigned int subtype) {
    typedef unsigned int (*calculate_count_fn_t)(const void *, uint16_t,
                                                  uint8_t, uint8_t);
    const uintptr_t *vtable;
    calculate_count_fn_t calculate_count;

    if (!store_purchases_object_has_full_state(purchases) ||
        id > 0xffffu || type > 0xffu || subtype > 0xffu) {
        return 0u;
    }
    vtable = *(const uintptr_t * const *)(const void *)purchases;
    calculate_count = (calculate_count_fn_t)vtable[0x44u / sizeof(uintptr_t)];
    if (!trace_is_game_ptr((const void *)(uintptr_t)calculate_count)) {
        return 0u;
    }
    return calculate_count(purchases, (uint16_t)id, (uint8_t)type,
                           (uint8_t)subtype);
}

static int store_purchase_add_on(void *purchases,
                                 unsigned int type,
                                 unsigned int id,
                                 unsigned int subtype) {
    typedef void (*add_fn_t)(void *, uint16_t, uint8_t, uint8_t);
    const uintptr_t *vtable;
    add_fn_t add;
    unsigned int before;

    if (!store_purchases_object_has_full_state(purchases) ||
        id > 0xffffu || type > 0xffu || subtype > 0xffu) {
        return 0;
    }
    before = store_purchase_count_on(purchases, type, id, subtype);
    if (type == GUNBROS_OBJECT_TYPE_POWERUP &&
        before >= VITA_CONSUMABLE_PURCHASE_MAX) {
        return 0;
    }

    vtable = *(const uintptr_t * const *)(const void *)purchases;
    add = (add_fn_t)vtable[0x48u / sizeof(uintptr_t)];
    if (!trace_is_game_ptr((const void *)(uintptr_t)add)) {
        return 0;
    }
    add(purchases, (uint16_t)id, (uint8_t)type, (uint8_t)subtype);
    if (store_purchase_count_on(purchases, type, id, subtype) > before) {
        if (purchases == (void *)g_store_offline_purchases.bytes) {
            g_store_offline_purchases_has_state = 1;
        }
        return 1;
    }
    return 0;
}

static unsigned int store_offline_purchase_count(unsigned int type,
                                                 unsigned int id,
                                                 unsigned int subtype) {
    return store_purchase_count_on(store_offline_purchases_query_shim(),
                                   type, id, subtype);
}

static int store_query_is_offline_shim(const void *query_object) {
    const void *shim = store_offline_purchases_query_shim();
    return shim && query_object == shim;
}

static int store_query_is_real_purchases(const void *query_object) {
    return store_aggregator_query_object_is_valid(query_object) &&
           !store_query_is_offline_shim(query_object);
}

static int store_item_get_ref_info(const void *item,
                                   unsigned int slot,
                                   unsigned int *out_id,
                                   unsigned int *out_subtype,
                                   unsigned int *out_type) {
    const unsigned char *base = (const unsigned char *)item;
    const unsigned char *refs;
    unsigned int count;
    const unsigned char *ref;

    if (out_id) {
        *out_id = 0;
    }
    if (out_subtype) {
        *out_subtype = 0xffu;
    }
    if (out_type) {
        *out_type = 0xffu;
    }

    if (!trace_is_game_range(item, 0x14u)) {
        return 0;
    }

    count = *(const uint32_t *)(const void *)(base + 0x10);
    refs = *(const unsigned char * const *)(const void *)(base + 0x0c);
    if (slot >= count || count > 64u ||
        !trace_is_game_range(refs, (size_t)count * STORE_ITEM_REF_SIZE)) {
        return 0;
    }

    ref = refs + slot * STORE_ITEM_REF_SIZE;
    if (out_id) {
        *out_id = *(const uint16_t *)(const void *)(ref + 0x04);
    }
    if (out_subtype) {
        *out_subtype = *(const unsigned char *)(const void *)(ref + 0x06);
    }
    if (out_type) {
        *out_type = *(const uint32_t *)(const void *)(ref + 0x08) & 0xffu;
    }
    return 1;
}

static const unsigned char *store_item_get_ref_ptr(const void *item,
                                                   unsigned int slot,
                                                   unsigned int *out_id,
                                                   unsigned int *out_subtype,
                                                   unsigned int *out_type) {
    const unsigned char *base = (const unsigned char *)item;
    const unsigned char *refs;
    unsigned int count;
    const unsigned char *ref;

    if (!trace_is_game_range(item, 0x14u)) {
        return NULL;
    }

    count = *(const uint32_t *)(const void *)(base + 0x10);
    refs = *(const unsigned char * const *)(const void *)(base + 0x0c);
    if (slot >= count || count > 64u ||
        !trace_is_game_range(refs, (size_t)count * STORE_ITEM_REF_SIZE)) {
        return NULL;
    }

    ref = refs + slot * STORE_ITEM_REF_SIZE;
    (void)store_item_get_ref_info(item, slot, out_id, out_subtype, out_type);
    return ref;
}

static const unsigned char *store_item_find_ref_of_type(const void *item,
                                                        unsigned int desired_type,
                                                        unsigned int *out_id,
                                                        unsigned int *out_subtype) {
    const unsigned char *base = (const unsigned char *)item;
    unsigned int count;
    unsigned int i;

    if (!trace_is_game_range(item, 0x14u)) {
        return NULL;
    }

    count = *(const uint32_t *)(const void *)(base + 0x10);
    if (count > 64u) {
        return NULL;
    }

    for (i = 0; i < count; ++i) {
        unsigned int id = 0;
        unsigned int subtype = 0xffu;
        unsigned int type = 0xffu;
        const unsigned char *ref = store_item_get_ref_ptr(item, i, &id, &subtype, &type);

        if (ref && type == desired_type && subtype != 0xffu) {
            if (out_id) {
                *out_id = id;
            }
            if (out_subtype) {
                *out_subtype = subtype;
            }
            return ref;
        }
    }

    return NULL;
}

static int game_object_ref_get_info(const void *ref,
                                    unsigned int *out_id,
                                    unsigned int *out_subtype,
                                    unsigned int *out_type) {
    const unsigned char *r = (const unsigned char *)ref;

    if (out_id) {
        *out_id = 0;
    }
    if (out_subtype) {
        *out_subtype = 0xffu;
    }
    if (out_type) {
        *out_type = 0xffu;
    }

    if (!trace_is_game_range(ref, STORE_ITEM_REF_SIZE)) {
        return 0;
    }

    if (out_id) {
        *out_id = *(const uint16_t *)(const void *)(r + 0x04);
    }
    if (out_subtype) {
        *out_subtype = *(const unsigned char *)(const void *)(r + 0x06);
    }
    if (out_type) {
        *out_type = *(const uint32_t *)(const void *)(r + 0x08) & 0xffu;
    }
    return 1;
}

static int player_config_ref_matches(const void *config,
                                     unsigned int off,
                                     unsigned int id,
                                     unsigned int subtype) {
    const unsigned char *c = (const unsigned char *)config;

    if (!trace_is_player_config_for_brother(config) || subtype == 0xffu) {
        return 0;
    }

    return (unsigned int)(c[off] | (c[off + 1] << 8)) == id &&
           (unsigned int)c[off + 2] == subtype;
}

static int player_config_has_equipped_ref(const void *config,
                                          unsigned int type,
                                          unsigned int id,
                                          unsigned int subtype) {
    if (type == GUNBROS_OBJECT_TYPE_GUN) {
        return player_config_ref_matches(config, 0x10u, id, subtype) ||
               player_config_ref_matches(config, 0x18u, id, subtype);
    }

    if (type == GUNBROS_OBJECT_TYPE_ARMOR) {
        return player_config_ref_matches(config, 0x30u, id, subtype) ||
               player_config_ref_matches(config, 0x38u, id, subtype) ||
               player_config_ref_matches(config, 0x40u, id, subtype) ||
               player_config_ref_matches(config, 0x48u, id, subtype);
    }

    return 0;
}

static int player_config_has_any_store_item_ref(const void *config, const void *item) {
    const unsigned char *base = (const unsigned char *)item;
    unsigned int count;
    unsigned int i;

    if (!trace_is_player_config_for_brother(config) ||
        !trace_is_game_range(item, 0x14u)) {
        return 0;
    }

    count = *(const uint32_t *)(const void *)(base + 0x10);
    if (count > 64u) {
        return 0;
    }

    for (i = 0; i < count; ++i) {
        unsigned int id = 0;
        unsigned int subtype = 0xffu;
        unsigned int type = 0xffu;

        if (store_item_get_ref_ptr(item, i, &id, &subtype, &type) &&
            player_config_has_equipped_ref(config, type, id, subtype)) {
            return 1;
        }
    }

    return 0;
}

static int store_item_get_currency_costs(const void *item,
                                         uint32_t *out_common,
                                         uint32_t *out_rare) {
    const unsigned char *base = (const unsigned char *)item;

    if (out_common) {
        *out_common = 0;
    }
    if (out_rare) {
        *out_rare = 0;
    }

    if (!trace_is_game_range(item, 0x20u)) {
        return 0;
    }

    if (out_common) {
        *out_common = *(const uint32_t *)(const void *)(base + 0x18);
    }
    if (out_rare) {
        *out_rare = *(const uint32_t *)(const void *)(base + 0x1c);
    }
    return 1;
}

static uint64_t store_progress_common_currency(const void *progress) {
    const unsigned char *p = (const unsigned char *)progress;
    uint64_t lo;
    uint64_t hi;

    if (!trace_is_game_range(progress, 0x44u)) {
        return 0;
    }

    lo = *(const uint32_t *)(const void *)(p + 0x38);
    hi = *(const uint32_t *)(const void *)(p + 0x3c);
    return lo | (hi << 32);
}

static uint32_t store_progress_rare_currency(const void *progress) {
    if (!trace_is_game_range(progress, 0x44u)) {
        return 0;
    }
    return *(const uint32_t *)(const void *)((const unsigned char *)progress + 0x40);
}

static void store_progress_set_common_currency(void *progress, uint64_t value) {
    unsigned char *p = (unsigned char *)progress;

    if (!trace_is_game_range(progress, 0x44u)) {
        return;
    }

    *(uint32_t *)(void *)(p + 0x38) = (uint32_t)(value & 0xffffffffu);
    *(uint32_t *)(void *)(p + 0x3c) = (uint32_t)(value >> 32);
}

static void store_progress_set_rare_currency(void *progress, uint32_t value) {
    if (!trace_is_game_range(progress, 0x44u)) {
        return;
    }
    *(uint32_t *)(void *)((unsigned char *)progress + 0x40) = value;
}

static int store_item_is_affordable_offline(const void *item,
                                            const void *progress,
                                            uint32_t *out_common_cost,
                                            uint32_t *out_rare_cost,
                                            uint64_t *out_common_have,
                                            uint32_t *out_rare_have) {
    uint32_t common_cost = 0;
    uint32_t rare_cost = 0;
    uint64_t common_have;
    uint32_t rare_have;

    if (!store_item_get_currency_costs(item, &common_cost, &rare_cost)) {
        return 0;
    }

    common_have = store_progress_common_currency(progress);
    rare_have = store_progress_rare_currency(progress);

    if (out_common_cost) {
        *out_common_cost = common_cost;
    }
    if (out_rare_cost) {
        *out_rare_cost = rare_cost;
    }
    if (out_common_have) {
        *out_common_have = common_have;
    }
    if (out_rare_have) {
        *out_rare_have = rare_have;
    }

    /* Native AcquireItem selects common currency whenever +0x18 is nonzero;
     * only a zero common price falls through to the rare-currency field. */
    return common_cost != 0u ?
               common_have >= (uint64_t)common_cost :
               rare_have >= rare_cost;
}

static int store_item_status_from_marker(const void *item) {
    uint32_t marker;

    if (!trace_is_game_range(item, 0xfcu)) {
        return -1;
    }

    marker = *(const uint32_t *)(const void *)((const unsigned char *)item + 0xf8);
    /* ResetToDefaults writes marker 0 for an ordinary item. OverrideItem maps
     * the three real promotion labels to marker 1/2/3, and native
     * GetItemStatus maps those to UI status 0/1/2. Status 0 is specifically
     * the ON SALE ribbon, not the ordinary purchasable state. */
    if (marker >= 1u && marker <= 3u) {
        return (int)marker - 1;
    }
    return -1;
}

static int store_item_powerups_have_capacity(const void *item,
                                             int *out_has_powerups) {
    uint32_t count;
    uint32_t i;
    int has_powerups = 0;
    int has_capacity = 0;

    if (out_has_powerups) {
        *out_has_powerups = 0;
    }
    if (!trace_is_game_range(item, 0x14u)) {
        return 0;
    }
    count = *(const uint32_t *)(const void *)((const unsigned char *)item + 0x10u);
    if (count == 0u || count > 64u) {
        return 0;
    }

    for (i = 0; i < count; ++i) {
        unsigned int id = 0;
        unsigned int subtype = 0xffu;
        unsigned int type = 0xffu;

        if (!store_item_get_ref_info(item, i, &id, &subtype, &type) ||
            type != GUNBROS_OBJECT_TYPE_POWERUP || subtype == 0xffu) {
            continue;
        }
        has_powerups = 1;
        if (store_offline_purchase_count(type, id, subtype) <
            VITA_CONSUMABLE_PURCHASE_MAX) {
            has_capacity = 1;
        }
    }

    if (out_has_powerups) {
        *out_has_powerups = has_powerups;
    }
    return has_capacity;
}

static int store_item_grant_powerups(const void *item,
                                     const void *preferred_purchases,
                                     int *out_changed) {
    void *targets[3];
    unsigned int target_count = 0u;
    uint32_t count;
    uint32_t i;
    int found = 0;
    int changed = 0;
    void *shim;
    void *live = NULL;

    if (out_changed) {
        *out_changed = 0;
    }
    if (!trace_is_game_range(item, 0x14u)) {
        return 0;
    }

    shim = (void *)store_offline_purchases_query_shim();
    if (trace_is_game_range(g_live_gunbros_self, 0x18u)) {
        live = *(void **)(void *)
            ((unsigned char *)g_live_gunbros_self + 0x14u);
    }
    if (store_purchases_object_has_full_state(preferred_purchases)) {
        targets[target_count++] = (void *)preferred_purchases;
    }
    if (store_purchases_object_has_full_state(live) &&
        (target_count == 0u || targets[0] != live)) {
        targets[target_count++] = live;
    }
    if (store_purchases_object_has_full_state(shim)) {
        unsigned int target;
        int duplicate = 0;
        for (target = 0u; target < target_count; ++target) {
            if (targets[target] == shim) {
                duplicate = 1;
                break;
            }
        }
        if (!duplicate) {
            targets[target_count++] = shim;
        }
    }
    if (target_count == 0u) {
        return 0;
    }

    count = *(const uint32_t *)(const void *)((const unsigned char *)item + 0x10u);
    if (count == 0u || count > 64u) {
        return 0;
    }

    for (i = 0; i < count; ++i) {
        unsigned int id = 0;
        unsigned int subtype = 0xffu;
        unsigned int type = 0xffu;

        if (!store_item_get_ref_info(item, i, &id, &subtype, &type) ||
            type != GUNBROS_OBJECT_TYPE_POWERUP || subtype == 0xffu) {
            continue;
        }
        found = 1;
        {
            unsigned int target;
            for (target = 0u; target < target_count; ++target) {
                changed |= store_purchase_add_on(targets[target], type, id,
                                                 subtype);
            }
        }
    }

    if (out_changed) {
        *out_changed = changed;
    }
    return found;
}

static int store_item_is_owned_offline(const void *item) {
    uint32_t count;
    uint32_t i;

    if (!trace_is_game_range(item, 0x14u)) {
        return 0;
    }
    count = *(const uint32_t *)(const void *)((const unsigned char *)item + 0x10u);
    if (count == 0u || count > 64u) {
        return 0;
    }
    for (i = 0; i < count; ++i) {
        unsigned int id = 0;
        unsigned int subtype = 0xffu;
        unsigned int type = 0xffu;
        if (store_item_get_ref_info(item, i, &id, &subtype, &type) &&
            type != GUNBROS_OBJECT_TYPE_POWERUP &&
            vita_offline_owned_has(type, id, subtype)) {
            return 1;
        }
    }
    return 0;
}

static int store_item_add_owned_offline(const void *item) {
    uint32_t count;
    uint32_t i;
    int changed = 0;

    if (!trace_is_game_range(item, 0x14u)) {
        return 0;
    }
    count = *(const uint32_t *)(const void *)((const unsigned char *)item + 0x10u);
    if (count == 0u || count > 64u) {
        return 0;
    }
    for (i = 0; i < count; ++i) {
        unsigned int id = 0;
        unsigned int subtype = 0xffu;
        unsigned int type = 0xffu;
        if (store_item_get_ref_info(item, i, &id, &subtype, &type) &&
            type != GUNBROS_OBJECT_TYPE_POWERUP) {
            changed |= vita_offline_owned_add(type, id, subtype);
        }
    }
    return changed;
}

static int store_item_can_be_acquired_offline(const void *config,
                                              const void *progress,
                                              const void *item,
                                              uint32_t *out_common_cost,
                                              uint32_t *out_rare_cost,
                                              uint64_t *out_common_have,
                                              uint32_t *out_rare_have,
                                              const char **out_reason) {
    uint32_t common_cost = 0;
    uint32_t rare_cost = 0;
    uint64_t common_have = 0;
    uint32_t rare_have = 0;
    int has_powerups = 0;
    int affordable;

    if (out_reason) {
        *out_reason = "unknown";
    }

    if (player_config_has_any_store_item_ref(config, item)) {
        if (out_reason) {
            *out_reason = "already-equipped";
        }
        return 0;
    }

    if (store_item_is_owned_offline(item)) {
        if (out_reason) {
            *out_reason = "already-owned";
        }
        return 0;
    }

    if (!store_item_powerups_have_capacity(item, &has_powerups) &&
        has_powerups) {
        if (out_reason) {
            *out_reason = "consumable-max";
        }
        return 0;
    }

    affordable = store_item_is_affordable_offline(item, progress,
                                                  &common_cost, &rare_cost,
                                                  &common_have, &rare_have);
    if (out_common_cost) {
        *out_common_cost = common_cost;
    }
    if (out_rare_cost) {
        *out_rare_cost = rare_cost;
    }
    if (out_common_have) {
        *out_common_have = common_have;
    }
    if (out_rare_have) {
        *out_rare_have = rare_have;
    }

    if (out_reason) {
        *out_reason = (common_cost == 0 && rare_cost == 0) ? "free" :
                      (affordable ? "affordable" : "available-insufficient-currency");
    }

    /* Match the native CanItemBeAcquired contract: it checks whether the
     * referenced objects may be added, not whether the wallet can pay.  The
     * actual AcquireItem path performs the currency check and reports a
     * failed purchase.  Keeping affordability out of item status is what
     * allows the menu to create and display its Buy button. */
    return 1;
}

static void store_progress_pay_item_offline(void *progress, const void *item) {
    typedef void (*sub_common_currency_fn_t)(void *progress_data,
                                              uint64_t amount);
    typedef void (*sub_rare_currency_fn_t)(void *progress_data,
                                            uint32_t amount,
                                            const void *source,
                                            const void *detail);
    static sub_common_currency_fn_t sub_common_currency;
    static sub_rare_currency_fn_t sub_rare_currency;
    uint32_t common_cost = 0;
    uint32_t rare_cost = 0;
    uint64_t common_have;
    uint32_t rare_have;
    void *progress_data;

    if (!store_item_get_currency_costs(item, &common_cost, &rare_cost)) {
        return;
    }

    common_have = store_progress_common_currency(progress);
    rare_have = store_progress_rare_currency(progress);
    progress_data = (unsigned char *)progress + 0x28u;

    if (common_cost == 0u && rare_cost == 0u) {
        return;
    }

    if (!sub_common_currency) {
        sub_common_currency = (sub_common_currency_fn_t)(uintptr_t)
            so_symbol(&so_mod,
                      "_ZN15CPlayerProgress12ProgressData17SubCommonCurrencyEy");
    }
    if (!sub_rare_currency) {
        sub_rare_currency = (sub_rare_currency_fn_t)(uintptr_t)
            so_symbol(&so_mod,
                      "_ZN15CPlayerProgress12ProgressData15SubRareCurrencyEjPKwS2_");
    }

    if (common_cost != 0u && common_have >= (uint64_t)common_cost) {
        if (sub_common_currency) {
            sub_common_currency(progress_data, (uint64_t)common_cost);
        } else {
            store_progress_set_common_currency(progress,
                                               common_have - (uint64_t)common_cost);
        }
    } else if (common_cost == 0u && rare_have >= rare_cost) {
        if (sub_rare_currency) {
            sub_rare_currency(progress_data, rare_cost, NULL, NULL);
        } else {
            store_progress_set_rare_currency(progress, rare_have - rare_cost);
        }
    }
}

static void store_aggregator_set_failed_purchase_offline(void *self,
                                                          uint32_t common_cost,
                                                          uint32_t rare_cost,
                                                          const char *reason) {
    typedef void (*set_failed_purchase_fn_t)(void *self,
                                             unsigned int currency,
                                             uint32_t cost);
    static set_failed_purchase_fn_t set_failed_purchase;
    unsigned int currency = common_cost != 0u ? 0u : 1u;
    uint32_t cost = common_cost != 0u ? common_cost : rare_cost;

    if (!trace_is_game_range(self, 0x78u)) {
        return;
    }
    if (!set_failed_purchase) {
        set_failed_purchase = (set_failed_purchase_fn_t)(uintptr_t)
            so_symbol(&so_mod,
                      "_ZN16CStoreAggregator17SetFailedPurchaseE19InGameCurrencyTypesj");
    }

    if (set_failed_purchase) {
        set_failed_purchase(self, currency, cost);
    } else {
        /* The original setter is exactly these two stores. Keep a narrow
         * fallback so GetLastFailPurchaseInfo never receives currency 3 and
         * leaves both popup strings as NULL. */
        *(uint32_t *)(void *)((unsigned char *)self + 0x70u) = cost;
        *(uint32_t *)(void *)((unsigned char *)self + 0x74u) = currency;
    }

    if (trace_allow_ex("CStoreAggregator::SetFailedPurchase/offline", 24, 180)) {
        sceClibPrintf("[FIX-STORE] SetFailedPurchase(this=%p currency=%u cost=%u reason=%s) popup ready\n",
                      self, currency, cost, reason ? reason : "?");
    }
}

static void player_config_install_gun_store_ref(void *config,
                                                const unsigned char *ref,
                                                 unsigned int id,
                                                 unsigned int subtype,
                                                 unsigned int equip_now,
                                                 unsigned int *out_slot) {
    unsigned char *cfg = (unsigned char *)config;
    unsigned int slot;

    if (out_slot) {
        *out_slot = 0xffu;
    }
    if (!trace_is_player_config_for_brother(config) || subtype == 0xffu) {
        return;
    }

    /* Ownership is kept in the offline ledger. A purchase that did not ask
     * to equip must not silently overwrite either equipped slot. */
    if (!equip_now) {
        return;
    }

    (void)player_config_repair_loadout_defaults(config,
                                                "store-install/pre");
    slot = cfg[0x4cu] < 2u ? cfg[0x4cu] : 0u;
    /* CMenuStore's 1/2 selector and gameplay both use config+0x4c. Replace
     * only that selected slot and preserve the other equipped weapon. */
    if (trace_is_game_range(ref, STORE_ITEM_REF_SIZE)) {
        player_config_set_gun_ref_fn_t set_gun_ref = player_config_set_gun_ref_fn();
        if (set_gun_ref) {
            set_gun_ref(config, (unsigned char)slot, ref);
        }
    }
    player_config_set_store_ref(cfg, 0x10u + slot * 8u, ref,
                                (unsigned short)id, (unsigned char)subtype);
    cfg[0x4c] = (unsigned char)slot;
    if (out_slot) {
        *out_slot = slot;
    }
}

static const unsigned int g_player_config_armor_id_offsets[4] = {
    0x30u, 0x38u, 0x40u, 0x48u
};

enum {
    MENU_MESH_PLAYER_OFF_BROTHER = 0x60u,
    MENU_MESH_PLAYER_OFF_SAVED_CONFIG = 0x64u,
    MENU_MESH_PLAYER_OFF_CONTENT_BOUND = 0xe9u,
    MENU_MESH_PLAYER_SIZE_GUESS = 0xecu,
    GUNBROS_MENU_PLAYER_CONFIG_OFF = 0x258u,
    CBROTHER_OBJECT_SIZE = 0xb50u,
    CBROTHER_OFF_PANTS_SURFACE_A = 0x45cu,
    CBROTHER_OFF_PANTS_SURFACE_B = 0x460u,
    CBROTHER_OFF_TORSO_PERCY_SURFACE = 0x504u,
    CBROTHER_OFF_TORSO_FRANCIS_SURFACE = 0x508u,
    CBROTHER_OFF_CIGAR_SURFACE_A = 0x654u,
    CBROTHER_OFF_CIGAR_SURFACE_B = 0x658u,
    CBROTHER_OFF_CHARACTER = 0x784u
};

typedef void (*brother_lifecycle_fn_t)(void *self);

static int player_config_armor_slot_is_empty(const unsigned char *config,
                                             unsigned int slot) {
    return trace_is_player_config_for_brother(config) && slot < 4u &&
           config[g_player_config_armor_id_offsets[slot] + 2u] == 0xffu;
}

static int set_default_surface_if_changed(unsigned char *brother,
                                          unsigned int offset,
                                          void *surface) {
    void **field;

    if (!trace_is_game_range(surface, 0x24u)) {
        return 0;
    }
    field = (void **)(void *)(brother + offset);
    if (*field == surface) {
        return 0;
    }
    *field = surface;
    return 1;
}

static int player_model_apply_empty_slot_textures(void *brother,
                                                   const void *config,
                                                   const char *source) {
    unsigned char *model = (unsigned char *)brother;
    unsigned char *cfg = (unsigned char *)config;
    unsigned int character;
    int changed = 0;
    int pants_empty;
    int torso_empty;
    int cigar_empty;
    void *base_torso;

    if (!trace_is_game_range(brother, CBROTHER_OBJECT_SIZE) ||
        !trace_is_player_config_for_brother(config)) {
        return 0;
    }

    character = cfg[0x4d];
    if (character > 1u) {
        /* Only Percy (0) and Francis (1) have base torso atlases.  Invalid
         * offline/save values must fall back to Percy; never select Francis
         * merely because his surface happened to load last. */
        character = 0u;
    }
    if (model[CBROTHER_OFF_CHARACTER] != (unsigned char)character) {
        model[CBROTHER_OFF_CHARACTER] = (unsigned char)character;
        changed = 1;
    }

    pants_empty = player_config_armor_slot_is_empty(cfg, 0u);
    torso_empty = player_config_armor_slot_is_empty(cfg, 1u);
    cigar_empty = player_config_armor_slot_is_empty(cfg, 3u);
    base_torso = character == 1u ? g_core_default_textures.francis_torso :
                                   g_core_default_textures.percy_torso;

    if (pants_empty) {
        changed |= set_default_surface_if_changed(
            model, CBROTHER_OFF_PANTS_SURFACE_A,
            g_core_default_textures.pants);
        changed |= set_default_surface_if_changed(
            model, CBROTHER_OFF_PANTS_SURFACE_B,
            g_core_default_textures.pants);
    }
    if (torso_empty) {
        /* Both mesh-side torso fields can be sampled while the character mesh
         * changes animation state.  An empty outfit must therefore point both
         * fields at the selected character's base atlas.  For the default
         * Percy configuration this is pack0_core_wvga resource58/logical
         * 0x139; leaving resource66 in the alternate field caused the visible
         * upper-body flicker. */
        changed |= set_default_surface_if_changed(
            model, CBROTHER_OFF_TORSO_PERCY_SURFACE,
            base_torso);
        changed |= set_default_surface_if_changed(
            model, CBROTHER_OFF_TORSO_FRANCIS_SURFACE,
            base_torso);
    }
    if (cigar_empty) {
        changed |= set_default_surface_if_changed(
            model, CBROTHER_OFF_CIGAR_SURFACE_A,
            g_core_default_textures.cigar);
        changed |= set_default_surface_if_changed(
            model, CBROTHER_OFF_CIGAR_SURFACE_B,
            g_core_default_textures.cigar);
    }

    if (changed && trace_allow_ex("player-model/base-textures", 48, 180)) {
        sceClibPrintf("[FIX-TEXTURE] applied base atlases source=%s brother=%p config=%p character=%s(%u) empty(pants=%d torso=%d cigar=%d) surfaces(pants=%p torso=%p percy=%p francis=%p cigar=%p)\n",
                      source ? source : "?", brother, config,
                      character == 1u ? "Francis" : "Percy", character,
                      pants_empty, torso_empty, cigar_empty,
                      g_core_default_textures.pants,
                      base_torso,
                      g_core_default_textures.percy_torso,
                      g_core_default_textures.francis_torso,
                      g_core_default_textures.cigar);
    }

    return changed;
}

static brother_lifecycle_fn_t menu_mesh_player_brother_ctor_fn(void) {
    static brother_lifecycle_fn_t fn;
    static int looked_up;

    if (!looked_up) {
        fn = (brother_lifecycle_fn_t)(uintptr_t)
            so_symbol(&so_mod, "_ZN8CBrotherC1Ev");
        looked_up = 1;
    }
    return fn;
}

static brother_lifecycle_fn_t menu_mesh_player_brother_dtor_fn(void) {
    static brother_lifecycle_fn_t fn;
    static int looked_up;

    if (!looked_up) {
        fn = (brother_lifecycle_fn_t)(uintptr_t)
            so_symbol(&so_mod, "_ZN8CBrotherD1Ev");
        looked_up = 1;
    }
    return fn;
}

static int player_config_armor_refs_equal(const void *lhs, const void *rhs) {
    const unsigned char *a = (const unsigned char *)lhs;
    const unsigned char *b = (const unsigned char *)rhs;
    unsigned int slot;

    if (!trace_is_player_config_for_brother(lhs) ||
        !trace_is_player_config_for_brother(rhs)) {
        return 0;
    }

    for (slot = 0; slot < 4u; ++slot) {
        unsigned int ref_off = g_player_config_armor_id_offsets[slot] - 4u;

        if (memcmp(a + ref_off, b + ref_off, 8u) != 0) {
            return 0;
        }
    }
    return 1;
}

static int menu_mesh_player_reset_stale_armor_preview(void *self) {
    unsigned char *menu_mesh = (unsigned char *)self;
    unsigned char *live_game = (unsigned char *)g_live_gunbros_self;
    void *brother;
    const void *saved_config;
    const void *current_config;
    brother_lifecycle_fn_t dtor;
    brother_lifecycle_fn_t ctor;

    if (!trace_is_game_range(self, MENU_MESH_PLAYER_SIZE_GUESS) ||
        !trace_is_game_range(live_game,
                             GUNBROS_MENU_PLAYER_CONFIG_OFF + 0x82u) ||
        menu_mesh[MENU_MESH_PLAYER_OFF_CONTENT_BOUND] == 0u) {
        return 0;
    }

    brother = *(void **)(void *)(menu_mesh + MENU_MESH_PLAYER_OFF_BROTHER);
    saved_config = menu_mesh + MENU_MESH_PLAYER_OFF_SAVED_CONFIG;
    current_config = live_game + GUNBROS_MENU_PLAYER_CONFIG_OFF;
    if (player_config_armor_refs_equal(saved_config, current_config)) {
        return 0;
    }

    /* CMenuMeshPlayer::ReleaseUnusedContent frees the previous armor game
     * objects and then calls BindPlayer on the same CBrother. CBrother::Bind
     * only clears the four armor-active bytes for empty slots; it does not
     * reconstruct their embedded script/mesh state. That leaves the preview
     * drawing ICRenderSurface pointers owned by the just-freed armor template.
     * Reconstruct only this menu-preview brother before the original bind so
     * base pants/vest/helmet/trinket (including cigar) start from clean state.
     * The level CPlayer and its gameplay state never pass through this hook. */
    if (!trace_is_game_range(brother, CBROTHER_OBJECT_SIZE)) {
        return 0;
    }
    dtor = menu_mesh_player_brother_dtor_fn();
    ctor = menu_mesh_player_brother_ctor_fn();
    if (!dtor || !ctor) {
        if (trace_allow_ex("CMenuMeshPlayer::BindPlayer/lifecycle-missing", 8, 180)) {
            sceClibPrintf("[FIX-TEXTURE] menu preview reset unavailable self=%p brother=%p dtor=%p ctor=%p\n",
                          self, brother, dtor, ctor);
        }
        return 0;
    }

    if (trace_allow_ex("CMenuMeshPlayer::BindPlayer/reset-preview", 32, 120)) {
        sceClibPrintf("[FIX-TEXTURE] resetting stale menu armor preview self=%p brother=%p saved=%p current=%p\n",
                      self, brother, saved_config, current_config);
        trace_dump_player_config_summary("menu-preview/saved", saved_config);
        trace_dump_player_config_summary("menu-preview/current", current_config);
    }

    dtor(brother);
    ctor(brother);
    return 1;
}

static void trace_menu_mesh_player_bind_player(void *self) {
    int rebuilt;

    if (trace_is_game_range(g_live_gunbros_self,
                            GUNBROS_MENU_PLAYER_CONFIG_OFF + 0x82u)) {
        gameplay_request_configured_content(
            (const unsigned char *)g_live_gunbros_self +
                GUNBROS_MENU_PLAYER_CONFIG_OFF,
            "CMenuMeshPlayer::BindPlayer/pre");
    }
    rebuilt = menu_mesh_player_reset_stale_armor_preview(self);

    (void)SO_CONTINUE(int, h_menu_mesh_player_bind_player, self);
    if (trace_is_game_range(self, MENU_MESH_PLAYER_SIZE_GUESS)) {
        void *brother = *(void **)(void *)
            ((unsigned char *)self + MENU_MESH_PLAYER_OFF_BROTHER);
        const void *saved_config =
            (const unsigned char *)self + MENU_MESH_PLAYER_OFF_SAVED_CONFIG;
        (void)player_model_apply_empty_slot_textures(
            brother, saved_config, "CMenuMeshPlayer::BindPlayer");
    }
    if (rebuilt && trace_allow_ex("CMenuMeshPlayer::BindPlayer/reset-complete", 32, 120)) {
        const void *brother = trace_is_game_range(self, MENU_MESH_PLAYER_SIZE_GUESS) ?
            *(const void * const *)(const void *)
                ((const unsigned char *)self + MENU_MESH_PLAYER_OFF_BROTHER) : NULL;
        sceClibPrintf("[FIX-TEXTURE] rebound clean menu armor preview self=%p brother=%p\n",
                      self, brother);
    }
}

static const char *player_config_armor_slot_name(unsigned int slot) {
    static const char * const names[4] = {
        "pants", "vest", "helmet", "trinket/cigar"
    };

    return slot < 4u ? names[slot] : "invalid";
}

static int player_config_resolve_armor_slot(unsigned int id,
                                             unsigned int subtype,
                                             const char *source,
                                             unsigned int *out_slot,
                                             const void **out_template) {
    void *safe_self;
    void *templ;
    unsigned int slot;

    if (out_slot) {
        *out_slot = 0xffu;
    }
    if (out_template) {
        *out_template = NULL;
    }
    if (subtype == 0xffu) {
        return 0;
    }

    safe_self = gunbros_registry_self_or_live(g_live_gunbros_self,
                                              "player-config-armor-slot",
                                              GUNBROS_OBJECT_TYPE_ARMOR,
                                              id);
    if (!safe_self || !h_gunbros_get_game_object_pack_const.addr) {
        return 0;
    }

    templ = trace_gunbros_get_game_object_pack_const(safe_self,
                                                     GUNBROS_OBJECT_TYPE_ARMOR,
                                                     id,
                                                     subtype);
    if (!trace_is_game_range(templ, 5u)) {
        if (trace_allow_ex("player-config-armor-slot/unresolved", 16, 180)) {
            sceClibPrintf("[FIX-STORE] armor template unresolved source=%s ref=%u:%u template=%p\n",
                          source ? source : "?", id, subtype, templ);
        }
        return 0;
    }

    /* CPlayerConfiguration::SetArmor reads CArmor::Template+0x04 and writes
     * exactly one typed config field: 0=pants, 1=vest, 2=helmet, 3=trinket.
     * The trinket field is also the cigar/accessory texture path. */
    slot = *(const unsigned char *)(const void *)((const unsigned char *)templ + 4u);
    if (slot >= 4u) {
        if (trace_allow_ex("player-config-armor-slot/bad-type", 16, 180)) {
            sceClibPrintf("[FIX-STORE] armor template bad type source=%s ref=%u:%u template=%p type=%u\n",
                          source ? source : "?", id, subtype, templ, slot);
        }
        return 0;
    }

    if (out_slot) {
        *out_slot = slot;
    }
    if (out_template) {
        *out_template = templ;
    }
    return 1;
}

static void player_config_clear_armor_slot(unsigned char *cfg, unsigned int slot) {
    unsigned int id_off;

    if (!trace_is_player_config_for_brother(cfg) || slot >= 4u) {
        return;
    }

    id_off = g_player_config_armor_id_offsets[slot];
    sceClibMemset(cfg + id_off - 4u, 0, 8u);
    cfg[id_off + 2u] = 0xffu;
}

static int player_config_repair_armor_slots(void *config, const char *source) {
    unsigned char *cfg = (unsigned char *)config;
    unsigned int slot;
    int repaired = 0;
    int has_armor = 0;

    if (!trace_is_player_config_for_brother(config)) {
        return 0;
    }

    for (slot = 0; slot < 4u; ++slot) {
        unsigned int id_off = g_player_config_armor_id_offsets[slot];
        unsigned int id = (unsigned int)(cfg[id_off] | (cfg[id_off + 1u] << 8));
        unsigned int subtype = cfg[id_off + 2u];
        unsigned int typed_slot = 0xffu;

        if (subtype == 0xffu) {
            continue;
        }
        has_armor = 1;
        if (!player_config_resolve_armor_slot(id, subtype, source,
                                              &typed_slot, NULL) ||
            typed_slot == slot) {
            continue;
        }

        if (player_config_ref_matches(config,
                                      g_player_config_armor_id_offsets[typed_slot],
                                      id, subtype)) {
            /* Legacy builds first wrote the game-selected typed slot, then
             * copied the same ref into the first empty slot. Remove only that
             * exact duplicate; this is the vest-as-pants regression. */
            player_config_clear_armor_slot(cfg, slot);
            repaired++;
            sceClibPrintf("[FIX-STORE] removed duplicate armor source=%s config=%p wrong_slot=%s correct_slot=%s ref=%u:%u\n",
                          source ? source : "?", config,
                          player_config_armor_slot_name(slot),
                          player_config_armor_slot_name(typed_slot), id, subtype);
        } else if (cfg[g_player_config_armor_id_offsets[typed_slot] + 2u] == 0xffu) {
            unsigned char saved_ref[8];

            sceClibMemcpy(saved_ref, cfg + id_off - 4u, sizeof(saved_ref));
            player_config_clear_armor_slot(cfg, slot);
            sceClibMemcpy(cfg + g_player_config_armor_id_offsets[typed_slot] - 4u,
                          saved_ref, sizeof(saved_ref));
            repaired++;
            sceClibPrintf("[FIX-STORE] moved typed armor source=%s config=%p wrong_slot=%s correct_slot=%s ref=%u:%u\n",
                          source ? source : "?", config,
                          player_config_armor_slot_name(slot),
                          player_config_armor_slot_name(typed_slot), id, subtype);
        } else if (trace_allow_ex("player-config-armor-slot/conflict", 16, 180)) {
            sceClibPrintf("[FIX-STORE] kept armor conflict source=%s config=%p wrong_slot=%s expected_slot=%s ref=%u:%u\n",
                          source ? source : "?", config,
                          player_config_armor_slot_name(slot),
                          player_config_armor_slot_name(typed_slot), id, subtype);
        }
    }

    if (has_armor && (repaired ||
        trace_allow_ex("player-config-armor-bindings", 24, 180))) {
        for (slot = 0; slot < 4u; ++slot) {
            unsigned int id_off = g_player_config_armor_id_offsets[slot];
            unsigned int id = (unsigned int)(cfg[id_off] | (cfg[id_off + 1u] << 8));
            unsigned int subtype = cfg[id_off + 2u];
            unsigned int typed_slot = 0xffu;
            const void *templ = NULL;

            if (subtype == 0xffu) {
                continue;
            }
            (void)player_config_resolve_armor_slot(id, subtype, source,
                                                   &typed_slot, &templ);
            sceClibPrintf("[DEBUG-TEXTURE] armor binding source=%s config=%p slot=%s ref=%u:%u template=%p template_slot=%s\n",
                          source ? source : "?", config,
                          player_config_armor_slot_name(slot), id, subtype,
                          templ, player_config_armor_slot_name(typed_slot));
        }
    }

    return repaired;
}

static int player_config_install_armor_store_ref(void *config,
                                                 const unsigned char *ref,
                                                 unsigned int id,
                                                 unsigned int subtype,
                                                 unsigned int *out_slot) {
    unsigned char *cfg = (unsigned char *)config;
    player_config_set_armor_ref_fn_t set_armor_ref;
    unsigned int slot = 0xffu;

    if (out_slot) {
        *out_slot = 0xffu;
    }
    if (!trace_is_player_config_for_brother(config) || subtype == 0xffu ||
        !trace_is_game_range(ref, STORE_ITEM_REF_SIZE) ||
        !player_config_resolve_armor_slot(id, subtype,
                                          "install-armor", &slot, NULL)) {
        return 0;
    }

    set_armor_ref = player_config_set_armor_ref_fn();
    if (!set_armor_ref) {
        return 0;
    }
    set_armor_ref(config, ref);
    (void)player_config_repair_armor_slots(config, "install-armor/post-SetArmor");

    if (!player_config_ref_matches(config,
                                   g_player_config_armor_id_offsets[slot],
                                   id, subtype)) {
        /* This fallback is still type-driven by the real CArmor::Template.
         * It is used only if an already-corrupt config made IsArmorEquipped
         * return early before SetArmor could populate its authoritative slot. */
        player_config_set_store_ref(cfg,
                                    g_player_config_armor_id_offsets[slot],
                                    ref, (unsigned short)id,
                                    (unsigned char)subtype);
        sceClibPrintf("[FIX-STORE] restored selected armor to typed slot config=%p slot=%s ref=%u:%u\n",
                      config, player_config_armor_slot_name(slot), id, subtype);
        (void)player_config_repair_armor_slots(config,
                                               "install-armor/typed-fallback");
    }

    if (out_slot) {
        *out_slot = slot;
    }
    return player_config_ref_matches(config,
                                     g_player_config_armor_id_offsets[slot],
                                     id, subtype);
}

static int trace_store_aggregator_is_item_owned_or_equipped(void *self, const void *ref) {
    const void *query_object = NULL;
    const void *config = NULL;
    const void *progress = NULL;
    unsigned int id;
    unsigned int subtype;
    unsigned int type;

    if (!store_aggregator_repair_profile_state(self,
                                               "IsItemOwnedOrEquipped",
                                               &query_object,
                                               &config,
                                               &progress) ||
        !game_object_ref_get_info(ref, &id, &subtype, &type)) {
        if (trace_allow_ex("CStoreAggregator::IsItemOwnedOrEquipped/bad_state", 24, 180)) {
            sceClibPrintf("[FIX-STORE] IsItemOwnedOrEquipped(this=%p ref=%p) -> -1 bad state query=%p config=%p progress=%p\n",
                          self, ref, query_object, config, progress);
        }
        return -1;
    }

    if (store_query_is_real_purchases(query_object)) {
        typedef int (*owned_fn_t)(void *, const void *);
        if (g_original_store_aggregator_is_item_owned_or_equipped) {
            return ((owned_fn_t)
                    g_original_store_aggregator_is_item_owned_or_equipped)(
                        self, ref);
        }
        return SO_CONTINUE(int,
                           h_store_aggregator_is_item_owned_or_equipped,
                           self, ref);
    }

    if (player_config_has_equipped_ref(config, type, id, subtype)) {
        if (trace_allow_ex("CStoreAggregator::IsItemOwnedOrEquipped/equipped", 24, 180)) {
            sceClibPrintf("[FIX-STORE] IsItemOwnedOrEquipped(this=%p ref=%u:%u:%u) -> 4 equipped offline config=%p\n",
                          self, type, id, subtype, config);
        }
        return 4;
    }

    /* Consumables are quantities, not permanent ownership flags. Old profile
     * ledgers may contain a power-up entry from the previous set-only shim;
     * ignore it so grenades and other in-mission items remain repeatable. */
    if (type != GUNBROS_OBJECT_TYPE_POWERUP &&
        vita_offline_owned_has(type, id, subtype)) {
        if (trace_allow_ex("CStoreAggregator::IsItemOwnedOrEquipped/owned", 24, 180)) {
            sceClibPrintf("[FIX-STORE] IsItemOwnedOrEquipped(this=%p ref=%u:%u:%u) -> 3 owned offline\n",
                          self, type, id, subtype);
        }
        return 3;
    }

    if (trace_allow_ex("CStoreAggregator::IsItemOwnedOrEquipped/no_purchases", 40, 180)) {
        sceClibPrintf("[FIX-STORE] IsItemOwnedOrEquipped(this=%p ref=%u:%u:%u) -> -1 offline no purchases query=%p config=%p progress=%p\n",
                      self, type, id, subtype, query_object, config, progress);
    }
    return -1;
}

static void continue_store_aggregator_configure(void *self,
                                                void *purchases,
                                                void *config,
                                                void *progress) {
    void (*original)(void *, void *, void *, void *);

    kuKernelCpuUnrestrictedMemcpy((void *)h_store_aggregator_configure.addr,
                                  h_store_aggregator_configure.orig_instr,
                                  sizeof(h_store_aggregator_configure.orig_instr));
    kuKernelFlushCaches((void *)h_store_aggregator_configure.addr,
                        sizeof(h_store_aggregator_configure.orig_instr));

    original = (void (*)(void *, void *, void *, void *))
        (h_store_aggregator_configure.thumb_addr
             ? h_store_aggregator_configure.thumb_addr
             : h_store_aggregator_configure.addr);
    original(self, purchases, config, progress);

    kuKernelCpuUnrestrictedMemcpy((void *)h_store_aggregator_configure.addr,
                                  h_store_aggregator_configure.patch_instr,
                                  sizeof(h_store_aggregator_configure.patch_instr));
    kuKernelFlushCaches((void *)h_store_aggregator_configure.addr,
                        sizeof(h_store_aggregator_configure.patch_instr));
}

static void trace_store_aggregator_configure(void *self, void *purchases, void *config, void *progress) {
    void *safe_purchases = purchases;
    void *safe_config = config;
    void *safe_progress = progress;
    void *native_config = NULL;
    void *native_progress = NULL;
    void *local_config = NULL;
    void *local_progress = NULL;

    if (!store_aggregator_query_object_is_valid(safe_purchases)) {
        safe_purchases = (void *)store_offline_purchases_query_shim();
    }
    (void)vita_offline_profile_register_native_owner(
        safe_config, safe_progress, "CStoreAggregator::Configure/input");
    (void)vita_offline_profile_discover_native_owner(
        "CStoreAggregator::Configure/live");
    if (vita_offline_profile_get_native_pair(&native_config,
                                             &native_progress)) {
        safe_config = native_config;
        safe_progress = native_progress;
    } else {
        vita_offline_profile_get_best_local_pair(&local_config,
                                                 &local_progress);
        if (!trace_is_player_config_for_brother(safe_config)) {
            safe_config = local_config;
        }
        if (!trace_is_player_progress_for_brother(safe_progress)) {
            safe_progress = local_progress;
        }
    }

    /* The native HUD owner is authoritative for store currency and gear.
     * Make the active sidecar generation converge before any cost query. */
    vita_offline_profile_adopt_active(safe_config, safe_progress,
                                      "CStoreAggregator::Configure");
    (void)vita_offline_loadout_restore_config(
        safe_config, "CStoreAggregator::Configure/loadout");

    /* Repair saves produced by the old first-empty-slot compatibility code
     * before either the store preview or the live player consumes them. */
    (void)player_config_repair_armor_slots(safe_config,
                                           "CStoreAggregator::Configure");
    (void)player_config_repair_loadout_defaults(safe_config,
                                                "CStoreAggregator::Configure");

    if ((safe_purchases != purchases || safe_config != config || safe_progress != progress) &&
        trace_allow_ex("CStoreAggregator::Configure/profile-repair", 16, 180)) {
        sceClibPrintf("[FIX-STORE] Configure(this=%p) repaired purchases=%p->%p config=%p->%p progress=%p->%p\n",
                      self, purchases, safe_purchases, config, safe_config, progress, safe_progress);
        trace_dump_player_config_summary("store-configure", safe_config);
    }

    store_filtered_list_invalidate(self);
    continue_store_aggregator_configure(self, safe_purchases,
                                        safe_config, safe_progress);

    /* Configure is called before the first filtered list is built.  Resize
     * here while the constructor's twenty-entry cache is still empty, so the
     * initial shop scan does not thrash it and later category changes reuse
     * already initialized store metadata. */
    store_aggregator_expand_item_cache(self,
                                       "CStoreAggregator::Configure");

    if (trace_is_game_range(self, 0x44)) {
        store_aggregator_repair_profile_state(self, "Configure/post", NULL, NULL, NULL);
    }

}

static int trace_store_aggregator_is_item_level_locked(void *self, const void *item) {
    typedef int (*level_locked_fn_t)(void *, const void *);
    const void *query_object;
    const void *config;
    const void *progress;

    GUNBROS_PERF_COUNT(store_level_lock_calls);
    if (!trace_is_game_range(item, 0x16)) {
        if (trace_allow("CStoreAggregator::IsItemLevelLocked/bad_item")) {
            sceClibPrintf("[PATCH-STORE] IsItemLevelLocked(this=%p, item=%p) -> 0; bad item\n",
                          self, item);
        }
        return 0;
    }

    if (!store_aggregator_has_state_for_item_queries(self,
                                                     "IsItemLevelLocked",
                                                     &query_object,
                                                     &config,
                                                     &progress)) {
        if (trace_allow("CStoreAggregator::IsItemLevelLocked/no_progress")) {
            sceClibPrintf("[PATCH-STORE] IsItemLevelLocked(this=%p, item=%p) -> 0; missing state query=%p config=%p progress=%p\n",
                          self, item, query_object, config, progress);
        }
        return 0;
    }

    if (g_original_store_aggregator_is_item_level_locked) {
        return ((level_locked_fn_t)
                g_original_store_aggregator_is_item_level_locked)(self, item);
    }
    return SO_CONTINUE(int, h_store_aggregator_is_item_level_locked,
                       self, item);
}

static int trace_store_aggregator_can_item_be_acquired(void *self, const void *item) {
    typedef int (*can_acquire_fn_t)(void *, const void *);
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

    if (!store_aggregator_has_state_for_item_queries(self,
                                                     "CanItemBeAcquired",
                                                     &query_object,
                                                     &config,
                                                     &progress)) {
        if (trace_allow("CStoreAggregator::CanItemBeAcquired/missing_state")) {
            sceClibPrintf("[PATCH-STORE] CanItemBeAcquired(this=%p, item=%p) -> 0; missing state query=%p config=%p progress=%p\n",
                          self, item, query_object, config, progress);
        }
        return 0;
    }

    if (!store_query_is_real_purchases(query_object)) {
        uint32_t common_cost = 0;
        uint32_t rare_cost = 0;
        uint64_t common_have = 0;
        uint32_t rare_have = 0;
        const char *reason = NULL;
        int ret = store_item_can_be_acquired_offline(config, progress, item,
                                                     &common_cost, &rare_cost,
                                                     &common_have, &rare_have,
                                                     &reason);
        if (trace_allow_ex("CStoreAggregator::CanItemBeAcquired/no_purchases", 24, 180)) {
            sceClibPrintf("[FIX-STORE] CanItemBeAcquired(this=%p, item=%p) -> %d offline reason=%s cost(common=%u rare=%u) have(common=%u:%u rare=%u) query=%p config=%p progress=%p\n",
                          self, item, ret, reason ? reason : "?",
                          common_cost, rare_cost,
                          (unsigned int)(common_have >> 32),
                          (unsigned int)(common_have & 0xffffffffu),
                          rare_have, query_object, config, progress);
        }
        return ret;
    }

    if (g_original_store_aggregator_can_item_be_acquired) {
        return ((can_acquire_fn_t)
                g_original_store_aggregator_can_item_be_acquired)(self, item);
    }
    return SO_CONTINUE(int, h_store_aggregator_can_item_be_acquired,
                       self, item);
}

static int trace_store_aggregator_get_item_status(void *self, const void *item, unsigned int cache) {
    typedef int (*get_status_fn_t)(void *, const void *, unsigned int);
    const void *query_object;
    const void *config;
    const void *progress;

    GUNBROS_PERF_COUNT(store_status_calls);
    if (!trace_is_game_range(item, 0x14)) {
        if (trace_allow("CStoreAggregator::GetItemStatus/bad_item")) {
            sceClibPrintf("[PATCH-STORE] GetItemStatus(this=%p, item=%p, cache=%u) -> -1; bad item\n",
                          self, item, cache);
        }
        return -1;
    }

    if (!store_aggregator_has_state_for_item_queries(self,
                                                     "GetItemStatus",
                                                     &query_object,
                                                     &config,
                                                     &progress)) {
        if (trace_allow("CStoreAggregator::GetItemStatus/missing_state")) {
            sceClibPrintf("[PATCH-STORE] GetItemStatus(this=%p, item=%p, cache=%u) -> 7; missing state query=%p config=%p progress=%p\n",
                          self, item, cache, query_object, config, progress);
        }
        return 7;
    }

    if (!store_query_is_real_purchases(query_object)) {
        uint32_t common_cost = 0;
        uint32_t rare_cost = 0;
        uint64_t common_have = 0;
        uint32_t rare_have = 0;
        const char *reason = NULL;
        int can_acquire = store_item_can_be_acquired_offline(config, progress, item,
                                                             &common_cost, &rare_cost,
                                                             &common_have, &rare_have,
                                                             &reason);
        int status;

        if (player_config_has_any_store_item_ref(config, item)) {
            status = 4;
        } else if (store_item_is_owned_offline(item)) {
            status = 3;
        } else if (!can_acquire) {
            status = 7;
        } else if (*(const unsigned char *)(const void *)((const unsigned char *)item + 0x04) == 0x10u) {
            status = 6;
        } else {
            status = store_item_status_from_marker(item);
        }

        if (trace_allow_ex("CStoreAggregator::GetItemStatus/no_purchases", 24, 180)) {
            sceClibPrintf("[FIX-STORE] GetItemStatus(this=%p, item=%p, cache=%u) -> %d offline reason=%s can=%d cost(common=%u rare=%u) have(common=%u:%u rare=%u) query=%p config=%p progress=%p\n",
                          self, item, cache, status,
                          reason ? reason : "?", can_acquire,
                          common_cost, rare_cost,
                          (unsigned int)(common_have >> 32),
                          (unsigned int)(common_have & 0xffffffffu),
                          rare_have, query_object, config, progress);
        }
        return status;
    }

    if (g_original_store_aggregator_get_item_status) {
        return ((get_status_fn_t)g_original_store_aggregator_get_item_status)(
            self, item, cache);
    }
    return SO_CONTINUE(int, h_store_aggregator_get_item_status,
                       self, item, cache);
}

static int trace_store_aggregator_acquire_item(void *self, const void *item, unsigned int equip_now) {
    const void *query_object = NULL;
    const void *config = NULL;
    const void *progress = NULL;
    const void *query_hint = NULL;
    unsigned int type = 0xffu;
    unsigned int count = 0u;
    const void *refs = NULL;
    unsigned int ref_id = 0u;
    unsigned int ref_type = 0xffu;
    unsigned int ref_subtype = 0xffu;
    int ret;

    /* Ownership, affordability and status filters may all change.  Failed
     * purchases are uncommon and conservatively invalidate as well. */
    store_filtered_list_invalidate(self);

    if (trace_is_game_range(item, 0x20u)) {
        type = *(const unsigned char *)(const void *)((const unsigned char *)item + 0x4);
        count = *(const uint32_t *)(const void *)((const unsigned char *)item + 0x10);
        refs = *(const void * const *)(const void *)((const unsigned char *)item + 0x0c);
    }

    if (!trace_is_game_range(item, 0x14u)) {
        if (trace_allow("CStoreAggregator::AcquireItem/bad_item")) {
            sceClibPrintf("[FIX-STORE] AcquireItem(this=%p, item=%p, equip=%u) -> 0 bad item\n",
                          self, item, equip_now);
        }
        return 0;
    }

    if (count > 0 &&
        (count > 64u ||
         !trace_is_game_range(refs,
                              (size_t)count * STORE_ITEM_REF_SIZE))) {
        return 0;
    }

    if (trace_is_game_range(self,
                            STORE_AGG_OFF_PURCHASES + sizeof(void *))) {
        query_hint = *(const void * const *)(const void *)
            ((const unsigned char *)self + STORE_AGG_OFF_PURCHASES);
    }

    /* Boss/world pickups are free grants. They happen while the store's
     * config/progress pointers may be between gameplay generations, so do not
     * require that unrelated tuple. Increment every distinct purchases owner
     * used by the aggregator, HUD, and persistent sidecar before returning. */
    if (equip_now) {
        int powerup_changed = 0;
        if (store_item_grant_powerups(item, query_hint,
                                      &powerup_changed)) {
            if (powerup_changed) {
                vita_offline_profile_request_save();
            }
            return 1;
        }
    }

    if (!store_aggregator_has_state_for_item_queries(self,
                                                     "AcquireItem",
                                                     &query_object,
                                                     &config,
                                                     &progress)) {
        if (trace_allow("CStoreAggregator::AcquireItem/missing_state")) {
            sceClibPrintf("[FIX-STORE] AcquireItem(this=%p, item=%p type=%u count=%u refs=%p equip=%u) -> 0 missing state query=%p config=%p progress=%p\n",
                          self, item, type, count, refs, equip_now, query_object, config, progress);
        }
        return 0;
    }

    (void)store_item_get_ref_info(item, 0, &ref_id, &ref_subtype, &ref_type);
    if (!store_query_is_real_purchases(query_object)) {
        const unsigned char *gun_ref;
        const unsigned char *armor_ref;
        unsigned int slot = 0xffu;
        uint32_t common_cost = 0;
        uint32_t rare_cost = 0;
        uint64_t common_have = 0;
        uint32_t rare_have = 0;
        const char *reason = NULL;
        int has_powerups = 0;
        int powerup_changed = 0;

        if (!store_item_can_be_acquired_offline(config, progress, item,
                                                &common_cost, &rare_cost,
                                                &common_have, &rare_have,
                                                &reason)) {
            store_aggregator_set_failed_purchase_offline(self,
                                                         common_cost,
                                                         rare_cost,
                                                         reason);
            if (trace_allow_ex("CStoreAggregator::AcquireItem/offline_cannot", 32, 180)) {
                sceClibPrintf("[FIX-STORE] AcquireItem(this=%p, item=%p type=%u ref=%u:%u:%u equip=%u) -> 0 offline reason=%s cost(common=%u rare=%u) have(common=%u:%u rare=%u) query=%p config=%p progress=%p\n",
                              self, item, type, ref_type, ref_id, ref_subtype,
                              equip_now, reason ? reason : "?",
                              common_cost, rare_cost,
                              (unsigned int)(common_have >> 32),
                              (unsigned int)(common_have & 0xffffffffu),
                              rare_have, query_object, config, progress);
            }
            return 0;
        }
        if ((common_cost != 0u && common_have < (uint64_t)common_cost) ||
            (common_cost == 0u && rare_have < rare_cost)) {
            store_aggregator_set_failed_purchase_offline(
                self, common_cost, rare_cost, "insufficient-funds");
            if (trace_allow_ex("CStoreAggregator::AcquireItem/offline_funds", 32, 180)) {
                sceClibPrintf("[FIX-STORE] AcquireItem(this=%p, item=%p type=%u ref=%u:%u:%u equip=%u) -> 0 offline insufficient funds cost(common=%u rare=%u) have(common=%u:%u rare=%u) query=%p config=%p progress=%p\n",
                              self, item, type, ref_type, ref_id, ref_subtype,
                              equip_now, common_cost, rare_cost,
                              (unsigned int)(common_have >> 32),
                              (unsigned int)(common_have & 0xffffffffu),
                              rare_have, query_object, config, progress);
            }
            return 0;
        }

        (void)store_item_powerups_have_capacity(item, &has_powerups);
        if (has_powerups) {
            if (!store_item_grant_powerups(item, query_object,
                                           &powerup_changed) ||
                !powerup_changed) {
                store_aggregator_set_failed_purchase_offline(
                    self, common_cost, rare_cost, "consumable-max");
                return 0;
            }
            store_progress_pay_item_offline((void *)progress, item);
            vita_offline_profile_request_save();
            return 1;
        }

        gun_ref = store_item_find_ref_of_type(item, GUNBROS_OBJECT_TYPE_GUN,
                                              &ref_id, &ref_subtype);
        if (gun_ref) {
            /* Purchasing records ownership; only immediate equip needs the
             * referenced game object to be loaded right now. Requiring a live
             * template for equip_now=0 incorrectly rejected valid/free cards
             * whose content was still deferred. */
            if (equip_now &&
                !gameplay_gun_ref_resolves(ref_id, ref_subtype,
                                           "AcquireItem/offline-gun")) {
                store_aggregator_set_failed_purchase_offline(
                    self, common_cost, rare_cost, "gun-content-deferred");
                if (trace_allow_ex("CStoreAggregator::AcquireItem/offline_gun_unresolved", 24, 180)) {
                    sceClibPrintf("[FIX-STORE] AcquireItem(this=%p, item=%p type=%u ref=gun:%u:%u equip=%u) -> 0 offline unresolved gun query=%p config=%p progress=%p\n",
                                  self, item, type, ref_id, ref_subtype,
                                  equip_now, query_object, config, progress);
                }
                return 0;
            }

            /* Preserve the displaced active gun as owned, then add every ref
             * in this store item to the offline inventory. Only equip when the
             * native AcquireItem call explicitly requested it. */
            vita_offline_owned_capture_equipped(config);
            player_config_install_gun_store_ref((void *)config, gun_ref,
                                                ref_id, ref_subtype,
                                                equip_now ? 1u : 0u, &slot);
            if (equip_now && slot == 0xffu) {
                store_aggregator_set_failed_purchase_offline(
                    self, common_cost, rare_cost, "gun-equip-failed");
                return 0;
            }
            (void)store_item_add_owned_offline(item);
            store_progress_pay_item_offline((void *)progress, item);
            if (equip_now) {
                (void)gameplay_install_store_gun_ref(gun_ref, ref_id, ref_subtype,
                                                     1u, "AcquireItem/offline-gun");
                (void)vita_offline_loadout_save(
                    config, "CStoreAggregator::AcquireItem/gun");
            }
            vita_offline_profile_request_save();
            if (trace_allow_ex("CStoreAggregator::AcquireItem/offline_gun", 32, 180)) {
                sceClibPrintf("[FIX-STORE] AcquireItem(this=%p, item=%p type=%u ref=gun:%u:%u equip=%u) -> 1 offline reason=%s slot=%u live_install=%u paid(common=%u rare=%u) query=%p config=%p progress=%p\n",
                              self, item, type, ref_id, ref_subtype,
                              equip_now, reason ? reason : "?", slot,
                              equip_now ? 1u : 0u,
                              common_cost, rare_cost,
                              query_object, config, progress);
                trace_dump_player_config_summary("store-acquire-gun/config", config);
            }
            return 1;
        }

        armor_ref = store_item_find_ref_of_type(item, GUNBROS_OBJECT_TYPE_ARMOR,
                                                &ref_id, &ref_subtype);
        if (armor_ref) {
            void *brother;

            if (equip_now &&
                !gameplay_armor_ref_resolves(ref_id, ref_subtype,
                                             "AcquireItem/offline-armor")) {
                store_aggregator_set_failed_purchase_offline(
                    self, common_cost, rare_cost, "armor-content-deferred");
                if (trace_allow_ex("CStoreAggregator::AcquireItem/offline_armor_unresolved", 24, 180)) {
                    sceClibPrintf("[FIX-STORE] AcquireItem(this=%p, item=%p type=%u ref=armor:%u:%u equip=%u) -> 0 offline unresolved armor query=%p config=%p progress=%p\n",
                                  self, item, type, ref_id, ref_subtype,
                                  equip_now, query_object, config, progress);
                }
                return 0;
            }

            vita_offline_owned_capture_equipped(config);
            if (equip_now &&
                !player_config_install_armor_store_ref((void *)config, armor_ref,
                                                       ref_id, ref_subtype, &slot)) {
                store_aggregator_set_failed_purchase_offline(
                    self, common_cost, rare_cost, "armor-equip-failed");
                if (trace_allow_ex("CStoreAggregator::AcquireItem/offline_armor_install_failed", 24, 180)) {
                    sceClibPrintf("[FIX-STORE] AcquireItem(this=%p, item=%p ref=armor:%u:%u) -> 0 typed armor install failed config=%p\n",
                                  self, item, ref_id, ref_subtype, config);
                }
                return 0;
            }
            (void)store_item_add_owned_offline(item);
            store_progress_pay_item_offline((void *)progress, item);
            if (equip_now) {
                brother = gameplay_find_brother_target();
                if (brother) {
                    gameplay_repair_player_model(brother, "AcquireItem/offline-armor");
                }
            }
            vita_offline_profile_request_save();
            if (trace_allow_ex("CStoreAggregator::AcquireItem/offline_armor", 32, 180)) {
                sceClibPrintf("[FIX-STORE] AcquireItem(this=%p, item=%p type=%u ref=armor:%u:%u equip=%u) -> 1 offline reason=%s slot=%u paid(common=%u rare=%u) query=%p config=%p progress=%p\n",
                              self, item, type, ref_id, ref_subtype,
                              equip_now, reason ? reason : "?", slot,
                              common_cost, rare_cost,
                              query_object, config, progress);
                trace_dump_player_config_summary("store-acquire-armor/config", config);
            }
            return 1;
        }

        /* Permanent non-equipment items use the typed owned-item ledger.
         * Quantity-based power-ups were handled by CPurchases above. */
        if (store_item_add_owned_offline(item)) {
            store_progress_pay_item_offline((void *)progress, item);
            vita_offline_profile_request_save();
            if (trace_allow_ex("CStoreAggregator::AcquireItem/offline_generic", 32, 180)) {
                sceClibPrintf("[FIX-STORE] AcquireItem(this=%p, item=%p type=%u ref=%u:%u:%u equip=%u) -> 1 offline generic paid(common=%u rare=%u) query=%p config=%p progress=%p\n",
                              self, item, type, ref_type, ref_id, ref_subtype,
                              equip_now, common_cost, rare_cost,
                              query_object, config, progress);
            }
            return 1;
        }

        if (trace_allow_ex("CStoreAggregator::AcquireItem/no_purchases", 24, 180)) {
            sceClibPrintf("[FIX-STORE] AcquireItem(this=%p, item=%p type=%u ref=%u:%u:%u equip=%u) -> 0 offline item has no ownable refs query=%p config=%p progress=%p\n",
                          self, item, type, ref_type, ref_id, ref_subtype,
                          equip_now, query_object, config, progress);
        }
        store_aggregator_set_failed_purchase_offline(
            self, common_cost, rare_cost, "item-has-no-ownable-refs");
        return 0;
    }

    ret = SO_CONTINUE(int, h_store_aggregator_acquire_item, self, item, equip_now);
    if (ret && equip_now && ref_subtype != 0xffu &&
        (ref_type == GUNBROS_OBJECT_TYPE_GUN || type == GUNBROS_OBJECT_TYPE_GUN)) {
        gameplay_force_player_gun(ref_subtype, ref_id, "AcquireItem/ref");
    } else if (ret && ref_subtype != 0xffu &&
               (ref_type == GUNBROS_OBJECT_TYPE_ARMOR || type == GUNBROS_OBJECT_TYPE_ARMOR)) {
        void *brother = gameplay_find_brother_target();
        if (brother) {
            gameplay_repair_player_model(brother, "AcquireItem/real-armor");
        }
    }
    if (ret) {
        /* Mirror successful native CPurchases ownership into the Vita ledger
         * as well; the network profile's later SaveAll is not dependable in
         * the offline port, while the currency/config mutation is already in
         * the adopted live profile pair. */
        (void)store_item_add_owned_offline(item);
        vita_offline_profile_request_save();
    }

    if (trace_allow_ex("CStoreAggregator::AcquireItem/result", 24, 180)) {
        sceClibPrintf("[FIX-STORE] AcquireItem(this=%p item=%p type=%u ref=%u:%u:%u equip=%u) -> %d state query=%p config=%p progress=%p\n",
                      self, item, type, ref_type, ref_id, ref_subtype,
                      equip_now, ret, query_object, config, progress);
    }

    return ret;
}

static int trace_store_aggregator_equip_item(void *self,
                                             unsigned int filtered_index,
                                             void *config) {
    const void *equipped_config = NULL;
    const char *mode = "preview";
    int armor_repairs;
    int applied = 0;
    int ret;

    if (trace_is_game_range(self, STORE_AGG_OFF_CONFIG + sizeof(void *))) {
        equipped_config = *(const void * const *)(const void *)
            ((const unsigned char *)self + STORE_AGG_OFF_CONFIG);
    }
    if (config == equipped_config ||
        vita_offline_profile_config_is_authoritative(config)) {
        mode = "equip";
        store_filtered_list_invalidate(self);
        /* Capture the active refs before native SetGun/SetArmor replaces
         * them. Never promote a temporary preview configuration to owned. */
        vita_offline_owned_capture_equipped(config);
    }
    ret = SO_CONTINUE(int, h_store_aggregator_equip_item,
                      self, filtered_index, config);

    /* PreviewItem is an ELF tail-branch to this exact overload. Keep its
     * temporary configuration isolated; only an actual equipped config may
     * update the live CPlayer. Both copies still receive the narrow legacy
     * armor-slot cleanup so the preview renders pants/vest/trinket correctly. */
    armor_repairs = player_config_repair_armor_slots(config,
                                                     config == equipped_config ?
                                                     "CStoreAggregator::EquipItem/equip" :
                                                     "CStoreAggregator::EquipItem/preview");
    /* SetArmor changes only CPlayerConfiguration. Make the newly selected
     * helmet's template/mesh requirement live before CMenuMeshPlayer releases
     * the previous armor and rebinds its embedded CBrother. */
    gameplay_request_configured_content(
        config,
        config == equipped_config ?
            "CStoreAggregator::EquipItem/equip" :
            "CStoreAggregator::EquipItem/preview");
    if (config == equipped_config ||
        vita_offline_profile_config_is_authoritative(config)) {
        applied = gameplay_apply_configured_player_gun(config,
                                                       "CStoreAggregator::EquipItem");
        vita_offline_owned_capture_equipped(config);
        (void)vita_offline_loadout_save(
            config, "CStoreAggregator::EquipItem");
        vita_offline_profile_request_save();
    }

    if (trace_allow_ex("CStoreAggregator::EquipItem/result", 32, 180)) {
        sceClibPrintf("[FIX-STORE] EquipItem(this=%p index=%u config=%p configured=%p mode=%s) -> %d live_apply=%d armor_repairs=%d\n",
                      self, filtered_index, config, equipped_config, mode,
                      ret, applied, armor_repairs);
        if (trace_is_player_config_for_brother(config)) {
            trace_dump_player_config_summary("store-equip/config", config);
        }
    }
    return ret;
}

static unsigned int trace_store_aggregator_init_filtered_list(
    void *self, unsigned int type) {
    typedef unsigned int (*init_filtered_list_fn_t)(void *, unsigned int);
    const void *query_object;
    const void *config;
    const void *progress;
#ifdef GUNBROS_ENABLE_PERF_TRACE
    uint64_t begin_us;
    uint64_t end_us;
    unsigned int queries_before;
    unsigned int hits_before;
    unsigned int objects_before;
    unsigned int filtered_before;
    unsigned int locked_before;
    unsigned int status_before;
    unsigned int cost_before;
    unsigned int sale_before;
    unsigned int cache_capacity;
#endif
    unsigned int result;

    /* Configure can run before the Vita hooks are installed.  Build 48 never
     * printed PERF-STORE-CACHE and consequently rebuilt 290 objects on each
     * broad scan.  SetItemCacheSize is a native public lifecycle operation,
     * so applying it lazily here is safe and guarantees it happens before the
     * first measured filter starts populating the circular cache. */
    store_aggregator_expand_item_cache(
        self, "CStoreAggregator::InitFilteredList/preload");

    if (!store_aggregator_has_state_for_item_queries(self,
                                                     "InitFilteredList",
                                                     &query_object,
                                                     &config,
                                                     &progress)) {
        if (trace_allow("CStoreAggregator::InitFilteredList/missing_state")) {
            sceClibPrintf("[PATCH-STORE] InitFilteredList(this=%p, type=%u) skipped; missing state query=%p config=%p progress=%p\n",
                          self, type, query_object, config, progress);
        }
        return 0u;
    }

    if (!store_aggregator_query_object_is_valid(query_object)) {
        if (trace_allow_ex("CStoreAggregator::InitFilteredList/no_purchases_shim", 8, 180)) {
            sceClibPrintf("[FIX-STORE] InitFilteredList(this=%p, type=%u) skipped; no valid purchases shim query=%p config=%p progress=%p\n",
                          self, type, query_object, config, progress);
        }
        return 0u;
    }

    if (store_filtered_list_matches(self, type, query_object, config,
                                    progress)) {
        result = *(const unsigned short *)(const void *)
            ((const unsigned char *)self + 0x28u);
        GUNBROS_PERF_LOG("[PERF-STORE-CACHE] reused filter store=%p type=%u count=%u\n",
                         self, type, result);
        return result;
    }

#ifdef GUNBROS_ENABLE_PERF_TRACE
    queries_before = g_gunbros_perf_store_query_calls;
    hits_before = g_gunbros_perf_store_query_cache_hits;
    objects_before = g_gunbros_perf_game_object_init_calls;
    filtered_before = g_gunbros_perf_store_filtered_count_calls;
    locked_before = g_gunbros_perf_store_level_lock_calls;
    status_before = g_gunbros_perf_store_status_calls;
    cost_before = g_gunbros_perf_store_cost_string_calls;
    sale_before = g_gunbros_perf_store_sale_string_calls;
    begin_us = sceKernelGetProcessTimeWide();
#endif
    g_store_cache_filter_build_depth++;
    if (g_original_store_aggregator_init_filtered_list) {
        result = ((init_filtered_list_fn_t)
                  g_original_store_aggregator_init_filtered_list)(self, type);
    } else {
        result = SO_CONTINUE(unsigned int,
                             h_store_aggregator_init_filtered_list,
                             self, type);
    }
    g_store_cache_filter_build_depth--;
    store_filtered_list_remember(self, type, query_object, config, progress);
#ifdef GUNBROS_ENABLE_PERF_TRACE
    end_us = sceKernelGetProcessTimeWide();
    cache_capacity = trace_is_game_range(self, 0x2cu) ?
        *(const uint32_t *)(const void *)
            ((const unsigned char *)self + 0x18u) : 0u;
    GUNBROS_PERF_LOG("[PERF-STORE-LOAD] stage=InitFilteredList type=%u elapsed_us=%llu cache=%u queries=%u/%u gobj_init=%u hot=%u/%u/%u/%u/%u\n",
                     type,
                     (unsigned long long)(end_us >= begin_us ?
                         end_us - begin_us : 0u),
                     cache_capacity,
                     g_gunbros_perf_store_query_calls - queries_before,
                     g_gunbros_perf_store_query_cache_hits - hits_before,
                     g_gunbros_perf_game_object_init_calls - objects_before,
                     g_gunbros_perf_store_filtered_count_calls - filtered_before,
                     g_gunbros_perf_store_level_lock_calls - locked_before,
                     g_gunbros_perf_store_status_calls - status_before,
                     g_gunbros_perf_store_cost_string_calls - cost_before,
                     g_gunbros_perf_store_sale_string_calls - sale_before);
    if (g_gunbros_perf_game_object_init_calls != objects_before) {
        GUNBROS_PERF_LOG("[PERF-PRELOAD] kind=store-metadata type=%u cache=%u objects=%u elapsed_us=%llu status=retained\n",
                         type, cache_capacity,
                         g_gunbros_perf_game_object_init_calls - objects_before,
                         (unsigned long long)(end_us >= begin_us ?
                             end_us - begin_us : 0u));
    }
#endif
    return result;
}

static const void *store_aggregator_get_list_object(const void *self, unsigned short index);

/* Native GetFilteredItemCount resolves every list object on every query.
 * Build all category counts together; the list lifecycle owns invalidation. */
static int store_cached_category_count(void *self, unsigned type, unsigned *out) {
    static struct {
        void *store;
        const void *list;
        unsigned revision, generation, length, valid;
        unsigned counts[32];
    } cache;
    if(type>=32 || g_store_cache_filter_build_depth ||
       !g_store_filtered_list_cache.valid || g_store_filtered_list_cache.store!=self ||
       !trace_is_game_range(self,0x44)) return 0;
    const unsigned char *base=self;
    const void *list=*(const void * const *)(const void *)(base+0x20);
    unsigned length=*(const unsigned short *)(const void *)(base+0x28);
    unsigned generation=vita_offline_profile_generation();
    if(cache.valid && cache.store==self && cache.list==list &&
       cache.revision==g_store_list_revision && cache.generation==generation &&
       cache.length==length) { *out=cache.counts[type]; return 1; }
    cache.valid=0;
    memset(cache.counts,0,sizeof(cache.counts));
    for(unsigned i=0;i<length;++i) {
        const unsigned char *item=store_aggregator_get_list_object(self,(unsigned short)i);
        /* Incomplete resource loading must be retried, not cached as zero. */
        if(!trace_is_game_range(item,0x10)) return 0;
        const unsigned char *templ;
        memcpy(&templ,item+0xc,sizeof(templ));
        if(!trace_is_game_range(templ,12)) return 0;
        unsigned category=*(const unsigned *)(const void *)(templ+8);
        if(category<32) ++cache.counts[category];
    }
    cache.store=self; cache.list=list; cache.length=length;
    cache.revision=g_store_list_revision; cache.generation=generation; cache.valid=1;
    *out=cache.counts[type]; return 1;
}

static unsigned int trace_store_aggregator_get_filtered_item_count(void *self, unsigned int type) {
    typedef unsigned int (*filtered_count_fn_t)(void *, unsigned int);
    const void *query_object = NULL;
    const void *config = NULL;
    const void *progress = NULL;
    unsigned int count;

    GUNBROS_PERF_COUNT(store_filtered_count_calls);
    if (!trace_is_game_range(self, 0x44)) {
        if (trace_allow("CStoreAggregator::GetFilteredItemCount/bad_self")) {
            sceClibPrintf("[DIAG-STORE] GetFilteredItemCount(this=%p type=%u) -> 0 bad store\n",
                          self, type);
        }
        return 0;
    }

    (void)store_aggregator_repair_profile_state(self, "GetFilteredItemCount",
                                                &query_object, &config, &progress);
    if(store_cached_category_count(self,type,&count)) return count;
    if (g_original_store_aggregator_get_filtered_item_count) {
        count = ((filtered_count_fn_t)
                 g_original_store_aggregator_get_filtered_item_count)(
                     self, type);
    } else {
        count = SO_CONTINUE(unsigned int,
                            h_store_aggregator_get_filtered_item_count,
                            self, type);
    }

    if ((type == GUNBROS_OBJECT_TYPE_GUN || count == 0) &&
        trace_allow_ex("CStoreAggregator::GetFilteredItemCount/diag", 30, 180)) {
        sceClibPrintf("[DIAG-STORE] GetFilteredItemCount(this=%p type=%u) -> %u query=%p config=%p progress=%p\n",
                      self, type, count, query_object, config, progress);
    }

    return count;
}

static const void *store_aggregator_get_list_object(const void *self,
                                                    unsigned short index) {
    typedef const void *(*get_list_object_fn_t)(const void *self,
                                                unsigned short index);
    static get_list_object_fn_t get_list_object;

    if (!get_list_object) {
        get_list_object = (get_list_object_fn_t)(uintptr_t)
            so_symbol(&so_mod, "_ZNK16CStoreAggregator13GetListObjectEt");
    }
    return get_list_object ? get_list_object(self, index) : NULL;
}

static uint32_t trace_store_aggregator_get_item_cost(void *self,
                                                     unsigned short index,
                                                     unsigned int currency) {
    typedef uint32_t (*get_item_cost_fn_t)(void *, unsigned short,
                                           unsigned int);
    const void *query_object = NULL;
    const void *item;
    uint32_t common_cost = 0;
    uint32_t rare_cost = 0;
    unsigned int type;
    unsigned int iap;

    if (!store_aggregator_repair_profile_state(self, "GetItemCost",
                                               &query_object, NULL, NULL) ||
        store_query_is_real_purchases(query_object)) {
        if (g_original_store_aggregator_get_item_cost) {
            return ((get_item_cost_fn_t)
                    g_original_store_aggregator_get_item_cost)(
                        self, index, currency);
        }
        return SO_CONTINUE(uint32_t, h_store_aggregator_get_item_cost,
                           self, index, currency);
    }

    item = store_aggregator_get_list_object(self, index);
    if (!trace_is_game_range(item, 0x21u) ||
        !store_item_get_currency_costs(item, &common_cost, &rare_cost)) {
        return 0;
    }

    type = *(const unsigned char *)(const void *)
        ((const unsigned char *)item + 0x04u);
    iap = *(const unsigned char *)(const void *)
        ((const unsigned char *)item + 0x20u);

    /* The Android implementation returns zero for every IAP-marked item.
     * Billing is unavailable on Vita, but non-currency store cards still
     * carry their original in-game prices at +0x18/+0x1c.  Expose those
     * prices consistently to the menu and the offline acquisition path.
     * Type 0x10 is the actual currency-pack category and remains an IAP. */
    if (iap != 0u && type != 0x10u) {
        uint32_t result = currency == 0u ? common_cost : rare_cost;
        if (trace_allow_ex("CStoreAggregator::GetItemCost/offline_iap", 24, 180)) {
            sceClibPrintf("[FIX-STORE] GetItemCost(this=%p index=%u currency=%u) -> %u offline embedded price item=%p type=%u iap=%u common=%u rare=%u\n",
                          self, (unsigned int)index, currency, result,
                          item, type, iap, common_cost, rare_cost);
        }
        return result;
    }

    if (g_original_store_aggregator_get_item_cost) {
        return ((get_item_cost_fn_t)
                g_original_store_aggregator_get_item_cost)(
                    self, index, currency);
    }
    return SO_CONTINUE(uint32_t, h_store_aggregator_get_item_cost,
                       self, index, currency);
}

static int trace_store_aggregator_is_in_app_purchase(void *self,
                                                      unsigned short index) {
    const void *query_object = NULL;
    const void *item;
    unsigned int type;
    unsigned int iap;

    if (!store_aggregator_repair_profile_state(self, "IsInAppPurchase",
                                               &query_object, NULL, NULL) ||
        store_query_is_real_purchases(query_object)) {
        return SO_CONTINUE(int, h_store_aggregator_is_in_app_purchase,
                           self, index);
    }

    item = store_aggregator_get_list_object(self, index);
    if (!trace_is_game_range(item, 0x21u)) {
        return 0;
    }

    type = *(const unsigned char *)(const void *)
        ((const unsigned char *)item + 0x04u);
    iap = *(const unsigned char *)(const void *)
        ((const unsigned char *)item + 0x20u);

    if (iap != 0u && type != 0x10u) {
        if (trace_allow_ex("CStoreAggregator::IsInAppPurchase/offline", 24, 180)) {
            sceClibPrintf("[FIX-STORE] IsInAppPurchase(this=%p index=%u) -> 0 offline embedded-price item=%p type=%u iap=%u\n",
                          self, (unsigned int)index, item, type, iap);
        }
        return 0;
    }

    return iap != 0u;
}

static void *trace_store_aggregator_create_item_cost_string(void *self,
                                                            unsigned short index) {
    typedef void *(*cost_string_fn_t)(void *, unsigned short);
    const void *query_object = NULL;
    const void *config = NULL;
    const void *progress = NULL;
    const void *item;
    void *result;
    uint32_t common_cost = 0;
    uint32_t rare_cost = 0;
    unsigned int type = 0xffu;
    unsigned int iap = 0u;

    GUNBROS_PERF_COUNT(store_cost_string_calls);

    if (!store_aggregator_repair_profile_state(self, "CreateItemCostString",
                                               &query_object,
                                               &config,
                                               &progress)) {
        return NULL;
    }

    item = store_aggregator_get_list_object(self, index);
    if (trace_is_game_range(item, 0x21u)) {
        type = *(const unsigned char *)(const void *)
            ((const unsigned char *)item + 0x04u);
        iap = *(const unsigned char *)(const void *)
            ((const unsigned char *)item + 0x20u);
        (void)store_item_get_currency_costs(item, &common_cost, &rare_cost);
    }

    if (!store_query_is_real_purchases(query_object) &&
        trace_is_game_range(item, 0x21u) &&
        iap != 0u && type != 0x10u) {
        unsigned char *iap_flag = (unsigned char *)item + 0x20u;
        unsigned char saved_iap = *iap_flag;

        /* CreateItemCostString deliberately returns NULL for an Android IAP.
         * Temporarily expose the embedded in-game price to the original
         * formatter so localization, Free handling and currency icons remain
         * exactly native. Restore the immutable resource item immediately. */
        *iap_flag = 0u;
        if (g_original_store_aggregator_create_item_cost_string) {
            result = ((cost_string_fn_t)
                      g_original_store_aggregator_create_item_cost_string)(
                          self, index);
        } else {
            result = SO_CONTINUE(
                void *, h_store_aggregator_create_item_cost_string,
                self, index);
        }
        *iap_flag = saved_iap;
    } else {
        if (g_original_store_aggregator_create_item_cost_string) {
            result = ((cost_string_fn_t)
                      g_original_store_aggregator_create_item_cost_string)(
                          self, index);
        } else {
            result = SO_CONTINUE(
                void *, h_store_aggregator_create_item_cost_string,
                self, index);
        }
    }

    if (trace_allow_ex("CStoreAggregator::CreateItemCostString/result", 32, 180)) {
        sceClibPrintf("[FIX-STORE] CreateItemCostString(this=%p index=%u) -> %p item=%p type=%u iap=%u cost(common=%u rare=%u) query=%p config=%p progress=%p mode=%s\n",
                      self, (unsigned int)index, result, item, type, iap,
                      common_cost, rare_cost, query_object, config, progress,
                      !store_query_is_real_purchases(query_object) ? "offline" : "native");
    }
    return result;
}

static void *trace_store_aggregator_create_sale_string(void *self, unsigned short index) {
    typedef void *(*sale_string_fn_t)(void *, unsigned short);
    const void *query_object = NULL;
    const void *config = NULL;
    const void *progress = NULL;
    const void *item;
    int promotion_status;

    GUNBROS_PERF_COUNT(store_sale_string_calls);

    if (!store_aggregator_repair_profile_state(self, "CreateSaleString",
                                               &query_object,
                                               &config,
                                               &progress)) {
        return NULL;
    }

    if (!store_query_is_real_purchases(query_object)) {
        item = store_aggregator_get_list_object(self, index);
        promotion_status = store_item_status_from_marker(item);
        if (promotion_status < 0) {
            /* Ordinary embedded store items have marker 0 and must not create
             * a ribbon or replace their normal price. */
            return NULL;
        }
        /* Preserve an actual embedded/override promotion. The original
         * formatter consumes its sale text, price and expiry fields and calls
         * our corrected marker-to-status mapping. */
        if (g_original_store_aggregator_create_sale_string) {
            return ((sale_string_fn_t)
                    g_original_store_aggregator_create_sale_string)(
                        self, index);
        }
        return SO_CONTINUE(void *, h_store_aggregator_create_sale_string,
                           self, index);
    }

    if (g_original_store_aggregator_create_sale_string) {
        return ((sale_string_fn_t)
                g_original_store_aggregator_create_sale_string)(self, index);
    }
    return SO_CONTINUE(void *, h_store_aggregator_create_sale_string,
                       self, index);
}
