// 对应原 nvn_mtl.m frz_op 的世界冻结分支；运行时外推另行对应。
// Original frz_op frozen-world branch; runtime extrapolation is separate.
#include "nvn_vk_internal.h"
#include "nvn_vk_freeze.h"
#include "nvn_vk_extrap.h"
#include <stdatomic.h>

static _Atomic int freeze_request = -1, force_request;
static int freeze_on = -1, phase = 2, drop_stencil_draw, seen_depth;
static int full_request; // 原宿主在比例/目标重建前请求完整帧，刷新冻结世界缓存。
                         // Original scale/target-change hints refresh the frozen world cache.
static vk_freeze_stats current, published;
static pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t window_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint64_t *windows;
static size_t window_count, window_capacity;

void mtl_world_frozen(int on) { atomic_store(&freeze_request, on != 0); }
void mtl_rt_force_full(int n) { atomic_store(&force_request, n); }
uint64_t mtl_host_latch(void) {
  return (uint64_t)(uint32_t)(atomic_load(&freeze_request)+1) |
         (uint64_t)(uint32_t)atomic_exchange(&force_request,0)<<32;
}
void mtl_host_apply(uint64_t flags) {
  int frozen = (int)(uint32_t)flags-1, force = (int)(flags>>32);
  if (frozen>=0) freeze_on=frozen;
  if (force) { full_request=force;vk_runtime_force_full(force); }
}
static void publish(void) {
  pthread_mutex_lock(&stats_mutex);
  published=current;
  pthread_mutex_unlock(&stats_mutex);
}
void vk_freeze_snapshot(vk_freeze_stats *out) {
  pthread_mutex_lock(&stats_mutex);
  *out=published;
  pthread_mutex_unlock(&stats_mutex);
}
void mtl_xv_window_tex(uint64_t serial) {
  if (!serial) return;
  pthread_mutex_lock(&window_mutex);
  for (size_t i=0;i<window_count;i++) if (windows[i]==serial) {
    pthread_mutex_unlock(&window_mutex); return;
  }
  if (window_count==window_capacity) {
    size_t capacity=window_capacity?window_capacity*2:16;
    uint64_t *next=realloc(windows,capacity*sizeof *next);
    if (!next) die("Vulkan window identity allocation");
    windows=next; window_capacity=capacity;
  }
  windows[window_count++]=serial;
  pthread_mutex_unlock(&window_mutex);
}
static int window_target(const p_set_rt *targets) {
  int found=0;
  pthread_mutex_lock(&window_mutex);
  for (int i=0;i<targets->n && i<8 && !found;i++) if (targets->color[i])
    for (size_t w=0;w<window_count;w++) if (windows[w]==targets->color[i]->serial) {
      found=1; break;
    }
  pthread_mutex_unlock(&window_mutex);
  return found;
}
static int wanted(void) {
  if (freeze_on<0) {
    freeze_on=V.options.freeze;
  }
  return freeze_on>0 && vk_tbdr_enabled() && dm_nvn_ruler_truth()<=0;
}
int vk_replay_preserves_ops(void) {
  return !wanted() && !current.state && !full_request && vk_runtime_preserves_ops();
}
int vk_freeze_op(uint32_t op, const void *payload) {
  int window=op==OP_SET_RT?window_target(payload):0;
  int runtime=vk_runtime_op(op,payload,window,freeze_on);
  if (runtime>=0) {
    if (current.state) { current.state=0;phase=2;publish(); }
    return runtime;
  }
  if (!freeze_on && !current.state && !full_request) return 0;
  if (op==OP_SET_RT) {
    const p_set_rt *targets=payload;
    if (phase==2 && !window) {
      int was=current.state;
      phase=0; seen_depth=0;
      // 按帧消耗，不能在中途提交时切换；重画帧仍须去掉界面模板，
      // 否则面板以后缩小时会露出未画过的世界区域。
      // Consume per frame, never on a mid-frame submit. Refreshed full frames
      // still omit the UI stencil so a smaller panel reveals a complete world.
      int refresh=full_request>0;
      if (refresh) full_request--;
      if (!wanted()) current.state=0;
      else if (!was || refresh) { current.state=1; current.full_frames++; }
      else { current.state=2; current.frozen_frames++; }
      if (current.state!=was) {
        LOG("[vulkan] original world freeze state=%d full=%llu frozen=%llu skipped_targets=%llu",
            current.state,(unsigned long long)current.full_frames,
            (unsigned long long)current.frozen_frames,(unsigned long long)current.skipped_targets);
        publish();
      }
    }
    drop_stencil_draw=0;
    if (window) { phase=2; publish(); return 0; }
    if (!current.state || phase==2) return 0;
    if (phase==0 && !targets->n && targets->depth && !seen_depth) {
      seen_depth=1;
      if (current.state==2) phase=1;
      else {
        const view_t *view=targets->has_dview?&targets->dview:NULL;
        unsigned level=view && view->set_levels?view->base_level:0;
        unsigned width=targets->depth->d.width>>level, height=targets->depth->d.height>>level;
        if (!width) width=1;
        if (!height) height=1;
        drop_stencil_draw=!V.options.freeze_negative && width!=height;
      }
    }
    if (phase==1) { current.skipped_targets++; return 1; }
    return 0;
  }
  int skip=drop_stencil_draw ? op==OP_DRAW || op==OP_DRAW_IDX :
      current.state==2 && phase==1 &&
      (op==OP_CLEAR_COLOR || op==OP_CLEAR_DS || op==OP_DRAW || op==OP_DRAW_IDX ||
       op==OP_COPY_T2T || op==OP_CLEAR_TEX || op==OP_DISPATCH);
  if (skip) current.skipped_operations++;
  return skip;
}
