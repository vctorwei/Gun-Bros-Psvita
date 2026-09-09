#ifndef GUNBROS_NATIVE_TOUCH_H
#define GUNBROS_NATIVE_TOUCH_H

#include <stdint.h>
#include <string.h>

/* ARM CInput::OnTouch (0x266554), OnTouchMove (0x2666e0), and
 * OnTouchRelease (0x2666a4). Preserve their record/state layout, but use
 * the platform contact ID instead of nearest-coordinate matching. */
static void gunbros_native_touch(void *input, int x, int y, int id, int action) {
    unsigned char *base = (unsigned char *)input;
    int32_t *count = (int32_t *)(void *)(base + 0x3b0);
    int32_t *record = NULL;
    int i;
    if (*count < 0 || *count > 30) return;
    for (i = 0; i < *count; ++i) {
        int32_t *candidate = (int32_t *)(void *)(base + 0x68 + i * 0x1c);
        if (candidate[6] == id && candidate[4] != 3) {
            record = candidate;
            break;
        }
    }
    *(int32_t *)(void *)(base + 0x3cc) = id;
    if (action == 1) {
        if (record || *count == 30) return;
        record = (int32_t *)(void *)(base + 0x68 + (*count)++ * 0x1c);
        record[0] = record[2] = x;
        record[1] = record[3] = y;
        record[4] = 1;
        record[5] = 0;
        record[6] = id;
        /* Retain the native sequence counter for other observers. */
        ++*(uint32_t *)(void *)(base + 0x3b4);
    } else if (record) {
        record[2] = record[0];
        record[3] = record[1];
        record[0] = x;
        record[1] = y;
        if (action == 3) record[4] = 3;
        else record[5] = 0;
    }
}
#endif
