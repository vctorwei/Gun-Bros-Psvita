#ifndef GUNBROS_NATIVE_REFINERY_H
#define GUNBROS_NATIVE_REFINERY_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Verified against CRefinementSlot::Commit/GetCurrentYield/Update/Collect. */
typedef struct GunBrosRefinerySlot {
    uint32_t state;
    float efficiency;
    uint32_t remaining_ms;
    uint32_t started_seconds;
    uint32_t total_ms;
    uint32_t padding;
    uint64_t amount;
} GunBrosRefinerySlot;
_Static_assert(sizeof(GunBrosRefinerySlot) == 0x20, "native refinery slot size");
_Static_assert(offsetof(GunBrosRefinerySlot, efficiency) == 4, "native yield offset");
_Static_assert(offsetof(GunBrosRefinerySlot, amount) == 0x18, "native amount offset");

static int gunbros_refinery_slot_valid(const void *bytes, uint32_t duration) {
    GunBrosRefinerySlot slot;
    memcpy(&slot, bytes, sizeof(slot));
    if (slot.state > 3u) return 0;
    if (slot.state < 2u) return 1;
    if (!slot.amount || slot.total_ms != duration || slot.total_ms > 604800000u)
        return 0;
    if (slot.state == 3u) return slot.remaining_ms == 0u;
    return slot.total_ms != 0u && slot.remaining_ms != 0u &&
           slot.remaining_ms <= slot.total_ms;
}
#endif
