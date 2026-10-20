// Owner-direct required CPU visibility, called before borrowing draw/copy
// resources. HLE guest callers are serialized by the original translation
// queue; completion workers must never enter this function.
int vk_pool_cpu_unknown(uint64_t address,uint64_t size) {
  if(!V.options.pool_defer_publication || !size)return 0;
  pthread_mutex_lock(&V.resources);pool *p=pool_find(address,size);
  int unknown=p&&!pool_cpu_copy_proven(p,address-(uintptr_t)p->guest,size);
  pthread_mutex_unlock(&V.resources);return unknown;
}
uint64_t mtl_pool_cpu_access_needed(uint64_t address,uint64_t size,uint64_t *begin,uint64_t *bytes) {
  int deferred=atomic_load_explicit(&pool_defer_publication_enabled,memory_order_acquire);
  int encoding=0;
#ifdef DM_VULKAN_SHARED_POOLS
  encoding=legacy_encoding_maybe(address,size);
#endif
  if((!deferred && !encoding) || !size)return 0;
  if(size>UINT64_MAX-address)die("Vulkan CPU exposure address overflow");
  uint64_t end=address+size,identity=0,best=UINT64_MAX,length=0;
  pthread_mutex_lock(&V.resources);
#ifdef DM_VULKAN_SHARED_POOLS
  if(encoding)for(pool *p=pools;p;p=p->next)for(legacy_encoding_range *g=p->encoding_guards;g;g=g->next) {
    uint64_t lo=address>g->begin?address:g->begin,hi=end<g->end?end:g->end;
    if(lo<hi && lo<best){identity=p->identity;best=lo;length=hi-lo;}
  }
#endif
  for(pool *p=pools;deferred && p;p=p->next)if(pool_publication_candidate(p)) {
    uint64_t base=(uintptr_t)p->guest,lo=address>base?address:base;
    uint64_t hi=end<base+p->size?end:base+p->size;
    if(lo>=hi || lo>=best)continue;
    if(pool_cpu_fully_exposed(p,lo-base,hi-lo) && pool_cpu_copy_proven(p,lo-base,hi-lo))continue;
    identity=p->identity;best=lo;length=hi-lo;
  }
  pthread_mutex_unlock(&V.resources);
  if(identity){*begin=best;*bytes=length;}
  return identity;
}
int vk_pool_cpu_source_ready(uint64_t address,uint64_t size) {
  if(!V.options.pool_defer_publication || !size)return 1;
  pthread_mutex_lock(&V.resources);pool *p=pool_find(address,size);
  // Exposure only controls future publication. Actual CPU source visibility
  // also requires every already-recorded GPU writer to have been published.
  // The outer logical operation restores this before borrowing any image.
  int ready=p?(!pool_publication_candidate(p) ||
      (pool_cpu_fully_exposed(p,address-(uintptr_t)p->guest,size) &&
       pool_cpu_copy_proven(p,address-(uintptr_t)p->guest,size))):1;
  if(!p) {
    if(size>UINT64_MAX-address)ready=0;
    else for(pool *v=pools;v;v=v->next)if(pool_publication_candidate(v) &&
        address<(uintptr_t)v->guest+v->size && address+size>(uintptr_t)v->guest){ready=0;break;}
  }
  pthread_mutex_unlock(&V.resources);return ready;
}
void mtl_pool_defer_publication_disable(void) {
  // Only the original HLE queue's startup failure/reference mode uses this,
  // before its first GPU submission. Never a hot runtime policy switch.
  atomic_store_explicit(&pool_defer_publication_enabled,0,memory_order_release);
  V.options.pool_defer_publication=0;
}
static int pool_cpu_prepare_identity(uint64_t address,uint64_t size,uint64_t expected) {
#ifdef DM_VULKAN_SHARED_POOLS
  if(legacy_encoding_maybe(address,size)) {
    pthread_mutex_lock(&V.resources);pool *p=pool_find(address,size);
    if(expected && (!p || p->identity!=expected)){pthread_mutex_unlock(&V.resources);return -1;}
    for(pool *v=pools;v;v=v->next)for(legacy_encoding_range *g=v->encoding_guards;g;g=g->next)
      if(range_overlap(address,size,g->begin,g->end-g->begin))
        die("ASTC propagated storage cannot supply CPU exposure of logical BC3 bytes");
    pthread_mutex_unlock(&V.resources);
  }
#endif
  if(!V.options.pool_defer_publication || !size)return 0;
  pthread_mutex_lock(&V.resources);pool *p=pool_find(address,size);
  if(!p || (expected && p->identity!=expected)) {
    int overlap=0;
    if(!p && size<=UINT64_MAX-address)for(pool *v=pools;v;v=v->next)
      if(pool_publication_candidate(v) && address<(uintptr_t)v->guest+v->size && address+size>(uintptr_t)v->guest){overlap=1;break;}
    pthread_mutex_unlock(&V.resources);return expected||overlap?-1:0;
  }
  if(!pool_publication_candidate(p)) {pthread_mutex_unlock(&V.resources);return 0;}
  uint64_t offset=address-(uintptr_t)p->guest,identity=p->identity;
  if(pool_cpu_fully_exposed(p,offset,size) && pool_cpu_copy_proven(p,offset,size)) {
    pthread_mutex_unlock(&V.resources);return 0;
  }
  pthread_mutex_unlock(&V.resources);
  // A real CPU observation may alias a deferred logical clear. Do not invoke
  // this drain for ordinary pools or already-fresh sources (that would break
  // unrelated continuation), and never hold the registry lock over callbacks.
  vk_mip_clear_buffer_access(address,size);
  pthread_mutex_lock(&V.resources);p=pool_find(address,size);
  if(!p || p->identity!=identity) {pthread_mutex_unlock(&V.resources);return -1;}
  pool_cpu_expose(p,offset,size);
  vk_pool_requirement needs[POOL_CPU_UNKNOWN_LIMIT];unsigned count=0;
  pthread_mutex_lock(&pool_cpu_authority_mu);
  pool_cpu_authority *state=p->cpu_authority;
  if(!state && p->cpu_copy_requires_gpu)needs[count++]=(vk_pool_requirement){address,size,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0};
  if(state)for(unsigned k=0;k<state->count;k++) {
    uint64_t lo=state->ranges[k].begin>offset?state->ranges[k].begin:offset;
    uint64_t hi=state->ranges[k].end<offset+size?state->ranges[k].end:offset+size;
    if(lo<hi)needs[count++]=(vk_pool_requirement){(uintptr_t)p->guest+lo,hi-lo,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0};
  }
  pthread_mutex_unlock(&pool_cpu_authority_mu);
  pthread_mutex_unlock(&V.resources);
  if(!count)return 0;
  // Once exposed, every intersecting pending/future writeback is eager. The
  // explicit request below restores historical unknown bytes as well.
  int reopen=V.cmd!=VK_NULL_HANDLE;
  if(!reopen)mtl_sub_begin();
  int operation=vk_pool_operation_begin_ranges(needs,count);
  // Admission may drain/promote. No old pool pointer crosses that callback;
  // re-resolve exact lifetime before pinning (a pin here cannot block migration).
  pthread_mutex_lock(&V.resources);p=pool_find(address,size);
  if(!p || p->identity!=identity) {
    pthread_mutex_unlock(&V.resources);vk_pool_operation_end(operation);return -1;
  }
  p->references++;
  pthread_mutex_unlock(&V.resources);
  // No fabricated GPU write/authority generation: this is a CPU observation.
  // Initial CPU edits are uploaded through the ordinary commit upload path.
  for(unsigned k=0;k<count;k++)buffer_writeback_intent(needs[k].address,needs[k].size,NULL,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,NULL);
  uint64_t serial=vk_internal_commit();vk_internal_wait(serial);
  vk_pool_operation_end(operation);
  pthread_mutex_lock(&V.resources);
  if(p->identity!=identity)die("Vulkan CPU demand pool identity changed");
  if(!p->references)die("Vulkan CPU demand reference underflow");
  p->references--;pthread_cond_broadcast(&pool_cv);
  pthread_mutex_unlock(&V.resources);
  if(reopen)mtl_sub_begin();
  return 1;
}
int vk_pool_cpu_prepare(uint64_t address,uint64_t size) {
  return pool_cpu_prepare_identity(address,size,0);
}
int mtl_pool_cpu_access(uint64_t address,uint64_t size,uint64_t identity) {
  return pool_cpu_prepare_identity(address,size,identity)>=0;
}
#ifdef DM_VULKAN_TEST_EMBED
int vk_test_pool_publication(uint64_t address,uint64_t size,int *eligible,int *unknown,int *exposed) {
  pthread_mutex_lock(&V.resources);pool *p=pool_find(address,size);
  if(p) {*eligible=pool_publication_can_defer(p,address-(uintptr_t)p->guest,size);
    *unknown=!pool_cpu_copy_proven(p,address-(uintptr_t)p->guest,size);
    *exposed=pool_cpu_exposed(p,address-(uintptr_t)p->guest,size);}
  pthread_mutex_unlock(&V.resources);return p!=NULL;
}
#endif
