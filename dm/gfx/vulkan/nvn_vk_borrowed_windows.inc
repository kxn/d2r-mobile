// Default-OFF borrowed subwindows. One logical Pool lease supplies lifetime;
// each exact page-complete physical node owns its own exclusion/pin. No allocator
// provenance, image interpretation, whole-pool import or shader-specific rule.
enum { BORROWED_WINDOW_LIMIT=8, BORROWED_HISTORY_LIMIT=64 };
struct pool_borrowed_windows {
  struct {uint64_t begin,end;} blocked[BORROWED_HISTORY_LIMIT];
  unsigned count,active;int all_blocked;
};
static pool_borrowed_windows *borrowed_windows_state(pool *p) {
  if(!p->borrowed_windows)p->borrowed_windows=calloc(1,sizeof *p->borrowed_windows);
  return p->borrowed_windows;
}
// Registry lock required. Historical declarations are page-rounded so a byte
// alias can never allow another tracker to protect any imported page.
static void borrowed_windows_block(pool *p,uint64_t offset,uint64_t bytes) {
  if(!bytes || offset>p->size || bytes>p->size-offset)die("Borrowed history extent");
  pool_borrowed_windows *s=borrowed_windows_state(p);
  if(!s) {p->borrowed_history=1;return;}
  uint64_t begin=offset&~UINT64_C(4095),end=(offset+bytes+4095)&~UINT64_C(4095);
  if(end>p->size)end=p->size;
  for(unsigned k=0;k<s->count;k++)if(begin<=s->blocked[k].end && s->blocked[k].begin<=end) {
    if(begin<s->blocked[k].begin)s->blocked[k].begin=begin;
    if(end>s->blocked[k].end)s->blocked[k].end=end;
    return; // overlapping entries need not merge: finite union remains conservative
  }
  if(s->count==BORROWED_HISTORY_LIMIT){s->all_blocked=1;return;}
  s->blocked[s->count].begin=begin;s->blocked[s->count++].end=end;
}
static int borrowed_windows_blocked(const pool *p,uint64_t begin,uint64_t end) {
  const pool_borrowed_windows *s=p->borrowed_windows;
  if(p->borrowed_history || (s && s->all_blocked))return 1;
  if(s)for(unsigned k=0;k<s->count;k++)if(begin<s->blocked[k].end && s->blocked[k].begin<end)return 1;
  return 0;
}
static int borrowed_windows_overlap(const pool *p,uint64_t offset,uint64_t bytes) {
  if(!borrowed_subwindows_requested || !p->borrowed_windows || !p->borrowed_windows->active ||
     !bytes || offset>p->size || bytes>p->size-offset)return 0;
  for(pool_backing_span *b=p->backings;b && b->begin<offset+bytes;b=b->next)
    if(b->end>offset && b->buffer.owned_host_import)return 1;
  return 0;
}
static void borrowed_windows_free(pool *p) {
  if(p->borrowed_windows && p->borrowed_windows->active)die("Borrowed windows freed while imported");
  free(p->borrowed_windows);
}
// Called only on the ORIGINAL physical span miss with registry lock held.
// Import exactly the canonical node that the original complete plan would
// reserve, including descriptor prefix; no request truncation/cross-node glue.
static int borrowed_windows_admit(pool *p,const vk_pool_requirement *ranges,size_t count,
    uint64_t *address,uint64_t *bytes,uint64_t *identity) {
  if(!p->borrowed_lease.lifetime || p->borrowed_closing || p->borrowed_history ||
      !owned_fixture_supported || !V.options.pool_window_bytes)return 0;
  pool_borrowed_windows *s=borrowed_windows_state(p);
  if(!s || s->all_blocked || s->active==BORROWED_WINDOW_LIMIT)return 0;
  uint64_t base=(uintptr_t)p->guest;
  // First record ALL non-readonly uses in the complete bounded plan, even if
  // another requirement is an earlier eligible missing node in the same pool.
  for(size_t k=0;k<count;k++) {
    const vk_pool_requirement *r=ranges+k;
    uint64_t lo=r->address>base?r->address:base,hi=r->address+r->size<base+p->size?r->address+r->size:base+p->size;
    if(lo<hi && (r->address<base || r->size>p->size-(r->address-base) || r->writable ||
       (r->binding_kind!=1 && r->binding_kind!=VK_POOL_REQUIRE_VERTEX)))
      borrowed_windows_block(p,lo-base,hi-lo);
  }
  for(size_t k=0;k<count;k++) {
    const vk_pool_requirement *r=ranges+k;
    if(r->address<base || r->address-base>=p->size || r->size>p->size-(r->address-base))continue;
    pool_span_piece existing;
    if(pool_span_resolve_locked(p,r->address-base,r->size,&existing))continue;
    uint64_t begin,end;
    if(!pool_window_bounds(p,r,V.options.pool_window_bytes,&begin,&end) ||
       !owned_fixture_alignment || (base+begin)%owned_fixture_alignment || (end-begin)%owned_fixture_alignment ||
       end-begin>(16u<<20) || pool_window_overlaps(p,begin,end) || borrowed_windows_blocked(p,begin,end))continue;
    // A failed attach is sticky for this exact node, not every unrelated byte
    // in this logical pool. Mark only AFTER selection, then attach must check
    // that no independent alias/closing publication raced that selection.
    *address=base+begin;*bytes=end-begin;*identity=p->identity;return 1;
  }
  return 0;
}
static int borrowed_windows_attach(uint64_t address,uint64_t bytes,uint64_t identity) {
  if(!bytes || bytes>(16u<<20) || bytes>UINT64_MAX-address || !owned_fixture_alignment ||
      address%owned_fixture_alignment || bytes%owned_fixture_alignment || address%4096 || bytes%4096 ||
      pool_operation_depth)return 0;
  pthread_mutex_lock(&V.resources);pool *p=pool_find(address,bytes);
  if(!p || p->identity!=identity || !p->borrowed_lease.lifetime || p->borrowed_closing ||
      V.options.pool_mirror_reference || !V.options.pool_window_bytes) {
    pthread_mutex_unlock(&V.resources);return 0;
  }
  uint64_t begin=address-(uintptr_t)p->guest,end=begin+bytes;
  pool_borrowed_windows *s=borrowed_windows_state(p);
  if(!s || s->active==BORROWED_WINDOW_LIMIT || pool_window_overlaps(p,begin,end) ||
      borrowed_windows_blocked(p,begin,end)) {pthread_mutex_unlock(&V.resources);return 0;}
  pool_backing_span *node=calloc(1,sizeof *node);pool_owned_readonly *cert=calloc(1,sizeof *cert);
  if(!node || !cert) {
    free(node);free(cert);borrowed_windows_block(p,begin,bytes);pthread_mutex_unlock(&V.resources);return 0;
  }
  int excluded=nvn_dirty_pool_lease_exclude_window(&p->borrowed_lease,(void *)(uintptr_t)address,bytes,&cert->exclusion);
  if(!excluded) {free(node);free(cert);borrowed_windows_block(p,begin,bytes);pthread_mutex_unlock(&V.resources);return 0;}
  node->buffer=(vk_buffer){.size=bytes,.map=(void *)(uintptr_t)address,.owned_host_import=1};
  VkResult status=pool_host_buffer_create((void *)(uintptr_t)address,bytes,&node->buffer);
  if(status!=VK_SUCCESS) {
    vk_buffer_destroy(&node->buffer);
    if(!nvn_dirty_owned_release(&cert->exclusion))die("Borrowed failed window exclusion release");
    free(node);free(cert);borrowed_windows_block(p,begin,bytes);pthread_mutex_unlock(&V.resources);return 0;
  }
  pool_materialize_metadata(p);node->begin=begin;node->end=end;node->host_certificate=cert;
  if(p->backing_generation==UINT64_MAX)die("Borrowed window generation overflow");
  node->generation=++p->backing_generation;
  pool_backing_span **at=&p->backings;while(*at && (*at)->begin<begin)at=&(*at)->next;
  node->next=*at;*at=node;s->active++;
  if(atomic_fetch_add_explicit(&pool_registry_epoch,1,memory_order_release)==UINT64_MAX)die("Borrowed registry overflow");
  pthread_mutex_unlock(&V.resources);return 1;
}
static void borrowed_window_flush(pool *p,pool_backing_span *b,uint64_t logical,uint64_t bytes) {
  uint64_t offset=logical-b->begin;
  if(!b->host_certificate || !b->buffer.owned_host_mapped || logical<b->begin || offset>b->buffer.size ||
      bytes>b->buffer.size-offset)die("Borrowed window flush extent");
  if(b->buffer.properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)return;
  uint64_t atom=V.props.limits.nonCoherentAtomSize,limit=offset+bytes;
  if(!atom || (atom&(atom-1)) || limit>UINT64_MAX-(atom-1))die("Borrowed window flush atom");
  offset&=~(atom-1);limit=(limit+atom-1)&~(atom-1);
  if(limit>b->buffer.size)die("Borrowed window flush crosses imported allocation");
  VkMappedMemoryRange range={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=b->buffer.memory,.offset=offset,.size=limit-offset};
  VK_CHECK(vkFlushMappedMemoryRanges(V.device,1,&range));
#ifdef DM_VULKAN_TEST_EMBED
  printf("{\"test_details\":\"borrowed-nvn-window-flush\",\"pool_identity\":%llu,\"node_begin\":%llu,\"node_end\":%llu,"
    "\"logical\":%llu,\"read_bytes\":%llu,\"memory_offset\":%llu,\"flush_bytes\":%llu,\"atom\":%llu,\"flags\":%u}\n",
    (unsigned long long)p->identity,(unsigned long long)b->begin,(unsigned long long)b->end,
    (unsigned long long)logical,(unsigned long long)bytes,(unsigned long long)offset,(unsigned long long)(limit-offset),
    (unsigned long long)atom,b->buffer.properties);
#endif
}
// One matching node at a time. No mutation of existing GPU handles until the
// original queue, completion effects, reset and all pool references are gone.
// Unrelated imported/mirrored nodes retain handle+generation and page stamps.
static int borrowed_windows_retire(uint64_t address,uint64_t bytes,uint64_t identity) {
  if(!bytes || bytes>UINT64_MAX-address)return 0;
  pthread_mutex_lock(&V.resources);pool *p=NULL;pool_backing_span *node=NULL;
  for(pool *q=pools;q;q=q->next) {
    if(identity && q->identity!=identity)continue;
    uint64_t base=(uintptr_t)q->guest,lo=address>base?address:base,hi=address+bytes<base+q->size?address+bytes:base+q->size;
    if(lo>=hi || !q->borrowed_windows || !q->borrowed_windows->active)continue;
    for(pool_backing_span *b=q->backings;b && b->begin<hi-base;b=b->next)
      if(b->end>lo-base && b->buffer.owned_host_import) {p=q;node=b;break;}
    if(node)break;
  }
  if(!node) {
    int valid=!identity;
    if(identity)for(pool *q=pools;q;q=q->next)if(q->identity==identity)valid=1;
    pthread_mutex_unlock(&V.resources);return valid?0:-1;
  }
  uint64_t base=(uintptr_t)p->guest,begin=node->begin,end=node->end,life=p->identity;
  borrowed_windows_block(p,begin,end-begin);pthread_mutex_unlock(&V.resources);
  if(pool_operation_depth || pool_maintenance || vk_shader_operation_active() ||
     vk_texture_binding_scope_active() || vk_texture_shader_collecting())die("Borrowed retirement missed outer scope");
  pool_backing_span replacement={0};VK_CHECK(pool_span_try_allocate(&replacement,begin,end));
  int reopen=V.cmd!=VK_NULL_HANDLE,was_stream=stream_recording;pool_maintenance=1;
  if(V.cmd){vk_render_end_preserve();vk_internal_wait(vk_internal_commit());}
  stream_recording=0;mtl_finish();reclaim_submissions();
  pthread_mutex_lock(&V.resources);p=pool_find(base,1);
  if(!p || p->identity!=life || p->references || !node->buffer.owned_host_import || !node->host_certificate)
    die("Borrowed window retirement lifetime");
  pthread_mutex_lock(&pool_publication_mu);
  memcpy(replacement.buffer.map,p->guest+begin,end-begin);memcpy(p->shadow+begin,p->guest+begin,end-begin);
  for(uint64_t page=begin>>12;page<end>>12;page++) {
    p->page_upload_stamps[page]=0;p->initialized_pages[page/64]&=~(UINT64_C(1)<<(page%64));
  }
  p->initialized_first_page=p->initialized_end_page=0;
  pthread_mutex_unlock(&pool_publication_mu);
  if(p->backing_generation==UINT64_MAX || V.buffer_epoch==UINT64_MAX ||
     atomic_load(&pool_registry_epoch)==UINT64_MAX)die("Borrowed retirement generation overflow");
  vk_buffer prior=node->buffer;pool_owned_readonly *cert=node->host_certificate;
  node->buffer=replacement.buffer;node->host_certificate=NULL;node->generation=++p->backing_generation;
  p->locked_span_hint=NULL;V.buffer_epoch++;atomic_fetch_add_explicit(&pool_registry_epoch,1,memory_order_release);
  vk_buffer_destroy(&prior);
  if(!nvn_dirty_owned_release(&cert->exclusion))die("Borrowed retired window exclusion release");
  free(cert);p->borrowed_windows->active--;owned_fixture_retirements++;
  pthread_mutex_unlock(&V.resources);stream_recording=was_stream;
  if(was_stream){stream_last_draw=V.draws;stream_head_pending=0;}
  pool_maintenance=0;if(reopen){mtl_sub_begin();vk_ghost_flush();}
  // Callers expect this maintenance request to close ALL intersecting nodes.
  if(borrowed_windows_retire(address,bytes,identity)<0)return -1;
  return 1;
}
