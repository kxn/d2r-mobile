// Explicit, non-evicting reservation. Default lookups materialize the whole
// logical pool; opt-in operation admission invokes this before handle borrows.
// New nodes remain private until the complete batch has allocated successfully.
typedef struct {
  pool *owner;
  uint64_t begin,end;
  pool_backing_span *backing;
} pool_window_plan;
#ifdef DM_VULKAN_TEST_EMBED
static int pool_reserve_fail_after=-1;
void vk_test_pool_reserve_fail_after(int successful_allocations) {
  pool_reserve_fail_after=successful_allocations;
}
int vk_test_pool_reserve_metadata(uint64_t address) {
  int flags=-1;pthread_mutex_lock(&V.resources);pool *p=pool_find(address,1);
  if(p)flags=(!!p->shadow) | ((!!p->page_upload_stamps)<<1) | ((!!p->initialized_pages)<<2);
  pthread_mutex_unlock(&V.resources);return flags;
}
#endif
static int pool_window_power_of_two(uint64_t n) { return n && !(n&(n-1)); }
static int pool_window_chain_valid(const pool *p) {
  uint64_t end=0;
  for(const pool_backing_span *b=p->backings;b;b=b->next) {
    if(b->begin<end || b->begin>=b->end || b->end>p->size ||
        (b->begin&4095) || !b->buffer.handle || b->buffer.size!=b->end-b->begin ||
        !b->generation || b->generation>p->backing_generation)return 0;
    end=b->end;
  }
  return 1;
}
// A complete binding must fit one physical buffer. Its optional descriptor
// prefix is relative to that buffer's origin, never to the guest pointer.
static int pool_window_bounds(const pool *p,const vk_pool_requirement *r,uint64_t window,
    uint64_t *begin,uint64_t *end) {
  uint64_t logical=r->address-(uintptr_t)p->guest;
  *begin=logical&~(window-1);
  *end=p->size-*begin>window?*begin+window:p->size;
  uint64_t prefix_begin=r->prefix_alignment?logical&~(r->prefix_alignment-1):logical;
  return prefix_begin>=*begin && logical+r->size<=*end;
}
static int pool_window_overlaps(const pool *p,uint64_t begin,uint64_t end) {
  for(const pool_backing_span *b=p->backings;b && b->begin<end;b=b->next)
    if(begin<b->end)return 1;
  return 0;
}
static int pool_window_whole(const pool *p) {
  return p->backings && !p->backings->next && !p->backings->begin && p->backings->end==p->size;
}
static int pool_window_plan_compare(const void *left,const void *right) {
  const pool_window_plan *a=left,*b=right;
  if(a->owner->identity!=b->owner->identity)return a->owner->identity<b->owner->identity?-1:1;
  if(a->begin!=b->begin)return a->begin<b->begin?-1:1;
  return a->end==b->end?0:a->end>b->end?-1:1;
}
int vk_pool_reserve_ranges(const vk_pool_requirement *requirements,size_t count,
    uint64_t window_bytes,vk_pool_reserve_result *out) {
  vk_pool_reserve_result result={0};int status=VK_POOL_RESERVE_INVALID;
  if(out)*out=result;
  uint64_t minimum=UINT64_C(1)<<GPU_ACCESS_SHIFT;
  const uint64_t alignments[]={V.props.limits.minUniformBufferOffsetAlignment,
    V.props.limits.minStorageBufferOffsetAlignment,V.props.limits.minTexelBufferOffsetAlignment};
  if(!V.device || !pool_window_power_of_two(window_bytes) || window_bytes<minimum ||
      count>VK_POOL_RESERVE_MAX_REQUIREMENTS || (count&&!requirements))return status;
  for(unsigned k=0;k<sizeof alignments/sizeof *alignments;k++)
    if(!pool_window_power_of_two(alignments[k]) || alignments[k]>window_bytes)return status;
  // Full-pool reference sync must not visit unbacked holes. In that mode a
  // fresh pool is whole, and an existing windowed pool requires explicit
  // quiescent whole migration by the caller before enabling that reference.
  int whole_reference=V.options.pool_mirror_reference || atomic_load(&pool_sync_active) ||
      atomic_load(&pool_sync_requested)==1;
  pthread_mutex_lock(&V.resources);
  uint64_t need_base=0,need_identity=0;vk_pool_requirement trigger={0};
  // Reference sync enumerates all materialized pools, not just this draw's
  // requirements. Even an unrelated reserved pool must leave its holes behind
  // through the explicit whole fallback before that global sync can run.
  if(whole_reference)for(pool *p=pools;p;p=p->next)
    if(p->backings && !pool_window_whole(p)) {
      if(!pool_window_chain_valid(p))goto done;
      need_base=(uintptr_t)p->guest;need_identity=p->identity;break;
    }
  // Validate the ENTIRE batch before even allocating its host plan. No source
  // pointer/initialized-page/shadow state is acquired or changed here.
  for(size_t k=0;k<count;k++) {
    const vk_pool_requirement *r=&requirements[k];
    if(!r->address || !r->size || r->size>UINT64_MAX-r->address ||
        (r->prefix_alignment && (!pool_window_power_of_two(r->prefix_alignment) ||
          r->prefix_alignment>window_bytes)))goto done;
    pool *p=pool_find(r->address,r->size);
    if(!p || !pool_window_chain_valid(p))goto done;
    if(whole_reference && p->backings && !pool_window_whole(p)) {
      if(!need_identity) { need_base=(uintptr_t)p->guest;need_identity=p->identity; }
      continue;
    }
    pool_span_piece existing;
    if(pool_span_resolve_locked(p,r->address-(uintptr_t)p->guest,r->size,&existing))continue;
    if(p->backing_generation>UINT64_MAX-count)goto done;
    uint64_t begin,end;int single=pool_window_bounds(p,r,window_bytes,&begin,&end);
    if(p->backings && (!single || pool_window_overlaps(p,begin,end)))
      if(!need_identity) { need_base=(uintptr_t)p->guest;need_identity=p->identity;trigger=*r; }
  }
  if(need_identity) {
    result.pool_base=need_base;result.pool_identity=need_identity;result.trigger=trigger;
    status=VK_POOL_RESERVE_NEED_WHOLE;goto done;
  }
  if(!count) { status=VK_POOL_RESERVE_OK;goto done; }
  if(atomic_load_explicit(&pool_registry_epoch,memory_order_relaxed)==UINT64_MAX)goto done;
  pool_window_plan *plan=calloc(count,sizeof *plan);
  if(!plan) { status=VK_POOL_RESERVE_OOM;result.allocation_error=VK_ERROR_OUT_OF_HOST_MEMORY;goto done; }
  size_t planned=0;
  for(size_t k=0;k<count;k++) {
    const vk_pool_requirement *r=&requirements[k];pool *p=pool_find(r->address,r->size);
    pool_span_piece existing;
    if(pool_span_resolve_locked(p,r->address-(uintptr_t)p->guest,r->size,&existing))continue;
    uint64_t begin,end;int single=pool_window_bounds(p,r,window_bytes,&begin,&end);
    // One crossing binding in a fresh pool makes this whole pool's plan
    // whole, including requirements appearing earlier in the same batch.
    if(whole_reference || !single) { begin=0;end=p->size; }
    plan[planned++]=(pool_window_plan){.owner=p,.begin=begin,.end=end};
  }
  qsort(plan,planned,sizeof *plan,pool_window_plan_compare);
  size_t unique=0;uint64_t bytes=0;
  for(size_t k=0;k<planned;k++) {
    pool_window_plan q=plan[k];
    if(unique && plan[unique-1].owner==q.owner && plan[unique-1].begin<=q.begin &&
        plan[unique-1].end>=q.end)continue;
    if(q.end-q.begin>UINT64_MAX-bytes) { free(plan);goto done; }
    bytes+=q.end-q.begin;plan[unique++]=q;
  }
  planned=unique;
  for(size_t k=0;k<planned;k++) {
    pool_window_plan *q=&plan[k];q->backing=calloc(1,sizeof *q->backing);
    VkResult allocated=q->backing?VK_SUCCESS:VK_ERROR_OUT_OF_HOST_MEMORY;
#ifdef DM_VULKAN_TEST_EMBED
    if(allocated==VK_SUCCESS && pool_reserve_fail_after==0)allocated=VK_ERROR_OUT_OF_DEVICE_MEMORY;
#endif
    if(allocated==VK_SUCCESS && !pool_host_try_span(q->owner,q->backing,q->begin,q->end,requirements,count)) {
#ifdef DM_VULKAN_TEST_EMBED
      if(pool_host_test_fail_mirror) {pool_host_test_fail_mirror=0;allocated=VK_ERROR_OUT_OF_DEVICE_MEMORY;}
      else
#endif
      allocated=pool_span_try_allocate(q->backing,q->begin,q->end);
    }
    if(allocated!=VK_SUCCESS) {
      result.allocation_error=allocated;
      status=(allocated==VK_ERROR_OUT_OF_HOST_MEMORY || allocated==VK_ERROR_OUT_OF_DEVICE_MEMORY)?
          VK_POOL_RESERVE_OOM:VK_POOL_RESERVE_ALLOCATION_FAILED;
      for(size_t j=0;j<=k;j++)if(plan[j].backing) {
        vk_buffer_destroy(&plan[j].backing->buffer);free(plan[j].backing);
      }
      free(plan);goto done;
    }
#ifdef DM_VULKAN_TEST_EMBED
    if(pool_reserve_fail_after>0)pool_reserve_fail_after--;
#endif
  }
  // All fallible work is complete. Publish disjoint nodes without moving any
  // old node, handle or physical origin. Existing async readers remain valid.
  for(size_t k=0;k<planned;k++) {
    pool_window_plan *q=&plan[k];pool_backing_span *b=q->backing;
    b->generation=++q->owner->backing_generation;
    pool_backing_span **at=&q->owner->backings;
    while(*at && (*at)->begin<b->begin)at=&(*at)->next;
    b->next=*at;*at=b;
    result.added_spans++;result.added_buffer_bytes+=b->end-b->begin;
  }
  if(planned)atomic_fetch_add_explicit(&pool_registry_epoch,1,memory_order_release);
  free(plan);status=VK_POOL_RESERVE_OK;
done:
  pthread_mutex_unlock(&V.resources);
  if(out)*out=result;
  return status;
}
