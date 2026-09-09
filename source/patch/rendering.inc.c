/* Mesh, sprite, animation, and render metadata safety patches. */

static int mesh_has_valid_frames(const void *self) {
    const unsigned char *base = (const unsigned char *)self;
    const void *frames;
    uint32_t frame_count;

    if (!trace_is_game_range(self, 0x30u)) {
        return 0;
    }

    frames = *(const void * const *)(const void *)(base + 0x28);
    frame_count = *(const uint32_t *)(const void *)(base + 0x2c);
    if (frame_count == 0) {
        return 0;
    }
    if (frame_count > 512u || !trace_is_game_range(frames, (size_t)frame_count * 0x14u)) {
        return 0;
    }

    return 1;
}

static int mesh_vertex_buffer_is_valid(const void *vertex_buffer) {
    const void *vptr;

    if (!trace_is_game_range(vertex_buffer, sizeof(void *))) {
        return 0;
    }

    vptr = *(const void * const *)vertex_buffer;
    return trace_is_game_range(vptr, 0x84u);
}

typedef void (*mesh_build_tween_frame_fn_t)(const void *, int, int, float, void *);

static void continue_mesh_build_tween_frame(const void *self,
                                            int frame_a,
                                            int frame_b,
                                            float t,
                                            void *vertex_buffer) {
    mesh_build_tween_frame_fn_t fn;

    if (!h_mesh_build_tween_frame.addr) {
        return;
    }

    kuKernelCpuUnrestrictedMemcpy((void *)h_mesh_build_tween_frame.addr,
                                  h_mesh_build_tween_frame.orig_instr,
                                  sizeof(h_mesh_build_tween_frame.orig_instr));
    kuKernelFlushCaches((void *)h_mesh_build_tween_frame.addr,
                        sizeof(h_mesh_build_tween_frame.orig_instr));
    fn = (mesh_build_tween_frame_fn_t)(uintptr_t)(h_mesh_build_tween_frame.thumb_addr ?
                                                  h_mesh_build_tween_frame.thumb_addr :
                                                  h_mesh_build_tween_frame.addr);
    fn(self, frame_a, frame_b, t, vertex_buffer);
    kuKernelCpuUnrestrictedMemcpy((void *)h_mesh_build_tween_frame.addr,
                                  h_mesh_build_tween_frame.patch_instr,
                                  sizeof(h_mesh_build_tween_frame.patch_instr));
    kuKernelFlushCaches((void *)h_mesh_build_tween_frame.addr,
                        sizeof(h_mesh_build_tween_frame.patch_instr));
}

static void trace_mesh_build_tween_frame(const void *self,
                                         int frame_a,
                                         int frame_b,
                                         float t,
                                         void *vertex_buffer) {
    if (!mesh_has_valid_frames(self) || !mesh_vertex_buffer_is_valid(vertex_buffer)) {
        if (trace_allow_ex("CMesh::BuildTweenFrame/bad_input", 30, 180)) {
            const unsigned char *base = (const unsigned char *)self;
            const void *frames = trace_is_game_range(self, 0x30u) ?
                                 *(const void * const *)(const void *)(base + 0x28) : NULL;
            uint32_t frame_count = trace_is_game_range(self, 0x30u) ?
                                   *(const uint32_t *)(const void *)(base + 0x2c) : 0;
            const void *vb_vptr = trace_is_game_range(vertex_buffer, sizeof(void *)) ?
                                  *(const void * const *)vertex_buffer : NULL;
            sceClibPrintf("[FIX-MESH] BuildTweenFrame(mesh=%p frames=%d/%d t=%.3f vb=%p vb_vptr=%p) skipped bad frames=%p count=%u\n",
                          self, frame_a, frame_b, t, vertex_buffer, vb_vptr,
                          frames, (unsigned int)frame_count);
        }
        return;
    }

    continue_mesh_build_tween_frame(self, frame_a, frame_b, t, vertex_buffer);
}

static void trace_mesh_get_vertices_at(const void *self, int frame, void *vertex_buffer) {
    if (!mesh_has_valid_frames(self) || !mesh_vertex_buffer_is_valid(vertex_buffer)) {
        if (trace_allow_ex("CMesh::GetVerticesAt/bad_input", 30, 180)) {
            const unsigned char *base = (const unsigned char *)self;
            const void *frames = trace_is_game_range(self, 0x30u) ?
                                 *(const void * const *)(const void *)(base + 0x28) : NULL;
            uint32_t frame_count = trace_is_game_range(self, 0x30u) ?
                                   *(const uint32_t *)(const void *)(base + 0x2c) : 0;
            const void *vb_vptr = trace_is_game_range(vertex_buffer, sizeof(void *)) ?
                                  *(const void * const *)vertex_buffer : NULL;
            sceClibPrintf("[FIX-MESH] GetVerticesAt(mesh=%p frame=%d vb=%p vb_vptr=%p) skipped bad frames=%p count=%u\n",
                          self, frame, vertex_buffer, vb_vptr,
                          frames, (unsigned int)frame_count);
        }
        return;
    }

    (void)SO_CONTINUE(int, h_mesh_get_vertices_at, self, frame, vertex_buffer);
}

