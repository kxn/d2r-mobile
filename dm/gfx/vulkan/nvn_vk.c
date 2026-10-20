// Vulkan 设备、池缓冲、真实栅栏与完成副作用。游戏地址沿用原 NVN 接口层的 CPU
// 地址。 Vulkan device, pool buffers, real fences and completion effects. Game
// addresses remain CPU addresses as in the original NVN interface.
// 无外部宿主内存扩展时使用一致映射镜像；只同步 CPU 改过的页，GPU
// 写回在栅栏完成之后发生。 Without external host-memory import use coherent
// mapped mirrors; upload only CPU-modified pages and publish GPU writes only
// after fence completion.
#include "../nvn/nvn_dirty.h"
#include "nvn_vk_shader.h"
#include "nvn_vk_plan.h"
#include "nvn_vk_window.h"
#include "nvn_vk_freeze.h"
#include "nvn_vk_system_trace.h"
#include "nvn_vk_pool_copy_stats.h"
#include <errno.h>
#include <limits.h>
#include <stdatomic.h>
#include <unistd.h>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

vk_context V = {.resources = PTHREAD_MUTEX_INITIALIZER,
                .queue_mu = PTHREAD_MUTEX_INITIALIZER,
                .present_queue_mu = PTHREAD_MUTEX_INITIALIZER};
typedef struct pool_gpu_access pool_gpu_access;
typedef struct pool_cpu_authority pool_cpu_authority;
typedef struct pool_cpu_exposure pool_cpu_exposure;
typedef struct pool_backing_span {
  struct pool_backing_span *next;
  uint64_t begin,end,generation;
  vk_buffer buffer;
} pool_backing_span;
typedef struct pool {
  struct pool *next;
  uint8_t *guest, *shadow;
  uint64_t size;
  uint64_t identity;
  uint64_t stamp;
  uint64_t *page_upload_stamps;
  uint64_t *initialized_pages;
  // A proved contiguous subset of the monotonic initialized-page bitmap.
  // This describes first materialization only, never CPU/GPU content freshness.
  uint64_t initialized_first_page,initialized_end_page;
  _Atomic uint64_t initialized_bytes;
  unsigned references;
  uint64_t access_serial;
  pool_gpu_access *recording_access;
  // Canonical physical ownership. Default policy remains one whole node.
  // Startup opt-in reserves disjoint windows; explicit quiescent operation
  // maintenance may replace them with a sticky whole backing.
  pool_backing_span whole,*backings;
  // Only the registry-locked resolver may read/write this non-owning hint.
  pool_backing_span *locked_span_hint;
  uint64_t backing_generation;
  // Tracking starts with window-mode pool creation, before any buffer writer.
  // The legacy flag is only a fail-closed fallback for untracked migration/OOM.
  int cpu_copy_requires_gpu,cpu_authority_tracking;
#ifdef DM_VULKAN_SHARED_POOLS
  int shared,shared_closing;
#endif
  pool_cpu_authority *cpu_authority;
  // Original NVN intent only; never interpreted as VkMemoryPropertyFlags.
  // Known zero flags and unknown legacy callers remain distinguishable.
  uint32_t nvn_flags;
  int nvn_flags_known;
  pool_cpu_exposure *cpu_exposure; // experiment-only, lazy bounded range exclusions
  int cpu_exposure_full; // metadata OOM/size fallback, never drops excluded pages
} pool;
#ifdef DM_VULKAN_SHARED_POOLS
static int shared_requested,shared_active,shared_extension;
#ifdef DM_VULKAN_TEST_EMBED
static int shared_cache_fixture_records;
#endif
static uint64_t shared_alignment;
static void shared_device_probe(void);
static void shared_pool_materialize(pool *);
static void shared_cache(vk_buffer *,uint64_t,uint64_t,int);
#endif
#include "nvn_vk_cpu_authority.inc"
#include "nvn_vk_cpu_exposure.inc"
// 上传按最多 4 KiB 的连续段复制，GPU 使用也按页保守记录；不扫描每次绑定的字节区间链。
// Uploads copy spans of at most 4 KiB. Conservatively index GPU use by page,
// rather than walking a growing byte-interval list for every buffer binding.
enum { GPU_ACCESS_SHIFT = 12 };
static VkResult buffer_try_create(vk_buffer *b,uint64_t size,VkBufferUsageFlags usage,int readback);
#include "nvn_vk_pool_spans.inc"
#include "nvn_vk_upload_dual.inc"
static const VkPipelineStageFlags pool_stages[]={
  VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
  VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
  VK_PIPELINE_STAGE_ALL_COMMANDS_BIT};
enum { POOL_STAGE_COUNT=sizeof pool_stages/sizeof *pool_stages };
struct pool_gpu_access {
  pool_gpu_access *next;
  pool *owner;
  uint64_t bitmap_words;
  unsigned allocated_stages;
  uint64_t *pages;
  uint64_t *upload_pages,*overwritten_pages;
  uint64_t *stage_pages[POOL_STAGE_COUNT];
  uint64_t last_first_page,last_end_page;
  VkPipelineStageFlags last_stages;
  int last_initial;
};
static pool *pools;
static atomic_uint_fast64_t pool_registry_epoch;
static pool *pool_find(uint64_t addr,uint64_t size);
static uint64_t pool_adds;
static pthread_cond_t pool_cv = PTHREAD_COND_INITIALIZER;
typedef struct writeback_range {
  struct writeback_range *next;
  uint64_t offset, end;
  const uint8_t *known;
  uint64_t known_cutoff;
  VkPipelineStageFlags producers;
} writeback_range;
typedef struct pool_writeback {
  struct pool_writeback *next;
  pool *owner;
  writeback_range *ranges;
} pool_writeback;
static void pool_cpu_published_ranges(void *context) {
  pool_writeback *w=context;
  // All guest+shadow memcpy effects precede callbacks, and the submission's
  // original GPU-use references retain this logical pool until callbacks end.
  while(w->ranges) {
    writeback_range *r=w->ranges;w->ranges=r->next;
    pool_cpu_synced(w->owner,r->offset,r->end-r->offset,r->known_cutoff);free(r);
  }
  free(w);
}
static void pool_cpu_publish_ranges(pool_writeback *w) {
  if(w->owner->cpu_authority_tracking || w->owner->cpu_copy_requires_gpu)
    vk_completion(pool_cpu_published_ranges,w);
  else {
    // Default whole mode retains original immediate freeing/no authority map.
    while(w->ranges) {writeback_range *r=w->ranges;w->ranges=r->next;free(r);}
    free(w);
  }
}
typedef struct {
  uint64_t address, size;
} cpu_write_range;
typedef struct pending_cpu_copy {
  struct pending_cpu_copy *next;
  pool *source;
  uint64_t offset,size;
  uint8_t *snapshot;
} pending_cpu_copy;
typedef struct queued_pool_upload {
  struct queued_pool_upload *next;
  VkBuffer source,destination;
  const pool_backing_span *backing;
  uint64_t backing_generation;
  unsigned count;
  VkPipelineStageFlags before,after;
  VkBufferCopy regions[];
} queued_pool_upload;
// 外部厂商采集时关闭自定义计时；NVN 自身查询和真实完成栅栏不受影响。
// Disable custom timing for vendor profiling, preserving guest NVN queries
// and actual completion fences. Latched once at device initialization.
static int external_profile;
static _Atomic int tbdr_reference=-1,load_probe_request=-1,gpu_profile_request=-1;
int vk_tbdr_enabled(void) {
  int reference=atomic_load(&tbdr_reference);
  return reference<0?V.options.tbdr:!reference;
}
void vk_tbdr_reference(int reference) { atomic_store(&tbdr_reference,!!reference); }
int vk_load_probe_mode(void) {
  int mode=atomic_load(&load_probe_request);
  return mode<0?V.options.load_probe:mode;
}
void vk_load_probe_request(int mode) { atomic_store(&load_probe_request,mode); }
void vk_gpu_profile_enabled(int enabled) { atomic_store(&gpu_profile_request,!!enabled); }
static int async_submissions;
static unsigned resource_sets;
// Optional accounting is latched by init_options before workers start. Copies
// can originate from more than the recording owner: use relaxed per-page/batch
// atomics only while ON, never per-word counters or a second memory scan.
static _Atomic int pool_copy_enabled;
#define VK_POOL_COPY_COUNTER(name) static _Atomic uint64_t pool_copy_##name;
VK_POOL_COPY_FIELDS(VK_POOL_COPY_COUNTER)
#undef VK_POOL_COPY_COUNTER
#define POOL_COPY_ADD(name, bytes) do { if(V.options.pool_copy_accounting) \
  atomic_fetch_add_explicit(&pool_copy_##name,(bytes),memory_order_relaxed); } while(0)
void vk_pool_copy_snapshot(vk_pool_copy_stats *out) {
  *out=(vk_pool_copy_stats){.enabled=atomic_load_explicit(&pool_copy_enabled,memory_order_relaxed)};
  if(!out->enabled)return;
#define VK_POOL_COPY_READ(name) out->name=atomic_load_explicit(&pool_copy_##name,memory_order_relaxed);
  VK_POOL_COPY_FIELDS(VK_POOL_COPY_READ)
#undef VK_POOL_COPY_READ
}
// src!=NULL effects do not use atomic-event mode. Reuse that existing word;
// no extra bytes or allocation in the default-OFF effect/submission objects.
enum {POOL_COPY_GENERIC,POOL_COPY_READBACK_GUEST,POOL_COPY_READBACK_SHADOW,
      POOL_COPY_KNOWN_GUEST,POOL_COPY_KNOWN_SHADOW};
static _Atomic uint64_t sync_guest_submits,sync_internal_submits,sync_readback_ranges,sync_readback_bytes;
static _Atomic uint64_t sync_internal_readback_bytes,sync_cpu_reads,sync_cpu_submit_waits,sync_cpu_pending_waits;
static _Atomic uint64_t sync_proof_reads,sync_proof_pending;
static _Atomic int cover_wait_reference=-1;
static _Atomic int readback_reference_requested=-1,readback_reference_active=1;
static _Atomic uint64_t cached_readback_bytes,readback_invalidations;
static _Atomic int pool_sync_requested=-1,pool_sync_active;
static _Atomic uint64_t sync_full_bytes,sync_used_bytes,sync_protected_bytes;
static _Atomic uint64_t overwritten_upload_pages;
static _Atomic int pool_mirror_requested=-1;
static _Atomic uint64_t pool_window_requested=UINT64_MAX;
static _Atomic int pool_options_initialized;
static unsigned pool_operation_depth,pool_maintenance;
static uint64_t pool_window_admissions,pool_window_added_bytes,pool_window_added_spans,pool_window_promotions;
static _Atomic uint64_t published_window_admissions,published_window_added_bytes,published_window_added_spans,published_window_promotions;
static _Atomic uint64_t published_window_fallbacks;
void vk_pool_window_bytes(uint64_t bytes) {
  if(atomic_load(&pool_options_initialized))die("Vulkan pool windows are a startup-only option");
  if(bytes && (bytes<4096 || (bytes&(bytes-1))))die("Vulkan pool window size must be a power of two >=4096");
  atomic_store(&pool_window_requested,bytes);
}
int vk_pool_maintenance_active(void) { return pool_maintenance!=0; }
static void pool_window_publish(void) {
  atomic_store(&published_window_admissions,pool_window_admissions);
  atomic_store(&published_window_added_bytes,pool_window_added_bytes);
  atomic_store(&published_window_added_spans,pool_window_added_spans);
  atomic_store(&published_window_promotions,pool_window_promotions);
}
uint64_t vk_pool_window_fallbacks(void) { return atomic_load(&published_window_fallbacks); }
void vk_pool_window_stats(uint64_t *bytes,uint64_t *admissions,uint64_t *added_bytes,uint64_t *added_spans,uint64_t *promotions) {
  if(bytes)*bytes=V.options.pool_window_bytes;
  if(admissions)*admissions=atomic_load(&published_window_admissions);
  if(added_bytes)*added_bytes=atomic_load(&published_window_added_bytes);
  if(added_spans)*added_spans=atomic_load(&published_window_added_spans);
  if(promotions)*promotions=atomic_load(&published_window_promotions);
}
void vk_pool_mirror_reference(int reference) { atomic_store(&pool_mirror_requested,!!reference); }
uint64_t vk_pool_initialized_bytes(void) {
  uint64_t bytes=0;
  pthread_mutex_lock(&V.resources);
  for(pool *p=pools;p;p=p->next) bytes+=p->initialized_bytes;
  pthread_mutex_unlock(&V.resources);
  return bytes;
}
void vk_pool_mirror_stats(uint64_t *registered,uint64_t *backed,uint64_t *count,uint64_t *materialized,int *reference) {
  *registered=*backed=*count=*materialized=0;
  pthread_mutex_lock(&V.resources);
  for(pool *p=pools;p;p=p->next) {
    *registered+=p->size;(*count)++;
    if(p->backings) { *backed+=pool_spans_bytes(p);(*materialized)++; }
  }
  pthread_mutex_unlock(&V.resources);
  *reference=V.options.pool_mirror_reference;
}
uint64_t vk_pool_overwrite_skipped_pages(void) { return atomic_load(&overwritten_upload_pages); }
static _Atomic int cpu_copy_requested=-1,cpu_copy_active=1;
static _Atomic uint64_t cpu_copy_snapshots,cpu_copy_published,cpu_copy_fallbacks;
void vk_cpu_copy_reference(int reference) { atomic_store(&cpu_copy_requested,!!reference); }
void vk_cpu_copy_stats(uint64_t *snapshots,uint64_t *published,uint64_t *fallbacks,int *reference) {
  *snapshots=atomic_load(&cpu_copy_snapshots);*published=atomic_load(&cpu_copy_published);
  *fallbacks=atomic_load(&cpu_copy_fallbacks);*reference=atomic_load(&cpu_copy_active);
}
void vk_pool_sync_reference(int reference) {
  if(reference && atomic_load_explicit(&pool_defer_publication_enabled,memory_order_acquire)) {
    LOG("[vulkan] pool sync reference rejected: deferred publication requires cold restart with experiment OFF");return;
  }
  atomic_store(&pool_sync_requested,!!reference);
}
void vk_pool_sync_policy_stats(uint64_t *full,uint64_t *used,uint64_t *protected_bytes,int *reference) {
  *full=atomic_load(&sync_full_bytes);*used=atomic_load(&sync_used_bytes);
  *protected_bytes=atomic_load(&sync_protected_bytes);*reference=atomic_load(&pool_sync_active);
}
void vk_readback_coherent_reference(int reference) { atomic_store(&readback_reference_requested,!!reference); }

void vk_cover_wait_reference(int reference) { atomic_store(&cover_wait_reference,!!reference); }
void vk_buffer_sync_snapshot(vk_buffer_sync_stats *out) {
  *out=(vk_buffer_sync_stats){.guest_submits=atomic_load(&sync_guest_submits),.internal_submits=atomic_load(&sync_internal_submits),
    .readback_ranges=atomic_load(&sync_readback_ranges),.readback_bytes=atomic_load(&sync_readback_bytes),
    .internal_readback_bytes=atomic_load(&sync_internal_readback_bytes),.cpu_reads=atomic_load(&sync_cpu_reads),
    .cpu_submit_waits=atomic_load(&sync_cpu_submit_waits),.cpu_pending_waits=atomic_load(&sync_cpu_pending_waits),
    .proof_reads=atomic_load(&sync_proof_reads),.proof_pending=atomic_load(&sync_proof_pending),
    .upload_waits=vk_pool_upload_waits(),.cover_reference=atomic_load(&cover_wait_reference),
    .cached_readback_bytes=atomic_load(&cached_readback_bytes),.readback_invalidations=atomic_load(&readback_invalidations),
    .readback_reference=atomic_load(&readback_reference_active)};
}

static _Atomic uint64_t profile_times[VK_TIME_COUNT];
uint64_t vk_profile_start(void) {
  return !external_profile && V.options.stats ? now_ns() : 0;
}
void vk_profile_end(unsigned category, uint64_t started) {
  if (started)
    atomic_fetch_add(&profile_times[category], now_ns() - started);
}
void vk_profile_read(uint64_t times[VK_TIME_COUNT]) {
  for (unsigned i = 0; i < VK_TIME_COUNT; i++)
    times[i] = atomic_load(&profile_times[i]);
}
typedef struct temporary {
  struct temporary *next;
  vk_buffer b;
  uint64_t last_used_serial;
} temporary;
enum { GPU_REGIONS = 512, GPU_REGION_STATS = 1024 };
typedef struct {
  const char *kind;
  char key[17];
  unsigned x, y;
  int ended;
} gpu_region;
// 录制、在途、重置中、可复用资源分开。完成线程重置独占命令池；提交线程
// 只接收重置完成的一份，再回收元数据并复用。
// Separate recording, in-flight, resetting and ready ownership. Completion
// resets the exclusive command pool; only then can submission recycle/reuse it.
typedef struct submit_resources {
  struct submit_resources *next;
  VkCommandPool command_pool;
  struct {
    VkCommandBuffer *handles;
    unsigned count, used, capacity;
  } command_buffers[2];
  temporary *temporaries;
  vk_shader_submission *shader;
  vk_graphics_submission *graphics;
  vk_texture_submission *texture;
  pool_writeback *writebacks;
#ifdef DM_VULKAN_SHARED_POOLS
  pool_writeback *shared_publishes;
#endif
  pending_cpu_copy *cpu_copies;
  pool_gpu_access *gpu_accesses;
  pool_gpu_access *cached_gpu_accesses;
  uint64_t cached_gpu_access_bytes;
  unsigned cached_gpu_access_count;
  cpu_write_range *cpu_writes;
  size_t cpu_write_count;
#ifdef DM_VULKAN_SHARED_POOLS
  size_t cpu_effect_write_count; // excludes GPU-written shared publication ranges
#endif
  uint64_t queue_serial,readback_bytes;
  VkCommandBuffer uploads;
  queued_pool_upload *pool_uploads;
  queued_pool_upload **pool_upload_tail;
  int pool_sync_reference,cpu_copy_reference,submission_barrier_reference;
  int pool_lookup_reference;
  uint64_t cpu_authority_begin;
  // Isolated GPU-only backing migration: never upload guest/shadow over source GPU bytes.
  int pool_migration;
  int upload_scan_reference;
  // Private to this recording and its retained owner. Never share the
  // registry-locked pool hint with the completion or recording fast path.
  struct { pool *owner;pool_gpu_access *use;pool_backing_span *span_hint; } pool_lookups[8];
  unsigned pool_lookup_count,pool_lookup_next;
  uint64_t pool_lookup_epoch;
  // Exact negative result for immutable *earlier* CPU-publication ranges.
  // Completion only removes ranges while this recording owns enqueue order.
  // This is not a cache of current writes, guest bytes or GPU dependencies.
  uint64_t pending_read_address,pending_read_size;
  VkQueryPool timing_queries, region_queries;
  unsigned region_count;
  int timing_active, region_active;
  gpu_region regions[GPU_REGIONS];
} submit_resources;
static submit_resources *recording_resources, *available_resources;
#include "nvn_vk_pool_access_cache.inc"
VkCommandBuffer vk_command_buffer(VkCommandBufferLevel level) {
  submit_resources *r = recording_resources;
  if (!r || (level != VK_COMMAND_BUFFER_LEVEL_PRIMARY &&
             level != VK_COMMAND_BUFFER_LEVEL_SECONDARY))
    die("Vulkan command buffer allocation outside a submission");
  if (!r->command_pool) {
    VkCommandPoolCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      // Handles are recorded once per submission and reused only after a
      // completed whole-pool reset. Individual reset support is unnecessary
      // and can force the driver to keep separate per-buffer allocators.
      .flags = 0,
      .queueFamilyIndex = V.queue_family};
    VK_CHECK(vkCreateCommandPool(V.device, &info, NULL, &r->command_pool));
  }
  unsigned k = level == VK_COMMAND_BUFFER_LEVEL_SECONDARY;
  if (r->command_buffers[k].used == r->command_buffers[k].count) {
    if (r->command_buffers[k].count == r->command_buffers[k].capacity) {
      unsigned capacity = r->command_buffers[k].capacity ?
                          r->command_buffers[k].capacity * 2 : 16;
      VkCommandBuffer *handles = realloc(r->command_buffers[k].handles,
                                        capacity * sizeof *handles);
      if (!handles) die("Vulkan command buffer cache allocation");
      r->command_buffers[k].handles = handles;
      r->command_buffers[k].capacity = capacity;
    }
    VkCommandBufferAllocateInfo info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = r->command_pool, .level = level, .commandBufferCount = 1};
    VK_CHECK(vkAllocateCommandBuffers(V.device, &info,
      &r->command_buffers[k].handles[r->command_buffers[k].count]));
    r->command_buffers[k].count++;
  }
  return r->command_buffers[k].handles[r->command_buffers[k].used++];
}
static temporary *reusable_temporaries;
static uint64_t reusable_bytes;
static uint64_t temporary_last_prune;
static int temporary_age_reference;
#ifdef DM_VULKAN_TEST_EMBED
static uint64_t temporary_soft_limit=64ull<<20;
static uint64_t temporary_idle_age=1024,temporary_prune_interval=256;
#else
static const uint64_t temporary_soft_limit=64ull<<20;
static const uint64_t temporary_idle_age=1024,temporary_prune_interval=256;
#endif
static void buffer_create(vk_buffer *b,uint64_t size,VkBufferUsageFlags usage,int readback);
static vk_buffer *temp_buffer(uint64_t size,int readback) {
  if (!recording_resources)
    die("Vulkan staging allocation outside a submission");
  temporary **reuse = &reusable_temporaries;
  while (*reuse && ((*reuse)->b.size != size || (*reuse)->b.readback != readback))
    reuse = &(*reuse)->next;
  temporary *t = *reuse;
  if (t) {
    *reuse = t->next;
    reusable_bytes -= t->b.size;
    t->last_used_serial = recording_resources->queue_serial;
    t->next = recording_resources->temporaries;
    recording_resources->temporaries = t;
    return &t->b;
  }
  t = calloc(1, sizeof *t);
  if (!t)
    die("Vulkan staging allocation");
  buffer_create(&t->b, size,
                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                       VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,readback);
  t->last_used_serial = recording_resources->queue_serial;
  t->next = recording_resources->temporaries;
  recording_resources->temporaries = t;
  return &t->b;
}
vk_buffer *vk_temp_buffer(uint64_t size) { return temp_buffer(size,0); }
static vk_buffer *temp_readback(uint64_t size) {
  return temp_buffer(size,!atomic_load(&readback_reference_active));
}
static pthread_once_t init_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t completion_mu = PTHREAD_MUTEX_INITIALIZER;
// Guest bytes and their upload shadow form one publication. A dirty page may
// contain no CPU changes at all; compare it without racing the worker copying
// GPU results into those two arrays. Never hold this lock across a GPU wait.
static pthread_mutex_t pool_publication_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t completion_cv = PTHREAD_COND_INITIALIZER;
static uint64_t submitted, completed, queue_submitted, queue_completed;
// GPU/CPU-effect completion is observable before driver cleanup. Only this
// separate, ordered watermark proves a command pool can be reused/destroyed.
static uint64_t queue_reset_completed;
#ifdef DM_VULKAN_TEST_EMBED
static _Atomic int command_reset_test_enabled;
static uint64_t command_reset_test_blocked;
static unsigned command_reset_test_pressure_waiters,command_reset_test_finish_waiters;
#endif
static _Atomic unsigned in_flight;
static _Atomic int submission_barrier_requested = -1;
static _Atomic int submission_barrier_active = -1;
int vk_submission_barrier_mode(void) { return atomic_load(&submission_barrier_active); }
void vk_submission_barrier_reference(int reference) {
  if(reference && atomic_load_explicit(&pool_defer_publication_enabled,memory_order_acquire)) {
    LOG("[vulkan] full submission reference rejected: deferred publication requires cold restart with experiment OFF");return;
  }
  atomic_store(&submission_barrier_requested, !!reference);
}
static _Atomic int stream_requested=-1,stream_active=1;
static _Atomic uint64_t stream_splits,stream_boundaries,stream_draws;
static int stream_recording,stream_reference=1;
static int replay_system_trace;
static uint64_t stream_last_draw;
static int stream_head_pending;
void vk_stream_submit_reference(int reference) { atomic_store(&stream_requested,!!reference); }
void vk_stream_stats(uint64_t *splits,uint64_t *boundaries,uint64_t *draws,int *reference) {
  *splits=atomic_load(&stream_splits);*boundaries=atomic_load(&stream_boundaries);
  *draws=atomic_load(&stream_draws);*reference=atomic_load(&stream_active);
}
void vk_stream_begin(void) {
  replay_system_trace=vk_system_trace_begin("NVN command replay");
  int reference=atomic_load(&stream_requested);
  if(reference<0) {
    const char *setting=getenv("DM_VULKAN_STREAM_SUBMIT_REFERENCE");
    if(setting && strcmp(setting,"0") && strcmp(setting,"1"))
      die("DM_VULKAN_STREAM_SUBMIT_REFERENCE expects 0 or 1");
    reference=!setting || strcmp(setting,"0");atomic_store(&stream_requested,reference);
  }
  if(!reference && !vk_async_submissions()) die("Vulkan stream splitting requires asynchronous submissions");
  stream_reference=reference;atomic_store(&stream_active,reference);
  stream_recording=1;stream_last_draw=V.draws;stream_head_pending=1;
}
void vk_stream_end(void) {
  stream_recording=0;vk_system_trace_end(replay_system_trace);replay_system_trace=0;
}
static void stream_flush(int target_boundary,int compute_boundary) {
  if(!stream_recording) return;
  // Do not cut a render pass or an occlusion query merely to meet a draw
  // quota. A real target change is already a pass boundary; other checkpoints
  // must find both scopes closed. The existing resource cap supplies pressure.
  // Start GPU work at the first safe opportunity, then batch the remainder.
  // A uniform small quota adds submissions without necessarily advancing the
  // first batch. Prefer a completed dispatch for the initial batch; timestamp
  // checkpoints between draws must retain the pass/query grouping.
  uint64_t quota=64;
  if(stream_head_pending && compute_boundary) quota=1;
  if(stream_reference || V.draws-stream_last_draw<quota) return;
  // Most checkpoints are inside the same long pass. Reject them before the
  // optional upstream freeze/extrapolation proof, which reads environment
  // controls. It is needed only where we can actually submit a chunk.
  if(!target_boundary && (vk_render_active() || vk_counter_active())) return;
  if(!vk_replay_preserves_ops()) return;
  if(target_boundary) vk_render_end_preserve();
  if(vk_render_active() || vk_counter_active()) return;
  vk_internal_commit();
  stream_head_pending=0;
  atomic_fetch_add(&stream_splits,1);atomic_fetch_add(&stream_draws,V.draws-stream_last_draw);
  stream_last_draw=V.draws;
  mtl_sub_begin();
}
void vk_stream_target_boundary(void) {
  if(stream_recording) atomic_fetch_add(&stream_boundaries,1);
  stream_flush(1,0);
}
void vk_stream_checkpoint(void) { stream_flush(0,0); }
void vk_stream_compute_checkpoint(void) { stream_flush(0,1); }
static _Atomic uint64_t upload_waits;
static _Atomic uint64_t region_samples;
uint64_t vk_gpu_profile_samples(void) { return atomic_load(&region_samples); }
static void gpu_profile_begin_submit(submit_resources *r) {
  r->region_count = 0;
  int requested=atomic_load(&gpu_profile_request);
  r->region_active = !external_profile && V.timestamp_bits &&
                     (requested<0?V.options.gpu_profile:requested);
  if (!r->region_active) return;
  if (!r->region_queries) {
    VkQueryPoolCreateInfo info = {.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                  .queryType = VK_QUERY_TYPE_TIMESTAMP,
                                  .queryCount = GPU_REGIONS * 2};
    VK_CHECK(vkCreateQueryPool(V.device, &info, NULL, &r->region_queries));
  }
  vkCmdResetQueryPool(V.cmd, r->region_queries, 0, GPU_REGIONS * 2);
}
unsigned vk_gpu_region_begin(const char *kind, const char *key, unsigned x, unsigned y) {
  submit_resources *r = recording_resources;
  if (!r || !r->region_active) return 0;
  if (r->region_count == GPU_REGIONS) die("Vulkan GPU region diagnostic capacity exceeded");
  unsigned i = r->region_count++;
  r->regions[i].kind = kind;
  snprintf(r->regions[i].key, sizeof r->regions[i].key, "%s", key ? key : "");
  r->regions[i].x = x; r->regions[i].y = y; r->regions[i].ended = 0;
  vkCmdWriteTimestamp(V.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, r->region_queries, i * 2);
  return i + 1;
}
void vk_gpu_region_label(unsigned token, const char *key) {
  if (!token) return;
  submit_resources *r = recording_resources;
  if (!r || token > r->region_count) die("Vulkan GPU region label outside submission");
  if (!r->regions[token - 1].key[0] && key)
    snprintf(r->regions[token - 1].key, sizeof r->regions[0].key, "%s", key);
}
void vk_gpu_region_end(unsigned token) {
  if (!token) return;
  submit_resources *r = recording_resources;
  if (!r || token > r->region_count) die("Vulkan GPU region end outside submission");
  unsigned i = token - 1;
  vkCmdWriteTimestamp(V.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, r->region_queries, i * 2 + 1);
  r->regions[i].ended = 1;
}
static void gpu_profile_complete(const submit_resources *r, uint64_t frame) {
  if (!r->region_count) return;
  // 完成线程只读该提交拥有的查询和元数据，不读取当前录制状态。
  // Completion reads only this submission's queries and metadata, never the
  // state currently being recorded by the submit thread.
  const gpu_region *regions = r->regions;
  uint64_t ticks[GPU_REGIONS * 2];
  VK_CHECK(vkGetQueryPoolResults(V.device, r->region_queries, 0, r->region_count * 2,
                                r->region_count * 2 * sizeof(uint64_t), ticks,
                                sizeof(uint64_t), VK_QUERY_RESULT_64_BIT));
  static struct {
    const char *kind;
    char key[17];
    unsigned x, y;
    uint64_t ns, max_ns, count;
  } stats[GPU_REGION_STATS];
  static unsigned stat_count;
  static uint64_t first_frame, last_report;
  if (!first_frame) first_frame = frame;
  uint64_t mask = V.timestamp_bits == 64 ? UINT64_MAX : (1ull << V.timestamp_bits) - 1;
  for (unsigned i = 0; i < r->region_count; i++) {
    if (!regions[i].ended) die("Vulkan unclosed GPU diagnostic region");
    uint64_t ns = (uint64_t)((long double)((ticks[i * 2 + 1] - ticks[i * 2]) & mask) *
                             V.props.limits.timestampPeriod);
    unsigned j = 0;
    while (j < stat_count && (strcmp(stats[j].kind, regions[i].kind) ||
           strcmp(stats[j].key, regions[i].key) || stats[j].x != regions[i].x ||
           stats[j].y != regions[i].y)) j++;
    if (j == stat_count) {
      if (stat_count == GPU_REGION_STATS) die("Vulkan GPU diagnostic grouping capacity exceeded");
      stats[j].kind = regions[i].kind;
      memcpy(stats[j].key, regions[i].key, sizeof stats[j].key);
      stats[j].x = regions[i].x; stats[j].y = regions[i].y;
      stat_count++;
    }
    stats[j].ns += ns; stats[j].count++;
    if (ns > stats[j].max_ns) stats[j].max_ns = ns;
    atomic_fetch_add(&region_samples, 1);
  }
  if (frame < last_report || frame - last_report < 60) return;
  last_report = frame;
  uint64_t frames = frame >= first_frame ? frame - first_frame + 1 : 1;
  for (unsigned rank = 0; rank < 8 && rank < stat_count; rank++) {
    unsigned best = 0;
    for (unsigned j = 1; j < stat_count; j++) if (stats[j].ns > stats[best].ns) best = j;
    if (!stats[best].count) break;
    LOG("[vulkan gpu region] frame=%llu kind=%s shader=%s size=%ux%u "
        "ms_per_frame=%.3f ms_per_region=%.3f max_ms=%.3f samples=%llu",
        (unsigned long long)frame, stats[best].kind,
        stats[best].key[0] ? stats[best].key : "clear", stats[best].x, stats[best].y,
        stats[best].ns / 1e6 / frames, stats[best].ns / 1e6 / stats[best].count,
        stats[best].max_ns / 1e6, (unsigned long long)stats[best].count);
    stats[best].ns = stats[best].count = stats[best].max_ns = 0;
  }
  for (unsigned j = 0; j < stat_count; j++) stats[j].ns = stats[j].count = stats[j].max_ns = 0;
  first_frame = frame + 1;
}
enum { GPU_TIME_RING = 128 };
static struct {
  uint64_t frame, ns;
  unsigned pending;
  int valid;
} gpu_times[GPU_TIME_RING];
double mtl_gpu_ms(uint64_t frame) {
  if (external_profile) return -1;
  pthread_mutex_lock(&completion_mu);
  unsigned k = frame % GPU_TIME_RING;
  double ms =
      gpu_times[k].valid && gpu_times[k].frame == frame && !gpu_times[k].pending
          ? gpu_times[k].ns / 1e6
          : -1;
  pthread_mutex_unlock(&completion_mu);
  return ms;
}
typedef struct submission {
  struct submission *next;
  VkFence fence;
  VkCommandBuffer cmd;
  vk_effect *effects;
  submit_resources *resources;
  uint64_t serial, queue_serial, frame, draws;
  int system_trace;
} submission;
static submission *pending, **pending_tail = &pending;
static submission *retired;

