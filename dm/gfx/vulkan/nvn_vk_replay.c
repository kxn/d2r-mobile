#include "nvn_vk_pixel_local_runtime.h"
// 直接回放原 hle_nvn.c 写出的命令；未移植的实际操作明确失败，不返回伪成功。
// Replay commands written by the original hle_nvn.c directly; unported
// operations fail explicitly rather than report success.
#include "../nvn/nvn_pace.h"
#include "nvn_vk_state.h"
#include "nvn_vk_window.h"
#include "nvn_vk_freeze.h"
#include "nvn_vk_extrap.h"
#include "nvn_vk_plan.h"
#include "nvn_vk_system_trace.h"
double dm_pace_hz(double screen, double floor) {
  return nvn_pace_hz(screen, floor);
}
int dm_nvn_ruler_truth(void) {
  if (V.options.ruler)
    die("Vulkan visual ruler comparison is not implemented");
  return 0;
}
static uint64_t row_stride;
static unsigned nesting;
// Validated command sizes are multiples of eight: an argument word is
// entirely present or zero padded. Decode only in commands that need words;
// ordinary state/draw operations already consume their typed payloads.
static inline uint64_t replay_word(const uint8_t *a,uint32_t size,unsigned index) {
  uint64_t value=0;
  if(size>=sizeof(op_hdr)+(index+1)*sizeof value)
    memcpy(&value,a+index*sizeof value,sizeof value);
  return value;
}
static const char *op_name[] = {
    "END",          "JUMP",           "BEGIN",         "CALL",
    "SIGNAL_EVENT", "REPORT_COUNTER", "COPY_B2B",      "UPDATE_UB",
    "SET_RT",       "CLEAR_COLOR",    "CLEAR_DS",      "VIEWPORTS",
    "SCISSOR",      "DEPTH_RANGE",    "BIND_PROGRAM",  "BIND_STATE",
    "BIND_VATTR",   "BIND_VSTREAM",   "BIND_VBUF",     "BIND_UBO",
    "BIND_SSBO",    "BIND_TEX",       "BIND_SAMP",     "STENCIL",
    "POLY_OFFSET",  "DRAW",           "DRAW_IDX",      "DISPATCH",
    "BARRIER",      "SET_TEX_POOL",   "SET_SAMP_POOL", "COPY_B2T",
    "COPY_STRIDE",  "BLEND_COLOR",    "COPY_T2B",      "BIND_IMG",
    "COPY_T2T",     "RESET_COUNTER",  "CLEAR_TEX"};
