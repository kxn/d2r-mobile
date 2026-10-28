// Owned host-only comparison against the complete pre-fusion forward pass.
// No app observer, timing, game state or cross-submission command cache.
#include "store_walk_reference.inc"
static unsigned store_walk_checks,store_walk_comparisons;
static int store_walk_verify_enabled;
static void store_domain_test_verify(void);
#define SW_CHECK(c) do { ++store_walk_checks;if(!(c)) die("Store walk fusion oracle: %s at %u",#c,__LINE__); } while(0)
static void store_walk_test_verify(void) {
  if(!store_walk_verify_enabled) return;
  // Filtering intentionally removes pre-target reads. Compare observable
  // Store decisions only at legitimate prefix-target/start/done positions;
  // the unfiltered mode retains the original exact whole-table oracle.
  if(build_domain) {store_domain_test_verify();return;}
  unsigned nb=bucket_count,ne=event_count,ng=global_count,na=alias_count;
  store_bucket *bs=malloc(nb*sizeof *bs+1);
  store_event *es=malloc(ne*sizeof *es+1),*ss=malloc(ne*sizeof *ss+1);
  uint32_t *gs=malloc(ng*sizeof *gs+1);store_alias *as=malloc(na*sizeof *as+1);
  SW_CHECK(bs&&es&&ss&&gs&&as);
  memcpy(bs,buckets,nb*sizeof *bs);memcpy(es,events,ne*sizeof *es);memcpy(ss,sorted_events,ne*sizeof *ss);
  if(ng)memcpy(gs,globals,ng*sizeof *gs);if(na)memcpy(as,aliases,na*sizeof *as);
  uint64_t old_epoch=built_epoch;
  uint64_t old_stats[]={atomic_load(&descriptor_cache_hits),atomic_load(&descriptor_cache_misses),
    atomic_load(&slot_cache_stages),atomic_load(&slot_cache_skipped_bindings),
    atomic_load(&recorded_events),atomic_load(&duplicate_reads),atomic_load(&index_stats.builds)};
  // This independently traverses the completed flattened plan, reconstructs
  // all state, then performs its original scatter/alias closure unchanged.
  store_index_build_reference();
  SW_CHECK(index_valid&&built_epoch==old_epoch);
  SW_CHECK(nb==bucket_count&&ne==event_count&&ng==global_count&&na==alias_count);
  for(unsigned i=0;i<nb;i++) {
    const store_bucket *a=&bs[i],*b=&buckets[i];
    SW_CHECK(a->texture.serial==b->texture.serial&&a->texture.base==b->texture.base&&a->texture.size==b->texture.size);
    SW_CHECK(a->begin==b->begin&&a->end==b->end&&a->aliases==b->aliases&&a->last_read==b->last_read);
  }
  for(unsigned array=0;array<2;array++) for(unsigned i=0;i<ne;i++) {
    const store_event *a=array?&ss[i]:&es[i],*b=array?&sorted_events[i]:&events[i];
    SW_CHECK(a->seq==b->seq&&a->level==b->level&&a->layer==b->layer&&a->bucket==b->bucket&&
      a->kind==b->kind&&a->full==b->full&&a->bits==b->bits);
  }
  if(ng)SW_CHECK(!memcmp(gs,globals,ng*sizeof *gs));if(na)SW_CHECK(!memcmp(as,aliases,na*sizeof *as));
  atomic_store(&descriptor_cache_hits,old_stats[0]);atomic_store(&descriptor_cache_misses,old_stats[1]);
  atomic_store(&slot_cache_stages,old_stats[2]);atomic_store(&slot_cache_skipped_bindings,old_stats[3]);
  atomic_store(&recorded_events,old_stats[4]);atomic_store(&duplicate_reads,old_stats[5]);atomic_store(&index_stats.builds,old_stats[6]);
  free(bs);free(es);free(ss);free(gs);free(as);store_walk_comparisons++;
}
unsigned vk_pass_test_store_walk_begin(void) {
  SW_CHECK(vk_tbdr_enabled());
  vk_state saved=S;int saved_reference=V.options.store_reference,saved_check=V.options.store_check;
  V.options.store_reference=0;V.options.store_check=1;store_walk_verify_enabled=1;
  uint8_t memory[8192]={0};tex_t t[3]={0};
  for(unsigned i=0;i<3;i++) {t[i].serial=UINT64_C(0x100000000)+i;t[i].mem=memory+(i==1?512:i==2?4096:0);t[i].L.size=2048;t[i].d.width=t[i].d.height=16;}
  for(unsigned round=0;round<24;round++) {
    memset(&S,0,sizeof S);S.targets=(p_set_rt){.n=1,.color={&t[round%3]},.depth=&t[(round+1)%3]};
    S.scissor=(p_scissor){0,0,round&1?8:16,16};S.has_scissor=round&2;
    uint64_t aa[512]={0},bb[512]={0},cc[512]={0},dd[512]={0};
    uint8_t *a=(void *)aa,*b=(void *)bb,*c=(void *)cc,*d=(void *)dd;unsigned an=0,bn=0,cn=0,dn=0;
    p_set_rt rt={.n=1,.color={&t[0]},.depth=&t[1]};
    rt.has_cview[0]=1;rt.cview[0]=(view_t){.set_levels=1,.base_level=(round>>1)&1,.set_layers=1,.min_layer=round&1};
    p_clear_color clear={.index=0,.mask=round&1?7:15};p_clear_ds ds={.depth_mask=1,.stencil_mask=255};
    p_draw draw={NVN_DRAW_PRIMITIVE_TRIANGLES,0,3};p_dispatch dispatch={1,1,1,0};
    p_scissor sc={0,0,16,16};p_bind_handles bind={.stage=4,.first=0,.n=1,.h={0xfffff}};
    p_copy_t2t cp={.src=&t[1],.dst=&t[2]};p_copy_b2t copy={.tex=&t[0]};p_clear_tex ct={.tex=&t[2]};
    forward_test_append(c,&cn,OP_CLEAR_COLOR,&clear,sizeof clear);
    forward_test_append(c,&cn,OP_DRAW,&draw,sizeof draw);forward_test_append(c,&cn,OP_END,NULL,0);
    forward_test_append(d,&dn,OP_COPY_T2T,&cp,sizeof cp);forward_test_append(d,&dn,OP_END,NULL,0);
    uint64_t call=(uintptr_t)c,jump=(uintptr_t)d;
    forward_test_append(a,&an,OP_SET_RT,&rt,sizeof rt);forward_test_append(a,&an,OP_CALL,&call,8);
    forward_test_append(a,&an,OP_BIND_TEX,&bind,offsetof(p_bind_handles,h)+8);
    forward_test_append(a,&an,OP_DISPATCH,&dispatch,sizeof dispatch);forward_test_append(a,&an,OP_END,NULL,0);
    rt.color[0]=&t[2];rt.depth=&t[0];bind.stage=5;bind.first=31;bind.n=1;
    forward_test_append(b,&bn,OP_SET_RT,&rt,sizeof rt);forward_test_append(b,&bn,OP_SCISSOR,&sc,sizeof sc);
    forward_test_append(b,&bn,OP_BIND_IMG,&bind,offsetof(p_bind_handles,h)+8);
    forward_test_append(b,&bn,OP_CALL,&call,8);forward_test_append(b,&bn,OP_CLEAR_DS,&ds,sizeof ds);
    forward_test_append(b,&bn,OP_COPY_B2T,&copy,sizeof copy);forward_test_append(b,&bn,OP_COPY_T2B,&copy,sizeof copy);
    forward_test_append(b,&bn,OP_CLEAR_TEX,&ct,sizeof ct);forward_test_append(b,&bn,OP_SET_TEX_POOL,NULL,0);
    forward_test_append(b,&bn,OP_JUMP,&jump,8);
    uint64_t handles[]={(uintptr_t)a,(uintptr_t)b};unsigned before=store_walk_comparisons;
    submission_index_build(handles,2);SW_CHECK(valid&&index_valid&&store_walk_comparisons==before+1);
    // A later invalid root discards the entire fused prefix and all reverse outputs.
    op_hdr bad={OP_END,7};handles[1]=(uintptr_t)&bad;submission_index_build(handles,2);SW_CHECK(!valid&&!index_valid);
  }
  // Each store-only short payload disables the index before dereferencing it,
  // while the original structural/DS plan stays valid. ASan checks the exact
  // two-header allocation, including payloads larger than the following END.
  const unsigned short_ops[]={OP_SET_RT,OP_SCISSOR,OP_BIND_PROGRAM,OP_BIND_TEX,OP_BIND_IMG,
    OP_CLEAR_COLOR,OP_CLEAR_DS,OP_COPY_T2T,OP_COPY_B2T,OP_COPY_T2B,OP_CLEAR_TEX};
  memset(&S,0,sizeof S);
  for(unsigned k=0;k<sizeof short_ops/sizeof *short_ops;k++) {
    op_hdr *shorts=malloc(16);SW_CHECK(shorts);shorts[0]=(op_hdr){short_ops[k],8};shorts[1]=(op_hdr){OP_END,8};
    uint64_t h=(uintptr_t)shorts;submission_index_build(&h,1);SW_CHECK(valid&&!index_valid&&count==2);free(shorts);
  }
  // A complete prefix with a truncated descriptor array must not consume the
  // next operation as handle bytes. Invalid stage/range is globally unknown.
  uint64_t stream[128]={0};unsigned used=0;p_bind_handles bind={.stage=4,.n=1};
  forward_test_append((void *)stream,&used,OP_BIND_TEX,&bind,offsetof(p_bind_handles,h));
  forward_test_append((void *)stream,&used,OP_END,NULL,0);uint64_t h=(uintptr_t)stream;
  submission_index_build(&h,1);SW_CHECK(valid&&!index_valid);
  used=0;bind.stage=6;forward_test_append((void *)stream,&used,OP_BIND_TEX,&bind,sizeof bind);
  forward_test_append((void *)stream,&used,OP_END,NULL,0);submission_index_build(&h,1);SW_CHECK(valid&&index_valid&&global_count==1);
  // Store-event allocation failure must not invalidate the completed DS/query
  // plan or leave an apparently usable partial store index. Plan allocation
  // has already grown independently and remains reusable.
  unsigned ec=event_capacity;event_capacity=0;store_walk_fail_allocation=1;
  S.targets=(p_set_rt){.n=1,.color={&t[0]}};used=0;p_draw draw={NVN_DRAW_PRIMITIVE_TRIANGLES,0,3};
  forward_test_append((void *)stream,&used,OP_DRAW,&draw,sizeof draw);forward_test_append((void *)stream,&used,OP_END,NULL,0);
  submission_index_build(&h,1);SW_CHECK(valid&&!index_valid&&count==2&&plan[0].sample_live==1);
  store_walk_fail_allocation=0;event_capacity=ec;submission_index_build(&h,1);SW_CHECK(valid&&index_valid);
  valid=store_valid=index_valid=0;count=replay=done=0;S=saved;V.options.store_reference=saved_reference;V.options.store_check=saved_check;
  // Keep comparison enabled for the real GPU store-overwrite fixture below.
  unsigned result=store_walk_checks;store_walk_checks=0;return result;
}
unsigned vk_pass_test_store_walk_end(void) {
  SW_CHECK(store_walk_comparisons>100);store_walk_verify_enabled=0;
  LOG("[store walk fusion fixture] %u complete original-index comparisons; context=%zu bytes",store_walk_comparisons,sizeof(store_build_context));
  return store_walk_checks;
}
#undef SW_CHECK
#include "store_attachment_domain.inc"