uint64_t vk_recording_serial(void) {
  return recording_resources ? recording_resources->queue_serial : 0;
}
uint64_t vk_completed_serial(void) {
  pthread_mutex_lock(&completion_mu);
  uint64_t serial = queue_completed;
  pthread_mutex_unlock(&completion_mu);
  return serial;
}
int vk_async_submissions(void) { mtl_init(); return async_submissions; }
static int range_overlap(uint64_t a, uint64_t as, uint64_t b, uint64_t bs) {
  return as && bs && (a >= b ? a - b < bs : b - a < as);
}
static int recording_cpu_write(uint64_t address, uint64_t size) {
  if (!recording_resources) return 0;
  for (pool_writeback *w = recording_resources->writebacks; w; w = w->next)
    for (writeback_range *r = w->ranges; r; r = r->next)
      if (range_overlap(address, size, (uintptr_t)w->owner->guest + r->offset,
                         r->end - r->offset)) return 1;
#ifdef DM_VULKAN_SHARED_POOLS
  for(pool_writeback *w=recording_resources->shared_publishes;w;w=w->next)
    for(writeback_range *r=w->ranges;r;r=r->next)
      if(range_overlap(address,size,(uintptr_t)w->owner->guest+r->offset,r->end-r->offset))return 1;
#endif
  for (vk_effect *e = V.effects; e; e = e->next)
    if (e->dst && range_overlap(address, size, (uintptr_t)e->dst, e->size)) return 1;
  return 0;
}
static uint64_t pending_cpu_write(uint64_t address, uint64_t size) {
  if (!atomic_load(&in_flight)) return 0;
  submit_resources *r=recording_resources;
  if(r && size && r->pending_read_size==size && r->pending_read_address==address)
    return 0;
  uint64_t serial = 0;
  pthread_mutex_lock(&completion_mu);
  for (submission *s = pending; s; s = s->next)
    for (size_t i = 0; i < s->resources->cpu_write_count; i++) {
      const cpu_write_range *r = &s->resources->cpu_writes[i];
      if (range_overlap(address, size, r->address, r->size)) {
        serial = s->queue_serial;
        break;
      }
    }
  pthread_mutex_unlock(&completion_mu);
  if(!serial && r && size) {
    r->pending_read_address=address;r->pending_read_size=size;
  }
  return serial;
}
static uint64_t pending_gpu_access(pool *p, uint64_t offset, uint64_t size) {
  if (!atomic_load(&in_flight)) return 0;
  uint64_t serial = 0;
  pthread_mutex_lock(&completion_mu);
  for (submission *s = pending; s; s = s->next)
    for (pool_gpu_access *use = s->resources->gpu_accesses; use; use = use->next)
      if (use->owner == p) {
        uint64_t first = offset >> GPU_ACCESS_SHIFT,
                 last = (offset + size - 1) >> GPU_ACCESS_SHIFT;
        for (uint64_t word = first / 64; word <= last / 64; word++) {
          uint64_t mask = UINT64_MAX;
          if (word == first / 64) mask &= UINT64_MAX << (first % 64);
          if (word == last / 64) mask &= UINT64_MAX >> (63 - last % 64);
          if (use->pages[word] & mask) { serial = s->queue_serial; break; }
        }
        break;
      }
  pthread_mutex_unlock(&completion_mu);
  return serial;
}
uint64_t vk_pool_upload_waits(void) { return atomic_load(&upload_waits); }
static int bitmap_overlaps(const uint64_t *pages,uint64_t offset,uint64_t size) {
  if(!pages||!size) return 0;
  uint64_t first=offset>>GPU_ACCESS_SHIFT,last=(offset+size-1)>>GPU_ACCESS_SHIFT;
  for(uint64_t word=first/64;word<=last/64;word++) {
    uint64_t mask=UINT64_MAX;
    if(word==first/64) mask&=UINT64_MAX<<(first%64);
    if(word==last/64) mask&=UINT64_MAX>>(63-last%64);
    if(pages[word]&mask) return 1;
  }
  return 0;
}
static VkPipelineStageFlags access_stages(const pool_gpu_access *use,uint64_t offset,uint64_t size) {
  VkPipelineStageFlags stages=0;
  for(unsigned k=0;k<POOL_STAGE_COUNT;k++)
    if(bitmap_overlaps(use->stage_pages[k],offset,size)) stages|=pool_stages[k];
  return stages;
}
static VkPipelineStageFlags pending_pool_stages(pool *p,uint64_t offset,uint64_t size) {
  VkPipelineStageFlags stages=0;
  pthread_mutex_lock(&completion_mu);
  for(submission *s=pending;s;s=s->next)
    for(pool_gpu_access *use=s->resources->gpu_accesses;use;use=use->next)
      if(use->owner==p) { stages|=access_stages(use,offset,size);break; }
  pthread_mutex_unlock(&completion_mu);
  return stages;
}
VkPipelineStageFlags vk_pool_pending_stages(uint64_t address,uint64_t size) {
  if(!size) return 0;
  pthread_mutex_lock(&V.resources);
  pool *p=pool_find(address,size);
  VkPipelineStageFlags stages=p?pending_pool_stages(p,address-(uintptr_t)p->guest,size):0;
  pthread_mutex_unlock(&V.resources);
  return stages;
}
static void bitmap_mark(uint64_t *pages,uint64_t offset,uint64_t size) {
  uint64_t first=offset>>GPU_ACCESS_SHIFT,last=(offset+size-1)>>GPU_ACCESS_SHIFT;
  uint64_t first_word=first/64,last_word=last/64;
  uint64_t first_mask=UINT64_MAX<<(first%64),last_mask=UINT64_MAX>>(63-last%64);
  if(first_word==last_word) pages[first_word]|=first_mask&last_mask;
  else {
    pages[first_word]|=first_mask;
    if(last_word>first_word+1) memset(pages+first_word+1,0xff,(last_word-first_word-1)*sizeof(uint64_t));
    pages[last_word]|=last_mask;
  }
}
// Recording-thread-only statistics. Publish at submission boundaries rather
// than adding an atomic read-modify-write to every buffer lookup.
static atomic_int pool_lookup_requested,pool_lookup_active;
static atomic_int upload_scan_requested,upload_scan_active;
static uint64_t upload_page_rechecks_skipped;
static atomic_uint_fast64_t published_upload_page_rechecks_skipped;
void vk_upload_scan_reference(int reference) { atomic_store(&upload_scan_requested,!!reference); }
int vk_upload_scan_reference_mode(void) { return atomic_load(&upload_scan_active); }
uint64_t vk_upload_page_rechecks_skipped(void) { return atomic_load(&published_upload_page_rechecks_skipped); }
static uint64_t pool_lookup_hits,pool_range_reuses,pool_lookup_slow;
static atomic_uint_fast64_t published_pool_lookup_hits,published_pool_range_reuses,published_pool_lookup_slow;
void vk_pool_lookup_reference(int reference) { atomic_store(&pool_lookup_requested,!!reference); }
void vk_pool_lookup_stats(uint64_t *hits,uint64_t *ranges,uint64_t *slow,int *reference) {
  *hits=atomic_load(&published_pool_lookup_hits);*ranges=atomic_load(&published_pool_range_reuses);
  *slow=atomic_load(&published_pool_lookup_slow);*reference=atomic_load(&pool_lookup_active);
}
static void record_pool_range(pool_gpu_access *use,uint64_t offset,uint64_t size,
                              VkPipelineStageFlags stages,int initial_contents) {
  VkPipelineStageFlags known=0;
  for(unsigned k=0;k<POOL_STAGE_COUNT-1;k++) known|=pool_stages[k];
  if(!stages||(stages&~known)) stages=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  uint64_t first=offset>>GPU_ACCESS_SHIFT;
  uint64_t end=((offset+size-1)>>GPU_ACCESS_SHIFT)+1;
  // All these bitmaps only gain bits during this recording. A previously
  // covered page range/stage/read requirement therefore needs no remarking.
  // Only page bookkeeping is cached: actual byte-range initialization and
  // GPU resource dependencies are still resolved for every binding.
  // GPU ownership can only remove upload requirements, not introduce new ones.
  if(!recording_resources->pool_lookup_reference && first>=use->last_first_page &&
      end<=use->last_end_page && !(stages&~use->last_stages) &&
      (!initial_contents || use->last_initial)) { pool_range_reuses++;return; }
  int single_word=!recording_resources->pool_lookup_reference && first/64==(end-1)/64;
  uint64_t word=first/64,mask=0;
  int covered=single_word;
  if(single_word) {
    // Interleaved bindings can miss the last-range proof while their actual
    // page bits are already present. Check and merge in one pass: a miss must
    // not scan the requested stage bitmaps twice. Only missing bits are written.
    mask=(UINT64_MAX<<(first%64)) & (UINT64_MAX>>(63-(end-1)%64));
    if((use->pages[word]&mask)!=mask) { use->pages[word]|=mask;covered=0; }
    // A GPU overwrite discharges initial contents only for its proven full
    // pages. The union also handles ranges mixing CPU inputs and GPU outputs.
    if(initial_contents) {
      uint64_t missing=mask & ~(use->upload_pages[word]|use->overwritten_pages[word]);
      if(missing) { use->upload_pages[word]|=missing;covered=0; }
    }
  } else bitmap_mark(use->pages,offset,size);
  if(initial_contents && !single_word) {
    uint64_t last=end-1;
    for(uint64_t word=first/64;word<=last/64;word++) {
      uint64_t mask=UINT64_MAX;
      if(word==first/64) mask &= UINT64_MAX<<(first%64);
      if(word==last/64) mask &= UINT64_MAX>>(63-last%64);
      use->upload_pages[word] |= mask & ~use->overwritten_pages[word];
    }
  }
  for(unsigned k=0;k<POOL_STAGE_COUNT;k++) if(stages&pool_stages[k]) {
    if(!use->stage_pages[k]) {
      use->stage_pages[k]=calloc(use->bitmap_words,sizeof(uint64_t));
      if(!use->stage_pages[k]) die("Vulkan stage access bitmap allocation");
      use->allocated_stages|=1u<<k;
    }
    if(single_word) {
      if((use->stage_pages[k][word]&mask)!=mask) {
        use->stage_pages[k][word]|=mask;covered=0;
      }
    } else bitmap_mark(use->stage_pages[k],offset,size);
  }
  if(covered) { pool_range_reuses++;return; }
  if(recording_resources->pool_lookup_reference)return;
  if(first==use->last_first_page && end==use->last_end_page) {
    use->last_stages|=stages;use->last_initial|=initial_contents;
  } else {
    use->last_first_page=first;use->last_end_page=end;
    use->last_stages=stages;use->last_initial=initial_contents;
  }
}
// Caller holds resources for reference acquisition and the pool's recording
// index. Current-use bitmaps themselves have only the recording thread owner.
static pool_gpu_access *record_pool_use(pool *p,uint64_t offset,uint64_t size,
                                        VkPipelineStageFlags stages,int initial_contents) {
  if (!recording_resources || !size) return NULL;
  pool_gpu_access *use;
  if (p->access_serial == recording_resources->queue_serial) {
    use = p->recording_access;
  } else {
    uint64_t words = ((p->size - 1) >> (GPU_ACCESS_SHIFT + 6)) + 1;
    if (words > SIZE_MAX / (3*sizeof(uint64_t))) die("Vulkan GPU-access bitmap size");
    use=pool_access_cache_take(recording_resources,words);
    if(!use) {
      use = calloc(1, sizeof *use);
      if (!use) die("Vulkan GPU-access pool allocation");
      use->bitmap_words=words;
      use->pages = calloc((size_t)words*3, sizeof(uint64_t));
      if (!use->pages) die("Vulkan GPU-access bitmap allocation");
      use->upload_pages=use->pages+words;
      use->overwritten_pages=use->upload_pages+words;
    }
    use->owner = p;
    use->next = recording_resources->gpu_accesses;
    recording_resources->gpu_accesses = use;
    p->access_serial = recording_resources->queue_serial;
    p->recording_access = use;
    p->references++;
  }
  record_pool_range(use,offset,size,stages,initial_contents);
  return use;
}
static void record_pool_access(pool *p,uint64_t offset,uint64_t size,VkPipelineStageFlags stages) {
  record_pool_use(p,offset,size,stages,1);
}
int vk_pool_pending_writes(uint64_t address, uint64_t size) {
  return size && (recording_cpu_write(address, size) || pending_cpu_write(address, size));
}

// Only the recording owner touches this list. Entries arrive after GPU/CPU
// effects AND the exclusive command-pool reset; active, pending and reset-
// blocked allocations cannot be considered here. Keep the hot working set,
// even above the soft limit, rather than repeatedly allocating it each frame.
static void prune_reusable_temporaries(uint64_t now) {
  if(temporary_age_reference || reusable_bytes<=temporary_soft_limit ||
     now<temporary_last_prune || now-temporary_last_prune<temporary_prune_interval)return;
  temporary_last_prune=now;
  temporary **link=&reusable_temporaries;
  while(*link && reusable_bytes>temporary_soft_limit) {
    temporary *t=*link;
    if(now>=t->last_used_serial && now-t->last_used_serial>=temporary_idle_age) {
      *link=t->next;reusable_bytes-=t->b.size;
      vk_buffer_destroy(&t->b);free(t);
    } else link=&t->next;
  }
}
static void reclaim_submissions(void) {
  pthread_mutex_lock(&completion_mu);
  submission *list = retired;
  retired = NULL;
  pthread_mutex_unlock(&completion_mu);
  while (list) {
    submission *s = list;
    list = s->next;
    // The worker publishes retired only after its exclusive command-pool
    // reset. Shared metadata caches and reusable allocations stay on this
    // submission thread; they are not safe to recycle on completion.
    submit_resources *r = s->resources;
    r->command_buffers[0].used = r->command_buffers[1].used = 0;
    r->uploads = VK_NULL_HANDLE;
    vk_graphics_recycle(r->graphics);
    vk_shader_recycle(r->shader);
    vk_texture_recycle(r->texture);
    free(r->cpu_writes);
    r->cpu_writes = NULL;
    r->cpu_write_count = 0;
    while (r->gpu_accesses) {
      pool_gpu_access *use = r->gpu_accesses;
      r->gpu_accesses = use->next;
      pool_access_cache_retire(r,use);
    }
    while (r->temporaries) {
      temporary *t = r->temporaries;
      r->temporaries = t->next;
      if (t->b.size <= (256ull << 20) - reusable_bytes) {
        t->next = reusable_temporaries;
        reusable_temporaries = t;
        reusable_bytes += t->b.size;
      } else {
        vk_buffer_destroy(&t->b);
        free(t);
      }
    }
    r->next = available_resources;
    available_resources = r;
    free(s);
  }
  prune_reusable_temporaries(queue_submitted);
}

// Owner control boundary only. No wait, submit, initialization or active-data
// eviction: the worker has already reset every submission in retired.
int mtl_trim_idle_resources(void) {
  if(!V.device || V.cmd || recording_resources)return 0;
  reclaim_submissions();
  // Available contexts have completed effects AND command-pool reset. Drop
  // the entire idle descriptor owner so no retained/history set outlives its
  // pool; the next begin lazily rebuilds it. Active/in-flight owners stay put.
  for(submit_resources *r=available_resources;r;r=r->next) {
    vk_shader_destroy(r->shader);r->shader=NULL;
    pool_access_cache_trim(r);
  }
  while(reusable_temporaries) {
    temporary *t=reusable_temporaries;reusable_temporaries=t->next;
    if(t->b.size>reusable_bytes)die("Vulkan idle temporary ownership");
    reusable_bytes-=t->b.size;vk_buffer_destroy(&t->b);free(t);
  }
  if(reusable_bytes)die("Vulkan idle temporary accounting");
  vk_shader_trim_idle_arenas();vk_texture_trim_idle();return 1;
}
#ifdef DM_VULKAN_TEST_EMBED
void vk_test_idle_temporaries(uint64_t *bytes,unsigned *count) {
  *bytes=reusable_bytes;*count=0;
  for(temporary *t=reusable_temporaries;t;t=t->next)(*count)++;
}
void vk_test_temporary_age_policy(int enabled) {
  temporary_soft_limit=enabled?512:64ull<<20;
  temporary_idle_age=enabled?4:1024;
  temporary_prune_interval=enabled?1:256;
  temporary_last_prune=0;
}
#endif

