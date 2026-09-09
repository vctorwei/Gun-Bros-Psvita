/* Early graphics, brother binding, resolver, and spawn guards. */

static void patch_gunbros_unflatten_null_self_inline(void) {
    static const char unflatten_symbol[] =
        "_ZNK8CGunBros20UnFlattenObjectIndexE14GameObjectTypetRtRh";
    uintptr_t unflatten = so_symbol(&so_mod, unflatten_symbol);
    uintptr_t patch_at;
    uintptr_t resume_at;
    uintptr_t failure_at;
    uintptr_t cave;
    uint32_t code[8];
    uint32_t first;
    uint32_t second;

    if (!unflatten) {
        sceClibPrintf("[PATCH] CGunBros::UnFlattenObjectIndex symbol not found\n");
        return;
    }

    /* Original ARM body:
     *   +0x08 strh ip, [r3]       ; initialize *out_index = 0
     *   +0x0c ldr  ip, [r0,#13c] ; crashes when self is NULL
     *   +0x10 cmp  ip, #0
     *   +0x14 beq  failure
     *
     * Guard only the faulting load. A null self goes to the function's own
     * false-return epilogue at +0x6c, preserving its stack frame and ABI.
     * A valid self replays both overwritten instructions and resumes at the
     * original count check. This avoids a whole-function SO_CONTINUE return
     * chain, which the coredump repeatedly showed returning to 0x98360214. */
    patch_at = unflatten + 0x0cu;
    resume_at = unflatten + 0x14u;
    failure_at = unflatten + 0x6cu;
    first = *(volatile uint32_t *)patch_at;
    second = *(volatile uint32_t *)(patch_at + 4u);
    if (first != 0xe590c13cu || second != 0xe35c0000u) {
        sceClibPrintf("[PATCH] CGunBros::UnFlattenObjectIndex inline guard skipped; unexpected words at %p: %08x %08x\n",
                      (void *)patch_at, first, second);
        return;
    }

    cave = alloc_patch_cave(sizeof(code));
    if (!cave) {
        return;
    }

    code[0] = 0xe3500000u;       /* cmp r0, #0 */
    code[1] = 0x0a000003u;       /* beq null_self */
    code[2] = 0xe590c13cu;       /* ldr ip, [r0, #0x13c] */
    code[3] = 0xe35c0000u;       /* cmp ip, #0 */
    code[4] = 0xe51ff004u;       /* ldr pc, [pc, #-4] */
    code[5] = (uint32_t)resume_at;
    code[6] = 0xe51ff004u;       /* null_self: ldr pc, [pc, #-4] */
    code[7] = (uint32_t)failure_at;

    kuKernelCpuUnrestrictedMemcpy((void *)cave, code, sizeof(code));
    kuKernelFlushCaches((void *)cave, sizeof(code));
    hook_arm(patch_at, cave);
    kuKernelFlushCaches((void *)patch_at, 8u);

    sceClibPrintf("[PATCH] CGunBros::UnFlattenObjectIndex inline null-self guard at %p -> %p\n",
                  (void *)patch_at, (void *)cave);
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
    uintptr_t i2f = so_symbol(&so_mod, "__aeabi_i2f");
    uintptr_t patch_at;
    uintptr_t resume_at;
    uintptr_t cave;
    uint32_t table_code[8];
    uint32_t delta_code[6];
    uint32_t clear_code[11];

    if (!bind || !i2f) {
        sceClibPrintf("[PATCH] CBrother::Bind progress guard symbols missing bind=%p i2f=%p\n",
                      (void *)bind, (void *)i2f);
        return;
    }

    if (*(volatile uint32_t *)(bind + 0x90) != 0xe19200fau ||
        (*(volatile uint32_t *)(bind + 0x94) & 0xff000000u) != 0xeb000000u) {
        sceClibPrintf("[PATCH] CBrother::Bind progress guard unexpected words at %p: %08x %08x; skipped\n",
                      (void *)(bind + 0x90),
                      *(volatile uint32_t *)(bind + 0x90),
                      *(volatile uint32_t *)(bind + 0x94));
        return;
    }

    patch_at = bind + 0x90;   /* ldrsh r0, [r2, sl] */
    resume_at = bind + 0x98;  /* first instruction after the 8-byte hook */
    cave = alloc_patch_cave(sizeof(table_code));
    if (!cave) {
        return;
    }

    /* hook_arm replaces both +0x90 and +0x94. Replay the table read and the
     * displaced __aeabi_i2f call in the cave, then resume at +0x98. The old
     * trampoline resumed at +0x94, which is the hook's destination literal,
     * and executed that address as an ARM instruction. */
    table_code[0] = 0xe3520000;          /* cmp r2, #0 */
    table_code[1] = 0x03a00064;          /* moveq r0, #100 */
    table_code[2] = 0x119200fa;          /* ldrshne r0, [r2, sl] */
    table_code[3] = 0xe59fc004;          /* ldr ip, [pc, #4] -> i2f */
    table_code[4] = 0xe12fff3c;          /* blx ip */
    table_code[5] = 0xe59ff000;          /* ldr pc, [pc] -> resume */
    table_code[6] = (uint32_t)i2f;
    table_code[7] = (uint32_t)resume_at;

    kuKernelCpuUnrestrictedMemcpy((void *)cave, table_code, sizeof(table_code));
    kuKernelFlushCaches((void *)cave, sizeof(table_code));
    hook_arm(patch_at, cave);

    sceClibPrintf("[PATCH] CBrother::Bind missing progress table guard at %p -> %p resume=%p\n",
                  (void *)patch_at, (void *)cave, (void *)resume_at);

    if (*(volatile uint32_t *)(bind + 0x114) != 0xe7912002u ||
        *(volatile uint32_t *)(bind + 0x118) != 0xe3a03e73u) {
        sceClibPrintf("[PATCH] CBrother::Bind XP delta guard unexpected words at %p: %08x %08x; skipped\n",
                      (void *)(bind + 0x114),
                      *(volatile uint32_t *)(bind + 0x114),
                      *(volatile uint32_t *)(bind + 0x118));
        return;
    }

    patch_at = bind + 0x114;   /* ldr r2, [r1, r2] */
    resume_at = bind + 0x11c;  /* first instruction after the 8-byte hook */
    cave = alloc_patch_cave(sizeof(delta_code));
    if (!cave) {
        return;
    }

    /* +0x118 is also displaced by hook_arm, so replay its mov r3,#0x730 in
     * the cave and continue at +0x11c. This is the exact bad resume exposed
     * by the coredump PC (CBrother::Bind+0x118). */
    delta_code[0] = 0xe3510000;          /* cmp r1, #0 */
    delta_code[1] = 0x03a02064;          /* moveq r2, #100 */
    delta_code[2] = 0x17912002;          /* ldrne r2, [r1, r2] */
    delta_code[3] = 0xe3a03e73;          /* mov r3, #0x730 */
    delta_code[4] = 0xe51ff004;          /* ldr pc, [pc, #-4] */
    delta_code[5] = (uint32_t)resume_at;

    kuKernelCpuUnrestrictedMemcpy((void *)cave, delta_code, sizeof(delta_code));
    kuKernelFlushCaches((void *)cave, sizeof(delta_code));
    hook_arm(patch_at, cave);

    sceClibPrintf("[PATCH] CBrother::Bind missing XP delta table guard at %p -> %p resume=%p\n",
                  (void *)patch_at, (void *)cave, (void *)resume_at);

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
