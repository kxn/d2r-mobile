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
#include "nvn_vk_memory.h"
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
  VkMemoryPropertyFlags properties;
  int readback;
} vk_buffer;
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
  int runtime,runtime_forced,runtime_toggle,vertex_divisor_fallback;
  int store_reference,store_check,ruler,extrap_no_pipeline,extrap_mask;
  int gpu_profile,trace_frame_set;
  int pool_ownership_reference;
  int pool_mirror_reference;
  unsigned descriptor_sets;
  uint64_t trace_frame,frame_dump_at;
  const char *shader_root,*trace_directory,*frame_dump,*pipeline_cache_directory;
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
  VkPipelineCache pipeline_cache;
  uint32_t queue_family;
  uint32_t timestamp_bits;
  VkPhysicalDeviceProperties props;
  VkPhysicalDeviceMemoryProperties mem;
  VkPhysicalDeviceFeatures features;
  VkBool32 shader_draw_parameters;
  VkBool32 shader_float16;
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
// Recording-owner idle proofs; never synchronize or end an active scope.
int vk_shader_operation_active(void);
int vk_texture_binding_scope_active(void);
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
void vk_buffer_writeback(uint64_t dst, uint64_t size);
void vk_buffer_writeback_stage(uint64_t dst,uint64_t size,VkPipelineStageFlags producers);
// Immutable CPU producer snapshot; publication still waits for the real fence.
void vk_buffer_writeback_known(uint64_t dst,uint64_t size,const void *bytes);
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
