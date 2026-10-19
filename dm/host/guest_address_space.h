#pragma once
#include <stddef.h>
#include <stdint.h>

#define DM_GUEST_AS_BYTES (UINT64_C(5) << 30)
#define DM_GUEST_HEAP_OFFSET (UINT64_C(1) << 30)
#define DM_GUEST_HEAP_BYTES (UINT64_C(4) << 30)

// Startup-only, externally serialized layout around an existing RW mapped
// allocation. No guest pointers may be published and no GPU/dirty tracker may
// use the allocation until reserve succeeds. The mapped owner must outlive this
// layout. raw_map/raw_size include ALL its head/tail padding and must be OS-page
// aligned; slice must be inside them. No driver page is mmap'ed or munmap'ed.
// Anonymous prefix/tail are the only reservations owned by this object.
typedef struct {
  uint8_t *base, *heap, *raw_map;
  size_t raw_size, slice_heap_offset, slice_size, opened;
  uint8_t *prefix, *tail;
  size_t prefix_size, tail_size;
  int raw_protection_changed, ready;
} dm_guest_as;

// out must be zero-initialized. Returns 0 or a POSIX error number. All
// sizes/offsets are page aligned. The
// explicit slice_heap_offset is geometry, never a game-address/size classifier.
// initial_opened is the initial logical heap prefix. Only its intersection
// with raw_map is explicitly zeroed; anonymous pages retain lazy-zero backing.
// Flush/invalidate and GPU ownership remain the mapped owner's responsibility.
// Closed raw pages become PROT_NONE. Original raw contents outside the opened
// prefix are retained. On failure no layout is ready; out normally stays zero.
// If a rollback syscall itself fails, out retains cleanup ownership (ready=0)
// and release must be retried; its base MUST NOT be published as a guest AS.
int dm_guest_as_reserve(dm_guest_as *out, void *raw_map, size_t raw_size,
                        void *slice, size_t slice_size,
                        size_t slice_heap_offset, size_t initial_opened);
// No implicit GPU wait/cache operation. Caller has retired all guest/GPU/dirty
// references. A failed release retains cleanup ownership with ready=0.
// Restore the driver's original RW permissions, release only our
// anonymous segments, never unmap the driver mapping. Idempotent when empty.
int dm_guest_as_release(dm_guest_as *space);