static void trace_mesh_animation_controller_render(const void *self) {
    const unsigned char *base = (const unsigned char *)self;
    const void *mesh;
    const void *vertex_buffer;
    int frame;

    if (!trace_is_game_range(self, 0x10u)) {
        if (trace_allow_ex("CMeshAnimationController::Render/bad_self", 12, 180)) {
            sceClibPrintf("[FIX-MESH] AnimationController::Render(self=%p) skipped bad controller\n",
                          self);
        }
        return;
    }

    mesh = *(const void * const *)(const void *)(base + 0x04);
    vertex_buffer = *(const void * const *)(const void *)(base + 0x08);
    frame = *(const int *)(const void *)(base + 0x0c);
    if (!mesh_has_valid_frames(mesh) || !mesh_vertex_buffer_is_valid(vertex_buffer)) {
        if (trace_allow_ex("CMeshAnimationController::Render/bad_input", 30, 180)) {
            const unsigned char *mesh_base = (const unsigned char *)mesh;
            const void *frames = trace_is_game_range(mesh, 0x30u) ?
                                 *(const void * const *)(const void *)(mesh_base + 0x28) : NULL;
            uint32_t frame_count = trace_is_game_range(mesh, 0x30u) ?
                                   *(const uint32_t *)(const void *)(mesh_base + 0x2c) : 0;
            const void *vb_vptr = trace_is_game_range(vertex_buffer, sizeof(void *)) ?
                                  *(const void * const *)vertex_buffer : NULL;
            sceClibPrintf("[FIX-MESH] AnimationController::Render(self=%p mesh=%p frame=%d vb=%p vb_vptr=%p) skipped bad frames=%p count=%u\n",
                          self, mesh, frame, vertex_buffer, vb_vptr,
                          frames, (unsigned int)frame_count);
        }
        return;
    }

    (void)SO_CONTINUE(int, h_mesh_animation_controller_render, self);
}

static void trace_mesh_get_interpolation_values(const void *self,
                                                int frame,
                                                int *out_a,
                                                int *out_b,
                                                float *out_t) {
    if (!mesh_has_valid_frames(self)) {
        if (out_a) {
            *out_a = 0;
        }
        if (out_b) {
            *out_b = 0;
        }
        if (out_t) {
            *out_t = 0.0f;
        }
        if (trace_allow_ex("CMesh::GetInterpolationValues/bad_frames", 24, 180)) {
            const unsigned char *base = (const unsigned char *)self;
            const void *frames = trace_is_game_range(self, 0x30u) ?
                                 *(const void * const *)(const void *)(base + 0x28) : NULL;
            uint32_t frame_count = trace_is_game_range(self, 0x30u) ?
                                   *(const uint32_t *)(const void *)(base + 0x2c) : 0;
            sceClibPrintf("[FIX-MESH] GetInterpolationValues(mesh=%p frame=%d) guarded bad frames=%p count=%u\n",
                          self, frame, frames, (unsigned int)frame_count);
        }
        return;
    }

    (void)SO_CONTINUE(int, h_mesh_get_interpolation_values, self, frame, out_a, out_b, out_t);
}

static int trace_mesh_get_nodes_count(const void *self, int frame) {
    if (!mesh_has_valid_frames(self)) {
        if (trace_allow_ex("CMesh::GetNodesCount/bad_frames", 24, 180)) {
            const unsigned char *base = (const unsigned char *)self;
            const void *frames = trace_is_game_range(self, 0x30u) ?
                                 *(const void * const *)(const void *)(base + 0x28) : NULL;
            uint32_t frame_count = trace_is_game_range(self, 0x30u) ?
                                   *(const uint32_t *)(const void *)(base + 0x2c) : 0;
            sceClibPrintf("[FIX-MESH] GetNodesCount(mesh=%p frame=%d) -> 0 guarded bad frames=%p count=%u\n",
                          self, frame, frames, (unsigned int)frame_count);
        }
        return 0;
    }

    return SO_CONTINUE(int, h_mesh_get_nodes_count, self, frame);
}