void mtl_exec(const uint8_t *p) {
  if (++nesting > 32)
    die("Vulkan command call depth exceeded");
  for (unsigned guard = 0; guard < 1000000; guard++) {
    op_hdr h;
    memcpy(&h, p, sizeof h);
    if (h.size < 8 || (h.size & 7) || h.op >= OP_COUNT_)
      die("Vulkan invalid command op=%u size=%u", h.op, h.size);
    const uint8_t *a = p + sizeof h;
    vk_pass_plan_op(p);
    vk_mip_clear_replay(p,h.op);
    vk_pixel_local_replay(p);
    if (vk_freeze_op(h.op,a)) {
      p+=h.size;
      continue;
    }
    switch (h.op) {
    case OP_END:
      nesting--;
      return;
    case OP_BEGIN:
      break;
    case OP_JUMP:
      p = (const void *)(uintptr_t)replay_word(a,h.size,0);
      continue;
    case OP_CALL:
      mtl_exec((const void *)(uintptr_t)replay_word(a,h.size,0));
      break;
    case OP_SIGNAL_EVENT: {
      uint32_t mode, value;
      memcpy(&mode, a + 8, 4);
      memcpy(&value, a + 12, 4);
      mtl_sub_event((void *)(uintptr_t)replay_word(a,h.size,0), mode, value);
      break;
    }
    case OP_RESET_COUNTER:
      vk_counter_reset(replay_word(a,h.size,0));
      break;
    case OP_REPORT_COUNTER:
      mtl_sub_counter(replay_word(a,h.size,1), replay_word(a,h.size,0));
      break;
    case OP_COPY_B2B: {
      uint64_t source=replay_word(a,h.size,0),destination=replay_word(a,h.size,1),
               bytes=replay_word(a,h.size,2);
      mtl_sub_copy(source,destination,bytes);
      break;
    }
    case OP_UPDATE_UB: {
      uint64_t address=replay_word(a,h.size,0),bytes=replay_word(a,h.size,1);
      if (h.size < 24 || bytes > h.size - 24)
        die("Vulkan invalid uniform update");
      if (!bytes) break;
      vk_pool_requirement need={address,bytes,0,VK_PIPELINE_STAGE_TRANSFER_BIT,1,0};
      int operation=vk_pool_operation_begin_ranges(&need,1);
      vk_buffer *dst;
      uint64_t off;
      if (!vk_pool_address_overwrite(address,bytes,(void **)&dst,&off,VK_PIPELINE_STAGE_TRANSFER_BIT))
        die("Vulkan uniform update outside pool");
      // 已录制的数据经暂存缓冲按字节拷贝，不使用有四字节限制的 vkCmdUpdateBuffer。
      // Recorded data is copied bytewise from staging, not vkCmdUpdateBuffer.
      // The staging source is private and initialized before submit. Only
      // prior accesses to this destination can conflict with the update.
      const vk_buffer_use destination={address,bytes,1};
      vk_transfer_buffer_dependencies(&destination,1);
      // Dependency resolution can end a pass and replay callbacks. Allocate
      // only afterwards, in the recording that emits this copy and owns all
      // its completion effects. No callback follows before borrowing the map.
      vk_buffer *src = vk_temp_buffer(bytes);
      memcpy(src->map, a + 16, bytes);
      VkBufferCopy copy = {.dstOffset = off, .size = bytes};
      vkCmdCopyBuffer(V.cmd, src->handle, dst->handle, 1, &copy);
      vk_buffer_transfer_written_range(address,bytes);
      vk_pool_overwritten(address,bytes);
      if(!vk_buffer_writeback_known_temporary(address,bytes,src))
        vk_buffer_writeback_known(address,bytes,a+16);
      vk_pool_operation_end(operation);
      break;
    }
    case OP_SET_RT:
      if(memcmp(&S.targets,a,sizeof S.targets))vk_image_split_break();
      if(memcmp(&S.targets,a,sizeof S.targets) && !vk_pixel_local_target_boundary(p)) vk_stream_target_boundary();
      S.targets = *(const p_set_rt *)a;
      break;
    case OP_SCISSOR:
      S.scissor = *(const p_scissor *)a;
      S.has_scissor = 1;
      break;
    case OP_CLEAR_COLOR:
      vk_target_clear_color(&S.targets, S.has_scissor ? &S.scissor : NULL,
                            (const void *)a);
      break;
    case OP_CLEAR_DS:
      vk_target_clear_depth(&S.targets, S.has_scissor ? &S.scissor : NULL,
                            (const void *)a);
      break;
    case OP_CLEAR_TEX:
      vk_texture_clear((const void *)a);
      break;
    case OP_COPY_B2T:
      vk_texture_copy_b2t((const void *)a, row_stride, 0);
      break;
    case OP_COPY_T2B:
      vk_texture_copy_t2b((const void *)a, row_stride, 0);
      break;
    case OP_COPY_T2T:
      vk_texture_copy_t2t((const void *)a);
      break;
    case OP_COPY_STRIDE:
      row_stride = ((const p_copy_stride *)a)->stride;
      break;
    case OP_BARRIER:
      vk_nvn_barrier(replay_word(a,h.size,0));
      break;
    case OP_SET_TEX_POOL:
    case OP_SET_SAMP_POOL:
      break;
    case OP_BIND_PROGRAM:
    case OP_BIND_STATE:
    case OP_BIND_VATTR:
    case OP_BIND_VSTREAM:
    case OP_BIND_VBUF:
    case OP_BIND_UBO:
    case OP_BIND_SSBO:
    case OP_BIND_TEX:
    case OP_BIND_SAMP:
    case OP_BIND_IMG:
    case OP_STENCIL:
    case OP_POLY_OFFSET:
    case OP_VIEWPORTS:
    case OP_DEPTH_RANGE:
    case OP_BLEND_COLOR:
      vk_state_op(h.op, a);
      break;
    case OP_DISPATCH:
      vk_dispatch((const void *)a);
      break;
    case OP_DRAW:
      vk_draw((const void *)a, NULL);
      break;
    case OP_DRAW_IDX:
      vk_draw(NULL, (const void *)a);
      break;
    default:
      die("Vulkan replay not yet implemented: %s (%u)", op_name[h.op], h.op);
    }
    vk_pass_plan_done(a);
    if(h.op==OP_DISPATCH) vk_stream_compute_checkpoint();
    else vk_stream_checkpoint();
    p += h.size;
  }
  die("Vulkan command stream guard exceeded");
}
void mtl_set_frame(uint64_t f) {
  vk_runtime_tick();
  if(V.options.stats && f%120==0) {
    vk_pass_index_stats s;vk_pass_index_snapshot(&s);
    LOG("[vulkan store index] builds=%llu indexed=%llu reference=%llu reference_ops=%llu query_events=%llu comparisons=%llu mismatches=%llu fallbacks=%llu",
      (unsigned long long)s.builds,(unsigned long long)s.indexed,(unsigned long long)s.reference,
      (unsigned long long)s.reference_ops,(unsigned long long)s.query_events,(unsigned long long)s.comparisons,
      (unsigned long long)s.mismatches,(unsigned long long)s.fallbacks);
  }
  static uint64_t previous_draws, previous_passes;
  static uint64_t previous_times[VK_TIME_COUNT];
  uint64_t times[VK_TIME_COUNT];
  vk_profile_read(times);
  vk_render_end_stats(V.options.stats && (f < 8 || f % 60 == 0));
  if (V.options.stats && (f < 8 || f % 60 == 0)) {
    LOG("[vulkan frame] frame=%llu draws=%llu passes=%llu cached_pipelines=%u",
        (unsigned long long)V.frame,
        (unsigned long long)(V.draws - previous_draws),
        (unsigned long long)(V.passes - previous_passes),
        vk_graphics_pipeline_count());
    // 宿主墙钟区间可能重叠；等待时长不是 GPU 时间。
    // Host wall-clock intervals can overlap; fence waiting is not GPU timing.
    LOG("[vulkan host ms] pool_sync=%.2f draw_encode=%.2f present=%.2f "
        "fence_wait=%.2f gpu_wait=%.2f completion_effects=%.2f",
        (times[VK_TIME_POOL] - previous_times[VK_TIME_POOL]) / 1e6,
        (times[VK_TIME_DRAW] - previous_times[VK_TIME_DRAW]) / 1e6,
        (times[VK_TIME_PRESENT] - previous_times[VK_TIME_PRESENT]) / 1e6,
        (times[VK_TIME_WAIT] - previous_times[VK_TIME_WAIT]) / 1e6,
        (times[VK_TIME_GPU_WAIT] - previous_times[VK_TIME_GPU_WAIT]) / 1e6,
        (times[VK_TIME_EFFECTS] - previous_times[VK_TIME_EFFECTS]) / 1e6);
    if (f >= 8)
      LOG("[vulkan gpu ms] frame=%llu completed=%.3f",
          (unsigned long long)(f - 8), mtl_gpu_ms(f - 8));
  }
  memcpy(previous_times, times, sizeof times);
  previous_draws = V.draws;
  previous_passes = V.passes;
  V.frame = f;
}
void bf_want_note(const char *key) {
  LOG("[vulkan] shader requested: %s", key);
}
int mtl_has_window(void) { return vk_window_attached(); }
int mtl_display_hz(void) { return (int)(vk_window_refresh() + .5); }
double mtl_display_refresh(void) { return vk_window_refresh(); }
int mtl_link_active(void) { return 0; }
int mtl_link_wait_tick(uint64_t ns) {
  (void)ns;
  return 0;
}
int mtl_warm_wait(int ms) {
  (void)ms;
  return 0;
}
void mtl_present_test_stall(uint64_t ns) {
  if (ns)
    die("Vulkan present-stall diagnostic unavailable");
}
void mtl_present_wait(uint64_t ticket) { vk_present_ticket_wait(ticket); }
void mtl_present_flush(void) { mtl_finish(); }
double mtl_presented_time(uint64_t n) { return vk_window_presented_time(n); }
static uint64_t offscreen_present(const tex_t *t, double period,
                                  const int crop[4]) {
  (void)period;
  const char *path = V.options.frame_dump;
  static int dumped;
  static uint64_t presents;
  uint64_t count = ++presents;
  uint64_t at = V.options.frame_dump_at;
  if (!path)
    die("Vulkan presentation surface not connected; offscreen diagnosis "
        "requires DM_VULKAN_FRAME_DUMP");
  if (!dumped && count >= at) {
    // The onscreen shell also requests this one-shot diagnostic. Waiting
    // outside this branch drains the GPU on every present, even after the
    // dump was written, defeating asynchronous submission/completion.
    int capture_trace=vk_system_trace_begin("Vulkan one-shot frame capture");
    mtl_finish();
    if (t->d.format != NVN_FORMAT_RGBA8 && t->d.format != NVN_FORMAT_RGBA8_SRGB)
      die("Vulkan frame dump format %#x", t->d.format);
    int x = crop ? crop[0] : 0, y = crop ? crop[1] : 0,
        w = crop && crop[2] > 0 ? crop[2] : t->d.width,
        h = crop && crop[3] > 0 ? crop[3] : t->d.height;
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || (uint64_t)x + w > t->d.width ||
        (uint64_t)y + h > t->d.height)
      die("Vulkan frame crop out of bounds");
    size_t size = (size_t)w * h * 4;
    uint8_t *pixels = malloc(size);
    if (!pixels)
      die("Vulkan frame dump allocation");
    NVNcopyRegion region = {x, y, 0, w, h, 1};
    mtl_read_texels(t, 0, 0, &region, pixels, w * 4, (size_t)w * h * 4);
    FILE *f = fopen(path, "wb");
    if (!f)
      die("Vulkan frame dump open %s", path);
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (size_t i = 0; i < size; i += 4)
      if (fwrite(pixels + i, 1, 3, f) != 3)
        die("Vulkan frame dump write");
    if (fclose(f))
      die("Vulkan frame dump close");
    free(pixels);
    dumped = 1;
    vk_system_trace_end(capture_trace);
    LOG("[vulkan] offscreen present %llu captured %dx%d: %s draws=%llu "
        "passes=%llu",
        (unsigned long long)count, w, h, path, (unsigned long long)V.draws,
        (unsigned long long)V.passes);
  }
  return 0;
}
void mtl_ub_present(double ms) { (void)ms; }
int mtl_gpu_span(uint64_t f, uint64_t *a, uint64_t *b) {
  (void)f;
  *a = *b = 0;
  return 0;
}
void mtl_stats(uint64_t *passes, uint64_t *draws) {
  *passes = V.passes;
  *draws = V.draws;
}
void mtl_args_stats(uint64_t *enc, uint64_t *reuse) { *enc = *reuse = 0; }
void mtl_use_stats(uint64_t *call, uint64_t *skip) { *call = *skip = 0; }
uint64_t mtl_ds_omit(void) { return 0; }
uint64_t mtl_drawable_ns_take(void) { return 0; }
void mtl_bw(uint64_t *load, uint64_t *store, uint64_t *full) {
  *load = *store = *full = 0;
}
uint64_t mtl_udc_saved_bytes(void) { return 0; }
uint64_t mtl_pkg_miss(void) { return 0; }
uint64_t mtl_upload_ns_take(void) { return 0; }
uint64_t mtl_upload_gpu_ns_take(void) { return 0; }
void mtl_create_stats(uint64_t *lib, uint64_t *pso) { *lib = *pso = 0; }
void mtl_create_ns(uint64_t *lib, uint64_t *pso) { *lib = *pso = 0; }
uint64_t mtl_upload_bytes(void) { return 0; }
void mtl_alloc_stats(uint64_t *wait, uint64_t *ns, int *n) {
  *wait = *ns = 0;
  *n = 0;
}
uint32_t mtl_present_qos(void) { return 0; }
uint64_t (*g_ub_sub_cpu_ns)(void), (*g_ub_sub_cyc)(void);
void mtl_mem_stat(unsigned long long f, unsigned long long resident,
                  int heap_ok, unsigned long long total,
                  unsigned long long free_bytes, unsigned long long max) {
  LOG("[vulkan] frame=%llu resident=%llu MiB heap=%llu/%llu largest=%llu "
      "valid=%d",
      f, resident, free_bytes, total, max, heap_ok);
}
void mtl_prog_release(prog_t *p) { (void)p; }
void mtl_present_discard(int on) { vk_window_discard(on); }

uint64_t mtl_present(const tex_t *t, double period, const int crop[4]) {
  uint64_t sequence = vk_window_begin_present();
  if (vk_window_discarding()) {
    vk_window_drop_present(sequence);
    return 0;
  }
  void vk_capture_present(const tex_t *, const int[4]);
  vk_capture_present(t, crop);
  if (!vk_window_attached())
    return offscreen_present(t, period, crop);
  if (V.options.frame_dump)
    offscreen_present(t, period, crop);
  uint64_t started = vk_profile_start();
  uint64_t result = vk_window_present(t, crop, sequence);
  vk_profile_end(VK_TIME_PRESENT, started);
  return result;
}
