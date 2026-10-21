#include "../nvn/nvn_dirty.h"
// Recording-owner diagnostic for a FAILED logical proof, not timing/sampling.
static const char *pool_range_error="batch/capacity or unsupported logical state";
const char *vk_shader_pool_range_error(void) { return pool_range_error; }
static int pool_range_fail(const char *reason) {pool_range_error=reason;return 0;}
// One logical resource enumeration for actual compute dependencies and future
// physical-backing preflight. Do not acquire VkBuffer/descriptor/temp handles.
static int shader_pool_requirement(const p_bind_buf *buffer,int kind,int writable,
    VkPipelineStageFlags stages,uint64_t prefix_alignment,
    vk_pool_requirement_sink sink,void *context) {
  if(!buffer->addr || !buffer->size)return 1; // existing zero-buffer fallback
  if(buffer->size>UINT64_MAX-buffer->addr)return pool_range_fail("buffer address overflow");
  vk_pool_requirement range={buffer->addr,buffer->size,prefix_alignment,stages,writable,kind};
  return sink(context,&range);
}
int vk_shader_pool_ranges_state(vk_shader *const list[6],const vk_state *state,int compute,
    vk_pool_requirement_sink sink,void *context) {
  if(!list || !state || !sink)return pool_range_fail("invalid shader/state/sink");
  for(unsigned stage=compute?5:0;stage<(compute?6:5);stage++)if(list[stage]) {
    const vk_shader *s=list[stage];
    if(s->stage!=stage || s->nb>128 || (s->nb&&!s->bindings))return pool_range_fail("shader stage/binding table");
    if(!s->nb)continue;
    // Snapshot only immutable eligibility, never current addresses or ranges.
    // Consume each mask word locally instead of reloading/shifting it for
    // every selected binding. Mocks/unproved schemas keep the original scan.
    int masked=s->pool_binding_mask_ready;
    // Reuse the descriptor consumer's final immutable offsets, not a second
    // schema or a resource cache. The supplied live state can be a saved ghost.
    const struct vk_binding_consumer *program=masked && !vk_binding_consumer_reference_mode()?s->binding_consumers:NULL;
    uint64_t remaining=masked?s->pool_binding_mask[0]:0;
    unsigned base=0,cursor=0;
    for(;;) {
      unsigned k;
      if(masked) {
        if(!remaining) {
          if(base)break;
          base=64;remaining=s->pool_binding_mask[1];
          if(!remaining)break;
        }
        k=base+(unsigned)__builtin_ctzll(remaining);remaining&=remaining-1;
      } else {
        if(cursor>=s->nb)break;
        k=cursor++;
      }
      const vk_binding *b=&s->bindings[k];
      const struct vk_binding_consumer *consumer=program?program+k:NULL;
      if(!consumer && b->count!=1)return pool_range_fail("descriptor array count");
      const p_bind_buf *buffer=NULL;uint64_t prefix=0;int writable=0;
      unsigned op=consumer?consumer->op&BIND_OP_MASK:BIND_INVALID;
      if(consumer && (op==BIND_UNIFORM || op==BIND_STORAGE)) {
        // These exact source offsets/register bounds were validated by the
        // final shader constructor for the same descriptor consumer. Never
        // retain addr/size or a backing: gather current values on every call.
        if(consumer->source!=UINT16_MAX)
          buffer=(const p_bind_buf *)((const uint8_t *)state+consumer->source);
        if(op==BIND_STORAGE) {
          writable=!!(consumer->op&BIND_PARAM_STORAGE);
          if(s->storage_bias_mask&(UINT32_C(1)<<consumer->aux))
            prefix=V.props.limits.minStorageBufferOffsetAlignment;
        }
      } else if(b->kind==1) {
        if(b->reg<0 || b->reg>=19)return pool_range_fail("uniform register range");
        // c0/c1/c2 are host-owned driver/embedded constants. Ordinary UBOs
        // still resolve their FULL declared size before alignment/copy logic;
        // descriptor.range=min(size,64KiB) is not the lookup's range.
        if(b->reg>=3)buffer=&state->ubo[stage][b->reg-3];
      } else if(b->kind==2 || b->kind==8) {
        if(b->reg<0 || b->reg>=16)return pool_range_fail("storage register range");
        buffer=b->kind==8?&state->ubo[stage][b->reg]:&state->ssbo[stage][b->reg];
        writable=vk_binding_writable(b);
        unsigned bias=(unsigned)b->reg+(b->kind==8?16:0);
        if(s->storage_bias_mask&(UINT32_C(1)<<bias))
          prefix=V.props.limits.minStorageBufferOffsetAlignment;
      } else if(b->kind==7 || b->kind==9) {
        if(b->reg<0 || b->reg>=(b->kind==9?32:128))return pool_range_fail("texel register range");
        uint64_t handle=b->kind==9?state->img[stage][b->reg]:state->tex[stage][b->reg];
        tex_t texture;
        if(!vk_texture_descriptor(handle&0xfffff,&texture) ||
            texture.d.target!=NVN_TEXTURE_TARGET_BUFFER)return pool_range_fail("missing/non-buffer texel descriptor");
        p_bind_buf texel={.addr=(uintptr_t)texture.mem,.size=texture.L.size};
        if(!shader_pool_requirement(&texel,b->kind,vk_binding_writable(b),
            producer_stage(stage),0,sink,context))return 0;
      } else if(b->kind!=0 && (b->kind<3 || b->kind>6)) {
        // Optional texel-storage image alternatives need their selected
        // physical-image recipe, not a guessed NVN buffer extent. An unknown
        // representation cannot prove complete window requirements.
        return pool_range_fail("unsupported physical alternative binding");
      }
      if(buffer && !shader_pool_requirement(buffer,b->kind,writable,
          producer_stage(stage),prefix,sink,context))return 0;
    }
#ifdef DM_VULKAN_NATIVE_RESOURCES
    // Image schemas use their existing immutable image iterator, independent
    // of the buffer-only mask. Native payloads retain their logical Pool; immutable image imports are separate from real Buffer admission.
    image_source_plan *source_plan=image_sources_current();
    image_source_stage *source_part=image_sources_stage(source_plan,s,state,stage,compute);
    if(source_part) {
      for(unsigned j=0;j<source_part->count;j++) {
        image_source_entry *entry=&source_plan->entries[source_part->begin+j];
        const tex_t *t=image_source_resolve(source_plan,entry,state,stage);
        if(!t)return pool_range_fail("missing native candidate image descriptor");
        if(!vk_texture_native_pool_collect(t,producer_stage(stage),0,sink,context))return 0;
      }
    } else if(vk_native_resources_active())for(vk_image_binding_iterator it=vk_image_binding_begin(s);;) {
      unsigned k;if(!vk_image_binding_next(s,&it,&k))break;
      const vk_binding *b=&s->bindings[k];
      if(b->kind!=3 && b->kind!=4 && b->kind!=6)continue;
      if(b->count!=1)return pool_range_fail("native descriptor array count");
      if(b->reg<0 || b->reg>=(b->kind==4?32:128))return pool_range_fail("native image register range");
      uint64_t handle=b->kind==4?state->img[stage][b->reg]:state->tex[stage][b->reg];
      tex_t t;
      if(!vk_texture_descriptor(handle&0xfffff,&t))return pool_range_fail("missing native candidate image descriptor");
      if(!vk_texture_native_pool_collect(&t,producer_stage(stage),vk_binding_writable(b),sink,context))return 0;
    }
#endif
  }
  return 1;
}
int vk_shader_pool_ranges(vk_shader *const list[6],int compute,
    vk_pool_requirement_sink sink,void *context) {
  return vk_shader_pool_ranges_state(list,&S,compute,sink,context);
}
// A complete draw includes all shader stages and each declared vertex range,
// even when CPU instance expansion later uses only a subset of that range.
// Logical NVN state is supplied explicitly so a deferred original host draw
// can be planned without installing its saved state into the active recorder.
static int graphics_pool_ranges(vk_shader *const list[6],const vk_state *state,
    const p_draw_idx *indexed,vk_pool_requirement_sink sink,void *context,int admission) {
  if(!list || !list[0] || !state || !sink)return pool_range_fail("missing graphics vertex shader/state");
  if(!vk_shader_pool_ranges_state(list,state,0,sink,context))return 0;
  unsigned seen=0;
  for(unsigned attribute=0;attribute<16;attribute++)
    if((list[0]->input_mask&(1u<<attribute)) && state->attr[attribute].fmt) {
      unsigned stream=state->attr[attribute].stream;
      if(stream>=16)return pool_range_fail("vertex stream index");
      if(seen&(1u<<stream))continue;
      seen|=1u<<stream;
      const NVNbufferRange *vertex=&state->vertex[stream];
      // Unlike optional shader bindings, mandatory vertex bindings have no
      // zero-buffer fallback. An address-only binding needs conservative
      // whole-pool handling until its zero-length semantics are planned too.
      if(!vertex->address)return pool_range_fail("missing mandatory vertex address");
      p_bind_buf source={.addr=vertex->address,.size=vertex->size};
      if(!source.size) {
        // The existing binding permits address-only VBOs and exposes the rest
        // of their physical pool. Their finite access bound is unknown: admit
        // this ONE complete logical pool, not every image-only pool in registry.
        vk_pool_span_info p;
        if(!admission || vk_pool_span_query(source.addr,1,&p)==VK_POOL_SPAN_NONE)
          return pool_range_fail("address-only/empty mandatory vertex range");
        source.addr=p.pool_base;source.size=p.pool_size;
      }
      if(!shader_pool_requirement(&source,VK_POOL_REQUIRE_VERTEX,0,
          VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,0,sink,context))return 0;
    }
  if(indexed) {
    if(indexed->count<0 || (!admission && !indexed->count) || !indexed->ibuf || (indexed->itype!=NVN_INDEX_TYPE_UNSIGNED_SHORT &&
        indexed->itype!=NVN_INDEX_TYPE_UNSIGNED_INT))return pool_range_fail("empty/invalid index range/type");
    p_bind_buf source={.addr=indexed->ibuf,
      .size=indexed->count?(uint64_t)indexed->count*(indexed->itype==NVN_INDEX_TYPE_UNSIGNED_SHORT?2:4):1};
    // An empty draw fetches no indices but its unchanged original executor
    // still binds a real buffer at this valid offset. One byte proves that
    // binding's address without making unrelated pools whole.
    if(!shader_pool_requirement(&source,VK_POOL_REQUIRE_INDEX,0,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,0,sink,context))return 0;
  }
  return 1;
}