static void trace_mesh_get_node_at(const void *self, int frame, int node, void *out_node) {
    if (!mesh_has_valid_frames(self)) {
        if (trace_is_game_range(out_node, 0x1cu)) {
            memset(out_node, 0, 0x1cu);
        }
        if (trace_allow_ex("CMesh::GetNodeAt/bad_frames", 24, 180)) {
            const unsigned char *base = (const unsigned char *)self;
            const void *frames = trace_is_game_range(self, 0x30u) ?
                                 *(const void * const *)(const void *)(base + 0x28) : NULL;
            uint32_t frame_count = trace_is_game_range(self, 0x30u) ?
                                   *(const uint32_t *)(const void *)(base + 0x2c) : 0;
            sceClibPrintf("[FIX-MESH] GetNodeAt(mesh=%p frame=%d node=%d out=%p) guarded bad frames=%p count=%u\n",
                          self, frame, node, out_node, frames, (unsigned int)frame_count);
        }
        return;
    }

    (void)SO_CONTINUE(int, h_mesh_get_node_at, self, frame, node, out_node);
}

static int sprite_player_can_draw(const void *self) {
    const unsigned char *base = (const unsigned char *)self;
    const unsigned char *archetype;
    const unsigned char *animation;
    const unsigned char *frame_table;
    const unsigned char *chunk_table;
    const unsigned char *chunk_record;
    const unsigned char *layer_data;
    uintptr_t chunk_record_address;
    unsigned int frame;
    unsigned int frame_count;
    unsigned int chunk_index;
    unsigned int layer_count;

    if (!trace_is_game_range(self, 0x20u)) {
        return 0;
    }

    archetype = *(const unsigned char * const *)(const void *)(base + 0x18);
    animation = *(const unsigned char * const *)(const void *)(base + 0x1c);
    if (!trace_is_game_range(archetype, 0x20u) ||
        !trace_is_game_range(animation, 0x10u)) {
        return 0;
    }

    frame_table = *(const unsigned char * const *)(const void *)(animation + 0x04);
    chunk_table = *(const unsigned char * const *)(const void *)(archetype + 0x0c);
    frame_count = animation[0x0c];
    frame = base[0x0a];
    if (frame_count == 0 ||
        frame >= frame_count ||
        !trace_is_game_range(frame_table, (size_t)frame_count * 4u) ||
        !trace_is_game_ptr(chunk_table)) {
        return 0;
    }

    chunk_index = *(const uint16_t *)(const void *)(frame_table + frame * 4 + 2);
    chunk_record_address = (uintptr_t)chunk_table +
                           (uintptr_t)chunk_index * 8u;
    chunk_record = (const unsigned char *)(uintptr_t)chunk_record_address;
    if (!trace_is_game_range(chunk_record, 8u)) {
        return 0;
    }

    /* CSpriteIterator::SetLayer reads the pointer in the first word of this
     * record and treats each layer as a six-byte entry.  Merely validating the
     * record address allowed malformed values such as 0x01000000 through and
     * caused a data abort at SetLayer+0x44. */
    layer_data = *(const unsigned char * const *)(const void *)chunk_record;
    layer_count = chunk_record[4];
    return layer_count > 0u &&
           trace_is_game_range(layer_data, (size_t)layer_count * 6u);
}

typedef void (*sprite_player_set_byte_fn_t)(void *self, unsigned char value);
typedef void (*sprite_player_reset_fn_t)(void *self);

static sprite_player_set_byte_fn_t sprite_player_set_animation_fn(void) {
    static sprite_player_set_byte_fn_t fn;
    static int looked_up;

    if (!looked_up) {
        fn = (sprite_player_set_byte_fn_t)(uintptr_t)
            so_symbol(&so_mod, "_ZN13CSpritePlayer12SetAnimationEh");
        looked_up = 1;
    }
    return fn;
}

static sprite_player_set_byte_fn_t sprite_player_set_frame_fn(void) {
    static sprite_player_set_byte_fn_t fn;
    static int looked_up;

    if (!looked_up) {
        fn = (sprite_player_set_byte_fn_t)(uintptr_t)
            so_symbol(&so_mod, "_ZN13CSpritePlayer8SetFrameEh");
        looked_up = 1;
    }
    return fn;
}

static sprite_player_reset_fn_t sprite_player_reset_fn(void) {
    static sprite_player_reset_fn_t fn;
    static int looked_up;

    if (!looked_up) {
        fn = (sprite_player_reset_fn_t)(uintptr_t)
            so_symbol(&so_mod, "_ZN13CSpritePlayer5ResetEv");
        looked_up = 1;
    }
    return fn;
}