uint32_t vk_memory_type(uint32_t bits, VkMemoryPropertyFlags flags) {
  for (uint32_t i = 0; i < V.mem.memoryTypeCount; ++i)
    if ((bits & (1u << i)) &&
        (V.mem.memoryTypes[i].propertyFlags & flags) == flags)
      return i;
  die("Vulkan memory type unavailable: bits=%#x flags=%#x", bits, flags);
}
static VkResult buffer_try_create(vk_buffer *b,uint64_t size,VkBufferUsageFlags usage,int readback) {
  if(!size)return VK_ERROR_INITIALIZATION_FAILED;
  vk_buffer fresh={0};
  VkBufferCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                           .size = size,
                           .usage = usage};
  VkResult result=vkCreateBuffer(V.device,&ci,NULL,&fresh.handle);
  if(result!=VK_SUCCESS)return result;
  result=readback?vk_memory_buffer_readback(fresh.handle,&fresh.memory,&fresh.map,&fresh.allocation):
      vk_memory_buffer(fresh.handle,&fresh.memory,&fresh.map,&fresh.allocation);
  if(result!=VK_SUCCESS) { vkDestroyBuffer(V.device,fresh.handle,NULL);return result; }
  fresh.properties=vk_memory_properties(fresh.allocation);fresh.readback=readback;
  fresh.size=size;*b=fresh;return VK_SUCCESS;
}
static void buffer_create(vk_buffer *b,uint64_t size,VkBufferUsageFlags usage,int readback) {
  if(!size)die("Vulkan zero-sized buffer");
  VK_CHECK(buffer_try_create(b,size,usage,readback));
  static int logged;
  if(readback&&!logged++) LOG("[vulkan readback] preferred HOST_CACHED actual_flags=%#x invalidate_after_fence=%d",b->properties,
      !(b->properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
}
void vk_buffer_create(vk_buffer *b,uint64_t size,VkBufferUsageFlags usage) { buffer_create(b,size,usage,0); }
void vk_buffer_destroy(vk_buffer *b) {
  if (b->handle)
    vkDestroyBuffer(V.device, b->handle, NULL);
#ifdef DM_VULKAN_SHARED_POOLS
  if(b->shared_import) {if(b->map)vkUnmapMemory(V.device,b->memory);vkFreeMemory(V.device,b->memory,NULL);}
  else
#endif
  vk_memory_free(b->allocation);
  memset(b, 0, sizeof *b);
}
static void *completion_thread(void *unused) {
  (void)unused;
#ifdef __ANDROID__
  extern void crash_thread_init(void);
  crash_thread_init();
  pthread_setname_np(pthread_self(), "dm-gpu-complete");
#endif
  for (;;) {
    pthread_mutex_lock(&completion_mu);
    while (!pending)
      pthread_cond_wait(&completion_cv, &completion_mu);
    submission *s = pending;
    pthread_mutex_unlock(&completion_mu);
    uint64_t started = vk_profile_start();
    int fence_trace=vk_system_trace_begin("Vulkan completion worker fence");
    VkResult waited =
        vkWaitForFences(V.device, 1, &s->fence, VK_TRUE, 30000000000ull);
    vk_system_trace_end(fence_trace);
    if (waited == VK_TIMEOUT)
      die("Vulkan GPU fence timed out after 30s: queue=%llu guest=%llu "
          "frame=%llu draws=%llu",
          (unsigned long long)s->queue_serial, (unsigned long long)s->serial,
          (unsigned long long)s->frame, (unsigned long long)s->draws);
    if (waited != VK_SUCCESS)
      die("Vulkan GPU fence wait: %d", waited);
    vk_system_trace_async_end("Vulkan submit to observed fence",s->queue_serial,s->system_trace);
#ifdef DM_VULKAN_SHARED_POOLS
    // These writes are already in the original host allocation. Make caches
    // visible BEFORE any original completion event/callback/waiter is released.
    while(s->resources->shared_publishes) {
      pool_writeback *w=s->resources->shared_publishes;s->resources->shared_publishes=w->next;
      while(w->ranges) {writeback_range *r=w->ranges;w->ranges=r->next;
        shared_cache(&w->owner->whole.buffer,r->offset,r->end-r->offset,1);free(r);}
      free(w);
    }
#endif
    int publish_trace=vk_system_trace_begin("Vulkan CPU publish");
    vk_profile_end(VK_TIME_GPU_WAIT, started);
    // Non-coherent CPU caches become valid only after the real GPU fence.
    // Keep cached readback allocations separate from all CPU-written uploads.
    for(temporary *t=s->resources->temporaries;t;t=t->next)
      if(t->b.readback&&!(t->b.properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        VK_CHECK(vk_memory_invalidate(t->b.allocation));
        atomic_fetch_add(&readback_invalidations,1);
      }
    gpu_profile_complete(s->resources, s->frame);
    uint64_t gpu_ns = 0;
    if (s->resources->timing_active) {
      uint64_t ticks[2];
      VK_CHECK(vkGetQueryPoolResults(V.device, s->resources->timing_queries, 0, 2,
                                     sizeof ticks, ticks, sizeof(uint64_t),
                                     VK_QUERY_RESULT_64_BIT));
      uint64_t mask =
          V.timestamp_bits == 64 ? UINT64_MAX : (1ull << V.timestamp_bits) - 1;
      gpu_ns = (uint64_t)((long double)((ticks[1] - ticks[0]) & mask) *
                          V.props.limits.timestampPeriod);
    }
    started = vk_profile_start();
    // 发布 GPU 写回，然后才允许事件、栅栏与等待者观察完成。
    // Publish GPU writes before events, completion counters and waiters observe
    // completion.
    uint64_t completed_copy_bytes[5]={0};
    pthread_mutex_lock(&pool_publication_mu);
    for (vk_effect *e = s->effects; e; e = e->next)
      if (e->src) {
        dm_guest_write_prepare(e->dst, e->size);
        memcpy(e->dst, e->src, e->size);
        if(V.options.pool_copy_accounting && e->mode>POOL_COPY_GENERIC && e->mode<=POOL_COPY_KNOWN_SHADOW)
          completed_copy_bytes[e->mode]+=e->size;
      }
    pthread_mutex_unlock(&pool_publication_mu);
    POOL_COPY_ADD(readback_guest_completed_bytes,completed_copy_bytes[POOL_COPY_READBACK_GUEST]);
    POOL_COPY_ADD(readback_shadow_completed_bytes,completed_copy_bytes[POOL_COPY_READBACK_SHADOW]);
    POOL_COPY_ADD(known_guest_completed_bytes,completed_copy_bytes[POOL_COPY_KNOWN_GUEST]);
    POOL_COPY_ADD(known_shadow_completed_bytes,completed_copy_bytes[POOL_COPY_KNOWN_SHADOW]);
    atomic_thread_fence(memory_order_release);
    for (vk_effect *e = s->effects; e;) {
      // Query/event callbacks can also write pool bytes. Pure cleanup and
      // diagnostic file callbacks do not need to exclude upload comparisons.
      if(e->dst) pthread_mutex_lock(&pool_publication_mu);
      if (e->complete)
        e->complete(e->context);
      else if (!e->src) {
        dm_guest_write_prepare(e->dst, sizeof(uint32_t));
        if (!e->mode)
          __atomic_store_n((uint32_t *)e->dst, e->value, __ATOMIC_RELEASE);
        else
          __atomic_fetch_add((uint32_t *)e->dst, e->value, __ATOMIC_RELEASE);
      }
      if(e->dst) pthread_mutex_unlock(&pool_publication_mu);
      vk_effect *next = e->next;
      free(e);
      e = next;
    }
    vk_system_trace_end(publish_trace);
    // 先完成所有 CPU 副作用再释放 GPU 使用引用，不在资源锁内等 GPU。
    // Finish all CPU effects before releasing GPU-use references. Never wait
    // for the GPU while holding the resource lock.
    pthread_mutex_lock(&V.resources);
    for (pool_gpu_access *use = s->resources->gpu_accesses; use; use = use->next) {
      if (!use->owner->references) die("Vulkan pool reference underflow");
      use->owner->references--;
      if (use->owner->access_serial == s->queue_serial) {
        use->owner->access_serial = 0;
        use->owner->recording_access = NULL;
      }
    }
    pthread_cond_broadcast(&pool_cv);
    pthread_mutex_unlock(&V.resources);
    pthread_mutex_lock(&completion_mu);
    if (s->resources->timing_active) {
      unsigned k = s->frame % GPU_TIME_RING;
      if (gpu_times[k].valid && gpu_times[k].frame == s->frame) {
        gpu_times[k].ns += gpu_ns;
        gpu_times[k].pending--;
      }
    }
    vk_profile_end(VK_TIME_EFFECTS, started);
    pending = s->next;
    atomic_fetch_sub(&in_flight, 1);
    if (!pending)
      pending_tail = &pending;
    if (s->serial)
      completed = s->serial;
    queue_completed = s->queue_serial;
    vkDestroyFence(V.device, s->fence, NULL);
    // Wake ordinary GPU/effect waiters before driver cleanup. A completed
    // queue serial does not yet make this resource set ready for reuse.
    pthread_cond_broadcast(&completion_cv);
    pthread_mutex_unlock(&completion_mu);
#ifdef DM_VULKAN_TEST_EMBED
    if(atomic_load(&command_reset_test_enabled)) {
      pthread_mutex_lock(&completion_mu);
      command_reset_test_blocked=s->queue_serial;
      pthread_cond_broadcast(&completion_cv);
      while(atomic_load(&command_reset_test_enabled))
        pthread_cond_wait(&completion_cv,&completion_mu);
      command_reset_test_blocked=0;
      pthread_mutex_unlock(&completion_mu);
    }
#endif
    // This pool and all its primary/secondary buffers are private to s. No
    // recording thread can acquire them until the ready publication below.
    // Do not hold completion/resource/queue locks across the driver reset.
    int reset_trace=vk_system_trace_begin("Vulkan completion command pool reset");
    VK_CHECK(vkResetCommandPool(V.device,s->resources->command_pool,0));
    vk_system_trace_end(reset_trace);
    pthread_mutex_lock(&completion_mu);
    queue_reset_completed=s->queue_serial;
    // Staging, queries and metadata outlive both the GPU fence and callbacks;
    // shared metadata cleanup remains deferred to reclaim_submissions.
    s->next = retired;
    retired = s;
    pthread_cond_broadcast(&completion_cv);
    pthread_mutex_unlock(&completion_mu);
  }
  return NULL;
}
static _Atomic unsigned validation_errors;
unsigned vk_validation_error_count(void) {
  return atomic_load(&validation_errors);
}
static VkBool32 VKAPI_PTR validation_message(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT *data, void *user) {
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    atomic_fetch_add(&validation_errors, 1);
  (void)type;
  (void)user;
  LOG("[vulkan validation] %s: %s", data->pMessageIdName, data->pMessage);
  return VK_FALSE;
}
static int option_number(const char *name,int fallback) {
  const char *value=getenv(name);return value?atoi(value):fallback;
}
static int option_one(const char *name) {
  const char *value=getenv(name);return value&&!strcmp(value,"1");
}
static char *option_string(const char *name) {
  const char *value=getenv(name);if(!value) return NULL;
  char *owned=strdup(value);if(!owned) die("Vulkan startup option allocation");return owned;
}
static void init_options(void) {
  atomic_store(&pool_options_initialized,1);
  temporary_age_reference=option_one("DM_VULKAN_TEMP_CACHE_AGE_REFERENCE");
  vk_startup_options *o=&V.options;
  o->pool_window_bytes=atomic_load(&pool_window_requested);
  if(o->pool_window_bytes==UINT64_MAX) {
    const char *text=getenv("DM_VULKAN_POOL_WINDOW_BYTES");char *end=NULL;
    errno=0;o->pool_window_bytes=text?strtoull(text,&end,10):0;
    if(text && (!*text || *text=='-' || errno || *end))die("Vulkan invalid pool window bytes");
  }
  if(o->pool_window_bytes && (o->pool_window_bytes<4096 || (o->pool_window_bytes&(o->pool_window_bytes-1))))
    die("Vulkan pool window size must be a power of two >=4096");
  LOG("[vulkan] pool backing policy: %s window_bytes=%llu",o->pool_window_bytes?"canonical windows":"whole",(unsigned long long)o->pool_window_bytes);
  const char *account=getenv("DM_VULKAN_POOL_COPY_ACCOUNTING");
  if(account && strcmp(account,"0") && strcmp(account,"1"))
    die("DM_VULKAN_POOL_COPY_ACCOUNTING expects 0 or 1");
  o->pool_copy_accounting=account && !strcmp(account,"1");
  int defer_requested=atomic_load_explicit(&pool_defer_publication_requested,memory_order_acquire);
  o->pool_defer_publication=defer_requested>=0?defer_requested:option_one("DM_VULKAN_DEFER_POOL_PUBLICATION");
  const char *sync_nvn=getenv("DM_NVN_SYNC");
  if(!async_submissions || (sync_nvn && atoi(sync_nvn)))o->pool_defer_publication=0;
  if(o->pool_defer_publication) {
    int pool_ref=atomic_load(&pool_sync_requested),barrier_ref=atomic_load(&submission_barrier_requested);
    if(pool_ref<0)pool_ref=option_one("DM_VULKAN_POOL_SYNC_REFERENCE");
    if(barrier_ref<0)barrier_ref=option_one("DM_VULKAN_SUBMISSION_BARRIER_REFERENCE");
    if(pool_ref || barrier_ref)o->pool_defer_publication=0;
  }
  atomic_store_explicit(&pool_defer_publication_enabled,o->pool_defer_publication,memory_order_release);
  atomic_store_explicit(&pool_copy_enabled,o->pool_copy_accounting,memory_order_relaxed);
  const char *dual=getenv("DM_VULKAN_UPLOAD_DUAL_STORE");
  if(dual && strcmp(dual,"0") && strcmp(dual,"1"))die("DM_VULKAN_UPLOAD_DUAL_STORE expects 0 or 1");
  upload_dual_enabled=dual && !strcmp(dual,"1"); // startup-only, experimental OFF
  LOG("[vulkan] direct upload dual store experimental=%d",upload_dual_enabled);
  o->astc_decode_unorm8=option_one("DM_VULKAN_ASTC_DECODE_UNORM8");
  o->defer_mip_clears=option_one("DM_VULKAN_DEFER_MIP_CLEARS");
  o->stats=!external_profile && getenv("DM_VULKAN_STATS")!=NULL;
  o->validate=getenv("DM_VULKAN_VALIDATE")!=NULL;
  const char *tbdr=getenv("DM_NVN_TBDR");o->tbdr=!(tbdr&&!strcmp(tbdr,"0"));
  o->load_probe=option_number("DM_NVN_LOADPROBE",0);
  o->freeze=option_number("DM_NVN_TEST_FRZ",0);
  o->freeze_negative=getenv("DM_NVN_TEST_FRZ_NEG")!=NULL;
  const char *runtime=getenv("DM_RT_AB");o->runtime_forced=runtime!=NULL;o->runtime=runtime?atoi(runtime):0;
  o->runtime_toggle=option_number("DM_RT_AB_TOGGLE",0);
  const char *divisor=getenv("DM_TEST_VULKAN_VERTEX_DIVISOR_FALLBACK");
  o->vertex_divisor_fallback=divisor&&divisor[0]=='1';
  o->store_reference=option_one("DM_VULKAN_STORE_REFERENCE");
  o->store_check=option_one("DM_VULKAN_STORE_CHECK")||option_one("DM_VULKAN_VALIDATE");
  o->ruler=getenv("DM_NVN_RULER")!=NULL;
  o->extrap_no_pipeline=getenv("DM_RT_TEST_NOPSO")!=NULL;
  o->extrap_mask=getenv("DM_RT_TEST_MASK")!=NULL;
  o->gpu_profile=getenv("DM_VULKAN_GPU_PROFILE")!=NULL;
  o->pool_ownership_reference=option_one("DM_VULKAN_POOL_OWNERSHIP_REFERENCE");
  o->pool_access_cache_reference=option_one("DM_VULKAN_POOL_ACCESS_CACHE_REFERENCE");
  o->pool_mirror_reference=atomic_load(&pool_mirror_requested);
  if(o->pool_mirror_reference<0) o->pool_mirror_reference=option_one("DM_VULKAN_EAGER_POOL_MIRRORS");
  const char *sets=getenv("DM_TEST_VULKAN_DESCRIPTOR_POOL_SETS");
  unsigned long max_sets=sets?strtoul(sets,NULL,10):2048;
  if(!max_sets||max_sets>2048) die("Vulkan test descriptor capacity");
  o->descriptor_sets=max_sets;
  if(!o->descriptor_sets||o->descriptor_sets>2048) die("Vulkan test descriptor capacity");
  o->shader_root=option_string("DM_VULKAN_SHADERS");
  o->pipeline_cache_directory=option_string("DM_VULKAN_PIPELINE_CACHE_DIR");
  if(!o->pipeline_cache_directory) o->pipeline_cache_directory=option_string("DM_TEMP");
  const char *frame=getenv("DM_VULKAN_TRACE_FRAME");o->trace_frame_set=frame!=NULL;
  o->trace_frame=frame?strtoull(frame,NULL,10):0;
  o->trace_directory=option_string("DM_VULKAN_TRACE_DIR");
  V.trace_frame_set=o->trace_frame_set;V.trace_frame=o->trace_frame;
  if(o->trace_frame_set && o->trace_directory) {
    V.trace_directory=strdup(o->trace_directory);
    if(!V.trace_directory) die("Vulkan startup trace directory allocation");
  }
  o->frame_dump=option_string("DM_VULKAN_FRAME_DUMP");
  const char *at=getenv("DM_VULKAN_FRAME_DUMP_AT");o->frame_dump_at=at?strtoull(at,NULL,10):1;
}
static void init_device(void) {
  vk_loader_initialize();
  const char *async = getenv("DM_VULKAN_ASYNC");
  async_submissions = async && !strcmp(async, "1");
  LOG("[vulkan] submission overlap: %s", async_submissions ? "enabled" : "serial reference");
  const char *external = getenv("DM_VULKAN_EXTERNAL_PROFILE");
  external_profile = external && !strcmp(external, "1");
  if (external_profile) {
    unsetenv("DM_VULKAN_STATS");
    LOG("[vulkan] external profiling: custom CPU/GPU timers disabled; NVN queries retained");
  }
  init_options();
  VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                           .pApplicationName = "d2r-mobile-native",
                           .apiVersion = VK_API_VERSION_1_1};
  VkInstanceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                             .pApplicationInfo = &app};
  const char *layer = "VK_LAYER_KHRONOS_validation",
             *extension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
  const char *extensions[4];
  uint32_t extension_count = 0;
#ifdef __ANDROID__
  if (vk_window_attached()) {
    extensions[extension_count++] = VK_KHR_SURFACE_EXTENSION_NAME;
    extensions[extension_count++] = VK_KHR_ANDROID_SURFACE_EXTENSION_NAME;
  }
#endif
  VkDebugUtilsMessengerCreateInfoEXT debug = {
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
      .pfnUserCallback = validation_message};
  VkValidationFeatureEnableEXT sync_feature =
      VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
  VkValidationFeaturesEXT validation = {
      .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
      .enabledValidationFeatureCount = 1,
      .pEnabledValidationFeatures = &sync_feature};
  if (getenv("DM_VULKAN_VALIDATE")) {
    uint32_t count = 0;
    VK_CHECK(vkEnumerateInstanceLayerProperties(&count, NULL));
    VkLayerProperties *layers = calloc(count ? count : 1, sizeof *layers);
    VK_CHECK(vkEnumerateInstanceLayerProperties(&count, layers));
    int found = 0;
    for (uint32_t i = 0; i < count; i++) {
      LOG("[vulkan] available layer %s", layers[i].layerName);
      if (!strcmp(layers[i].layerName, layer))
        found = 1;
    }
    free(layers);
    if (!found)
      die("Vulkan validation layer not discoverable in this process");
    ci.enabledLayerCount = 1;
    ci.ppEnabledLayerNames = &layer;
    extensions[extension_count++] = extension;
    if (getenv("DM_VULKAN_SYNC_VALIDATE")) {
      extensions[extension_count++] = VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME;
      debug.pNext = &validation;
      LOG("[vulkan] synchronization validation requested");
    }
    ci.pNext = &debug;
  }
  ci.enabledExtensionCount = extension_count;
  ci.ppEnabledExtensionNames = extensions;
  VK_CHECK(vkCreateInstance(&ci, NULL, &V.instance));
  volkLoadInstance(V.instance);
  vk_loader_check_instance(V.instance,vk_window_attached());
  vk_window_surface_create();
  if (getenv("DM_VULKAN_VALIDATE")) {
    VkDebugUtilsMessengerEXT messenger;
    PFN_vkCreateDebugUtilsMessengerEXT create = (void *)vkGetInstanceProcAddr(
        V.instance, "vkCreateDebugUtilsMessengerEXT");
    if (!create)
      die("Vulkan debug-utils unavailable");
    VK_CHECK(create(V.instance, &debug, NULL, &messenger));
  }
  uint32_t n = 0;
  VK_CHECK(vkEnumeratePhysicalDevices(V.instance, &n, NULL));
  if (!n)
    die("Vulkan has no physical devices");
  VkPhysicalDevice *devices = calloc(n, sizeof *devices);
  VK_CHECK(vkEnumeratePhysicalDevices(V.instance, &n, devices));
  for (uint32_t d = 0; d < n && !V.physical; ++d) {
    uint32_t qn;
    vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &qn, NULL);
    VkQueueFamilyProperties *q = calloc(qn, sizeof *q);
    vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &qn, q);
    for (uint32_t i = 0; i < qn; ++i)
      if ((q[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
          (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
        if (V.surface) {
          VkBool32 present;
          VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(devices[d], i,
                                                        V.surface, &present));
          if (!present)
            continue;
        }
        V.physical = devices[d];
        V.queue_family = i;
        V.timestamp_bits = q[i].timestampValidBits;
        int present_mode=vk_window_present_worker_requested_mode();
        V.queue_count=(present_mode==1 && async_submissions && q[i].queueCount>1)?2:1;
        V.present_worker=async_submissions && (present_mode==2 || V.queue_count==2);
        break;
      }
    free(q);
  }
  free(devices);
  if (!V.physical)
    die("Vulkan has no graphics+compute queue");
  vkGetPhysicalDeviceProperties(V.physical, &V.props);
  if(V.options.pool_window_bytes && (V.options.pool_window_bytes<V.props.limits.minUniformBufferOffsetAlignment ||
      V.options.pool_window_bytes<V.props.limits.minStorageBufferOffsetAlignment ||
      V.options.pool_window_bytes<V.props.limits.minTexelBufferOffsetAlignment))die("Vulkan pool window smaller than device offset alignment");
  vkGetPhysicalDeviceMemoryProperties(V.physical, &V.mem);
  vk_format_properties_init();
  VkPhysicalDeviceShaderDrawParametersFeatures draw_parameters = {
      .sType =
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES};
  VkPhysicalDeviceFeatures2 features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &draw_parameters};
  vkGetPhysicalDeviceFeatures2(V.physical, &features);
  V.features = features.features;
  V.shader_draw_parameters = draw_parameters.shaderDrawParameters;
  float priorities[2]={1,1};
  VkDeviceQueueCreateInfo q = {.sType =
                                   VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                               .queueFamilyIndex = V.queue_family,
                               .queueCount = V.queue_count,
                               .pQueuePriorities = priorities};
  VkDeviceCreateInfo dc = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                           .pNext = &draw_parameters,
                           .queueCreateInfoCount = 1,
                           .pQueueCreateInfos = &q,
                           .pEnabledFeatures = &V.features};
#ifdef DM_VULKAN_SHARED_POOLS
  const char *shared=getenv("DM_VULKAN_SHARED_POOLS");
  if(shared && strcmp(shared,"0") && strcmp(shared,"1"))die("DM_VULKAN_SHARED_POOLS expects0or1");
  shared_requested=shared && !strcmp(shared,"1");
#ifdef DM_VULKAN_TEST_EMBED
  shared_cache_fixture_records=getenv("DM_FIXED_SHARED_BUFFER_FIXTURE")!=NULL;
