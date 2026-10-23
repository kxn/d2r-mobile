// 原 NVN 命令的持久状态；阶段编号保持 nvn.h 的布局。
// Persistent state of original NVN commands; stage indices retain the nvn.h
// layout.
#pragma once
#include "nvn_vk_internal.h"
typedef struct {
  const prog_t *program;
  p_set_rt targets;
  p_scissor scissor;
  int has_scissor, has_viewport;
  float viewport[16][6], depth_range[2], blend_color[4];
  blend_st blend[8];
  color_st color;
  chmask_st chmask;
  ms_st ms;
  poly_st poly;
  ds_st ds;
  va_st attr[16];
  vs_st stream[16];
  NVNbufferRange vertex[16];
  p_bind_buf ubo[6][16], ssbo[6][16];
  uint64_t tex[6][128], samp[6][128], img[6][32];
  uint32_t stencil_value[2], stencil_mask[2], stencil_ref[2];
  p_poly_offset offset;
} vk_state;
extern vk_state S;
// Recording-thread-owned versions of the host-generated c0/c2 ABI. Keep
// these outside S: restoring a ghost snapshot must not roll a version back.
extern uint64_t vk_driver_state_versions[6][2] __attribute__((visibility("hidden")));
void vk_driver_state_invalidate(unsigned stage,unsigned slot) __attribute__((visibility("hidden")));
void vk_state_restore(const vk_state *state) __attribute__((visibility("hidden")));
uint64_t vk_pipeline_state_epoch(void);
void vk_pipeline_state_changed(void);
void vk_pipeline_state_tracking(int enabled);
void vk_state_op(uint32_t op, const void *data);
void vk_dispatch(const p_dispatch *p);
void vk_draw(const p_draw *p, const p_draw_idx *idx);
