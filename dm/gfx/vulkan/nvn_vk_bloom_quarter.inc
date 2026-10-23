// Original game-specific bloomq_* from nvn_mtl.m:4497-4540,4663-4700.
// Unlike its streaming heuristic, prove the entire chain before omitting A.
// Materialize B0 at the original C position for unknown/cross-submission
// readers: guarded linear blit, otherwise the original C shader. No lazy state.
#include "nvn_vk_bloom_quarter_vert_spv.h"
#include "nvn_vk_bloom_quarter_frag_spv.h"
enum { BQ_SCAN_LIMIT=4096, BQ_DRAW_LIMIT=64 };
static atomic_int bq_requested=-1,bq_active;
static atomic_uint_fast64_t bq_chains,bq_skipped,bq_replaced,bq_redirected,bq_fallbacks;
static atomic_uint_fast64_t bq_clear_before_a,bq_clear_before_b1;
static atomic_uint_fast64_t bq_c_blits,bq_c_query_fallbacks,bq_c_resource_fallbacks;
static char bq_fixture_keys[4][17];
static VkFormat bq_fixture_blit_format;
static const char *const bq_keys[]={"2938a5b71b8e21b2","019ce2f82edbda84","cc264583f2000c81","2e2d6700a2569795"};
static VkDescriptorSetLayout bq_descriptors;
static VkPipelineLayout bq_layout;
static VkSampler bq_sampler;
typedef struct bq_pipeline { struct bq_pipeline *next; VkFormat format; VkPipeline handle; } bq_pipeline;
static bq_pipeline *bq_pipelines;
typedef struct { float threshold,green,blue,width,height; } bq_parameters;
typedef struct {
  uint64_t generation;
  int overlay_sampler;
  samp_par overlay_sampler_parameters;
  const void *replacement,*materialization,*overlay;
  uint64_t source_handle,b1_handle;
  tex_t source,b0,b1;
  bq_parameters parameters;
} bq_chain;
static bq_chain bq;
static int bq_restore_reg=-1;
static uint64_t bq_restore_handle;
static int bq_restore_color;
static color_st bq_original_color;
static void bq_bind_texture(unsigned reg,uint64_t handle) {
  if((uint32_t)S.tex[1][reg]!=(uint32_t)handle)vk_driver_state_invalidate(1,2);
  S.tex[1][reg]=handle;
}
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
static int bq_driver_fixture_reg=-1;
static uint64_t bq_driver_fixture_handle;
void vk_test_driver_bloom_begin(unsigned reg,uint64_t handle) {
  if(reg>=128 || bq_restore_reg>=0 || bq_restore_color || bq_driver_fixture_reg>=0)
    die("Vulkan driver bloom fixture ownership");
  bq_driver_fixture_reg=reg;bq_driver_fixture_handle=handle;
}
#endif
static unsigned bq_failure;
static char bq_failure_shader[17];
void vk_bloom_quarter_enabled(int enabled) { atomic_store(&bq_requested,!!enabled); }
void vk_bloom_quarter_blit_stats(uint64_t *blits,uint64_t *query_fallbacks,uint64_t *resource_fallbacks) {
  *blits=atomic_load(&bq_c_blits);*query_fallbacks=atomic_load(&bq_c_query_fallbacks);
  *resource_fallbacks=atomic_load(&bq_c_resource_fallbacks);
}
void vk_bloom_quarter_clears(uint64_t *before_a,uint64_t *before_b1) {
  *before_a=atomic_load(&bq_clear_before_a);*before_b1=atomic_load(&bq_clear_before_b1);
}
void vk_bloom_quarter_stats(uint64_t *chains,uint64_t *skipped,uint64_t *replaced,uint64_t *redirected,uint64_t *fallbacks,int *enabled) {
  *chains=atomic_load(&bq_chains);*skipped=atomic_load(&bq_skipped);
  *replaced=atomic_load(&bq_replaced);*redirected=atomic_load(&bq_redirected);
  *fallbacks=atomic_load(&bq_fallbacks);*enabled=atomic_load(&bq_active);
}
void vk_bloom_quarter_test_blit_format(VkFormat format) {
  if(format!=VK_FORMAT_UNDEFINED && format!=VK_FORMAT_R8G8B8A8_UNORM)
    die("Vulkan bloom fixture format");
  bq_fixture_blit_format=format;
}
void vk_bloom_quarter_test_facts(const char *keys[4]) {
  if(!keys)bq_fixture_blit_format=VK_FORMAT_UNDEFINED;
  for(unsigned i=0;i<4;i++) {
    if(keys && (!keys[i] || strlen(keys[i])!=16))die("Vulkan bloom fixture key");
    snprintf(bq_fixture_keys[i],17,"%s",keys?keys[i]:"");
  }
}
static int bq_fact(vk_shader *fragment,unsigned index) {
  return fragment && (!strcmp(fragment->key,bq_keys[index]) ||
      (bq_fixture_keys[index][0] && !strcmp(fragment->key,bq_fixture_keys[index])));
}
static int bq_enabled(void) {
  int enabled=atomic_load(&bq_requested);
  if(enabled<0) {
    const char *value=getenv("DM_NVN_BLOOM_Q");
    if(value && strcmp(value,"0") && strcmp(value,"1"))die("DM_NVN_BLOOM_Q expects 0 or 1");
    // Match upstream after real-game visual/synchronization acceptance. Keep
    // explicit zero for comparison; unknown chains retain the original draws.
    enabled=!value || !strcmp(value,"1");atomic_store(&bq_requested,enabled);
  }
  atomic_store(&bq_active,enabled);return enabled && vk_tbdr_enabled();
}
static int bq_alias(const tex_t *a,const tex_t *b) {
  if(a->serial==b->serial)return 1;
  uintptr_t x=(uintptr_t)a->mem,y=(uintptr_t)b->mem;
  return x && y && (x==y || (x<y?y-x<a->L.size:x-y<b->L.size));
}
static int bq_plain(const tex_t *t,const view_t *v) {
  if(!t || !t->serial || t->d.target!=NVN_TEXTURE_TARGET_2D || t->L.layers!=1 || t->d.samples>1 ||
      t->d.width<1 || t->d.height<1)return 0;
  if(v && ((v->set_levels && (v->base_level!=0 || v->nlevels!=1)) ||
      (v->set_layers && (v->min_layer!=0 || v->nlayers!=1)) ||
      (v->set_format && v->format!=t->d.format) ||
      (v->set_target && v->target!=NVN_TEXTURE_TARGET_2D)))return 0;
  VkFormat f=vk_nvn_format(t->d.format);
  if(f!=VK_FORMAT_R16G16B16A16_SFLOAT && f!=VK_FORMAT_B10G11R11_UFLOAT_PACK32 &&
      f!=VK_FORMAT_R8G8B8A8_UNORM && f!=VK_FORMAT_R8G8B8A8_SRGB)return 0;
  return 1;
}
static int bq_color_target(tex_t *t) {
  if(S.targets.n!=1 || !S.targets.color[0] || S.ms.a2c || S.ms.cov2color_en)return 0;
  const view_t *v=S.targets.has_cview[0]?&S.targets.cview[0]:NULL;
  if(!bq_plain(S.targets.color[0],v))return 0;
  *t=*S.targets.color[0];return 1;
}
static int bq_target(tex_t *t) {
  return !S.ds.dt && !S.ds.dw && !S.ds.st && bq_color_target(t);
}
static int bq_texture(uint64_t handle,tex_t *texture) {
  view_t view;int has_view;
  if(!handle || !vk_texture_descriptor_view(handle&0xfffff,texture,&view,&has_view) ||
      !bq_plain(texture,has_view?&view:NULL))return 0;
  // The built-in uses a single selected level, never an implicit mip chain.
  return has_view && view.set_levels?view.nlevels==1:texture->d.levels==1;
}
static int bq_input(vk_shader *sh,uint64_t *handle,tex_t *texture,int *reg) {
  unsigned textures=0;*handle=0;
  for(unsigned i=0;i<sh->nb;i++) {
    const vk_binding *b=&sh->bindings[i];
    if(b->kind==0 || b->kind==1 || b->kind==5)continue;
    if(b->kind!=3 && b->kind!=6)return 0;
    if(b->count!=1 || b->reg<0 || b->reg>=128 || (b->type&255)!=3 || ++textures!=1)return 0;
    *handle=S.tex[1][b->reg];if(reg)*reg=b->reg;
  }
  return textures==1 && bq_texture(*handle,texture);
}
static int bq_sampler_identity(vk_shader *shader,uint64_t replacement_handle,int *id,samp_par *parameters) {
  int found=0;
  for(unsigned k=0;k<shader->nb;k++) {
    const vk_binding *binding=&shader->bindings[k];
    if(binding->kind!=3 && binding->kind!=5)continue;
    if(found++ || binding->count!=1)return 0;
    if(binding->kind==5) {
      if(binding->reg<0 || binding->reg>=128)return 0;
      *id=S.samp[1][binding->reg]>>20;
    } else if(binding->sampler>=0) {
      if(binding->sampler>=128)return 0;
      *id=S.samp[1][binding->sampler]>>20;
    } else *id=replacement_handle>>20;
  }
  return found==1 && vk_sampler_descriptor(*id,parameters);
}
static int bq_sampler_same(const samp_par *a,const samp_par *b) {
  return a->minf==b->minf && a->magf==b->magf && a->ws==b->ws && a->wt==b->wt && a->wr==b->wr &&
      a->cmp_mode==b->cmp_mode && a->cmp_func==b->cmp_func && a->lod_min==b->lod_min &&
      a->lod_max==b->lod_max && a->lod_bias==b->lod_bias && a->aniso==b->aniso && !memcmp(a->border,b->border,sizeof a->border);
}
static int bq_safe_shaders(vk_shader *list[6]) {
  if(!list[0] || !list[1] || list[2] || list[3] || list[4] || list[1]->color_mask!=1 ||
      list[1]->may_discard || list[1]->writes_smask)return 0;
  for(unsigned st=0;st<2;st++)for(unsigned i=0;i<list[st]->nb;i++) {
    const vk_binding *b=&list[st]->bindings[i];
    if(vk_binding_writable(b) || b->kind==2 || b->kind==4 || b->kind==7 || b->kind==8 || b->kind==9)return 0;
    if(st==0 && b->kind!=0 && b->kind!=1)return 0;
  }
  return 1;
}
static int bq_covers(vk_shader *list[6],const p_draw *draw,const p_draw_idx *idx,const tex_t *target,int materialize) {
  if(!bq_safe_shaders(list) || (!materialize && S.color.blend_en) || S.color.logic_op!=NVN_LOGIC_OP_COPY || (S.chmask.mask&15)!=15)return 0;
  // Optional coverage proofs must not drain the GPU even when the generic
  // load-cover reference mode deliberately permits blocking CPU reads.
  if(idx && (idx->count<0 || vk_pool_pending_writes(idx->ibuf,(uint64_t)idx->count*(idx->itype==1?2:4))))return 0;
  int attribute=list[0]->pos_attr;
  if(attribute<0 || attribute>=16 || S.attr[attribute].stream>=16)return 0;
  const NVNbufferRange *vertices=&S.vertex[S.attr[attribute].stream];
  if(vk_pool_pending_writes(vertices->address,vertices->size))return 0;
  float positions[64][4];if(!cover_positions(list[0],list[1],draw,idx,positions))return 0;
  int sc[]={S.scissor.x,S.scissor.y,S.scissor.w,S.scissor.h};
  return nvn_cover(positions,draw?draw->count:idx->count,draw?draw->prim:idx->prim,
      S.poly.cull,S.poly.front==NVN_FRONT_FACE_CCW,S.has_viewport,S.viewport[0],S.has_scissor,sc,target->d.width,target->d.height);
}
// Pure shadow decoder for existing state commands. It changes no persistent S,
// pipeline epoch, buffers, descriptors or command recording. Unknown ops abort.
static int bq_shadow(vk_state *state,uint32_t op,const void *payload) {
  switch(op) {
  case OP_BEGIN:case OP_END:case OP_JUMP:case OP_CALL:case OP_BARRIER:
  case OP_COPY_STRIDE:return 1;
  // Sample reset has no memory/resource output. Preserve its real execution;
  // the planner independently proves which draw contributions are still live.
  case OP_RESET_COUNTER:return *(const uint64_t *)payload==NVN_COUNTER_TYPE_SAMPLES_PASSED;
  // A future pool switch cannot be resolved through the present global pool.
  case OP_SET_TEX_POOL:case OP_SET_SAMP_POOL:return 0;
  case OP_SET_RT:state->targets=*(const p_set_rt *)payload;return 1;
  case OP_SCISSOR:state->scissor=*(const p_scissor *)payload;state->has_scissor=1;return 1;
  case OP_BIND_PROGRAM:state->program=((const p_bind_program *)payload)->prog;return 1;
  case OP_BIND_STATE: {
    const p_bind_state *p=payload;
    switch(p->kind) {
    case 0:{blend_st b;memcpy(&b,p->bytes,sizeof b);if(b.target>=8)return 0;state->blend[b.target]=b;return 1;}
    case 1:memcpy(&state->color,p->bytes,sizeof state->color);return 1;
    case 2:memcpy(&state->chmask,p->bytes,sizeof state->chmask);return 1;
    case 3:memcpy(&state->ms,p->bytes,sizeof state->ms);return 1;
    case 4:memcpy(&state->poly,p->bytes,sizeof state->poly);return 1;
    case 5:memcpy(&state->ds,p->bytes,sizeof state->ds);return 1;
    default:return 0;
    }
  }
  case OP_BIND_VATTR:{const p_bind_vattr *p=payload;if(p->first<0||p->n<0||p->first+p->n>16)return 0;memcpy(state->attr+p->first,p->st,p->n*sizeof *p->st);return 1;}
  case OP_BIND_VSTREAM:{const p_bind_vstream *p=payload;if(p->first<0||p->n<0||p->first+p->n>16)return 0;memcpy(state->stream+p->first,p->st,p->n*sizeof *p->st);return 1;}
  case OP_BIND_VBUF:{const p_bind_vbuf *p=payload;if(p->first<0||p->n<0||p->first+p->n>16)return 0;memcpy(state->vertex+p->first,p->r,p->n*sizeof *p->r);return 1;}
  case OP_BIND_UBO:case OP_BIND_SSBO:{const p_bind_buf *p=payload;if(p->stage<0||p->stage>=6||p->index<0||p->index>=16)return 0;if(state==&S && (op==OP_BIND_SSBO || p->stage==5))vk_driver_state_invalidate(p->stage,0);(op==OP_BIND_UBO?state->ubo:state->ssbo)[p->stage][p->index]=*p;return 1;}
  case OP_BIND_TEX:case OP_BIND_SAMP:case OP_BIND_IMG:{const p_bind_handles *p=payload;int max=op==OP_BIND_IMG?32:128;if(p->stage<0||p->stage>=6||p->first<0||p->n<0||p->first+p->n>max)return 0;if(state==&S && p->n && p->first<(op==OP_BIND_IMG?8:128))vk_driver_state_invalidate(p->stage,2);if(op==OP_BIND_IMG)memcpy(state->img[p->stage]+p->first,p->h,p->n*8);else memcpy((op==OP_BIND_TEX?state->tex:state->samp)[p->stage]+p->first,p->h,p->n*8);return 1;}
  case OP_VIEWPORTS:{const p_viewports *p=payload;if(p->first<0||p->n<0||p->first+p->n>16)return 0;memcpy(state->viewport+p->first,p->v,p->n*sizeof *p->v);state->has_viewport=1;return 1;}
  case OP_DEPTH_RANGE:{const p_depth_range *p=payload;state->depth_range[0]=p->n;state->depth_range[1]=p->f;return 1;}
  case OP_BLEND_COLOR:memcpy(state->blend_color,((const p_blend_color *)payload)->rgba,16);return 1;
  case OP_POLY_OFFSET:state->offset=*(const p_poly_offset *)payload;return 1;
  case OP_STENCIL:{
    const p_stencil *p=payload;
    if(p->which>2)return 0;
    for(unsigned f=0;f<2;f++)if(p->face&(1u<<f)) {
      if(p->which==0)state->stencil_value[f]=p->value_mask;
      else if(p->which==1)state->stencil_mask[f]=p->mask;
      else state->stencil_ref[f]=p->ref;
    }
    return 1;
  }
  default:return 0;
  }
}
static int bq_half(unsigned small,unsigned large) { return small && (small==large/2 || small==(large+1)/2); }
// Keep the large lookahead snapshots in the optional proof's own frame.
static __attribute__((noinline)) int bq_prove(vk_shader *list[6],const p_draw *draw,const p_draw_idx *idx,bq_chain *out) {
  const uint8_t *current;int samples_live;bq_failure=1;bq_failure_shader[0]=0;
  if(!vk_pass_future_op(0,&current,&samples_live) || samples_live || !bq_target(&out->b0) ||
      !bq_covers(list,draw,idx,&out->b0,0) || !bq_input(list[1],&out->source_handle,&out->source,NULL) ||
      bq_alias(&out->source,&out->b0) || !bq_half(out->b0.d.width,out->source.d.width) ||
      !bq_half(out->b0.d.height,out->source.d.height) || list[1]->nc1<8)return 0;
  bq_failure=2;
  VkFormatProperties properties;vk_format_properties(vk_nvn_format(out->source.d.format),&properties);
  if(!(properties.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))return 0;
  memcpy(&out->parameters.green,list[1]->c1,8);
  bq_failure=3;int threshold_found=0;
  for(unsigned i=0;i<list[1]->nb;i++) {
    const vk_binding *binding=&list[1]->bindings[i];
    if(binding->kind!=1 || binding->reg<3 || binding->reg>=19)continue;
    if(threshold_found++)return 0;
    const p_bind_buf *u=&S.ubo[1][binding->reg-3];
    if(u->size<4 || vk_pool_try_read(u->addr,4,&out->parameters.threshold)!=1)return 0;
  }
  if(!threshold_found || !isfinite(out->parameters.threshold) || out->parameters.threshold<0 ||
      !isfinite(out->parameters.green) || !isfinite(out->parameters.blue) ||
      out->parameters.green<0 || out->parameters.blue<0)return 0;
  out->parameters.width=out->source.d.width;out->parameters.height=out->source.d.height;
  out->generation=vk_pass_generation();
  // The proof helpers use the recording owner's S. Keep one original snapshot
  // and advance S as the lookahead cursor, restoring it at the single exit.
  // Helpers do not replay, submit or wait for GPU work; no other owner observes S.
  // This avoids copying the entire state into/out of each intermediate draw.
  vk_state original=S;int phase=0,success=0;unsigned draws=0;
  tex_t levels[BQ_DRAW_LIMIT];levels[0]=out->b0;unsigned level_count=1,up_level=0;
  // No copies, updates, dispatches, query/event side effects or unknown ops are
  // crossed. In particular S and the original A constants cannot be produced
  // or changed on the GPU between A and the replacement B1.
  for(unsigned offset=1;offset<BQ_SCAN_LIMIT;offset++) {
    const uint8_t *op;int live;
    if(!vk_pass_future_op(offset,&op,&live))break;
    op_hdr header;memcpy(&header,op,sizeof header);const void *payload=op+8;
    bq_failure=100*phase+header.op;
    if(header.op==OP_DRAW || header.op==OP_DRAW_IDX) {
      if(++draws>BQ_DRAW_LIMIT || !S.program)break;
      vk_shader *next[6]={0};vk_program_shaders(S.program,next);
      const p_draw *d=header.op==OP_DRAW?payload:NULL;const p_draw_idx *di=d?NULL:payload;
      snprintf(bq_failure_shader,sizeof bq_failure_shader,"%s",next[1]?next[1]->key:"");
      tex_t target,input;uint64_t handle;
      if(!bq_safe_shaders(next) || !(phase==2?bq_color_target(&target):bq_target(&target)) || !bq_input(next[1],&handle,&input,NULL))break;
      if(!phase) {
        if(!bq_fact(next[1],1) || live || input.serial!=out->b0.serial ||
            bq_alias(&target,&out->source) || bq_alias(&target,&out->b0) ||
            !bq_half(target.d.width,out->b0.d.width) || !bq_half(target.d.height,out->b0.d.height) ||
            !bq_covers(next,d,di,&target,0))break;
        out->replacement=payload;out->b1=target;levels[level_count++]=target;phase=1;
      } else if(phase==1 && bq_fact(next[1],1)) {
        // Remaining downsample levels are unmodified; none may access S/B0
        // or overwrite B1. Capture the actual B1 sampling descriptor handle.
        if(up_level || level_count>=BQ_DRAW_LIMIT || input.serial!=levels[level_count-1].serial ||
            bq_alias(&target,&out->source) || !bq_half(target.d.width,input.d.width) ||
            !bq_half(target.d.height,input.d.height))break;
        int alias=0;for(unsigned i=0;i<level_count;i++)alias|=bq_alias(&target,&levels[i]);
        if(alias)break;
        levels[level_count++]=target;
        if(input.serial==out->b1.serial)out->b1_handle=handle;
      } else if(phase==1 && bq_fact(next[1],2)) {
        if(!up_level)up_level=level_count-1;
        if(!up_level || input.serial!=levels[up_level].serial || target.serial!=levels[up_level-1].serial)break;
        up_level--;
        if(target.serial==out->b0.serial) {
          // Metal skips additive C and uses B1 directly. Materialize the same
          // B1-derived result into B0 for unknown later readers: keep C's
          // original upsampling shader/UVs, but never add deleted A contents.
          if(input.serial!=out->b1.serial || !bq_covers(next,d,di,&target,1))break;
          out->materialization=payload;phase=2;
        }
      } else if(phase==2 && bq_fact(next[1],3)) {
        if(input.serial!=out->b0.serial || !out->b1_handle || bq_alias(&target,&out->b1) || bq_alias(&target,&out->b0))break;
        if(!bq_sampler_identity(next[1],out->b1_handle,&out->overlay_sampler,&out->overlay_sampler_parameters))break;
        out->overlay=payload;success=1;break;
      } else break;
    } else if(header.op==OP_CLEAR_COLOR) {
      const p_clear_color *clear=payload;
      // B0 clear before C is harmless; clearing S changes the original A
      // input, while clearing B1 after generation changes what D must read.
      if(S.targets.n!=1 || clear->index!=0 || !S.targets.color[0] ||
          bq_alias(S.targets.color[0],&out->source) ||
          (phase && bq_alias(S.targets.color[0],&out->b1)))break;
    } else if(!bq_shadow(&S,header.op,payload))break;
  }
  vk_state_restore(&original);

  return success;
}
static VkPipeline bq_pipeline_get(VkFormat format,VkRenderPass pass) {
  for(bq_pipeline *p=bq_pipelines;p;p=p->next)if(p->format==format)return p->handle;
  if(!bq_layout) {
    VkDescriptorSetLayoutBinding binding={0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,NULL};
    VkDescriptorSetLayoutCreateInfo descriptor={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&binding};
    VK_CHECK(vkCreateDescriptorSetLayout(V.device,&descriptor,NULL,&bq_descriptors));
    VkPushConstantRange push={VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(bq_parameters)};
    VkPipelineLayoutCreateInfo layout={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&bq_descriptors,.pushConstantRangeCount=1,.pPushConstantRanges=&push};
    VK_CHECK(vkCreatePipelineLayout(V.device,&layout,NULL,&bq_layout));
    VkSamplerCreateInfo sampler={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=VK_FILTER_LINEAR,.minFilter=VK_FILTER_LINEAR,.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    VK_CHECK(vkCreateSampler(V.device,&sampler,NULL,&bq_sampler));
  }
  VkShaderModule modules[2];const uint32_t *codes[]={dm_bloom_quarter_vert_spv,dm_bloom_quarter_frag_spv};
  size_t sizes[]={sizeof dm_bloom_quarter_vert_spv,sizeof dm_bloom_quarter_frag_spv};
  VkPipelineShaderStageCreateInfo stages[2];
  for(unsigned i=0;i<2;i++) {
    VkShaderModuleCreateInfo module={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=sizes[i],.pCode=codes[i]};
    VK_CHECK(vkCreateShaderModule(V.device,&module,NULL,&modules[i]));
    stages[i]=(VkPipelineShaderStageCreateInfo){.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT,.module=modules[i],.pName="main"};
  }
  VkPipelineVertexInputStateCreateInfo vertex={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo assembly={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
  VkPipelineViewportStateCreateInfo viewport={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.scissorCount=1};
  VkPipelineRasterizationStateCreateInfo raster={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE,.lineWidth=1};
  VkPipelineMultisampleStateCreateInfo samples={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
  VkPipelineColorBlendAttachmentState blend={.colorWriteMask=15};
  VkPipelineColorBlendStateCreateInfo colors={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&blend};
  VkDynamicState states[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic={.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,.dynamicStateCount=2,.pDynamicStates=states};
  VkGraphicsPipelineCreateInfo create={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=stages,.pVertexInputState=&vertex,.pInputAssemblyState=&assembly,.pViewportState=&viewport,.pRasterizationState=&raster,.pMultisampleState=&samples,.pColorBlendState=&colors,.pDynamicState=&dynamic,.layout=bq_layout,.renderPass=pass};
  bq_pipeline *p=calloc(1,sizeof *p);if(!p)die("Vulkan bloom pipeline allocation");p->format=format;
  vk_pipeline_cache_begin();
  VkResult compiled=vkCreateGraphicsPipelines(V.device,V.pipeline_cache,1,&create,NULL,&p->handle);
  vk_pipeline_cache_end(compiled==VK_SUCCESS);VK_CHECK(compiled);
  for(unsigned i=0;i<2;i++)vkDestroyShaderModule(V.device,modules[i],NULL);
  p->next=bq_pipelines;bq_pipelines=p;return p->handle;
}
static void bq_capture(void) {
  const tex_t *target=S.targets.color[0];const view_t *view=S.targets.has_cview[0]?&S.targets.cview[0]:NULL;
  // Full coverage was proven before omitting A. This pending B1 clear has no
  // reader before the built-in overwrites every pixel, so never store it only
  // to reopen a second pass for that overwrite.
  if(render_cancel_obsolete_clear(target,view))atomic_fetch_add(&bq_clear_before_b1,1);
  vk_render_end();vk_counter_pass_end();
  vk_texture_prepare_attachment(target,view);
  vk_texture_shader_begin();
  VkImageLayout source_layout;
  VkImageView source_view=vk_texture_view_binding(bq.source_handle&0xfffff,0,VK_SHADER_STAGE_FRAGMENT_BIT,&source_layout);
  vk_attachment attachment=vk_texture_attachment(target,view);
  vk_texture_attachment_record(target,view,0);
  vk_graphics_resource_dependencies(0);vk_texture_graphics_barrier();
  VkAttachmentDescription description={.format=attachment.format,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.initialLayout=attachment.layout,.finalLayout=attachment.layout};
  VkAttachmentReference color={0,attachment.layout};
  VkSubpassDescription subpass={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&color};
  VkRenderPassCreateInfo pass={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=1,.pAttachments=&description,.subpassCount=1,.pSubpasses=&subpass};
  render_job *job=calloc(1,sizeof *job);if(!job)die("Vulkan bloom render allocation");
  job->pass_object=render_pass_acquire(&pass);job->pass=job->pass_object->handle;
  VkFramebufferCreateInfo framebuffer={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=job->pass,.attachmentCount=1,.pAttachments=&attachment.view,.width=attachment.extent.width,.height=attachment.extent.height,.layers=1};
  job->framebuffer_object=framebuffer_acquire(job->pass_object,&framebuffer);job->framebuffer=job->framebuffer_object->handle;
  job->next=graphics_state->jobs;graphics_state->jobs=job;
  VkPipeline pipeline=bq_pipeline_get(attachment.format,job->pass);
  VkDescriptorSet set=vk_descriptor_allocate(bq_descriptors);
  VkDescriptorImageInfo image={bq_sampler,source_view,source_layout};
  VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&image};
  vkUpdateDescriptorSets(V.device,1,&write,0,NULL);
  VkRect2D area={{0,0},{attachment.extent.width,attachment.extent.height}};
  VkRenderPassBeginInfo begin={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=job->pass,.framebuffer=job->framebuffer,.renderArea=area};
  vkCmdBeginRenderPass(V.cmd,&begin,VK_SUBPASS_CONTENTS_INLINE);
  VkViewport viewport={0,attachment.extent.height,attachment.extent.width,-(float)attachment.extent.height,0,1};
  vkCmdSetViewport(V.cmd,0,1,&viewport);vkCmdSetScissor(V.cmd,0,1,&area);
  vkCmdBindPipeline(V.cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
  vkCmdBindDescriptorSets(V.cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,bq_layout,0,1,&set,0,NULL);
  vkCmdPushConstants(V.cmd,bq_layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof bq.parameters,&bq.parameters);
  vkCmdDraw(V.cmd,3,1,0,0);vkCmdEndRenderPass(V.cmd);
  vk_texture_shader_record();V.draws++;V.passes++;atomic_fetch_add(&bq_replaced,1);
}
// Metal deletes C and feeds B1 directly to D. Define the compatibility B0
// as a linear-clamp enlargement of that B1, not the original four-offset blur.
// Execute at C's own position: no cross-submission recipe or borrowed state.
static int bq_materialize_blit(const tex_t *input,const tex_t *target,const void *payload,const vk_shader *fragment) {
  const uint8_t *op;int live=1;
  if(!vk_pass_future_op(0,&op,&live) || live) {
    atomic_fetch_add(&bq_c_query_fallbacks,1);return 0;
  }
  VkFormat format=vk_nvn_format(input->d.format);VkFormatProperties features;
  vk_format_properties(format,&features);
  const VkFormatFeatureFlags required=VK_FORMAT_FEATURE_BLIT_SRC_BIT|VK_FORMAT_FEATURE_BLIT_DST_BIT|
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
  // Lavapipe may lack R11 BLIT_DST. Owned GPU fixtures can exercise this
  // exact path with another genuinely supported format; real game keys can
  // never use the override, and their production format gate stays R11-only.
  int owned_format=bq_fixture_keys[2][0] && strcmp(fragment->key,bq_keys[2]) &&
      !strcmp(fragment->key,bq_fixture_keys[2]) &&
      format==bq_fixture_blit_format && format!=VK_FORMAT_UNDEFINED;
  if(input->d.format!=target->d.format || (format!=VK_FORMAT_B10G11R11_UFLOAT_PACK32 && !owned_format) ||
      input->d.levels!=1 || target->d.levels!=1 || input->L.layers!=1 || target->L.layers!=1 ||
      input->d.samples>1 || target->d.samples>1 || bq_alias(input,target) ||
      (features.optimalTilingFeatures&required)!=required) {
    atomic_fetch_add(&bq_c_resource_fallbacks,1);return 0;
  }
  // Commit the sample-liveness proof before omitting this draw. A live query
  // cannot be reproduced by a transfer blit, even when every pixel is covered.
  if(vk_pass_samples_live(payload))die("Vulkan bloom C query liveness changed");
  // Do not encode a dead clear-only B0 pass just before the full-image blit.
  // Draws, sibling attachments, depth and already-encoded work remain intact.
  render_cancel_obsolete_clear(target,S.targets.has_cview[0]?&S.targets.cview[0]:NULL);
  vk_render_end();vk_counter_pass_end();
  p_copy_t2t copy={.src=input,.dst=target,
      .sr={0,0,0,input->d.width,input->d.height,1},
      .dr={0,0,0,target->d.width,target->d.height,1},.flags=NVN_COPY_FLAGS_LINEAR_FILTER_BIT};
  vk_texture_copy_t2t(&copy);
  atomic_fetch_add(&bq_c_blits,1);return 1;
}
// Only real A/B/C/D handling owns the large logical snapshots and candidate.
// The dispatcher retains all negative checks; no new draw packet is copied.
static VK_COLD_PHASE int bq_before_draw_cold(vk_shader *list[6],const p_draw *draw,
    const p_draw_idx *idx,const void *payload) {
  if(bq.replacement==payload) {
    // Verify only the identities actually used by this draw. Unrelated pool
    // registrations do not invalidate this proof or terminate the game.
    tex_t source,input,target;uint64_t handle;
    if(!bq_texture(bq.source_handle,&source) || source.serial!=bq.source.serial ||
        !bq_input(list[1],&handle,&input,NULL) || input.serial!=bq.b0.serial ||
        !bq_target(&target) || target.serial!=bq.b1.serial)
      die("Vulkan bloom relevant resource identity changed inside proven chain");
    bq_capture();bq.replacement=NULL;return 1;
  }
  if(bq.materialization==payload) {
    tex_t input,target;uint64_t handle;
    if(!bq_fact(list[1],2) || !bq_input(list[1],&handle,&input,NULL) || input.serial!=bq.b1.serial ||
        !bq_target(&target) || target.serial!=bq.b0.serial)
      die("Vulkan bloom materialization resource identity changed inside proven chain");
    bq.materialization=NULL;
    if(bq_materialize_blit(&input,&target,payload,list[1]))return 1;
    bq_original_color=S.color;bq_restore_color=1;S.color.blend_en&=~1u;
    vk_pipeline_state_changed();return 0;
  }
  if(bq.overlay==payload) {
    tex_t texture,b1;uint64_t handle;int reg,sampler;samp_par parameters;
    bq.overlay=NULL;
    // C kept B0 valid: any changed consumer/view/sampler simply uses the
    // original path. This is also safe for another D in a later submission.
    if(!bq_fact(list[1],3) || !bq_input(list[1],&handle,&texture,&reg) || texture.serial!=bq.b0.serial ||
        !bq_texture(bq.b1_handle,&b1) || b1.serial!=bq.b1.serial ||
        !bq_sampler_identity(list[1],bq.b1_handle,&sampler,&parameters) || sampler!=bq.overlay_sampler ||
        !bq_sampler_same(&parameters,&bq.overlay_sampler_parameters)) {
      atomic_fetch_add(&bq_fallbacks,1);return 0;
    }
    bq_restore_reg=reg;bq_restore_handle=handle;
    bq_bind_texture(reg,bq.b1_handle);
    bq.overlay=NULL;atomic_fetch_add(&bq_redirected,1);return 0;
  }
  bq_chain candidate={0};
  if(!bq_prove(list,draw,idx,&candidate)) {
    uint64_t rejected=atomic_fetch_add(&bq_fallbacks,1);
    if(rejected<4)LOG("[vulkan bloom-quarter] conservative fallback point=%u shader=%s",bq_failure,bq_failure_shader);
    return 0;
  }
  bq=candidate;
  // B1 no longer reads B0, and full materialization C initializes it before
  // any later reader. Cancel only a still-pending pure clear of this B0.
  if(render_cancel_obsolete_clear(S.targets.color[0],S.targets.has_cview[0]?&S.targets.cview[0]:NULL))
    atomic_fetch_add(&bq_clear_before_a,1);
  atomic_fetch_add(&bq_chains,1);atomic_fetch_add(&bq_skipped,1);
  // Mark the omitted query contribution dead through the existing proof API.
  if(vk_pass_samples_live(payload))die("Vulkan bloom query liveness changed");
  return 1;
}
static int bq_before_draw(vk_shader *list[6],const p_draw *draw,const p_draw_idx *idx) {
  if(bq_restore_reg>=0 || bq_restore_color)die("Vulkan bloom unfinished state restoration");
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  if(bq_driver_fixture_reg>=0) {
    bq_restore_reg=bq_driver_fixture_reg;bq_restore_handle=S.tex[1][bq_restore_reg];
    bq_bind_texture(bq_restore_reg,bq_driver_fixture_handle);bq_driver_fixture_reg=-1;return 0;
  }
#endif
  if(bq.generation!=vk_pass_generation())memset(&bq,0,sizeof bq);
  const void *payload=draw?(const void *)draw:(const void *)idx;
  // Keep ordinary/disabled draws in the existing cheap dispatcher. Pending
  // chain stages must still restore/materialize even when the option is OFF.
  if(bq.replacement!=payload && bq.materialization!=payload && bq.overlay!=payload &&
      (!bq_fact(list[1],0) || !bq_enabled()))return 0;
  return bq_before_draw_cold(list,draw,idx,payload);
}
static void bq_after_draw(void) {
  if(bq_restore_color) { S.color=bq_original_color;bq_restore_color=0;vk_pipeline_state_changed(); }
  if(bq_restore_reg>=0) {
    bq_bind_texture(bq_restore_reg,bq_restore_handle);bq_restore_reg=-1;
  }
}