#endif
#endif
  const char *device_extensions[16];
  int have_divisor = 0, have_timeline = 0, have_float16 = 0;
  int have_driver_properties=0;
  VkPhysicalDeviceShaderFloat16Int8FeaturesKHR float16 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES_KHR};
  VkPhysicalDeviceTimelineSemaphoreFeaturesKHR timeline = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR};
  VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT divisor = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT};
  uint32_t extension_count_device = 0;
  VK_CHECK(vkEnumerateDeviceExtensionProperties(V.physical, NULL,
                                                &extension_count_device, NULL));
  VkExtensionProperties *available = calloc(
      extension_count_device ? extension_count_device : 1, sizeof *available);
  if (!available)
    die("Vulkan device extension allocation");
  VK_CHECK(vkEnumerateDeviceExtensionProperties(
      V.physical, NULL, &extension_count_device, available));
  for (uint32_t i = 0; i < extension_count_device; i++) {
    if (!strcmp(available[i].extensionName,VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME))
      have_driver_properties=1;
#ifdef DM_VULKAN_SHARED_POOLS
    if(shared_requested && !strcmp(available[i].extensionName,VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME))shared_extension=1;
#endif
    if (V.options.astc_decode_unorm8 &&
        !strcmp(available[i].extensionName,VK_EXT_ASTC_DECODE_MODE_EXTENSION_NAME))
      V.astc_decode_mode=1;
    if (!strcmp(available[i].extensionName, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME))
      have_timeline = 1;
    if (!strcmp(available[i].extensionName,VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME))
      have_float16 = 1;
    if (!strcmp(available[i].extensionName,VK_EXT_SHADER_STENCIL_EXPORT_EXTENSION_NAME))
      V.stencil_export=1;
    // The backend requests API 1.1. A newer physical API alone does not
    // enable this enum: negotiate the advertised device extension explicitly.
    if (!strcmp(available[i].extensionName,VK_EXT_LOAD_STORE_OP_NONE_EXTENSION_NAME))
      V.load_store_op_none=1;
    if (V.surface && !strcmp(available[i].extensionName,
                             VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME))
      V.display_timing = 1;
    if (!strcmp(available[i].extensionName,
                VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME))
      V.sampler_mirror_clamp = 1;
    if (!strcmp(available[i].extensionName,
                VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME) &&
        available[i].specVersion >= 3)
      have_divisor = 1;
  }
#if defined(DM_VULKAN_SHARED_POOLS) && defined(DM_VULKAN_TEST_EMBED)
  if(getenv("DM_FIXED_SHARED_CAPABILITY_DENY"))shared_extension=0; // strict negative only
#endif
  free(available);
  vk_loader_check_device(V.physical,have_driver_properties);
  if (have_divisor) {
    VkPhysicalDeviceFeatures2 query = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &divisor};
    vkGetPhysicalDeviceFeatures2(V.physical, &query);
    if (divisor.vertexAttributeInstanceRateDivisor) {
      VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT limits = {
          .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES_EXT};
      VkPhysicalDeviceProperties2 props = {
          .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &limits};
      vkGetPhysicalDeviceProperties2(V.physical, &props);
      V.max_vertex_divisor = limits.maxVertexAttribDivisor;
      divisor.vertexAttributeInstanceRateZeroDivisor = VK_FALSE;
      draw_parameters.pNext = &divisor;
    }
  }
  if (have_timeline) {
    VkPhysicalDeviceFeatures2 query = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &timeline};
    vkGetPhysicalDeviceFeatures2(V.physical, &query);
    if (timeline.timelineSemaphore) {
      V.timeline_semaphore = 1;
      timeline.pNext = draw_parameters.pNext;
      draw_parameters.pNext = &timeline;
    }
  }
  if(have_float16) {
    VkPhysicalDeviceFeatures2 query={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&float16};
    vkGetPhysicalDeviceFeatures2(V.physical,&query);
    if(float16.shaderFloat16) {
      V.shader_float16=VK_TRUE;
      // Arithmetic only. Keep int8 and all 16-bit interface/storage features
      // disabled; buffers, push constants, varyings and outputs retain ABI.
      float16.shaderInt8=VK_FALSE;
      float16.pNext=draw_parameters.pNext;draw_parameters.pNext=&float16;
    }
  }
  dc.ppEnabledExtensionNames = device_extensions;
  if (V.surface) {
    device_extensions[dc.enabledExtensionCount++] =
        VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    if (V.display_timing)
      device_extensions[dc.enabledExtensionCount++] =
          VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME;
    LOG("[vulkan] actual presentation timing: %s",
        V.display_timing ? "enabled" : "unavailable");
  }
  if (V.sampler_mirror_clamp)
    device_extensions[dc.enabledExtensionCount++] =
        VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME;
  if (V.max_vertex_divisor)
    device_extensions[dc.enabledExtensionCount++] =
        VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME;
  if (V.stencil_export)
    device_extensions[dc.enabledExtensionCount++] = VK_EXT_SHADER_STENCIL_EXPORT_EXTENSION_NAME;
  if (V.load_store_op_none)
    device_extensions[dc.enabledExtensionCount++] = VK_EXT_LOAD_STORE_OP_NONE_EXTENSION_NAME;
  if (V.timeline_semaphore)
    device_extensions[dc.enabledExtensionCount++] = VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME;
  if(V.astc_decode_mode)
    device_extensions[dc.enabledExtensionCount++]=VK_EXT_ASTC_DECODE_MODE_EXTENSION_NAME;
  LOG("[vulkan] ASTC UNORM8 decode: %s (LDR-content proof required)",
      V.astc_decode_mode?"experimental":"disabled or unsupported");
  if(V.shader_float16)
    device_extensions[dc.enabledExtensionCount++]=VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME;
  LOG("[vulkan] shader stencil export: %s",V.stencil_export?"enabled":"unavailable");
  LOG("[vulkan] read-only attachment store NONE: %s",V.load_store_op_none?"VK_EXT_load_store_op_none":"unavailable (STORE fallback)");
#ifdef DM_VULKAN_SHARED_POOLS
  if(shared_extension)device_extensions[dc.enabledExtensionCount++]=VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME;
#endif
  VK_CHECK(vkCreateDevice(V.physical, &dc, NULL, &V.device));
  volkLoadDevice(V.device);
  LOG("[vulkan] explicit Float16 arithmetic: %s (32-bit interfaces)",V.shader_float16?"enabled":"unavailable");
  VK_CHECK(vk_memory_init(V.instance, V.physical, V.device));
  if (V.timeline_semaphore) {
    V.signal_timeline = (PFN_vkSignalSemaphoreKHR)vkGetDeviceProcAddr(V.device, "vkSignalSemaphoreKHR");
    if (!V.signal_timeline) die("Vulkan timeline semaphore entrypoint unavailable");
  }
  LOG("[vulkan] timeline semaphore: %s", V.timeline_semaphore ? "enabled" : "unavailable");
  vkGetDeviceQueue(V.device, V.queue_family, 0, &V.queue);
  V.present_queue=V.queue;
  if(V.queue_count>1)vkGetDeviceQueue(V.device,V.queue_family,1,&V.present_queue);
  LOG("[vulkan] queue family=%u queues=%u dedicated WSI worker=%s present queue=%s",V.queue_family,V.queue_count,V.present_worker?"enabled":"reference/unavailable",V.present_queue==V.queue?"render":"separate");
#ifdef DM_VULKAN_SHARED_POOLS
  shared_device_probe(); // once, before any NVN registration or worker
  if(shared_active && (V.options.pool_window_bytes || V.options.pool_mirror_reference || V.options.pool_defer_publication))
    die("Fixed shared mode conflicts with legacy window/mirror/defer options");
#endif
  vk_pipeline_cache_init();
  pthread_t t;
  if (pthread_create(&t, NULL, completion_thread, NULL))
    die("Vulkan completion worker");
  pthread_detach(t);
  LOG("[vulkan] device=%s api=%u.%u.%u shaderInt64=%u", V.props.deviceName,
      VK_VERSION_MAJOR(V.props.apiVersion),
      VK_VERSION_MINOR(V.props.apiVersion),
      VK_VERSION_PATCH(V.props.apiVersion), V.features.shaderInt64);
  LOG("[vulkan] instance vertex divisor max=%u", V.max_vertex_divisor);
}
void mtl_init(void) { pthread_once(&init_once, init_device); }
// Caller holds resources. Image-only pools have no VkBuffer users; allocate
// their coherent mirror/shadow only on an actual buffer address or writeback.
// Publish guest/shadow as one initial snapshot, excluding completion copies.
// The caller owns resources. An unset bit proves this logical page has never
// been exposed through the pool buffer, so no earlier GPU use can race its
// initial copy. This does not infer ownership from dirty generations.
static void pool_initialize_range(pool *p,uint64_t off,uint64_t size) {
  if(!size) return;
#ifdef DM_VULKAN_SHARED_POOLS
  if(p->shared)return;
#endif
  uint64_t first=off>>GPU_ACCESS_SHIFT,last=(off+size-1)>>GPU_ACCESS_SHIFT;
  uint64_t end_page=last+1;
  // Every completed scan below initialized all requested pages. These bits
  // never clear during this pool's lifetime; CPU edits still take the normal
  // dirty-upload path, and GPU ownership/dependencies are handled separately.
  if(first>=p->initialized_first_page && end_page<=p->initialized_end_page)return;
  for(uint64_t word=first/64;word<=last/64;word++) {
    unsigned low=word==first/64?first%64:0,high=word==last/64?last%64:63;
    uint64_t mask=(UINT64_MAX<<low)&(UINT64_MAX>>(63-high));
    uint64_t missing=mask&~p->initialized_pages[word];
    if(!missing) continue;
    pthread_mutex_lock(&pool_publication_mu);
    while(missing) {
      unsigned bit=__builtin_ctzll(missing),end=bit;
      while(end<63 && (missing&(UINT64_C(1)<<(end+1)))) end++;
      uint64_t lo=(word*64+bit)<<GPU_ACCESS_SHIFT,hi=(word*64+end+1)<<GPU_ACCESS_SHIFT;
      if(hi>p->size) hi=p->size;
      pool_spans_copy_from_guest(p,lo,hi-lo);
      memcpy(p->shadow+lo,p->guest+lo,hi-lo);
      POOL_COPY_ADD(initial_mirror_bytes,hi-lo);
      POOL_COPY_ADD(initial_shadow_bytes,hi-lo);
      // The unset initialized bit proves this page has never been exposed to
      // a GPU binding, including before a canonical backing migration.
      pool_cpu_synced(p,lo,hi-lo,UINT64_MAX);
      p->initialized_bytes+=hi-lo;
      uint64_t run=(UINT64_MAX<<bit)&(UINT64_MAX>>(63-end));
      p->initialized_pages[word]|=run;missing&=~run;
    }
    pthread_mutex_unlock(&pool_publication_mu);
  }
  // Union only intersecting or touching proven spans. Disjoint spans must
  // replace the cache entry, since pages in their gap may still be untouched.
  if(p->initialized_end_page && first<=p->initialized_end_page &&
      end_page>=p->initialized_first_page) {
    if(first<p->initialized_first_page)p->initialized_first_page=first;
    if(end_page>p->initialized_end_page)p->initialized_end_page=end_page;
  } else {
    p->initialized_first_page=first;p->initialized_end_page=end_page;
  }
}
static void pool_materialize_metadata(pool *p) {
#ifdef DM_VULKAN_SHARED_POOLS
  if(p->shared && p->page_upload_stamps)return;
#endif
  if(p->shadow)return;
  uint64_t size=p->size;
  p->page_upload_stamps=calloc(((size-1)>>GPU_ACCESS_SHIFT)+1,sizeof(uint64_t));
  if(!p->page_upload_stamps) die("Vulkan pool page upload generations");
  p->initialized_pages=calloc(((size-1)>>(GPU_ACCESS_SHIFT+6))+1,sizeof(uint64_t));
  if(!p->initialized_pages) die("Vulkan pool initial page ownership");
#ifdef DM_VULKAN_SHARED_POOLS
  if(p->shared)return; // hazard bitmaps stay; no shadow allocation/content scan
#endif
  p->shadow = calloc(1, size);
  if (!p->shadow)
    die("Vulkan pool shadow allocation: %llu", (unsigned long long)size);
}
static void pool_materialize(pool *p) {
#ifdef DM_VULKAN_SHARED_POOLS
  if(p->shared) {shared_pool_materialize(p);return;}
#endif
  // Explicit range reservation may create physical buffers without exposing
  // CPU pages. Initialize this metadata only at the first actual lookup.
  pool_materialize_metadata(p);
  if(p->backings)return;
  pool_span_create(p,&p->whole,0,p->size);p->backings=&p->whole;
  if(V.options.pool_mirror_reference) pool_initialize_range(p,0,p->size);
}
static void pool_add_metadata(const void *mem, uint64_t size, uint32_t flags, int known) {
  mtl_init();
  // A nonwrapping interval plus the overlap rejection below makes a retained
  // pool's nonempty address coverage unambiguous for this registry lifetime.
  if(size>UINTPTR_MAX-(uintptr_t)mem)die("Vulkan pool address range overflow");
  pool *p = calloc(1, sizeof *p);
  if (!p || !size) die("Vulkan pool allocation");
  p->guest = (uint8_t *)mem;p->size = size;
  p->nvn_flags=flags;p->nvn_flags_known=known;
#ifdef DM_VULKAN_SHARED_POOLS
  p->shared=shared_active;
  if(p->shared && (!known || (uintptr_t)mem%shared_alignment || size%shared_alignment || nvn_dirty_tracked(mem,size)))
    die("Shared Pool invalid host range: no online legacy fallback");
#endif
  p->cpu_authority_tracking=V.options.pool_window_bytes!=0 ||
      (V.options.pool_defer_publication && known && (flags&7u)==NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS);
  pthread_mutex_lock(&V.resources);
  for (pool *v = pools; v; v = v->next)
    if ((uintptr_t)mem < (uintptr_t)v->guest + v->size &&
        (uintptr_t)mem + size > (uintptr_t)v->guest)
      die("Vulkan overlapping pools");
#ifdef DM_VULKAN_SHARED_POOLS
  if(!p->shared)
#endif
  if(V.options.pool_mirror_reference) pool_materialize(p);
  p->next = pools;
  pools = p;
  uint64_t prior_identity=atomic_fetch_add_explicit(&pool_registry_epoch,1,memory_order_release);
  if(prior_identity==UINT64_MAX)
    die("Vulkan pool registry epoch overflow");
  p->identity=prior_identity+1;
  pool_adds++;
  pthread_mutex_unlock(&V.resources);
}
void mtl_pool_add(const void *mem, uint64_t size) { pool_add_metadata(mem,size,0,0); }
void mtl_pool_add_flags(const void *mem, uint64_t size, int flags) {
  pool_add_metadata(mem,size,(uint32_t)flags,1);
}
#ifdef DM_VULKAN_TEST_EMBED
// Owned functional metadata assertion only; not used by the game or profiler.
int vk_test_pool_flags(uint64_t address,uint32_t *flags,int *known,uint64_t *identity) {
  pthread_mutex_lock(&V.resources);pool *p=pool_find(address,1);
  if(p) { *flags=p->nvn_flags;*known=p->nvn_flags_known;*identity=p->identity; }
  pthread_mutex_unlock(&V.resources);return p!=NULL;
}
#endif

