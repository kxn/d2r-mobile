// Frozen pre-fusion forward interpreter, owned host oracle only.
static void store_index_build_reference(void) {
  index_valid=1;built_epoch=vk_texture_descriptor_epoch();bucket_count=event_count=global_count=alias_count=0;
  int reference=atomic_load(&descriptor_reference_requested);
  if(reference<0) {
    const char *setting=getenv("DM_VULKAN_STORE_DESCRIPTOR_REFERENCE");
    if(setting && strcmp(setting,"0") && strcmp(setting,"1")) die("DM_VULKAN_STORE_DESCRIPTOR_REFERENCE expects 0 or 1");
    reference=setting&&!strcmp(setting,"1");atomic_store(&descriptor_reference_requested,reference);
  }
  build_descriptor_reference=reference;atomic_store(&descriptor_reference_active,reference);
  build_descriptor_hits=build_descriptor_misses=0;
  build_slot_stages=build_slot_skipped=build_duplicate_reads=0;
  if(!reference) memset(descriptor_buckets,0,sizeof descriptor_buckets);
  memset(bucket_hash,0,sizeof bucket_hash);
  p_set_rt rt=S.targets;p_scissor sc=S.scissor;int has_sc=S.has_scissor;
  int target_buckets[9];for(unsigned k=0;k<9;k++) target_buckets[k]=-1;
  const prog_t *program=S.program;uint64_t slots[6][160];uint8_t known_zero[6]={0};
  for(unsigned st=0;st<6;st++) {
    memcpy(slots[st],S.tex[st],sizeof S.tex[st]);memcpy(slots[st]+128,S.img[st],sizeof S.img[st]);
  }
  for(unsigned seq=0;seq<count && index_valid;seq++) {
    unsigned opcode=plan[seq].opcode;const void *a=plan[seq].op+8;
    switch(opcode) {
    case OP_SET_RT:
      rt=*(const p_set_rt *)a;if(rt.n<0||rt.n>8) index_valid=0;
      for(unsigned k=0;k<9;k++) target_buckets[k]=-1;
      break;
    case OP_SCISSOR:sc=*(const p_scissor *)a;has_sc=1;break;
    case OP_BIND_PROGRAM:program=((const p_bind_program *)a)->prog;break;
    case OP_BIND_TEX:case OP_BIND_IMG: {
      const p_bind_handles *b=a;int base=opcode==OP_BIND_IMG?128:0,limit=opcode==OP_BIND_IMG?32:128;
      if(b->stage<0||b->stage>=6||b->first<0||b->n<0||b->n>limit-b->first) { global_need(seq);break; }
      // Only unknown shaders need the whole-stage empty proof. Invalidate it
      // cheaply on a binding write; known-shader binds need no per-slot count.
      if(b->n) known_zero[b->stage]=0;
      // Single descriptors dominate the real command stream. Keep this copy
      // as a load/store rather than entering libc for a dynamic eight bytes.
      if(b->n==1)slots[b->stage][base+b->first]=b->h[0];
      else memcpy(slots[b->stage]+base+b->first,b->h,b->n*8);
      break;
    }
    case OP_DRAW:case OP_DRAW_IDX:case OP_DISPATCH: {
      vk_shader *shaders[6]={0};if(program) vk_program_shaders(program,shaders);
      for(unsigned st=0;st<6;st++) {
        if((opcode==OP_DISPATCH)!=(st==5)) continue;
        vk_shader *s=shaders[st];
        // The conservative unknown-stage scan produces no events when every
        // handle is zero. Retain it for any populated stage, including stale
        // handles; this preserves the reference's exact dependency decisions.
        if(!reference && !s && known_zero[st]) continue;
        unsigned n=s?(!reference?s->image_read_count:s->nb):160;
        int nonzero=0;
        if(!reference && s) { build_slot_stages++;build_slot_skipped+=s->nb-s->image_read_count; }
        for(unsigned i=0;i<n;i++) {
          int slot=i;
          if(s && !reference) slot=s->image_read_slots[i];
          else if(s) {
            const vk_binding *b=&s->bindings[i];
            if(b->kind==4||b->kind==9) slot=128+b->reg;
            else if(b->kind==3||b->kind==6||b->kind==7) slot=b->reg;
            else continue;
          }
          if(slot<0||slot>=160) { global_need(seq);continue; }
          uint64_t handle=slots[st][slot];if(!handle) continue;
          nonzero=1;
          int b=descriptor_bucket(handle&0xfffff);
          if(b<0) global_need(seq);
          else bucket_event_add(seq,b,NULL,IX_READ,0,0);
        }
        // Learn emptiness during the required conservative scan, never by a
        // second pass. Nonempty unknown stages still emit reads at every seq.
        if(!reference && !s && !nonzero) known_zero[st]=1;
      }
      if(opcode!=OP_DISPATCH) {
        if(rt.n<0||rt.n>8) { global_need(seq);break; }
        for(int k=0;k<rt.n;k++) attachment_event_add(seq,&target_buckets[k],rt.color[k],rt.has_cview[k]?&rt.cview[k]:NULL,IX_ATT,0,0);
        attachment_event_add(seq,&target_buckets[8],rt.depth,rt.has_dview?&rt.dview:NULL,IX_ATT,0,0);
      }
      break;
    }
    case OP_CLEAR_COLOR: {
      const p_clear_color *c=a;
      if(rt.n<0||rt.n>8||c->index<0||c->index>=rt.n) { global_need(seq);break; }
      const tex_t *t=rt.color[c->index];const view_t *v=rt.has_cview[c->index]?&rt.cview[c->index]:NULL;
      if(t) attachment_event_add(seq,&target_buckets[c->index],t,v,IX_COLOR,c->mask&15,full_clear(t,v,&sc,has_sc));
      break;
    }
    case OP_CLEAR_DS: {
      const p_clear_ds *c=a;const view_t *v=rt.has_dview?&rt.dview:NULL;
      if(rt.depth) attachment_event_add(seq,&target_buckets[8],rt.depth,v,IX_DS,(c->depth_mask?1:0)|((c->stencil_mask&255)==255?2:0),full_clear(rt.depth,v,&sc,has_sc));
      break;
    }
    case OP_COPY_T2T: {
      const p_copy_t2t *c=a;event_add(seq,c->src,NULL,IX_READ,0,0);event_add(seq,c->dst,NULL,IX_READ,0,0);break;
    }
    case OP_COPY_T2B:case OP_COPY_B2T:event_add(seq,((const p_copy_b2t *)a)->tex,NULL,IX_READ,0,0);break;
    case OP_CLEAR_TEX:event_add(seq,((const p_clear_tex *)a)->tex,NULL,IX_READ,0,0);break;
    case OP_SET_TEX_POOL:case OP_SET_SAMP_POOL:global_need(seq);break;
    default:break;
    }
  }
  atomic_fetch_add(&descriptor_cache_hits,build_descriptor_hits);
  atomic_fetch_add(&descriptor_cache_misses,build_descriptor_misses);
  atomic_fetch_add(&slot_cache_stages,build_slot_stages);
  atomic_fetch_add(&slot_cache_skipped_bindings,build_slot_skipped);
  atomic_fetch_add(&recorded_events,event_count);
  atomic_fetch_add(&duplicate_reads,build_duplicate_reads);
  if(!index_valid || built_epoch!=vk_texture_descriptor_epoch()) { index_valid=0;return; }
  uint32_t write[4096],order[4096];
  for(unsigned b=0,offset=0;b<bucket_count;b++) {
    unsigned n=buckets[b].end;
    buckets[b].begin=write[b]=offset;offset+=n;buckets[b].end=offset;order[b]=b;
  }
  for(unsigned i=0;i<event_count;i++) sorted_events[write[events[i].bucket]++]=events[i];
  qsort(order,bucket_count,sizeof *order,bucket_address_compare);
  for(unsigned i=0;i<bucket_count && index_valid;i++) {
    unsigned b=order[i];const vk_texture_footprint *t=&buckets[b].texture;uintptr_t x=t->base;if(!x) continue;
    for(unsigned j=i+1;j<bucket_count && index_valid;j++) {
      unsigned other=order[j];uintptr_t y=buckets[other].texture.base;
      if(y>x && y-x>=t->size) break;
      if(bucket_footprints_overlap(t,&buckets[other].texture)) { alias_add(b,other);alias_add(other,b); }
    }
  }
  if(index_valid) atomic_fetch_add(&index_stats.builds,1);
}
