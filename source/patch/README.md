# Patch categories

`source/patch.c` is intentionally only a small composition unit. It includes
the category files below so existing `static` hook state and call ordering are
preserved without keeping patch implementations in the root file.

- `menus.inc.c`: splash, main-menu handoff, menu stack/system, and menu resource loading.
- `gameplay.inc.c`: player/brother lifecycle, controls, shooting, swapping, and level safety.
- `shops.inc.c`: store queries, offline purchases, currency, buy/equip, and item status.
- `objects.inc.c`: game-object registry, profile, progress, mission, and object-pack state.
- `rendering.inc.c`: mesh, sprite, animation, and render-metadata safety.
- `graphics.inc.c`: texture and graphics-instruction stream handling.
- `network.inc.c`: offline CNGS/GameSpy behavior and keepalive guards.
- `bootstrap_guards.inc.c`: early brother, resolver, binding, and graphics guards.
- `shared.inc.c`: shared hook state, pointer validation, and diagnostics.
- `hook_helpers.inc.c`: symbol-hook and executable patch-cave helpers.
- `install.inc.c`: hook registration and ordered patch installation.

Only these ordered includes and the common platform headers stay in
`source/patch.c`.

The latest reference comparison, direct ARM-site verification, and complete
installed-hook inventory are recorded in [PATCH_FUNCTION_AUDIT.md](../../docs/PATCH_FUNCTION_AUDIT.md).