#if defined(DM_VULKAN_SHARED_POOLS) && defined(DM_VULKAN_TEST_EMBED)
static pthread_mutex_t shared_close_test_mu=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t shared_close_test_cv=PTHREAD_COND_INITIALIZER;
static int shared_close_test_armed,shared_close_test_entered;
void vk_test_shared_close_arm(void) {pthread_mutex_lock(&shared_close_test_mu);shared_close_test_armed=1;shared_close_test_entered=0;pthread_mutex_unlock(&shared_close_test_mu);}
void vk_test_shared_close_wait(void) {pthread_mutex_lock(&shared_close_test_mu);while(!shared_close_test_entered)pthread_cond_wait(&shared_close_test_cv,&shared_close_test_mu);pthread_mutex_unlock(&shared_close_test_mu);}
#endif
void mtl_pool_del(const void *mem) {
#if defined(DM_VULKAN_SHARED_POOLS) && defined(DM_VULKAN_TEST_EMBED)
  if(shared_active) {pthread_mutex_lock(&shared_close_test_mu);if(shared_close_test_armed) {shared_close_test_armed=0;shared_close_test_entered=1;pthread_cond_broadcast(&shared_close_test_cv);}pthread_mutex_unlock(&shared_close_test_mu);}
#endif
  mtl_finish();
  pthread_mutex_lock(&V.resources);
  pool **pp = &pools;
  while (*pp && (*pp)->guest != mem)
    pp = &(*pp)->next;
  if (!*pp)
    die("Vulkan deleting unknown pool");
  pool *p = *pp;
#ifdef DM_VULKAN_SHARED_POOLS
  p->shared_closing=1;
#endif
  *pp = p->next;
  if(atomic_fetch_add_explicit(&pool_registry_epoch,1,memory_order_release)==UINT64_MAX)
    die("Vulkan pool registry epoch overflow");
  while (p->references) pthread_cond_wait(&pool_cv, &V.resources);
  pool_spans_destroy(p);
  free(p->cpu_exposure);
  free(p->cpu_authority);
  free(p->shadow);
  free(p->page_upload_stamps);
  free(p->initialized_pages);
  free(p);
  pthread_mutex_unlock(&V.resources);
}
static pool *pool_find(uint64_t addr, uint64_t size) {
  for (pool *p = pools; p; p = p->next)
    if (addr >= (uintptr_t)p->guest && addr - (uintptr_t)p->guest <= p->size &&
        size <= p->size - (addr - (uintptr_t)p->guest))
      return p;
  return NULL;
}
int vk_pool_span_query(uint64_t addr,uint64_t size,vk_pool_span_info *out) {
  vk_pool_span_info value={0};int status=VK_POOL_SPAN_NONE;
  pthread_mutex_lock(&V.resources);
  pool *p=size?pool_find(addr,size):NULL;
  if(p) {
    value.pool_identity=p->identity;value.pool_base=(uintptr_t)p->guest;value.pool_size=p->size;
    value.logical_begin=addr-value.pool_base;value.logical_end=value.logical_begin+size;value.size=size;
    pool_span_piece span;
    if(!p->backings) status=VK_POOL_SPAN_UNBACKED;
    else if(!pool_span_resolve_locked(p,value.logical_begin,size,&span)) status=VK_POOL_SPAN_CROSSING;
    else {
      status=VK_POOL_SPAN_SINGLE;value.logical_begin=span.backing->begin;value.logical_end=span.backing->end;
      value.backing_generation=span.backing->generation;value.local_offset=span.offset;
      value.handle=span.backing->buffer.handle;
    }
  }
  pthread_mutex_unlock(&V.resources);
  if(out)*out=value;
  return status;
}
#include "nvn_vk_pool_windows.inc"
#ifdef DM_VULKAN_TEST_EMBED
// Only a fresh unused owned fixture pool may acquire two real immutable
// backings. This does not migrate, discard initialized bytes or bypass refs.
static int pool_test_split_backings(uint64_t address,uint64_t split,uint64_t tail_begin) {
  int result=0;pthread_mutex_lock(&V.resources);
  pool *p=pool_find(address,1);
  if(p && (uintptr_t)p->guest==address && !p->backings && !p->references &&
      !atomic_load(&p->initialized_bytes) && split && split<=tail_begin && tail_begin<p->size &&
      !(split&4095) && !(tail_begin&4095)) {
    pool_backing_span *tail=calloc(1,sizeof *tail);if(!tail)die("Vulkan fixture span allocation");
    pool_materialize_metadata(p);pool_span_create(p,&p->whole,0,split);
    pool_span_create(p,tail,tail_begin,p->size);p->whole.next=tail;p->backings=&p->whole;result=1;
  }
  pthread_mutex_unlock(&V.resources);return result;
}
// Owned death fixtures retain the pool and read only its lock-free atomic
// byte count from SIGABRT; never traverse the registry inside a signal handler.
const _Atomic uint64_t *vk_test_pool_initialized_counter(uint64_t address) {
  pthread_mutex_lock(&V.resources);pool *p=pool_find(address,1);
  const _Atomic uint64_t *result=p?&p->initialized_bytes:NULL;
  pthread_mutex_unlock(&V.resources);return result;
}
VkBuffer vk_test_pool_locked_span_hint(uint64_t address) {
  pthread_mutex_lock(&V.resources);pool *p=pool_find(address,1);
  VkBuffer result=p&&p->locked_span_hint?p->locked_span_hint->buffer.handle:VK_NULL_HANDLE;
  pthread_mutex_unlock(&V.resources);return result;
}
int vk_test_pool_split_backings(uint64_t address,uint64_t split) {
  return pool_test_split_backings(address,split,split);
}
int vk_test_pool_gap_backings(uint64_t address,uint64_t first_end,uint64_t tail_begin) {
  return pool_test_split_backings(address,first_end,tail_begin);
}
#endif
int vk_pool_range_offset(uint64_t addr,uint64_t size,uint64_t *offset) {
  pthread_mutex_lock(&V.resources);
  pool *p=pool_find(addr,size);
  if(p) *offset=addr-(uintptr_t)p->guest;
  pthread_mutex_unlock(&V.resources);
  return p!=NULL;
}
static int pool_address(uint64_t addr,uint64_t len,void **buf,uint64_t *off,VkPipelineStageFlags stages,int initial_contents) {
  vk_mip_clear_buffer_access(addr,len);
  // Admission is checked by the same exact physical resolution used below,
  // before initialization may copy CPU bytes into any backing. A missing
  // window is never permission to stitch or silently create whole storage.
  int guarded=V.options.pool_window_bytes && len && !pool_maintenance;
  submit_resources *r=recording_resources;
  if(r && len && !r->pool_lookup_reference) {
    uint64_t epoch=atomic_load_explicit(&pool_registry_epoch,memory_order_acquire);
    if(r->pool_lookup_epoch!=epoch) {
      r->pool_lookup_count=r->pool_lookup_next=0;r->pool_lookup_epoch=epoch;
    }
    for(unsigned k=0;k<r->pool_lookup_count;k++) {
      pool *p=r->pool_lookups[k].owner;
      uint64_t base=(uintptr_t)p->guest;
      if(addr<base || addr-base>p->size || len>p->size-(addr-base))continue;
      // This exact context already owns use->owner's reference. Pool removal
      // waits for that reference through true fence and CPU effects; no stale
      // pool/list index or completion-mutated recording_access is consulted.
      uint64_t logical=addr-base;
      pool_span_piece span;
      if(!pool_span_resolve_retained(p,r->pool_lookups[k].span_hint,logical,len,&span)) {
        if(guarded)
          die("Vulkan missing operation pool admission address=%#llx bytes=%llu depth=%u",
              (unsigned long long)addr,(unsigned long long)len,pool_operation_depth);
        die("Vulkan pool range requires operation preflight: identity=%llu offset=%llu bytes=%llu",
            (unsigned long long)p->identity,(unsigned long long)logical,(unsigned long long)len);
      }
      r->pool_lookups[k].span_hint=span.backing;
      pool_initialize_range(p,logical,len);
      *buf=&span.backing->buffer;*off=span.offset;
      record_pool_range(r->pool_lookups[k].use,logical,len,stages,initial_contents);
      pool_lookup_hits++;return 1;
    }
  }
  pthread_mutex_lock(&V.resources);
  pool *p = pool_find(addr, len);
  // The former admission query returned unknown guarded ranges before the
  // slow counter. Keep that counter's meaning while doing only one lookup.
  if(p || !guarded)pool_lookup_slow++;
  if (p) {
    uint64_t logical=addr-(uintptr_t)p->guest;
    pool_span_piece span;
    if(guarded && p->backings) {
      if(!pool_span_resolve_locked(p,logical,len,&span))
        die("Vulkan missing operation pool admission address=%#llx bytes=%llu depth=%u",
            (unsigned long long)addr,(unsigned long long)len,pool_operation_depth);
      // Reservation can allocate buffers without initializing CPU metadata.
      pool_materialize_metadata(p);
    } else {
      if(guarded && pool_operation_depth)
        die("Vulkan missing operation pool admission address=%#llx bytes=%llu depth=%u",
            (unsigned long long)addr,(unsigned long long)len,pool_operation_depth);
      // Preserve the original unbacked depth-zero, maintenance, whole-mode
      // and zero-length address-only materialization behavior.
      pool_materialize(p);
      span=pool_span_require_locked(p,logical,len);
    }
    pool_initialize_range(p,logical,len);
    *buf=&span.backing->buffer;*off=span.offset;
    pool_gpu_access *use=record_pool_use(p,logical,len,stages,initial_contents);
    if(r && use && !r->pool_lookup_reference) {
      uint64_t epoch=atomic_load_explicit(&pool_registry_epoch,memory_order_relaxed);
      if(r->pool_lookup_epoch!=epoch) {
        r->pool_lookup_count=r->pool_lookup_next=0;r->pool_lookup_epoch=epoch;
      }
      unsigned k=r->pool_lookup_next++%8;
      r->pool_lookups[k].owner=p;r->pool_lookups[k].use=use;
      r->pool_lookups[k].span_hint=span.backing;
      if(r->pool_lookup_count<8)r->pool_lookup_count++;
    }
  }
  pthread_mutex_unlock(&V.resources);
  return p != NULL;
}
int vk_pool_address_stage(uint64_t addr,uint64_t len,void **buf,uint64_t *off,VkPipelineStageFlags stages) {
  return pool_address(addr,len,buf,off,stages,1);
}
// Only exact transfer destinations use this lookup. Shader writable bindings
// remain ordinary reads: a writable descriptor does not prove full coverage.
int vk_pool_address_overwrite(uint64_t addr,uint64_t len,void **buf,uint64_t *off,VkPipelineStageFlags stages) {
  return pool_address(addr,len,buf,off,stages,0);
}
// Call after recording the exact write, including any nested render-pass end
// replay. Never publish this proof at lookup time, before the command exists.
void vk_pool_overwritten(uint64_t addr,uint64_t size) {
  if(!size) return;
  pthread_mutex_lock(&V.resources);
  pool *p=pool_find(addr,size);
  if(!p || !recording_resources)
    die("Vulkan complete overwrite outside recording pool");
  uint64_t off=addr-(uintptr_t)p->guest,end=off+size;
  // Pass-end replay can internally submit between the lookup and exact write.
  // Associate the physical write with the command buffer that actually owns it.
  record_pool_use(p,off,size,VK_PIPELINE_STAGE_TRANSFER_BIT,0);
  pool_gpu_access *use=p->recording_access;
  uint64_t first=off>>GPU_ACCESS_SHIFT,last=(end-1)>>GPU_ACCESS_SHIFT;
  for(uint64_t page=first;page<=last;page++) {
    uint64_t lo=page<<GPU_ACCESS_SHIFT,hi=lo+(1ull<<GPU_ACCESS_SHIFT);
    if(hi>p->size) hi=p->size;
    uint64_t bit=UINT64_C(1)<<(page%64),word=page/64;
    if(off<=lo && end>=hi && !V.options.pool_ownership_reference)
      use->overwritten_pages[word]|=bit;
    else if(!(use->overwritten_pages[word]&bit)) use->upload_pages[word]|=bit;
  }
  pthread_mutex_unlock(&V.resources);
}
int mtl_addr(uint64_t addr,uint64_t len,void **buf,uint64_t *off) {
  return vk_pool_address_stage(addr,len,buf,off,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
}
int vk_pool_read(uint64_t address, size_t size, void *out) {
  if(V.options.pool_defer_publication && V.cmd && size && out &&
      vk_pool_cpu_prepare(address,size)<0)return -1;
  vk_mip_clear_buffer_access(address,size);
  atomic_fetch_add(&sync_cpu_reads,1);
  if (!V.cmd || !size || !out) return -1;
  pthread_mutex_lock(&V.resources);
  int valid=pool_find(address,size)!=NULL;
  pthread_mutex_unlock(&V.resources);
  if (!valid) return -1;
  int wait = recording_cpu_write(address, size);
  if (wait) {
    atomic_fetch_add(&sync_cpu_submit_waits,1);
    vk_internal_wait(vk_internal_commit());
    mtl_sub_begin();
  } else {
    // 前序提交的范围表不随完成回调释放；持锁查元数据，放锁后才等完成。
    // Earlier submissions retain immutable range metadata through callbacks.
    // Inspect under the queue lock, then release it before waiting.
    uint64_t serial = pending_cpu_write(address, size);
    if (serial) { atomic_fetch_add(&sync_cpu_pending_waits,1);vk_internal_wait(serial); wait = 1; }
  }
  pthread_mutex_lock(&V.resources);
  pool *p=pool_find(address,size);
  // 读 guest 而非映射镜像，保留 begin 之后 CPU 刚写的常量；不回写客体。
  // Read guest rather than its mirror to see CPU edits after begin. No guest writes.
  if (p) memcpy(out,p->guest+(address-(uintptr_t)p->guest),size);
  pthread_mutex_unlock(&V.resources);
  return p?wait:-1;
}
int vk_pool_try_read(uint64_t address,size_t size,void *out) {
  vk_mip_clear_buffer_access(address,size);
  if(!V.cmd||!size||!out) return -1;
  // Single recording owner: no later submission can be enqueued concurrently.
  // Completion only removes hazards, so a negative pending check stays safe.
  if(vk_pool_pending_writes(address,size)) return 0;
  pthread_mutex_lock(&V.resources);
  pool *p=pool_find(address,size);
  int known=p && (!V.options.pool_defer_publication || pool_cpu_copy_proven(p,address-(uintptr_t)p->guest,size));
  if(known) memcpy(out,p->guest+(address-(uintptr_t)p->guest),size);
  pthread_mutex_unlock(&V.resources);
  return !p?-1:known?1:0;
}
int vk_cover_read(uint64_t address,size_t size,void *out) {
  int reference=atomic_load(&cover_wait_reference);
  if(reference<0) {
    const char *v=getenv("DM_VULKAN_COVER_WAIT_REFERENCE");
    if(v&&strcmp(v,"0")&&strcmp(v,"1")) die("DM_VULKAN_COVER_WAIT_REFERENCE expects 0 or 1");
    reference=v&&!strcmp(v,"1");atomic_store(&cover_wait_reference,reference);
  }
  atomic_fetch_add(&sync_proof_reads,1);
  // This is an optional rendering proof, including its legacy wait reference.
  // Never introduce a new demand submission after the caller borrowed state.
  if(V.options.pool_defer_publication &&
      (vk_pool_pending_writes(address,size) || vk_pool_cpu_unknown(address,size)))return 0;
  if(reference) return vk_pool_read(address,size,out)>=0;
  int result=vk_pool_try_read(address,size,out);
  if(!result) atomic_fetch_add(&sync_proof_pending,1);
  return result==1;
}
// Conservative dependency proof: a full barrier covers all earlier buffer writes.
// Any later GPU buffer transfer invalidates that proof. Vertex image consumers
// also require full visibility after a fragment-only pass boundary.
static int buffer_transfer_pending, vertex_visibility_pending;
static VkPipelineStageFlags buffer_transfer_visible;
// GPU-produced logical pool intervals survive internal submits and CPU
// completion. Only a dependency reaching all GPU stages can retire this proof.
static struct {
  // Canonical half-open endpoints: binary searches consume the end directly.
  // Producers already compute it with checked addition before publication.
  uint64_t address,exclusive_end;
  // A guest address can be reused by a later pool with another GPU mirror.
  // Keep the identity of the actual, context-retained physical producer.
  uint64_t pool_identity,backing_generation;
  VkBuffer buffer;
  VkPipelineStageFlags producers;
  VkPipelineStageFlags visible;
} vertex_buffer_writes[256];
_Static_assert(sizeof vertex_buffer_writes[0]==48,"buffer write history layout");
static unsigned vertex_buffer_write_count;
static int vertex_buffer_write_overflow;
// Exact lower-bound partition, not a cached dependency decision. Until the
// interval geometry changes, every address in [previous_end,current_end)
// returns the same first possible overlap. Tail index==count has no upper
// bound, including UINT64_MAX. Size, visibility and owner checks stay live.
static struct {
  uint64_t lower,upper;
  unsigned index;
  int valid;
} buffer_write_partition;
_Static_assert(sizeof buffer_write_partition==24,"buffer partition proof size");
// Exact intersection of the retained histories' destination-stage visibility.
// It proves only a negative read query; overflow and every new writer defeat
// it. This uses the same literal stage bits as the per-range checks below.
static VkPipelineStageFlags buffer_common_visible=~(VkPipelineStageFlags)0;
static atomic_int buffer_memory_reference;
void vk_buffer_memory_reference(int reference) { atomic_store(&buffer_memory_reference,!!reference); }
int vk_buffer_memory_reference_state(void) { return atomic_load(&buffer_memory_reference); }
static unsigned buffer_write_start(uint64_t address);
// Only resolve owners this exact recording already retains through its fence
// and CPU effects. Never materialize/upload memory merely to scope a barrier.
static pool *buffer_recording_owner(uint64_t address,uint64_t size) {
  if(!recording_resources || !size) return NULL;
  pool *owner=NULL;
  for(pool_gpu_access *use=recording_resources->gpu_accesses;use;use=use->next) {
    pool *p=use->owner;uint64_t base=(uintptr_t)p->guest;
    if(address>=base && address-base<=p->size && size<=p->size-(address-base) &&
        p->backings && p->access_serial==recording_resources->queue_serial &&
        p->recording_access==use) {
      // A removed pool can remain alive until this context completes. If a
      // concurrent registration reused its address, never guess which mirror
      // an address-only producer call meant.
      if(owner && owner!=p) return NULL;
      owner=p;
    }
  }
  return owner;
}
static int buffer_history_owner(unsigned index,const pool *p,const pool_backing_span *backing) {
  uint64_t base=(uintptr_t)p->guest,address=vertex_buffer_writes[index].address;
  pool_span_piece span;
  return vertex_buffer_writes[index].pool_identity==p->identity && address>=base &&
      pool_span_resolve((pool *)p,address-base,vertex_buffer_writes[index].exclusive_end-address,&span) &&
      span.backing==backing && vertex_buffer_writes[index].buffer==backing->buffer.handle &&
      vertex_buffer_writes[index].backing_generation==backing->generation;
}
static int buffer_named_dependencies(VkCommandBuffer command,const vk_buffer_use *uses,unsigned count,
    VkPipelineStageFlags sources,VkPipelineStageFlags destination,VkAccessFlags writes,
    VkAccessFlags access,VkPipelineStageFlags visible_producers) {
  if(atomic_load(&buffer_memory_reference) || vertex_buffer_write_overflow || !recording_resources)
    return 0;
  pool *owners[16];pool_backing_span *backings[16];unsigned owners_count=0;
  for(unsigned k=0;k<count;k++) {
    const vk_buffer_use *u=&uses[k];
    if(!u->address || !u->size) continue;
    pool *p=buffer_recording_owner(u->address,u->size);pool_span_piece span;
    if(!p || !pool_span_resolve(p,u->address-(uintptr_t)p->guest,u->size,&span)) return 0;
    unsigned at=0;while(at<owners_count && backings[at]!=span.backing) at++;
    if(at==owners_count) {
      if(owners_count==sizeof owners/sizeof *owners) return 0;
      owners[owners_count]=p;backings[owners_count++]=span.backing;
    }
    // Logical overlap alone cannot prove a named physical barrier. Unknown,
    // coalesced cross-backing or retired producers use the original global fallback.
    for(unsigned j=buffer_write_start(u->address);j<vertex_buffer_write_count;j++) {
      uint64_t a=vertex_buffer_writes[j].address;
      if(a>=u->address && a-u->address>=u->size) break;
      if(!u->writable && !(destination&~vertex_buffer_writes[j].visible)) continue;
      if(!buffer_history_owner(j,p,span.backing)) return 0;
    }
  }
  if(!owners_count) return 0;
  VkBufferMemoryBarrier barriers[16];
  for(unsigned k=0;k<owners_count;k++) barriers[k]=(VkBufferMemoryBarrier){
    .sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=writes,.dstAccessMask=access,
    .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
    .buffer=backings[k]->buffer.handle,.offset=0,.size=VK_WHOLE_SIZE};
  vkCmdPipelineBarrier(command,sources,destination,0,0,NULL,owners_count,barriers,0,NULL);
  VkPipelineStageFlags common=~(VkPipelineStageFlags)0;
  for(unsigned j=0;j<vertex_buffer_write_count;j++) {
    if(vertex_buffer_writes[j].producers && !(vertex_buffer_writes[j].producers&~visible_producers))
      for(unsigned k=0;k<owners_count;k++) if(buffer_history_owner(j,owners[k],backings[k])) {
        vertex_buffer_writes[j].visible|=destination;break;
      }
    common&=vertex_buffer_writes[j].visible;
  }
  buffer_common_visible=common;
  return 1;
}
// Coalesced intervals are disjoint and sorted. Most shader constants do not
// overlap a GPU writer; find the first possible overlap without scanning every
// producer retained from previous submissions.
static unsigned buffer_write_start(uint64_t address) {
  unsigned count=vertex_buffer_write_count;
  if(!count) return 0;
  if(buffer_write_partition.valid && address>=buffer_write_partition.lower &&
      (buffer_write_partition.index==count || address<buffer_write_partition.upper))
    return buffer_write_partition.index;
  unsigned lo=0,hi=count;
  while(lo<hi) {
    unsigned middle=lo+(hi-lo)/2;
    if(vertex_buffer_writes[middle].exclusive_end<=address) lo=middle+1;
    else hi=middle;
  }
  buffer_write_partition.lower=lo?vertex_buffer_writes[lo-1].exclusive_end:0;
  buffer_write_partition.upper=lo<count?vertex_buffer_writes[lo].exclusive_end:0;
  buffer_write_partition.index=lo;buffer_write_partition.valid=1;
  return lo;
}
static int buffer_stage_pending(uint64_t address,uint64_t size,VkPipelineStageFlags stages) {
  if(!address||!size) return 0;
  if(vertex_buffer_write_overflow) return 1;
  if(!(stages&~buffer_common_visible)) return 0;
  for(unsigned i=buffer_write_start(address);i<vertex_buffer_write_count;i++) {
    uint64_t a=vertex_buffer_writes[i].address;
    if(a>=address && a-address>=size) break;
    if(!(stages&~vertex_buffer_writes[i].visible)) continue;
    return 1;
  }
  return 0;
}
int vk_buffer_vertex_pending(uint64_t address,uint64_t size) {
  return buffer_stage_pending(address,size,
      VK_PIPELINE_STAGE_VERTEX_INPUT_BIT|VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);
}
static VkPipelineStageFlags buffer_read_producers(uint64_t address,uint64_t size,
                                                 VkPipelineStageFlags stages) {
  VkPipelineStageFlags producers=0;
  for(unsigned i=buffer_write_start(address);i<vertex_buffer_write_count;i++) {
    uint64_t a=vertex_buffer_writes[i].address;
    if(a>=address && a-address>=size) break;
    if(!(stages&~vertex_buffer_writes[i].visible)) continue;
    producers|=vertex_buffer_writes[i].producers;
  }
  return producers;
}
void vk_buffer_shader_read(uint64_t address,uint64_t size,VkPipelineStageFlags stages) {
  vk_mip_clear_buffer_access(address,size);
  if(!buffer_stage_pending(address,size,stages)) return;
  VkPipelineStageFlags known=VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT;
  VkPipelineStageFlags producers=0;
  VkCommandBuffer command=VK_NULL_HANDLE;
  if(stages==VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT && !vertex_buffer_write_overflow &&
      !recording_resources->submission_barrier_reference) {
    producers=buffer_read_producers(address,size,stages);
    // Compute and transfers always close a render pass before writing. These
    // known producers therefore precede the pending secondary; synchronize its
    // late fragment binding in the primary without a tile store/reload.
    if(!(producers&~known)) command=vk_render_pending_primary();
  }
  // Ending a pass can replay the original drag-follow draw. Query history
  // afterwards, so an inner operation cannot invalidate the selected proof.
  if(!command) {
    vk_render_end();
    if(vertex_buffer_write_overflow || recording_resources->submission_barrier_reference) {
      vk_barrier();return;
    }
    producers=buffer_read_producers(address,size,stages);
    command=V.cmd;
  }
  if(!producers) return;
  if(producers&~known) { vk_barrier();return; }
  VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .srcAccessMask=((producers&VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)?VK_ACCESS_SHADER_WRITE_BIT:0)|
      ((producers&VK_PIPELINE_STAGE_TRANSFER_BIT)?VK_ACCESS_TRANSFER_WRITE_BIT:0),
    .dstAccessMask=((stages&VK_PIPELINE_STAGE_TRANSFER_BIT)?VK_ACCESS_TRANSFER_READ_BIT:0)|
      ((stages&VK_PIPELINE_STAGE_VERTEX_INPUT_BIT)?VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT|VK_ACCESS_INDEX_READ_BIT:0)|
      ((stages&(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT))?VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_UNIFORM_READ_BIT:0)};
  vk_buffer_use use={address,size,0};
  if(buffer_named_dependencies(command,&use,1,producers,stages,
      barrier.srcAccessMask,barrier.dstAccessMask,producers)) return;
  vkCmdPipelineBarrier(command,producers,stages,0,1,&barrier,0,NULL,0,NULL);
  // This memory barrier also covers earlier writes by the same producers.
  // Visibility belongs to the actual destination stages, not one global bit.
  vk_buffer_producers_visible(producers,stages);
}
void vk_buffer_producers_visible(VkPipelineStageFlags producers,VkPipelineStageFlags consumers) {
  VkPipelineStageFlags common=~(VkPipelineStageFlags)0;
  for(unsigned i=0;i<vertex_buffer_write_count;i++) {
    if(vertex_buffer_writes[i].producers && !(vertex_buffer_writes[i].producers&~producers))
      vertex_buffer_writes[i].visible|=consumers;
    common&=vertex_buffer_writes[i].visible;
  }
  buffer_common_visible=common;
}
void vk_buffer_vertex_require(uint64_t address,uint64_t size) {
  vk_buffer_shader_read(address,size,
      VK_PIPELINE_STAGE_VERTEX_INPUT_BIT|VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);
}
static void buffer_cpu_written(pool *owner,uint64_t address,uint64_t size) {
  if(pool_cpu_preserving)return;
  if(owner)pool_cpu_written(owner,address-(uintptr_t)owner->guest,size);
  else if(recording_resources) {
    // Removed/recreated guest addresses can be ambiguous. Every intersecting
    // context-retained owner loses authority; never guess one physical pool.
    for(pool_gpu_access *use=recording_resources->gpu_accesses;use;use=use->next) {
      pool *p=use->owner;uint64_t base=(uintptr_t)p->guest;
      uint64_t lo=address>base?address:base,hi=address+size<base+p->size?address+size:base+p->size;
      if(lo<hi)pool_cpu_written(p,lo-base,hi-lo);
    }
  }
}
static void vertex_buffer_produced(uint64_t address,uint64_t size,VkPipelineStageFlags producers) {
  if(!address||!size) return;
  if(size>UINT64_MAX-address) die("Vulkan buffer producer range overflow");
  // Invalidate before the first possible merge/removal/insertion, including
  // the bounded-table overflow return. Visibility-only updates need no reset.
  buffer_write_partition.valid=0;
  buffer_common_visible=0;
  vertex_visibility_pending=1;
  pool *owner=buffer_recording_owner(address,size);
  buffer_cpu_written(owner,address,size);
  pool_span_piece span;
  int backed=owner&&pool_span_resolve(owner,address-(uintptr_t)owner->guest,size,&span);
  uint64_t identity=backed?owner->identity:0,generation=backed?span.backing->generation:0;
  VkBuffer physical=backed?span.backing->buffer.handle:VK_NULL_HANDLE;
  uint64_t end=address+size;
  // Producer histories outlive CPU completion. Coalesce touching intervals
  // rather than filling the bounded table with each frame's overlapping
  // upload/copy slices. Adjacent ranges merge only with identical producer
  // stages, so independent transfer/compute ranges keep their stage scopes.
  // Union overlapping producers and invalidate visibility for the whole
  // merged range: extra dependencies are safe, dropped writes are not.
  for(unsigned i=0;i<vertex_buffer_write_count;) {
    uint64_t a=vertex_buffer_writes[i].address;
    uint64_t b=vertex_buffer_writes[i].exclusive_end;
    if((address<b && a<end) ||
        ((address==b || end==a) && vertex_buffer_writes[i].producers==producers &&
          vertex_buffer_writes[i].pool_identity==identity && vertex_buffer_writes[i].buffer==physical &&
          vertex_buffer_writes[i].backing_generation==generation)) {
      if(a<address) address=a;
      if(b>end) end=b;
      producers|=vertex_buffer_writes[i].producers;
      if(vertex_buffer_writes[i].pool_identity!=identity || vertex_buffer_writes[i].buffer!=physical ||
          vertex_buffer_writes[i].backing_generation!=generation) {
        identity=0;generation=0;physical=VK_NULL_HANDLE;
      }
      vertex_buffer_write_count--;
      memmove(vertex_buffer_writes+i,vertex_buffer_writes+i+1,
          (vertex_buffer_write_count-i)*sizeof *vertex_buffer_writes);
      i=0;
    } else {
      i++;
    }
  }
  if(vertex_buffer_write_count==256) { vertex_buffer_write_overflow=1;return; }
  unsigned at=0;
  while(at<vertex_buffer_write_count && vertex_buffer_writes[at].address<address) at++;
  memmove(vertex_buffer_writes+at+1,vertex_buffer_writes+at,
      (vertex_buffer_write_count-at)*sizeof *vertex_buffer_writes);
  vertex_buffer_writes[at].address=address;
  vertex_buffer_writes[at].exclusive_end=end;
  vertex_buffer_writes[at].pool_identity=identity;
  vertex_buffer_writes[at].backing_generation=generation;
  vertex_buffer_writes[at].buffer=physical;
  vertex_buffer_writes[at].producers=producers;
  vertex_buffer_writes[at].visible=0;vertex_buffer_write_count++;
}
void vk_buffer_transfer_written_range(uint64_t address,uint64_t size) {
  if(recording_resources->submission_barrier_reference) {
    pool *owner=buffer_recording_owner(address,size);
    buffer_cpu_written(owner,address,size);
    buffer_transfer_pending=1;
  } else vertex_buffer_produced(address,size,VK_PIPELINE_STAGE_TRANSFER_BIT);
}
void vk_buffer_shader_written(uint64_t address,uint64_t size,VkPipelineStageFlags stages) {
  vertex_buffer_produced(address,size,stages);
}
static void buffer_access_dependencies(const vk_buffer_use *uses,unsigned count,int conservative,
    VkPipelineStageFlags destination,VkAccessFlags access,VkPipelineStageFlags extra_sources,VkAccessFlags extra_writes) {
  // All dispatch inputs/outputs have been collected. Completing the prior
  // pass can replay original UI draws; inspect histories only afterwards.
  vk_render_end();
  if(conservative || vertex_buffer_write_overflow || recording_resources->submission_barrier_reference) {
    if(destination==VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT) vk_compute_barrier();
    else vk_transfer_barrier();
    return;
  }
  VkPipelineStageFlags sources=extra_sources,producers=0;
  VkAccessFlags writes=extra_writes;
  for(unsigned k=0;k<count;k++) {
    const vk_buffer_use *u=&uses[k];
    if(!u->address || !u->size) continue;
    if(u->writable) {
      pthread_mutex_lock(&V.resources);
      pool *p=pool_find(u->address,u->size);
      if(!p) die("Vulkan compute dependency outside pool");
      uint64_t offset=u->address-(uintptr_t)p->guest;
      sources|=pending_pool_stages(p,offset,u->size);
      if(p->access_serial==recording_resources->queue_serial)
        sources|=access_stages(p->recording_access,offset,u->size);
      pthread_mutex_unlock(&V.resources);
    }
    for(unsigned j=buffer_write_start(u->address);j<vertex_buffer_write_count;j++) {
      uint64_t a=vertex_buffer_writes[j].address;
      if(a>=u->address && a-u->address>=u->size) break;
      if(!u->writable && !(destination&~vertex_buffer_writes[j].visible)) continue;
      producers|=vertex_buffer_writes[j].producers;
    }
  }
  VkPipelineStageFlags known=VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
  if((sources&VK_PIPELINE_STAGE_ALL_COMMANDS_BIT) || (producers&~known)) {
    if(destination==VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT) vk_compute_barrier();
    else vk_transfer_barrier();
    return;
  }
  sources|=producers;
  if(producers&VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT) writes|=VK_ACCESS_SHADER_WRITE_BIT;
  if(producers&VK_PIPELINE_STAGE_TRANSFER_BIT) writes|=VK_ACCESS_TRANSFER_WRITE_BIT;
  // Unnamed copies are fresh, exclusively owned staging buffers, not unknown
  // writers from arbitrary stages. Keep their other consumer proofs pending.
  if(buffer_transfer_pending && (destination&~buffer_transfer_visible)) {
    sources|=VK_PIPELINE_STAGE_TRANSFER_BIT;writes|=VK_ACCESS_TRANSFER_WRITE_BIT;
  }
  if(!sources) return;
  VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=writes,
      .dstAccessMask=access};
  VkPipelineStageFlags covered=((writes&VK_ACCESS_SHADER_WRITE_BIT)?VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT:0)|
      ((writes&VK_ACCESS_TRANSFER_WRITE_BIT)?VK_PIPELINE_STAGE_TRANSFER_BIT:0);
  // Private alignment sources and unnamed staging writes have no physical
  // pool proof. Keep their existing global barrier and broad visibility.
  if(destination==VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT && !extra_sources && !extra_writes &&
      !(buffer_transfer_pending && (destination&~buffer_transfer_visible)) &&
      buffer_named_dependencies(V.cmd,uses,count,sources,destination,writes,access,covered)) return;
  vkCmdPipelineBarrier(V.cmd,sources,destination,0,1,&barrier,0,NULL,0,NULL);
  vk_buffer_producers_visible(covered,destination);
  if(writes&VK_ACCESS_TRANSFER_WRITE_BIT) buffer_transfer_visible|=destination;
}
void vk_compute_buffer_dependencies(const vk_buffer_use *uses,unsigned count,int conservative) {
  // Direct dispatches describe all shader buffer accesses. Deferred NVN order
  // flags remain live for indirect consumers/TOP timestamps; they are not a
  // reason to order independent memory here (same resource model as Metal).
  buffer_access_dependencies(uses,count,conservative,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_UNIFORM_READ_BIT|VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,0,0);
}
void vk_transfer_buffer_dependencies(const vk_buffer_use *uses,unsigned count) {
  // Source reads need prior writers; destination writes also need prior readers.
  // A named pool copy/update must not wait for unrelated attachment stages.
  // Unknown accesses and the full reference retain the conservative fallback.
  buffer_access_dependencies(uses,count,0,VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,0,0);
}
void vk_buffer_copyback_dependencies(uint64_t address,uint64_t size) {
  vk_buffer_use use={address,size,1};
  // The private source holds both its initial transfer copy and subsequent
  // partial compute writes. Destination hazards concern the original pool.
  buffer_access_dependencies(&use,1,0,VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT);
}
void vk_compute_produced(void) {
  if(recording_resources->submission_barrier_reference) { vk_barrier();return; }
  vertex_visibility_pending=1;
  // Validated logical pool ranges survive command buffers and CPU completion.
  // Earlier producers in this submission are included conservatively.
  for(pool_writeback *p=recording_resources->writebacks;p;p=p->next)
    for(writeback_range *r=p->ranges;r;r=r->next)
      vertex_buffer_produced((uintptr_t)p->owner->guest+r->offset,r->end-r->offset,r->producers);
}
void vk_compute_barrier(void) {
  if(recording_resources->submission_barrier_reference) { vk_barrier();return; }
  vk_render_end();
  VkMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_UNIFORM_READ_BIT|VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&b,0,NULL,0,NULL);
  vertex_visibility_pending=1;
}
void vk_transfer_barrier(void) {
  if(recording_resources->submission_barrier_reference) { vk_barrier();return; }
  vk_render_end();
  // A copy/clear only consumes prior accesses at TRANSFER. Do not make
  // independent later vertex work wait for earlier fragment work here.
  // This does not retire buffer/vertex visibility: actual shader consumers
  // still establish their dependencies, including across command buffers.
  VkMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&b,0,NULL,0,NULL);
  vertex_visibility_pending=1;
}
static _Atomic int graphics_stage_reference = -1;
static _Atomic uint64_t graphics_stage_relaxed, graphics_stage_fallback;
// Typed dependency decisions remain unchanged; batch only their diagnostics.
static uint64_t recorded_graphics_stage_relaxed,recorded_graphics_stage_fallback;
void vk_buffer_transfer_written(void) { buffer_transfer_pending = 1;buffer_transfer_visible=0; }
void vk_graphics_stage_reference(int reference) { atomic_store(&graphics_stage_reference,!!reference); }
void vk_graphics_stage_stats(uint64_t *relaxed,uint64_t *fallback,int *reference) {
  *relaxed=atomic_load(&graphics_stage_relaxed);*fallback=atomic_load(&graphics_stage_fallback);
  *reference=atomic_load(&graphics_stage_reference);
}
void vk_vertex_images_require(void) {
  if(vertex_visibility_pending) vk_barrier();
}
void vk_barrier_at(const char *file,unsigned line) {
  vk_render_end_at(file, line);
  V.nvn_barrier = 0;
  buffer_transfer_pending = vertex_visibility_pending = 0;
  buffer_transfer_visible=0;
  // render_end above can replay nested work and repopulate the partition;
  // invalidate only after it returns, at the actual geometry clear.
  buffer_write_partition.valid=0;
  vertex_buffer_write_count=0;vertex_buffer_write_overflow=0;
  buffer_common_visible=~(VkPipelineStageFlags)0;
  VkMemoryBarrier b = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT |
                       VK_ACCESS_HOST_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT |
                       VK_ACCESS_HOST_READ_BIT};
  vkCmdPipelineBarrier(
      V.cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT,
      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &b,
      0, NULL, 0, NULL);
}
// Return true only when graphics buffer dependencies are covered by typed
// readonly consumers. Writable/unknown buffers stay conservative.
int vk_graphics_resource_dependencies(int writable) {
  int reference=atomic_load(&graphics_stage_reference);
  if(reference<0) {
    const char *value=getenv("DM_VULKAN_GRAPHICS_STAGE_REFERENCE");
    if(value&&strcmp(value,"0")&&strcmp(value,"1")) die("DM_VULKAN_GRAPHICS_STAGE_REFERENCE expects 0 or 1");
    reference=value&&!strcmp(value,"1");atomic_store(&graphics_stage_reference,reference);
  }
  if(writable||(buffer_transfer_pending &&
      (reference||recording_resources->submission_barrier_reference))||
      (!vk_render_active() &&
       (reference||recording_resources->submission_barrier_reference))) {
    vk_barrier();++recorded_graphics_stage_fallback;return 0;
  }
  VkPipelineStageFlags consumers=VK_PIPELINE_STAGE_VERTEX_INPUT_BIT|
      VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  if(buffer_transfer_pending && (consumers&~buffer_transfer_visible)) {
    vk_render_end();
    VkMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask=VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT|VK_ACCESS_INDEX_READ_BIT|
          VK_ACCESS_UNIFORM_READ_BIT|VK_ACCESS_SHADER_READ_BIT};
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,consumers,
        0,1,&b,0,NULL,0,NULL);
    buffer_transfer_visible|=consumers;
    vk_buffer_producers_visible(VK_PIPELINE_STAGE_TRANSFER_BIT,consumers);
  }
  ++recorded_graphics_stage_relaxed;return 1;
}
void vk_draw_barrier_at(const char *file,unsigned line,int vertex_images) {
  int reference=atomic_load(&graphics_stage_reference);
  if(reference<0) {
    const char *v=getenv("DM_VULKAN_GRAPHICS_STAGE_REFERENCE");
    if(v&&strcmp(v,"0")&&strcmp(v,"1")) die("DM_VULKAN_GRAPHICS_STAGE_REFERENCE expects 0 or 1");
    reference=v&&!strcmp(v,"1");atomic_store(&graphics_stage_reference,reference);
  }
  // Buffer producers (including alignment copies) must reach vertex input,
  // uniform/storage and indirect consumers. Unknown vertex image accesses and writable vertex buffers also
  // fall back. Never narrow explicit NVN, transfer, compute, submit/readback.
  vk_render_end_at(file,line);
  if(reference||buffer_transfer_pending||vertex_images||
      (V.nvn_barrier&(NVN_BARRIER_ORDER_PRIMITIVES_BIT|NVN_BARRIER_ORDER_INDIRECT_DATA_BIT))) {
    vk_barrier_at(file,line);++recorded_graphics_stage_fallback;return;
  }
  VkMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|
      VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,
    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|
      VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    0,1,&b,0,NULL,0,NULL);
  // Keep deferred NVN flags: a TOP timestamp still needs the full dependency.
  // A later vertex image consumer must also restore the complete dependency.
  vertex_visibility_pending=1;
  ++recorded_graphics_stage_relaxed;
}
// Early-stage-only uploads precede fragment-dependent ones, so a late-stage
// dependency cannot hold up an unrelated vertex upload. Reordering is allowed
// only after proving each record owns a distinct destination buffer page.
static unsigned upload_phase(VkPipelineStageFlags stages) {
  if(stages&VK_PIPELINE_STAGE_ALL_COMMANDS_BIT) return 3;
  if(stages&VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT) return 2;
  if(stages&(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT)) return 1;
  return 0;
}
static int upload_compare(const void *left,const void *right) {
  const queued_pool_upload *a=*(queued_pool_upload *const *)left,*b=*(queued_pool_upload *const *)right;
  unsigned ap=upload_phase(a->before),bp=upload_phase(b->before);
  if(ap!=bp) return ap<bp?-1:1;
  if(a->before!=b->before) return a->before<b->before?-1:1;
  if(a->after!=b->after) return a->after<b->after?-1:1;
  return 0;
}
static void record_pool_upload_commands(void) {
  submit_resources *r=recording_resources;
  if(!r->pool_uploads) return;
  size_t count=0,records=0;
  for(queued_pool_upload *u=r->pool_uploads;u;u=u->next) { count+=u->count;records++; }
  if(count>UINT32_MAX||count>SIZE_MAX/sizeof(VkBufferMemoryBarrier)||records>SIZE_MAX/sizeof(queued_pool_upload *))
    die("Vulkan upload barrier capacity");
  VkBufferMemoryBarrier *barriers=calloc(count,sizeof *barriers);
  queued_pool_upload **order=malloc(records*sizeof *order);
  if(!barriers||!order) die("Vulkan upload range barriers allocation");
  queued_pool_upload *pages[512]={0};int independent=records<=256;
  size_t index=0;
  for(queued_pool_upload *u=r->pool_uploads;u;u=u->next) {
    if(!u->backing || u->backing->generation!=u->backing_generation ||
       u->backing->buffer.handle!=u->destination)die("Vulkan queued pool backing changed");
    if(!u->before) u->before=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    if(!u->after) u->after=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    order[index++]=u;
    if(independent) {
      uint64_t page=u->regions[0].dstOffset>>GPU_ACCESS_SHIFT;
      VkBufferCopy *last=&u->regions[u->count-1];
      if(((last->dstOffset+last->size-1)>>GPU_ACCESS_SHIFT)!=page) { independent=0;continue; }
      unsigned bucket=(((uint64_t)(uintptr_t)u->destination>>4)^page^(page>>9))%512;
      while(pages[bucket]) {
        queued_pool_upload *previous=pages[bucket];
        if(previous->destination==u->destination &&
            (previous->regions[0].dstOffset>>GPU_ACCESS_SHIFT)==page) { independent=0;break; }
        bucket=(bucket+1)%512;
      }
      if(independent) pages[bucket]=u;
    }
  }
  if(independent) qsort(order,records,sizeof *order,upload_compare);
  r->uploads = vk_command_buffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
  VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
  VK_CHECK(vkBeginCommandBuffer(r->uploads,&begin));
  for(size_t first=0;first<records;) {
    VkPipelineStageFlags before=order[first]->before,after=order[first]->after;
    size_t end=first+1,n=0;uint64_t copy_bytes=0;
    while(end<records&&order[end]->before==before&&order[end]->after==after) end++;
    for(size_t i=first;i<end;i++) {
      queued_pool_upload *u=order[i];
      for(unsigned k=0;k<u->count;k++) {
        if(V.options.pool_copy_accounting)copy_bytes+=u->regions[k].size;
        barriers[n++]=(VkBufferMemoryBarrier){
        .sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .buffer=u->destination,.offset=u->regions[k].dstOffset,.size=u->regions[k].size};
      }
    }
    vkCmdPipelineBarrier(r->uploads,before,VK_PIPELINE_STAGE_TRANSFER_BIT,
      0,0,NULL,(uint32_t)n,barriers,0,NULL);
    for(size_t i=first;i<end;i++) {
      queued_pool_upload *u=order[i];
      vkCmdCopyBuffer(r->uploads,u->source,u->destination,u->count,u->regions);free(u);
    }
    POOL_COPY_ADD(gpu_upload_recorded_bytes,copy_bytes);
    for(size_t k=0;k<n;k++) {
      barriers[k].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
      barriers[k].dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    }
    vkCmdPipelineBarrier(r->uploads,VK_PIPELINE_STAGE_TRANSFER_BIT,after,
      0,0,NULL,(uint32_t)n,barriers,0,NULL);
    first=end;
  }
  r->pool_uploads=NULL;r->pool_upload_tail=&r->pool_uploads;
  free(order);free(barriers);
  VK_CHECK(vkEndCommandBuffer(r->uploads));
}
// Exact four-byte change runs, with bounded vector loads. Coalesce runs across
// vector/block boundaries, but never include equal words: those bytes may still
// contain a newer GPU value than the CPU mirror. Worst case is 512 runs/page.
// Read each 64-byte block once. Equal blocks need no per-word reductions;
// changed blocks reuse these exact comparisons when constructing upload runs.
// This replaces the former full-page memcmp followed by a second full scan.
static inline uint32_t changed_word_mask(const uint8_t *guest,const uint8_t *shadow,unsigned words) {
#if defined(__aarch64__)
  if(words==16) {
    uint32x4_t equal0=vceqq_u32(vreinterpretq_u32_u8(vld1q_u8(guest)),vreinterpretq_u32_u8(vld1q_u8(shadow)));
    uint32x4_t equal1=vceqq_u32(vreinterpretq_u32_u8(vld1q_u8(guest+16)),vreinterpretq_u32_u8(vld1q_u8(shadow+16)));
    uint32x4_t equal2=vceqq_u32(vreinterpretq_u32_u8(vld1q_u8(guest+32)),vreinterpretq_u32_u8(vld1q_u8(shadow+32)));
    uint32x4_t equal3=vceqq_u32(vreinterpretq_u32_u8(vld1q_u8(guest+48)),vreinterpretq_u32_u8(vld1q_u8(shadow+48)));
    if(vminvq_u32(vandq_u32(vandq_u32(equal0,equal1),vandq_u32(equal2,equal3)))==UINT32_MAX)return 0;
    // Equality lanes are all-zero/all-one, so narrowing retains the entire
    // comparison. Each word then owns a distinct bit in the horizontal sum;
    // its maximum is65535, with no carries between bits or uint16 overflow.
    // Equal blocks keep the original early return.
    uint16x8_t equal_low=vcombine_u16(vmovn_u32(equal0),vmovn_u32(equal1));
    uint16x8_t equal_high=vcombine_u16(vmovn_u32(equal2),vmovn_u32(equal3));
    const uint16x8_t weights={1,2,4,8,16,32,64,128};
    uint16x8_t low=vbicq_u16(weights,equal_low);
    uint16x8_t high=vshlq_n_u16(vbicq_u16(weights,equal_high),8);
    return vaddvq_u16(vorrq_u16(low,high));
  }
#endif
  uint32_t changed=0;unsigned word=0;
#if defined(__aarch64__)
    const uint32x4_t weights={1,2,4,8};
    for(;word+4<=words;word+=4) {
      uint32x4_t a=vreinterpretq_u32_u8(vld1q_u8(guest+word*4));
      uint32x4_t b=vreinterpretq_u32_u8(vld1q_u8(shadow+word*4));
      uint32x4_t different=vmvnq_u32(vceqq_u32(a,b));
      changed|=vaddvq_u32(vandq_u32(different,weights))<<word;
    }
#endif
    for(;word<words;word++) {
      uint32_t a,b;memcpy(&a,guest+word*4,4);memcpy(&b,shadow+word*4,4);
      changed|=(uint32_t)(a!=b)<<word;
    }
  return changed;
}
static inline unsigned append_changed_word_runs(uint32_t changed,uint64_t block,
    uint64_t offset,VkBufferCopy *runs,unsigned count) {
  while(changed) {
    unsigned first=__builtin_ctz(changed);
    unsigned length=__builtin_ctz(~(changed>>first));
    uint64_t source=block+first*4,bytes=length*4;
    if(count && runs[count-1].srcOffset+runs[count-1].size==source)
      runs[count-1].size+=bytes;
    else runs[count++]=(VkBufferCopy){.srcOffset=source,.dstOffset=offset+source,.size=bytes};
    changed&=~(((1u<<length)-1u)<<first);
  }
  return count;
}
static unsigned changed_word_ranges(const uint8_t *guest,const uint8_t *shadow,
                                    uint64_t offset,size_t size,VkBufferCopy *runs) {
  unsigned count=0;size_t block=0,whole=size&~(size_t)63,paired=size&~(size_t)127;
  // Two proven in-bounds blocks share only loop bookkeeping. Each block keeps
  // the original mask/append order; equal GPU-owned words are never copied.
  for(;block<paired;block+=128) {
    count=append_changed_word_runs(changed_word_mask(guest+block,shadow+block,16),block,offset,runs,count);
    count=append_changed_word_runs(changed_word_mask(guest+block+64,shadow+block+64,16),block+64,offset,runs,count);
  }
  // Retain the original single full block and bounded tail operations.
  if(block<whole) {
    count=append_changed_word_runs(changed_word_mask(guest+block,shadow+block,16),block,offset,runs,count);
    block+=64;
  }
  if(block<size)
    count=append_changed_word_runs(changed_word_mask(guest+block,shadow+block,(size-block)/4),block,offset,runs,count);
  return count;
}
#ifdef DM_VULKAN_TEST_EMBED
// Functional oracle entry only; no production counter, sampler or timing.
uint32_t vk_test_upload_word_mask(const uint8_t *guest,const uint8_t *shadow,unsigned words) {
  return changed_word_mask(guest,shadow,words);
}
#ifdef DM_VULKAN_SCAN_PAIR_ORACLE
unsigned vk_test_upload_word_runs(const uint8_t *guest,const uint8_t *shadow,
    uint64_t offset,size_t size,VkBufferCopy *runs) {
  return changed_word_ranges(guest,shadow,offset,size,runs);
}
#endif
// Functional boundary oracle only, with real registered physical backings.
// It never copies, initializes, allocates, changes history or counts hot work.
int vk_test_pool_upload_page_span(uint64_t address,uint64_t size,unsigned count) {
  pthread_mutex_lock(&V.resources);
  pool *p=pool_find(address,size);pool_span_piece page;
  int single=p && pool_upload_page_span(p,address-(uintptr_t)p->guest,size,count,&page);
  pthread_mutex_unlock(&V.resources);return single;
}
#endif
// Capture actual immutable backing/local offsets while holding the original
// pool reference. Later upload command recording never re-resolves a logical
// address against a newer physical map. One record per touched backing keeps
// sparse runs grouped, exactly as the whole-pool policy did.
static void queue_pool_page_upload(pool *p,uint64_t off,size_t size,const vk_buffer *snapshot,
    uint64_t snapshot_offset,const VkBufferCopy *regions,unsigned count) {
  pool_span_iter it;if(!pool_span_iter_begin(p,off,size,&it))die("Vulkan queued pool bounds");
  pool_span_piece span;int next;submit_resources *r=recording_resources;
  while((next=pool_span_iter_next(&it,&span))==1) {
    queued_pool_upload *u=NULL;
    for(unsigned k=0;k<count;k++) {
      uint64_t lo=regions[k].dstOffset,hi=lo+regions[k].size;
      if(lo<span.logical)lo=span.logical;
      if(hi>span.logical+span.size)hi=span.logical+span.size;
      if(lo>=hi)continue;
      if(!u) {
        u=malloc(sizeof *u+count*sizeof *regions);if(!u)die("Vulkan queued pool upload allocation");
        *u=(queued_pool_upload){.source=snapshot->handle,.destination=span.backing->buffer.handle,
          .backing=span.backing,.backing_generation=span.backing->generation};
        *r->pool_upload_tail=u;r->pool_upload_tail=&u->next;
      }
      uint64_t src=regions[k].srcOffset+(lo-regions[k].dstOffset);
      if(src<snapshot_offset || src-snapshot_offset>snapshot->size || hi-lo>snapshot->size-(src-snapshot_offset))
        die("Vulkan queued pool snapshot bounds");
      u->regions[u->count++]=(VkBufferCopy){.srcOffset=src-snapshot_offset,
        .dstOffset=lo-span.backing->begin,.size=hi-lo};
      u->before|=pending_pool_stages(p,lo,hi-lo);
      u->after|=p->access_serial==r->queue_serial?
        access_stages(p->recording_access,lo,hi-lo):VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
      pool_cpu_preserving++;
      vk_buffer_transfer_written_range((uintptr_t)p->guest+lo,hi-lo);
      pool_cpu_preserving--;
    }
  }
  if(next<0)die("Vulkan queued upload across unbacked hole");
}
#ifdef DM_VULKAN_TEST_EMBED
static void (*pool_copy_wait_hook)(void *);
static void *pool_copy_wait_context;
void vk_test_pool_copy_wait(void (*hook)(void *),void *context) {
  pool_copy_wait_hook=hook;pool_copy_wait_context=context;
}
#endif
static void sync_queued_page(pool *p,uint64_t off,size_t size) {
  // Dirty tracking is page-granular, GPU publication is byte-granular. Upload
  // only changed words; copying unchanged neighbours would overwrite queued
  // GPU results with stale CPU data. Pools/runs are checked for word alignment
  // by the caller; the conservative host path handles unusual tails.
  for(;;) {
    VkBufferCopy regions[512];unsigned count=0;
    pthread_mutex_lock(&pool_publication_mu);
    // Keep the original full-page/scalar scan as an independent reference.
    // The default scanner handles equal and changed blocks together: there is
    // no second read of the whole page after an initial memcmp finds a change.
    if(recording_resources->upload_scan_reference)POOL_COPY_ADD(reference_page_span_bytes,size);
    else POOL_COPY_ADD(scan_bytes,size);
    if(recording_resources->upload_scan_reference && !memcmp(p->guest+off,p->shadow+off,size)) {
      pthread_mutex_unlock(&pool_publication_mu);return;
    }
    if(recording_resources->upload_scan_reference) {
      for(size_t i=0;i<size;) {
        if(!memcmp(p->guest+off+i,p->shadow+off+i,4)) { i+=4;continue; }
        size_t first=i;
        do { i+=4; } while(i<size&&memcmp(p->guest+off+i,p->shadow+off+i,4));
        regions[count++]=(VkBufferCopy){.srcOffset=first,.dstOffset=off+first,.size=i-first};
      }
    } else count=changed_word_ranges(p->guest+off,p->shadow+off,off,size,regions);
    if(!count) { pthread_mutex_unlock(&pool_publication_mu);return; }
    uint64_t serial=0;
    if(pending_cpu_write((uintptr_t)p->guest+off,size))
      for(unsigned i=0;i<count;i++) {
        uint64_t overlap=pending_cpu_write((uintptr_t)p->guest+regions[i].dstOffset,regions[i].size);
        if(overlap>serial) serial=overlap;
      }
    if(serial) {
      pthread_mutex_unlock(&pool_publication_mu);
      atomic_fetch_add(&upload_waits,1);
      int trace=vk_system_trace_begin("Vulkan upload waits CPU publication");
#ifdef DM_VULKAN_TEST_EMBED
      if(pool_copy_wait_hook)pool_copy_wait_hook(pool_copy_wait_context);
#endif
      vk_internal_wait(serial);vk_system_trace_end(trace);continue;
    }
    uint64_t copied_bytes=0;
    if(pending_gpu_access(p,off,size)) {
      // A single sparse edit needs only its immutable changed bytes in the
      // queued upload. Do not copy/allocate a whole CPU page to transfer one
      // small run. Keep fragmented/full-page uploads on the original path,
      // avoiding many tiny memcpy calls. Destination ranges, dependencies,
      // shadow publication and fence ownership stay exactly the same.
      int compact=!recording_resources->upload_scan_reference && size>=128 && count==1 &&
        regions[0].size<=size/2;
      uint64_t snapshot_offset=compact?regions[0].srcOffset:0;
      uint64_t snapshot_size=compact?regions[0].size:size;
      // The temporary pool reuses exact sizes. Bound the new size classes
      // instead of creating a distinct allocation class for every run length.
      uint64_t snapshot_capacity=size;
      if(compact) {
        snapshot_capacity=64;
        while(snapshot_capacity<snapshot_size) snapshot_capacity*=2;
      }
      vk_buffer *snapshot=vk_temp_buffer(snapshot_capacity);
      memcpy(snapshot->map,p->guest+off+snapshot_offset,snapshot_size);
      POOL_COPY_ADD(snapshot_bytes,snapshot_size);
      queue_pool_page_upload(p,off,size,snapshot,snapshot_offset,regions,count);
    } else if(upload_dual_enabled && !recording_resources->upload_scan_reference) {
      for(unsigned i=0;i<count;i++) {
        pool_spans_copy_dual(p,regions[i].dstOffset,regions[i].size);
        if(V.options.pool_copy_accounting)copied_bytes+=regions[i].size;
      }
      POOL_COPY_ADD(mirror_bytes,copied_bytes);
      POOL_COPY_ADD(shadow_bytes,copied_bytes);
      pool_cpu_synced_runs(p,regions,count,recording_resources->cpu_authority_begin);
      pthread_mutex_unlock(&pool_publication_mu);return;
    } else {
      // Several exact edits in one physical page reuse only its immutable
      // mapping. Content/dirty/authority proofs remain the original per-run
      // operations; no unchanged GPU-owned word is copied from stale CPU data.
      if(count==1 || recording_resources->upload_scan_reference ||
          !pool_spans_copy_changed_page(p,off,size,regions,count))
        for(unsigned i=0;i<count;i++)
          pool_spans_copy_from_guest(p,regions[i].dstOffset,regions[i].size);
      if(V.options.pool_copy_accounting)
        for(unsigned i=0;i<count;i++)copied_bytes+=regions[i].size;
      POOL_COPY_ADD(mirror_bytes,copied_bytes);
    }
    copied_bytes=0;
    for(unsigned i=0;i<count;i++) {
      memcpy(p->shadow+regions[i].dstOffset,p->guest+regions[i].dstOffset,regions[i].size);
      if(V.options.pool_copy_accounting)copied_bytes+=regions[i].size;
    }
    POOL_COPY_ADD(shadow_bytes,copied_bytes);
    pool_cpu_synced_runs(p,regions,count,recording_resources->cpu_authority_begin);
    pthread_mutex_unlock(&pool_publication_mu);return;
  }
}
static void sync_page(const void *memory, size_t length, void *context) {
  pool *p = context;
  uint64_t off = (const uint8_t *)memory - p->guest;
  while (length) {
    size_t size = length < 4096 ? length : 4096;
    if(recording_resources&&async_submissions&&!recording_resources->pool_sync_reference&&
        !recording_resources->submission_barrier_reference&&!(off%4)&&!(size%4)) {
      sync_queued_page(p,off,size);
      off+=size;length-=size;continue;
    }
    for (;;) {
      pthread_mutex_lock(&pool_publication_mu);
      POOL_COPY_ADD(fallback_compare_span_bytes,size);
      if (!memcmp(p->guest + off, p->shadow + off, size)) {
        pthread_mutex_unlock(&pool_publication_mu);
        break;
      }
      uint64_t serial = pending_cpu_write((uintptr_t)p->guest + off, size);
      if (serial) {
        pthread_mutex_unlock(&pool_publication_mu);
        atomic_fetch_add(&upload_waits, 1);
        int wait_trace=vk_system_trace_begin("Vulkan upload waits CPU publication");
        vk_internal_wait(serial);
        vk_system_trace_end(wait_trace);
        continue;
      }
      // 实际复制整段页范围，因此 GPU 访问冲突也必须按整段检查。
      // The actual upload copies this whole page span, so check the same span
      // against every earlier GPU access before touching its coherent mirror.
      serial = pending_gpu_access(p, off, size);
      if (serial) {
        pthread_mutex_unlock(&pool_publication_mu);
        atomic_fetch_add(&upload_waits, 1);
        int wait_trace=vk_system_trace_begin("Vulkan upload waits GPU access");
        vk_internal_wait(serial);
        vk_system_trace_end(wait_trace);
        continue;
      }
      pool_spans_copy_from_guest(p,off,size);
      memcpy(p->shadow + off, p->guest + off, size);
      POOL_COPY_ADD(mirror_bytes,size);
      POOL_COPY_ADD(shadow_bytes,size);
      pool_cpu_synced(p,off,size,recording_resources?recording_resources->cpu_authority_begin:0);
      pthread_mutex_unlock(&pool_publication_mu);
      break;
    }
    off += size;
    length -= size;
  }
}
void vk_pool_sync(void) {
#ifdef DM_VULKAN_SHARED_POOLS
  if(shared_active)die("Legacy whole-sync reference incompatible with fixed shared mode");
#endif
  uint64_t started = vk_profile_start();
  pthread_mutex_lock(&V.resources);
  size_t count = 0;
  for (pool *p = pools; p; p = p->next) if(p->backings) count++;
  pool **snapshot = count ? malloc(count * sizeof *snapshot) : NULL;
  if (count && !snapshot) die("Vulkan upload pool snapshot allocation");
  size_t n = 0;
  for (pool *p = pools; p; p = p->next) {
    if(!p->backings) continue;
    // A whole reservation may precede its first actual lookup. Preserve the
    // eager reference's CPU initialization, never manufacture data at reserve.
    pool_materialize_metadata(p);
    pool_initialize_range(p,0,p->size);
    p->references++;
    snapshot[n++] = p;
  }
  pthread_mutex_unlock(&V.resources);
  for (size_t i = 0; i < count; i++) {
    pool *p = snapshot[i];
    atomic_fetch_add(&sync_full_bytes,p->size);
    // 沿用原脏页代号，只比较 CPU
    // 可能改过的页；保护后再读取，避免整个池重复扫描。 Reuse original dirty
    // generations and compare only potentially changed pages; protect before
    // reading instead of repeatedly scanning whole pools.
    int tracked = nvn_dirty_tracked(p->guest, p->size);
    if (tracked && p->stamp && !nvn_dirty_since(p->guest, p->size, p->stamp))
      goto release;
    uint64_t stamp = nvn_dirty_now();
    if (tracked) {
      atomic_fetch_add(&sync_protected_bytes,p->size);
      nvn_dirty_protect(p->guest, p->size);
    }
    nvn_dirty_visit(p->guest, p->size, tracked ? p->stamp : 0, sync_page, p);
    p->stamp = tracked ? stamp : 0;
  release:
    pthread_mutex_lock(&V.resources);
    if (!p->references) die("Vulkan upload pool reference underflow");
    p->references--;
    pthread_cond_broadcast(&pool_cv);
    pthread_mutex_unlock(&V.resources);
  }
  free(snapshot);
  vk_profile_end(VK_TIME_POOL, started);
}
// Synchronize only buffer pages referenced by this recording. Images upload
// from their own CPU sources; an internal image-only present has no pool use.
// Keep per-page generations: advancing a pool-wide stamp after a partial sync
// would hide CPU edits in pages that this submission never visited.
typedef struct { pool *owner;uint64_t oldest; } used_pool_sync;
static void sync_used_changed_pages(const void *memory,size_t length,void *context) {
  used_pool_sync *sync=context;pool *p=sync->owner;
  uint64_t offset=(const uint8_t *)memory-p->guest;
  while(length) {
    size_t size=(1u<<GPU_ACCESS_SHIFT)-(offset&((1u<<GPU_ACCESS_SHIFT)-1));
    if(size>length)size=length;
    uint64_t prior=p->page_upload_stamps[offset>>GPU_ACCESS_SHIFT];
    if(prior<p->stamp)prior=p->stamp;
    // The range visitor used its oldest page stamp. A newer individual page
    // needs no byte comparison if its own dirty generation has not changed.
    // Recheck with the original tracker; do not infer this from GPU ownership
    // or from an address/contents cache. A later CPU edit must still be seen.
    if(prior>sync->oldest && !nvn_dirty_since(p->guest+offset,size,prior))
      upload_page_rechecks_skipped++;
    else sync_page(p->guest+offset,size,p);
    offset+=size;length-=size;
  }
}
#ifdef DM_VULKAN_SHARED_POOLS
#ifdef DM_VULKAN_TEST_EMBED
static pthread_mutex_t shared_dependency_test_mu=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t shared_dependency_test_cv=PTHREAD_COND_INITIALIZER;
static int shared_dependency_test_armed,shared_dependency_test_force_cache;
static uint64_t shared_dependency_test_observed;
void vk_test_shared_dependency_arm(int force_cache) {
  pthread_mutex_lock(&shared_dependency_test_mu);
  shared_dependency_test_armed=1;shared_dependency_test_observed=0;
  shared_dependency_test_force_cache=force_cache;
  pthread_mutex_unlock(&shared_dependency_test_mu);
}
uint64_t vk_test_shared_dependency_observed(void) {
  pthread_mutex_lock(&shared_dependency_test_mu);
  while(!shared_dependency_test_observed)
    pthread_cond_wait(&shared_dependency_test_cv,&shared_dependency_test_mu);
  uint64_t v=shared_dependency_test_observed;
  pthread_mutex_unlock(&shared_dependency_test_mu);return v;
}
#endif
// Host cache maintenance / CPU effects need a CPU phase between overlapping
// GPU users. Preserve asynchronous submission for nonoverlapping ranges.
// Call only on the translation owner, outside resource and queue locks.
static void shared_access_host_phase(pool *p,uint64_t offset,uint64_t bytes) {
  int cache=!(p->whole.buffer.properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
#ifdef DM_VULKAN_TEST_EMBED
  if(shared_dependency_test_force_cache)cache=1; // tests admission only, not NC Vk cache calls
#endif
  uint64_t lo=offset,hi=offset+bytes;
  if(cache) {
    uint64_t atom=V.props.limits.nonCoherentAtomSize;
    if(!atom || (atom&(atom-1)) || hi>UINT64_MAX-(atom-1))die("Shared host phase atom");
    lo&=~(atom-1);hi=(hi+atom-1)&~(atom-1);
    if(hi>p->size)die("Shared host phase exceeds allocation");
  }
  uint64_t serial=0,base=(uintptr_t)p->guest;
  pthread_mutex_lock(&completion_mu);
  for(submission *s=pending;s;s=s->next) {
    int conflict=0;
    // Stable snapshots remain valid until pending removal, after callbacks.
    for(size_t k=0;k<s->resources->cpu_effect_write_count;k++) {
      const cpu_write_range *e=&s->resources->cpu_writes[k];
      if(range_overlap(base+lo,hi-lo,e->address,e->size)){conflict=1;break;}
    }
    if(cache && !conflict)for(pool_gpu_access *u=s->resources->gpu_accesses;u;u=u->next)
      if(u->owner==p && bitmap_overlaps(u->pages,lo,hi-lo)){conflict=1;break;}
    if(conflict)serial=s->queue_serial;
  }
  pthread_mutex_unlock(&completion_mu);
  if(serial) {
#ifdef DM_VULKAN_TEST_EMBED
    pthread_mutex_lock(&shared_dependency_test_mu);
    if(shared_dependency_test_armed) {shared_dependency_test_armed=0;shared_dependency_test_observed=serial;
      pthread_cond_broadcast(&shared_dependency_test_cv);}
    pthread_mutex_unlock(&shared_dependency_test_mu);
#endif
    vk_internal_wait(serial); // true GPU + CPU effects, not a full-queue drain
  }
}
#endif
static void sync_used_run(pool *p,uint64_t first,uint64_t end,int tracked) {
#ifdef DM_VULKAN_SHARED_POOLS
  if(p->shared) {uint64_t off=first<<GPU_ACCESS_SHIFT,limit=end<<GPU_ACCESS_SHIFT;
    if(limit>p->size)limit=p->size;
    shared_access_host_phase(p,off,limit-off);
    shared_cache(&p->whole.buffer,off,limit-off,0);return;}
#endif
  uint64_t offset=first<<GPU_ACCESS_SHIFT,limit=end<<GPU_ACCESS_SHIFT;
  if(limit>p->size) limit=p->size;
  uint64_t stamp=UINT64_MAX;
  for(uint64_t page=first;page<end;page++) {
    uint64_t prior=p->page_upload_stamps[page];
    if(prior<p->stamp) prior=p->stamp;
    if(prior<stamp) stamp=prior;
  }
  atomic_fetch_add(&sync_used_bytes,limit-offset);
  if(tracked&&stamp&&!nvn_dirty_since(p->guest+offset,limit-offset,stamp)) return;
  uint64_t current=nvn_dirty_now();
  if(tracked) {
    atomic_fetch_add(&sync_protected_bytes,limit-offset);
    nvn_dirty_protect(p->guest+offset,limit-offset);
  }
  if(tracked && !recording_resources->upload_scan_reference) {
    used_pool_sync sync={.owner=p,.oldest=stamp};
    nvn_dirty_visit(p->guest+offset,limit-offset,stamp,sync_used_changed_pages,&sync);
  } else nvn_dirty_visit(p->guest+offset,limit-offset,tracked?stamp:0,sync_page,p);
  if(tracked) for(uint64_t page=first;page<end;page++) p->page_upload_stamps[page]=current;
}
static void sync_used_pools(void) {
  uint64_t started=vk_profile_start();
  for(pool_gpu_access *use=recording_resources->gpu_accesses;use;use=use->next) {
    pool *p=use->owner;
    int tracked;
#ifdef DM_VULKAN_SHARED_POOLS
    if(p->shared)tracked=0;else
#endif
    tracked=nvn_dirty_tracked(p->guest,p->size);
    uint64_t words=((p->size-1)>>(GPU_ACCESS_SHIFT+6))+1;
    uint64_t first=0,end=0,skipped=0;
    for(uint64_t word=0;word<words;word++) {
      uint64_t bits=use->upload_pages[word];
#ifdef DM_VULKAN_SHARED_POOLS
      if(p->shared)bits=use->pages[word]; // clean CPU cache before GPU writers too
#endif
      skipped+=__builtin_popcountll(use->pages[word]&~bits);
      while(bits) {
        uint64_t page=word*64+__builtin_ctzll(bits);bits&=bits-1;
        if(end&&page!=end) { sync_used_run(p,first,end,tracked);end=0; }
        if(!end) first=page;
        end=page+1;
      }
    }
    if(end) sync_used_run(p,first,end,tracked);
    if(skipped) atomic_fetch_add(&overwritten_upload_pages,skipped);
  }
  vk_profile_end(VK_TIME_POOL,started);
}
static pthread_mutex_t trace_request_mutex=PTHREAD_MUTEX_INITIALIZER;
static _Atomic int trace_requested;
static int trace_frame_requested;
static uint64_t trace_frame_request;
static char *trace_directory_request;
void vk_trace_request(int frame_set,uint64_t frame,const char *directory) {
  char *copy=directory?strdup(directory):NULL;
  if(directory&&!copy) die("Vulkan trace request allocation");
  pthread_mutex_lock(&trace_request_mutex);
  free(trace_directory_request);trace_directory_request=copy;
  trace_frame_requested=!!frame_set;trace_frame_request=frame;atomic_store(&trace_requested,1);
  pthread_mutex_unlock(&trace_request_mutex);
}
static void trace_begin_submit(void) {
  if(!atomic_load(&trace_requested)) return;
  pthread_mutex_lock(&trace_request_mutex);
  V.trace_frame_set=trace_requested?trace_frame_requested:V.options.trace_frame_set;
  V.trace_frame=trace_requested?trace_frame_request:V.options.trace_frame;
  const char *root=V.trace_frame_set?
      (trace_requested?trace_directory_request:V.options.trace_directory):NULL;
  // A running request is explicit and owned. The current submission keeps its
  // snapshot if the caller changes the request while its work is in flight.
  if ((!root && V.trace_directory) ||
      (root && (!V.trace_directory || strcmp(root, V.trace_directory)))) {
    char *copy = root ? strdup(root) : NULL;
    if (root && !copy) die("Vulkan trace directory allocation");
    free(V.trace_directory);
    V.trace_directory = copy;
  }
  pthread_mutex_unlock(&trace_request_mutex);
}
static void pool_window_before_submission(void);
static void begin_submission(int pool_migration) {
  mtl_init();
  if(!pool_migration)pool_window_before_submission();
  vk_graphics_prewarm();
  if (V.cmd)
    die("Vulkan nested submit");
  // 保留串行对照；并发资源到上限才等一份真实可复用资源，不在每次 begin 全局等完。
  // Retain the serial reference. At the resource cap wait only for one ready
  // set, rather than draining the whole queue on every begin.
  if (!async_submissions) mtl_finish();
  reclaim_submissions();
  if (recording_resources)
    die("Vulkan resource ownership overlaps submissions");
  // Early-submit batches share this budget. Eight sets caused observed
  // translation-thread pressure waits before the next guest fence; permit
  // more independent batches while retaining a bounded allocation count.
  if (!available_resources && resource_sets >= 16) {
    pthread_mutex_lock(&completion_mu);
    int pressure_trace=vk_system_trace_begin("Vulkan in-flight resource pressure");
    // The worker may already have published queue completion and removed the
    // last pending submission while still resetting its pool. Wait for real
    // ready ownership, never infer it from a completed queue serial.
#ifdef DM_VULKAN_TEST_EMBED
    if(!retired) {
      command_reset_test_pressure_waiters++;
      pthread_cond_broadcast(&completion_cv);
    }
    int pressure_waiting=!retired;
#endif
    while(!retired) pthread_cond_wait(&completion_cv,&completion_mu);
#ifdef DM_VULKAN_TEST_EMBED
    if(pressure_waiting)command_reset_test_pressure_waiters--;
#endif
    pthread_mutex_unlock(&completion_mu);
    vk_system_trace_end(pressure_trace);
    reclaim_submissions();
    if (!available_resources) die("Vulkan submission resource ownership lost at cap");
  }
  recording_resources = available_resources;
  if (recording_resources) {
    available_resources = recording_resources->next;
    recording_resources->next = NULL;
  } else {
    recording_resources = calloc(1, sizeof *recording_resources);
    if (!recording_resources) die("Vulkan submission resources allocation");
    resource_sets++;
  }
  pthread_mutex_lock(&completion_mu);
  recording_resources->queue_serial = queue_submitted + 1;
  recording_resources->pool_migration=pool_migration;
  recording_resources->cpu_authority_begin=(V.options.pool_window_bytes || V.options.pool_defer_publication)?pool_cpu_cutoff():0;
  recording_resources->pending_read_size=0;
  pthread_mutex_unlock(&completion_mu);
  int pool_reference=atomic_load(&pool_sync_requested);
  if(pool_reference<0) {
    const char *value=getenv("DM_VULKAN_POOL_SYNC_REFERENCE");
    if(value&&strcmp(value,"0")&&strcmp(value,"1")) die("DM_VULKAN_POOL_SYNC_REFERENCE expects 0 or 1");
    pool_reference=value&&!strcmp(value,"1");atomic_store(&pool_sync_requested,pool_reference);
  }
  recording_resources->pool_sync_reference=pool_reference;
  recording_resources->pool_lookup_count=recording_resources->pool_lookup_next=0;
  recording_resources->pool_lookup_reference=atomic_load(&pool_lookup_requested);
  atomic_store(&pool_lookup_active,recording_resources->pool_lookup_reference);
  recording_resources->upload_scan_reference=atomic_load(&upload_scan_requested);
  atomic_store(&upload_scan_active,recording_resources->upload_scan_reference);
  atomic_store(&pool_sync_active,pool_reference);
  int reference=atomic_load(&readback_reference_requested);
  if(reference<0) {
    const char *v=getenv("DM_VULKAN_READBACK_COHERENT_REFERENCE");
    if(v&&strcmp(v,"0")&&strcmp(v,"1")) die("DM_VULKAN_READBACK_COHERENT_REFERENCE expects 0 or 1");
    reference=v&&!strcmp(v,"1");atomic_store(&readback_reference_requested,reference);
  }
  atomic_store(&readback_reference_active,reference);
  reference=atomic_load(&cpu_copy_requested);
  if(reference<0) {
    const char *value=getenv("DM_VULKAN_CPU_COPY_REFERENCE");
    if(value&&strcmp(value,"0")&&strcmp(value,"1")) die("DM_VULKAN_CPU_COPY_REFERENCE expects 0 or 1");
    reference=value&&!strcmp(value,"1");atomic_store(&cpu_copy_requested,reference);
  }
  recording_resources->cpu_copy_reference=reference;atomic_store(&cpu_copy_active,reference);
  recording_resources->pool_upload_tail=&recording_resources->pool_uploads;
  trace_begin_submit();
  vk_counter_begin();
  recording_resources->graphics = vk_graphics_begin(recording_resources->graphics);
  vk_texture_end_submit();
  recording_resources->texture = vk_texture_begin(recording_resources->texture);
  recording_resources->shader = vk_shader_begin(recording_resources->shader);
  if(recording_resources->pool_sync_reference && !pool_migration) vk_pool_sync();
  int submission_reference=atomic_load(&submission_barrier_requested);
  if(submission_reference<0) {
    const char *setting=getenv("DM_VULKAN_SUBMISSION_BARRIER_REFERENCE");
    if(setting && strcmp(setting,"0") && strcmp(setting,"1"))
      die("DM_VULKAN_SUBMISSION_BARRIER_REFERENCE expects 0 or 1");
    submission_reference=setting&&!strcmp(setting,"1");
    atomic_store(&submission_barrier_requested,submission_reference);
  }
  recording_resources->submission_barrier_reference=submission_reference;
  if(atomic_exchange(&submission_barrier_active,submission_reference)!=submission_reference)
    LOG("[vulkan] submission dependencies: %s",submission_reference?"global reference":"scoped readback; deferred GPU consumers");
  V.cmd = vk_command_buffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
  VkCommandBufferBeginInfo bi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
  VK_CHECK(vkBeginCommandBuffer(V.cmd, &bi));
  submit_resources *r = recording_resources;
  gpu_profile_begin_submit(r);
  r->timing_active = V.timestamp_bits && !external_profile;
  if (r->timing_active) {
    // 仅重置本份已完成资源的查询，别的在途提交可持有自己的查询池。
    // Reset only queries owned by this completed resource set. Other in-flight
    // submissions may retain separate query pools.
    if (!r->timing_queries) {
      VkQueryPoolCreateInfo qi = {.sType =
                                      VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                  .queryType = VK_QUERY_TYPE_TIMESTAMP,
                                  .queryCount = 2};
      VK_CHECK(vkCreateQueryPool(V.device, &qi, NULL, &r->timing_queries));
    }
    vkCmdResetQueryPool(V.cmd, r->timing_queries, 0, 2);
    vkCmdWriteTimestamp(V.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        r->timing_queries, 0);
  }
  r->readback_bytes=0;
  V.effects = NULL;
  V.effect_tail = &V.effects;
  // Queue submission makes pre-submit host writes available. GPU consumers
  // still synchronize at their actual transfer/compute/render dependency.
  // Preserve pending buffer/vertex visibility across command-buffer boundaries.
  if(r->submission_barrier_reference) vk_barrier();
}
void mtl_sub_begin(void) { begin_submission(0); }

static void effect_copy_kind(void *dst,const void *src,size_t size,uint32_t kind) {
  vk_effect *e = calloc(1, sizeof *e);
  if (!e)
    die("Vulkan completion allocation");
  e->dst = dst;
  e->src = src;
  e->size = size;
  if(V.options.pool_copy_accounting)e->mode=kind;
  *V.effect_tail = e;
  V.effect_tail = &e->next;
}
void vk_effect_copy(void *dst,const void *src,size_t size) {
  effect_copy_kind(dst,src,size,POOL_COPY_GENERIC);
}
static void buffer_writeback_intent(uint64_t dst,uint64_t size,const uint8_t *known,
                             VkPipelineStageFlags producers,int actual_write) {
  V.buffer_epoch++;
  if (!size) return;
  if (!recording_resources) die("Vulkan writeback outside a submission");
  pthread_mutex_lock(&V.resources);
  pool *p = pool_find(dst, size);
  if (!p)
    die("Vulkan writeback outside a pool");
  pool_materialize(p);
  uint64_t off = dst - (uintptr_t)p->guest;
  pool_initialize_range(p,off,size);
  if(actual_write)pool_cpu_written(p,off,size);
  uint64_t known_cutoff=known&&(p->cpu_authority_tracking||p->cpu_copy_requires_gpu)?pool_cpu_cutoff():0;
  record_pool_access(p, off, size,producers);
  pool_writeback *w = recording_resources->writebacks;
  while (w && w->owner != p) w = w->next;
  if (!w) {
    w = calloc(1, sizeof *w);
    if (!w) die("Vulkan writeback pool allocation");
    w->owner = p;
    w->next = recording_resources->writebacks;
    recording_resources->writebacks = w;
  }
  // 仅合并相交或相邻范围，不覆盖空隙；所有范围都在 pool_find 验过边界。
  // Merge only overlapping/adjacent intervals, never gaps. pool_find validated
  // every endpoint before addition.
  uint64_t end = off + size,known_offset=off,known_end=end;
  writeback_range **at = &w->ranges;
  while (*at && (*at)->end < off) at = &(*at)->next;
  while (*at && (*at)->offset <= end) {
    writeback_range *old = *at;
    producers |= old->producers;
    if (old->offset < off) off = old->offset;
    if (old->end > end) end = old->end;
    *at = old->next;
    free(old);
  }
  writeback_range *r = malloc(sizeof *r);
  if (!r) die("Vulkan writeback range allocation");
  // Merging a partial/adjacent write must invalidate the complete proof. A new
  // known write may replace an old interval only when it covers their union.
  if(off!=known_offset||end!=known_end) known=NULL;
  *r = (writeback_range){.next=*at,.offset=off,.end=end,.known=known,
    .known_cutoff=known?known_cutoff:0,.producers=producers};
  *at = r;
  pthread_mutex_unlock(&V.resources);
}
static void buffer_writeback(uint64_t dst,uint64_t size,const uint8_t *known,VkPipelineStageFlags stages) {
  buffer_writeback_intent(dst,size,known,stages,1);
}
#include "nvn_vk_cpu_demand.inc"
void vk_buffer_writeback_stage(uint64_t dst,uint64_t size,VkPipelineStageFlags producers) {
  buffer_writeback(dst,size,NULL,producers);
}
void vk_buffer_writeback(uint64_t dst,uint64_t size) {
  buffer_writeback(dst,size,NULL,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT);
}
static uint8_t *cpu_copy_allocate(uint64_t size) {
  uint8_t *snapshot=malloc(size);
  if(!snapshot) die("Vulkan known buffer copy allocation");
  // Completion publishes every copy effect before running any free callbacks.
  vk_completion(free,snapshot);atomic_fetch_add(&cpu_copy_snapshots,size);
  return snapshot;
}
void vk_buffer_writeback_known(uint64_t dst,uint64_t size,const void *bytes) {
  const uint8_t *known=NULL;
  if(size&&!recording_resources->cpu_copy_reference) {
    uint8_t *snapshot=cpu_copy_allocate(size);memcpy(snapshot,bytes,size);known=snapshot;
    POOL_COPY_ADD(known_snapshot_bytes,size);
  }
  buffer_writeback(dst,size,known,VK_PIPELINE_STAGE_TRANSFER_BIT);
}
int vk_buffer_writeback_known_temporary(uint64_t dst,uint64_t size,const vk_buffer *source) {
  // A just-created private upload is the head of this recording's retained
  // temporaries. Do not accept an arbitrary caller map, an older submission's
  // buffer, or a copied VkBuffer wrapper as an immutable lifetime proof.
  submit_resources *r=recording_resources;
  if(!V.cmd || !r || !size || !source || !r->temporaries ||
      source!=&r->temporaries->b || source->readback || !source->map ||
      !source->handle || size>source->size ||
      (source->properties&(VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT))!=
          (VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT))return 0;
  // Require cached CPU reads too: borrowing an uncached/write-combined upload
  // could replace cached heap reads with slower completion reads. Such devices
  // retain the old copying API. Caller promises no further writes. Recycle
  // occurs only after all copy effects/callbacks complete and the pool resets;
  // no extra snapshot/free callback or pool-content prediction is required.
  buffer_writeback(dst,size,r->cpu_copy_reference?NULL:source->map,VK_PIPELINE_STAGE_TRANSFER_BIT);
  return 1;
}
void vk_buffer_writebacks_flush(void) {
  if (!recording_resources || !V.cmd) die("Vulkan writeback flush outside submission");
  if (!recording_resources->writebacks) return;
  if(recording_resources->submission_barrier_reference) vk_barrier();
  else vk_render_end();
  pool_writeback *list = recording_resources->writebacks;
  recording_resources->writebacks = NULL;
  int copied=0;
  while (list) {
    pool_writeback *w = list;
    list = w->next;
#ifdef DM_VULKAN_SHARED_POOLS
    if(w->owner->shared) {
      for(writeback_range *r=w->ranges;r;r=r->next) {
        VkBufferMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
          .srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,
          .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
          .buffer=w->owner->whole.buffer.handle,.offset=r->offset,.size=r->end-r->offset};
        vkCmdPipelineBarrier(V.cmd,r->producers,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&b,0,NULL);
      }
      w->next=recording_resources->shared_publishes;recording_resources->shared_publishes=w;continue;
    }
#endif
    writeback_range **range_at=&w->ranges;
    while(*range_at) {
      writeback_range *r=*range_at;
      uint64_t size = r->end - r->offset;
      if(!r->known && pool_publication_can_defer(w->owner,r->offset,size)) {
        *range_at=r->next;free(r);continue; // no CPU synchronization proof was produced
      }
      if(r->known) {
        effect_copy_kind(w->owner->guest+r->offset,r->known,size,POOL_COPY_KNOWN_GUEST);
        effect_copy_kind(w->owner->shadow+r->offset,r->known,size,POOL_COPY_KNOWN_SHADOW);
        atomic_fetch_add(&cpu_copy_published,size);
        range_at=&r->next;continue;
      }
      copied=1;
      recording_resources->readback_bytes+=size;
      atomic_fetch_add(&sync_readback_ranges,1);atomic_fetch_add(&sync_readback_bytes,size);
      pool_span_iter it;
      if(!pool_span_iter_begin(w->owner,r->offset,size,&it))die("Vulkan writeback bounds");
      pool_span_piece span;int span_status;
      while((span_status=pool_span_iter_next(&it,&span))==1) {
        vk_buffer *snapshot=temp_readback(span.size);
        if(snapshot->readback&&(snapshot->properties&VK_MEMORY_PROPERTY_HOST_CACHED_BIT))
          atomic_fetch_add(&cached_readback_bytes,span.size);
        VkBufferCopy copy={.srcOffset=span.offset,.size=span.size};
        if(!recording_resources->submission_barrier_reference) {
          VkBufferMemoryBarrier dependency={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,
            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .buffer=span.backing->buffer.handle,.offset=span.offset,.size=span.size};
          vkCmdPipelineBarrier(V.cmd,r->producers,VK_PIPELINE_STAGE_TRANSFER_BIT,
                               0,0,NULL,1,&dependency,0,NULL);
        }
        // Keep logical page ownership separate from the local physical copy.
        pthread_mutex_lock(&V.resources);
        record_pool_access(w->owner,span.logical,span.size,VK_PIPELINE_STAGE_TRANSFER_BIT);
        pthread_mutex_unlock(&V.resources);
        vkCmdCopyBuffer(V.cmd,span.backing->buffer.handle,snapshot->handle,1,&copy);
        if(recording_resources->submission_barrier_reference)vk_buffer_transfer_written();
        effect_copy_kind(w->owner->guest+span.logical,snapshot->map,span.size,POOL_COPY_READBACK_GUEST);
        effect_copy_kind(w->owner->shadow+span.logical,snapshot->map,span.size,POOL_COPY_READBACK_SHADOW);
      }
      if(span_status<0)die("Vulkan writeback across unbacked hole");
      // Unlike a known CPU value captured at its original copy, a GPU
      // readback snapshots every earlier writer at this physical command.
      r->known_cutoff=(w->owner->cpu_authority_tracking||w->owner->cpu_copy_requires_gpu)?pool_cpu_cutoff():0;
      range_at=&r->next;
    }
    pool_cpu_publish_ranges(w);
  }
  VkMemoryBarrier host_read = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
  if(copied) vkCmdPipelineBarrier(V.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host_read, 0, NULL, 0, NULL);
}
void mtl_sub_copy(uint64_t src, uint64_t dst, uint64_t size) {
  if (!size)
    return;
  vk_pool_requirement needs[]={
    {src,size,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0},
    {dst,size,0,VK_PIPELINE_STAGE_TRANSFER_BIT,1,0}};
  int operation=vk_pool_operation_begin_ranges(needs,2);
  vk_buffer *s, *d;
  uint64_t so, doff;
  if (!vk_pool_address_stage(src,size,(void **)&s,&so,VK_PIPELINE_STAGE_TRANSFER_BIT) ||
      !vk_pool_address_overwrite(dst,size,(void **)&d,&doff,VK_PIPELINE_STAGE_TRANSFER_BIT))
    die("Vulkan copy outside pools: %#llx -> %#llx +%llu",
        (unsigned long long)src, (unsigned long long)dst,
        (unsigned long long)size);
  // vkCmdCopyBuffer 按字节定义范围，沿原 Metal 拷贝语义保留未对齐首尾。
  // vkCmdCopyBuffer ranges are byte-addressed, matching original Metal copies.
  if (s == d && so < doff + size && doff < so + size)
    die("Vulkan overlapping buffer copy");
  const uint8_t *known=NULL;
  if(!recording_resources->cpu_copy_reference) {
    // No preceding GPU/CPU publication may overlap; never wait for this proof.
    // Pool uploads happen at commit, so fill the immutable snapshot *after*
    // those uploads, from their exact shadow bytes, rather than reading stale
    // shadow now or potentially newer guest memory later at completion.
    if(!vk_pool_pending_writes(src,size)) {
      pthread_mutex_lock(&V.resources);
      pool *source=pool_find(src,size);
      if(source && pool_cpu_copy_proven(source,src-(uintptr_t)source->guest,size)) {
        pending_cpu_copy *copy=malloc(sizeof *copy);
        if(!copy) die("Vulkan known copy source allocation");
        uint8_t *snapshot=cpu_copy_allocate(size);
        *copy=(pending_cpu_copy){.next=recording_resources->cpu_copies,.source=source,
          .offset=src-(uintptr_t)source->guest,.size=size,.snapshot=snapshot};
        recording_resources->cpu_copies=copy;known=snapshot;
      }
      pthread_mutex_unlock(&V.resources);
    }
    if(!known) atomic_fetch_add(&cpu_copy_fallbacks,size);
  }
  const vk_buffer_use uses[]={{src,size,0},{dst,size,1}};
  vk_transfer_buffer_dependencies(uses,2);
  VkBufferCopy c = {.srcOffset = so, .dstOffset = doff, .size = size};
  vkCmdCopyBuffer(V.cmd, s->handle, d->handle, 1, &c);
  vk_buffer_transfer_written_range(dst,size);
  vk_pool_overwritten(dst,size);
  buffer_writeback(dst,size,known,VK_PIPELINE_STAGE_TRANSFER_BIT);
  vk_pool_operation_end(operation);
}
void mtl_sub_event(volatile uint32_t *val, uint32_t mode, uint32_t v) {
  if (mode > 1)
    die("Vulkan unsupported event mode %u", mode);
  if(vk_event_signal((uintptr_t)val,mode,v))return;
  // Preserve the original backend's non-Pool completion contract (including
  // stack events in owned fixtures). Registered NVN Pool payloads never fall
  // back to a CPU store, even in fixed shared mode.
  if(V.options.pool_defer_publication)vk_pool_cpu_prepare((uintptr_t)val,sizeof *val);
  vk_effect *e = calloc(1, sizeof *e);
  if (!e)
    die("Vulkan event allocation");
  e->dst = (void *)val;
  e->size = sizeof *val;
  e->mode = mode;
  e->value = v;
  *V.effect_tail = e;
  V.effect_tail = &e->next;
}

