/* Graphics instruction stream and texture-record patches. */

typedef struct TraceCalledTexture {
    const void *surface;
    uint32_t gl_name;
    uint32_t texture_index;
} TraceCalledTexture;

typedef struct TraceTextureInstructionRecord {
    const unsigned char *surface;
    const uint32_t *texture_table;
    uint32_t texture_unit;
    uint32_t texture_index;
    uint32_t gl_name;
    int texture_count;
    int table_backed;
} TraceTextureInstructionRecord;

enum {
    TRACE_VITAGL_TEXTURE_UNITS = 16u,
    TRACE_VITAGL_TEXTURE_SLOTS = 16384u,
    TRACE_TEXTURE_SURFACE_COUNT_OFF = 0x1cu,
    TRACE_TEXTURE_SURFACE_STORAGE_OFF = 0x20u
};

static TraceCalledTexture g_trace_called_textures[128];
static unsigned int g_trace_called_texture_count;

static int trace_texture_instr_resolve(
    const unsigned char *record,
    TraceTextureInstructionRecord *out) {
    TraceTextureInstructionRecord resolved;
    uintptr_t storage;

    memset(&resolved, 0, sizeof(resolved));
    resolved.texture_unit = *(const uint32_t *)(const void *)record;
    resolved.surface = *(const unsigned char * const *)(const void *)(record + 4u);
    resolved.texture_index = *(const uint32_t *)(const void *)(record + 8u);

    if (resolved.texture_unit >= TRACE_VITAGL_TEXTURE_UNITS ||
        !trace_is_game_range(resolved.surface,
                             TRACE_TEXTURE_SURFACE_STORAGE_OFF + sizeof(uint32_t))) {
        if (out) {
            *out = resolved;
        }
        return 0;
    }

    resolved.texture_count = *(const int *)(const void *)
        (resolved.surface + TRACE_TEXTURE_SURFACE_COUNT_OFF);
    if (resolved.texture_count <= 0 || resolved.texture_count > 4096) {
        if (out) {
            *out = resolved;
        }
        return 0;
    }

    storage = (uintptr_t)*(const uint32_t *)(const void *)
        (resolved.surface + TRACE_TEXTURE_SURFACE_STORAGE_OFF);
    if (resolved.texture_count == 1) {
        resolved.gl_name = (uint32_t)storage;
    } else {
        resolved.table_backed = 1;
        resolved.texture_table = (const uint32_t *)storage;
        if (resolved.texture_index >= (uint32_t)resolved.texture_count ||
            !trace_is_game_range(resolved.texture_table,
                                 (size_t)resolved.texture_count * sizeof(uint32_t))) {
            if (out) {
                *out = resolved;
            }
            return 0;
        }
        resolved.gl_name = resolved.texture_table[resolved.texture_index];
    }

    if (resolved.gl_name >= TRACE_VITAGL_TEXTURE_SLOTS) {
        if (out) {
            *out = resolved;
        }
        return 0;
    }

    if (out) {
        *out = resolved;
    }
    return 1;
}

static void trace_called_texture(void *owner,
                                 const unsigned char *stream,
                                 unsigned int record_index,
                                 const TraceTextureInstructionRecord *resolved,
                                 const void *caller) {
#ifdef GUNBROS_QUIET_LOGS
    (void)owner;
    (void)stream;
    (void)record_index;
    (void)resolved;
    (void)caller;
    return;
#else
    unsigned int i;
    int is_new = 1;

    if (!resolved) {
        return;
    }

    for (i = 0; i < g_trace_called_texture_count; ++i) {
        if (g_trace_called_textures[i].surface == resolved->surface &&
            g_trace_called_textures[i].gl_name == resolved->gl_name &&
            g_trace_called_textures[i].texture_index == resolved->texture_index) {
            is_new = 0;
            break;
        }
    }

    if (!is_new) {
        return;
    }
    if (g_trace_called_texture_count <
        sizeof(g_trace_called_textures) / sizeof(g_trace_called_textures[0])) {
        TraceCalledTexture *seen =
            &g_trace_called_textures[g_trace_called_texture_count++];
        seen->surface = resolved->surface;
        seen->gl_name = resolved->gl_name;
        seen->texture_index = resolved->texture_index;
    }

    if (trace_allow_ex("InstrTexure/called-texture", 128, 600)) {
        sceClibPrintf("[DEBUG-TEXTURE] called texture owner=%p stream=%p record=%u caller=%p game+0x%08x unit=%u surface=%p tex_index=%u count=%d storage=%s table=%p gl_name=%u\n",
                      owner, stream, record_index, caller,
                      game_text_offset_or_invalid(caller),
                      (unsigned int)resolved->texture_unit,
                      resolved->surface,
                      (unsigned int)resolved->texture_index,
                      resolved->texture_count,
                      resolved->table_backed ? "table" : "direct",
                      resolved->texture_table,
                      (unsigned int)resolved->gl_name);
    }
#endif
}

static int trace_graphics_instr_texture(void *self, unsigned char *stream) {
    uint32_t count;
    uint32_t read_index;
    uint32_t write_index = 0;
    int dropped = 0;
    const void *caller = __builtin_return_address(0);

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
        TraceTextureInstructionRecord resolved;

        if (!trace_texture_instr_resolve(record, &resolved)) {
            if (trace_allow_ex("InstrTexure/wait_for_source_surface", 32, 120)) {
                sceClibPrintf("[FIX-GFX] InstrTexure deferred invalid source record owner=%p stream=%p index=%u caller=%p game+0x%08x unit=%u surface=%p tex_index=%u count=%d storage=%s table=%p gl_name=%u\n",
                              self, stream, (unsigned int)read_index,
                              caller, game_text_offset_or_invalid(caller),
                              (unsigned int)resolved.texture_unit,
                              resolved.surface,
                              (unsigned int)resolved.texture_index,
                              resolved.texture_count,
                              resolved.table_backed ? "table" : "direct",
                              resolved.texture_table,
                              (unsigned int)resolved.gl_name);
            }
            dropped = 1;
            continue;
        }

        trace_called_texture(self, stream, read_index, &resolved, caller);

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