int vk_graphics_pool_ranges(vk_shader *const list[6],const vk_state *state,
    const p_draw_idx *indexed,vk_pool_requirement_sink sink,void *context) {
  return graphics_pool_ranges(list,state,indexed,sink,context,0);
}
int vk_graphics_pool_admission(vk_shader *const list[6],const vk_state *state,
    const p_draw_idx *indexed,vk_pool_requirement_sink sink,void *context) {
  return graphics_pool_ranges(list,state,indexed,sink,context,1);
}

typedef struct { vk_buffer_use *uses;unsigned count; } compute_pool_requirements;
static int compute_pool_requirement(void *context,const vk_pool_requirement *range) {
  compute_pool_requirements *out=context;
  // Preserve the old compute dependency rule: UBO/SSBO uses represented by an
  // alignment copy are already ordered by that copy. Texel views still name
  // the original pool and must not be hidden by another binding's copy.
  if(range->binding_kind!=7 && range->binding_kind!=9)
    for(unsigned j=0;j<current_operation.storage_copy_count;j++)
      if(current_operation.storage_copies[j].address==range->address &&
          current_operation.storage_copies[j].size==range->size)return 1;
  if(out->count==128)return 0;
  out->uses[out->count++]=(vk_buffer_use){range->address,range->size,range->writable};
  return 1;
}

