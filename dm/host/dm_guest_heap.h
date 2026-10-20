#pragma once
#include <stddef.h>
#include <stdint.h>

// Cold startup only, before the first SVC/guest AS publication. Returns 0 on
// a late/conflicting request. Does not initialize Vulkan or change an existing AS.
int dm_guest_heap_backing_select(int shared);
// Shared backing provenance of an entire committed, nonempty heap range.
// Not an allocation/pool identity, lifetime pin, CPU visibility or GPU sync proof.
int dm_guest_shared_heap_contains(uintptr_t address, size_t bytes);
// RW view of the same shared backing, for host-pointer GPU registration only.
// The guest view may stay write-protected for image tracking. Returns NULL for
// uncommitted/non-shared/out-of-range memory; not a lifetime or visibility pin.
void *dm_guest_shared_heap_import_pointer(uintptr_t address, size_t bytes);
typedef struct dm_guest_heap_probe_views {
    void *guest, *import_alias;
    size_t bytes;
} dm_guest_heap_probe_views;
// Owned finite startup test of the same backing class, before AS publication.
// Does not select a mode or mutate/publish the real heap's alias.
int dm_guest_heap_probe_views_create(size_t bytes, size_t alignment, dm_guest_heap_probe_views *out);
void dm_guest_heap_probe_views_destroy(dm_guest_heap_probe_views *views);

// -1 before AS publication, 0 private/legacy, 1 shared; read-only metadata.
int dm_guest_heap_shared_mode(void);
