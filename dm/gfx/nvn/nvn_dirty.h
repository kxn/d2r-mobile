// 客体内存写保护脏页跟踪（nvn_dirty.c）
// Write-protect dirty-page tracking for guest memory (nvn_dirty.c)
#pragma once
#include <stddef.h>
#include <stdint.h>

void nvn_dirty_add_range(const void *mem, size_t size);           // 登记一个内存池为跟踪区间（首次调用时注册缺页钩子与宿主写前钩子） | Register a memory pool as a tracked range (the first call registers the page-fault hook and the host pre-write hook)
void nvn_dirty_stat(int *live, int *peak, int *cap);                // 在册的内存池数、峰值、跟踪表容量（内存读数行用） | number of registered memory pools, peak, tracking table capacity (for the memory readout line)
void nvn_dirty_remove_range(const void *mem);                     // 池销毁：受保护页还原可写并标失效 | Pool destruction: restore protected pages to writable and mark invalid
int nvn_dirty_tracked(const void *addr, size_t len);
void nvn_dirty_protect(const void *addr, size_t len);              // 覆盖页设只读 | Set the covered pages read-only
uint64_t nvn_dirty_now(void);                                      // 当前代号：上传前取，作为该份内容的戳 | Current generation: taken before upload, as the stamp of that content
// Invalidates cached negative dirty tests on every published write or range
// lifetime change. Returns 0 permanently on completion-sequence saturation;
// callers must not cache that key. NOT an upload stamp or page-gen replacement.
uint64_t nvn_dirty_change_epoch(void);
// Opaque negative-test proof for one tracked envelope. Never an upload stamp.
// Observe is cold/trylock; refresh reads atomics only and may return a newer
// completion sequence in the SAME lifetime. Reuse only for the original immutable
// addr/len envelope; this is not a CPU/GPU authority proof. Capture before dirty_since.
typedef struct { uint32_t slot, reserved; uint64_t lifetime, changes; } nvn_dirty_range_proof;
int nvn_dirty_range_observe(const void *addr, size_t len, nvn_dirty_range_proof *proof);
int nvn_dirty_range_refresh(const nvn_dirty_range_proof *old, nvn_dirty_range_proof *proof);
#ifdef DM_DIRTY_RANGE_TEST_HOOKS
typedef void (*nvn_dirty_test_hook)(const void *, uint64_t, void *);
void nvn_dirty_test_publication_hook(nvn_dirty_test_hook hook, void *context);
void nvn_dirty_test_poison_range(const void *addr, int lifetime);
uint64_t nvn_dirty_test_global_epoch(uint64_t value);
void nvn_dirty_test_lock_slots(int lock);
#endif
int nvn_dirty_since(const void *addr, size_t len, uint64_t stamp); // 覆盖页在戳之后被写过 → 1 | Covered pages were written after the stamp -> 1
// 一次查找区间，逐个访问脏页；未跟踪或 stamp=0 时保守访问全部。
// Find the range once and visit dirty pages; untracked memory or stamp=0
// conservatively visits all bytes. Callbacks retain the caller's pointer tag.
void nvn_dirty_visit(const void *addr, size_t len, uint64_t stamp,
                     void (*visit)(const void *, size_t, void *), void *context);
void nvn_dirty_stats(uint64_t *faults, uint64_t *preps);           // 累计：游戏写触发的缺页次数、宿主写前解保护的页数 | Cumulative: number of page faults triggered by game writes, number of pages unprotected before host writes
void nvn_dirty_mp_stats(uint64_t *calls, uint64_t *pages, uint64_t *ns);   // 累计：本模块 mprotect 调用次数、覆盖页数、耗时 | Cumulative: this module's mprotect call count, pages covered, time spent
void nvn_dirty_slot_stats(int *live, int *used_slots, uint64_t *adds_total); // 当前存活槽、槽表高水位、登记总次数 | Currently live slots, slot table high-water mark, total number of registrations

// Image-only ownership. Exact page references; overlapping image scopes share
// the original state/generation, and final release restores RW. Not a Pool pin
// or backing provenance certificate. Caller keeps storage mapped through release.
typedef struct nvn_dirty_scope nvn_dirty_scope;
nvn_dirty_scope *nvn_dirty_scope_acquire(const void *address,size_t bytes);
void nvn_dirty_scope_release(nvn_dirty_scope *scope);
// Known GPU writes invalidate page generations without pretending mprotect sees
// GPU access. This never unprotects pages or makes GPU data CPU-visible.
void nvn_dirty_scope_gpu_write(const void *address,size_t bytes);
