// NVN 状态对象与绑定直接沿用原回放格式，所有索引先检查边界。
// NVN state objects and bindings reuse the original replay format, checking
// every index first.
#include "nvn_vk_state.h"
vk_state S = {.depth_range = {0, 1}, .chmask = {~0u}};
uint64_t vk_driver_state_versions[6][2] = {{1,1},{1,1},{1,1},{1,1},{1,1},{1,1}};
void vk_driver_state_invalidate(unsigned stage,unsigned slot) {
  if(stage>=6 || (slot!=0 && slot!=2))die("Vulkan driver state identity");
  if(!++vk_driver_state_versions[stage][slot/2])die("Vulkan driver state version overflow");
}
void vk_state_restore(const vk_state *state) {
  S=*state;
  for(unsigned stage=0;stage<6;stage++)for(unsigned slot=0;slot<=2;slot+=2)
    vk_driver_state_invalidate(stage,slot);
}
// 只对实际变化的静态管线状态递增；动态视口、资源绑定和缓冲地址不使其失效。
// Advance only for changed static pipeline state, excluding dynamic viewports,
// resource bindings and buffer addresses. Owned by the translation thread.
static uint64_t pipeline_epoch=1;
static int pipeline_tracking=1;
uint64_t vk_pipeline_state_epoch(void) { return pipeline_epoch; }
void vk_pipeline_state_changed(void) { if(!++pipeline_epoch) die("Vulkan pipeline epoch overflow"); }
void vk_pipeline_state_tracking(int enabled) {
  pipeline_tracking=enabled;vk_pipeline_state_changed();
}
static void pipeline_set(void *dst,const void *src,size_t size) {
  // 对照路径保留原直接复制成本，不让失效检查的开销藏在基线里。
  // Reference mode retains direct copies so invalidation checks are included
  // in the measured net cost, rather than hidden in the baseline.
  if(!pipeline_tracking) { memcpy(dst,src,size);return; }
  if(memcmp(dst,src,size)) { memcpy(dst,src,size);vk_pipeline_state_changed(); }
}
static void bounds(int first, int n, int limit) {
  if (first < 0 || n < 0 || first > limit || n > limit - first)
    die("Vulkan state range: %d+%d/%d", first, n, limit);
}
void vk_state_op(uint32_t op, const void *data) {
  switch (op) {
  case OP_BIND_PROGRAM:
    pipeline_set(&S.program,&((const p_bind_program *)data)->prog,sizeof S.program);
    break;
  case OP_BIND_STATE: {
    const p_bind_state *p = data;
    switch (p->kind) {
    case 0: {
      blend_st b;
      memcpy(&b, p->bytes, sizeof b);
      pipeline_set(&S.blend[b.target],&b,sizeof b);
      break;
    }
    case 1:
      pipeline_set(&S.color, p->bytes, sizeof S.color);
      break;
    case 2:
      pipeline_set(&S.chmask, p->bytes, sizeof S.chmask);
      break;
    case 3:
      pipeline_set(&S.ms, p->bytes, sizeof S.ms);
      break;
    case 4:
      pipeline_set(&S.poly, p->bytes, sizeof S.poly);
      break;
    case 5:
      pipeline_set(&S.ds, p->bytes, sizeof S.ds);
      break;
    default:
      die("Vulkan invalid state kind %d", p->kind);
    }
    break;
  }
  case OP_BIND_VATTR: {
    const p_bind_vattr *p = data;
    bounds(p->first, p->n, 16);
    pipeline_set(S.attr + p->first, p->st, p->n * 4);
    break;
  }
  case OP_BIND_VSTREAM: {
    const p_bind_vstream *p = data;
    bounds(p->first, p->n, 16);
    pipeline_set(S.stream + p->first, p->st, p->n * sizeof *p->st);
    break;
  }
  case OP_BIND_VBUF: {
    const p_bind_vbuf *p = data;
    bounds(p->first, p->n, 16);
    memcpy(S.vertex + p->first, p->r, p->n * sizeof *p->r);
    break;
  }
  case OP_BIND_UBO:
  case OP_BIND_SSBO: {
    const p_bind_buf *p = data;
    bounds(p->stage, 1, 6);
    bounds(p->index, 1, 16);
    p_bind_buf *dst=&(op == OP_BIND_UBO ? S.ubo : S.ssbo)[p->stage][p->index];
    // Graphics UBOs are separate descriptors; only CS includes them in c0.
    // Size high words and the binding record's index fields are not c0 bytes.
    if((op==OP_BIND_SSBO || p->stage==5) &&
        (dst->addr!=p->addr || (uint32_t)dst->size!=(uint32_t)p->size))
      vk_driver_state_invalidate(p->stage,0);
    *dst=*p;
    break;
  }
  case OP_BIND_TEX:
  case OP_BIND_SAMP:
  case OP_BIND_IMG: {
    const p_bind_handles *p = data;
    bounds(p->stage, 1, 6);
    bounds(p->first, p->n, op == OP_BIND_IMG ? 32 : 128);
    uint64_t *dst=op==OP_BIND_IMG?S.img[p->stage]:
        op==OP_BIND_TEX?S.tex[p->stage]:S.samp[p->stage];
    int limit=op==OP_BIND_IMG?8:128;
    for(int n=0;n<p->n && p->first+n<limit;n++)
      if((uint32_t)dst[p->first+n]!=(uint32_t)p->h[n]) {
        vk_driver_state_invalidate(p->stage,2);break;
      }
    if (op == OP_BIND_IMG)
      memcpy(S.img[p->stage] + p->first, p->h, p->n * 8);
    else
      memcpy((op == OP_BIND_TEX ? S.tex : S.samp)[p->stage] + p->first, p->h,
             p->n * 8);
    break;
  }
  case OP_STENCIL: {
    const p_stencil *p = data;
    for (int f = 0; f < 2; f++)
      if (p->face & (1 << f)) {
        if (p->which == 0)
          pipeline_set(&S.stencil_value[f],&p->value_mask,sizeof p->value_mask);
        else if (p->which == 1)
          pipeline_set(&S.stencil_mask[f],&p->mask,sizeof p->mask);
        else
          pipeline_set(&S.stencil_ref[f],&p->ref,sizeof p->ref);
      }
    break;
  }
  case OP_POLY_OFFSET:
    S.offset = *(const p_poly_offset *)data;
    break;
  case OP_VIEWPORTS: {
    const p_viewports *p = data;
    bounds(p->first, p->n, 16);
    memcpy(S.viewport + p->first, p->v, p->n * sizeof *p->v);
    S.has_viewport = 1;
    break;
  }
  case OP_DEPTH_RANGE: {
    const p_depth_range *p = data;
    S.depth_range[0] = p->n;
    S.depth_range[1] = p->f;
    break;
  }
  case OP_BLEND_COLOR:
    pipeline_set(S.blend_color, ((const p_blend_color *)data)->rgba, 16);
    break;
  default:
    die("Vulkan state opcode %u", op);
  }
}
