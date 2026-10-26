// Experimental bounded color-clear transaction. No shader/game identities,
// command reordering across consumers, new submits or per-access atomics.
static atomic_int mip_clear_requested=-1;
static int mip_clear_enabled,mip_clear_flushing;
static struct {
  vk_mip_clear_plan plan;
  p_set_rt original;
  tex_t textures[2];
  VkImage handles[2];
  render_job *job;
  uint64_t serial,epoch;
  unsigned issued,next_op,returned;
} mip_clear;
static struct { uint64_t admitted,materialized,continued,rejected; } mip_clear_recorded;
static atomic_uint_fast64_t mip_clear_admitted,mip_clear_materialized,mip_clear_continued,mip_clear_rejected;
static atomic_int mip_clear_active;
void vk_mip_clear_enable(int enabled) { atomic_store(&mip_clear_requested,!!enabled); }
void vk_mip_clear_stats(uint64_t *admitted,uint64_t *materialized,uint64_t *continued,
                        uint64_t *rejected,int *enabled) {
  *admitted=atomic_load(&mip_clear_admitted);*materialized=atomic_load(&mip_clear_materialized);
  *continued=atomic_load(&mip_clear_continued);*rejected=atomic_load(&mip_clear_rejected);
  *enabled=atomic_load(&mip_clear_active);
}
int vk_mip_clear_pending(void) { return mip_clear.issued!=0 && !mip_clear_flushing; }
static int mip_clear_overlap(uint64_t a,uint64_t an,uint64_t b,uint64_t bn) {
  if(!a || !b || !an || !bn)return 0;
  if(an>UINT64_MAX-a || bn>UINT64_MAX-b)return 1;
  return a<b?b-a<an:a-b<bn;
}
void vk_mip_clear_drain(void) { if(vk_mip_clear_pending())vk_render_end(); }
void vk_mip_clear_buffer_access(uint64_t address,uint64_t size) {
  if(!vk_mip_clear_pending() || !size)return;
  for(unsigned k=0;k<2;k++)if(mip_clear_overlap(address,size,
      (uintptr_t)mip_clear.textures[k].mem,mip_clear.textures[k].L.size)) {
    vk_mip_clear_drain();return;
  }
}
void vk_mip_clear_texture_access(const tex_t *t,const view_t *v,int attachment) {
  if(!vk_mip_clear_pending())return;
  if(!t || !t->serial || !t->mem || !t->L.size) { vk_mip_clear_drain();return; }
  for(unsigned k=0;k<2;k++) {
    const tex_t *q=&mip_clear.textures[k];
    if(t->serial!=q->serial && !mip_clear_overlap((uintptr_t)t->mem,t->L.size,(uintptr_t)q->mem,q->L.size))continue;
    // Only exact original attachment mips are allowed to continue. Sampled,
    // raw, aliased or unknown consumers drain before any resource preparation.
    if(attachment && t->serial==q->serial && t->mem==q->mem &&
        !memcmp(&t->d,&q->d,sizeof t->d) && !memcmp(&t->L,&q->L,sizeof t->L)) {
      int level=v&&v->set_levels?v->base_level:0,disjoint=1;
      if(level<0 || level>=t->d.levels || (v && ((v->set_levels&&v->nlevels!=1)||
          (v->set_format&&v->format!=t->d.format)||
          (v->set_target&&v->target!=NVN_TEXTURE_TARGET_2D)||
          (v->set_layers&&(v->min_layer||v->nlayers!=1)))))disjoint=0;
      for(unsigned n=0;n<mip_clear.issued;n++) {
        const vk_mip_clear_record *r=&mip_clear.plan.records[n];
        int target=r->clear.index;
        if(r->targets.color[target]->serial==t->serial &&
            r->targets.cview[target].base_level==level)disjoint=0;
      }
      if(disjoint)return;
    }
    vk_mip_clear_drain();return;
  }
}
static int mip_clear_state_opcode(unsigned opcode) {
  switch(opcode) {
  case OP_SCISSOR:case OP_VIEWPORTS:case OP_DEPTH_RANGE:case OP_BIND_PROGRAM:
  case OP_BIND_STATE:case OP_BIND_VATTR:case OP_BIND_VSTREAM:case OP_BIND_VBUF:
  case OP_BIND_UBO:case OP_BIND_SSBO:case OP_BIND_TEX:case OP_BIND_SAMP:case OP_BIND_IMG:
  case OP_STENCIL:case OP_POLY_OFFSET:case OP_BLEND_COLOR:return 1;
  default:return 0;
  }
}
void vk_mip_clear_replay(const uint8_t *op,unsigned opcode) {
  if(!vk_mip_clear_pending())return;
  if(!vk_replay_preserves_ops() || mip_clear.serial!=vk_recording_serial() ||
      mip_clear.plan.generation!=vk_pass_generation() ||
      mip_clear.epoch!=vk_texture_descriptor_epoch() || active_job!=mip_clear.job) {
    vk_mip_clear_drain();return;
  }
  if(!mip_clear.returned) {
    if(mip_clear.next_op>=mip_clear.plan.op_count ||
        mip_clear.plan.ops[mip_clear.next_op++]!=op) { vk_mip_clear_drain();return; }
    if(mip_clear.next_op==mip_clear.plan.op_count) {
      if(mip_clear.issued!=mip_clear.plan.record_count || opcode!=OP_SET_RT) { vk_mip_clear_drain();return; }
      mip_clear.returned=1;
    }
    return;
  }
  if(mip_clear_state_opcode(opcode))return;
  if((opcode==OP_DRAW || opcode==OP_DRAW_IDX) &&
      target_fields_equal(&S.targets,&mip_clear.original))return;
  if(opcode==OP_SET_RT && !memcmp(op+8,&mip_clear.original,sizeof mip_clear.original))return;
  vk_mip_clear_drain();
}
// Called before shader variants, zero-content queries or any binding work.
static void mip_clear_before_draw(vk_shader *const list[6]) {
  if(!vk_mip_clear_pending())return;
  if(!mip_clear.returned || active_job!=mip_clear.job ||
      !target_fields_equal(&S.targets,&mip_clear.original) || !vk_replay_preserves_ops() || !vk_tbdr_enabled() ||
      !vk_ghost_idle()) { vk_mip_clear_drain();return; }
  for(unsigned st=0;st<5;st++)if(list[st] && list[st]->has_resource_writes) {
    vk_mip_clear_drain();return;
  }
}
int vk_mip_clear_try(const p_set_rt *rt,const p_scissor *sc,const p_clear_color *clear) {
  if(V.options.render_record_mode)return 0;
  if(mip_clear_flushing || !mip_clear_enabled || !vk_tbdr_enabled())return 0;
  if(!vk_mip_clear_pending()) {
    if(!active_job || !active_job->first_draw || !active_job->secondary || V.cmd!=active_job->secondary ||
        vk_submission_barrier_mode()!=0 || !vk_replay_preserves_ops() ||
        vk_counter_active() || !vk_ghost_idle() || active_targets.n!=2 ||
        !active_targets.color[0] || !active_targets.color[1])return 0;
    vk_mip_clear_plan proof;
    if(!vk_pass_mip_clear_plan(&active_targets,rt,sc,&proof))return 0;
    if(proof.record_count!=4)return 0; // Also bounds the local four-view batch.
    tex_t textures[2]={*active_targets.color[0],*active_targets.color[1]};
    if(!textures[0].serial || !textures[1].serial || !textures[0].mem || !textures[1].mem ||
        !textures[0].L.size || !textures[1].L.size || textures[0].serial==textures[1].serial ||
        textures[0].d.width!=textures[1].d.width || textures[0].d.height!=textures[1].d.height ||
        textures[0].d.levels!=textures[1].d.levels ||
        mip_clear_overlap((uintptr_t)textures[0].mem,textures[0].L.size,
                          (uintptr_t)textures[1].mem,textures[1].L.size))return 0;
    VkImage handles[2]={0};
    for(unsigned n=0;n<proof.record_count;n++) {
      vk_mip_clear_record *r=&proof.records[n];
      if(r->targets.cview[0].base_level!=r->targets.cview[1].base_level)return 0;
      // The complete clear-only plan forbids every depth/stencil consumer.
      // Temporary DS bindings may differ; deferred physical jobs omit DS.
      // Original targets (including DS) must still be restored exactly.
      for(unsigned k=0;k<2;k++) {
        const tex_t *t=r->targets.color[k],*a=active_targets.color[k];
        const view_t *v=r->targets.has_cview[k]?&r->targets.cview[k]:NULL;
        const view_t *av=active_targets.has_cview[k]?&active_targets.cview[k]:NULL;
        if(!t || t->serial!=a->serial || t->mem!=a->mem || memcmp(&t->d,&a->d,sizeof t->d) ||
            memcmp(&t->L,&a->L,sizeof t->L) || t->d.levels<2 || t->d.levels>16 ||
            !v || !v->set_levels || v->nlevels!=1 || v->base_level<0 || v->base_level>=t->d.levels ||
            (av && av->set_levels && av->nlevels!=1) ||
            v->base_level==(av&&av->set_levels?av->base_level:0) ||
            (active_targets.depth && mip_clear_overlap((uintptr_t)t->mem,t->L.size,
                 (uintptr_t)active_targets.depth->mem,active_targets.depth->L.size)))return 0;
      }
      int level=r->targets.cview[r->clear.index].base_level;
      unsigned width=textures[r->clear.index].d.width>>level,height=textures[r->clear.index].d.height>>level;
      if(!width)width=1;if(!height)height=1;
      // Match full_clear and the real clear's image-bound clipping. Promote
      // before adding signed extents, retaining empty/partial fallbacks.
      if(r->has_scissor && (r->scissor.x>0 || r->scissor.y>0 ||
          (int64_t)r->scissor.x+r->scissor.w<width ||
          (int64_t)r->scissor.y+r->scissor.h<height))return 0;
    }
    // These records have the same two exact allocations. No resource call or
    // callback intervenes in this local proof: check all views/cells, but scan
    // allocation aliases only once per image. Every replayed clear below still
    // performs the original complete one-view admission again.
    for(unsigned k=0;k<2;k++) {
      const view_t *views[4];
      for(unsigned n=0;n<proof.record_count;n++)views[n]=&proof.records[n].targets.cview[k];
      if(!vk_texture_mip_clear_admit_views(&textures[k],views,proof.record_count,&handles[k]))return 0;
    }
    memset(&mip_clear,0,sizeof mip_clear);mip_clear.plan=proof;mip_clear.original=active_targets;
    memcpy(mip_clear.textures,textures,sizeof textures);memcpy(mip_clear.handles,handles,sizeof handles);
    for(unsigned n=0;n<proof.record_count;n++)for(unsigned k=0;k<2;k++)
      mip_clear.plan.records[n].targets.color[k]=&mip_clear.textures[k];
    mip_clear.job=active_job;mip_clear.serial=vk_recording_serial();
    mip_clear.epoch=vk_texture_descriptor_epoch();mip_clear.next_op=1;
  }
  const uint8_t *op=NULL;
  if(!vk_pass_future_op(0,&op,NULL) || mip_clear.issued>=mip_clear.plan.record_count ||
      op!=mip_clear.plan.records[mip_clear.issued].op) { vk_mip_clear_drain();return 0; }
  const vk_mip_clear_record *r=&mip_clear.plan.records[mip_clear.issued];
  int matches=rt->n==2 && (!!sc)==r->has_scissor &&
      (!sc || !memcmp(sc,&r->scissor,sizeof *sc));
  for(unsigned k=0;k<2 && matches;k++)matches=rt->color[k] &&
      rt->color[k]->serial==r->targets.color[k]->serial &&
      rt->color[k]->mem==r->targets.color[k]->mem &&
      !memcmp(&rt->color[k]->d,&r->targets.color[k]->d,sizeof rt->color[k]->d) &&
      !memcmp(&rt->color[k]->L,&r->targets.color[k]->L,sizeof rt->color[k]->L) &&
      rt->has_cview[k]==r->targets.has_cview[k] &&
      !memcmp(&rt->cview[k],&r->targets.cview[k],sizeof rt->cview[k]);
  if(!matches) { mip_clear_recorded.rejected++;vk_mip_clear_drain();return 0; }
  VkImage handle;
  if(!vk_texture_mip_clear_admit(rt->color[clear->index],
      rt->has_cview[clear->index]?&rt->cview[clear->index]:NULL,&handle) ||
      handle!=mip_clear.handles[clear->index] || memcmp(&r->clear,clear,sizeof *clear)) {
    mip_clear_recorded.rejected++;vk_mip_clear_drain();return 0;
  }
  if(!mip_clear.issued) {
    vk_ubo_plan_break();mip_clear_recorded.admitted++;
  }
  mip_clear.issued++;
  vk_texture_mip_clear_mark(rt->color[clear->index],
      rt->has_cview[clear->index]?&rt->cview[clear->index]:NULL);
  V.nvn_barrier|=NVN_BARRIER_INVALIDATE_TEXTURE_BIT;
  return 1;
}
static void mip_clear_materialize(void) {
  if(!mip_clear.issued || mip_clear_flushing)return;
  if(active_job)die("Vulkan deferred mip clear materialized inside render pass");
  unsigned count=mip_clear.issued;mip_clear.issued=0;mip_clear_flushing=1;
  // Original plan position cannot justify discarding stores of postponed ops.
  for(unsigned n=0;n<count;n++) {
    const vk_mip_clear_record *r=&mip_clear.plan.records[n];p_set_rt rt=r->targets;
    rt.depth=NULL;rt.has_dview=0;memset(&rt.dview,0,sizeof rt.dview);
    vk_target_clear_color(&rt,r->has_scissor?&r->scissor:NULL,&r->clear);
    if(active_job)active_job->preserve_stores=1;
    mip_clear_recorded.materialized++;
  }
  vk_render_end();
  memset(&mip_clear,0,sizeof mip_clear);mip_clear_flushing=0;
}
static void mip_clear_begin(void) {
  if(mip_clear.issued || mip_clear_flushing)die("Vulkan deferred mip clear crossed submission");
  int requested=atomic_load_explicit(&mip_clear_requested,memory_order_relaxed);
  mip_clear_enabled=requested<0?V.options.defer_mip_clears:requested;
  atomic_store_explicit(&mip_clear_active,mip_clear_enabled,memory_order_relaxed);
}
static void mip_clear_publish(void) {
  atomic_store_explicit(&mip_clear_admitted,mip_clear_recorded.admitted,memory_order_relaxed);
  atomic_store_explicit(&mip_clear_materialized,mip_clear_recorded.materialized,memory_order_relaxed);
  atomic_store_explicit(&mip_clear_continued,mip_clear_recorded.continued,memory_order_relaxed);
  atomic_store_explicit(&mip_clear_rejected,mip_clear_recorded.rejected,memory_order_relaxed);
}
