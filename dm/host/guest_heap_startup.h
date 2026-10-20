#pragma once
#include "dm_guest_heap.h"
// Caller supplies the existing fixed-mode query only after all device/provider,
// window and logging dependencies are ready. No probe in the default OFF path.
// Unsupported mode fixes the entire launch to original private/legacy before
// any SVC. Once selected there is no later promotion, remap or per-Pool demotion.
static inline int dm_guest_heap_startup_freeze(int requested, int (*probe)(void)) {
    if (requested != 0 && requested != 1) return -1;
    int actual = requested && probe && probe();
    return dm_guest_heap_backing_select(actual) ? actual : -1;
}
