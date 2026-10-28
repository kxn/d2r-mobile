// Independent unfiltered forward interpreter remains the oracle. Query every
// ever-seen attachment after its enrollment, at both original start/done
// boundaries. Never require equality for a target queried before it exists.
static void store_domain_test_verify(void) {
  typedef struct {const tex_t *t;view_t v;unsigned first;int has;} target;
  target *targets=calloc((count+1)*9,sizeof *targets);
  if(!targets)die("Store domain oracle target allocation");
  unsigned nt=0,old_replay=replay,old_done=done;
  unsigned nb=bucket_count,ne=event_count,ng=global_count,na=alias_count;
  uint64_t old_epoch=built_epoch;
  store_bucket *saved_b=malloc(nb*sizeof *saved_b+1);
  store_event *saved_e=malloc(ne*sizeof *saved_e+1),*saved_s=malloc(ne*sizeof *saved_s+1);
  uint32_t *saved_g=malloc(ng*sizeof *saved_g+1);
  store_alias *saved_a=malloc(na*sizeof *saved_a+1);
  uint16_t *saved_hash=malloc(sizeof bucket_hash);
  typeof(*descriptor_buckets) *saved_descriptors=malloc(sizeof descriptor_buckets);
  if(!saved_b||!saved_e||!saved_s||!saved_g||!saved_a||!saved_hash||!saved_descriptors)
    die("Store domain oracle candidate snapshot allocation");
  memcpy(saved_b,buckets,nb*sizeof *saved_b);memcpy(saved_e,events,ne*sizeof *saved_e);
  memcpy(saved_s,sorted_events,ne*sizeof *saved_s);
  if(ng)memcpy(saved_g,globals,ng*sizeof *saved_g);
  if(na)memcpy(saved_a,aliases,na*sizeof *saved_a);
  memcpy(saved_hash,bucket_hash,sizeof bucket_hash);
  memcpy(saved_descriptors,descriptor_buckets,sizeof descriptor_buckets);
  uint64_t saved_counters[]={atomic_load(&descriptor_cache_hits),atomic_load(&descriptor_cache_misses),
    atomic_load(&slot_cache_stages),atomic_load(&slot_cache_skipped_bindings),
    atomic_load(&recorded_events),atomic_load(&duplicate_reads),atomic_load(&index_stats.builds)};
  uint64_t saved_build[]={build_descriptor_hits,build_descriptor_misses,build_slot_stages,
    build_slot_skipped,build_duplicate_reads};
  p_set_rt rt=S.targets;
  vk_pass_cursor cursor={.op=count?plan[0].op:NULL,.generation=generation};
  for(unsigned step=0;step<=count;step++) {
    unsigned seq=step?step-1:0;int enroll=step==0;
    if(step) {
      unsigned op=cursor_opcode(&cursor);
      if(op==OP_SET_RT) {rt=*(const p_set_rt *)(cursor.op+8);enroll=1;}
    }
    if(enroll && rt.n>=0 && rt.n<=8)for(int k=0;k<=rt.n;k++) {
      const tex_t *t=k==rt.n?rt.depth:rt.color[k];if(!t)continue;
      int has=k==rt.n?rt.has_dview:rt.has_cview[k];
      const view_t *v=k==rt.n?&rt.dview:&rt.cview[k];unsigned i=0;
      for(;i<nt;i++)if(targets[i].t->serial==t->serial && targets[i].has==has &&
          (!has || !memcmp(&targets[i].v,v,sizeof *v)))break;
      if(i==nt) {targets[nt]=(target){.t=t,.has=has,.first=seq};if(has)targets[nt].v=*v;nt++;}
    }
    if(step)cursor_advance_raw(&cursor);
  }
  size_t checks=(size_t)count*nt*6;
  uint8_t *answers=malloc(checks+1);if(!answers)die("Store domain oracle answers allocation");
  uint64_t query_events=atomic_load(&index_stats.query_events);
  for(unsigned pass=0;pass<2;pass++) {
    if(pass) {store_index_build_reference();if(!index_valid)die("Store domain reference invalid");}
    size_t at=0;
    for(unsigned seq=0;seq<count;seq++)for(unsigned t=0;t<nt;t++) {
      if(targets[t].first>seq)continue;
      replay=seq+1;
      for(unsigned finished=0;finished<2;finished++)for(unsigned plane=1;plane<=4;plane*=2) {
        done=finished;int answer=store_index_query(targets[t].t,targets[t].has?&targets[t].v:NULL,plane);
        if(!pass)answers[at]=(uint8_t)answer;
        else {store_walk_checks++;if(answers[at]!=answer)
          die("Store domain oracle mismatch seq=%u done=%u serial=%llu plane=%u filtered=%u reference=%u",
              seq,finished,(unsigned long long)targets[t].t->serial,plane,answers[at],answer);}
        at++;
      }
    }
  }
  // Subsequent real GPU fixtures must consume the filtered candidate, never
  // the reference tables just used by the decision oracle. Production builds
  // do not compile this snapshot/restore path.
  bucket_count=nb;event_count=ne;global_count=ng;alias_count=na;built_epoch=old_epoch;
  memcpy(buckets,saved_b,nb*sizeof *saved_b);memcpy(events,saved_e,ne*sizeof *saved_e);
  memcpy(sorted_events,saved_s,ne*sizeof *saved_s);
  if(ng)memcpy(globals,saved_g,ng*sizeof *saved_g);
  if(na)memcpy(aliases,saved_a,na*sizeof *saved_a);
  memcpy(bucket_hash,saved_hash,sizeof bucket_hash);
  memcpy(descriptor_buckets,saved_descriptors,sizeof descriptor_buckets);
  build_descriptor_hits=saved_build[0];build_descriptor_misses=saved_build[1];
  build_slot_stages=saved_build[2];build_slot_skipped=saved_build[3];build_duplicate_reads=saved_build[4];
  atomic_store(&descriptor_cache_hits,saved_counters[0]);atomic_store(&descriptor_cache_misses,saved_counters[1]);
  atomic_store(&slot_cache_stages,saved_counters[2]);atomic_store(&slot_cache_skipped_bindings,saved_counters[3]);
  atomic_store(&recorded_events,saved_counters[4]);atomic_store(&duplicate_reads,saved_counters[5]);
  atomic_store(&index_stats.builds,saved_counters[6]);
  replay=old_replay;done=old_done;
  atomic_store(&index_stats.query_events,query_events);
  free(saved_b);free(saved_e);free(saved_s);free(saved_g);free(saved_a);free(saved_hash);free(saved_descriptors);
  free(answers);free(targets);store_walk_comparisons++;
}
