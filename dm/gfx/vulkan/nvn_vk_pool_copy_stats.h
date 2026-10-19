// Optional logical work accounting, not a timer, sampler, DRAM or RSS report.
#pragma once
#include <stdint.h>
#define VK_POOL_COPY_FIELDS(X) \
  X(scan_bytes) \
  X(fallback_compare_span_bytes) \
  X(reference_page_span_bytes) \
  X(initial_mirror_bytes) \
  X(initial_shadow_bytes) \
  X(mirror_bytes) \
  X(shadow_bytes) \
  X(snapshot_bytes) \
  X(gpu_upload_recorded_bytes) \
  X(known_snapshot_bytes) \
  X(readback_guest_completed_bytes) \
  X(readback_shadow_completed_bytes) \
  X(known_guest_completed_bytes) \
  X(known_shadow_completed_bytes) \
  X(migration_gpu_recorded_bytes) \
  X(host_flush_bytes) \
  X(host_flush_calls)
typedef struct {
  int enabled;
#define VK_POOL_COPY_MEMBER(name) uint64_t name;
  VK_POOL_COPY_FIELDS(VK_POOL_COPY_MEMBER)
#undef VK_POOL_COPY_MEMBER
} vk_pool_copy_stats;
// Fields are independently atomic cumulative snapshots, not a transaction.
void vk_pool_copy_snapshot(vk_pool_copy_stats *out);
