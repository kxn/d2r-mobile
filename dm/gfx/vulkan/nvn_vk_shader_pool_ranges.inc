// One logical resource enumeration for actual compute dependencies and future
// physical-backing preflight. Do not acquire VkBuffer/descriptor/temp handles.
static int shader_pool_requirement(const p_bind_buf *buffer,int kind,int writable,
    VkPipelineStageFlags stages,uint64_t prefix_alignment,
    vk_pool_requirement_sink sink,void *context) {
  if(!buffer->addr || !buffer->size)return 1; // existing zero-buffer fallback
  if(buffer->size>UINT64_MAX-buffer->addr)return 0;
  vk_pool_requirement range={buffer->addr,buffer->size,prefix_alignment,stages,writable,kind};
  return sink(context,&range);
}
int vk_shader_pool_ranges_state(vk_shader *const list[6],const vk_state *state,int compute,
    vk_pool_requirement_sink sink,void *context) {
  if(!list || !state || !sink)return 0;
  for(unsigned stage=compute?5:0;stage<(compute?6:5);stage++)if(list[stage]) {
    const vk_shader *s=list[stage];
    if(s->stage!=stage || s->nb>128 || (s->nb&&!s->bindings))return 0;
    for(unsigned k=0;k<s->nb;k++) {
      const vk_binding *b=&s->bindings[k];
      if(b->count!=1)return 0;
      const p_bind_buf *buffer=NULL;uint64_t prefix=0;
      if(b->kind==1) {
        if(b->reg<0 || b->reg>=19)return 0;
        // c0/c1/c2 are host-owned driver/embedded constants. Ordinary UBOs
        // still resolve their FULL declared size before alignment/copy logic;
        // descriptor.range=min(size,64KiB) is not the lookup's range.
        if(b->reg>=3)buffer=&state->ubo[stage][b->reg-3];
      } else if(b->kind==2 || b->kind==8) {
        if(b->reg<0 || b->reg>=16)return 0;
        buffer=b->kind==8?&state->ubo[stage][b->reg]:&state->ssbo[stage][b->reg];
        unsigned bias=(unsigned)b->reg+(b->kind==8?16:0);
        if(s->storage_bias_mask&(UINT32_C(1)<<bias))
          prefix=V.props.limits.minStorageBufferOffsetAlignment;
      } else if(b->kind==7 || b->kind==9) {
        if(b->reg<0 || b->reg>=(b->kind==9?32:128))return 0;
        uint64_t handle=b->kind==9?state->img[stage][b->reg]:state->tex[stage][b->reg];
        tex_t texture;
        if(!vk_texture_descriptor(handle&0xfffff,&texture) ||
            texture.d.target!=NVN_TEXTURE_TARGET_BUFFER)return 0;
        p_bind_buf texel={.addr=(uintptr_t)texture.mem,.size=texture.L.size};
        if(!shader_pool_requirement(&texel,b->kind,vk_binding_writable(b),
            producer_stage(stage),0,sink,context))return 0;
      } else if(b->kind!=0 && (b->kind<3 || b->kind>6)) {
        // Optional texel-storage image alternatives need their selected
        // physical-image recipe, not a guessed NVN buffer extent. An unknown
        // representation cannot prove complete window requirements.
        return 0;
      }
      if(buffer && !shader_pool_requirement(buffer,b->kind,vk_binding_writable(b),
          producer_stage(stage),prefix,sink,context))return 0;
    }
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
int vk_graphics_pool_ranges(vk_shader *const list[6],const vk_state *state,
    const p_draw_idx *indexed,vk_pool_requirement_sink sink,void *context) {
  if(!list || !list[0] || !state || !sink)return 0;
  if(!vk_shader_pool_ranges_state(list,state,0,sink,context))return 0;
  unsigned seen=0;
  for(unsigned attribute=0;attribute<16;attribute++)
    if((list[0]->input_mask&(1u<<attribute)) && state->attr[attribute].fmt) {
      unsigned stream=state->attr[attribute].stream;
      if(stream>=16)return 0;
      if(seen&(1u<<stream))continue;
      seen|=1u<<stream;
      const NVNbufferRange *vertex=&state->vertex[stream];
      // Unlike optional shader bindings, mandatory vertex bindings have no
      // zero-buffer fallback. An address-only binding needs conservative
      // whole-pool handling until its zero-length semantics are planned too.
      if(!vertex->address || !vertex->size)return 0;
      p_bind_buf source={.addr=vertex->address,.size=vertex->size};
      if(!shader_pool_requirement(&source,VK_POOL_REQUIRE_VERTEX,0,
          VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,0,sink,context))return 0;
    }
  if(indexed) {
    if(indexed->count<=0 || !indexed->ibuf || (indexed->itype!=NVN_INDEX_TYPE_UNSIGNED_SHORT &&
        indexed->itype!=NVN_INDEX_TYPE_UNSIGNED_INT))return 0;
    p_bind_buf source={.addr=indexed->ibuf,
      .size=(uint64_t)indexed->count*(indexed->itype==NVN_INDEX_TYPE_UNSIGNED_SHORT?2:4)};
    if(!shader_pool_requirement(&source,VK_POOL_REQUIRE_INDEX,0,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,0,sink,context))return 0;
  }
  return 1;
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