static uint64_t commit(int guest_submit, VkSemaphore wait, VkPipelineStageFlags wait_stage,
                       VkSemaphore signal) {
  if (!V.cmd)
    die("Vulkan commit without begin");
  vk_mip_clear_drain();
  vk_counter_flush();
  if(recording_resources->submission_barrier_reference) vk_barrier();
  else vk_render_end();
  vk_buffer_writebacks_flush();
  submit_resources *r = recording_resources;
  if (!r) die("Vulkan commit without submission resources");
  // Covers query-result copies as well as pool snapshots. HOST is the actual
  // consumer; no ALL_COMMANDS destination that serializes later GPU stages.
  VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,
                       0,1,&host,0,NULL,0,NULL);
  if (r->timing_active)
    vkCmdWriteTimestamp(V.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        r->timing_queries, 1);
  VK_CHECK(vkEndCommandBuffer(V.cmd));
  vk_graphics_end(r->graphics);
  vk_shader_end(r->shader);
  vk_texture_end(r->texture);
  vk_pass_stats_publish();
  atomic_store_explicit(&graphics_stage_relaxed,recorded_graphics_stage_relaxed,memory_order_relaxed);
  atomic_store_explicit(&graphics_stage_fallback,recorded_graphics_stage_fallback,memory_order_relaxed);
  pool_window_publish();
  atomic_store(&published_pool_lookup_hits,pool_lookup_hits);
  atomic_store(&published_pool_range_reuses,pool_range_reuses);
  atomic_store(&published_pool_lookup_slow,pool_lookup_slow);
  submission *s = calloc(1, sizeof *s);
  if (!s)
    die("Vulkan submission allocation");
  VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  VK_CHECK(vkCreateFence(V.device, &fi, NULL, &s->fence));
  s->cmd = V.cmd;
  s->frame = V.frame;
  s->draws = V.draws;
  s->effects = V.effects;
  s->resources = r;
  for (vk_effect *e = s->effects; e; e = e->next)
    if (e->dst && e->size) r->cpu_write_count++;
