// One actual render job's fulfilled fixed-function accesses. No GPU command,
// source-publication, visibility transition or CPU ownership operation is delayed.
// Only repeated, unobserved image-history scatters are represented by a run.
typedef struct {
  image_access *cell;
  uint64_t before_use,before_write,use_offset,write_offset;
  unsigned use_changed,write_changed;
} raster_cell;
typedef struct {
  image_request request;
  uint64_t before_generation,generation_step,use_offset;
  size_t first,count;
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  image expected;
  image_access *expected_cells;
#endif
} raster_member;
typedef struct {
  raster_member member[9];
  raster_cell *cells;size_t cell_count;
  unsigned count,valid,building;
  int depth_role; // -1 absent, 0 read, 1 write; cold product, no hot member loop
  uint64_t serial,pass,stride,last_base,pending;
} raster_run;
static raster_run *raster_current;
static void **raster_capture_slot;
static int raster_enabled=-1,raster_guard;
static unsigned raster_depth;
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
static void (*raster_test_callback)(void *);
static void *raster_test_context;
static unsigned raster_test_wait;
void vk_test_raster_after_draws(unsigned count,void (*callback)(void *),void *context) {
  raster_test_wait=count;raster_test_callback=callback;raster_test_context=context;
}
#endif
void vk_texture_raster_draw_enter(void) {
  if(raster_depth++)vk_texture_raster_break();
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  if(raster_depth==1 && raster_test_callback && !raster_test_wait--) {
    if(raster_enabled==1 && (!raster_current || !raster_current->pending))
      die("Raster callback fixture did not reach an unscattered hot run");
    void (*callback)(void *)=raster_test_callback;void *context=raster_test_context;
    raster_test_callback=NULL;raster_test_context=NULL;callback(context);
  }
#endif
}
void vk_texture_raster_draw_leave(void) {if(!raster_depth)die("Raster draw nesting");raster_depth--;}
// Owner-only counters; hot total is accumulated from the existing pending
// run length when scattered, not with another per-draw increment/atomic.
static uint64_t raster_stats[5]; // captures, hot records, scatters, expansions, host oracle
static _Atomic uint64_t raster_published[4];
static _Atomic int raster_published_enabled;
#define RASTER_COUNT(n) (raster_stats[n]++)
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
void vk_test_raster_stats(uint64_t out[5]) {
  memcpy(out,raster_stats,sizeof raster_stats);
  if(raster_current)out[1]+=raster_current->pending;
}
#endif
static void raster_publish(void) {
  if(raster_enabled!=1)return;
  for(unsigned k=0;k<4;k++)atomic_store_explicit(&raster_published[k],
      raster_stats[k]+(k==1 && raster_current?raster_current->pending:0),memory_order_relaxed);
}
void vk_attachment_raster_stats(uint64_t out[4]) {
  for(unsigned k=0;k<4;k++)out[k]=atomic_load_explicit(&raster_published[k],memory_order_relaxed);
}
int vk_attachment_raster_enabled(void) {return atomic_load_explicit(&raster_published_enabled,memory_order_relaxed);}
static int raster_on(void) {
  if(raster_enabled<0) {
    const char *e=getenv("DM_VULKAN_ATTACHMENT_RASTER_RUN");
    if(e && strcmp(e,"0") && strcmp(e,"1"))die("DM_VULKAN_ATTACHMENT_RASTER_RUN expects 0 or 1");
    raster_enabled=e && !strcmp(e,"1");
    atomic_store_explicit(&raster_published_enabled,raster_enabled,memory_order_relaxed);
    LOG("[vulkan attachment raster run] enabled=%d",raster_enabled);
  }
  return raster_enabled;
}
static void raster_scatter(raster_run *g) {
  if(!g->pending)return;
  for(unsigned n=0;n<g->count;n++) {
    raster_member *m=&g->member[n];image *i=m->request.owner;
    i->use_clock=g->last_base+m->use_offset;
    i->access_generation+=g->pending*m->generation_step;
    for(size_t k=m->first;k<m->first+m->count;k++) {
      raster_cell *c=&g->cells[k];
      if(c->use_changed)c->cell->use_clock=g->last_base+c->use_offset;
      if(c->write_changed)c->cell->write_clock=g->last_base+c->write_offset;
    }
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
    image *e=&m->expected;
    if(i->use_clock!=e->use_clock || i->access_generation!=e->access_generation ||
        i->zero_mips!=e->zero_mips || i->read_proof_next!=e->read_proof_next ||
        memcmp(i->read_proofs,e->read_proofs,sizeof i->read_proofs) ||
        i->initialized!=e->initialized || i->cpu_rebuildable!=e->cpu_rebuildable ||
        i->astc_ldr_proven!=e->astc_ldr_proven ||
        memcmp(i->access,m->expected_cells,(size_t)i->levels*i->layers*3*sizeof *i->access))
      die("Raster run original-history differential oracle");
#ifdef DM_VULKAN_SHARED_POOLS
    if(i->shared_alias_linked!=e->shared_alias_linked || i->shared_writer!=e->shared_writer ||
        i->shared_writer_source!=e->shared_writer_source ||
        memcmp(&i->shared_writer_region,&e->shared_writer_region,sizeof i->shared_writer_region))
      die("Raster run shared ownership differential oracle");
#endif
    RASTER_COUNT(4);
#endif
  }
  raster_stats[1]+=g->pending;g->pending=0;RASTER_COUNT(2);
}
void vk_texture_raster_break(void) {
  if(raster_guard || !raster_current)return;
  raster_run *g=raster_current;raster_current=NULL;g->valid=0;
  raster_guard++;
  raster_scatter(g);
  for(unsigned k=0;k<g->count;k++)g->member[k].request.owner->raster_run=NULL;
  // Prospective current/nested draws still need their original attachment
  // requests. Expand BEFORE promotion redirects handles or a callback retires jobs.
  unsigned active=image_binding_active;
  for(unsigned b=0;b<active;b++)if(image_batches[b].raster_group==g) {
    image_batches[b].raster_group=NULL;image_binding_active=b+1;
    for(unsigned k=0;k<g->count;k++) {
      const image_request *q=&g->member[k].request;
      image_request_add_normalized(q->owner,q->range,q->stages,q->access);
      image_bindings[image_binding_count-1].attachment=1;
    }
    RASTER_COUNT(3);
  }
  image_binding_active=active;raster_guard--;
}
static void raster_observe(image *i) {
  if(!raster_guard && i->raster_run)vk_texture_raster_break();
}
void vk_texture_raster_free(void *group) {
  raster_run *g=group;if(!g)return;
  // Recycle can be a completion thread: it must never change recording state.
  if(g->valid || g->pending)die("Raster group reached completion without owner retirement");
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  for(unsigned k=0;k<g->count;k++)free(g->member[k].expected_cells);
#endif
  free(g->cells);free(g);
}
int vk_texture_raster_borrow(void *group,int depth_writable) {
  if(!raster_on() || !group || image_binding_active!=1)return 0;
  raster_run *g=group;
  if(g!=raster_current || !g->valid || g->serial!=vk_recording_serial() ||
      g->pass!=V.passes || !vk_render_active() ||
      image_batch_current.writer_begin!=SIZE_MAX)return 0;
  if(g->depth_role>=0 && g->depth_role!=!!depth_writable) {
    vk_texture_raster_break();return 0;
  }
  image_batch_current.raster_group=g;return 1;
}
void vk_texture_raster_capture(void **group) {
  raster_capture_slot=raster_on()?group:NULL;
}
static void raster_capture_begin(void) {
  void **slot=raster_capture_slot;raster_capture_slot=NULL;
  if(!slot || image_binding_active!=1 || image_batch_current.raster_group ||
      !vk_render_active())return;
  if(raster_current)vk_texture_raster_break();
  raster_run *old=*slot;
  if(old) {vk_texture_raster_free(old);*slot=NULL;}
  // Qualify only the existing request vector. No new all-resource resolver.
  unsigned count=0;size_t cells=0;int seen_attachment=0;
  for(size_t k=0;k<image_binding_count;k++) {
    image_request *q=&image_bindings[k];image *i=q->owner;
    if(!q->attachment) {if(image_writes(q->access) || seen_attachment)return;continue;}
    seen_attachment=1;
    if(count==9 || i->tex.has_layout || i->external_access || !i->initialized ||
        i->astc_copy_encoding || i->bc_copy_decoded)return;
#ifdef DM_VULKAN_NATIVE_RESOURCES
    if(i->native_entry)return;
#endif
    // One owner once: no overlapping-view overwrite order to summarize.
    for(size_t j=0;j<k;j++)if(image_bindings[j].owner==i)return;
    // A later shader read of the same owner is excluded too.
    for(size_t j=k+1;j<image_binding_count;j++)if(image_bindings[j].owner==i)return;
    count++;
    for(unsigned a=0;a<3;a++)if(q->range.aspectMask&(1u<<a))
      cells+=(size_t)q->range.levelCount*q->range.layerCount;
  }
  if(!count)return;
  raster_run *g=calloc(1,sizeof *g);if(!g)die("Raster group allocation");
  g->cells=calloc(cells,sizeof *g->cells);if(!g->cells)die("Raster cells allocation");
  g->serial=vk_recording_serial();g->pass=V.passes;g->building=1;g->depth_role=-1;
  for(size_t k=0;k<image_binding_count;k++)if(image_bindings[k].attachment) {
    image_request q=image_bindings[k];image *i=q.owner;
    raster_member *m=&g->member[g->count++];m->request=q;
    if(q.range.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT)g->depth_role=!!image_writes(q.access);
    m->before_generation=i->access_generation;m->first=g->cell_count;
    for(unsigned a=0;a<3;a++)if(q.range.aspectMask&(1u<<a))
      for(unsigned l=q.range.baseMipLevel;l<q.range.baseMipLevel+q.range.levelCount;l++)
        for(unsigned b=q.range.baseArrayLayer;b<q.range.baseArrayLayer+q.range.layerCount;b++) {
          image_access *c=image_cell(i,a,l,b);
          g->cells[g->cell_count++]=(raster_cell){.cell=c,.before_use=c->use_clock,.before_write=c->write_clock};
          m->count++;
        }
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
    m->expected_cells=malloc((size_t)i->levels*i->layers*3*sizeof *i->access);
    if(!m->expected_cells)die("Raster oracle allocation");
#endif
  }
  *slot=g;raster_current=g;
}
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
// Execute the ORIGINAL history algorithm against its last reference state,
// then restore the still-unscattered optimized state. This is a functional
// oracle only, compiled out of Android; it is never a performance measurement.
static void raster_original_oracle(raster_run *g) {
  image saved[9];image_access *saved_cells[9];uint64_t clock=image_write_clock;
  int merging=image_record_merging;uint64_t raster=image_record_raster;
  raster_guard++;
  for(unsigned n=0;n<g->count;n++) {
    image *i=g->member[n].request.owner;size_t bytes=(size_t)i->levels*i->layers*3*sizeof *i->access;
    saved[n]=*i;saved_cells[n]=malloc(bytes);if(!saved_cells[n])die("Raster oracle snapshot");
    memcpy(saved_cells[n],i->access,bytes);*i=g->member[n].expected;
    memcpy(i->access,g->member[n].expected_cells,bytes);
  }
  image_record_merging=0;image_record_raster=0;
  for(unsigned n=0;n<g->count;n++) {
    image_request *q=&g->member[n].request;
    if(image_writes(q->access))image_record_normalized(q->owner,q->range,0,q->access,0);
  }
  image_record_merging=1;
  for(unsigned w=0;w<2;w++)for(unsigned n=0;n<g->count;n++) {
    image_request *q=&g->member[n].request;
    if(!!image_writes(q->access)!=w)continue;
    image_record_raster=V.passes;image_record_normalized(q->owner,q->range,q->stages,q->access,1);
  }
  if(image_write_clock-clock!=g->stride)die("Raster original clock stride changed");
  for(unsigned n=0;n<g->count;n++) {
    image *i=g->member[n].request.owner;size_t bytes=(size_t)i->levels*i->layers*3*sizeof *i->access;
    g->member[n].expected=*i;memcpy(g->member[n].expected_cells,i->access,bytes);
    *i=saved[n];memcpy(i->access,saved_cells[n],bytes);free(saved_cells[n]);
  }
  image_write_clock=clock;image_record_merging=merging;image_record_raster=raster;raster_guard--;
}
#endif
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
static unsigned raster_interleave_mode,raster_forced_expands;
static image_request raster_fixture_read;
void vk_test_raster_interleave(unsigned mode) {raster_interleave_mode=mode;raster_forced_expands=0;raster_fixture_read=(image_request){0};}
unsigned vk_test_raster_forced_expands(void) {return raster_forced_expands;}
static void raster_test_dependency_interleave(void) {
  if(!raster_interleave_mode)return;
  if(raster_interleave_mode==3) {
    raster_run *g=image_batch_current.raster_group;
    if(!g || g->pending<2)return;
    VkImageLayout layout;
    // The fixture's actual fragment texture is descriptor 1. A real late
    // consumer append must restore readonly-DS ordering, even for another owner.
    if(!vk_texture_view_binding(1,0,VK_SHADER_STAGE_FRAGMENT_BIT,&layout))die("Raster late source view missing");
    if(image_batch_current.raster_group || g->pending)die("Late readonly append retained borrowed raster order");
    raster_forced_expands++;raster_interleave_mode=0;return;
  }
  if(raster_interleave_mode==2 && image_batch_current.raster_group && raster_fixture_read.owner) {
    raster_run *g=image_batch_current.raster_group;
    // Force the real append allocation full without introducing a production
    // late-add event (which correctly expands immediately at its own entry).
    while(image_binding_count<image_binding_capacity)
      image_bindings[image_binding_count++]=raster_fixture_read;
    size_t old=image_binding_capacity;raster_observe(g->member[0].request.owner);
    if(image_binding_capacity<=old)die("Raster expansion fixture did not reallocate full vector");
    raster_forced_expands++;raster_interleave_mode=0;return;
  }
  vk_image_split_break(); // real callback materializes original deferred FS reads
  image_request q={0};int attachment=0;
  for(size_t k=0;k<image_binding_count;k++) {
    if(image_bindings[k].attachment)attachment=1;
    else if(!image_writes(image_bindings[k].access))q=image_bindings[k];
  }
  if(!q.owner)return;
  raster_fixture_read=q;
  if(raster_interleave_mode==1 && attachment) {
    // Same physical shader read, appended after attachments by a callback.
    // It must reject cold qualification, not enter the clock summary.
    image_request_add_normalized(q.owner,q.range,q.stages,q.access);
  }
}
void vk_test_raster_observe(unsigned kind) {
  if(!raster_current || !raster_current->pending)die("Raster observer requires pending actual hot run");
  raster_run *g=raster_current;image_request q=g->member[0].request;
  if(kind==0)(void)image_read_cached(q.owner,q.range);
  else if(kind==1)(void)image_cell(q.owner,0,q.range.baseMipLevel,q.range.baseArrayLayer);
  else die("Unknown raster observer fixture");
  if(raster_current || g->pending)die("Raster history observer failed to materialize");
}
#endif
static void raster_record_done(void) {
  raster_run *g=image_batch_current.raster_group;
  if(g) {
    if(g!=raster_current || !g->valid)die("Raster borrowed group lost owner");
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
    raster_original_oracle(g);
#endif
    g->last_base=image_write_clock;image_write_clock+=g->stride;g->pending++;
    static int first_recorded;
    if(!first_recorded) {first_recorded=1;
      LOG("[vulkan attachment raster run] first actual fulfilled history members=%u",g->count);
    }
    image_batch_current.raster_group=NULL;return;
  }
  g=raster_current;if(!g || !g->building)return;
  uint64_t first=UINT64_MAX;
  for(unsigned n=0;n<g->count;n++)if(g->member[n].request.owner->use_clock<first)
    first=g->member[n].request.owner->use_clock;
  if(!first || first==UINT64_MAX)die("Raster first actual use clock");
  uint64_t base=first-1;g->stride=image_write_clock-base;
  for(unsigned n=0;n<g->count;n++) {
    raster_member *m=&g->member[n];image *i=m->request.owner;
    m->generation_step=i->access_generation-m->before_generation;m->use_offset=i->use_clock-base;
    for(size_t k=m->first;k<m->first+m->count;k++) {
      raster_cell *c=&g->cells[k];c->use_changed=c->cell->use_clock!=c->before_use;
      c->write_changed=c->cell->write_clock!=c->before_write;
      if(c->use_changed)c->use_offset=c->cell->use_clock-base;
      if(c->write_changed)c->write_offset=c->cell->write_clock-base;
    }
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
    m->expected=*i;memcpy(m->expected_cells,i->access,(size_t)i->levels*i->layers*3*sizeof *i->access);
#endif
    i->raster_run=g;
  }
  g->building=0;g->valid=1;RASTER_COUNT(0);
}
#undef RASTER_COUNT
