#include "nvn_vk_phase_layout.h"
// 原 NVN 绘制状态映射到 Vulkan 渲染通道与管线；保守保留附件内容与命令次序。
// Map original NVN draw state to Vulkan render passes and pipelines,
// conservatively preserving attachments and command ordering.
#include "nvn_vk_shader.h"
#include "nvn_vk_extrap.h"
#include "nvn_vk_freeze.h"
#include "nvn_vk_plan.h"
#include "nvn_vk_system_trace.h"
#include "nvn_vk_pixel_local.h"
#include "nvn_vk_pixel_local_runtime.h"
#include <math.h>
#include <stdatomic.h>
#include "nvn_vk_render_objects.inc"
typedef struct pl_job pl_job;
typedef struct render_job {
  pl_job *local;
  struct render_job *next;
  VkRenderPass pass;
  VkRenderPass final_pass;
  VkCommandBuffer primary, secondary;
  int direct; // The experimental primary path aliases secondary to its actual primary recording context.
  int primary_begun;
  unsigned pending_clear_count;
  VkClearAttachment pending_clears[8];
  VkClearRect pending_rectangles[8];
  VkAttachmentDescription attachments[9];
  VkAttachmentReference colors[8], depth;
  VkClearValue clears[9];
  uint32_t attachment_count, color_count;
  VkFramebuffer framebuffer;
  render_pass_object *pass_object,*final_pass_object;
  framebuffer_object *framebuffer_object;
  VkPipeline pipeline;
  VkPipelineLayout layout;
  // Actual state emitted in this secondary, never inherited across recordings.
  VkPipeline bound_pipeline;
  VkPipelineLayout bound_layout;
  VkDescriptorSet bound_sets[6];
  vk_uniform_offsets bound_uniform_offsets[6];
  vk_push_bias_state bias_state;
  VkViewport viewport;
  VkRect2D scissor;
  float depth_bias[3];
  int dynamic_state_valid;
  VkRect2D area;
  unsigned gpu_region;
  int has_depth;
  int first_draw;
  int clear_only;
  unsigned load_elisions;
  int preserve_stores;
  // Physical binding state of this secondary only. Buffer contents/ownership
  // are resolved separately on every draw before consulting these keys.
  VkBuffer bound_vertex_buffers[16];
  VkDeviceSize bound_vertex_offsets[16];
  VkBuffer bound_index_buffer;
  VkDeviceSize bound_index_offset;
  VkIndexType bound_index_type;
  uint16_t bound_vertex_valid;
  uint8_t bound_index_valid;
  // Irreversible physical-pass may-write proof, separately for DS planes.
  // Uses existing struct tail padding; no per-descriptor state or counter.
  uint8_t ds_written;
  uint8_t attachment_representation_valid;
  void *raster_group; // private texture-history product, owned until job recycle
} render_job;
struct vk_graphics_submission {
  render_job *jobs;
};
static vk_graphics_submission *graphics_state;
// 同目标的连续绘制保持通道打开；传输、查询重置或目标变化时再结束。
// Keep consecutive draws to the same targets in one pass; end it for transfers,
// query resets or target changes.
static render_job *active_job;
static int pl_render_end(render_job *job);
static int pl_target_clear(const p_set_rt*,const p_scissor*,int,VkImageAspectFlags,const VkClearValue*);
// Single recording owner; cross-thread getters see the last graphics_end.
static struct {
  uint64_t depth_omissions,store_omissions,load_omissions,readonly_store_omissions;
  uint64_t clear_attachment_omissions,prepare_reuses,prepare_checks;
  uint64_t prepare_lookups;
  uint64_t pixel_local_plans,pixel_local_draws,pixel_local_aborts;
} draw_recorded;
static int draw_state_reference(void) {
  static int reference=-1;
  if(reference<0) {
    const char *v=getenv("DM_VULKAN_DRAW_STATE_REFERENCE");
    if(v && strcmp(v,"0") && strcmp(v,"1"))
      die("DM_VULKAN_DRAW_STATE_REFERENCE expects 0 or 1");
    reference=v && !strcmp(v,"1");
  }
  return reference;
}
// Representation-only experiment. Actual preparation, accesses and histories remain live.
static int attachment_reuse_mode(void) {
  static int enabled=-1;
  if(enabled<0) {
    const char *v=getenv("DM_VULKAN_ATTACHMENT_JOB_REUSE");
    if(v && strcmp(v,"0") && strcmp(v,"1"))die("DM_VULKAN_ATTACHMENT_JOB_REUSE expects 0 or 1");
    enabled=v && !strcmp(v,"1");
    LOG("[vulkan attachment reuse] enabled=%d",enabled);
  }
  return enabled;
}
void vk_render_attachment_representation_changed(void) {
  if(active_job)active_job->attachment_representation_valid=0;
}
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
static uint64_t attachment_reuse_hits,attachment_reuse_retries;
static void (*attachment_test_callback)(void *);
static void *attachment_test_context;
void vk_test_attachment_reuse_callback(void (*callback)(void *),void *context) {
  attachment_test_callback=callback;attachment_test_context=context;
}
void vk_test_attachment_reuse_stats(uint64_t *hits,uint64_t *retries) {
  *hits=attachment_reuse_hits;*retries=attachment_reuse_retries;
}
#endif
static p_set_rt active_targets;
static uint32_t active_writes;
static void render_binding_reset(render_job *j) {
  j->bound_vertex_valid=0;j->bound_index_valid=0;
}
static int render_binding_context(render_job *j) {
  if(j && active_job==j && V.cmd==j->secondary)return 1;
  // An unknown/reentrant command-buffer switch must invalidate the old job,
  // even if a future path later returns to that same VkCommandBuffer handle.
  if(j)render_binding_reset(j);
  if(active_job && active_job!=j)render_binding_reset(active_job);
  return 0;
}
static void render_bind_vertex(render_job *j,unsigned stream,VkBuffer buffer,
                               VkDeviceSize offset,int reference) {
  int current=render_binding_context(j);
  if(!reference && current && (j->bound_vertex_valid&(1u<<stream)) &&
      j->bound_vertex_buffers[stream]==buffer && j->bound_vertex_offsets[stream]==offset)return;
  vkCmdBindVertexBuffers(V.cmd,stream,1,&buffer,&offset);
  if(current) {
    j->bound_vertex_buffers[stream]=buffer;j->bound_vertex_offsets[stream]=offset;
    j->bound_vertex_valid|=1u<<stream;
  }
}
static void render_bind_index(render_job *j,VkBuffer buffer,VkDeviceSize offset,
                              VkIndexType type,int reference) {
  int current=render_binding_context(j);
  if(!reference && current && j->bound_index_valid && j->bound_index_buffer==buffer &&
      j->bound_index_offset==offset && j->bound_index_type==type)return;
  vkCmdBindIndexBuffer(V.cmd,buffer,offset,type);
  if(current) {
    j->bound_index_buffer=buffer;j->bound_index_offset=offset;j->bound_index_type=type;
    j->bound_index_valid=1;
  }
}
static atomic_uint_fast64_t pixel_local_plans,pixel_local_draws,pixel_local_aborts;
void vk_pixel_local_stats(uint64_t *planned,uint64_t *draws,uint64_t *aborts) {
  *planned=atomic_load(&pixel_local_plans);*draws=atomic_load(&pixel_local_draws);*aborts=atomic_load(&pixel_local_aborts);
}
static atomic_uint_fast64_t depth_omissions;
static atomic_uint_fast64_t store_omissions;
static atomic_uint_fast64_t load_omissions;
static atomic_uint_fast64_t readonly_store_omissions;
static atomic_uint_fast64_t clear_attachment_omissions;
static atomic_int clear_attachment_reference;
void vk_render_clear_reference(int reference) { atomic_store(&clear_attachment_reference,!!reference); }
int vk_render_clear_reference_mode(void) { return atomic_load(&clear_attachment_reference); }
uint64_t vk_render_clear_attachment_omissions(void) {
  return atomic_load(&clear_attachment_omissions);
}
uint64_t vk_render_depth_omissions(void) { return atomic_load(&depth_omissions); }
uint64_t vk_render_store_omissions(void) { return atomic_load(&store_omissions); }
uint64_t vk_render_load_omissions(void) { return atomic_load(&load_omissions); }
uint64_t vk_render_readonly_store_omissions(void) { return atomic_load(&readonly_store_omissions); }
static void render_primary_begin(render_job *j) {
  if(!j->direct || j->primary_begun)return;
  VkRenderPassBeginInfo begin={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    .renderPass=j->pass,.framebuffer=j->framebuffer,.renderArea=j->area,
    .clearValueCount=j->attachment_count,.pClearValues=j->clears};
  vkCmdBeginRenderPass(V.cmd,&begin,VK_SUBPASS_CONTENTS_INLINE);
  j->primary_begun=1;
  for(unsigned n=0;n<j->pending_clear_count;n++)
    vkCmdClearAttachments(V.cmd,1,&j->pending_clears[n],1,&j->pending_rectangles[n]);
  j->pending_clear_count=0;
}
void vk_render_materialize_pending(void) {
  if(active_job)render_primary_begin(active_job);
}
static void render_record_begin(render_job *j,const VkAttachmentDescription *attachments,
                                unsigned count,const VkAttachmentReference *colors,
                                unsigned color_count,const VkAttachmentReference *depth,
                                const VkClearValue *clears) {
  vk_texture_pass_begin();
  vk_system_trace_counter("Vulkan render area width",j->area.extent.width);
  vk_system_trace_counter("Vulkan render area height",j->area.extent.height);
  j->primary=V.cmd;j->attachment_count=count;j->color_count=color_count;
  j->attachment_representation_valid=1;
  memcpy(j->attachments,attachments,count*sizeof *attachments);
  memcpy(j->colors,colors,color_count*sizeof *colors);
  if(depth) j->depth=*depth;
  if(clears) memcpy(j->clears,clears,count*sizeof *clears);
  for(unsigned n=0;n<count;n++) {
    VkFormat f=attachments[n].format;
    if(f==VK_FORMAT_D16_UNORM || f==VK_FORMAT_X8_D24_UNORM_PACK32 || f==VK_FORMAT_D32_SFLOAT ||
        f==VK_FORMAT_D16_UNORM_S8_UINT || f==VK_FORMAT_D24_UNORM_S8_UINT || f==VK_FORMAT_D32_SFLOAT_S8_UINT) {
      if(attachments[n].loadOp==VK_ATTACHMENT_LOAD_OP_CLEAR)j->ds_written|=VK_IMAGE_ASPECT_DEPTH_BIT;
      if(attachments[n].stencilLoadOp==VK_ATTACHMENT_LOAD_OP_CLEAR)j->ds_written|=VK_IMAGE_ASPECT_STENCIL_BIT;
    } else if(f==VK_FORMAT_S8_UINT && attachments[n].stencilLoadOp==VK_ATTACHMENT_LOAD_OP_CLEAR)
      j->ds_written|=VK_IMAGE_ASPECT_STENCIL_BIT;
  }
  j->direct=V.options.render_record_mode==2;
  if(j->direct) {
    j->secondary=j->primary;
    // Clear-only jobs remain logical until their first consuming command.
    // Existing pre-pass dependency proofs can still place earlier producers'
    // barriers before them. A dependency on these clears ends/materializes
    // the job normally; their access clocks are already recorded below.
    render_binding_reset(j);return;
  }
  j->secondary = vk_command_buffer(VK_COMMAND_BUFFER_LEVEL_SECONDARY);
  VkCommandBufferInheritanceInfo inherit={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
    .renderPass=j->pass,.framebuffer=j->framebuffer};
  VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT|VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
    .pInheritanceInfo=&inherit};
  VK_CHECK(vk_command_begin(j->secondary,&bi));V.cmd=j->secondary;
  render_binding_reset(j);
}
// NONE preserves the untouched plane even when the other plane is writable.
// Keep LOAD and coupled layouts/history unchanged. Never use DONT_CARE for
// preserved guest-visible contents; a later writer/clear irrevocably rejects.
static int render_ds_plane_stores(render_job *j) {
  if(!V.load_store_op_none || !j->ds_written ||
      (j->ds_written&(VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT))==
      (VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT))return 0;
  int changed=0;
  for(unsigned n=0;n<j->attachment_count;n++) {
    VkAttachmentDescription *a=&j->attachments[n];
    if(a->format!=VK_FORMAT_D16_UNORM_S8_UINT && a->format!=VK_FORMAT_D24_UNORM_S8_UINT &&
        a->format!=VK_FORMAT_D32_SFLOAT_S8_UINT)continue;
    if(a->initialLayout!=a->finalLayout ||
        (a->initialLayout!=VK_IMAGE_LAYOUT_GENERAL &&
         a->initialLayout!=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
         a->initialLayout!=VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL))continue;
    if(!(j->ds_written&VK_IMAGE_ASPECT_DEPTH_BIT) && a->loadOp==VK_ATTACHMENT_LOAD_OP_LOAD &&
        a->storeOp==VK_ATTACHMENT_STORE_OP_STORE) {
      a->storeOp=VK_ATTACHMENT_STORE_OP_NONE_EXT;changed=1;
    }
    if(!(j->ds_written&VK_IMAGE_ASPECT_STENCIL_BIT) && a->stencilLoadOp==VK_ATTACHMENT_LOAD_OP_LOAD &&
        a->stencilStoreOp==VK_ATTACHMENT_STORE_OP_STORE) {
      a->stencilStoreOp=VK_ATTACHMENT_STORE_OP_NONE_EXT;changed=1;
    }
  }
  return changed;
}
static VkRect2D draw_area(uint32_t width, uint32_t height, int primitive) {
  VkRect2D area = {{0, 0}, {width, height}};
  // 填充三角形的附件覆盖限于视口；大于视口的后备图像不必整张读写。
  // Filled triangles only cover the viewport. Avoid loading/storing the whole
  // backing attachment when it is larger; points/lines retain their full area.
  if (!S.has_viewport || S.poly.mode != NVN_POLYGON_MODE_FILL ||
      primitive < 4 || primitive > 6)
    return area;
  const float *v = S.viewport[0];
  if (!isfinite(v[0]) || !isfinite(v[1]) || !isfinite(v[2]) ||
      !isfinite(v[3]) || v[2] <= 0 || v[3] <= 0)
    return area;
  int32_t left = fmax(0, fmin(width, floor(v[0]))),
          top = fmax(0, fmin(height, floor(v[1]))),
          right = fmax(0, fmin(width, ceil((double)v[0] + v[2]))),
          bottom = fmax(0, fmin(height, ceil((double)v[1] + v[3])));
  if (right <= left || bottom <= top)
    return (VkRect2D){{0, 0}, {1, 1}};
  return (VkRect2D){{left, top}, {right - left, bottom - top}};
}
static int contains(VkRect2D outer, VkRect2D inner) {
  return outer.offset.x <= inner.offset.x && outer.offset.y <= inner.offset.y &&
         (int64_t)outer.offset.x + outer.extent.width >=
             (int64_t)inner.offset.x + inner.extent.width &&
         (int64_t)outer.offset.y + outer.extent.height >=
             (int64_t)inner.offset.y + inner.extent.height;
}
// One vk_draw invocation owns this proof. SET_RT and direct S restoration
// between draws therefore need no new global target generation. Resource
// resolution still runs normally and can end/change the physical pass.
typedef struct {
  render_job *job;
  uint64_t pass,epoch;
  int enabled,valid,equal;
} draw_target_match;
static int target_fields_equal(const p_set_rt *a,const p_set_rt *b) {
  if(draw_state_reference())return !memcmp(a,b,sizeof *a);
  unsigned n=(unsigned)a->n;
  if(n>8 || n!=(unsigned)b->n || a->depth!=b->depth)return 0;
  // Attachment/clear/store and feedback consumers only inspect slots below n,
  // then require a non-null texture before consulting its view. Keep present
  // views byte-exact: even their currently disabled set_* fields are retained.
  // This is a comparison of current state, not a content/lifetime cache.
  if(a->depth && (a->has_dview!=b->has_dview ||
      (a->has_dview && memcmp(&a->dview,&b->dview,sizeof a->dview))))return 0;
  for(unsigned k=0;k<n;k++) {
    if(a->color[k]!=b->color[k])return 0;
    if(a->color[k] && (a->has_cview[k]!=b->has_cview[k] ||
        (a->has_cview[k] && memcmp(&a->cview[k],&b->cview[k],sizeof a->cview[k]))))return 0;
  }
  return 1;
}
static int render_targets_equal(const p_set_rt *targets,draw_target_match *proof) {
  render_job *job=active_job;
  if(!job) { if(proof)proof->valid=0;return 0; }
  if(!proof || !proof->enabled || targets!=&S.targets)
    return target_fields_equal(&active_targets,targets);
  uint64_t pass=V.passes,epoch=vk_pipeline_state_epoch();
  if(proof->valid && proof->job==job && proof->pass==pass && proof->epoch==epoch)
    return proof->equal;
  int equal=target_fields_equal(&active_targets,targets);
  *proof=(draw_target_match){job,pass,epoch,1,1,equal};
  return equal;
}
// The existing submission walk proves no future depth/stencil writers up to
// the next logical pass boundary. Do not split an established writable pass
// just to tighten its layout; an unexpected later writer still ends read-only
// recording through the ordinary image transition path.
static int readonly_depth_target(const p_set_rt *targets,int may_write,draw_target_match *proof) {
  if(may_write || vk_pass_depth_writes()) return 0;
  if(active_job && active_job->has_depth && render_targets_equal(targets,proof))
    return active_job->attachments[active_job->depth.attachment].initialLayout==
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
  return 1;
}
#include "nvn_vk_deferred_mip_clear.inc"
// 清除在命令原位置生效；同目标的后续绘制可继续当前通道。
// Clears take effect at their original command position. Subsequent draws to
// the same targets can continue the pass. A fresh pass uses attachment clears
// instead of loading old pixels only to overwrite them immediately.
int vk_render_target_clear(const p_set_rt *rt, const p_scissor *sc, int index,
                           VkImageAspectFlags aspect,
                           const VkClearValue *value) {
  if(pl_target_clear(rt,sc,index,aspect,value))return 1;
  if (rt->n < 0 || rt->n > 8)
    die("Vulkan clear render target count");
  // A clear-only pass must not load/store untouched siblings. Keep the normal
  // loadOp=CLEAR render path with just its target, rather than using a transfer
  // clear or discarding any sibling's contents. Active passes keep merging.
  unsigned siblings=0;
  for(int k=0;k<rt->n;k++) if(rt->color[k] && k!=index) siblings++;
  if(index>=0 && rt->depth) siblings++;
  if(!mip_clear_flushing && !active_job && siblings && !vk_render_clear_reference_mode() && vk_pass_clear_without_draws()) {
    p_set_rt isolated={0};int target=index;
    if(index>=0) {
      if(index>=rt->n || !rt->color[index]) die("Vulkan isolated clear target absent");
      isolated.n=1;isolated.color[0]=rt->color[index];
      isolated.has_cview[0]=rt->has_cview[index];
      if(isolated.has_cview[0]) isolated.cview[0]=rt->cview[index];
      target=0;
    } else {
      isolated.depth=rt->depth;isolated.has_dview=rt->has_dview;
      if(isolated.has_dview) isolated.dview=rt->dview;
    }
    int result=vk_render_target_clear(&isolated,sc,target,aspect,value);
    if(result==1) draw_recorded.clear_attachment_omissions += siblings;
    return result;
  }
  VkAttachmentDescription descriptions[9] = {0};
  VkAttachmentReference colors[8], depth = {0};
  VkImageView views[9];
  VkClearValue clears[9] = {0};
  uint32_t width = UINT32_MAX, height = UINT32_MAX, n = 0, target = UINT32_MAX;
  uint32_t clear_width = 0, clear_height = 0;
  int include_depth = rt->depth && (index<0 || vk_pass_depth_need());
  if(active_job && target_fields_equal(&active_targets,rt)) {
    if(include_depth && !active_job->has_depth) vk_render_end();
    else include_depth=active_job->has_depth;
  }
  for (int k = 0; k <= rt->n; k++) {
    const tex_t *t = k == rt->n ? (include_depth ? rt->depth : NULL) : rt->color[k];
    const view_t *v = k == rt->n ? (rt->has_dview ? &rt->dview : NULL)
                                 : (rt->has_cview[k] ? &rt->cview[k] : NULL);
    if (!t) {
      if (k < rt->n)
        colors[k] = (VkAttachmentReference){VK_ATTACHMENT_UNUSED,
                                            VK_IMAGE_LAYOUT_GENERAL};
      continue;
    }
    vk_attachment a = k==rt->n && readonly_depth_target(rt,index<0,NULL)?
        vk_texture_readonly_attachment(t,v):vk_texture_attachment(t,v);
    if (a.extent.width < width)
      width = a.extent.width;
    if (a.extent.height < height)
      height = a.extent.height;
    views[n] = a.view;
    descriptions[n] = (VkAttachmentDescription){
        .format = a.format,
        .samples = a.samples,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
        .initialLayout = a.layout,
        .finalLayout = a.layout};
    VkAttachmentReference reference = {n, a.layout};
    if (k == rt->n)
      depth = reference;
    else
      colors[k] = reference;
    if ((index < 0 && k == rt->n) || (index >= 0 && k == index)) {
      target = n;
      clear_width = a.extent.width;
      clear_height = a.extent.height;
    }
    n++;
  }
  if (target == UINT32_MAX)
    die("Vulkan clear target absent");
  int64_t x = sc ? sc->x : 0, y = sc ? sc->y : 0,
          right = sc ? x + sc->w : clear_width,
          bottom = sc ? y + sc->h : clear_height;
  if (x < 0)
    x = 0;
  if (y < 0)
    y = 0;
  if (right > clear_width)
    right = clear_width;
  if (bottom > clear_height)
    bottom = clear_height;
  if (right <= x || bottom <= y)
    return 2;
  // 混合尺寸目标的清除不得被更小附件裁掉，交回单附件路径处理。
  // A smaller sibling attachment must not truncate the clear; retain the
  // separate-attachment path for mixed extents or incompatible sample counts.
  if (right > width || bottom > height)
    return 0;
  for (unsigned k = 1; k < n; k++)
    if (descriptions[k].samples != descriptions[0].samples)
      return 0;
  VkRect2D area = {{x, y}, {right - x, bottom - y}};
  render_job *job = active_job;
  if (job && target_fields_equal(&active_targets,rt) &&
      (index >= 0 || job->has_depth) &&
      contains(job->area, area)) {
    VkClearAttachment clear = {.aspectMask = aspect,
                               .colorAttachment = index < 0 ? 0 : index,
                               .clearValue = *value};
    VkClearRect rectangle = {.rect = area, .layerCount = 1};
    if(job->direct && !job->primary_begun && job->pending_clear_count<8) {
      unsigned n=job->pending_clear_count++;
      job->pending_clears[n]=clear;job->pending_rectangles[n]=rectangle;
    } else {
      render_primary_begin(job);
      vkCmdClearAttachments(V.cmd, 1, &clear, 1, &rectangle);
    }
  } else {
    // SET_RT need not end an old pass. Reconsider sibling isolation only at
    // the point where this clear already cannot reuse it, never by splitting
    // an otherwise reusable pass. Keep empty/partial clears on the old path.
    const uint8_t *clear_op=NULL;
    uint64_t clear_generation=0;
    int isolate=!mip_clear_flushing && siblings && !vk_render_clear_reference_mode() &&
        !area.offset.x && !area.offset.y &&
        area.extent.width==clear_width && area.extent.height==clear_height &&
        vk_pass_clear_without_draws() && vk_pass_future_op(0,&clear_op,NULL);
    p_set_rt requested;
    p_scissor requested_sc;
    if(isolate) {
      requested=*rt;
      if(sc) requested_sc=*sc;
      clear_generation=vk_pass_generation();
    }
    vk_render_end();
    // Ending can flush upstream drag-follow draws. Revalidate the immutable
    // command proof and restored state, then resolve fresh attachments in the
    // existing isolated path (nested draws may have changed resource views).
    const uint8_t *current_op=NULL;
    if(isolate && !active_job && !vk_render_clear_reference_mode() &&
        clear_generation==vk_pass_generation() &&
        vk_pass_future_op(0,&current_op,NULL) && current_op==clear_op &&
        !memcmp(&requested,rt,sizeof requested) &&
        (!sc || !memcmp(&requested_sc,sc,sizeof requested_sc)) &&
        vk_pass_clear_without_draws())
      return vk_render_target_clear(&requested,sc?&requested_sc:NULL,index,aspect,value);
    if(vk_graphics_resource_dependencies(0)) vk_texture_targets_barrier(rt,include_depth);
    job = calloc(1, sizeof *job);
    if (!job)
      die("Vulkan clear pass allocation");
    job->area = area;job->clear_only=1;
    job->has_depth = include_depth;
    if (aspect & (VK_IMAGE_ASPECT_COLOR_BIT | VK_IMAGE_ASPECT_DEPTH_BIT))
      descriptions[target].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    if (aspect & VK_IMAGE_ASPECT_STENCIL_BIT)
      descriptions[target].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    clears[target] = *value;
    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = rt->n,
        .pColorAttachments = colors,
        .pDepthStencilAttachment = include_depth ? &depth : NULL};
    VkRenderPassCreateInfo rp = {.sType =
                                     VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                                 .attachmentCount = n,
                                 .pAttachments = descriptions,
                                 .subpassCount = 1,
                                 .pSubpasses = &subpass};
    job->pass_object=render_pass_acquire(&rp);job->pass=job->pass_object->handle;
    VkFramebufferCreateInfo fb = {.sType =
                                      VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                                  .renderPass = job->pass,
                                  .attachmentCount = n,
                                  .pAttachments = views,
                                  .width = width,
                                  .height = height,
                                  .layers = 1};
    job->framebuffer_object=framebuffer_acquire(job->pass_object,&fb);
    job->framebuffer=job->framebuffer_object->handle;
    job->next = graphics_state->jobs;
    graphics_state->jobs = job;
    job->gpu_region = vk_gpu_region_begin("render", NULL, area.extent.width, area.extent.height);
    render_record_begin(job,descriptions,n,colors,rt->n,include_depth?&depth:NULL,clears);
    active_job = job;
    active_targets = *rt;
    active_writes = 0;
    vk_ubo_plan_break();V.passes++;
    if(rt->depth && !job->has_depth) {
      uint64_t n=(draw_recorded.depth_omissions += 1);
      if(n<=4 || (V.options.stats && n%600==0))
        LOG("[vulkan pass] omitted unused depth/stencil attachment total=%llu",(unsigned long long)n);
    }
  }
  active_writes |= 1u << (index < 0 ? 8 : index);
  if(index<0) job->ds_written |= aspect&(VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT);
  // Load/store operations access every included attachment, not just the one
  // explicitly cleared. Include those accesses in later transfer hazards.
  for(int k=0;k<=rt->n;k++) {
    const tex_t *t=k==rt->n?(include_depth?rt->depth:NULL):rt->color[k];
    const view_t *v=k==rt->n?(rt->has_dview?&rt->dview:NULL):
        (rt->has_cview[k]?&rt->cview[k]:NULL);
    if(t) vk_texture_attachment_record(t,v,0);
  }
  // 清除后的采样必须可见，即使调用方没有另发显式纹理屏障。
  // Sampling after a clear must observe it even without a separate explicit
  // texture barrier; actual read dependencies end the pass as usual.
  V.nvn_barrier |= NVN_BARRIER_INVALIDATE_TEXTURE_BIT;
  return 1;
}
void vk_nvn_barrier(uint32_t flags) {
  vk_texture_raster_break();
  if(flags)vk_image_split_break();
  vk_mip_clear_drain();
  if (flags & ~255u)
    die("Vulkan unknown NVN barrier flags %#x", flags);
  if (!flags)
    return;
  if (!active_job && vk_submission_barrier_mode()!=0) {
    vk_barrier();
    return;
  }
  // 同一通道内颜色混合/深度模板按光栅顺序执行。把缓存/顺序请求保留到实际读依赖，
  // 不能仅因 NVN 屏障就把所有附件写回。可写着色器资源另有逐绘制屏障。
  // Blending and depth/stencil retain raster order in a pass. Defer cache/order
  // requests until an actual read dependency, rather than storing every
  // attachment just for an NVN barrier. Shader writes have per-draw barriers.
  V.nvn_barrier |= flags;
}
void vk_render_texture_read(const tex_t *texture, VkShaderStageFlagBits stage) {
  vk_mip_clear_texture_access(texture,NULL,0);
  if (!V.nvn_barrier || !active_job)
    return;
  for (int i = 0; i <= active_targets.n; i++) {
    if (!(active_writes & (1u << (i == active_targets.n ? 8 : i))))
      continue;
    const tex_t *target =
        i == active_targets.n ? active_targets.depth : active_targets.color[i];
    if (!target)
      continue;
    int overlap =
        texture->mem && target->mem &&
        (texture->mem == target->mem ||
         (texture->L.size && target->L.size &&
          (uintptr_t)texture->mem < (uintptr_t)target->mem + target->L.size &&
          (uintptr_t)target->mem < (uintptr_t)texture->mem + texture->L.size));
    if (texture->serial == target->serial || overlap) {
      // Exact images are covered after all bindings have been collected.
      // Guest-memory aliases with distinct VkImages retain the old fallback.
      if(texture->serial==target->serial && vk_texture_shader_collecting() &&
          vk_submission_barrier_mode()==0) return;
      // Descriptor preparation knows the actual consuming stage. A fragment
      // sample of a prior attachment must not also stall independent vertices.
      if(vk_submission_barrier_mode()==0 && stage==VK_SHADER_STAGE_FRAGMENT_BIT)
        vk_draw_barrier(0);
      else if(vk_submission_barrier_mode()==0 && stage==VK_SHADER_STAGE_COMPUTE_BIT &&
          !(V.nvn_barrier&(NVN_BARRIER_ORDER_PRIMITIVES_BIT|NVN_BARRIER_ORDER_INDIRECT_DATA_BIT)))
        vk_compute_barrier();
      else vk_barrier();
      return;
    }
  }
}
static struct {
  const char *file;
  unsigned line, count;
} end_sites[64];
static unsigned end_site_count;
void vk_render_end_at(const char *file, unsigned line) {
  vk_texture_raster_break();
  vk_image_split_break();
  vk_ubo_plan_break();
  if (active_job && !vk_pool_maintenance_active()) vk_ghost_flush();
  if(active_job && active_job->local) {pl_render_end(active_job);mip_clear_materialize();return;}
  if (active_job) {
    if (V.options.stats) {
      unsigned i = 0;
      while (i < end_site_count &&
             (end_sites[i].file != file || end_sites[i].line != line))
        i++;
      if (i == end_site_count && i < 64) {
        end_sites[i].file = file;
        end_sites[i].line = line;
        end_site_count++;
      }
      if (i < 64)
        end_sites[i].count++;
    }
    render_job *j=active_job;
    render_primary_begin(j);
    vk_counter_pass_end();
    if(!j->direct)VK_CHECK(vkEndCommandBuffer(j->secondary));
    V.cmd=j->primary;
    unsigned dead=0;
    int readonly_none=0;
    // Both comparison modes retain their identical initial attachment ops.
    // The primary pass has already begun; never retroactively change them.
    if(!V.options.render_record_mode) {
    for(unsigned k=0;k<j->color_count;k++) {
      unsigned a=j->colors[k].attachment;if(a==VK_ATTACHMENT_UNUSED) continue;
      if(!j->preserve_stores && !vk_pass_store_needed(active_targets.color[k],active_targets.has_cview[k]?&active_targets.cview[k]:NULL,VK_IMAGE_ASPECT_COLOR_BIT)) {
        j->attachments[a].storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;dead++;
      }
    }
    if(j->has_depth) {
      VkAttachmentDescription *a=&j->attachments[j->depth.attachment];
      const view_t *v=active_targets.has_dview?&active_targets.dview:NULL;
      // NONE preserves memory only when no values were written in this
      // physical pass. Require the complete read-only layout and LOAD proof
      // for both aspects; a clear or actual writer must retain ordinary stores.
      readonly_none=V.load_store_op_none && !(active_writes&(1u<<8)) &&
          j->depth.layout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL &&
          a->initialLayout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL &&
          a->finalLayout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL &&
          a->loadOp==VK_ATTACHMENT_LOAD_OP_LOAD &&
          a->stencilLoadOp==VK_ATTACHMENT_LOAD_OP_LOAD;
      if(readonly_none) {
        a->storeOp=a->stencilStoreOp=VK_ATTACHMENT_STORE_OP_NONE_EXT;
        draw_recorded.readonly_store_omissions++;
      }
      if(!readonly_none && !j->preserve_stores && a->format!=VK_FORMAT_S8_UINT && !vk_pass_store_needed(active_targets.depth,v,VK_IMAGE_ASPECT_DEPTH_BIT)) {
        a->storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;dead++;
      }
      if(!readonly_none && !j->preserve_stores && (a->format==VK_FORMAT_S8_UINT || a->format==VK_FORMAT_D16_UNORM_S8_UINT || a->format==VK_FORMAT_D24_UNORM_S8_UINT || a->format==VK_FORMAT_D32_SFLOAT_S8_UINT) &&
         !vk_pass_store_needed(active_targets.depth,v,VK_IMAGE_ASPECT_STENCIL_BIT)) {
        a->stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;dead++;
      }
    }
    if(j->load_elisions) {
      uint64_t n=(draw_recorded.load_omissions += j->load_elisions);
      if(n<=8 || (V.options.stats && n%600<j->load_elisions))
        LOG("[vulkan pass] omitted fully overwritten attachment loads total=%llu",(unsigned long long)n);
    }
    int plane_none=render_ds_plane_stores(j);
    if(dead||j->load_elisions||readonly_none||plane_none) {
      VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount=j->color_count,.pColorAttachments=j->colors,
        .pDepthStencilAttachment=j->has_depth?&j->depth:NULL};
      VkRenderPassCreateInfo ci={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount=j->attachment_count,.pAttachments=j->attachments,
        .subpassCount=1,.pSubpasses=&sub};
      j->final_pass_object=render_pass_acquire(&ci);j->final_pass=j->final_pass_object->handle;
      uint64_t n=(draw_recorded.store_omissions += dead);
      if(dead && (n<=8 || (V.options.stats && n%600<dead)))
        LOG("[vulkan pass] omitted overwritten attachment stores total=%llu",(unsigned long long)n);
    }
    }
    if(!j->direct) {
    VkRenderPassBeginInfo begin={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass=j->final_pass?j->final_pass:j->pass,.framebuffer=j->framebuffer,.renderArea=j->area,
      .clearValueCount=j->attachment_count,.pClearValues=j->clears};
    vkCmdBeginRenderPass(V.cmd,&begin,VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
    vk_command_execute(1,&j->secondary);
    }
    vkCmdEndRenderPass(V.cmd);
    // Read-only depth testing/sampling may share the pass. Its physical store
    // is an access at pass end, not an imaginary write by every depth test.
    // Record stores directly even while an outer descriptor batch is pending.
    for(int k=0;k<=active_targets.n;k++) {
      const tex_t *t=k==active_targets.n?(j->has_depth?active_targets.depth:NULL):active_targets.color[k];
      const view_t *v=k==active_targets.n?(active_targets.has_dview?&active_targets.dview:NULL):
          (active_targets.has_cview[k]?&active_targets.cview[k]:NULL);
      if(t) {
        unsigned a=k==active_targets.n?j->depth.attachment:j->colors[k].attachment;
        if(k==active_targets.n && readonly_none) continue;
        vk_texture_attachment_store(t,v,j->attachments[a].storeOp==VK_ATTACHMENT_STORE_OP_STORE);
      }
    }
    vk_gpu_region_end(active_job->gpu_region);
    active_job = NULL;
    active_writes = 0;
  }
  mip_clear_materialize();
}
void vk_render_end_preserve(void) {
  if(active_job) active_job->preserve_stores=1;
  vk_render_end();
}
int vk_render_active(void) { return active_job!=NULL; }
// A secondary leaves its primary pass unstarted until recording ends. The
// experimental primary also delays clear-only jobs until their first consumer.
// Proven dependencies on earlier producers may still precede either pending pass.
VkCommandBuffer vk_render_pending_primary(void) {
  return active_job && (!active_job->direct || !active_job->primary_begun)?active_job->primary:VK_NULL_HANDLE;
}
void vk_render_end_stats(int print) {
  for (unsigned i = 0; i < end_site_count; i++) {
    if (print && end_sites[i].count) {
      const char *file = strrchr(end_sites[i].file, '/');
      LOG("[vulkan pass end] %s:%u count=%u",
          file ? file + 1 : end_sites[i].file, end_sites[i].line,
          end_sites[i].count);
    }
    end_sites[i].count = 0;
  }
}
// 按实际 Vulkan 管线状态缓存；缓冲地址、纹理与动态视口不属于管线键。
// Cache actual Vulkan pipeline state; buffer addresses, textures and dynamic
// viewports are not part of the pipeline key.
typedef struct {
  VkShaderModule modules[5];
  uint32_t specialization_word,specialization_mask,dynamic_uniforms;
  VkFormat formats[9];
  VkAttachmentReference colors[8], depth;
  VkVertexInputBindingDescription streams[16];
  uint32_t divisors[16];
  VkVertexInputAttributeDescription attributes[16];
  VkPipelineRasterizationStateCreateInfo raster;
  VkPipelineMultisampleStateCreateInfo multisample;
  VkPipelineDepthStencilStateCreateInfo depth_stencil;
  VkPipelineColorBlendAttachmentState blends[8];
  float blend_constants[4];
  uint32_t targets, attachments, streams_count, attributes_count, topology;
  uint32_t has_depth, logic_enable, logic_op;
} pipeline_key;
// Vulkan ABI 结构有未定义填充字节，不能把整结构赋值带来的栈内容当状态键。
// Vulkan ABI structs have unspecified padding. Copy scalar payload fields,
// never incidental stack padding, into zero-initialized cache keys.
static void pipeline_raster_key(VkPipelineRasterizationStateCreateInfo *d,const VkPipelineRasterizationStateCreateInfo *s) {
  d->flags=s->flags;d->depthClampEnable=s->depthClampEnable;d->rasterizerDiscardEnable=s->rasterizerDiscardEnable;
  d->polygonMode=s->polygonMode;d->cullMode=s->cullMode;d->frontFace=s->frontFace;
  d->depthBiasEnable=s->depthBiasEnable;d->depthBiasConstantFactor=s->depthBiasConstantFactor;
  d->depthBiasClamp=s->depthBiasClamp;d->depthBiasSlopeFactor=s->depthBiasSlopeFactor;d->lineWidth=s->lineWidth;
}
static void pipeline_multisample_key(VkPipelineMultisampleStateCreateInfo *d,const VkPipelineMultisampleStateCreateInfo *s) {
  d->flags=s->flags;d->rasterizationSamples=s->rasterizationSamples;d->sampleShadingEnable=s->sampleShadingEnable;
  d->minSampleShading=s->minSampleShading;d->pSampleMask=s->pSampleMask;
  d->alphaToCoverageEnable=s->alphaToCoverageEnable;d->alphaToOneEnable=s->alphaToOneEnable;
}
static void pipeline_depth_key(VkPipelineDepthStencilStateCreateInfo *d,const VkPipelineDepthStencilStateCreateInfo *s) {
  d->flags=s->flags;d->depthTestEnable=s->depthTestEnable;d->depthWriteEnable=s->depthWriteEnable;
  d->depthCompareOp=s->depthCompareOp;d->depthBoundsTestEnable=s->depthBoundsTestEnable;d->stencilTestEnable=s->stencilTestEnable;
  d->front=s->front;d->back=s->back;d->minDepthBounds=s->minDepthBounds;d->maxDepthBounds=s->maxDepthBounds;
}
typedef struct cached_pipeline {
  struct cached_pipeline *next[2];
  pipeline_key key;
  VkPipeline pipeline;
  VkPipelineLayout layout;
  unsigned warm_origin;
  VkRenderPass local_pass;
  render_pass_object *local_object;
  unsigned subpass;
} cached_pipeline;
static cached_pipeline *pipelines[2][1024];
static _Atomic uint32_t pipeline_count;
uint32_t vk_graphics_pipeline_count(void) { return pipeline_count; }
static _Atomic int pipeline_byte_hash;
void vk_graphics_byte_hash(int legacy) { atomic_store(&pipeline_byte_hash,legacy!=0); }
typedef struct {
  uint64_t epoch,program_serial;
  VkShaderModule fragment_module;
  uint32_t specialization_word,specialization_mask,dynamic_uniforms;
  VkFormat formats[9];
  VkAttachmentReference colors[8],depth;
  VkPipelineDepthStencilStateCreateInfo ds;
  uint32_t targets,attachments,has_depth,samples,topology,expansion;
} pipeline_context;
static pipeline_context prepared_context;
static cached_pipeline *prepared_pipeline;
static uint32_t prepared_streams;
static _Atomic int pipeline_prepare_mode;
static int active_pipeline_prepare_mode;
static _Atomic uint64_t prepare_reuses,prepare_lookups,prepare_checks;
void vk_graphics_prepare_mode(int mode) {
  if(mode<0||mode>2) die("Vulkan pipeline preparation mode");
  atomic_store(&pipeline_prepare_mode,mode);
}
void vk_graphics_prepare_stats(uint64_t *reuses,uint64_t *lookups,uint64_t *checks) {
  *reuses=atomic_load(&prepare_reuses);*lookups=atomic_load(&prepare_lookups);*checks=atomic_load(&prepare_checks);
}
static unsigned pipeline_bucket(const pipeline_key *key,int legacy) {
  if(legacy) {
    uint32_t hash=2166136261u;
    for(size_t i=0;i<sizeof *key;i++) hash=(hash^((const uint8_t *)key)[i])*16777619u;
    return hash%1024;
  }
  // 哈希只选桶，完整键比较仍决定匹配；64 位读取避免逐字节乘法依赖链。
  // Hashes only select buckets; full-key comparison still decides matches.
  // Word loads avoid the measured serial byte-by-byte multiply dependency.
  _Static_assert(sizeof *key%8==0,"pipeline key word alignment");
  uint64_t hash=UINT64_C(14695981039346656037);
  for(size_t i=0;i<sizeof *key;i+=8) {
    uint64_t word;memcpy(&word,(const uint8_t *)key+i,8);
    hash=(hash^word)*UINT64_C(1099511628211);
  }
  hash^=hash>>32;hash^=hash>>16;hash^=hash>>10;
  return hash%1024;
}
#include "nvn_vk_prewarm.inc"
vk_graphics_submission *vk_graphics_begin(vk_graphics_submission *state) {
  if (graphics_state || active_job) die("Vulkan nested graphics recording");
  if (!state) {
    state = calloc(1, sizeof *state);
    if (!state) die("Vulkan graphics resources allocation");
  }
  if (state->jobs) die("Vulkan graphics resources reused before recycling");
  graphics_state = state;
  mip_clear_begin();
  // 按提交锁定模式；命令更新和绘制必须使用同一套失效规则。
  // Latch per submission: state updates and draws must share invalidation rules.
  int mode=atomic_load_explicit(&pipeline_prepare_mode,memory_order_relaxed);
  if(mode!=active_pipeline_prepare_mode) {
    active_pipeline_prepare_mode=mode;prepared_pipeline=NULL;
    vk_pipeline_state_tracking(mode!=1);
  }
  return state;
}
void vk_graphics_end(vk_graphics_submission *state) {
  if (!state || graphics_state != state || active_job)
    die("Vulkan graphics submit ended with an open pass");
  atomic_store_explicit(&depth_omissions,draw_recorded.depth_omissions,memory_order_relaxed);
  atomic_store_explicit(&store_omissions,draw_recorded.store_omissions,memory_order_relaxed);
  atomic_store_explicit(&load_omissions,draw_recorded.load_omissions,memory_order_relaxed);
  atomic_store_explicit(&readonly_store_omissions,draw_recorded.readonly_store_omissions,memory_order_relaxed);
  atomic_store_explicit(&clear_attachment_omissions,draw_recorded.clear_attachment_omissions,memory_order_relaxed);
  atomic_store_explicit(&prepare_reuses,draw_recorded.prepare_reuses,memory_order_relaxed);
  atomic_store_explicit(&prepare_checks,draw_recorded.prepare_checks,memory_order_relaxed);
  atomic_store_explicit(&prepare_lookups,draw_recorded.prepare_lookups,memory_order_relaxed);
  atomic_store(&pixel_local_plans,draw_recorded.pixel_local_plans);
  atomic_store(&pixel_local_draws,draw_recorded.pixel_local_draws);
  atomic_store(&pixel_local_aborts,draw_recorded.pixel_local_aborts);
  mip_clear_publish();
  graphics_state = NULL;
}
void vk_graphics_recycle(vk_graphics_submission *state) {
  if (!state) return;
  if (graphics_state == state) die("Vulkan recycling recording graphics resources");
  while (state->jobs) {
    render_job *j = state->jobs;
    state->jobs = j->next;
    if(j->framebuffer_object) framebuffer_release(j->framebuffer_object);
    else vkDestroyFramebuffer(V.device,j->framebuffer,NULL);
    if(j->pass_object) render_pass_release(j->pass_object);
    else vkDestroyRenderPass(V.device,j->pass,NULL);
    if(j->final_pass_object) render_pass_release(j->final_pass_object);
    vk_texture_raster_free(j->raster_group);
    free(j->local);free(j);
  }
}
static VkPrimitiveTopology topology(int p) {
  switch (p) {
  case 0:
    return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
  case 1:
    return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  case 3:
    return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
  case 4:
  case 7:
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  case 5:
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  case 6:
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
  case 10:
    return VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY;
  case 11:
    return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY;
  case 12:
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY;
  case 13:
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY;
  case 14:
    return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
  default:
    die("Vulkan topology pending: %d", p);
  }
}
static vk_buffer *quad_index_buffer(void) {
  // 原 Metal g_quad_ib：65536 个四边形，共用 0,1,2,0,2,3 的 32 位索引。
  // Original Metal g_quad_ib: immutable 32-bit 0,1,2,0,2,3 indices for 65536 quads.
  static vk_buffer indices;
  if (!indices.handle) {
    vk_buffer_create(&indices,65536u*6*4,VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    uint32_t *out=(void *)indices.map;
    for (uint32_t q=0;q<65536;q++) {
      uint32_t b=q*4,values[]={b,b+1,b+2,b,b+2,b+3};memcpy(out+q*6,values,sizeof values);
    }
  }
  return &indices;
}
static VkBlendFactor blend_factor(int f) {
  switch (f) {
  case 1:
    return VK_BLEND_FACTOR_ZERO;
  case 2:
    return VK_BLEND_FACTOR_ONE;
  case 3:
    return VK_BLEND_FACTOR_SRC_COLOR;
  case 4:
    return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
  case 5:
    return VK_BLEND_FACTOR_SRC_ALPHA;
  case 6:
    return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  case 7:
    return VK_BLEND_FACTOR_DST_ALPHA;
  case 8:
    return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
  case 9:
    return VK_BLEND_FACTOR_DST_COLOR;
  case 10:
    return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
  case 11:
    return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
  case 0x10:
    return VK_BLEND_FACTOR_SRC1_COLOR;
  case 0x11:
    return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
  case 0x12:
    return VK_BLEND_FACTOR_SRC1_ALPHA;
  case 0x13:
    return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
  case 0x61:
    return VK_BLEND_FACTOR_CONSTANT_COLOR;
  case 0x62:
    return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
  case 0x63:
    return VK_BLEND_FACTOR_CONSTANT_ALPHA;
  case 0x64:
    return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
  default:
    die("Vulkan blend factor %d", f);
  }
}
static VkCompareOp compare(int op) {
  if (op < 1 || op > 8)
    die("Vulkan compare %d", op);
  return op - 1;
}
static VkStencilOp stencil(int op) {
  if (op < 1 || op > 8)
    die("Vulkan stencil op %d", op);
  return op - 1;
}
static int stencil_face_writes(const VkStencilOpState *face) {
  return face->writeMask && (face->failOp!=VK_STENCIL_OP_KEEP ||
      face->passOp!=VK_STENCIL_OP_KEEP || face->depthFailOp!=VK_STENCIL_OP_KEEP);
}
static int pipeline_stencil_writes(const VkPipelineDepthStencilStateCreateInfo *state) {
  return state->stencilTestEnable &&
      (stencil_face_writes(&state->front) || stencil_face_writes(&state->back));
}
// A caller must prove that all contents written by this exact color-only
// clear are dead before any reader. Active jobs have not encoded their render
// pass into the primary yet; only their secondary is currently recording.
// Retain the job and immutable cache references until ordinary fence recycle.
static int render_cancel_obsolete_clear(const tex_t *texture,const view_t *view) {
  if(V.options.render_record_mode)return 0; // Keep the same clear work in both comparison arms.
  render_job *job=active_job;
  if(!job || !job->clear_only || job->first_draw || job->has_depth || job->preserve_stores ||
      !job->secondary || V.cmd!=job->secondary || vk_counter_active() ||
      job->attachment_count!=1 || job->color_count!=1 || job->colors[0].attachment!=0 ||
      job->attachments[0].loadOp!=VK_ATTACHMENT_LOAD_OP_CLEAR ||
      active_targets.n!=1 || active_targets.depth || !active_targets.color[0] ||
      active_targets.color[0]->serial!=texture->serial || active_writes!=1 ||
      active_targets.has_cview[0]!=!!view ||
      (view && memcmp(view,&active_targets.cview[0],sizeof *view)))return 0;
  VK_CHECK(vkEndCommandBuffer(job->secondary));V.cmd=job->primary;
  vk_gpu_region_end(job->gpu_region);job->gpu_region=0;
  active_job=NULL;active_writes=0;memset(&active_targets,0,sizeof active_targets);
  // Do not decrement V.passes: image/ghost histories use its monotonic value
  // as a pass identity. Actual vkCmdBeginRenderPass counts exclude this job.
  return 1;
}
#include "nvn_vk_cover.inc"
#include "nvn_vk_zero_draw.inc"
#include "nvn_vk_nofxaa.inc"
#include "nvn_vk_bloom_quarter.inc"
#include "nvn_vk_pixel_local_geometry.inc"
#include "nvn_vk_pixel_local_plan.inc"
#include "nvn_vk_pixel_local_render.inc"
static _Atomic int vertex_divisor_fallback=-1;
void vk_vertex_divisor_fallback(int enabled) { atomic_store(&vertex_divisor_fallback,!!enabled); }
// Optional host replay: metadata only, before borrowing any operation data.
// Late CPU expansion inputs must be current AND have no pending publication;
// neither a required read nor the original pending fallback may submit here.
int vk_ghost_cpu_draw_ready(vk_shader *const list[6],const vk_state *state,
    const p_draw *draw,const p_draw_idx *idx) {
  (void)draw;
  if(!V.options.pool_defer_publication
#ifdef DM_VULKAN_SHARED_POOLS
      && !vk_legacy_astc_copy_mode()
#endif
      )return 1;
  if(!vk_shader_cpu_sources_ready(list,state,0) || !list[0])return 0;
  uint32_t spans[16]={0},mask=0;
  int force=atomic_load(&vertex_divisor_fallback);if(force<0)force=V.options.vertex_divisor_fallback;
  for(unsigned a=0;a<16;a++)if((list[0]->input_mask&(1u<<a)) && state->attr[a].fmt) {
    unsigned stream=state->attr[a].stream;if(stream>=16)return 0;
    if(state->stream[stream].stride && state->stream[stream].divisor>1 &&
        (force || state->stream[stream].divisor>V.max_vertex_divisor)) {
      int bw,bh,bytes=nvn_fmt_bpb(state->attr[a].fmt,&bw,&bh);
      uint64_t span=(uint64_t)state->attr[a].offset+bytes;
      if(bytes<=0 || bw!=1 || bh!=1 || span>V.props.limits.maxVertexInputBindingStride)return 0;
      mask|=1u<<stream;if(span>spans[stream])spans[stream]=(uint32_t)span;
    }
  }
  uint64_t count=idx?idx->instances:1,base=idx?idx->base_instance:0;
  for(unsigned stream=0;stream<16;stream++)if((mask&(1u<<stream)) && count) {
    uint64_t stride=state->stream[stream].stride,last=(count-1)/state->stream[stream].divisor;
    if(base>UINT64_MAX-last || base+last>UINT64_MAX/stride)return 0;
    uint64_t first=base*stride,end=(base+last)*stride,span=spans[stream],offset;
    if(end>state->vertex[stream].size || span>state->vertex[stream].size-end ||
        state->vertex[stream].address>UINT64_MAX-first)return 0;
    uint64_t address=state->vertex[stream].address+first,size=end-first+span;
    if(!vk_pool_range_offset(address,size,&offset) || vk_pool_pending_writes(address,size) ||
        vk_pool_cpu_unknown(address,size))return 0;
  }
  return 1;
}
typedef struct {const p_draw_idx *indexed;} draw_pool_plan;
static int draw_pool_collect(void *context,vk_pool_requirement_sink sink,void *out) {
  draw_pool_plan *plan=context;vk_shader *list[6]={0};
  if(!S.program)return 0;
  vk_program_shaders(S.program,list);list[5]=NULL;
  return vk_graphics_pool_admission(list,&S,plan->indexed,sink,out);
}
// Created only behind the existing preparation-miss/check guard. All borrows
// are synchronous, with the same resource work and retry cleanup as before.
// No large key or Vk creation arrays are built/copied on a context reuse.
typedef struct {
  vk_shader **list;
  const p_draw *draw;const p_draw_idx *idx;
  vk_buffer **expanded;const uint32_t *expanded_stride;
  int dynamic_uniforms,physical_depth,original_specialized,hybrid_selected;
  uint32_t na;VkSampleCountFlagBits samples;
  const VkPipelineDepthStencilStateCreateInfo *ds;
  const VkAttachmentDescription *descriptions;
  const VkAttachmentReference *color;VkAttachmentReference pipeline_depth;
  VkShaderModule fragment_module;
  uint32_t *specialization_words;VkSpecializationInfo *specialization_info;
  render_job *job;const vk_pixel_local_proof *local_consumer;
  pipeline_context *context;
  VkRenderPass local_pass;unsigned local_subpass;
  int reuse,prepare_mode;
} pipeline_prepare_input;
static VK_COLD_PHASE cached_pipeline *draw_pipeline_prepare(
    const pipeline_prepare_input *input,uint32_t *out_streams,int *retry) {
  vk_shader **list=input->list;
  const p_draw *draw=input->draw;const p_draw_idx *idx=input->idx;
  vk_buffer **expanded=input->expanded;const uint32_t *expanded_stride=input->expanded_stride;
  int dynamic_uniforms=input->dynamic_uniforms,physical_depth=input->physical_depth;
  int original_specialized=input->original_specialized,hybrid_selected=input->hybrid_selected;
  uint32_t na=input->na;VkSampleCountFlagBits samples=input->samples;
  const VkPipelineDepthStencilStateCreateInfo *ds=input->ds;
  const VkAttachmentDescription *descriptions=input->descriptions;
  const VkAttachmentReference *color=input->color;VkAttachmentReference pipeline_depth=input->pipeline_depth;
  VkShaderModule fragment_module=input->fragment_module;
  uint32_t *specialization_words=input->specialization_words;
  VkSpecializationInfo *specialization_info=input->specialization_info;
  render_job *j=input->job;const vk_pixel_local_proof *local_consumer=input->local_consumer;
  pipeline_context *context=input->context;
  VkRenderPass local_pass=input->local_pass;unsigned local_subpass=input->local_subpass;
  int reuse=input->reuse,prepare_mode=input->prepare_mode;
  uint32_t streams=0;
  cached_pipeline *cached;
  VkVertexInputBindingDescription vb[16];
  VkVertexInputBindingDivisorDescriptionEXT divisors[16];
  unsigned divisor_count = 0;
  VkVertexInputAttributeDescription va[16];
  uint32_t nb = 0, nattr = 0;
  for (int i = 0; i < 16; i++)
    if ((list[0]->input_mask & (1u << i)) && S.attr[i].fmt) {
      va_st a = S.attr[i];
      if (a.stream >= 16)
        die("Vulkan vertex stream %u", a.stream);
      VkFormat format = vk_nvn_format(a.fmt);
      VkFormatProperties prop;
      vk_format_properties(format, &prop);
      if (!(prop.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT))
        die("Vulkan vertex format unsupported: %#x", a.fmt);
      va[nattr++] =
          (VkVertexInputAttributeDescription){i, a.stream, format, a.offset};
      streams |= 1u << a.stream;
    }
  for (int i = 0; i < 16; i++)
    if (streams & (1u << i)) {
      if (!expanded[i] && S.stream[i].divisor > 1 && S.stream[i].stride) {
        if (S.stream[i].divisor > V.max_vertex_divisor)
          die("Vulkan vertex instance divisor %u exceeds device limit %u",
              S.stream[i].divisor, V.max_vertex_divisor);
        divisors[divisor_count++] = (VkVertexInputBindingDivisorDescriptionEXT){
            .binding = i, .divisor = S.stream[i].divisor};
      }
      vb[nb++] = (VkVertexInputBindingDescription){
          i, expanded[i] ? expanded_stride[i] : S.stream[i].stride,
          S.stream[i].divisor ? VK_VERTEX_INPUT_RATE_INSTANCE
                              : VK_VERTEX_INPUT_RATE_VERTEX};
    }
  VkPipelineVertexInputDivisorStateCreateInfoEXT divisor_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT,
      .vertexBindingDivisorCount = divisor_count,
      .pVertexBindingDivisors = divisors};
  VkPipelineVertexInputStateCreateInfo vi = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .pNext = divisor_count ? &divisor_info : NULL,
      .vertexBindingDescriptionCount = nb,
      .pVertexBindingDescriptions = vb,
      .vertexAttributeDescriptionCount = nattr,
      .pVertexAttributeDescriptions = va};
  VkPipelineInputAssemblyStateCreateInfo ia = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = topology(idx ? idx->prim : draw->prim)};
  VkPipelineViewportStateCreateInfo vp = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .scissorCount = 1};
  VkPolygonMode mode = S.poly.mode == 2   ? VK_POLYGON_MODE_FILL
                       : S.poly.mode == 1 ? VK_POLYGON_MODE_LINE
                                          : VK_POLYGON_MODE_POINT;
  if(list[1] && list[1]->quad_transfer) {
    const char *error=vk_shader_quad_transfer_error(list[1],samples,ia.topology,mode);
    if(error)die("Vulkan quad transfer contract: %s (%s)",error,list[1]->key);
  }
  if (mode != VK_POLYGON_MODE_FILL && !V.features.fillModeNonSolid)
    die("Vulkan non-solid polygon unsupported");
  VkPipelineRasterizationStateCreateInfo raster = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode = mode,
      .cullMode = S.poly.cull == 1   ? VK_CULL_MODE_FRONT_BIT
                  : S.poly.cull == 2 ? VK_CULL_MODE_BACK_BIT
                  : S.poly.cull == 3 ? VK_CULL_MODE_FRONT_AND_BACK
                                     : VK_CULL_MODE_NONE,
      .frontFace = S.poly.front ? VK_FRONT_FACE_COUNTER_CLOCKWISE
                                : VK_FRONT_FACE_CLOCKWISE,
      .depthBiasEnable = S.poly.offset_en != 0,
      .lineWidth = 1};
  VkPipelineMultisampleStateCreateInfo ms = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = samples,
      .alphaToCoverageEnable = S.ms.a2c != 0};
  VkPipelineColorBlendAttachmentState blends[8];
  for (int i = 0; i < S.targets.n; i++) {
    blend_st b = S.blend[i];
    blends[i] = (VkPipelineColorBlendAttachmentState){
        .blendEnable = (S.color.blend_en >> i) & 1,
        .srcColorBlendFactor =
            b.src_rgb ? blend_factor(b.src_rgb) : VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor =
            b.dst_rgb ? blend_factor(b.dst_rgb) : VK_BLEND_FACTOR_ZERO,
        .colorBlendOp = b.eq_rgb ? b.eq_rgb - 1 : VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor =
            b.src_a ? blend_factor(b.src_a) : VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor =
            b.dst_a ? blend_factor(b.dst_a) : VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = b.eq_a ? b.eq_a - 1 : VK_BLEND_OP_ADD,
        .colorWriteMask = (S.chmask.mask >> (i * 4)) & 15};
  }
  if (S.color.logic_op != NVN_LOGIC_OP_COPY && !V.features.logicOp)
    die("Vulkan logic op unsupported");
  VkPipelineColorBlendStateCreateInfo blend = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable = S.color.logic_op != NVN_LOGIC_OP_COPY,
      .logicOp = S.color.logic_op,
      .attachmentCount = S.targets.n,
      .pAttachments = blends};
  memcpy(blend.blendConstants, S.blend_color, 16);
  const VkDynamicState dynamic[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                    VK_DYNAMIC_STATE_SCISSOR,
                                    VK_DYNAMIC_STATE_DEPTH_BIAS};
  VkPipelineDynamicStateCreateInfo dyn = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = 3,
      .pDynamicStates = dynamic};
  VkGraphicsPipelineCreateInfo ci = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pVertexInputState = &vi,
      .pInputAssemblyState = &ia,
      .pViewportState = &vp,
      .pRasterizationState = &raster,
      .pMultisampleState = &ms,
      .pDepthStencilState = ds,
      .pColorBlendState = &blend,
      .pDynamicState = &dyn,
      .renderPass = j->pass,.subpass=local_subpass};
  pipeline_key key;
  memset(&key, 0, sizeof key);
  key.specialization_word=specialization_words[0];key.specialization_mask=specialization_words[1];
  key.dynamic_uniforms=dynamic_uniforms;
  for (int i = 0; i < 5; i++)
    key.modules[i] = i==1?fragment_module:list[i] ? list[i]->module : VK_NULL_HANDLE;
  for (unsigned i = 0; i < na; i++)
    key.formats[i] = descriptions[i].format;
  memcpy(key.colors, color, S.targets.n * sizeof *color);
  key.depth = pipeline_depth;
  memcpy(key.streams, vb, nb * sizeof *vb);
  // stepRate 是管线状态；漏掉它会把 divisor=3 的绘制复用成 divisor=2。
  // stepRate is pipeline state: omitting it would reuse divisor=2 for a
  // divisor=3 draw.
  for (unsigned i = 0; i < divisor_count; ++i)
    key.divisors[divisors[i].binding] = divisors[i].divisor;
  memcpy(key.attributes, va, nattr * sizeof *va);
  pipeline_raster_key(&key.raster,&raster);
  pipeline_multisample_key(&key.multisample,&ms);
  pipeline_depth_key(&key.depth_stencil,ds);
  memcpy(key.blends, blends, S.targets.n * sizeof *blends);
  memcpy(key.blend_constants, S.blend_color, sizeof key.blend_constants);
  key.targets = S.targets.n;
  key.attachments = na;
  key.streams_count = nb;
  key.attributes_count = nattr;
  key.topology = ia.topology;
  key.has_depth = physical_depth;
  key.logic_enable = blend.logicOpEnable;
  key.logic_op = blend.logicOp;
  if(reuse&&prepare_mode==2) {
    draw_recorded.prepare_checks++;
    if(memcmp(&key,&prepared_pipeline->key,sizeof key)) {
      unsigned logged=0;
      for(size_t i=0;i<sizeof key&&logged<12;i++) if(((const uint8_t *)&key)[i]!=((const uint8_t *)&prepared_pipeline->key)[i]) {
        LOG("[vulkan pipeline proof] byte=%zu current=%u cached=%u epoch=%llu",i,
          ((const uint8_t *)&key)[i],((const uint8_t *)&prepared_pipeline->key)[i],(unsigned long long)context->epoch);logged++;
      }
      die("Vulkan pipeline preparation proof mismatch");
    }
  }
  draw_recorded.prepare_lookups++;
  int legacy=atomic_load_explicit(&pipeline_byte_hash,memory_order_relaxed);
  for(;;) {
  unsigned bucket=pipeline_bucket(&key,legacy);
  for (cached = pipelines[legacy][bucket]; cached; cached = cached->next[legacy])
    if (cached->local_pass==local_pass && cached->subpass==local_subpass && !memcmp(&key, &cached->key, sizeof key))
      break;
  if (!cached) {
    cached = calloc(1, sizeof *cached);
    if (!cached)
      die("Vulkan pipeline cache allocation");
    cached->key = key;cached->local_pass=local_pass;cached->subpass=local_subpass;
    if(local_pass) {
      cached->local_object=j->pass_object;
      pthread_mutex_lock(&render_object_mu);cached->local_object->references++;pthread_mutex_unlock(&render_object_mu);
    }
    cached->layout = vk_program_layout_mode(list,dynamic_uniforms);
    ci.layout = cached->layout;
    // Stage create-info is consumed only by the actual driver MISS. Keep all
    // per-draw resource work above; cached pipelines need no Vulkan creation
    // representation. Rebuild from the current module/tuple on each retry.
    VkPipelineShaderStageCreateInfo stages[5];
    uint32_t ns=0;
    for(unsigned i=0;i<5;i++) if(list[i])
      stages[ns++]=(VkPipelineShaderStageCreateInfo){
        .sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage=vk_stage(i),.module=i==1?fragment_module:list[i]->module,
        .pSpecializationInfo=i==1 && original_specialized?specialization_info:NULL,
        .pName="main"};
    ci.stageCount=ns;ci.pStages=stages;
    int compile_trace=vk_system_trace_begin("Vulkan graphics PSO creation");
    vk_pipeline_cache_begin();
    VkResult compiled=vkCreateGraphicsPipelines(V.device,V.pipeline_cache,1,&ci,NULL,&cached->pipeline);
    vk_pipeline_cache_end(compiled==VK_SUCCESS);
    vk_system_trace_end(compile_trace);
    if(compiled!=VK_SUCCESS && j->local && !local_consumer &&
        j->local->shared_first_depth && !j->local->subpass &&
        ((!j->secondary && !active_job) || active_job==j)) {
      // This current producer draw was not issued. A fresh job has no secondary
      // to end; a previously recorded one must STORE its earlier real draws.
      // Neither path replays those draws or consumes a future clear command.
      if(cached->pipeline)vkDestroyPipeline(V.device,cached->pipeline,NULL);
      render_pass_release(cached->local_object);free(cached);
      vk_texture_shader_abandon();vk_shader_operation_abandon();
      if(!j->secondary) {
        // Only release this job's references, not shared cached/in-flight FB/RP.
        if(graphics_state->jobs!=j)die("Vulkan unrecorded pixel-local job order");
        graphics_state->jobs=j->next;
        framebuffer_release(j->framebuffer_object);render_pass_release(j->pass_object);
        vk_texture_raster_free(j->raster_group);
        free(j->local);free(j);draw_recorded.pixel_local_plans--;
      } else vk_render_end();
      *retry=1;return NULL;
    }
    if(compiled!=VK_SUCCESS && local_consumer) {
      if(cached->pipeline)vkDestroyPipeline(V.device,cached->pipeline,NULL);
      render_pass_release(cached->local_object);free(cached);
      vk_texture_shader_abandon();vk_shader_operation_abandon();vk_render_end();
      *retry=2;return NULL;
    }
    if(compiled==VK_ERROR_UNKNOWN && list[1] &&
        (fragment_module==list[1]->specialization_module || hybrid_selected)) {
      // Optional hybrid -> original exact-word specialization -> base. Each
      // retry updates the complete module/tuple key; no failed PSO is cached.
      if(cached->pipeline)vkDestroyPipeline(V.device,cached->pipeline,NULL);
      render_pass_release(cached->local_object);free(cached);cached=NULL;
      if(hybrid_selected) {
        fragment_module=vk_uniform_predicate_rejected(list[1],specialization_words);
        hybrid_selected=0;specialization_info->mapEntryCount=1;specialization_info->dataSize=4;
        LOG("[vulkan Uniform predicate] driver rejected variant for %s; retain original specialization",list[1]->key);
      } else {
        list[1]->specialization_disabled=1;fragment_module=list[1]->module;
        specialization_words[0]=specialization_words[1]=0;
        LOG("[vulkan Uniform specialization] driver rejected variant for %s; retain original module",list[1]->key);
      }
      context->fragment_module=fragment_module;context->specialization_word=specialization_words[0];
      context->specialization_mask=specialization_words[1];
      key.modules[1]=fragment_module;key.specialization_word=specialization_words[0];key.specialization_mask=specialization_words[1];
      original_specialized=fragment_module==list[1]->specialization_module;
      continue;
    }
    VK_CHECK(compiled);
    // 两种查找共享已编译管线；切换对照开关不能引入重编译或改变资源寿命。
    // Both indexes share compiled pipelines: A/B switching cannot recompile
    // a previously cached pipeline or change resource lifetime.
    for(int mode=0;mode<2;mode++) {
      unsigned b=mode==legacy?bucket:pipeline_bucket(&key,mode);
      cached->next[mode]=pipelines[mode][b];pipelines[mode][b]=cached;
    }
    pipeline_count++;
    if(!local_pass)pipeline_recipe_record(&key,list);
  }
  break;
  }
  prepared_pipeline=cached;prepared_context=*context;prepared_streams=streams;
  *out_streams=streams;return cached;
}
// The attribute/divisor predicate remains in the draw. Positive expansion
// owns all original CPU visibility, waits, allocation and per-instance reads.
// Output arrays are the caller's existing arrays, with no packet/S snapshot.
static VK_COLD_PHASE int draw_expand_instances(vk_shader *const list[6],
    const p_draw_idx *idx,uint32_t expansion,vk_buffer *expanded[16],
    uint32_t expanded_stride[16],int ghost_cpu_guard) {
    uint64_t count = idx ? idx->instances : 1, base = idx ? idx->base_instance : 0;
    int pending=0;
    // CPU-authored instance attributes need no GPU drain. Prove every source
    // range before allocating expanded temporaries: vk_pool_read can otherwise
    // submit the current resource owner while another stream still uses it.
    for(unsigned i=0;i<16;i++) if((expansion&(1u<<i))&&count) {
      uint64_t stride=S.stream[i].stride,span=expanded_stride[i];
      uint64_t last=(count-1)/S.stream[i].divisor;
      if(base>UINT64_MAX-last || base+last>UINT64_MAX/stride)
        die("Vulkan instance source overflow");
      uint64_t start=base*stride,end=(base+last)*stride;
      if(end>S.vertex[i].size || span>S.vertex[i].size-end ||
         S.vertex[i].address>UINT64_MAX-start)
        die("Vulkan instance source range invalid");
      // Restore persistent deferred bytes before allocating any expanded temp.
      if(ghost_cpu_guard && vk_ghost_replaying()) {
        if(vk_pool_pending_writes(S.vertex[i].address+start,end-start+span) ||
            vk_pool_cpu_unknown(S.vertex[i].address+start,end-start+span)) {
          return 0;
        }
      } else if(V.options.pool_defer_publication)
        vk_pool_cpu_prepare(S.vertex[i].address+start,end-start+span);
      pending|=vk_pool_pending_writes(S.vertex[i].address+start,end-start+span);
    }
    if(pending) {
      if(ghost_cpu_guard && vk_ghost_replaying()) {
        return 0;
      }
      // Actual GPU-produced inputs retain the proven synchronous fallback.
      // InstanceIndex/BaseInstance and CPU publication semantics are unchanged.
      int wait_trace=vk_system_trace_begin("Vulkan vertex expansion waits GPU");
      uint64_t fence=mtl_last_fence();
      vk_internal_wait(vk_internal_commit());mtl_sub_begin();
      vk_system_trace_end(wait_trace);
      if(mtl_last_fence()!=fence) die("Internal vertex expansion advanced guest fence");
    }
    for (unsigned i = 0; i < 16; ++i) if (expansion & (1u << i)) {
      uint32_t stride = (expanded_stride[i] + 3) & ~3u;
      uint64_t elements = base + (count ? count : 1);
      if (elements > UINT64_MAX / stride) die("Vulkan instance expansion overflow");
      expanded_stride[i] = stride;
      expanded[i] = vk_temp_buffer(elements * stride);
      vk_buffer *source;
      uint64_t offset;
      if (!S.vertex[i].address ||
          !vk_pool_address_stage(S.vertex[i].address,S.vertex[i].size,(void **)&source,&offset,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT))
        die("Vulkan instance source outside pool");
      // 只复制实际用到的属性跨度，既支持紧密的字节属性，也不越过最后一项。
      // Copy the actual attribute span, supporting packed byte attributes
      // without reading beyond the final source element.
      unsigned span = 0;
      for (unsigned a = 0; a < 16; ++a)
        if ((list[0]->input_mask & (1u << a)) && S.attr[a].fmt && S.attr[a].stream == i) {
          unsigned end = S.attr[a].offset + nvn_fmt_bpb(S.attr[a].fmt, NULL, NULL);
          if (end > span) span = end;
        }
      for (uint64_t instance = 0; instance < count; ++instance) {
        uint64_t element = base + instance / S.stream[i].divisor;
        if (element > UINT64_MAX / S.stream[i].stride)
          die("Vulkan instance source overflow");
        uint64_t at = element * S.stream[i].stride;
        if (at > S.vertex[i].size || span > S.vertex[i].size - at)
          die("Vulkan instance source range invalid");
        if(ghost_cpu_guard && vk_ghost_replaying()) {
          if(vk_pool_try_read(S.vertex[i].address+at,span,
                              expanded[i]->map+(base+instance)*stride)!=1) {
            return 0;
          }
        } else if(vk_pool_read(S.vertex[i].address+at,span,
                               expanded[i]->map+(base+instance)*stride)<0)
          die("Vulkan instance source CPU read failed");
      }
    }
  return 1;
}
static void draw_vertex_binding_require(const vk_binding *binding,int *writable_buffers) {
    int kind=binding->kind,reg=binding->reg;
    if(kind==7||kind==9) {
      // Buffer textures consume the same pool bytes as UBO/SSBO bindings.
      // A readonly view does not depend on unrelated fragment/image writes.
      // Keep unknown descriptors and writable views on the full dependency.
      tex_t texture;
      int limit=kind==9?32:128;
      if(reg<0||reg>=limit || vk_binding_writable(binding)) *writable_buffers=1;
      else {
        uint64_t handle=kind==9?S.img[0][reg]:S.tex[0][reg];
        if(!vk_texture_descriptor(handle&0xfffff,&texture) ||
           texture.d.target!=NVN_TEXTURE_TARGET_BUFFER ||
           !texture.mem || !texture.L.size)
          *writable_buffers=1;
        else vk_buffer_vertex_require((uintptr_t)texture.mem,texture.L.size);
      }
    }
    const p_bind_buf *buffer=NULL;
    if(kind==1 && reg>=3 && reg<19) buffer=&S.ubo[0][reg-3];
    if((kind==2||kind==8) && reg>=0 && reg<16)
      buffer=kind==8?&S.ubo[0][reg]:&S.ssbo[0][reg];
    if(buffer && buffer->addr && buffer->size)
      vk_buffer_vertex_require(buffer->addr,buffer->size);
}
static void vk_draw_record_impl(const p_draw *draw,const p_draw_idx *idx,int disable_local_plan);
static void vk_draw_record(const p_draw *draw,const p_draw_idx *idx,int disable_local_plan) {
  vk_texture_raster_draw_enter();
  vk_image_split_draw_begin();
  vk_ubo_plan_draw_enter();
  vk_texture_lookup_scope_begin();int source_operation=vk_shader_image_sources_begin();
  vk_draw_record_impl(draw,idx,disable_local_plan);
  vk_shader_image_sources_end(source_operation);vk_texture_lookup_scope_end();
  vk_image_split_draw_end();
  vk_ubo_plan_draw_leave();
  vk_texture_raster_draw_leave();
}
static void vk_draw_record_impl(const p_draw *draw,const p_draw_idx *idx,int disable_local_plan) {
  // A state change outside the complete planned sequence cannot enable
  // format-dependent bias inside a synthetic no-DS producer pass.
  if(active_job && active_job->local && !active_job->local->subpass &&
      active_job->local->shared_first_depth && S.poly.offset_en)vk_render_end();
  uint64_t started = vk_profile_start();
  int quads=draw&&draw->prim==NVN_DRAW_PRIMITIVE_QUADS;
  if (idx&&idx->prim==NVN_DRAW_PRIMITIVE_QUADS)
    die("Vulkan indexed quads unsupported in original Metal backend");
  if (quads&&(draw->count<0||draw->first<0||draw->count/4>65536))
    die("Vulkan quads exceed original shared-index range");
  if (!S.program)
    die("Vulkan draw without program");
  vk_shader *list[6] = {0};
  vk_program_shaders(S.program, list);
  int ghost_cpu_guard=V.options.pool_defer_publication;
#ifdef DM_VULKAN_SHARED_POOLS
  ghost_cpu_guard|=vk_legacy_astc_copy_mode();
#endif
  int cpu_preflight=ghost_cpu_guard;
#ifdef DM_VULKAN_SHARED_POOLS
  cpu_preflight|=vk_pool_shared_active();
#endif
  if(cpu_preflight && vk_ghost_replaying() &&
      !vk_ghost_cpu_draw_ready(list,&S,draw,idx))return;
  mip_clear_before_draw(list);
  list[5] = NULL;
  if(!list[0])die("Vulkan draw without vertex shader");
  int zero_checked=0;
  // Preserve the original proof that this draw consumes no resources. Run it
  // before allocation when no optional physical representation must be picked.
  // With an alternative retain the original selected-module proof below.
  if(V.options.pool_window_bytes && (!list[1] || !list[1]->texel_storage_alternative)) {
    if(vk_zero_draw_can_skip(list,draw?(const void *)draw:(const void *)idx))return;
    zero_checked=1;
  }
  // A saved optional ghost draw was checked without waiting at its entry;
  // it must never demand/commit inside an outer draw's borrowed resources.
  if(cpu_preflight && !vk_ghost_replaying())vk_shader_cpu_sources(list,&S,0);
  draw_pool_plan pool_plan={idx};
  int pool_operation=vk_pool_draw_operation_begin(draw_pool_collect,&pool_plan);
  vk_texel_storage_selection texel_selection;
  if(list[1] && list[1]->texel_storage_alternative) {
    uint32_t storage_descriptors=0;
    for(unsigned st=0;st<5;st++)if(list[st])storage_descriptors+=list[st]->storage_descriptor_count;
    uint32_t storage_budget=storage_descriptors<V.props.limits.maxDescriptorSetStorageBuffers?
        V.props.limits.maxDescriptorSetStorageBuffers-storage_descriptors:0;
    list[1]=vk_shader_texel_pick(list[1],storage_budget,&texel_selection);
  }
  else { texel_selection.shader=NULL;texel_selection.count=0; }
  if(list[1] && list[1]->quad_readonly_sources) {
    const char *error=vk_shader_quad_readonly_stages_error(list);
    if(error)die("Vulkan readonly quad source contract: %s (%s)",error,list[1]->key);
  }
  int dynamic_uniforms=vk_program_dynamic_uniforms(list);
  // Each active stage fills count and exactly its live offsets below. Absent
  // stages have no ordered descriptor set and never read this local slot.
  vk_uniform_offsets uniform_offsets[6];
  if (!list[0])
    die("Vulkan draw without vertex shader");
  VkShaderModule fragment_module=no_fxaa_fragment(list[1]);
  uint32_t specialization_words[2]={0};
  if(list[1] && list[1]->specialization_module && fragment_module==list[1]->module) {
    // Earlier producers are covered by try_read. Current graphics writes are
    // not recorded yet and could alias the selector. Immutable shader flags
    // keep this guard independent of descriptor counts and buffer contents.
    int writer=0;
    for(unsigned st=0;st<5;st++)if(list[st] && list[st]->has_resource_writes)writer=1;
    if(!writer)fragment_module=vk_uniform_hybrid_pick(list[1],specialization_words);
  }
  VkSpecializationMapEntry specialization_entries[2]={
    {.constantID=list[1]?list[1]->specialization_id:0,.offset=0,.size=4},
    {.constantID=list[1]&&list[1]->predicate?list[1]->predicate->id:0,.offset=4,.size=4}};
  int hybrid_selected=list[1] && list[1]->predicate && fragment_module==list[1]->predicate->module;
  VkSpecializationInfo specialization_info={.mapEntryCount=hybrid_selected?2:1,.pMapEntries=specialization_entries,
    .dataSize=hybrid_selected?8:4,.pData=specialization_words};
  if(!zero_checked && vk_zero_draw_can_skip(list,draw?(const void *)draw:(const void *)idx)) {
    vk_pool_operation_end(pool_operation);return;
  }
  if(bq_before_draw(list,draw,idx)) {vk_pool_operation_end(pool_operation);return;}
  if(list[1] && list[1]->quad_readonly_sources && fragment_module!=list[1]->module &&
      fragment_module!=list[1]->specialization_module &&
      (!list[1]->predicate || fragment_module!=list[1]->predicate->module))
    die("Vulkan readonly quad sources require a verified original/specialized module");
  int original_specialized=list[1] && (fragment_module==list[1]->specialization_module || hybrid_selected);
  const vk_pixel_local_proof *local_consumer=pl_consumer(list[0],list[1],fragment_module,draw,idx);
  render_job *local_job=local_consumer?active_job:NULL;
  uint64_t local_recording=local_consumer?vk_recording_serial():0,local_pass_number=V.passes;
  VkImageView local_views[8];VkImageLayout local_layouts[8];
  if(local_consumer) {
    memcpy(local_views,local_job->local->input_views,sizeof local_views);
    memcpy(local_layouts,local_job->local->input_layouts,sizeof local_layouts);
  }
  if(local_consumer) {fragment_module=local_consumer->module;list[1]=local_consumer->shader;}
  pl_job *local_ready=NULL;
  if(vk_pixel_local_enabled() && !disable_local_plan && !local_consumer) {
    if(active_job && !target_fields_equal(&active_targets,&S.targets))vk_render_end();
    if(!active_job && S.targets.n>0 && S.targets.n<=8 && S.targets.color[0]) {
      const tex_t *t=S.targets.color[0];
      local_ready=pl_prepare(draw,idx,t->d.width,t->d.height);
    }
  }
  float cover_p[64][4];int cover_ready=cover_positions(list[0],list[1],draw,idx,cover_p);
  vk_buffer *expanded[16] = {0};
  uint32_t expanded_stride[16] = {0}, expansion = 0;
  int force=atomic_load(&vertex_divisor_fallback);
  if(force<0) force=V.options.vertex_divisor_fallback;
  for (unsigned i = 0; i < 16; ++i)
    if ((list[0]->input_mask & (1u << i)) && S.attr[i].fmt) {
      unsigned stream = S.attr[i].stream;
      if (stream >= 16) die("Vulkan vertex stream %u", stream);
      if (S.stream[stream].stride && S.stream[stream].divisor > 1 &&
          (force ||
           S.stream[stream].divisor > V.max_vertex_divisor)) {
        int bw, bh, bytes = nvn_fmt_bpb(S.attr[i].fmt, &bw, &bh);
        uint64_t span = (uint64_t)S.attr[i].offset + bytes;
        if (bytes <= 0 || bw != 1 || bh != 1 ||
            span > V.props.limits.maxVertexInputBindingStride)
          die("Vulkan instance attribute span invalid");
        expansion |= 1u << stream;
        if (span > expanded_stride[stream]) expanded_stride[stream] = span;
      }
    }
  if (expansion && !draw_expand_instances(list,idx,expansion,expanded,expanded_stride,ghost_cpu_guard)) {
    free(local_ready);vk_pool_operation_end(pool_operation);return;
  }
  if (vk_trace_frame_active()) {
    LOG("[vulkan trace] frame=%llu draw=%llu vs=%s ps=%s count=%u first=%u "
        "viewport=%.0f,%.0f,%.0f,%.0f cull=%u front=%u depth=%u/%u",
        (unsigned long long)V.frame, (unsigned long long)V.draws, list[0]->key,
        list[1] ? list[1]->key : "none", idx ? idx->count : draw->count,
        idx ? 0 : draw->first, S.viewport[0][0], S.viewport[0][1],
        S.viewport[0][2], S.viewport[0][3], S.poly.cull, S.poly.front, S.ds.dt,
        S.ds.dw);
  }
  if (list[2] || list[3] || list[4])
    die("Vulkan geometry/tessellation graphics setup pending");
  int attachment_retry=0;
attachment_prepare:
  ; // A cold retry starts after one-time shader/bloom selection and expansion.
  int writable_buffers=0;
  // Immutable binding kinds/write flags were classified with the final
  // shader schema. Keep the dynamic VS descriptor checks below unchanged.
  for(unsigned stage=0;stage<5;stage++)
    if(list[stage] && list[stage]->has_writable_buffers) writable_buffers=1;
  if(vk_ubo_plan_active()) {
    int uniform_plan_ok=active_job && !active_job->local && !local_ready && !local_consumer &&
        !vk_ghost_replaying() && !writable_buffers && !vk_mip_clear_pending();
    for(unsigned st=0;st<5;st++)if(list[st] && list[st]->has_resource_writes)uniform_plan_ok=0;
    if(list[1] && (list[1]->quad_readonly_sources || list[1]->texel_storage_alternative))uniform_plan_ok=0;
    vk_ubo_plan_admit(list,dynamic_uniforms,uniform_plan_ok);
  }
  uint64_t uniform_vs_skipped[2]={0,0};
  vk_ubo_plan_vertex_mask(list[0],uniform_vs_skipped);
  uint64_t uniform_vs_remaining[2]={
      (list[0]->nb>=64?UINT64_MAX:(UINT64_C(1)<<list[0]->nb)-1)&~uniform_vs_skipped[0],
      (list[0]->nb<=64?0:list[0]->nb==128?UINT64_MAX:(UINT64_C(1)<<(list[0]->nb-64))-1)&~uniform_vs_skipped[1]};
  int uniform_vs_sparse=!!(uniform_vs_skipped[0]|uniform_vs_skipped[1]);
uniform_vertex_remaining:
  if(!uniform_vs_sparse) {
    for(unsigned n=0;n<list[0]->nb;n++)draw_vertex_binding_require(&list[0]->bindings[n],&writable_buffers);
  } else {
    for(unsigned word=0;word<2;word++)while(uniform_vs_remaining[word]) {
      unsigned n=word*64+__builtin_ctzll(uniform_vs_remaining[word]);
      uniform_vs_remaining[word]&=uniform_vs_remaining[word]-1;
      draw_vertex_binding_require(&list[0]->bindings[n],&writable_buffers);
    }
  }
  if(vk_ubo_plan_vertex_retry(uniform_vs_skipped)) {
    uniform_vs_remaining[0]=uniform_vs_skipped[0];uniform_vs_remaining[1]=uniform_vs_skipped[1];
    uniform_vs_skipped[0]=uniform_vs_skipped[1]=0;uniform_vs_sparse=1;
    goto uniform_vertex_remaining;
  }
  vk_ubo_plan_vertex_done();
  // Consecutive attributes often share one complete VBO range. Only reuse
  // the immediately preceding require in this draw; a different range must
  // still run, even if an earlier stream already named the same bytes.
  uint64_t previous_vertex_address=0,previous_vertex_size=0;
  int previous_vertex_valid=0;
  for(unsigned a=0;a<16;a++)
    if((list[0]->input_mask&(1u<<a))&&S.attr[a].fmt) {
      unsigned stream=S.attr[a].stream;
      if(stream>=16) {previous_vertex_valid=0;continue;}
      uint64_t address=S.vertex[stream].address,size=S.vertex[stream].size;
      if(previous_vertex_valid && address==previous_vertex_address && size==previous_vertex_size)continue;
      VkCommandBuffer command=V.cmd;
      uint64_t serial=vk_recording_serial(),pass=V.passes,epoch=V.buffer_epoch;
      uint64_t state=vk_pipeline_state_epoch();
      vk_buffer_vertex_require(address,size);
      previous_vertex_address=address;previous_vertex_size=size;
      // Require re-queries writers after pass-end/ghost callbacks. Still do
      // not cache a call that changed its recording or state: let the next
      // identical attribute conservatively re-enter the ordinary closed loop.
      previous_vertex_valid=command==V.cmd && serial==vk_recording_serial() && pass==V.passes &&
          epoch==V.buffer_epoch && state==vk_pipeline_state_epoch();
    }
  // Empty indexed draws bind a valid address but fetch no index bytes.
  // size==0 remains the distinct unknown-extent VBO contract.
  if(idx && idx->count) vk_buffer_vertex_require(idx->ibuf,(uint64_t)idx->count*(idx->itype==1?2:4));
  VkDescriptorSet sets[6] = {0};
  if(S.targets.n<0 || S.targets.n>8) die("Vulkan color target count");
  for(int k=0;k<S.targets.n;k++) if(S.targets.color[k])
    vk_texture_prepare_attachment(S.targets.color[k],S.targets.has_cview[k]?&S.targets.cview[k]:NULL);
  uint64_t cover_descriptor_epoch=cover_ready?vk_texture_descriptor_epoch():0;
  int split_ok=active_job && !active_job->local && !local_ready && !local_consumer && !vk_ghost_replaying() && !writable_buffers;
  for(unsigned st=0;st<5;st++)if(list[st] && list[st]->has_resource_writes)split_ok=0;
  if(list[1] && (list[1]->quad_readonly_sources || list[1]->texel_storage_alternative))split_ok=0;
  vk_image_split_admit(list[1],split_ok);
  vk_texture_binding_scope_begin();
  vk_shader_prepare_images(list,0);
  if(local_consumer && (active_job!=local_job || local_recording!=vk_recording_serial() || local_pass_number!=V.passes)) {
    vk_texture_binding_scope_end();free(local_ready);
    vk_pool_operation_end(pool_operation);vk_draw_record(draw,idx,disable_local_plan);return;
  }
  vk_shader_operation_begin();
  vk_texture_shader_begin();
  for (int i = 0; i < 5; i++)
    if (list[i]) {
      if(local_consumer && (active_job!=local_job || local_recording!=vk_recording_serial() || local_pass_number!=V.passes)) {
        vk_texture_binding_scope_end();vk_texture_shader_abandon();vk_shader_operation_abandon();
        free(local_ready);vk_pool_operation_end(pool_operation);vk_draw_record(draw,idx,disable_local_plan);return;
      }
      if(i==1 && local_consumer) {
        sets[i]=vk_shader_pixel_local_bindings(local_consumer,dynamic_uniforms,&uniform_offsets[list[i]->set],
            local_views,local_layouts,texel_selection.shader?&texel_selection:NULL);
      } else sets[i] = vk_shader_bindings_selected(list[i],dynamic_uniforms,&uniform_offsets[list[i]->set],
          i==1?&texel_selection:NULL);
    }
  vk_texture_binding_scope_end();
  if (S.targets.n < 0 || S.targets.n > 8)
    die("Vulkan color target count");
  // 原 ds_scan 的整通道证明；额外策略只消费最终管线状态。清除通道保守保留。
  // Upstream ds_scan's whole-pass proof. Extra policies are consumed only as
  // final pipeline state. Clear passes conservatively retain attachments.
  unsigned need_depth = vk_pass_depth_need();
  // Use the final pipeline state for attachment access decisions as well as
  // pipeline creation. A read-only stencil test must not become a GPU writer;
  // optional upstream overrides still take effect before this classification.
  VkPipelineDepthStencilStateCreateInfo ds = {
      .sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable=S.ds.dt,.depthWriteEnable=S.ds.dw,
      .depthCompareOp=S.ds.df?compare(S.ds.df):VK_COMPARE_OP_ALWAYS,
      .stencilTestEnable=S.ds.st};
  if(S.ds.st) {
    ds.front=(VkStencilOpState){stencil(S.ds.fail_f),stencil(S.ds.zpass_f),
        stencil(S.ds.zfail_f),compare(S.ds.sf_f),S.stencil_value[0],S.stencil_mask[0],S.stencil_ref[0]};
    ds.back=(VkStencilOpState){stencil(S.ds.fail_b),stencil(S.ds.zpass_b),
        stencil(S.ds.zfail_b),compare(S.ds.sf_b),S.stencil_value[1],S.stencil_mask[1],S.stencil_ref[1]};
  }
  if(S.targets.depth) vk_runtime_apply_stencil(&ds);
  int shared_first_depth=(local_ready && local_ready->shared_first_depth) ||
      (!local_consumer && active_job && active_job->local && !active_job->local->subpass && active_job->local->shared_first_depth);
  if(shared_first_depth) {
    // Logical no-DS ignores all leftover tests/writes. A physical DS reference
    // must not turn those stale NVN flags into observable comparisons/writes.
    ds=(VkPipelineDepthStencilStateCreateInfo){
      .sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthCompareOp=VK_COMPARE_OP_ALWAYS,
      .front={.compareOp=VK_COMPARE_OP_ALWAYS},.back={.compareOp=VK_COMPARE_OP_ALWAYS}};
  }
  int stencil_writing=pipeline_stencil_writes(&ds);
  need_depth |= ds.depthTestEnable || ds.depthWriteEnable || ds.stencilTestEnable;
  int has_color = 0;
  for (int k=0;k<S.targets.n;k++) has_color |= S.targets.color[k]!=NULL;
  int include_depth = S.targets.depth && (need_depth || !has_color);
  draw_target_match target_match={.enabled=!draw_state_reference()};
  if (render_targets_equal(&S.targets,&target_match)) {
    if (include_depth && !active_job->has_depth) vk_render_end();
    else include_depth = active_job->has_depth;
  }
  if(local_consumer && (active_job!=local_job || local_recording!=vk_recording_serial() ||
      local_pass_number!=V.passes || (ds.depthTestEnable && ds.depthWriteEnable) || stencil_writing || !pl_enter(local_job))) {
    // No consumer command has been emitted. Abandon the descriptor batch and
    // redo the ordinary draw, preserving already-executed producer commands.
    vk_texture_shader_abandon();vk_shader_operation_abandon();vk_render_end();
    free(local_ready);vk_pool_operation_end(pool_operation);vk_draw_record(draw,idx,disable_local_plan);return;
  }
  VkAttachmentDescription descriptions_storage[9],*descriptions=descriptions_storage;
  VkImageView views_storage[9],*views_=views_storage;
  VkAttachmentReference color_storage[8],*color=color_storage, depth = {0};
  uint32_t na = 0, width = UINT32_MAX, height = UINT32_MAX;
  VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
  render_job *attachment_job=NULL;
  if(attachment_reuse_mode() && !attachment_retry && !expansion && !local_ready && !local_consumer &&
      active_job && !active_job->local && active_job->attachment_representation_valid &&
      !vk_ghost_replaying() && !vk_mip_clear_pending() &&
      V.cmd==active_job->secondary && render_targets_equal(&S.targets,&target_match) &&
      include_depth==active_job->has_depth && active_job->framebuffer_object) {
    int role_ok=1;
    if(include_depth) {
      int readonly=readonly_depth_target(&S.targets,ds.depthWriteEnable || stencil_writing,&target_match);
      VkImageLayout layout=active_job->depth.layout;
      // A newly writable DS role must still execute the original layout transition.
      if(layout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL && !readonly)role_ok=0;
    }
    if(role_ok)attachment_job=active_job;
  }
  if(attachment_job) {
    // These arrays already belong to the live job; no second representation cache.
    descriptions=attachment_job->attachments;color=attachment_job->colors;
    views_=attachment_job->framebuffer_object->views;depth=attachment_job->depth;
    na=attachment_job->attachment_count;width=attachment_job->framebuffer_object->width;
    height=attachment_job->framebuffer_object->height;samples=descriptions[0].samples;
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
    if(attachment_test_callback) {
      void (*callback)(void *)=attachment_test_callback;void *context=attachment_test_context;
      attachment_test_callback=NULL;attachment_test_context=NULL;callback(context);
    }
#endif
    int raster_borrowed=!writable_buffers &&
        vk_texture_raster_borrow(attachment_job->raster_group,
            stencil_writing || (ds.depthTestEnable && ds.depthWriteEnable));
    if(!raster_borrowed)for(int k=0;k<=S.targets.n;k++) {
      const tex_t *t=k==S.targets.n?(include_depth?S.targets.depth:NULL):S.targets.color[k];
      const view_t *v=k==S.targets.n?(S.targets.has_dview?&S.targets.dview:NULL):
          (S.targets.has_cview[k]?&S.targets.cview[k]:NULL);
      if(!t)continue;
      if(k==S.targets.n && !stencil_writing && !(ds.depthTestEnable && ds.depthWriteEnable))
        vk_texture_attachment_read(t,v);
      else vk_texture_attachment_record(t,v,0);
    }
    if(active_job!=attachment_job || !attachment_job->attachment_representation_valid ||
        V.cmd!=attachment_job->secondary)goto attachment_cold_retry;
  } else for (int k = 0; k <= S.targets.n; k++) {
    const tex_t *t = k == S.targets.n ? (include_depth ? S.targets.depth : NULL) : S.targets.color[k];
    const view_t *v =
        k == S.targets.n
            ? (S.targets.has_dview ? &S.targets.dview : NULL)
            : (S.targets.has_cview[k] ? &S.targets.cview[k] : NULL);
    if (!t) {
      if (k < S.targets.n)
        color[k] = (VkAttachmentReference){VK_ATTACHMENT_UNUSED,
                                           VK_IMAGE_LAYOUT_GENERAL};
      continue;
    }
    vk_attachment a;
    if(local_consumer) a=k==S.targets.n?local_job->local->depth_attachment:local_job->local->second_attachments[k];
    else a=k==S.targets.n && readonly_depth_target(&S.targets,
        ds.depthWriteEnable || stencil_writing,&target_match)?
        vk_texture_readonly_attachment(t,v):vk_texture_attachment(t,v);
    if(k==S.targets.n && !stencil_writing &&
        !(ds.depthTestEnable && ds.depthWriteEnable))
      vk_texture_attachment_read(t,v);
    else vk_texture_attachment_record(t,v,0);
    if (!na)
      samples = a.samples;
    else if (samples != a.samples)
      die("Vulkan mixed attachment samples");
    views_[na] = a.view;
    if (width > a.extent.width)
      width = a.extent.width;
    if (height > a.extent.height)
      height = a.extent.height;
    descriptions[na] = (VkAttachmentDescription){
        .format = a.format,
        .samples = a.samples,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
        .initialLayout = a.layout,
        .finalLayout = a.layout};
    VkAttachmentReference ref = {na, a.layout};
    if (k == S.targets.n)
      depth = ref;
    else
      color[k] = ref;
    na++;
  }
  if (!na)
    die("Vulkan draw without render targets");
  if(local_consumer) {
    memcpy(color,local_job->local->second_colors,S.targets.n*sizeof *color);
    depth=local_job->local->second_depth;na=local_job->attachment_count;
    memcpy(descriptions,local_job->attachments,na*sizeof *descriptions);
  }
  // 查询池重置可结束通道；必须在选择通道之前，避免重新开始带一次性清除的通道。
  // Query resets can end a pass. Do them before selecting it, so a pass with
  // one-time attachment clears is never accidentally begun again.
  VkRect2D area = draw_area(width, height, idx ? idx->prim : draw->prim);
  if (active_job && (!render_targets_equal(&S.targets,&target_match) ||
      !contains(active_job->area,area))) vk_render_end();
  if(list[1] && list[1]->quad_readonly_sources) {
    const char *error=vk_texture_quad_readonly_error();
    if(error)die("Vulkan readonly quad image contract: %s (%s)",error,list[1]->key);
  }
  int precise_dependencies=vk_graphics_resource_dependencies(writable_buffers);
  if(precise_dependencies) vk_texture_graphics_barrier();
  if(attachment_job && (active_job!=attachment_job ||
      !attachment_job->attachment_representation_valid || V.cmd!=attachment_job->secondary))
    goto attachment_cold_retry;
  int pass_before_query=vk_render_active();
  vk_sample_query occlusion = {0};
  int sample_live=vk_pass_samples_live(draw?(const void *)draw:(const void *)idx);
  // A live query can reset/replace this job. Retry before reserving any slot;
  // the true branch above has no elision publication or counter side effects.
  if(attachment_job && sample_live)goto attachment_cold_retry;
  if(sample_live) {
    vk_mip_clear_drain();
    occlusion=vk_counter_draw_prepare();
  } else vk_counter_pass_end();
  if(precise_dependencies && pass_before_query && !vk_render_active())
    vk_texture_graphics_barrier();
  if(local_consumer && (active_job!=local_job || local_recording!=vk_recording_serial() || local_pass_number!=V.passes)) {
    vk_texture_shader_abandon();vk_shader_operation_abandon();free(local_ready);
    vk_pool_operation_end(pool_operation);vk_draw_record(draw,idx,disable_local_plan);return;
  }
  render_job *j = active_job;
  if (!j || !render_targets_equal(&S.targets,&target_match) ||
      !contains(j->area, area)) {
    vk_render_end();
    j = calloc(1, sizeof *j);
    if (!j)
      die("Vulkan render allocation");
    j->area = area;
    j->has_depth = include_depth;
    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = S.targets.n,
        .pColorAttachments = color,
        .pDepthStencilAttachment = include_depth ? &depth : NULL};
    VkRenderPassCreateInfo rp = {.sType =
                                     VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                                 .attachmentCount = na,
                                 .pAttachments = descriptions,
                                 .subpassCount = 1,
                                 .pSubpasses = &subpass};
    if(!pl_job_create(j,local_ready,descriptions,views_,color,include_depth?&depth:NULL,&na,width,height)) {
      j->pass_object=render_pass_acquire(&rp);j->pass=j->pass_object->handle;
    }
    local_ready=NULL;
    VkFramebufferCreateInfo fb = {.sType =
                                      VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                                  .renderPass = j->pass,
                                  .attachmentCount = na,
                                  .pAttachments = views_,
                                  .width = width,
                                  .height = height,
                                  .layers = 1};
    j->framebuffer_object=framebuffer_acquire(j->pass_object,&fb);
    j->framebuffer=j->framebuffer_object->handle;
    j->next = graphics_state->jobs;
    graphics_state->jobs = j;
  }
  free(local_ready);local_ready=NULL;
  int physical_depth=include_depth;
  VkAttachmentReference pipeline_depth=depth;
  if(j->local && !j->local->subpass && j->local->shared_first_depth) {
    physical_depth=1;pipeline_depth=j->local->second_depth;
    na=j->attachment_count;memcpy(descriptions,j->attachments,na*sizeof *descriptions);
  }
  uint32_t effective_expansion=0;
  for(unsigned i=0;i<16;i++) if(expanded[i]) effective_expansion|=1u<<i;
  pipeline_context context={.epoch=vk_pipeline_state_epoch(),.program_serial=S.program->serial,
    .fragment_module=fragment_module,.specialization_word=specialization_words[0],
    .specialization_mask=specialization_words[1],.dynamic_uniforms=dynamic_uniforms,
    .targets=S.targets.n,.attachments=na,.has_depth=physical_depth,.samples=samples,
    .topology=topology(idx?idx->prim:draw->prim),.expansion=effective_expansion,.depth=pipeline_depth};
  pipeline_depth_key(&context.ds,&ds);
  for(unsigned i=0;i<na;i++) context.formats[i]=descriptions[i].format;
  memcpy(context.colors,color,S.targets.n*sizeof *color);
  int prepare_mode=active_pipeline_prepare_mode;
  VkRenderPass local_pass=j->local?j->pass:VK_NULL_HANDLE;
  unsigned local_subpass=j->local?j->local->subpass:0;
  int reuse=prepared_pipeline && prepared_pipeline->local_pass==local_pass &&
      prepared_pipeline->subpass==local_subpass && !memcmp(&prepared_context,&context,sizeof context);
  cached_pipeline *cached=prepared_pipeline;
  uint32_t streams=prepared_streams;
  // 状态代号与实际附件/模板角色未变时，直接复用已解析管线，不再组键或哈希。
  // Reuse the resolved pipeline when the state epoch and effective attachment/
  // stencil context match: no pipeline-key construction or hash on this path.
  if(reuse&&prepare_mode!=1) draw_recorded.prepare_reuses++;
  if(!reuse||prepare_mode) {
  pipeline_prepare_input preparation={.list=list,.draw=draw,.idx=idx,
    .expanded=expanded,.expanded_stride=expanded_stride,.dynamic_uniforms=dynamic_uniforms,
    .physical_depth=physical_depth,.original_specialized=original_specialized,.hybrid_selected=hybrid_selected,
    .na=na,.samples=samples,.ds=&ds,.descriptions=descriptions,.color=color,.pipeline_depth=pipeline_depth,
    .fragment_module=fragment_module,.specialization_words=specialization_words,.specialization_info=&specialization_info,
    .job=j,.local_consumer=local_consumer,.context=&context,.local_pass=local_pass,.local_subpass=local_subpass,
    .reuse=reuse,.prepare_mode=prepare_mode};
  int retry=0;
  cached=draw_pipeline_prepare(&preparation,&streams,&retry);
  if(!cached) {
    // The failed unissued draw has already abandoned its shader/job effects.
    // End THIS operation before the original recursive replay entry.
    vk_pool_operation_end(pool_operation);
    vk_draw_record(draw,idx,retry==1?1:disable_local_plan);return;
  }
  }
  j->pipeline = cached->pipeline;
  if(cached->warm_origin==1){cached->warm_origin=2;vk_prewarm_note_hit();}
  j->layout = cached->layout;
  VkViewport viewport = {
      0, 0, width, height, S.depth_range[0], S.depth_range[1]};
  if (S.has_viewport) {
    viewport.x = S.viewport[0][0];
    viewport.y = S.viewport[0][1];
    viewport.width = S.viewport[0][2];
    viewport.height = S.viewport[0][3];
  }
  // 原 NVN 左上原点与 Metal 的裁剪坐标 +Y 朝上；Vulkan 正高度会翻转画面与绕序。
  // Original NVN upper-left origin and Metal keep clip +Y upward; a positive
  // Vulkan viewport height would invert the image and triangle winding.
  viewport.y += viewport.height;
  viewport.height = -viewport.height;
  int32_t x = S.has_scissor ? S.scissor.x : 0,
          y = S.has_scissor ? S.scissor.y : 0;
  int64_t right = S.has_scissor ? (int64_t)x + S.scissor.w : width,
          bottom = S.has_scissor ? (int64_t)y + S.scissor.h : height;
  if (x < 0)
    x = 0;
  if (y < 0)
    y = 0;
  if (right > width)
    right = width;
  if (bottom > height)
    bottom = height;
  VkRect2D sc = {{x, y},
                 {right > x ? right - x : 0, bottom > y ? bottom - y : 0}};
  if (!active_job) {
    j->gpu_region = vk_gpu_region_begin("render", list[1] ? list[1]->key : list[0]->key,
                                       j->area.extent.width, j->area.extent.height);
    render_record_begin(j,descriptions,na,color,S.targets.n,include_depth?&depth:NULL,j->local?j->clears:NULL);
    active_job = j;
    active_writes = 0;
    active_targets = S.targets;
    vk_ubo_plan_break();V.passes++;
    if (S.targets.depth && !j->has_depth) {
      uint64_t n = (draw_recorded.depth_omissions += 1);
      if (n<=4 || (V.options.stats && n%600==0))
        LOG("[vulkan pass] omitted unused depth/stencil attachment total=%llu",(unsigned long long)n);
    }
  }
  vk_gpu_region_label(active_job->gpu_region, list[1] ? list[1]->key : list[0]->key);
  active_job->clear_only=0;
  if(!active_job->local)cover_first_draw(active_job,list,&ds,draw,idx,cover_ready,(const float (*)[4])cover_p,cover_descriptor_epoch);
  int state_reference=draw_state_reference();
  for (int i = 0; i < 16; i++)
    if (streams & (1u << i)) {
      vk_buffer *b;
      uint64_t off;
      if (expanded[i]) { b = expanded[i]; off = 0; }
      else if (!S.vertex[i].address ||
          !vk_pool_address_stage(S.vertex[i].address,S.vertex[i].size,(void **)&b,&off,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT))
        die("Vulkan vertex buffer missing: shader=%s stream=%d address=%#llx "
            "size=%llu inputs=%#x",
            list[0]->key, i, (unsigned long long)S.vertex[i].address,
            (unsigned long long)S.vertex[i].size, list[0]->input_mask);
      render_bind_vertex(j,i,b->handle,off,state_reference);
    }
  if (quads) {
    vk_buffer *b=quad_index_buffer();
    render_bind_index(j,b->handle,0,VK_INDEX_TYPE_UINT32,state_reference);
  } else if (idx) {
    if (idx->itype != NVN_INDEX_TYPE_UNSIGNED_SHORT &&
        idx->itype != NVN_INDEX_TYPE_UNSIGNED_INT)
      die("Vulkan 8-bit indices pending");
    vk_buffer *b;
    uint64_t off;
    // Match admission and original Metal: prove the binding address even
    // when no indices are fetched. The one-byte ledger use is conservative.
    uint64_t index_bytes=idx->count?(uint64_t)idx->count*(idx->itype==1?2:4):1;
    if (!vk_pool_address_stage(idx->ibuf,index_bytes,
                  (void **)&b,&off,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT))
      die("Vulkan index buffer missing");
    render_bind_index(j,b->handle,off,
                      idx->itype==1?VK_INDEX_TYPE_UINT16:VK_INDEX_TYPE_UINT32,state_reference);
  }
  render_primary_begin(j);
  vk_counter_draw_begin(occlusion);
  if(state_reference || j->bound_pipeline!=j->pipeline) {
    vkCmdBindPipeline(V.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, j->pipeline);
    j->bound_pipeline=j->pipeline;
  }
  vk_shader_push_bias(list,j->layout,0,state_reference?NULL:&j->bias_state);
  float depth_bias[]={S.offset.units,S.offset.clamp,S.offset.factor};
  if(state_reference || !j->dynamic_state_valid || memcmp(&j->viewport,&viewport,sizeof viewport)) {
    vkCmdSetViewport(V.cmd,0,1,&viewport);j->viewport=viewport;
  }
  if(state_reference || !j->dynamic_state_valid || memcmp(&j->scissor,&sc,sizeof sc)) {
    vkCmdSetScissor(V.cmd,0,1,&sc);j->scissor=sc;
  }
  if(state_reference || !j->dynamic_state_valid || memcmp(j->depth_bias,depth_bias,sizeof depth_bias)) {
    vkCmdSetDepthBias(V.cmd,depth_bias[0],depth_bias[1],depth_bias[2]);
    memcpy(j->depth_bias,depth_bias,sizeof depth_bias);
  }
  j->dynamic_state_valid=1;
  if(state_reference) {
    for(int i=0;i<5;i++) if(list[i])
      vkCmdBindDescriptorSets(V.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, j->layout,
                              list[i]->set, 1, &sets[i], uniform_offsets[list[i]->set].count,
                              uniform_offsets[list[i]->set].offsets);
  } else {
    // Sets are immutable per draw. Bind contiguous set numbers together;
    // holes retain their original unbound state, including absent stages.
    VkDescriptorSet ordered[6]={0};
    for(int i=0;i<5;i++) if(list[i]) ordered[list[i]->set]=sets[i];
    // Different pipeline layouts may disturb other set numbers. Restrict
    // retention to the exact layout and this secondary's own emitted state.
    if(j->bound_layout!=j->layout) {
      memset(j->bound_sets,0,sizeof j->bound_sets);j->bound_layout=j->layout;
    }
    int changed[6]={0};
    for(unsigned i=0;i<6;i++)changed[i]=ordered[i] && (ordered[i]!=j->bound_sets[i] ||
      uniform_offsets[i].count!=j->bound_uniform_offsets[i].count ||
      memcmp(uniform_offsets[i].offsets,j->bound_uniform_offsets[i].offsets,
             uniform_offsets[i].count*sizeof *uniform_offsets[i].offsets));
    for(unsigned first=0;first<6;) {
      if(!changed[first]) { first++;continue; }
      unsigned end=first+1;
      while(end<6 && changed[end])end++;
      uint32_t offsets[6*VK_UNIFORM_OFFSET_CAPACITY],count=0;
      for(unsigned i=first;i<end;i++) {
        memcpy(offsets+count,uniform_offsets[i].offsets,uniform_offsets[i].count*sizeof *offsets);
        count+=uniform_offsets[i].count;
      }
      vkCmdBindDescriptorSets(V.cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,j->layout,
                              first,end-first,ordered+first,count,offsets);
      memcpy(j->bound_sets+first,ordered+first,(end-first)*sizeof *ordered);
      for(unsigned i=first;i<end;i++) {
        j->bound_uniform_offsets[i].count=uniform_offsets[i].count;
        memcpy(j->bound_uniform_offsets[i].offsets,uniform_offsets[i].offsets,
               uniform_offsets[i].count*sizeof *uniform_offsets[i].offsets);
      }
      first=end;
    }
  }
  if (quads)
    vkCmdDrawIndexed(V.cmd,(uint32_t)(draw->count/4)*6,1,0,draw->first,0);
  else if (idx)
    vkCmdDrawIndexed(V.cmd, idx->count, idx->instances, 0, idx->base_vertex,
                     idx->base_instance);
  else
    vkCmdDraw(V.cmd, draw->count, 1, draw->first, 0);
  if(local_consumer && active_job==local_job) {local_job->local->finished=1;draw_recorded.pixel_local_draws++;}
  if(vk_mip_clear_pending() && mip_clear.returned && active_job==mip_clear.job)
    mip_clear_recorded.continued++;
  vk_counter_draw_end(occlusion);
  if (list[1])
    for (int i = 0; i < S.targets.n; i++)
      if (S.targets.color[i] && ((S.chmask.mask >> (i * 4)) & 15))
        active_writes |= 1u << i;
  if ((ds.depthTestEnable && ds.depthWriteEnable) ||
      stencil_writing)
    active_writes |= 1u << 8;
  if(ds.depthTestEnable && ds.depthWriteEnable) j->ds_written |= VK_IMAGE_ASPECT_DEPTH_BIT;
  if(stencil_writing) j->ds_written |= VK_IMAGE_ASPECT_STENCIL_BIT;
  vk_pass_plan_done(draw?(const void *)draw:(const void *)idx);
  if(attachment_reuse_mode() && !writable_buffers && !local_ready && !local_consumer && !vk_ghost_replaying() &&
      !expansion && !vk_mip_clear_pending() && j==active_job)
    vk_texture_raster_capture(&j->raster_group);
  vk_texture_shader_record();
  vk_shader_writebacks();
  vk_ubo_plan_recorded();
  if (list[1] && vk_trace_frame_active() && V.trace_directory)
    for (int i = 0; i < S.targets.n; i++)
      if (S.targets.color[i])
        vk_texture_trace(S.targets.color[i],
                         S.targets.has_cview[i] ? &S.targets.cview[i] : NULL,
                         list[1]->key);
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  if(attachment_job)attachment_reuse_hits++;
#endif
  if(attachment_job) {
    // One semantic admission marker; official sampling cannot identify whether
    // a guarded draw actually retained its borrowed representation to record.
    static int first_recorded;
    if(!first_recorded) {
      first_recorded=1;
      LOG("[vulkan attachment reuse] first actual recorded draw attachments=%u",na);
    }
  }
  V.draws++;
  if (draw) vk_ghost_note(draw, local_consumer?local_consumer->source_shader:list[1]);
  bq_after_draw();
  vk_pool_operation_end(pool_operation);
  vk_profile_end(VK_TIME_DRAW, started);
  return;
attachment_cold_retry:
  // No query slot or draw has been emitted. Keep one-time no-FXAA/bloom
  // selection and the operation's owned Pool lifetime; only rebuild consumers.
  // The borrowed-job path excludes instance expansion, so no old-recording
  // expansion temporary becomes part of this separate retry contract. Full
  // commit remains forbidden while the existing shader operation is active.
  vk_image_split_break();vk_ubo_plan_break();
  vk_texture_shader_abandon();vk_shader_operation_abandon();
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  attachment_reuse_retries++;
#endif
  attachment_retry=1; // Cold path cannot jump back here again.
  goto attachment_prepare;
}
void vk_draw(const p_draw *draw,const p_draw_idx *idx) {
  vk_draw_record(draw,idx,0);
}

void vk_defer_pass(VkRenderPass pass, VkFramebuffer framebuffer) {
  render_job *j = calloc(1, sizeof *j);
  if (!j)
    die("Vulkan clear-pass allocation");
  j->pass = pass;
  j->framebuffer = framebuffer;
  j->next = graphics_state->jobs;
  graphics_state->jobs = j;
}
