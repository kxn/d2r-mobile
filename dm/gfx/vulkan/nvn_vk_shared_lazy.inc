// Opt-in, exact-provider coherent host imports. No original Buffer/memory is
// replaced: existing command buffers, descriptors and nested borrows keep the
// same handle/origin until the logical Pool's original close/ref drain.
enum { SHARED_LAZY_NODE_LIMIT=256 };
#define SHARED_LAZY_POOL_BYTES (UINT64_C(512)<<20)
#define SHARED_LAZY_GLOBAL_BYTES (UINT64_C(2)<<30)
static uint64_t shared_lazy_uncovered_locked(const pool *p,uint64_t begin,uint64_t end) {
  uint64_t at=begin,extra=0;
  while(at<end) {
    uint64_t covered=at,next=end;
    for(unsigned chain=0;chain<2;chain++)
      for(const pool_backing_span *b=chain?p->shared_covers:p->backings;b;b=b->next) {
        if(b->begin<=at && b->end>covered)covered=b->end;
        else if(b->begin>at && b->begin<next)next=b->begin;
      }
    if(covered>at)at=covered<end?covered:end;
    else {extra+=next-at;at=next;}
  }
  return extra;
}
static pool_backing_span *shared_lazy_ensure_locked(pool *p,uint64_t offset,uint64_t bytes) {
  if(!p || !p->shared_lazy_eligible || p->shared_closing || !bytes ||
      offset>p->size || bytes>p->size-offset || !shared_window_bytes)
    die("Shared lazy range/identity/closing violation");
  pool_span_piece prior;
  if(pool_span_resolve(p,offset,bytes,&prior))return prior.backing;
  uint64_t window=shared_window_bytes,begin=offset&~(window-1),last=offset+bytes;
  if(last>UINT64_MAX-(window-1))die("Shared lazy rounded extent overflow");
  uint64_t end=(last+window-1)&~(window-1);if(end>p->size)end=p->size;
  uint64_t length=end-begin,total=atomic_load_explicit(&shared_lazy_import_bytes,memory_order_relaxed);
  // Deliberately fail the EXPERIMENT rather than importing a whole pool,
  // draining the queue or silently returning to mirrored private storage.
  // Repeated imports share physical pages but still consume BO/GPUVA/heap
  // accounting. These are policy guards, not a claim of zero memory cost.
  if(p->shared_import_nodes==SHARED_LAZY_NODE_LIMIT ||
      length>SHARED_LAZY_POOL_BYTES-p->shared_import_bytes ||
      total>SHARED_LAZY_GLOBAL_BYTES || length>SHARED_LAZY_GLOBAL_BYTES-total)
    die("Shared lazy experimental import budget exhausted; candidate incomplete: "
        "pool=%p size=%llu offset=%llu request=%llu rounded_begin=%llu rounded_end=%llu "
        "nodes=%u imported=%llu global=%llu",
        (void *)p->guest,(unsigned long long)p->size,(unsigned long long)offset,
        (unsigned long long)bytes,(unsigned long long)begin,(unsigned long long)end,
        p->shared_import_nodes,(unsigned long long)p->shared_import_bytes,(unsigned long long)total);
  if(p->backing_generation==UINT64_MAX)die("Shared lazy backing generation exhausted");
  pool_backing_span *fresh=calloc(1,sizeof *fresh);
  if(!fresh)die("Shared lazy backing node allocation");
  VkResult result=shared_span_try_allocate(p,fresh,begin,end);
  if(result==VK_SUCCESS && !(fresh->buffer.properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
    result=VK_ERROR_INVALID_EXTERNAL_HANDLE;
  if(result!=VK_SUCCESS) {
    vk_buffer_destroy(&fresh->buffer);free(fresh);
    die("Shared lazy import failed=%d; prior data/handles intact, no online fallback",result);
  }
  uint64_t extra=shared_lazy_uncovered_locked(p,begin,end);
  int overlap=0;
  for(pool_backing_span *b=p->backings;b && b->begin<end;b=b->next)
    if(b->end>begin) {overlap=1;break;}
  fresh->generation=++p->backing_generation;
  if(overlap) {
    fresh->next=p->shared_covers;p->shared_covers=fresh;
    p->shared_cover_nodes++;atomic_fetch_add(&shared_lazy_cover_nodes,1);
  } else {
    pool_backing_span **at=&p->backings;while(*at && (*at)->begin<begin)at=&(*at)->next;
    fresh->next=*at;*at=fresh;
  }
  shared_buffer_index_add_locked(p,fresh);
  p->shared_properties=fresh->buffer.properties;
  p->shared_import_nodes++;p->shared_import_bytes+=length;p->shared_unique_bytes+=extra;
  atomic_fetch_add(&shared_lazy_import_nodes,1);
  atomic_fetch_add(&shared_lazy_import_bytes,length);
  atomic_fetch_add(&shared_lazy_unique_bytes,extra);
  // Append does not invalidate old physical nodes or dirty/content epochs.
  // The canonical sorted index alone notices its own topology generation.
  return fresh;
}
void vk_pool_lazy_window_stats(int *active,uint64_t *unique,uint64_t *mapped,
    uint64_t *nodes,uint64_t *covers) {
  *active=shared_lazy_active;
  *unique=atomic_load_explicit(&shared_lazy_unique_bytes,memory_order_relaxed);
  *mapped=atomic_load_explicit(&shared_lazy_import_bytes,memory_order_relaxed);
  *nodes=atomic_load_explicit(&shared_lazy_import_nodes,memory_order_relaxed);
  *covers=atomic_load_explicit(&shared_lazy_cover_nodes,memory_order_relaxed);
}
