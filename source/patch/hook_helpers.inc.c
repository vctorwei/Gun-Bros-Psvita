/* Shared symbol-hook and executable patch-cave helpers. */

static void hook_symbol(const char *label, const char *symbol, uintptr_t replacement) {
    uintptr_t addr = so_symbol(&so_mod, symbol);
    int log = trace_label_is_focused(label);

    if (!addr) {
        if (log) {
            sceClibPrintf("[PATCH-CNGS] missing %s (%s)\n",
                          label ? label : "(null)",
                          symbol ? symbol : "(null)");
        }
        return;
    }

    hook_addr(addr, replacement);
    if (log) {
        sceClibPrintf("[PATCH-CNGS] hook %s at %p -> %p\n",
                      label ? label : "(null)",
                      (void *)addr,
                      (void *)replacement);
    }
}

static void hook_symbol_store(const char *label, const char *symbol, uintptr_t replacement, so_hook *out) {
    uintptr_t addr = so_symbol(&so_mod, symbol);
    int log = trace_label_is_focused(label);

    if (!addr) {
        if (log) {
            sceClibPrintf("[TRACE-HOOK] missing %s (%s)\n",
                          label ? label : "(null)",
                          symbol ? symbol : "(null)");
        }
        return;
    }

    *out = hook_addr(addr, replacement);
    if (log) {
        sceClibPrintf("[TRACE-HOOK] hook %s at %p -> %p\n",
                      label ? label : "(null)",
                      (void *)addr,
                      (void *)replacement);
    }
}