#ifdef DM_VULKAN_SHARED_POOLS
  r->cpu_effect_write_count=r->cpu_write_count;
  for(pool_writeback *w=r->shared_publishes;w;w=w->next)
    for(writeback_range *range=w->ranges;range;range=range->next)r->cpu_write_count++;
#endif
  if (r->cpu_write_count) {
    r->cpu_writes = malloc(r->cpu_write_count * sizeof *r->cpu_writes);
    if (!r->cpu_writes) die("Vulkan CPU-write range snapshot allocation");
    size_t i = 0;
    for (vk_effect *e = s->effects; e; e = e->next)
      if (e->dst && e->size)
        r->cpu_writes[i++] = (cpu_write_range){(uintptr_t)e->dst, e->size};
#ifdef DM_VULKAN_SHARED_POOLS
    for(pool_writeback *w=r->shared_publishes;w;w=w->next)
      for(writeback_range *range=w->ranges;range;range=range->next)
        r->cpu_writes[i++]=(cpu_write_range){(uintptr_t)w->owner->guest+range->offset,range->end-range->offset};
#endif
  }
#ifdef DM_VULKAN_SHARED_POOLS
  if(shared_active)for(size_t k=0;k<r->cpu_effect_write_count;k++) {
    const cpu_write_range *e=&r->cpu_writes[k];
    for(pool_gpu_access *u=r->gpu_accesses;u;u=u->next) {
      pool *p=u->owner;if(!p->shared)continue;
      uint64_t base=(uintptr_t)p->guest,lo=e->address>base?e->address:base;
      uint64_t hi=e->size<=UINT64_MAX-e->address?e->address+e->size:UINT64_MAX;
      if(hi>base+p->size)hi=base+p->size;
      if(lo<hi && bitmap_overlaps(u->pages,lo-base,hi-lo))
        die("Fixed shared CPU effect overlaps same-recording GPU page; GPU-ordered effect not implemented");
    }
  }
