// A recording-local value shadow, not a resource or replay-state cache.
// It proves only that this same continuous command stream already binds the
// exact bytes for one of the six independent fixed pipeline-state setters.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct nvn_record_state_cache {
    uint8_t valid, sizes[6];
    uint8_t values[6][24];
} nvn_record_state_cache;

static inline void nvn_record_state_clear(nvn_record_state_cache *cache) {
    if (cache) cache->valid = 0;
}
static inline int nvn_record_state_matches(const nvn_record_state_cache *cache,
                                          unsigned kind, const void *value, size_t size) {
    return cache && kind < 6 && size && size <= 24 &&
        (cache->valid & (1u << kind)) && cache->sizes[kind] == size &&
        !memcmp(cache->values[kind], value, size);
}
static inline void nvn_record_state_capture(nvn_record_state_cache *cache,
                                           unsigned kind, const void *value, size_t size) {
    if (!cache) return;
    if (kind >= 6 || !size || size > 24) {
        nvn_record_state_clear(cache); // unknown state has no reusable proof
        return;
    }
    memcpy(cache->values[kind], value, size);
    cache->sizes[kind] = (uint8_t)size;
    cache->valid |= 1u << kind;
}
