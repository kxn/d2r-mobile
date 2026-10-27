// A recording-local value shadow, not a resource or replay-state cache.
// It proves only that this same continuous command stream already binds the
// exact bytes for an independent state setter. Extra stencil/layout shadows
// have a separate cold opt-in, so the qualified six-state path stays distinct.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct nvn_record_state_cache {
    uint8_t valid, sizes[6];
    uint8_t values[6][24];
    uint8_t extra_valid, extra_enabled, attribute_count, stream_count;
    struct { int32_t face, value; } stencil[3];
    uint8_t attributes[16 * 4], streams[16 * 8];
} nvn_record_state_cache;

static inline void nvn_record_state_clear(nvn_record_state_cache *cache) {
    if (cache) cache->valid = cache->extra_valid = 0;
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

// Each which has only ONE exact full-face proof. FRONT -> BACK -> FRONT
// always misses the last FRONT, even if it might have the same final value.
// No partial-face merging, inference or relationship to depth/stencil state.
static inline int nvn_record_stencil_matches(const nvn_record_state_cache *cache,
                                             unsigned which, int32_t face, int32_t value) {
    if (!cache || !cache->extra_enabled || which >= 3 ||
        !(cache->extra_valid & (1u << which))) return 0;
    const int32_t words[2] = {face, value};
    return !memcmp(&cache->stencil[which], words, sizeof words);
}
static inline void nvn_record_stencil_capture(nvn_record_state_cache *cache,
                                              unsigned which, int32_t face, int32_t value) {
    if (!cache || !cache->extra_enabled) return;
    if (which >= 3) { cache->extra_valid = 0; return; }
    cache->stencil[which].face = face; cache->stencil[which].value = value;
    cache->extra_valid |= 1u << which;
}
// Attribute and stream setters replace only [0,n). The exact previous n is
// part of this proof: never infer preserved tail state or compare a whole
// maximum array for a short prefix. Empty setters keep their original record.
static inline int nvn_record_layout_matches(const nvn_record_state_cache *cache,
                                            int stream, unsigned n, const void *value) {
    unsigned bit = 1u << (stream ? 4 : 3);
    if (!cache || !cache->extra_enabled || !n || n > 16 ||
        !(cache->extra_valid & bit) ||
        (stream ? cache->stream_count : cache->attribute_count) != n) return 0;
    return !memcmp(stream ? cache->streams : cache->attributes, value,
                   (size_t)n * (stream ? 8 : 4));
}
static inline void nvn_record_layout_capture(nvn_record_state_cache *cache,
                                             int stream, unsigned n, const void *value) {
    if (!cache || !cache->extra_enabled) return;
    unsigned bit = 1u << (stream ? 4 : 3);
    if (!n || n > 16) { cache->extra_valid &= ~bit; return; }
    memcpy(stream ? cache->streams : cache->attributes, value, (size_t)n * (stream ? 8 : 4));
    if (stream) cache->stream_count = (uint8_t)n;
    else cache->attribute_count = (uint8_t)n;
    cache->extra_valid |= bit;
}