// Required CPU image sources are prepared at the operation boundary, before
// physical pool admission and all shader/texture/temporary borrows. NVN state
// is stable on the owner; registry descriptors are copied under its own lock.
void vk_shader_cpu_sources(vk_shader *const list[6],const vk_state *state,int compute) {
  int packaged_only=0;
#ifdef DM_VULKAN_SHARED_POOLS
  int shared=!V.options.pool_defer_publication && vk_pool_shared_active();
  packaged_only=!V.options.pool_defer_publication && !shared;
#endif
  if(!V.options.pool_defer_publication
#ifdef DM_VULKAN_SHARED_POOLS
      && !shared && !vk_legacy_astc_copy_mode()
#endif
      )return;
#ifdef DM_VULKAN_NATIVE_RESOURCES
  uint64_t source_epoch=vk_texture_descriptor_epoch();
  image_source_plan *source_plan=image_sources_cpu_empty(list,state,compute);
  int cpu_empty=source_plan!=NULL;
  if(!cpu_empty)source_plan=image_sources_start(list,state,compute);
  int source_complete=1;
source_walk:
#endif
#ifdef DM_VULKAN_NATIVE_RESOURCES
  if(!cpu_empty)
#endif
  for(unsigned stage=compute?5:0;stage<(compute?6:5);stage++)if(list[stage]) {
    const vk_shader *s=list[stage];
#ifdef DM_VULKAN_NATIVE_RESOURCES
    image_source_stage *source_part=image_sources_stage(source_plan,s,state,stage,compute);
    if(source_part)source_part->begin=source_plan->count;
#endif
    for(vk_image_binding_iterator it=vk_image_binding_begin(s);;) {
      unsigned k;if(!vk_image_binding_next(s,&it,&k))break;
      const vk_binding *b=&s->bindings[k];
      if(b->kind!=3 && b->kind!=4 && b->kind!=6)continue;
      if(b->reg<0 || b->reg>=(b->kind==4?32:128))continue;
#ifdef DM_VULKAN_NATIVE_RESOURCES
      if(source_part && (b->kind==3 || b->kind==6) && b->count==1 && source_plan->count<source_plan->expected) {
        // The CPU source walk is the producer: enumerate and decode once, then
        // hand the same logical value to native admission after publication.
        image_source_entry *entry=&source_plan->entries[source_plan->count++];
        entry->reg=b->reg;source_part->count++;
        const tex_t *t=image_source_resolve(source_plan,entry,state,stage);
        if(t && (!packaged_only || (t->has_layout && t->mem && t->L.size)))vk_texture_cpu_preflight(t);
        continue;
      }
      if(source_part)source_complete=0; // Keep this consumer; only reject the handoff.
#endif
      uint64_t handle=b->kind==4?state->img[stage][b->reg]:state->tex[stage][b->reg];
      if(packaged_only)vk_texture_packaged_cpu_preflight(handle&0xfffff);
      else {tex_t t;if(vk_texture_descriptor(handle&0xfffff,&t))vk_texture_cpu_preflight(&t);}
    }
  }
  if(!compute) {
    for(int k=0;k<state->targets.n;k++)vk_texture_cpu_preflight(state->targets.color[k]);
    vk_texture_cpu_preflight(state->targets.depth);
  }
#ifdef DM_VULKAN_NATIVE_RESOURCES
  // Targets can publish CPU effects and re-enter descriptor registration or
  // live S. The pre-target logical proof is not authority after a callback.
  if(cpu_empty && image_sources_cpu_empty(list,state,compute)!=source_plan) {
    cpu_empty=0;source_epoch=vk_texture_descriptor_epoch();
    source_plan=image_sources_start(list,state,compute);goto source_walk;
  }
  if(cpu_empty)source_plan->valid=1;
  else image_sources_finish(source_plan,source_complete,source_epoch);
#endif
}