#endif
  VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                     .waitSemaphoreCount = wait != VK_NULL_HANDLE,
                     .pWaitSemaphores = wait ? &wait : NULL,
                     .pWaitDstStageMask = wait ? &wait_stage : NULL,
                     .commandBufferCount = 1,
                     .pCommandBuffers = &s->cmd,
                     .signalSemaphoreCount = signal != VK_NULL_HANDLE,
                     .pSignalSemaphores = signal ? &signal : NULL};
  if(r->pool_migration) {
    if(r->writebacks || r->cpu_copies || r->pool_uploads || V.effects)
      die("Vulkan backing migration acquired guest publication/upload effects");
    for(pool_gpu_access *use=r->gpu_accesses;use;use=use->next) {
      uint64_t words=((use->owner->size-1)>>(GPU_ACCESS_SHIFT+6))+1;
      for(uint64_t word=0;word<words;word++)if(use->upload_pages[word])
        die("Vulkan backing migration acquired CPU upload input");
    }
  } else if(r->pool_sync_reference) vk_pool_sync();
  else sync_used_pools();
  atomic_store(&published_upload_page_rechecks_skipped,upload_page_rechecks_skipped);
  record_pool_upload_commands();
  VkCommandBuffer commands[2]={r->uploads,s->cmd};
  if(r->uploads) {
    si.commandBufferCount=2;si.pCommandBuffers=commands;
  }
  while(r->cpu_copies) {
    pending_cpu_copy *copy=r->cpu_copies;r->cpu_copies=copy->next;
    memcpy(copy->snapshot,copy->source->shadow+copy->offset,copy->size);
    POOL_COPY_ADD(known_snapshot_bytes,copy->size);
    free(copy);
  }
  s->system_trace=vk_system_trace_async_begin("Vulkan submit to observed fence",r->queue_serial);
  int queue_lock_trace=vk_system_trace_begin("Vulkan submit queue lock");
  pthread_mutex_lock(&V.queue_mu);
  vk_system_trace_end(queue_lock_trace);
  int submit_trace=vk_system_trace_begin("Vulkan queue submit");
  VK_CHECK(vkQueueSubmit(V.queue, 1, &si, s->fence));
  vk_system_trace_end(submit_trace);
  if(guest_submit) atomic_fetch_add(&sync_guest_submits,1);
  else { atomic_fetch_add(&sync_internal_submits,1);atomic_fetch_add(&sync_internal_readback_bytes,r->readback_bytes); }

  pthread_mutex_unlock(&V.queue_mu);
  pthread_mutex_lock(&completion_mu);
  if (r->timing_active) {
    unsigned k = s->frame % GPU_TIME_RING;
    if (!gpu_times[k].valid || gpu_times[k].frame != s->frame) {
      gpu_times[k].frame = s->frame;
      gpu_times[k].ns = gpu_times[k].pending = 0;
      gpu_times[k].valid = 1;
    }
    gpu_times[k].pending++;
  }
  s->serial = guest_submit ? ++submitted : 0;
  s->queue_serial = ++queue_submitted;
  if (s->queue_serial != r->queue_serial) die("Vulkan recording serial changed at commit");
  uint64_t serial = guest_submit ? s->serial : s->queue_serial;
  // All guest/internal/stream/present submits reach this one enqueue point.
  // A recycled recording can never inherit a negative result across it.
  r->pending_read_size=0;
  *pending_tail = s;
  pending_tail = &s->next;
  atomic_fetch_add(&in_flight, 1);
  pthread_cond_broadcast(&completion_cv);
  pthread_mutex_unlock(&completion_mu);
  recording_resources = NULL;
  V.cmd = VK_NULL_HANDLE;
  V.effects = NULL;
  V.effect_tail = NULL;
  return serial;
}
uint64_t mtl_sub_commit(void) { return commit(1, VK_NULL_HANDLE, 0, VK_NULL_HANDLE); }
uint64_t vk_internal_commit(void) { return commit(0, VK_NULL_HANDLE, 0, VK_NULL_HANDLE); }
uint64_t vk_internal_commit_semaphores(VkSemaphore wait, VkPipelineStageFlags stage,
                                       VkSemaphore signal) {
  return commit(0, wait, stage, signal);
}
void vk_internal_wait(uint64_t serial) {
  pthread_mutex_lock(&completion_mu);
  int wait_trace=queue_completed<serial?vk_system_trace_begin("Vulkan CPU waits queue completion"):0;
  while (queue_completed < serial)
    pthread_cond_wait(&completion_cv, &completion_mu);
  pthread_mutex_unlock(&completion_mu);
  vk_system_trace_end(wait_trace);
}
void vk_present_ticket_wait(uint64_t serial) {
  // x_present_ticket already waited for the translation/presentation job.
  // Unlike Metal's separate present worker, vk_window_present returns only
  // after the blit is submitted. Reusing an NVN texture needs that ordering,
  // not CPU publication of all GPU results in the ticket's submission.
  if(!async_submissions || vk_submission_barrier_mode()!=0) {
    vk_internal_wait(serial);
    return;
  }
  pthread_mutex_lock(&completion_mu);
  int submitted_ticket=serial<=queue_submitted;
  pthread_mutex_unlock(&completion_mu);
  if(!submitted_ticket) die("Vulkan present ticket was not submitted");
}
uint64_t mtl_last_fence(void) {
  pthread_mutex_lock(&completion_mu);
  uint64_t f = submitted;
  pthread_mutex_unlock(&completion_mu);
  return f;
}
int mtl_wait(uint64_t fence, uint64_t timeout_ns) {
  struct timespec deadline;
  uint64_t ns = dm_platform_clock_ns(CLOCK_REALTIME);
  if (timeout_ns != UINT64_MAX) {
    ns = timeout_ns > UINT64_MAX - ns ? UINT64_MAX : ns + timeout_ns;
    deadline.tv_sec = ns / 1000000000ull;
    deadline.tv_nsec = ns % 1000000000ull;
  }
  pthread_mutex_lock(&completion_mu);
  int wait_trace=completed<fence&&timeout_ns?vk_system_trace_begin("NVN CPU waits guest GPU fence"):0;
  while (completed < fence) {
    if (!timeout_ns)
      break;
    int r =
        timeout_ns == UINT64_MAX
            ? pthread_cond_wait(&completion_cv, &completion_mu)
            : pthread_cond_timedwait(&completion_cv, &completion_mu, &deadline);
    if (r == ETIMEDOUT)
      break;
    if (r)
      die("Vulkan fence condition wait: %d", r);
  }
  int done = completed >= fence;
  pthread_mutex_unlock(&completion_mu);
  vk_system_trace_end(wait_trace);
  return done;
}
void mtl_finish(void) {
  uint64_t started = vk_profile_start();
  pthread_mutex_lock(&completion_mu);
  uint64_t serial = queue_submitted;
  pthread_mutex_unlock(&completion_mu);
  vk_internal_wait(serial);
  // Explicit Finish/Drain is also used before pool/texture/Surface teardown.
  // Ordinary mtl_wait/vk_internal_wait deliberately stop at GPU+CPU effects;
  // these teardown callers additionally require driver reset to have returned.
  pthread_mutex_lock(&completion_mu);
  int reset_trace=queue_reset_completed<serial?
      vk_system_trace_begin("Vulkan finish waits resource reset"):0;
#ifdef DM_VULKAN_TEST_EMBED
  int reset_waiting=queue_reset_completed<serial;
  if(reset_waiting) {
    command_reset_test_finish_waiters++;
    pthread_cond_broadcast(&completion_cv);
  }
#endif
  while(queue_reset_completed<serial)
    pthread_cond_wait(&completion_cv,&completion_mu);
#ifdef DM_VULKAN_TEST_EMBED
  if(reset_waiting)command_reset_test_finish_waiters--;
#endif
  pthread_mutex_unlock(&completion_mu);
  vk_system_trace_end(reset_trace);
  vk_profile_end(VK_TIME_WAIT, started);
}
#include "nvn_vk_pool_migration.inc"
#include "nvn_vk_pool_policy.inc"
void mtl_drain(void) { mtl_finish(); }
#ifdef DM_VULKAN_TEST_EMBED
// Deterministic ownership-gap fixture only. These expose logical readiness,
// not performance timing, and never change production GPU/effect semantics.
void vk_command_reset_test_gate(int enabled) {
  pthread_mutex_lock(&completion_mu);
  if(enabled&&command_reset_test_blocked)die("Vulkan reset fixture gate overlaps");
  atomic_store(&command_reset_test_enabled,!!enabled);
  pthread_cond_broadcast(&completion_cv);
  pthread_mutex_unlock(&completion_mu);
}
uint64_t vk_command_reset_test_wait_blocked(uint64_t serial) {
  pthread_mutex_lock(&completion_mu);
  while(command_reset_test_blocked<serial)
    pthread_cond_wait(&completion_cv,&completion_mu);
  uint64_t blocked=command_reset_test_blocked;
  pthread_mutex_unlock(&completion_mu);
  return blocked;
}
uint64_t vk_command_reset_test_release_when_waiting(int pressure) {
  pthread_mutex_lock(&completion_mu);
  while(!(pressure?command_reset_test_pressure_waiters:command_reset_test_finish_waiters))
    pthread_cond_wait(&completion_cv,&completion_mu);
  uint64_t blocked=command_reset_test_blocked;
  atomic_store(&command_reset_test_enabled,0);
  pthread_cond_broadcast(&completion_cv);
  pthread_mutex_unlock(&completion_mu);
  return blocked;
}
void vk_command_reset_test_snapshot(uint64_t *done,uint64_t *ready,unsigned *sets) {
  // The fixture invokes this on the submission owner only; resource_sets is
  // not a shared mutable counter. The worker-owned serials need the mutex.
  pthread_mutex_lock(&completion_mu);
  *done=queue_completed;*ready=queue_reset_completed;*sets=resource_sets;
  pthread_mutex_unlock(&completion_mu);
}
#endif

void mtl_res_snapshot(mtl_res_count *out) {
  memset(out, 0, sizeof *out);
  pthread_mutex_lock(&V.resources);
  for (pool *p = pools; p; p = p->next)
    out->pools++;
  out->pool_adds = pool_adds;
  out->live_tex = vk_texture_count();
  out->pso = vk_graphics_pipeline_count();
  pthread_mutex_unlock(&V.resources);
}
void mtl_res_counts(uint32_t tick, const char *why) {
  mtl_res_count c;
  mtl_res_snapshot(&c);
  LOG("[vulkan] resources tick=%u %s pools=%u total=%llu", tick, why, c.pools,
      (unsigned long long)c.pool_adds);
}

#ifdef DM_VULKAN_SHARED_POOLS
#include "nvn_vk_shared_pool.inc"
#endif
