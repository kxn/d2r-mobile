// 原 nvn_mtl.m rt_* 状态机；所有角色、着色器键与模板位来自原实现。
// Original rt_* state machine; pass roles, shader keys and stencil bits are upstream's.
#include "nvn_vk_extrap.h"
#include "nvn_vk_shader.h"
#include <math.h>
#include <time.h>
int g_dm_rt_ab=0;
static struct {
  int env,forced,phase,state,was_on,disabled,force,filter,skipall,ui_seen,main_seen;
  int copied,ex_done,mainp_seen,shadow,have_prev,have_ldr,oit_seen,tm_seen,fx_state,comp_seen;
  int camera_checked,override,sdx,sdy,nsh,cur_sh;
  uint64_t hdr_serial,depth_serial,ldr_serial;
  vk_rt_image hdr,depth,ldr;
  vk_rt_image *phdr,*pdepth,*pl468,*pl469;
  struct { uint64_t key; int skin,other; } shadows[16];
  vk_runtime_stats stats;
} R={.env=-1,.phase=2};
static pthread_mutex_t stats_mutex=PTHREAD_MUTEX_INITIALIZER;
static vk_runtime_stats published;
static void publish(void) {
  R.stats.enabled=R.was_on;R.stats.disabled=R.disabled;R.stats.state=R.state;
  pthread_mutex_lock(&stats_mutex);published=R.stats;pthread_mutex_unlock(&stats_mutex);
}
void vk_runtime_snapshot(vk_runtime_stats *out) {
  pthread_mutex_lock(&stats_mutex);*out=published;pthread_mutex_unlock(&stats_mutex);
}
void vk_runtime_force_full(int frames) { R.force=frames; }
void vk_runtime_tick(void) {
  int toggle=V.options.runtime_toggle;
  if (toggle>0) g_dm_rt_ab=(int)((time(NULL)/toggle)&1);
}
void vk_runtime_apply_stencil(VkPipelineDepthStencilStateCreateInfo *state) {
  // 原外推的模板位策略留在可选模块；通用绘制层只消费最终 Vulkan 状态。
  // Keep upstream extrapolation's stencil policy out of the generic draw executor.
  int mode=R.override;if (!mode) return;
  state->stencilTestEnable=1;
  state->front=state->back=(VkStencilOpState){VK_STENCIL_OP_KEEP,
    mode==3?VK_STENCIL_OP_REPLACE:VK_STENCIL_OP_KEEP,VK_STENCIL_OP_KEEP,
    mode==1?VK_COMPARE_OP_NOT_EQUAL:VK_COMPARE_OP_ALWAYS,
    mode==1?0xd0:0,mode==1?0:0x40,mode==1?0:0x40};
}
static int wanted(int frozen) {
  if (R.env<0) {
    R.env=V.options.runtime;R.forced=V.options.runtime_forced;
  }
  if (R.forced) g_dm_rt_ab=R.env;
  if (g_dm_rt_ab!=1||R.disabled) return 0;
  int mtl_fps_target(void);
  int want=g_dm_rt_ab==1&&!R.disabled&&frozen<=0&&vk_tbdr_enabled()&&
      !dm_nvn_ruler_truth()&&mtl_fps_target()==60;
  // Vulkan 当前每趟都写回；保留原两帧交接边界，不依赖尚未移植的不写回账本。
  // Vulkan stores every pass; retain upstream's handoff delay without a no-store ledger.
  static int effective=-1;static uint64_t pending;
  if (effective<0) effective=want;
  if (want==effective) { pending=0;return effective; }
  if (!pending) { pending=V.frame+1;return effective; }
  if (V.frame>pending) { effective=want;pending=0; }
  return effective;
}
int vk_runtime_preserves_ops(void) {
  // 可能过滤后续清除时，不把原命令流的覆盖证明交给通用写回规划。
  // Filtered future clears cannot prove overwrites in the generic store planner.
  return g_dm_rt_ab!=1 && !R.was_on && !R.filter && !R.skipall &&
      V.options.runtime!=1 && V.options.runtime_toggle<=0;
}
static int skin(void) {
  if (!S.program) return 0;
  vk_shader *shaders[6];vk_program_shaders(S.program,shaders);
  if (!shaders[0]) return 0;
  static const char *keys[]={"0fa4eb7559f1637a","1d97d33ab66abcba","3deaf68fd65b5298","4dffa355f2fbcdaf","54c93fa22abff9ee",
      "64dd7d022f3fae39","67e2ebc28c830f44","add86ee8e20f89b9","b3ae9be5cc670f35","fbf6dc1873a449e9"};
  for (unsigned i=0;i<sizeof keys/sizeof *keys;i++) if (!strcmp(shaders[0]->key,keys[i])) return 1;
  return 0;
}
static void copy_previous(void) {
  if (R.copied||!R.main_seen||!R.hdr.image||!R.depth.image) return;
  R.copied=1;
  if (!vk_extrap_setup(R.hdr.format,R.depth.format)) {
    R.disabled=1;LOG("[vulkan] original runtime extrapolation unavailable; full rendering for this run");publish();return;
  }
  R.phdr=vk_rt_copy(&R.hdr,R.phdr);R.pdepth=vk_rt_copy(&R.depth,R.pdepth);
  R.have_prev=R.phdr&&R.pdepth;
}
static void frame_begin(void) {
  R.filter=R.ui_seen=R.main_seen=R.copied=R.ex_done=R.mainp_seen=0;R.cur_sh=-1;
  R.oit_seen=R.tm_seen=R.fx_state=R.comp_seen=R.skipall=R.camera_checked=R.override=0;
  if (!R.state) {
    R.have_prev=R.have_ldr=0;
    for (int i=0;i<R.nsh;i++) R.shadows[i].skin=R.shadows[i].other=0;
  }
}
static void set_targets(const p_set_rt *rt,int window) {
  R.cur_sh=-1;R.shadow=0;
  if (window) {
    if (!R.state) {
      copy_previous();
      if (R.fx_state==2&&R.ldr.image) { R.pl469=vk_rt_copy(&R.ldr,R.pl469);R.have_ldr=R.pl469!=NULL;R.fx_state=3; }
    }
    R.filter=R.skipall=R.override=0;publish();return;
  }
  vk_rt_image c0={0},depth={0};
  if (rt->n>0&&rt->color[0]) vk_rt_texture(rt->color[0],&c0);
  if (rt->depth) vk_rt_texture(rt->depth,&depth);
  int hdr_here=0;for (int i=0;i<rt->n&&i<8;i++)
    if (rt->color[i]&&R.hdr_serial&&rt->color[i]->serial==R.hdr_serial) hdr_here=1;
  if (!R.state) {
    if (!R.main_seen&&rt->n==1&&c0.image&&c0.format==VK_FORMAT_B10G11R11_UFLOAT_PACK32&&
        (depth.format==VK_FORMAT_D32_SFLOAT_S8_UINT||depth.format==VK_FORMAT_D24_UNORM_S8_UINT)&&
        depth.extent.width>=500&&depth.extent.width!=depth.extent.height) {
      if (R.hdr_serial!=rt->color[0]->serial||R.depth_serial!=rt->depth->serial)
        LOG("[vulkan] original runtime main HDR=%llu depth=%llu %ux%u depth_format=%u",
            (unsigned long long)rt->color[0]->serial,(unsigned long long)rt->depth->serial,c0.extent.width,c0.extent.height,depth.format);
      R.main_seen=1;R.hdr_serial=rt->color[0]->serial;R.depth_serial=rt->depth->serial;R.hdr=c0;R.depth=depth;
    } else if (R.main_seen&&hdr_here&&!R.copied) copy_previous();
    else if (R.main_seen&&hdr_here&&rt->n==1&&rt->depth&&rt->depth->serial==R.depth_serial) R.fx_state=1;
    else if (R.fx_state==1) {
      if (rt->n>=1&&rt->color[0]&&c0.image) {
        R.ldr_serial=rt->color[0]->serial;R.ldr=c0;R.pl468=vk_rt_copy(&R.hdr,R.pl468);R.fx_state=2;
      } else R.fx_state=0;
    } else if (R.fx_state==2&&R.ldr.image) {
      R.pl469=vk_rt_copy(&R.ldr,R.pl469);R.have_ldr=R.pl468&&R.pl469;R.fx_state=3;
    }
  }
  if (!rt->n&&rt->depth&&rt->depth->d.width==rt->depth->d.height) {
    unsigned level=rt->has_dview&&rt->dview.set_levels?rt->dview.base_level:0;
    unsigned layer=rt->has_dview&&rt->dview.set_layers?rt->dview.min_layer:0;
    uint64_t key=rt->depth->serial*64+(uint64_t)level*8+layer;
    int i;for (i=0;i<R.nsh&&R.shadows[i].key!=key;i++) {}
    if (i==R.nsh&&i<16) { R.shadows[i].key=key;R.shadows[i].skin=R.shadows[i].other=0;R.nsh++; }
    R.cur_sh=i<16?i:-1;R.shadow=1;
  }
  int ds_here=rt->depth&&R.depth_serial&&rt->depth->serial==R.depth_serial;
  if (ds_here&&!rt->n&&!R.ui_seen) { R.ui_seen=1;R.filter=0;return; }
  int main_pass=hdr_here&&ds_here&&rt->n==1&&!R.mainp_seen;
  if (main_pass) R.mainp_seen=1;
  R.filter=R.shadow||(ds_here&&!rt->n)||main_pass;
  if (R.state==2&&R.mainp_seen&&!main_pass) {
    int c0ldr=rt->n>=1&&rt->color[0]&&R.ldr_serial&&rt->color[0]->serial==R.ldr_serial,override=0;
    R.skipall=0;
    if (R.fx_state==2) {}
    else if (R.fx_state==1) R.fx_state=2;
    else if (!R.oit_seen&&ds_here&&rt->n>=2) { R.oit_seen=1;override=3; }
    else if (!R.comp_seen) {
      if (ds_here&&c0ldr) { R.comp_seen=1;override=1;vk_rt_shift(R.pl469,&R.ldr,R.sdx,R.sdy); }
      else R.skipall=1;
    } else if (ds_here&&c0ldr&&R.tm_seen) { R.fx_state=1;override=1; }
    else if (ds_here) {
      if (hdr_here&&rt->n==1) { R.tm_seen=1;vk_rt_shift(R.pl468,&R.hdr,R.sdx,R.sdy); }
      override=1;
    } else R.skipall=1;
    R.override=override;
  }
}
static int camera(float current[16],float previous[16]) {
  const p_bind_buf *ubo=&S.ubo[0][3];vk_buffer *buffer;uint64_t offset;
  if (!ubo->addr||ubo->size<528||!mtl_addr(ubo->addr,ubo->size,(void **)&buffer,&offset)) return 0;
  // 沿原 CPU 常量读法，仅当所需范围被 GPU 更新时等完成，不因其他池写入停顿。
  // Preserve original CPU reads, waiting only for GPU writes to the required range.
  float constants[48];int waited=vk_pool_read(ubo->addr+336,sizeof constants,constants);
  if (waited<0) return 0;
  R.stats.camera_reads++;R.stats.camera_waits+=(unsigned)waited;
  memcpy(current,constants,64);memcpy(previous,constants+32,64);return 1;
}
static int operation(uint32_t op) {
  if (R.state==2&&R.override==1&&op==OP_CLEAR_COLOR) return 1;
  if (R.skipall&&(op==OP_DRAW||op==OP_DRAW_IDX||op==OP_CLEAR_COLOR||op==OP_CLEAR_DS||
      op==OP_COPY_T2T||op==OP_CLEAR_TEX||op==OP_DISPATCH)) return 1;
  if (op==OP_DRAW||op==OP_DRAW_IDX) {
    if (R.state==2&&!R.camera_checked) {
      R.camera_checked=1;float cur[16],prev[16];int ok=0,dx,dy;float difference=0;
      if (camera(cur,prev)) {
        for (int r=0;r<3;r++) for (int c=0;c<4;c++) difference=fmaxf(difference,fabsf(cur[r*4+c]-prev[r*4+c]));
        vk_extrap_shift(prev,cur,R.hdr.extent.width,R.hdr.extent.height,&dx,&dy);
        ok=difference<1e-4f&&abs(dx)<=64&&abs(dy)<=64;
      }
      if (!ok) { R.state=0;R.stats.jumps++;frame_begin();R.camera_checked=1;return 0; }
    }
    if (!R.state) {
      if (R.cur_sh>=0) { if (skin()) R.shadows[R.cur_sh].skin++;else R.shadows[R.cur_sh].other++; }
      return 0;
    }
    if (!R.filter) return 0;
    if (!skin()) return 1;
    if (!R.ex_done&&!R.shadow) {
      R.ex_done=1;float cur[16],prev[16];
      if (camera(cur,prev)) {
        vk_extrap_shift(prev,cur,R.hdr.extent.width,R.hdr.extent.height,&R.sdx,&R.sdy);
        if (vk_extrap_encode(&R.hdr,&R.depth,R.phdr,R.pdepth,prev,cur,0x10,2)) R.stats.encoded++;
        else R.stats.missed++;
      } else R.stats.missed++;
    }
    return 0;
  }
  if (R.state==2&&R.filter&&(op==OP_CLEAR_COLOR||op==OP_CLEAR_DS)) {
    if (R.shadow&&R.cur_sh>=0&&!R.shadows[R.cur_sh].other) return 0;
    return 1;
  }
  return 0;
}
int vk_runtime_op(uint32_t op,const void *payload,int window,int frozen) {
  if (!wanted(frozen)) {
    if (R.was_on) {
      R.state=0;R.phase=2;R.have_prev=R.have_ldr=R.override=R.skipall=R.was_on=0;
      LOG("[vulkan] original runtime extrapolation stopped");publish();
    }
    return -1;
  }
  if (op==OP_SET_RT) {
    if (R.phase==2&&!window) {
      int was=R.state;R.phase=0;R.was_on=1;
      int force=R.force>0;if (force) R.force--;
      R.state=!was&&R.have_prev&&R.have_ldr&&!force?2:0;
      if (R.state==2) R.stats.extrapolated++;else R.stats.full++;
      frame_begin();
    }
    if (window) R.phase=2;
    set_targets(payload,window);
    return R.skipall?1:0;
  }
  return R.phase!=2?operation(op):0;
}