static int sprite_player_has_repairable_archetype(const void *self) {
    const unsigned char *base = (const unsigned char *)self;
    const unsigned char *archetype;
    const unsigned char *animation_table;
    const unsigned char *first_animation;
    const void *chunk_table;
    const void *frame_table;
    unsigned int animation_count;
    unsigned int frame_count;

    if (!trace_is_game_range(self, 0x24u)) {
        return 0;
    }

    archetype = *(const unsigned char * const *)(const void *)(base + 0x18);
    if (!trace_is_game_range(archetype, 0x20u)) {
        return 0;
    }

    chunk_table = *(const void * const *)(const void *)(archetype + 0x0c);
    animation_table = *(const unsigned char * const *)(const void *)(archetype + 0x10);
    animation_count = *(const uint16_t *)(const void *)(archetype + 0x1c);
    if (!trace_is_game_ptr(chunk_table) ||
        animation_count == 0u ||
        animation_count > 256u ||
        !trace_is_game_range(animation_table, (size_t)animation_count * 0x10u)) {
        return 0;
    }

    first_animation = animation_table;
    frame_table = *(const void * const *)(const void *)(first_animation + 0x04);
    frame_count = *(const unsigned char *)(const void *)(first_animation + 0x0c);
    return frame_count > 0u &&
           trace_is_game_range(frame_table, (size_t)frame_count * 4u);
}

static int sprite_player_repair_for_draw(void *self) {
    sprite_player_set_byte_fn_t set_animation;
    sprite_player_set_byte_fn_t set_frame;
    sprite_player_reset_fn_t reset;

    if (sprite_player_can_draw(self)) {
        return 1;
    }
    if (!sprite_player_has_repairable_archetype(self)) {
        return 0;
    }

    set_animation = sprite_player_set_animation_fn();
    if (set_animation) {
        set_animation(self, 0);
        if (sprite_player_can_draw(self)) {
            return 1;
        }
    }

    set_frame = sprite_player_set_frame_fn();
    if (set_frame) {
        set_frame(self, 0);
        if (sprite_player_can_draw(self)) {
            return 1;
        }
    }

    reset = sprite_player_reset_fn();
    if (reset) {
        reset(self);
    }
    return sprite_player_can_draw(self);
}

static void trace_sprite_player_draw_rect(void *self, const void *rect, int x, int y, int alpha) {
    typedef void (*sprite_draw_rect_fn_t)(void *, const void *, int, int, int);

    GUNBROS_PERF_COUNT(sprite_draw_calls);
    if (!sprite_player_can_draw(self)) {
        if (sprite_player_repair_for_draw(self)) {
            if (trace_allow_ex("CSpritePlayer::Draw/repaired_metadata", 24, 180)) {
                const unsigned char *base = (const unsigned char *)self;
                const void *archetype = trace_is_game_ptr(self) ? *(const void * const *)(const void *)(base + 0x18) : NULL;
                const void *animation = trace_is_game_ptr(self) ? *(const void * const *)(const void *)(base + 0x1c) : NULL;

                sceClibPrintf("[FIX-SPRITE] CSpritePlayer::Draw repaired metadata this=%p arch=%p anim=%p frame=%u\n",
                              self, archetype, animation,
                              trace_is_game_range(self, 0x0bu) ?
                              (unsigned int)*(const unsigned char *)(const void *)(base + 0x0a) : 0xffu);
            }
            if (g_original_sprite_player_draw_rect) {
                ((sprite_draw_rect_fn_t)g_original_sprite_player_draw_rect)(
                    self, rect, x, y, alpha);
            } else {
                (void)SO_CONTINUE(int, h_sprite_player_draw_rect,
                                  self, rect, x, y, alpha);
            }
            return;
        }
        if (trace_allow("CSpritePlayer::Draw/bad_metadata")) {
            const unsigned char *base = (const unsigned char *)self;
            const void *archetype = trace_is_game_range(self, 0x20u) ? *(const void * const *)(const void *)(base + 0x18) : NULL;
            const void *animation = trace_is_game_range(self, 0x20u) ? *(const void * const *)(const void *)(base + 0x1c) : NULL;

            sceClibPrintf("[PATCH-SPRITE] CSpritePlayer::Draw(this=%p, rect=%p, x=%d, y=%d, alpha=%d) skipped unsafe metadata/chunk arch=%p anim=%p frame=%u\n",
                          self, rect, x, y, alpha, archetype, animation,
                          trace_is_game_range(self, 0x0bu) ?
                          (unsigned int)base[0x0a] : 0xffu);
        }
        return;
    }

    if (g_original_sprite_player_draw_rect) {
        ((sprite_draw_rect_fn_t)g_original_sprite_player_draw_rect)(
            self, rect, x, y, alpha);
    } else {
        (void)SO_CONTINUE(int, h_sprite_player_draw_rect,
                          self, rect, x, y, alpha);
    }
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
