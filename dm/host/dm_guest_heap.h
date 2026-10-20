#pragma once
#include <stddef.h>
#include <stdint.h>

// Cold startup only, before the first SVC/guest AS publication. Returns 0 on
// a late/conflicting request. Does not initialize Vulkan or change an existing AS.
int dm_guest_heap_backing_select(int shared);
// Shared backing provenance of an entire committed, nonempty heap range.
// Not an allocation/pool identity, lifetime pin, CPU visibility or GPU sync proof.
int dm_guest_shared_heap_contains(uintptr_t address, size_t bytes);

// -1 before AS publication, 0 private/legacy, 1 shared; read-only metadata.
int dm_guest_heap_shared_mode(void);
