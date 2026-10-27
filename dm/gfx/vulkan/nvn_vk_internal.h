// 原 NVN 回放格式的 Vulkan 执行层；保留 mtl_* C ABI 以复用接口层。
// Vulkan executor of the original NVN replay format; keeps the mtl_* C ABI to
// reuse the interface layer.
#pragma once
#include "../../host/dm.h"
#include "../nvn/nvn_mtl.h"
#include "../nvn/nvn_obj.h"
#include "../nvn/nvn_platform.h"
#include <pthread.h>
#ifdef __ANDROID__
#define VK_USE_PLATFORM_ANDROID_KHR 1
#endif
#include "nvn_vk_loader.h"
#include "nvn_vk_queue_sync.h"
#include "nvn_vk_memory.h"
#include "nvn_vk_pool_ranges.h"
#define VK_CHECK(call)                                                         \
  do {                                                                         \
    VkResult r_ = (call);                                                      \
    if (r_ != VK_SUCCESS)                                                      \
      die("Vulkan %s: %d", #call, r_);                                         \
  } while (0)
typedef struct vk_buffer {
  VkBuffer handle;
  VkDeviceMemory memory;
  void *allocation;
  uint8_t *map;
  VkDeviceSize size;
  // Immutable views belong to this physical Buffer, never a submission or bare handle.
  struct vk_texel_view_cache *texel_views;
  VkMemoryPropertyFlags properties;
  int readback;
#ifdef DM_VULKAN_SHARED_POOLS
  int shared_import;
  uint32_t shared_memory_type;
#endif
} vk_buffer;
void vk_buffer_views_destroy(vk_buffer *buffer);
typedef struct vk_effect {
  struct vk_effect *next;
  void *dst;
  const void *src;
  size_t size;
  uint32_t mode, value;
  void (*complete)(void *);
  void *context;
} vk_effect;
// Environment options are immutable after device initialization. Runtime
// controls use explicit requests; never re-read the process environment.
typedef struct {
  int stats,validate,tbdr,load_probe,freeze,freeze_negative;
  int defer_mip_clears;
  int astc_decode_unorm8;
  int runtime,runtime_forced,runtime_toggle,vertex_divisor_fallback;
  int store_reference,store_check,store_keep_all,ruler,extrap_no_pipeline,extrap_mask;
  int render_record_mode; // 0 ordinary; 1 fixed attachment secondary; 2 identical fixed attachment primary.
  int query_attachment_reference; // Cold comparison of conservative query retry.
  int state_segments; // Startup-only experimental validated state-span replay.
  int gpu_profile,trace_frame_set;
  int pool_ownership_reference;
  int pool_access_cache_reference;
  int pool_mirror_reference;
  int pool_copy_accounting;
  int pool_defer_publication;
  int shared_image_raw_observe; // Qualified coherent CPU-image GPU byte scope; startup ON.
  uint64_t pool_window_bytes;
  unsigned descriptor_sets;
  uint64_t trace_frame,frame_dump_at;
  const char *shader_root,*trace_directory,*frame_dump,*pipeline_cache_directory,*memory_dump_directory;
} vk_startup_options;
typedef struct {
  vk_startup_options options;
  VkInstance instance;
  VkSurfaceKHR surface;
  VkPhysicalDevice physical;
  VkDevice device;
  VkQueue queue,present_queue;
  uint32_t queue_count;
  int present_worker;
  int queue_internal_sync; // Actual enabled KHR feature AND queue creation flag.
  VkPipelineCache pipeline_cache;
  uint32_t queue_family;
  uint32_t timestamp_bits;
  VkPhysicalDeviceProperties props;
  VkPhysicalDeviceMemoryProperties mem;
  VkPhysicalDeviceFeatures features;
  VkBool32 shader_draw_parameters;
  VkBool32 shader_float16;
  int astc_decode_mode;
  int display_timing;
  int sampler_mirror_clamp;
  int stencil_export;
  int load_store_op_none;
  int timeline_semaphore;
  PFN_vkSignalSemaphoreKHR signal_timeline;
  uint32_t max_vertex_divisor;
  VkCommandBuffer cmd;
  vk_effect *effects, **effect_tail;
  pthread_mutex_t resources;
  pthread_mutex_t queue_mu,present_queue_mu;
  uint64_t frame, passes, draws;
  // 诊断配置在提交入口快照，关闭时绘制热路径不查环境变量。
  // Snapshot diagnostic options at submission boundaries, outside draw hot paths.
  uint64_t trace_frame;
  int trace_frame_set;
  char *trace_directory;
  uint64_t buffer_epoch;
  uint32_t nvn_barrier;
} vk_context;
extern vk_context V;
int vk_tbdr_enabled(void);
void vk_tbdr_reference(int reference);
int vk_load_probe_mode(void);
void vk_load_probe_request(int mode);
void vk_vertex_divisor_fallback(int enabled);
void vk_gpu_profile_enabled(int enabled);
void vk_trace_request(int frame_set,uint64_t frame,const char *directory);
void vk_pipeline_cache_init(void);
void vk_pipeline_cache_begin(void);
void vk_pipeline_cache_end(int created);
void vk_pipeline_cache_changed(void);
void vk_pipeline_cache_checkpoint(void);
VkPipelineCache vk_pipeline_cache_load(const char *path,size_t *loaded);
const char *vk_pipeline_cache_path(void);
static inline int vk_trace_frame_active(void) {
  return V.trace_frame_set && V.frame == V.trace_frame;
}
uint32_t vk_memory_type(uint32_t bits, VkMemoryPropertyFlags flags);
// Metadata-only proof; never uploads, records hazards, or materializes a pool.
int vk_pool_range_offset(uint64_t addr,uint64_t size,uint64_t *offset);
// Non-owning value snapshot for operation preflight, not permission to record
// GPU work. Coordinates are pool-relative; local_offset is relative to handle.
// A real address lookup still retains the pool through fence AND CPU effects.
typedef struct vk_pool_span_info {
  uint64_t pool_identity,pool_base,pool_size;
  uint64_t backing_generation,logical_begin,logical_end,local_offset,size;
  VkBuffer handle;
} vk_pool_span_info;
enum { VK_POOL_SPAN_NONE=0,VK_POOL_SPAN_SINGLE=1,
       VK_POOL_SPAN_UNBACKED=2,VK_POOL_SPAN_CROSSING=3 };
int vk_pool_span_query(uint64_t addr,uint64_t size,vk_pool_span_info *out);
// Explicit recording-owner reservation, not an upload/pin/preflight by itself.
// No existing span is replaced. Failure leaves the complete batch unchanged;
// NEED_WHOLE requires separately quiesced owner maintenance and retry.
// A nonempty trigger preserves the full known binding for finite consolidation;
// an empty trigger retains mandatory whole-reference maintenance.
enum { VK_POOL_RESERVE_INVALID=0,VK_POOL_RESERVE_OK=1,VK_POOL_RESERVE_NEED_WHOLE=2,
       VK_POOL_RESERVE_OOM=3,VK_POOL_RESERVE_ALLOCATION_FAILED=4 };
enum { VK_POOL_RESERVE_MAX_REQUIREMENTS=4096 };
typedef struct {
  uint64_t pool_base,pool_identity; // first pool requiring explicit maintenance
  uint64_t added_buffer_bytes;
  uint32_t added_spans;
  VkResult allocation_error;
  vk_pool_requirement trigger; // empty for full-reference maintenance
} vk_pool_reserve_result;
int vk_pool_reserve_ranges(const vk_pool_requirement *requirements,size_t count,
    uint64_t window_bytes,vk_pool_reserve_result *out);
// Startup only. Zero preserves the original whole-pool policy.
void vk_pool_window_bytes(uint64_t bytes);
uint64_t vk_pool_window_fallbacks(void);
#ifdef DM_VULKAN_SHARED_POOLS
void vk_pool_lazy_window_stats(int *,uint64_t *,uint64_t *,uint64_t *,uint64_t *);
#endif
void vk_pool_window_stats(uint64_t *bytes,uint64_t *admissions,uint64_t *added_bytes,
    uint64_t *added_spans,uint64_t *promotions);
typedef int (*vk_pool_operation_collect)(void *context,vk_pool_requirement_sink sink,void *sink_context);
// Collection is logical and may be retried after explicit owner maintenance.
// A nonzero token owns an immutable physical topology until operation_end.
int vk_pool_operation_begin(vk_pool_operation_collect collect,void *context);
// Draw only: complete live consumers can import on demand in the opt-in mode.
int vk_pool_draw_operation_begin(vk_pool_operation_collect collect,void *context);
int vk_pool_operation_begin_ranges(const vk_pool_requirement *ranges,size_t count);
void vk_pool_operation_end(int token);
int vk_pool_operation_active(void);
int vk_pool_maintenance_active(void);

// Recording-owner idle proofs; never synchronize or end an active scope.
int vk_shader_operation_active(void);
int vk_texture_binding_scope_active(void);
// Full-consumption experiment: opaque Pool owners are borrowed only by the
// current recording; an internal commit upgrades active operations first.
int vk_pool_uniform_capture(uint64_t addr,uint64_t size,void **buffer,uint64_t *offset,VkPipelineStageFlags stages,void **owner);
uint64_t vk_pool_uniform_epoch(void);
int vk_pool_uniform_group_add(unsigned stage,void *owner);
int vk_pool_uniform_group_borrow(unsigned stage);
uint64_t vk_texture_uniform_alias_epoch(void);
int vk_texture_uniform_alias_proved(uint64_t address,uint64_t bytes);
void vk_ubo_plan_break(void);
void vk_ubo_plan_changed(unsigned stage,unsigned slot,uint64_t address,uint64_t size);
int vk_pool_address_stage(uint64_t addr,uint64_t size,void **buffer,uint64_t *offset,VkPipelineStageFlags stages);
int vk_pool_address_overwrite(uint64_t addr,uint64_t size,void **buffer,uint64_t *offset,VkPipelineStageFlags stages);
void vk_pool_overwritten(uint64_t addr,uint64_t size);
uint64_t vk_pool_overwrite_skipped_pages(void);
void vk_pool_mirror_reference(int reference);
void vk_pool_mirror_stats(uint64_t *registered,uint64_t *backed,uint64_t *count,uint64_t *materialized,int *reference);
uint64_t vk_pool_initialized_bytes(void);
VkPipelineStageFlags vk_pool_pending_stages(uint64_t addr,uint64_t size);
void vk_buffer_create(vk_buffer *b, uint64_t size, VkBufferUsageFlags usage);
void vk_buffer_destroy(vk_buffer *b);
vk_buffer *vk_temp_buffer(uint64_t size);
VkCommandBuffer vk_command_buffer(VkCommandBufferLevel level);
// Recording-owner command state; never a resource readiness/lifetime proof.
VkResult vk_command_begin(VkCommandBuffer command,const VkCommandBufferBeginInfo *info);
void vk_command_execute(uint32_t count,const VkCommandBuffer *commands);
void vk_compute_bind_pipeline(VkPipeline pipeline);
void vk_compute_bind_descriptors(VkPipelineLayout layout,uint32_t first,uint32_t count,
    const VkDescriptorSet *sets,uint32_t offset_count,const uint32_t *offsets);
void vk_readback_coherent_reference(int reference);
void vk_image_layout_reference(int reference);
void vk_sampled_array_layout_reference(int reference);
int vk_sampled_array_layout_reference_state(void);
void vk_image_layout_stats(uint64_t *transitions,uint64_t *safe,uint64_t *fallback,int *reference);
void vk_depth_layout_stats(uint64_t *attachments,uint64_t *sampled,int *reference);
void vk_compute_layout_stats(uint64_t *samples,int *reference);
void vk_readonly_depth_stats(uint64_t *uses,int *reference);
uint64_t vk_render_readonly_store_omissions(void);
void vk_pool_sync_reference(int reference);
void vk_pool_lookup_reference(int reference);
void vk_upload_scan_reference(int reference);
int vk_upload_scan_reference_mode(void);
uint64_t vk_upload_page_rechecks_skipped(void);
void vk_pool_lookup_stats(uint64_t *hits,uint64_t *ranges,uint64_t *slow,int *reference);
void vk_pool_sync_policy_stats(uint64_t *full,uint64_t *used,uint64_t *protected_bytes,int *reference);
void vk_buffer_transfer_written(void);
void vk_buffer_memory_reference(int reference);
int vk_buffer_memory_reference_state(void);
void vk_buffer_transfer_written_range(uint64_t address,uint64_t size);
void vk_vertex_images_require(void);
int vk_buffer_vertex_pending(uint64_t address,uint64_t size);
void vk_buffer_vertex_require(uint64_t address,uint64_t size);
void vk_buffer_shader_read(uint64_t address,uint64_t size,VkPipelineStageFlags stages);
typedef struct { uint64_t address,size;int writable; } vk_buffer_use;
void vk_compute_buffer_dependencies(const vk_buffer_use *uses,unsigned count,int conservative);
void vk_transfer_buffer_dependencies(const vk_buffer_use *uses,unsigned count);
void vk_buffer_copyback_dependencies(uint64_t address,uint64_t size);
void vk_buffer_shader_written(uint64_t address,uint64_t size,VkPipelineStageFlags stages);
#ifdef DM_VULKAN_SHARED_POOLS
// Caller already published the exact dirty event through the counted scope
// API. Preserve ordinary GPU writer/authority dependencies without marking twice.
void vk_buffer_shader_written_pretracked(uint64_t address,uint64_t size,VkPipelineStageFlags stages);
#endif
void vk_buffer_producers_visible(VkPipelineStageFlags producers,VkPipelineStageFlags consumers);
int vk_graphics_resource_dependencies(int writable);
void vk_compute_barrier(void);
void vk_transfer_barrier(void);
void vk_compute_produced(void);
void vk_graphics_stage_reference(int reference);
void vk_submission_barrier_reference(int reference);
int vk_submission_barrier_mode(void);
void vk_graphics_stage_stats(uint64_t *relaxed,uint64_t *fallback,int *reference);
void vk_draw_barrier_at(const char *file,unsigned line,int vertex_images);
#define vk_draw_barrier(vertex_images) vk_draw_barrier_at(__FILE__, __LINE__,vertex_images)
void vk_barrier_at(const char *file, unsigned line);
void vk_render_end_at(const char *file, unsigned line);
#define vk_barrier() vk_barrier_at(__FILE__, __LINE__)
#define vk_render_end() vk_render_end_at(__FILE__, __LINE__)
void vk_render_end_stats(int print);
void vk_render_end_preserve(void);
void vk_stream_submit_reference(int reference);
void vk_stream_begin(void);
void vk_stream_end(void);
void vk_stream_target_boundary(void);
void vk_stream_checkpoint(void);
void vk_stream_compute_checkpoint(void);
int vk_render_active(void);
void vk_render_materialize_pending(void);
int vk_discard_covered_clears_enabled(void);
uint64_t vk_render_covered_clear_omissions(void);
void vk_render_attachment_representation_changed(void);
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
void vk_test_attachment_reuse_stats(uint64_t *hits,uint64_t *retries);
void vk_test_attachment_reuse_callback(void (*callback)(void *),void *context);
#endif
int vk_counter_active(void);
void vk_stream_stats(uint64_t *splits,uint64_t *boundaries,uint64_t *draws,int *reference);
void vk_nvn_barrier(uint32_t flags);
void vk_render_texture_read(const tex_t *texture, VkShaderStageFlagBits stage);
int vk_render_target_clear(const p_set_rt *targets, const p_scissor *scissor,
                           int index, VkImageAspectFlags aspect,
                           const VkClearValue *value);
enum {
  VK_TIME_POOL,
  VK_TIME_DRAW,
  VK_TIME_PRESENT,
  VK_TIME_WAIT,
  VK_TIME_GPU_WAIT,
  VK_TIME_EFFECTS,
  VK_TIME_COUNT
};
uint64_t vk_profile_start(void);
void vk_profile_end(unsigned category, uint64_t started);
void vk_profile_read(uint64_t times[VK_TIME_COUNT]);
unsigned vk_gpu_region_begin(const char *kind, const char *key, unsigned x, unsigned y);
void vk_gpu_region_label(unsigned token, const char *key);
void vk_gpu_region_end(unsigned token);
uint64_t vk_gpu_profile_samples(void);
void vk_ghost_flush(void);
void vk_effect_copy(void *dst, const void *src, size_t size);
int vk_event_signal(uint64_t address,uint32_t mode,uint32_t value);
void vk_buffer_writeback(uint64_t dst, uint64_t size);
void vk_buffer_writeback_stage(uint64_t dst,uint64_t size,VkPipelineStageFlags producers);
// Immutable CPU producer snapshot; publication still waits for the real fence.
void vk_buffer_writeback_known(uint64_t dst,uint64_t size,const void *bytes);
// Only a newly created current-recording private coherent+cached temporary, immutable
// through completion. Returns0 without side effects if that lifetime is unproved.
// Arbitrary caller bytes must use the copying API above.
int vk_buffer_writeback_known_temporary(uint64_t dst,uint64_t size,const vk_buffer *source);
void vk_cpu_copy_reference(int reference);
void vk_cpu_copy_stats(uint64_t *snapshots,uint64_t *published,uint64_t *fallbacks,int *reference);
// 提交边界：将登记范围的最终 GPU 数据截到独占暂存；后续写须重新登记。
// Submission boundary: snapshot final GPU contents into exclusive staging.
// Subsequent writes must be registered again.
void vk_buffer_writebacks_flush(void);
// 仅回放线程调用。读取原 CPU 池；0=无需提交，1=已等相交 GPU 写，-1=无效范围。
// Replay thread only. Read the original CPU pool; 0=no submit, 1=waited for
// overlapping GPU writes, -1=invalid range. Internal waits never advance guest fences.
int vk_pool_read(uint64_t address, size_t size, void *out);
// Optional CPU proofs must not force GPU completion: 1=read, 0=pending, -1=invalid.
int vk_pool_try_read(uint64_t address,size_t size,void *out);
// Required CPU exposure; owner-direct, before any temporary/descriptor/job borrow.
int vk_pool_cpu_prepare(uint64_t address,uint64_t size);
int vk_pool_cpu_unknown(uint64_t address,uint64_t size);
void vk_texture_cpu_preflight(const tex_t *texture);
// Private/defer-off operation preflight: only packaged descriptors read CPU contents.
void vk_texture_packaged_cpu_preflight(int id);
int vk_pool_cpu_source_ready(uint64_t address,uint64_t size);
void vk_cover_wait_reference(int reference);
int vk_cover_read(uint64_t address,size_t size,void *out);
typedef struct {
  uint64_t guest_submits,internal_submits,readback_ranges,readback_bytes;
  uint64_t internal_readback_bytes,cpu_reads,cpu_submit_waits,cpu_pending_waits;
  uint64_t proof_reads,proof_pending,upload_waits,cached_readback_bytes,readback_invalidations;
  int cover_reference,readback_reference;
} vk_buffer_sync_stats;
void vk_buffer_sync_snapshot(vk_buffer_sync_stats *out);

// 录制线程：包含未封口、已封口和在途 CPU 写回；不等待也不读取 GPU 数据。
// Recording thread: includes unflushed, flushed and in-flight CPU writes.
// Neither waits nor reads GPU contents.
int vk_pool_pending_writes(uint64_t address, uint64_t size);
#ifdef DM_VULKAN_NATIVE_RESOURCES
// Owner-only, no waits: full retained Pool/Buffer publication and access proof.
int vk_pool_native_acquire_independent(uint64_t address,uint64_t size);
int vk_pool_native_image_acquire_range(void *token,uint64_t address,uint64_t bytes,
                                     uint64_t *first,uint64_t *length);
#endif
uint64_t vk_recording_serial(void);
uint64_t vk_completed_serial(void);
int vk_async_submissions(void);
// 仅录制所有者调用；只等实际上传页相交的在途访问，不持资源锁等待。
// Recording owner only. Wait for in-flight accesses overlapping actual upload
// page spans, without holding the resource lock during waits.
void vk_pool_sync(void);
uint64_t vk_pool_upload_waits(void);
void vk_texture_shutdown(void);
uint32_t vk_texture_count(void);
uint32_t vk_sampler_count(void);
void vk_sampler_lookup_reference(int reference);
void vk_texture_lookup_scope_begin(void);
void vk_texture_lookup_scope_end(void);
void vk_texture_binding_scope_begin(void);
void vk_texture_binding_scope_end(void);
void vk_texture_binding_reference(int reference);
void vk_texture_binding_stats(uint64_t *views,uint64_t *prepares,uint64_t *misses,int *reference);
void vk_sampler_lookup_stats(uint64_t *hits,uint64_t *misses,int *reference);
void vk_texture_clear(const p_clear_tex *p);
void vk_target_clear_color(const p_set_rt *rt, const p_scissor *sc,
                           const p_clear_color *p);
void vk_target_clear_depth(const p_set_rt *rt, const p_scissor *sc,
                           const p_clear_ds *p);
void vk_texture_copy_b2t(const p_copy_b2t *p, uint64_t row, uint64_t image);
void vk_texture_copy_t2b(const p_copy_b2t *p, uint64_t row, uint64_t image);
void vk_texture_copy_t2t(const p_copy_t2t *p);
VkFormat vk_nvn_format(int format);
void vk_format_properties_init(void);
void vk_format_properties(VkFormat format,VkFormatProperties *properties);

void vk_completion(void (*fn)(void *), void *context);
// 声明完成回调写入的 CPU 范围，供只读同步使用。
// Declare the CPU range written by a completion callback for coherent reads.
void vk_completion_write(void (*fn)(void *), void *context, void *dst, size_t size);
void vk_counter_reset(uint32_t type);

uint64_t vk_internal_commit(void);
void vk_internal_wait(uint64_t serial);
// Keep the existing Perfetto wait scope and annotate its actual source only
// while that scope is enabled. No clock, sampling or normal-path allocation.
void vk_internal_wait_from(uint64_t serial,const char *caller);
#define vk_internal_wait(serial) vk_internal_wait_from((serial),__func__)
void vk_present_ticket_wait(uint64_t serial);
// WSI 的取图/完成依赖留在 GPU；返回内部队列序号，不能当作 guest fence。
// Keep WSI acquire/finish dependencies on the GPU; return an internal serial,
// which must never be interpreted as a guest fence.
uint64_t vk_internal_commit_semaphores(VkSemaphore wait, VkPipelineStageFlags stage,
                                       VkSemaphore signal);
typedef struct {
  VkQueryPool pool;
  uint32_t index;
} vk_sample_query;
vk_sample_query vk_counter_draw_prepare(void);
void vk_counter_draw_begin(vk_sample_query query);
void vk_counter_draw_end(vk_sample_query query);
void vk_counter_pass_end(void);
void vk_counter_span_mode(int enabled);
void vk_counter_query_stats(uint64_t *begins,uint64_t *ends,int *span);
void vk_counter_begin(void);
void vk_query_pool_stats(uint64_t *reuses,uint64_t *creates);
void vk_counter_flush(void);

VkImage vk_texture_native(const tex_t *t);
// Transfer-read source only; callers must not write through this handle.
VkImage vk_texture_present_source(const tex_t *t,VkImageLayout *layout);
void vk_texture_trace(const tex_t *t, const view_t *v, const char *shader);
uint32_t vk_graphics_pipeline_count(void);
void vk_graphics_byte_hash(int legacy);
void vk_graphics_prepare_mode(int mode);
void vk_graphics_prepare_stats(uint64_t *reuses,uint64_t *lookups,uint64_t *checks);

// Experimental bounded clear transaction; cached mode, no per-access atomics.
void vk_mip_clear_enable(int enabled);
int vk_mip_clear_pending(void);
void vk_mip_clear_drain(void);
void vk_mip_clear_replay(const uint8_t *op,unsigned opcode);
void vk_mip_clear_buffer_access(uint64_t address,uint64_t size);
void vk_mip_clear_texture_access(const tex_t *texture,const view_t *view,int attachment);
int vk_mip_clear_try(const p_set_rt *rt,const p_scissor *sc,const p_clear_color *clear);
int vk_texture_mip_clear_admit(const tex_t *texture,const view_t *view,VkImage *image);
int vk_texture_mip_clear_admit_views(const tex_t *texture,const view_t *const *views,
                                     unsigned count,VkImage *image);
void vk_texture_mip_clear_mark(const tex_t *texture,const view_t *view);
int vk_ghost_idle(void);
void vk_mip_clear_stats(uint64_t *admitted,uint64_t *materialized,uint64_t *continued,
                        uint64_t *rejected,int *enabled);

int vk_ghost_replaying(void);

// Startup policy enabled; NOT proof that a particular range qualifies.
int vk_pool_shared_image_raw_observe_mode(void);

#ifdef DM_VULKAN_SHARED_POOLS
int vk_pool_shared_mode(void);
int vk_pool_shared_active(void);
int vk_pool_shared_gpu_gate_mode(void);
int vk_pool_shared_materialized(uint64_t address,uint64_t bytes);
int vk_pool_shared_observe_protect(uint64_t address,uint64_t bytes,uint64_t *stamp);
// Prepare may submit/wait before physical borrows. Begin only checks that
// proof and waits original FS leases/PG_BUSY, with no Vulkan or renderer call.
// Retain scope and storage; end leaves RO and never advances captured stamp.
typedef struct {void *owner,*gate;uint64_t stamp;} vk_shared_cpu_observation;
int vk_legacy_astc_copy_mode(void);
void vk_legacy_astc_copy_startup(int enabled);
int vk_pool_legacy_encoding_guard_add(uint64_t address,uint64_t bytes);
int vk_pool_cpu_observe_mode(void);
int vk_pool_legacy_cpu_observe_pending(uint64_t address,uint64_t bytes);
int vk_pool_texture_cpu_effect_pending(uint64_t address,uint64_t bytes);
int vk_pool_cpu_observe_prepare(uint64_t address,uint64_t bytes);
int vk_pool_cpu_observe_begin(uint64_t address,uint64_t bytes,vk_shared_cpu_observation *token);
void vk_pool_cpu_observe_end(vk_shared_cpu_observation *token);
int vk_pool_shared_cpu_observe_prepare(uint64_t address,uint64_t bytes);
int vk_pool_shared_cpu_observe_begin(uint64_t address,uint64_t bytes,vk_shared_cpu_observation *token);
void vk_pool_shared_cpu_observe_end(vk_shared_cpu_observation *token);
void vk_pool_shared_publish(uint64_t address,uint64_t bytes,int invalidate);
#endif

// Fixed shared Pool counter publication; no CPU writeback for registered destinations.
typedef struct {VkQueryPool pool;uint32_t index;} vk_query_native_source;
// 1=complete live shared range;0=no shared overlap;-1=closing/partial/wrapping.
int vk_pool_shared_destination(uint64_t address,uint64_t size);
int vk_query_native_supported(void);
#ifdef DM_VULKAN_SHARED_POOLS
int vk_query_clock_parameters(float period,uint32_t bits,uint32_t *multiplier,int32_t *shift);
void vk_query_native_publish(uint64_t address,VkQueryPool timestamp,uint32_t slot,
    const vk_query_native_source *prefix,uint32_t count,uint32_t samples);
#ifdef DM_VULKAN_TEST_EMBED
void vk_counter_native_test_observer(void (*observe)(uint64_t,uint32_t,uint64_t,uint64_t));
int vk_query_native_test_dispatch(vk_buffer *source,vk_buffer *destination,uint64_t offset,
    float period,uint32_t bits,uint32_t count,uint32_t samples);
#endif
#endif

#ifdef DM_VULKAN_SHARED_POOLS
int vk_pool_shared_image_source(uint64_t address,uint64_t bytes,int prepare);
void vk_texture_shared_source_written(uint64_t address,uint64_t bytes);
void vk_texture_shared_linear_access(uint64_t address,uint64_t bytes);
void vk_texture_copy_encoding_access(uint64_t address,uint64_t bytes);
int vk_texture_shared_linear_conflict(uint64_t address,uint64_t bytes);
void vk_texture_shared_pool_close(uint64_t address,uint64_t bytes);
#endif

#ifdef DM_VULKAN_NATIVE_RESOURCES
void *vk_pool_native_image_bind(uint64_t,uint64_t,VkImage,const VkMemoryRequirements *,VkDeviceMemory *);
void vk_pool_native_image_writeback(void *,uint64_t,uint64_t,VkPipelineStageFlags);
void vk_pool_native_image_operation_retain(void *);
void vk_pool_native_image_use(void *,uint64_t,uint64_t,VkPipelineStageFlags);
void vk_pool_native_image_release(void *);
void vk_pool_native_image_collect(void);
int vk_native_resources_active(void);
void vk_native_allocation_stats(uint64_t *,uint64_t *,uint64_t *);
int vk_texture_native_pool_collect(const tex_t *,VkPipelineStageFlags,int,vk_pool_requirement_sink,void *);
int vk_pool_native_image_write_ready(void *);
int vk_pool_native_image_queue_resident(void *);
void vk_native_images_release(void);
uint64_t vk_buffer_native_image_read(uint64_t,uint64_t,VkPipelineStageFlags,uint64_t);
#endif

// Same-pass ordinary-FS image subset reuse; no shader/layout ABI change.
typedef struct { uint64_t object,offset,range; } vk_split_descriptor_value;
void vk_image_split_draw_begin(void);
void vk_image_split_draw_end(void);
void vk_image_split_admit(const void *shader,int eligible);
void vk_image_split_break(void);
void vk_image_split_native_writer_changed(void);
#ifdef DM_VULKAN_NATIVE_RESOURCES
uint64_t vk_buffer_native_writer_generation(void);
void *vk_pool_native_image_operation_owner(void *token);
#else
static inline uint64_t vk_buffer_native_writer_generation(void) {return 0;}
#endif
void vk_image_split_handles(uint32_t op,unsigned stage,unsigned first,unsigned count,const uint64_t *values);
int vk_image_split_prepare(const void *shader,uint64_t skip[2]);
// Optional caller-owned complete stage payload. It must live until the group
// is invalidated; any replacement fill must enter this API before overwriting
// it. Nested operations must use independent storage. NULL keeps the old owner.
vk_split_descriptor_value *vk_image_split_values(const void *shader,unsigned count,uint64_t skip[2],
                                               vk_split_descriptor_value *prepared_values);
int vk_image_split_retry(uint64_t retry[2]);
void vk_image_split_capture_begin(unsigned binding,unsigned slot,int sampler_slot);
void vk_image_split_capture_end(void);
void vk_image_split_stats(uint64_t out[6]);
int vk_image_split_enabled(void);
