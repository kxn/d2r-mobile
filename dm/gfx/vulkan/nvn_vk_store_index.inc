// 原 sdx 的按纹理分桶/稳定散列；别名走独立邻接表，未知读取保守作全局需求。
// Upstream sdx-style per-texture buckets and stable scatter. Aliases have their
// own adjacency lists; unknown reads conservatively require every attachment.
enum { IX_READ=1,IX_ATT,IX_COLOR,IX_DS };
// Keep full-width sequence and signed view coordinates. Buckets are bounded
// below, and kind/full need only three/one bits. The two event arrays now copy
// 16 bytes per event instead of 20 without changing their ordering or proof.
typedef struct {
  uint32_t seq;
  int32_t level,layer;
  uint16_t bucket;
  uint8_t kind:3,full:1;
  uint8_t bits;
} store_event;
_Static_assert(sizeof(store_event)==16,"Vulkan store event compact layout");
_Static_assert(IX_DS<8,"Vulkan store event kind width");
// This index only consumes serial and memory overlap, never texture layout or
// dimensions. Keep a value snapshot of that exact footprint; full_clear and
// the independent reference scan continue to use the original tex_t.
typedef struct { vk_texture_footprint texture;uint32_t begin,end,aliases,last_read; } store_bucket;
_Static_assert(sizeof(store_bucket)==40,"Vulkan compact store dependency bucket");
typedef struct { uint32_t other,next; } store_alias;
static store_bucket buckets[4096];
_Static_assert(sizeof buckets/sizeof *buckets<=UINT16_MAX,"Vulkan store event bucket width");
static uint16_t bucket_hash[8192];
static unsigned bucket_count,event_count,event_capacity,global_count,global_capacity,alias_count,alias_capacity;
static store_event *events,*sorted_events;
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
static int store_walk_fail_allocation;
#define STORE_INDEX_REALLOC(p,n) (store_walk_fail_allocation?NULL:realloc((p),(n)))
#else
#define STORE_INDEX_REALLOC(p,n) realloc((p),(n))
#endif
static uint32_t *globals;
static store_alias *aliases;
static uint64_t built_epoch;
static struct { uint32_t key;int32_t bucket; } descriptor_buckets[8192];
static _Atomic int descriptor_reference_requested=-1,descriptor_reference_active;
static _Atomic uint64_t descriptor_cache_hits,descriptor_cache_misses;
static uint64_t build_descriptor_hits,build_descriptor_misses;
static _Atomic uint64_t slot_cache_stages,slot_cache_skipped_bindings;
static uint64_t build_slot_stages,build_slot_skipped;
static uint64_t build_duplicate_reads;
static _Atomic uint64_t recorded_events,duplicate_reads;
static int build_descriptor_reference;
void vk_store_event_stats(uint64_t *recorded,uint64_t *duplicates) {
  *recorded=atomic_load(&recorded_events);*duplicates=atomic_load(&duplicate_reads);
}
void vk_store_descriptor_reference(int reference) {
  atomic_store(&descriptor_reference_requested,!!reference);
}
void vk_store_descriptor_stats(uint64_t *hits,uint64_t *misses,int *reference) {
  *hits=atomic_load(&descriptor_cache_hits);*misses=atomic_load(&descriptor_cache_misses);
  *reference=atomic_load(&descriptor_reference_active);
}
void vk_store_slot_stats(uint64_t *stages,uint64_t *skipped) {
  *stages=atomic_load(&slot_cache_stages);*skipped=atomic_load(&slot_cache_skipped_bindings);
}
static int bucket_for_footprint(const vk_texture_footprint *t,int add) {
  if(!t) return -1;
  if(!t->serial) { index_valid=0;return -1; }
  unsigned h=(unsigned)(t->serial*UINT64_C(0x9e3779b97f4a7c15)>>51);
  for(unsigned n=0;n<8192;n++,h=(h+1)&8191) {
    unsigned slot=bucket_hash[h];
    if(slot && buckets[slot-1].texture.serial==t->serial) return slot-1;
    if(!slot) {
      if(!add) return -1;
      if(bucket_count==4096) { index_valid=0;return -1; }
      unsigned b=bucket_count++;buckets[b]=(store_bucket){.texture=*t,.aliases=UINT32_MAX,.last_read=UINT32_MAX};
      bucket_hash[h]=b+1;return b;
    }
  }
  index_valid=0;return -1;
}
static int bucket_for(const tex_t *t,int add) {
  if(!t)return -1;
  if(!t->serial) { index_valid=0;return -1; }
  vk_texture_footprint footprint={t->serial,(uintptr_t)t->mem,t->L.size};
  return bucket_for_footprint(&footprint,add);
}
static int descriptor_bucket(unsigned id) {
  unsigned key=id+1,slot=(id*UINT32_C(2654435761))>>19;
  unsigned cache_slot=8192;
  if(!build_descriptor_reference) for(unsigned n=0;n<8192;n++,slot=(slot+1)&8191) {
    if(descriptor_buckets[slot].key==key) {
      build_descriptor_hits++;return descriptor_buckets[slot].bucket;
    }
    if(!descriptor_buckets[slot].key) { cache_slot=slot;break; }
  }
  build_descriptor_misses++;
  vk_texture_footprint texture;
  int bucket=vk_texture_descriptor_footprint(id,&texture)?bucket_for_footprint(&texture,1):-1;
  // Valid only within this index build. If descriptors change concurrently,
  // the existing epoch check discards the whole index, including this cache.
  // Cache failures too; a subsequent registration changes that same epoch.
  if(cache_slot<8192) descriptor_buckets[cache_slot]=(typeof(*descriptor_buckets)){key,bucket};
  return bucket;
}
static void global_need(unsigned seq) {
  if(global_count && globals[global_count-1]==seq) return;
  if(global_count==global_capacity) {
    unsigned n=global_capacity?global_capacity*2:256;uint32_t *p=STORE_INDEX_REALLOC(globals,n*sizeof *p);
    if(!p) { index_valid=0;return; }globals=p;global_capacity=n;
  }
  globals[global_count++]=seq;
}
static void bucket_event_add(unsigned seq,int b,const view_t *v,unsigned kind,unsigned bits,int full) {
  if(!index_valid || b<0) return;
  // A read conservatively covers the whole texture. Different slots/stages
  // reading it in this SAME operation have the same earliest-use position,
  // including for every guest-memory alias. Never merge different sequences:
  // an alias read before a full clear must not move past that clear.
  if(kind==IX_READ) {
    if(buckets[b].last_read==seq) { build_duplicate_reads++;return; }
    buckets[b].last_read=seq;
  }
  if(event_count==1000000) { index_valid=0;return; }
  if(event_count==event_capacity) {
    unsigned n=event_capacity?event_capacity*2:4096;
    store_event *p=STORE_INDEX_REALLOC(events,n*sizeof *p);if(!p) { index_valid=0;return; }events=p;
    p=STORE_INDEX_REALLOC(sorted_events,n*sizeof *p);if(!p) { index_valid=0;return; }sorted_events=p;event_capacity=n;
  }
  events[event_count++]=(store_event){.seq=seq,.bucket=b,.level=level(v),
    .layer=layer(v),.kind=kind,.bits=bits,.full=full};
  // During construction end is the bucket count. Only appended events count;
  // duplicate reads and failed growth return before this increment.
  buckets[b].end++;
}
static void event_add(unsigned seq,const tex_t *t,const view_t *v,unsigned kind,unsigned bits,int full) {
  if(!index_valid || !t) return;
  bucket_event_add(seq,bucket_for(t,1),v,kind,bits,full);
}
// One selected RT snapshot can participate in many draw events. Resolve each
// slot lazily at its original first use so bucket creation/event order stays
// unchanged. A hit is justified by the current serial, never slot/pointer alone.
// Preserve the original per-draw inline boundary after shrinking the bucket.
static inline __attribute__((always_inline)) void attachment_event_add(unsigned seq,int *cached,const tex_t *t,const view_t *v,
                                 unsigned kind,unsigned bits,int full) {
  if(!index_valid || !t) return;
  int b=*cached;
  if(b<0 || (unsigned)b>=bucket_count || buckets[b].texture.serial!=t->serial)
    *cached=b=bucket_for(t,1);
  bucket_event_add(seq,b,v,kind,bits,full);
}
static void alias_add(unsigned a,unsigned b) {
  if(alias_count==1000000) { index_valid=0;return; }
  if(alias_count==alias_capacity) {
    unsigned n=alias_capacity?alias_capacity*2:256;store_alias *p=STORE_INDEX_REALLOC(aliases,n*sizeof *p);
    if(!p) { index_valid=0;return; }aliases=p;alias_capacity=n;
  }
  aliases[alias_count]=(store_alias){b,buckets[a].aliases};buckets[a].aliases=alias_count++;
}
static int bucket_address_compare(const void *av,const void *bv) {
  uintptr_t a=buckets[*(const uint32_t *)av].texture.base;
  uintptr_t b=buckets[*(const uint32_t *)bv].texture.base;
  return (a>b)-(a<b);
}
static int bucket_footprints_overlap(const vk_texture_footprint *a,const vk_texture_footprint *b) {
  if(!a||!b)return 0;
  if(a->serial==b->serial)return 1;
  uintptr_t x=a->base,y=b->base;
  return x&&y&&(x==y||(x<y?y-x<a->size:x-y<b->size));
}
static void store_index_begin(store_build_context *ctx) {
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
  ctx->rt=S.targets;ctx->sc=S.scissor;ctx->has_sc=S.has_scissor;
  for(unsigned k=0;k<9;k++) ctx->target_buckets[k]=-1;
  ctx->program=S.program;memset(ctx->known_zero,0,sizeof ctx->known_zero);
  for(unsigned st=0;st<6;st++) {
    memcpy(ctx->slots[st],S.tex[st],sizeof S.tex[st]);
    memcpy(ctx->slots[st]+128,S.img[st],sizeof S.img[st]);
  }
}
static void store_index_op(store_build_context *ctx,unsigned seq,unsigned opcode,const void *a,unsigned size) {
  switch(opcode) {
  case OP_SET_RT:
    if(size<8+sizeof(p_set_rt)) { index_valid=0;return; }
    ctx->rt=*(const p_set_rt *)a;if(ctx->rt.n<0||ctx->rt.n>8) index_valid=0;
    for(unsigned k=0;k<9;k++) ctx->target_buckets[k]=-1;
    break;
  case OP_SCISSOR:
    if(size<8+sizeof(p_scissor)) { index_valid=0;return; }
    ctx->sc=*(const p_scissor *)a;ctx->has_sc=1;break;
  case OP_BIND_PROGRAM:
    if(size<8+sizeof(p_bind_program)) { index_valid=0;return; }
    ctx->program=((const p_bind_program *)a)->prog;break;
  case OP_BIND_TEX:case OP_BIND_IMG: {
    if(size<8+offsetof(p_bind_handles,h)) { index_valid=0;return; }
    const p_bind_handles *b=a;int base=opcode==OP_BIND_IMG?128:0,limit=opcode==OP_BIND_IMG?32:128;
    if(b->stage<0||b->stage>=6||b->first<0||b->n<0||b->n>limit-b->first) { global_need(seq);break; }
    if((size-8-offsetof(p_bind_handles,h))<(unsigned)b->n*8) { index_valid=0;return; }
    // Only unknown shaders need the whole-stage empty proof. Invalidate it
    // cheaply on a binding write; known-shader binds need no per-slot count.
    if(b->n) ctx->known_zero[b->stage]=0;
    // Single descriptors dominate the real command stream. Keep this copy
    // as a load/store rather than entering libc for a dynamic eight bytes.
    if(b->n==1)ctx->slots[b->stage][base+b->first]=b->h[0];
    else memcpy(ctx->slots[b->stage]+base+b->first,b->h,b->n*8);
    break;
  }
  case OP_DRAW:case OP_DRAW_IDX:case OP_DISPATCH: {
    vk_shader *shaders[6]={0};if(ctx->program) vk_program_shaders(ctx->program,shaders);
    for(unsigned st=0;st<6;st++) {
      if((opcode==OP_DISPATCH)!=(st==5)) continue;
      vk_shader *s=shaders[st];
      // The conservative unknown-stage scan produces no events when every
      // handle is zero. Retain it for any populated stage, including stale
      // handles; this preserves the reference's exact dependency decisions.
      if(!build_descriptor_reference && !s && ctx->known_zero[st]) continue;
      unsigned n=s?(!build_descriptor_reference?s->image_read_count:s->nb):160;
      int nonzero=0;
      if(!build_descriptor_reference && s) { build_slot_stages++;build_slot_skipped+=s->nb-s->image_read_count; }
      for(unsigned i=0;i<n;i++) {
        int slot=i;
        if(s && !build_descriptor_reference) slot=s->image_read_slots[i];
        else if(s) {
          const vk_binding *b=&s->bindings[i];
          if(b->kind==4||b->kind==9) slot=128+b->reg;
          else if(b->kind==3||b->kind==6||b->kind==7) slot=b->reg;
          else continue;
        }
        if(slot<0||slot>=160) { global_need(seq);continue; }
        uint64_t handle=ctx->slots[st][slot];if(!handle) continue;
        nonzero=1;
        int b=descriptor_bucket(handle&0xfffff);
        if(b<0) global_need(seq);
        else bucket_event_add(seq,b,NULL,IX_READ,0,0);
      }
      // Learn emptiness during the required conservative scan, never by a
      // second pass. Nonempty unknown stages still emit reads at every seq.
      if(!build_descriptor_reference && !s && !nonzero) ctx->known_zero[st]=1;
    }
    if(opcode!=OP_DISPATCH) {
      if(ctx->rt.n<0||ctx->rt.n>8) { global_need(seq);break; }
      for(int k=0;k<ctx->rt.n;k++) attachment_event_add(seq,&ctx->target_buckets[k],ctx->rt.color[k],ctx->rt.has_cview[k]?&ctx->rt.cview[k]:NULL,IX_ATT,0,0);
      attachment_event_add(seq,&ctx->target_buckets[8],ctx->rt.depth,ctx->rt.has_dview?&ctx->rt.dview:NULL,IX_ATT,0,0);
    }
    break;
  }
  case OP_CLEAR_COLOR: {
    if(size<8+sizeof(p_clear_color)) { index_valid=0;return; }
    const p_clear_color *c=a;
    if(ctx->rt.n<0||ctx->rt.n>8||c->index<0||c->index>=ctx->rt.n) { global_need(seq);break; }
    const tex_t *t=ctx->rt.color[c->index];const view_t *v=ctx->rt.has_cview[c->index]?&ctx->rt.cview[c->index]:NULL;
    if(t) attachment_event_add(seq,&ctx->target_buckets[c->index],t,v,IX_COLOR,c->mask&15,full_clear(t,v,&ctx->sc,ctx->has_sc));
    break;
  }
  case OP_CLEAR_DS: {
    if(size<8+sizeof(p_clear_ds)) { index_valid=0;return; }
    const p_clear_ds *c=a;const view_t *v=ctx->rt.has_dview?&ctx->rt.dview:NULL;
    if(ctx->rt.depth) attachment_event_add(seq,&ctx->target_buckets[8],ctx->rt.depth,v,IX_DS,(c->depth_mask?1:0)|((c->stencil_mask&255)==255?2:0),full_clear(ctx->rt.depth,v,&ctx->sc,ctx->has_sc));
    break;
  }
  case OP_COPY_T2T: {
    if(size<8+sizeof(p_copy_t2t)) { index_valid=0;return; }
    const p_copy_t2t *c=a;event_add(seq,c->src,NULL,IX_READ,0,0);event_add(seq,c->dst,NULL,IX_READ,0,0);break;
  }
  case OP_COPY_T2B:case OP_COPY_B2T:
    if(size<8+sizeof(p_copy_b2t)) { index_valid=0;return; }
    event_add(seq,((const p_copy_b2t *)a)->tex,NULL,IX_READ,0,0);break;
  case OP_CLEAR_TEX:
    if(size<8+sizeof(p_clear_tex)) { index_valid=0;return; }
    event_add(seq,((const p_clear_tex *)a)->tex,NULL,IX_READ,0,0);break;
  case OP_SET_TEX_POOL:case OP_SET_SAMP_POOL:global_need(seq);break;
  default:break;
  }
}
static void store_index_finish(void) {
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
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  if(index_valid) store_walk_test_verify();
#endif
}
static unsigned event_lower_bound(const store_bucket *b,unsigned seq) {
  unsigned lo=b->begin,hi=b->end;
  while(lo<hi) { unsigned m=lo+(hi-lo)/2;if(sorted_events[m].seq<seq) lo=m+1;else hi=m; }
  return lo;
}
static int store_index_query(const tex_t *target,const view_t *view,VkImageAspectFlagBits plane) {
  int b=bucket_for(target,0);if(b<0) return 1;
  unsigned start=replay-1+done,best=UINT32_MAX;int needed=1;
  store_bucket *bucket=&buckets[b];
  for(unsigned i=event_lower_bound(bucket,start);i<bucket->end;i++) {
    const store_event *e=&sorted_events[i];atomic_fetch_add(&index_stats.query_events,1);
    if(e->kind==IX_READ) { best=e->seq;break; }
    if(e->level!=level(view)||e->layer!=layer(view)) continue;
    if(e->kind==IX_ATT) { best=e->seq;break; }
    if(e->kind==IX_COLOR) { best=e->seq;needed=!(plane==VK_IMAGE_ASPECT_COLOR_BIT && e->bits==15 && e->full);break; }
    if(e->kind==IX_DS && (e->bits&(plane==VK_IMAGE_ASPECT_DEPTH_BIT?1:2))) { best=e->seq;needed=!e->full;break; }
  }
  unsigned lo=0,hi=global_count;
  while(lo<hi) { unsigned m=lo+(hi-lo)/2;if(globals[m]<start) lo=m+1;else hi=m; }
  if(lo<global_count && globals[lo]<=best) { best=globals[lo];needed=1; }
  for(unsigned a=bucket->aliases;a!=UINT32_MAX;a=aliases[a].next) {
    const store_bucket *other=&buckets[aliases[a].other];unsigned i=event_lower_bound(other,start);
    if(i<other->end && sorted_events[i].seq<=best) { best=sorted_events[i].seq;needed=1; }
  }
  return needed;
}
int vk_pass_store_needed(const tex_t *target,const view_t *view,VkImageAspectFlagBits plane) {
  if(!valid||!store_valid||!replay||!vk_replay_preserves_ops()) return 1;
  if(reference_mode) return store_scan(target,view,plane);
  if(!index_valid || built_epoch!=vk_texture_descriptor_epoch()) {
    atomic_fetch_add(&index_stats.fallbacks,1);return store_scan(target,view,plane);
  }
  int result=store_index_query(target,view,plane);
  // 录制线程也能登记描述符；跨查询/核对变化时不能比较不同快照。
  // The recording thread may register descriptors concurrently. Do not compare
  // different snapshots if the epoch changes during a query or cross-check.
  if(built_epoch!=vk_texture_descriptor_epoch()) {
    atomic_fetch_add(&index_stats.fallbacks,1);return store_scan(target,view,plane);
  }
  if(check_mode) {
    int reference=store_scan(target,view,plane);
    if(built_epoch!=vk_texture_descriptor_epoch()) {
      atomic_fetch_add(&index_stats.fallbacks,1);return reference;
    }
    atomic_fetch_add(&index_stats.comparisons,1);
    if(result!=reference) {
      atomic_fetch_add(&index_stats.mismatches,1);
      die("Vulkan store index mismatch seq=%u done=%d texture=%llu plane=%u index=%d reference=%d",replay-1,done,(unsigned long long)target->serial,plane,result,reference);
    }
  }
  atomic_fetch_add(&index_stats.indexed,1);
  return result;
}
void vk_pass_index_snapshot(vk_pass_index_stats *out) {
  *out=(vk_pass_index_stats){
    atomic_load(&index_stats.builds),atomic_load(&index_stats.indexed),atomic_load(&index_stats.reference),
    atomic_load(&index_stats.reference_ops),atomic_load(&index_stats.query_events),atomic_load(&index_stats.comparisons),
    atomic_load(&index_stats.mismatches),atomic_load(&index_stats.fallbacks)};
}

#undef STORE_INDEX_REALLOC