static void patch_store_stat_format_conversion(void) {
    uintptr_t fn = so_symbol(&so_mod,
        "_ZN16CStoreAggregator23SubstituteStatsInStringEP10CStoreItemPN3com3glu8platform10components9CStrWCharEh");
    uint32_t words[2];
    const uint32_t replacements[2] = {
        0xe1b07003u, /* movs r7, r3: preserve the signed-value branch */
        0xe1a07007u  /* mov r7, r7: preserve the unscaled stat */
    };
    if (!fn || (fn & 1u)) return;
    memcpy(&words[0], (const void *)(fn + 0x26cu), 4);
    memcpy(&words[1], (const void *)(fn + 0x36cu), 4);
    /* v3.1.4: two stat-token formatter paths shift by three. The separate
     * CStoreItem::Init price conversion at 0x307cac remains untouched. */
    if (words[0] != 0xe1b07183u || words[1] != 0xe1a07187u) {
        sceClibPrintf("[PATCH-STATS] unsupported formatter instructions\n");
        return;
    }
    kuKernelCpuUnrestrictedMemcpy((void *)(fn + 0x26cu), &replacements[0], 4);
    kuKernelCpuUnrestrictedMemcpy((void *)(fn + 0x36cu), &replacements[1], 4);
    kuKernelFlushCaches((void *)(fn + 0x26cu), 0x104u);
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

static uintptr_t hook_symbol_store_arm_trampoline_checked(
    const char *label, const char *symbol, uintptr_t replacement,
    so_hook *out, uint32_t expected_first, uint32_t expected_second) {
    uintptr_t addr = so_symbol(&so_mod, symbol);
    uintptr_t cave;
    uint32_t original[2];
    uint32_t trampoline[4];

    if (!addr || !out) {
        GUNBROS_PERF_LOG("[PERF-PATCH] trampoline missing label=%s symbol=%s addr=%p\n",
                         label ? label : "?", symbol ? symbol : "?",
                         (void *)addr);
        return 0;
    }

    kuKernelCpuUnrestrictedMemcpy(original, (const void *)addr,
                                  sizeof(original));
    if ((addr & 1u) != 0u ||
        original[0] != expected_first ||
        original[1] != expected_second) {
        *out = hook_addr(addr, replacement);
        GUNBROS_PERF_LOG("[PERF-PATCH] trampoline fallback label=%s addr=%p words=%08x,%08x expected=%08x,%08x\n",
                         label ? label : "?", (void *)addr,
                         original[0], original[1],
                         expected_first, expected_second);
        return 0;
    }

    cave = alloc_patch_cave(sizeof(trampoline));
    *out = hook_addr(addr, replacement);
    if (!cave) {
        GUNBROS_PERF_LOG("[PERF-PATCH] trampoline no-space label=%s addr=%p\n",
                         label ? label : "?", (void *)addr);
        return 0;
    }

    trampoline[0] = out->orig_instr[0];
    trampoline[1] = out->orig_instr[1];
    trampoline[2] = 0xe51ff004u; /* ldr pc, [pc, #-4] */
    trampoline[3] = (uint32_t)(addr + sizeof(out->orig_instr));
    kuKernelCpuUnrestrictedMemcpy((void *)cave, trampoline,
                                  sizeof(trampoline));
    kuKernelFlushCaches((void *)cave, sizeof(trampoline));
    GUNBROS_PERF_LOG("[PERF-PATCH] trampoline ready label=%s target=%p original=%p\n",
                     label ? label : "?", (void *)addr, (void *)cave);
    return cave;
}

static uintptr_t hook_symbol_store_arm_literal_trampoline_checked(
    const char *label, const char *symbol, uintptr_t replacement,
    so_hook *out, uint32_t expected_first, uint32_t expected_second) {
    uintptr_t addr = so_symbol(&so_mod, symbol);
    uintptr_t cave;
    uintptr_t source_literal;
    uint32_t original[2];
    uint32_t literal_value;
    uint32_t trampoline[5];
    uint32_t literal_offset;

    if (!addr || !out) {
        GUNBROS_PERF_LOG("[PERF-PATCH] literal trampoline missing label=%s symbol=%s addr=%p\n",
                         label ? label : "?", symbol ? symbol : "?",
                         (void *)addr);
        return 0;
    }

    kuKernelCpuUnrestrictedMemcpy(original, (const void *)addr,
                                  sizeof(original));
    if ((addr & 1u) != 0u || original[0] != expected_first ||
        original[1] != expected_second ||
        (original[1] & 0x0f7f0000u) != 0x051f0000u) {
        *out = hook_addr(addr, replacement);
        GUNBROS_PERF_LOG("[PERF-PATCH] literal trampoline fallback label=%s addr=%p words=%08x,%08x expected=%08x,%08x\n",
                         label ? label : "?", (void *)addr,
                         original[0], original[1],
                         expected_first, expected_second);
        return 0;
    }

    literal_offset = original[1] & 0x0fffu;
    source_literal = addr + 12u;
    if (original[1] & 0x00800000u) {
        source_literal += literal_offset;
    } else {
        source_literal -= literal_offset;
    }
    kuKernelCpuUnrestrictedMemcpy(&literal_value,
                                  (const void *)source_literal,
                                  sizeof(literal_value));

    cave = alloc_patch_cave(sizeof(trampoline));
    *out = hook_addr(addr, replacement);
    if (!cave) {
        GUNBROS_PERF_LOG("[PERF-PATCH] literal trampoline no-space label=%s addr=%p\n",
                         label ? label : "?", (void *)addr);
        return 0;
    }

    trampoline[0] = out->orig_instr[0];
    /* At cave+4, ARM's PC is cave+12; the copied literal is at cave+16. */
    trampoline[1] = (out->orig_instr[1] & 0xff7ff000u) |
                    0x00800004u;
    trampoline[2] = 0xe51ff004u; /* ldr pc, [pc, #-4] */
    trampoline[3] = (uint32_t)(addr + sizeof(out->orig_instr));
    trampoline[4] = literal_value;
    kuKernelCpuUnrestrictedMemcpy((void *)cave, trampoline,
                                  sizeof(trampoline));
    kuKernelFlushCaches((void *)cave, sizeof(trampoline));
    GUNBROS_PERF_LOG("[PERF-PATCH] literal trampoline ready label=%s target=%p original=%p literal=%p\n",
                     label ? label : "?", (void *)addr, (void *)cave,
                     (void *)source_literal);
    return cave;
}
