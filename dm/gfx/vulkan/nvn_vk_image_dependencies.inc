// Included after image's private definition. All access history is owned by
// the actual VkImage, survives submission boundaries, and is freed only with
// that image. No frame index or CPU completion is a visibility proof.
typedef struct {
  image *owner;
  // Normalized once when the request is constructed. The physical owner's
  // aspect/mip/layer topology is immutable, including across image promotion.
  // Access histories, visibility and stage hazards are still queried afresh.
  VkImageSubresourceRange range;
  VkPipelineStageFlags stages;
  VkAccessFlags access;
  int attachment;
} image_request;
typedef struct {
  image_request *requests;size_t count,capacity;
  // An operation result, not a cross-draw cache: original request order is
  // readonly prefix then writer suffix until an actual later read disproves
  // it. Promotions change owners, never request access flags or ordinals.
  size_t writer_begin;
  int writer_suffix;
  int split_read_group; // one borrowed immutable FS group, no per-draw members
  void *raster_group; // job-owned prospective attachment vector
} image_batch;
_Static_assert(sizeof(image_batch)==48,"Image batch adds one job-owned raster vector reference");
// Original drag-follow rendering can replay a draw while ending another
// operation's previous pass. Preserve the outer descriptor batch across it.
static image_batch image_batches[8];
static unsigned image_binding_active;
#define image_batch_current (image_batches[image_binding_active?image_binding_active-1:0])
#define image_bindings (image_batch_current.requests)
#define image_binding_count (image_batch_current.count)
#define image_binding_capacity (image_batch_current.capacity)
static int image_record_merging;
static int image_record_preserve_readers;
static uint64_t image_record_raster;
static uint64_t image_write_clock,image_pass_begin_clock;
static uint64_t image_reference_visible_clock;
static VkAccessFlags image_writes(VkAccessFlags access) {
  return access&(VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT|
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT|
      VK_ACCESS_MEMORY_WRITE_BIT);
}
static int image_access_contains(VkAccessFlags scope,VkAccessFlags access) {
  // Generic MEMORY scopes cover the concrete image access types used here.
  // Unknown access bits retain the ordinary dependency path.
  if(scope&VK_ACCESS_MEMORY_READ_BIT) scope|=VK_ACCESS_SHADER_READ_BIT|
      VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
  if(scope&VK_ACCESS_MEMORY_WRITE_BIT) scope|=image_writes(UINT32_MAX);
  return !(access&~scope);
}
static VkImageSubresourceRange image_layer_range(VkImageSubresourceLayers r) {
  return (VkImageSubresourceRange){r.aspectMask,r.mipLevel,1,r.baseArrayLayer,r.layerCount};
}
static VkImageSubresourceRange image_access_range(image *i,VkImageSubresourceRange r) {
  // This backend does not enable maintenance7's independent depth/stencil
  // attachment accesses. Attachment stores may read-modify-write the other
  // aspect, so keep their dependency histories coupled.
  if(i->aspect==(VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT)) r.aspectMask=i->aspect;
  // A 2D attachment view of a 3D image selects a depth slice. Vulkan image
  // barriers still address the whole 3D mip, whose array layer is always zero.
  if(i->description.imageType==VK_IMAGE_TYPE_3D) { r.baseArrayLayer=0;r.layerCount=1; }
  if(r.levelCount==VK_REMAINING_MIP_LEVELS) r.levelCount=i->levels-r.baseMipLevel;
  if(r.layerCount==VK_REMAINING_ARRAY_LAYERS) r.layerCount=i->layers-r.baseArrayLayer;
  if(!r.aspectMask || (r.aspectMask&~i->aspect) || !r.levelCount || !r.layerCount ||
      r.baseMipLevel>=i->levels || r.levelCount>i->levels-r.baseMipLevel ||
      r.baseArrayLayer>=i->layers || r.layerCount>i->layers-r.baseArrayLayer)
    die("Vulkan image access range");
  return r;
}
static image_request image_request_make(image *i,VkImageSubresourceRange r,
    VkPipelineStageFlags stages,VkAccessFlags access) {
  return (image_request){i,image_access_range(i,r),stages,access};
}
static image_access *image_cell(image *i,unsigned aspect,unsigned level,unsigned layer) {
  raster_observe(i);
  return &i->access[((size_t)aspect*i->levels+level)*i->layers+layer];
}
static image_read_proof *image_read_cached(image *i,VkImageSubresourceRange r) {
  raster_observe(i);
  for(unsigned k=0;k<2;k++) {
    image_read_proof *p=&i->read_proofs[k];
    if(p->generation==i->access_generation && !memcmp(&p->range,&r,sizeof r)) return p;
  }
  return NULL;
}
static int image_read_covered(image *i,VkImageSubresourceRange r,VkPipelineStageFlags stages,int visibility) {
  image_read_proof *p=image_read_cached(i,r);
  return p && !(stages&~(visibility?p->visible:p->readers));
}
static __attribute__((noinline)) void image_record_cells(image *i,VkImageSubresourceRange r,
    VkPipelineStageFlags stages,VkAccessFlags access,int visibility_proven,
    image_read_proof *read,int writing,int mip_stamped) {
  for(unsigned a=0;a<3;a++) if(r.aspectMask&(1u<<a))
    for(unsigned level=r.baseMipLevel;level<r.baseMipLevel+r.levelCount;level++)
      for(unsigned layer=r.baseArrayLayer;layer<r.baseArrayLayer+r.layerCount;layer++) {
        image_access *c=image_cell(i,a,level,layer);
        // mip_stamped implies a single-layer color image. The normalized
        // range therefore visits exactly the cells of the former stamp loop.
        if(mip_stamped) c->use_clock=i->use_clock;
        if(stages) { c->layout_ordered=0;c->layout_ordered_access=0; }
        if(writing) {
          if(image_record_merging) {
            if(!c->operation_writers) c->raster_pass=image_record_raster;
            else if(!image_record_raster || c->raster_pass!=image_record_raster) c->raster_pass=0;
            c->operation_writers|=stages;
          } else {
            if(!image_record_preserve_readers) c->readers=0;
            c->operation_writers=0;
            c->raster_pass=image_record_raster;
          }
          // Pixel rectangles are not tracked. A partial overwrite cannot
          // retire a writer of untouched pixels in this same mip/layer.
          c->writers|=stages;c->writes|=image_writes(access);
          if(stages) {
            c->write_clock=++image_write_clock;
          }
          c->visible=0;
        } else {
          c->readers|=stages;
          if(visibility_proven) c->visible|=stages;
        }
      }
  if(!writing) {
    // No callback or generation change occurs in the readonly cell loop, so
    // the exact proof looked up above remains valid; do not search it twice.
    if(!read) {
      read=&i->read_proofs[i->read_proof_next++%2];
      *read=(image_read_proof){.range=r,.generation=i->access_generation};
    }
    read->readers|=stages;
    if(visibility_proven) read->visible|=stages;
    if(mip_stamped && r.levelCount>1) read->mip_stamp=i->use_clock;
  }
}
static void image_record_normalized(image *i,VkImageSubresourceRange r,VkPipelineStageFlags stages,
                          VkAccessFlags access,int visibility_proven) {
  raster_observe(i);
  int writing=!!image_writes(access);
#ifdef DM_VULKAN_NATIVE_RESOURCES
  if(i->native_entry)native_image_record(i,r,stages,access);
#endif
  if(writing) {image_split_changed(i);image_rebuildable_set(i,0);image_astc_proof(i,0);
#ifdef DM_VULKAN_SHARED_POOLS
    i->shared_writer=1;i->shared_writer_source=0;i->shared_writer_region=(NVNcopyRegion){0};
    image_shared_alias_write(i);
#endif
  }
  image_read_proof *read=writing?NULL:image_read_cached(i,r);
  int covered=read && !(stages&~(visibility_proven?read->visible:read->readers));
  // Keep every logical clock increment and whole-image timestamp. Cell use
  // clocks are observed only relative to physical pass begin: once this exact
  // covered range has been stamped after that cutoff, restamping its mips
  // cannot change any consumer's before/after classification. A later pass
  // cutoff includes all intervening global increments and requires a restamp.
  if(stages) i->use_clock=++image_write_clock;
  int mip_stamped=stages && image_mip_tracked(i) &&
      !(r.levelCount>1 && covered && read->mip_stamp>image_pass_begin_clock);
  // Covered reads return before the cell walk. All other accesses already
  // visit these exact color/mip cells below; stamp them during that walk.
  if(mip_stamped && covered)
    for(unsigned level=r.baseMipLevel;level<r.baseMipLevel+r.levelCount;level++)
      image_cell(i,0,level,0)->use_clock=i->use_clock;
  if(writing && r.baseMipLevel<16) {
    unsigned n=r.levelCount<16-r.baseMipLevel?r.levelCount:16-r.baseMipLevel;
    i->zero_mips&=~zero_mip_mask(r.baseMipLevel,n);
  }
  // Repeated immutable shader bindings have the same normalized view. Retain
  // their exact-range proof instead of walking all mips/layers for each draw.
  // Every image write, including operation pre-clear and unknown access,
  // invalidates it. This does not expand a reader to unrelated subresources.
  if(writing) i->access_generation++;
  else if(covered) {
    if(mip_stamped && r.levelCount>1) read->mip_stamp=i->use_clock;
    return;
  }
  image_record_cells(i,r,stages,access,visibility_proven,read,writing,mip_stamped);
}
// Non-request callers still supply arbitrary view/layer ranges, so retain the
// depth/stencil coupling, 3D slice mapping and bounds checks at that boundary.
static void image_record(image *i,VkImageSubresourceRange r,VkPipelineStageFlags stages,
                          VkAccessFlags access,int visibility_proven) {
  image_record_normalized(i,image_access_range(i,r),stages,access,visibility_proven);
}
static void image_unknown(image *i) {
  raster_observe(i);
  if(i->astc_copy_encoding || i->bc_copy_decoded)die("ASTC propagated image cannot escape as untyped logical BC3 storage");
  i->external_access=1;
  image_astc_proof(i,0);
  if(!i->access) return;
  image_record(i,(VkImageSubresourceRange){i->aspect,0,i->levels,0,i->layers},
      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,0);
}
// Keep the stage pairs separate. A union across unrelated requests would
// recreate the fragment -> early-stage dependency that this removes.
static VkPipelineStageFlags image_dependency_source(const image_request *q,const image_access *c) {
  // Raster ordering covers only fixed-function accesses inside the same
  // actual render pass. A shader reader/writer or a new physical pass cannot
  // use this exemption, even if it addresses the same attachment view.
  VkPipelineStageFlags raster=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  if(q->attachment && c->raster_pass && !(c->readers&~raster) &&
      vk_render_active() && c->raster_pass==V.passes) return 0;
  int writing=!!image_writes(q->access);
  if(c->layout_ordered && ((c->layout_ordered&VK_PIPELINE_STAGE_ALL_COMMANDS_BIT) ||
      !(q->stages&~c->layout_ordered)) &&
      image_access_contains(c->layout_ordered_access,q->access)) return 0;
  VkPipelineStageFlags visible=c->visible;
  if(visible&VK_PIPELINE_STAGE_ALL_COMMANDS_BIT) visible|=q->stages;
  if(vk_submission_barrier_mode()!=0 && !(c->writers&VK_PIPELINE_STAGE_ALL_COMMANDS_BIT) &&
      c->write_clock<=image_reference_visible_clock) visible|=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  // A per-mip transition has its own destination scope. The whole-image
  // scope alone cannot chain that transition to a later access in another
  // stage (for example TRANSFER normalization followed by attachment CLEAR).
  return (writing?c->readers:0)|((writing || (q->stages&~visible))?
      (c->writers|c->mip_scope|q->owner->layout_scope):0);
}
void vk_texture_pass_begin(void) {
  // Secondary recording postpones the primary's actual BeginRenderPass.
  // Remember which writes precede it; do not synchronize unused images here.
  image_pass_begin_clock=image_write_clock;
  // Preserve the broad pre-pass proof in the full-barrier comparison mode.
  // Otherwise changing this optimization would also change its legacy pass
  // grouping (and upstream pass-scoped UI replay) in the reference run.
  if(vk_submission_barrier_mode()!=0) {
    VkMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT};
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,1,&b,0,NULL,0,NULL);
    vk_buffer_producers_visible(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|
        VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    image_reference_visible_clock=image_write_clock;
  }
}
static void image_dependencies_batch(const image_request *requests,size_t count,int preserve_render,int covered,image_batch *batch) {
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  if(batch) {
    raster_test_dependency_interleave();
    image_split_dependency_fixture();
    requests=batch->requests;count=batch->count;
  }
#endif
#ifdef DM_VULKAN_NATIVE_RESOURCES
  // Image read-cache proofs cannot cover VkBuffer aliases. Prepare their
  // actual RAW/WAR/WAW dependencies BEFORE the consumer, including covered
  // image reads and conservative/unknown Buffer writers. Post-draw access
  // recording is too late when that path must close a physical pass.
  for(size_t k=0;k<count;k++) {
    // Native preparation may end a pass, replay a nested draw and materialize
    // this batch. Never borrow its allocation across that callback.
    const image_request *q=&requests[k];
    if(q->owner->native_entry) {
      native_image_prepare(q->owner,q->range,q->stages,q->access);
      if(batch) {requests=batch->requests;count=batch->count;}
    }
  }
#endif
  if(!preserve_render) {
    vk_render_end();
    if(batch) {requests=batch->requests;count=batch->count;}
  }
  int explicit_order=!!(V.nvn_barrier&
      (NVN_BARRIER_ORDER_PRIMITIVES_BIT|NVN_BARRIER_ORDER_INDIRECT_DATA_BIT));
  int conservative=(!covered && explicit_order) || vk_submission_barrier_mode()!=0;
  if(conservative && (!preserve_render || !vk_render_active())) {
    vk_barrier();return;
  }
  VkCommandBuffer before_pass=preserve_render?vk_render_pending_primary():VK_NULL_HANDLE;
  int needed=0;
  for(size_t k=0;k<count;k++) {
    const image_request *q=&requests[k];image *i=q->owner;
    VkImageSubresourceRange r=q->range;
    if(!image_writes(q->access) && image_read_covered(i,r,q->stages,1)) continue;
    for(unsigned a=0;a<3;a++) if(r.aspectMask&(1u<<a))
      for(unsigned level=r.baseMipLevel;level<r.baseMipLevel+r.levelCount;level++)
        for(unsigned layer=r.baseArrayLayer;layer<r.baseArrayLayer+r.layerCount;layer++) {
          const image_access *c=image_cell(i,a,level,layer);
          if(!image_dependency_source(q,c)) continue;
          needed=1;
          // Only a read of an earlier producer may move before the pass.
          // Feedback, attachment conflicts, unknown access and writes from a
          // draw in this secondary retain the ordinary end-pass dependency.
          if(q->attachment || image_writes(q->access) ||
              q->stages!=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT ||
              c->write_clock>image_pass_begin_clock ||
              (c->writers&VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)) before_pass=VK_NULL_HANDLE;
        }
  }
  if(!needed) return;
  if(conservative) { vk_barrier();return; }
  // Only a real conflict ends a pass. Re-query afterwards: original drag-follow
  // replay while ending it can add accesses or upgrade a pending image view.
  if(!before_pass) {
    vk_render_end();
    if(batch) {requests=batch->requests;count=batch->count;}
  }
  VkCommandBuffer command=before_pass?before_pass:V.cmd;
  // GENERAL -> GENERAL does not require an image layout transition. Batch
  // memory dependencies by stage pair instead of emitting one barrier per
  // mip/layer. Resource ranges select hazards; stages select execution scope.
  struct { VkPipelineStageFlags src,dst;VkMemoryBarrier memory; } groups[64];
  unsigned n=0;
  for(size_t k=0;k<count;k++) {
    const image_request *q=&requests[k];image *i=q->owner;
    VkImageSubresourceRange r=q->range;
    if(!image_writes(q->access) && image_read_covered(i,r,q->stages,1)) continue;
    for(unsigned a=0;a<3;a++) if(r.aspectMask&(1u<<a))
      for(unsigned level=r.baseMipLevel;level<r.baseMipLevel+r.levelCount;level++)
        for(unsigned layer=r.baseArrayLayer;layer<r.baseArrayLayer+r.layerCount;layer++) {
          const image_access *c=image_cell(i,a,level,layer);
          VkPipelineStageFlags src=image_dependency_source(q,c);
          if(!src) continue;
          unsigned g=0;
          while(g<n && (groups[g].src!=src || groups[g].dst!=q->stages)) g++;
          if(g==64) {
            for(unsigned j=0;j<n;j++) vkCmdPipelineBarrier(command,groups[j].src,
                groups[j].dst,0,1,&groups[j].memory,0,NULL,0,NULL);
            g=n=0;
          }
          if(g==n) {
            groups[g].src=src;groups[g].dst=q->stages;
            groups[g].memory=(VkMemoryBarrier){.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER};n++;
          }
          groups[g].memory.srcAccessMask|=c->writes;
          groups[g].memory.dstAccessMask|=q->access;
        }
  }
  for(unsigned g=0;g<n;g++) vkCmdPipelineBarrier(command,groups[g].src,groups[g].dst,
      0,1,&groups[g].memory,0,NULL,0,NULL);
}
// Standalone copy/transition requests are by-value caller-owned arrays; only
// shader batches may grow during callbacks and must follow stable batch identity.
static void image_dependencies(const image_request *requests,size_t count,int preserve_render,int covered) {
  image_dependencies_batch(requests,count,preserve_render,covered,NULL);
}
static void image_transfer(image *i,VkImageSubresourceRange r,int writing) {
  image_request request=image_request_make(i,r,VK_PIPELINE_STAGE_TRANSFER_BIT,
      writing?VK_ACCESS_TRANSFER_WRITE_BIT:VK_ACCESS_TRANSFER_READ_BIT);
  image_dependencies(&request,1,0,1);
  image_record_normalized(i,request.range,request.stages,request.access,1);
}
static void image_transfer_pair(image *source,VkImageSubresourceLayers sr,
                                 image *destination,VkImageSubresourceLayers dr) {
  image_request requests[]={
    image_request_make(source,image_layer_range(sr),VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT),
    image_request_make(destination,image_layer_range(dr),VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT)};
  image_dependencies(requests,2,0,1);
  for(unsigned k=0;k<2;k++) image_record_normalized(requests[k].owner,requests[k].range,
      requests[k].stages,requests[k].access,1);
}
void vk_texture_shader_begin(void) {
  if(image_binding_active==sizeof image_batches/sizeof *image_batches) die("Vulkan image operation nesting");
  image_binding_active++;
  if(image_binding_count) die("Vulkan unfinished image operation");
  image_batch_current.writer_begin=SIZE_MAX;image_batch_current.writer_suffix=1;
  image_batch_current.split_read_group=0;image_batch_current.raster_group=NULL;
}
// Only immutable physical-view recipes may bypass normalization here. The
// stored access range is separate from the VkImageView's range: coupled DS
// aspects and a 3D slice's dependency layer zero must not change that view.
static void image_request_add_normalized(image *i,VkImageSubresourceRange r,
    VkPipelineStageFlags stages,VkAccessFlags access) {
  // Even a non-overlapping late read belongs AFTER the borrowed attachment
  // vector. Expand before appending so readonly depth keeps its original clock order.
  if(image_binding_active && image_batch_current.raster_group)vk_texture_raster_break();
  raster_observe(i);
  if(!image_binding_active || !stages) { image_unknown(i);return; }
  // Prospective writers conservatively invalidate before appending. This
  // may materialize reads/reallocate, before any caller borrows the new slot;
  // it publishes no GPU history and needs no extra per-draw writer scan.
  if(image_writes(access))image_split_changed(i);
  if(image_binding_count==image_binding_capacity) {
    size_t capacity=image_binding_capacity?image_binding_capacity*2:32;
    image_request *grown=realloc(image_bindings,capacity*sizeof *grown);
    if(!grown) die("Vulkan image operation allocation");
    image_bindings=grown;image_binding_capacity=capacity;
  }
  // Construct the final slot directly; normalization still performs every
  // aspect/topology/range check, without packing a second 40-byte request.
  size_t ordinal=image_binding_count++;
  if(image_writes(access)) {
    if(image_batch_current.writer_begin==SIZE_MAX)image_batch_current.writer_begin=ordinal;
  } else if(image_batch_current.writer_begin!=SIZE_MAX)image_batch_current.writer_suffix=0;
  image_request *request=&image_bindings[ordinal];
  request->owner=i;
  request->range=r;
  request->stages=stages;request->access=access;request->attachment=0;
}
static void image_request_add(image *i,VkImageSubresourceRange r,VkPipelineStageFlags stages,VkAccessFlags access) {
  if(!image_binding_active || !stages) { image_unknown(i);return; }
  image_request_add_normalized(i,image_access_range(i,r),stages,access);
}
#include "nvn_vk_attachment_raster_run.inc"
static VkPipelineStageFlags image_shader_stages(VkShaderStageFlagBits stage) {
  switch(stage) {
  case VK_SHADER_STAGE_VERTEX_BIT:return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
  case VK_SHADER_STAGE_FRAGMENT_BIT:return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  case VK_SHADER_STAGE_COMPUTE_BIT:return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
  default:return 0;
  }
}
static void image_binding_replaced(image *previous,image *retired) {
  // Promotion ghosts preserve the same image type, aspect and subresource
  // counts; only the VkImage/storage/view capabilities change. Thus a request
  // keeps its normalized range when redirected to the original physical image.
  for(unsigned b=0;b<image_binding_active;b++)
    for(size_t k=0;k<image_batches[b].count;k++)
      if(image_batches[b].requests[k].owner==previous) image_batches[b].requests[k].owner=retired;
}
const char *vk_texture_quad_readonly_error(void) {
  vk_image_split_break();
  if(!image_binding_active)return "image operation must be collecting";
  for(size_t n=0;n<image_binding_count;n++) {
    const image_request *q=&image_bindings[n];
    if(q->attachment || !(q->stages&VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT) ||
        !(q->access&VK_ACCESS_SHADER_READ_BIT))continue;
    if(!q->owner || !q->owner->handle)return "sampled physical image missing";
    if(image_writes(q->access))return "sampled image also writable";
    for(size_t k=0;k<image_binding_count;k++) {
      const image_request *w=&image_bindings[k];
      if(!w->attachment || !image_writes(w->access) || !w->owner ||
          w->owner->handle!=q->owner->handle)continue;
      const VkImageSubresourceRange *a=&q->range,*b=&w->range;
      if((a->aspectMask&b->aspectMask) &&
          a->baseMipLevel<b->baseMipLevel+b->levelCount && b->baseMipLevel<a->baseMipLevel+a->levelCount &&
          a->baseArrayLayer<b->baseArrayLayer+b->layerCount && b->baseArrayLayer<a->baseArrayLayer+a->layerCount)
        return "sampled physical subresource overlaps attachment writer";
    }
  }
  return NULL;
}
void vk_texture_shader_barrier(void) {
  image_dependencies_batch(image_bindings,image_binding_count,0,1,&image_batch_current);
}
void vk_texture_graphics_barrier(void) {
  image_dependencies_batch(image_bindings,image_binding_count,1,1,&image_batch_current);
}
int vk_texture_shader_collecting(void) {
  return image_binding_active!=0;
}
void vk_texture_shader_abandon(void) {
  if(!image_binding_active) die("Vulkan image operation not begun");
  // Binding preparation may execute uploads/copies and end a physical pass.
  // Those commands publish their own histories immediately. The collected
  // shader requests are only prospective: no draw/dispatch consumed them, so
  // neither access nor visibility may be published while discarding a retry.
  image_batch_current.split_read_group=0;image_batch_current.raster_group=NULL;
  image_binding_count=0;image_binding_active--;
}
void vk_texture_shader_record(void) {
  raster_capture_begin();
  if(!image_binding_active) die("Vulkan image operation not begun");
  // Query all old histories before recording any part of this operation.
  // Clear old histories of written cells once, then union all simultaneous
  // readers/writers, including one image bound to several shader stages.
  image_record_raster=0;
  int partition=image_batch_current.writer_suffix;
  size_t writer_begin=image_batch_current.writer_begin;
  if(writer_begin==SIZE_MAX)writer_begin=image_binding_count;
  // Consume the phase artifact produced with the real requests. The proved
  // suffix needs neither a whole-batch writer search nor per-request filters.
  // Interleaved storage/read cases retain the complete original algorithm.
  for(size_t k=partition?writer_begin:0;k<image_binding_count;k++) {
    image_request *q=&image_bindings[k];
    if(partition || image_writes(q->access))image_record_normalized(q->owner,q->range,0,q->access,0);
  }
  image_record_merging=1;
  for(unsigned writing=0;writing<2;writing++) {
    size_t first=partition && writing?writer_begin:0;
    size_t end=partition && !writing?writer_begin:image_binding_count;
    for(size_t k=first;k<end;k++) {
      image_request *q=&image_bindings[k];
      if(partition || !!image_writes(q->access)==writing) {
        image_record_raster=q->attachment && vk_render_active()?V.passes:0;
        image_record_normalized(q->owner,q->range,q->stages,q->access,1);
      }
    }
  }
  raster_record_done();
  image_split_record_done();
  image_batch_current.split_read_group=0;
  image_binding_count=0;image_binding_active--;
  image_record_merging=0;
  image_record_raster=0;
}

#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
#include "nvn_vk_image_phase_fixture.inc"
#endif