// Optional host UI replay must never start a CPU demand after an outer draw
// borrowed resources. Unknown image sources simply reject that extra replay.
int vk_shader_cpu_sources_ready(vk_shader *const list[6],const vk_state *state,int compute) {
  int legacy=0;
#ifdef DM_VULKAN_SHARED_POOLS
  legacy=vk_legacy_astc_copy_mode() && !vk_pool_shared_active();
#endif
  if(!V.options.pool_defer_publication && !legacy)return 1;
  for(unsigned stage=compute?5:0;stage<(compute?6:5);stage++)if(list[stage]) {
    const vk_shader *s=list[stage];
    for(vk_image_binding_iterator it=vk_image_binding_begin(s);;) {
      unsigned k;if(!vk_image_binding_next(s,&it,&k))break;
      const vk_binding *b=&s->bindings[k];
      if(b->kind!=3 && b->kind!=4 && b->kind!=6)continue;
      if(b->reg<0 || b->reg>=(b->kind==4?32:128))return 0;
      uint64_t handle=b->kind==4?state->img[stage][b->reg]:state->tex[stage][b->reg];
      tex_t t;if(vk_texture_descriptor(handle&0xfffff,&t) && t.mem && t.L.size) {
        // Optional replay holds an outer operation's borrows. A packaged
        // source can become dirty after a clean entry check; never enter the
        // short, potentially FS-blocking observation phase from that replay.
        if(legacy && t.has_layout && nvn_dirty_tracked(t.mem,t.L.size))return 0;
        if(!vk_pool_cpu_source_ready((uintptr_t)t.mem,t.L.size))return 0;
      }
    }
  }
  if(!compute) {
    for(int k=-1;k<state->targets.n;k++) {
      const tex_t *t=k<0?state->targets.depth:state->targets.color[k];
      if(t && t->mem && t->L.size) {
        if(legacy && t->has_layout && nvn_dirty_tracked(t->mem,t->L.size))return 0;
        if(!vk_pool_cpu_source_ready((uintptr_t)t->mem,t->L.size))return 0;
      }
    }
  }
  return 1;
}
