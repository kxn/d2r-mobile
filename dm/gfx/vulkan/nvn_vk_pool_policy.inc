// Canonical window allocation is selected once at startup. An operation first
// describes ALL logical ranges, then reserves nodes, then borrows handles. Only
// an explicit recording-owner maintenance boundary may replace old backings.
enum { POOL_OPERATION_DEPTH=8 };
typedef struct {
  vk_pool_requirement ranges[VK_POOL_RESERVE_MAX_REQUIREMENTS];
  pool *pins[VK_POOL_RESERVE_MAX_REQUIREMENTS];
  size_t count,pin_count;
} pool_operation_plan;
static pool_operation_plan *pool_operation_plans[POOL_OPERATION_DEPTH];
static uint64_t pool_operation_fallbacks;
static int pool_operation_add(void *context,const vk_pool_requirement *range) {
  pool_operation_plan *plan=context;
  if(!range || !range->address || !range->size || range->size>UINT64_MAX-range->address ||
      plan->count==VK_POOL_RESERVE_MAX_REQUIREMENTS)return 0;
  // Exact duplicates need only one physical reservation; keep access semantics
  // in the original consumers rather than modifying their dependency records.
  for(size_t k=0;k<plan->count;k++)if(plan->ranges[k].address==range->address &&
      plan->ranges[k].size==range->size && plan->ranges[k].prefix_alignment==range->prefix_alignment)return 1;
  plan->ranges[plan->count++]=*range;return 1;
}
static void pool_operation_unpin(pool_operation_plan *plan) {
  pthread_mutex_lock(&V.resources);
  while(plan->pin_count) {
    pool *p=plan->pins[--plan->pin_count];
    if(!p->references)die("Vulkan operation pool pin underflow");
    p->references--;
  }
  pthread_cond_broadcast(&pool_cv);pthread_mutex_unlock(&V.resources);
}
// Unknown legal representations cannot use a partially collected list. The
// conservative path admits complete registered pools, never truncated ranges.
static void pool_operation_whole_plan(pool_operation_plan *plan,const char *reason) {
#ifdef DM_VULKAN_SHARED_POOLS
  if(shared_window_bytes)die("Shared imported windows require complete %s: %s",reason,vk_shader_pool_range_error());
#endif
  pool_operation_fallbacks++;
  atomic_store(&published_window_fallbacks,pool_operation_fallbacks);
  if(pool_operation_fallbacks<=8 || !(pool_operation_fallbacks%1024))
    LOG("[vulkan pool windows] incomplete %s (%s), conservative whole-registry fallback=%llu program=%llu",
        reason,vk_shader_pool_range_error(),(unsigned long long)pool_operation_fallbacks,
        (unsigned long long)(S.program?S.program->serial:0));
  plan->count=0;pthread_mutex_lock(&V.resources);
  for(pool *p=pools;p;p=p->next) {
    vk_pool_requirement r={(uintptr_t)p->guest,p->size,V.options.pool_window_bytes,
      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,1,0};
    if(!pool_operation_add(plan,&r))die("Vulkan whole fallback requirement capacity");
  }
  pthread_mutex_unlock(&V.resources);
}
// Limit VkBuffer/VMA object multiplication before allocation. This is a
// lifetime policy, not eviction: the one complete range makes a fresh pool
// whole, or requests explicit migration of an existing windowed pool.
static void pool_operation_bound_nodes(pool_operation_plan *plan) {
  pthread_mutex_lock(&V.resources);
  for(size_t k=0;k<plan->count;k++) {
    vk_pool_requirement *r=&plan->ranges[k];pool *p=pool_find(r->address,r->size);
    if(!p || pool_window_whole(p))continue;
    unsigned nodes=0;for(pool_backing_span *b=p->backings;b;b=b->next)nodes++;
    unsigned missing=0;
    for(size_t j=0;j<plan->count;j++) {
      const vk_pool_requirement *q=&plan->ranges[j];
      if(q->address<(uintptr_t)p->guest || q->address-(uintptr_t)p->guest>=p->size ||
          q->size>p->size-(q->address-(uintptr_t)p->guest))continue;
      pool_span_piece span;
      if(!pool_span_resolve_locked(p,q->address-(uintptr_t)p->guest,q->size,&span))missing++;
    }
    // Counting duplicate missing windows conservatively can promote earlier;
    // it never creates overlapping nodes or silently truncates a binding.
    if(nodes+missing>256) {
      if(pool_shared_window_policy(p))die("Shared import span budget exceeded; no hidden whole fallback");
      *r=(vk_pool_requirement){(uintptr_t)p->guest,p->size,0,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,1,0};
      LOG("[vulkan pool windows] whole fallback at bounded span budget identity=%llu existing=%u requests=%u",
          (unsigned long long)p->identity,nodes,missing);
    }
  }
  pthread_mutex_unlock(&V.resources);
}
static int pool_operation_pin(pool_operation_plan *plan) {
  int good=1;pthread_mutex_lock(&V.resources);
  for(size_t k=0;k<plan->count;k++) {
    const vk_pool_requirement *r=&plan->ranges[k];pool *p=NULL;size_t j=0;
    // Registration rejects wrapping/overlapping intervals, and resources
    // stays locked throughout this attempt. Reuse only an owner already
    // pinned HERE; another range still needs its exact physical span proof.
    for(;j<plan->pin_count;j++) {
      pool *known=plan->pins[j];uint64_t base=(uintptr_t)known->guest;
      if(r->address>=base && r->address-base<=known->size &&
          r->size<=known->size-(r->address-base)) {p=known;break;}
    }
    if(!p)p=pool_find(r->address,r->size);
    pool_span_piece span;
    if(!p || !pool_span_resolve_locked(p,r->address-(uintptr_t)p->guest,r->size,&span)) {good=0;break;}
    if(j==plan->pin_count) {p->references++;plan->pins[plan->pin_count++]=p;}
  }
  pthread_mutex_unlock(&V.resources);
  if(!good)pool_operation_unpin(plan);
  return good;
}
// Close existing work before copying old buffers. In particular a migration
// must NOT be appended to a main CB whose later uploads execute before it.
// Ghost replay is postponed across this maintenance-only pass end, then runs
// immediately after reopen, before the interrupted original operation.
static void pool_policy_promote(uint64_t base,uint64_t identity,int reopen,
    const vk_pool_requirement *trigger) {
  if(pool_operation_depth || pool_maintenance || vk_shader_operation_active() ||
      vk_texture_binding_scope_active() || vk_texture_shader_collecting())
    die("Vulkan pool promotion attempted while physical handles are borrowed");
  int was_stream=stream_recording;
  pool_maintenance=1;
  if(V.cmd) {
    vk_render_end_preserve();
    uint64_t serial=vk_internal_commit();vk_internal_wait(serial);
  }
  // This is the single owner maintenance scope, not a new replay lifetime.
  // Keep planner/S/trace state intact, and account the work already submitted
  // in the stream quota instead of resetting options or guest fence numbers.
  stream_recording=0;
  mtl_finish();reclaim_submissions();
  pthread_mutex_lock(&V.resources);
  pool *p=pool_find(base,1);
  int present=p && (uintptr_t)p->guest==base && p->identity==identity;
  int whole=present && pool_window_whole(p);
  pthread_mutex_unlock(&V.resources);
  if(present && !whole) {
    int replaced;
#ifdef DM_VULKAN_SHARED_POOLS
    if(shared_window_bytes)replaced=shared_pool_union_quiescent(base,identity,trigger,V.options.pool_window_bytes);
    else
#endif
      replaced=pool_migrate_quiescent(base,identity,trigger,V.options.pool_window_bytes);
    if(replaced)pool_window_promotions++;
    else {
      // Registry removal may race the isolated copy despite its lifetime pin.
      // The helper never publishes to a replacement logical pool. Recollect
      // current identity after reopen; only a still-identical failure is fatal.
      pthread_mutex_lock(&V.resources);
      p=pool_find(base,1);
      int unchanged=p && (uintptr_t)p->guest==base && p->identity==identity;
      pthread_mutex_unlock(&V.resources);
      if(unchanged)die("Vulkan pool consolidation failed; old storage retained");
    }
  }
  stream_recording=was_stream;
  if(was_stream) {stream_last_draw=V.draws;stream_head_pending=0;}
  pool_maintenance=0;
  if(reopen) {
    mtl_sub_begin();
    vk_ghost_flush();
  }
}
static int pool_window_full_reference(void) {
  return V.options.pool_mirror_reference || atomic_load(&pool_sync_requested)==1 ||
      (atomic_load(&pool_sync_requested)<0 && atomic_load(&pool_sync_active));
}
static void pool_window_before_submission(void) {
  if(!V.options.pool_window_bytes || V.cmd || pool_maintenance || !pool_window_full_reference())return;
  // A reference transition is global, before begin creates a recording owner.
  // Even an unrelated partial pool must be whole before eager sync visits it.
  for(unsigned tries=0;tries<VK_POOL_RESERVE_MAX_REQUIREMENTS;tries++) {
    uint64_t base=0,identity=0;pthread_mutex_lock(&V.resources);
    for(pool *p=pools;p;p=p->next)if(p->backings && !pool_window_whole(p)) {
      base=(uintptr_t)p->guest;identity=p->identity;break;
    }
    pthread_mutex_unlock(&V.resources);
    if(!identity)return;
    pool_policy_promote(base,identity,0,NULL);
  }
  die("Vulkan whole-reference pool registry did not settle");
}
int vk_pool_operation_begin(vk_pool_operation_collect collect,void *context) {
  if(!V.options.pool_window_bytes)return 0;
  if(!collect || pool_operation_depth==POOL_OPERATION_DEPTH)
    die("Vulkan invalid/nested pool operation plan");
  pool_operation_plan **slot=&pool_operation_plans[pool_operation_depth];
  if(!*slot) {*slot=calloc(1,sizeof **slot);if(!*slot)die("Vulkan pool operation scratch allocation");}
  pool_operation_plan *plan=*slot;
  for(unsigned tries=0;tries<VK_POOL_RESERVE_MAX_REQUIREMENTS;tries++) {
    if(plan->pin_count)die("Vulkan stale operation plan pins");
    plan->count=0;
    uint64_t descriptors=vk_texture_descriptor_epoch();
    if(!collect(context,pool_operation_add,plan))pool_operation_whole_plan(plan,"operation enumeration");
    else if(!vk_ghost_pool_ranges(pool_operation_add,plan))pool_operation_whole_plan(plan,"pending ghost enumeration");
    if(descriptors!=vk_texture_descriptor_epoch())continue;
    // Steady state is allocation-free: complete already-backed operations
    // need neither a temporary reservation array nor sorting/allocation.
    if(!pool_window_full_reference() && pool_operation_pin(plan)) {
      if(descriptors==vk_texture_descriptor_epoch()) {
        pool_window_admissions++;return (int)++pool_operation_depth;
      }
      pool_operation_unpin(plan);continue;
    }
    pool_operation_bound_nodes(plan);
    vk_pool_reserve_result result;
    int reserved=vk_pool_reserve_ranges(plan->ranges,plan->count,V.options.pool_window_bytes,&result);
    if(reserved==VK_POOL_RESERVE_NEED_WHOLE) {
      // Parent admission includes every pending reentrant ghost resource.
      // An unexpected nested crossing is a proof failure, not permission to
      // invalidate its parent's already borrowed VkBuffer/copyback target.
      pool_policy_promote(result.pool_base,result.pool_identity,1,
          result.trigger.size?&result.trigger:NULL);continue;
    }
    if(reserved!=VK_POOL_RESERVE_OK)
      die("Vulkan pool operation reservation failed status=%d VkResult=%d; old storage retained",reserved,result.allocation_error);
    pool_window_added_bytes+=result.added_buffer_bytes;pool_window_added_spans+=result.added_spans;
    if(descriptors!=vk_texture_descriptor_epoch() || !pool_operation_pin(plan))continue;
    if(descriptors!=vk_texture_descriptor_epoch()) {pool_operation_unpin(plan);continue;}
    pool_window_admissions++;
    return (int)++pool_operation_depth;
  }
  die("Vulkan pool operation registry did not settle");return 0;
}
typedef struct {const vk_pool_requirement *ranges;size_t count;} pool_range_batch;
static int pool_collect_batch(void *context,vk_pool_requirement_sink sink,void *out) {
  const pool_range_batch *batch=context;
  if(batch->count>VK_POOL_RESERVE_MAX_REQUIREMENTS || (batch->count&&!batch->ranges))return 0;
  for(size_t k=0;k<batch->count;k++)if(!sink(out,&batch->ranges[k]))return 0;
  return 1;
}
int vk_pool_operation_begin_ranges(const vk_pool_requirement *ranges,size_t count) {
  pool_range_batch batch={ranges,count};return vk_pool_operation_begin(pool_collect_batch,&batch);
}
void vk_pool_operation_end(int token) {
  if(!token)return;
  if((unsigned)token!=pool_operation_depth)die("Vulkan pool operation lifetime mismatch");
  pool_operation_unpin(pool_operation_plans[--pool_operation_depth]);
}
#ifdef DM_VULKAN_TEST_EMBED
// Functional fixtures only; normal startup selection is immutable.
void vk_test_pool_window_bytes(uint64_t bytes) {
  if(V.cmd || pool_operation_depth || pool_maintenance)die("Vulkan fixture policy switch while recording");
  V.options.pool_window_bytes=bytes;
}
#endif
