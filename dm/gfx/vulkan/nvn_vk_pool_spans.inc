// Pool logical coordinates and physical VkBuffer coordinates are distinct.
// No caller may infer a physical offset from a guest/pool offset. Backings are
// disjoint, sorted, immutable for the retained pool's current lifetime. The
// initial production policy creates one [0,size) node; operation preflight and
// a separately proved migration are required before changing that policy.
typedef struct {
  pool_backing_span *backing;
  uint64_t logical,offset,size;
} pool_span_piece;
typedef struct { pool *owner;uint64_t at,end; } pool_span_iter;
#ifdef DM_VULKAN_SHARED_POOLS
// V.resources owns publication and teardown. Buffer imports remain immutable
// until the original Pool/reference drain; native imports use their separate
// index and release predicate. Typed selection never extends that lifetime.
static void shared_buffer_index_add_locked(pool *p,pool_backing_span *b,int cover) {
  b->shared_coverage=(alias_range_node){.start=b->begin,.end=b->end,
    .max_end=b->end,.serial=b->generation,.owner=b,.height=1,.kind=!!cover};
  int inserted=0;
  p->shared_buffer_index=alias_range_insert(p->shared_buffer_index,&b->shared_coverage,&inserted);
  if(!inserted)die("Shared Buffer coverage publication mismatch");
}
static uint64_t shared_buffer_cover_end(const alias_range_node *node,uint64_t at) {
  if(!node || node->max_end<=at)return at;
  uint64_t covered=shared_buffer_cover_end(node->left,at);
  if(node->start>at)return covered;
  const pool_backing_span *b=node->owner;
  if(b->end>covered)covered=b->end;
  uint64_t right=shared_buffer_cover_end(node->right,at);
  return right>covered?right:covered;
}
static pool_backing_span *shared_lazy_cover(const pool *p,uint64_t offset,uint64_t size) {
  if(!p->shared_lazy_eligible || offset>p->size || size>p->size-offset)return NULL;
  for(pool_backing_span *b=p->shared_covers;b;b=b->next)
    if(offset>=b->begin && (offset<b->end || (!size && offset==p->size && offset==b->end)) &&
        size<=b->end-offset)return b;
  return NULL;
}
#endif
static int pool_span_iter_begin(pool *p,uint64_t offset,uint64_t size,pool_span_iter *it) {
  if(offset>p->size || size>p->size-offset)return 0;
  *it=(pool_span_iter){p,offset,offset+size};return 1;
}
// 1 = a fully backed prefix, 0 = end, -1 = a hole/invalid canonical backing.
// Never jump over a hole and never concatenate it into a physical range.
static int pool_span_iter_next(pool_span_iter *it,pool_span_piece *out) {
  if(it->at==it->end)return 0;
#ifdef DM_VULKAN_SHARED_POOLS
  if(it->owner->shared_lazy_eligible) {
    // Iterate the logical UNION, not both imports' duplicate bytes. This is
    // only a backed prefix; a binding still requires one complete VkBuffer.
    pool_backing_span *best=NULL;
    for(unsigned chain=0;chain<2;chain++)
      for(pool_backing_span *b=chain?it->owner->shared_covers:it->owner->backings;b;b=b->next)
        if(b->begin<=it->at && b->end>it->at && (!best || b->end>best->end))best=b;
    if(!best)return -1;
    uint64_t end=best->end<it->end?best->end:it->end,offset=it->at-best->begin;
    if(!best->buffer.handle || !best->generation || end<=it->at ||
        offset>best->buffer.size || end-it->at>best->buffer.size-offset)return -1;
    *out=(pool_span_piece){best,it->at,offset,end-it->at};it->at=end;return 1;
  }
#endif
  for(pool_backing_span *b=it->owner->backings;b;b=b->next) {
    if(it->at<b->begin)return -1;
    if(it->at>=b->end)continue;
    uint64_t end=b->end<it->end?b->end:it->end;
    uint64_t offset=it->at-b->begin,size=end-it->at;
    if(!b->buffer.handle || !b->generation || b->begin>=b->end ||
       b->end>it->owner->size || offset>b->buffer.size || size>b->buffer.size-offset)return -1;
    *out=(pool_span_piece){b,it->at,offset,size};it->at=end;return 1;
  }
  return -1;
}
static int pool_span_resolve(pool *p,uint64_t offset,uint64_t size,pool_span_piece *out) {
#ifdef DM_VULKAN_SHARED_POOLS
  pool_backing_span *cover=shared_lazy_cover(p,offset,size);
  if(cover) {*out=(pool_span_piece){cover,offset,offset-cover->begin,size};return 1;}
#endif
  if(!size) {
    // Preserve the legacy address-only lookup, including the exact pool tail.
    // It records no GPU access; it cannot prove a nonempty physical range.
    if(offset>p->size)return 0;
    for(pool_backing_span *b=p->backings;b;b=b->next)
      if(offset>=b->begin && (offset<b->end || (offset==p->size && offset==b->end))) {
        *out=(pool_span_piece){b,offset,offset-b->begin,0};return 1;
      }
    return 0;
  }
#ifdef DM_VULKAN_SHARED_POOLS
  if(p->shared_lazy_eligible) {
    if(offset>p->size || size>p->size-offset)return 0;
    for(pool_backing_span *b=p->backings;b;b=b->next)
      if(offset>=b->begin && offset<b->end && size<=b->end-offset) {
        *out=(pool_span_piece){b,offset,offset-b->begin,size};return 1;
      }
    return 0;
  }
#endif
  pool_span_iter it;if(!pool_span_iter_begin(p,offset,size,&it))return 0;
  return pool_span_iter_next(&it,out)==1 && it.at==it.end;
}
#ifdef DM_VULKAN_SHARED_POOLS
// Same authoritative Buffer imports as the coverage tree, but a binding must
// select ONE original physical handle. start/max_end prune unrelated ranges;
// covers retain list priority (newest generation first), canonical ranges are
// disjoint. Do not borrow native-image nodes or concatenate adjacent imports.
static void shared_buffer_span_select(const alias_range_node *n,uint64_t first,uint64_t end,
    pool_backing_span **cover,pool_backing_span **canonical) {
  if(!n || n->max_end<end)return;
  shared_buffer_span_select(n->left,first,end,cover,canonical);
  if(n->start>first)return;
  if(n->end>=end) {
    pool_backing_span *b=n->owner;
    if(n->kind) {if(!*cover || b->generation>(*cover)->generation)*cover=b;}
    else *canonical=b; // production canonical imports are disjoint
  }
  shared_buffer_span_select(n->right,first,end,cover,canonical);
}
static int shared_lazy_span_resolve(pool *p,uint64_t offset,uint64_t size,pool_span_piece *out) {
  if(offset>p->size || size>p->size-offset)return 0;
  pool_backing_span *cover=NULL,*canonical=NULL;
  shared_buffer_span_select(p->shared_buffer_index,offset,offset+size,&cover,&canonical);
  pool_backing_span *b=cover?cover:canonical;
  if(!b)return 0;
  *out=(pool_span_piece){b,offset,offset-b->begin,size};return 1;
}
#endif
// Recording-owner MISS only, after original operation/retained hints and a
// current Pool reference/registry epoch check. Production lazy startup requires
// the dedicated NVN owner. Only that owner inserts Buffer nodes; completion
// cannot rotate this tree, and owner-FIFO close waits all refs before removal.
// A reference alone would NOT qualify an arbitrary unlocked AVL reader.
static int pool_span_resolve_recorded(pool *p,uint64_t offset,uint64_t size,pool_span_piece *out) {
#ifdef DM_VULKAN_SHARED_POOLS
  if(p->shared_lazy_eligible && size && !pool_maintenance)
    return shared_lazy_span_resolve(p,offset,size,out);
#endif
  return pool_span_resolve(p,offset,size,out);
}
static pool_span_piece pool_span_require(pool *p,uint64_t offset,uint64_t size) {
  pool_span_piece out;
  if(!pool_span_resolve(p,offset,size,&out))
    die("Vulkan pool range requires operation preflight: identity=%llu offset=%llu bytes=%llu",
        (unsigned long long)p->identity,(unsigned long long)offset,(unsigned long long)size);
  return out;
}
// The caller owns a current submission reference to p and has checked its
// registry epoch. Referenced nodes cannot be migrated or destroyed. The hint
// proves only physical address coverage: initialization, dirty checks and
// dependency recording still run on every lookup. A miss uses the original
// resolver at the call site, including its hole/cross-node failure and whole-pool behavior.
static int pool_span_retained_hint(pool *p,pool_backing_span *b,
    uint64_t offset,uint64_t size,pool_span_piece *out) {
  if(size && b && (b->begin || b->end!=p->size) &&
      offset>=b->begin && offset<b->end && size<=b->end-offset &&
      b->generation && b->buffer.handle && b->begin<b->end && b->end<=p->size &&
      offset-b->begin<=b->buffer.size && size<=b->buffer.size-(offset-b->begin)) {
    *out=(pool_span_piece){b,offset,offset-b->begin,size};return 1;
  }
  return 0;
}
// Sorted pointer metadata for the locked search MISS path. Physical ranges
// are immutable; every append/replacement advances backing_generation under
// V.resources. Rebuild only on that topology change, never on a CPU/GPU write.
// An allocation failure or unusually large chain keeps the original resolver
// for this generation rather than retrying allocation on every draw.
enum { POOL_SPAN_INDEX_MAX=4096 };
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
static int pool_span_index_test_oom;
#endif
static __attribute__((noinline)) void pool_span_index_build_locked(pool *p) {
  p->locked_span_index_generation=p->backing_generation;
  p->locked_span_index_count=0;
  unsigned count=0;uint64_t previous_end=0;
  for(pool_backing_span *b=p->backings;b;b=b->next) {
    if(count==POOL_SPAN_INDEX_MAX || b->begin>=b->end || b->end>p->size ||
        (count && b->begin<previous_end))return;
    previous_end=b->end;count++;
  }
  if(count>p->locked_span_index_capacity) {
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
    if(pool_span_index_test_oom)return;
#endif
    unsigned capacity=p->locked_span_index_capacity?p->locked_span_index_capacity:8;
    while(capacity<count)capacity*=2;
    pool_backing_span **index=realloc(p->locked_span_index,capacity*sizeof *index);
    if(!index)return;
    p->locked_span_index=index;p->locked_span_index_capacity=capacity;
  }
  unsigned at=0;
  for(pool_backing_span *b=p->backings;b;b=b->next)p->locked_span_index[at++]=b;
  p->locked_span_index_count=count;
}
// V.resources MUST be held. This searches physical topology only. It does not
// admit a range, join adjacent backings, retain objects or publish GPU access.
static int pool_span_search_locked(pool *p,uint64_t offset,uint64_t size,pool_span_piece *out) {
#ifdef DM_VULKAN_SHARED_POOLS
  if(p->shared_lazy_eligible)return pool_span_resolve_recorded(p,offset,size,out);
#endif
  if(!size || !p->backings || !p->backings->next)
    return pool_span_resolve(p,offset,size,out);
  if(offset>p->size || size>p->size-offset)return 0;
  if(p->locked_span_index_generation!=p->backing_generation)
    pool_span_index_build_locked(p);
  if(!p->locked_span_index_count)return pool_span_resolve(p,offset,size,out);
  unsigned first=0,last=p->locked_span_index_count;
  while(first<last) {
    unsigned middle=first+(last-first)/2;
    if(p->locked_span_index[middle]->begin<=offset)first=middle+1;
    else last=middle;
  }
  if(!first)return 0;
  pool_backing_span *b=p->locked_span_index[first-1];
  if(offset>=b->end || size>b->end-offset || !b->buffer.handle || !b->generation ||
      b->begin>=b->end || b->end>p->size || offset-b->begin>b->buffer.size ||
      size>b->buffer.size-(offset-b->begin))return 0;
  *out=(pool_span_piece){b,offset,offset-b->begin,size};return 1;
}
// V.resources MUST be held. Kept separate from the original retained-owner
// resolver: completion/recording callers never read or write the hint. A hit
// proves only the same physical mapping, never content freshness or visibility.
// Whole/single-node pools keep the original resolver without any hint access.
static int pool_span_resolve_locked(pool *p,uint64_t offset,uint64_t size,pool_span_piece *out) {
#ifdef DM_VULKAN_SHARED_POOLS
  if(p->shared_lazy_eligible)return pool_span_resolve_recorded(p,offset,size,out);
#endif
  if(!size || !p->backings || !p->backings->next)
    return pool_span_resolve(p,offset,size,out);
  pool_backing_span *b=p->locked_span_hint;
  if(b && offset>=b->begin && offset<b->end && size<=b->end-offset &&
      b->buffer.handle && b->generation && b->begin<b->end && b->end<=p->size &&
      offset-b->begin<=b->buffer.size && size<=b->buffer.size-(offset-b->begin)) {
    *out=(pool_span_piece){b,offset,offset-b->begin,size};return 1;
  }
  if(!pool_span_search_locked(p,offset,size,out))return 0;
  p->locked_span_hint=out->backing;return 1;
}
static pool_span_piece pool_span_require_locked(pool *p,uint64_t offset,uint64_t size) {
  pool_span_piece out;
  if(!pool_span_resolve_locked(p,offset,size,&out))
    die("Vulkan pool range requires operation preflight: identity=%llu offset=%llu bytes=%llu",
        (unsigned long long)p->identity,(unsigned long long)offset,(unsigned long long)size);
  return out;
}
static VkResult pool_span_try_allocate(pool_backing_span *b,uint64_t begin,uint64_t end) {
  VkResult result=buffer_try_create(&b->buffer,end-begin,
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT|
      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|
      VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT|
      VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,0);
  if(result==VK_SUCCESS) { b->begin=begin;b->end=end; }
  return result;
}
static void pool_span_create(pool *p,pool_backing_span *b,uint64_t begin,uint64_t end) {
  // Keep logical and physical page/alignment origins consistent. The final
  // tail need not be page sized; its real buffer extent remains exact.
  if(begin>=end || end>p->size || (begin&((UINT64_C(1)<<GPU_ACCESS_SHIFT)-1)) ||
     p->backing_generation==UINT64_MAX)die("Vulkan canonical pool backing bounds");
  VK_CHECK(pool_span_try_allocate(b,begin,end));
  b->generation=++p->backing_generation;
}
static void pool_span_chain_destroy(pool *p,pool_backing_span *b) {
  // Retirement is registry locked, including pool removal after its refs drain.
  p->locked_span_hint=NULL;
  free(p->locked_span_index);p->locked_span_index=NULL;
  p->locked_span_index_generation=0;
  p->locked_span_index_count=p->locked_span_index_capacity=0;
  while(b) {
    pool_backing_span *next=b->next;
#ifdef DM_VULKAN_SHARED_POOLS
    if(p->shared_lazy_eligible) {
      int removed=0;
      p->shared_buffer_index=alias_range_remove(p->shared_buffer_index,&b->shared_coverage,&removed);
      if(!removed)die("Shared Buffer coverage retirement mismatch");
    }
#endif
    vk_buffer_destroy(&b->buffer);
    if(b!=&p->whole)free(b);
    b=next;
  }
}
static void pool_spans_destroy(pool *p) {
  pool_span_chain_destroy(p,p->backings);p->backings=NULL;
#ifdef DM_VULKAN_SHARED_POOLS
  if(p->shared_buffer_index)die("Shared Buffer coverage index outlived imports");
#endif
}
// Caller preserves the original publication exclusion and GPU-access checks.
static void pool_spans_copy_from_guest(pool *p,uint64_t offset,uint64_t size) {
  pool_span_iter it;if(!pool_span_iter_begin(p,offset,size,&it))die("Vulkan pool copy range");
  pool_span_piece piece;int next;
  while((next=pool_span_iter_next(&it,&piece))==1) {
    if(!piece.backing->buffer.map)die("Vulkan pool backing not host mapped");
    memcpy(piece.backing->buffer.map+piece.offset,p->guest+piece.logical,piece.size);
  }
  if(next<0)die("Vulkan pool copy across unbacked hole");
}
// Short-lived physical proof only: the caller already owns a pool reference,
// holds publication exclusion and ruled out pending GPU access. The retained
// reference, not the publication mutex, prevents physical retirement. Never combine
// adjacent nodes or cross a hole, even when their logical ranges are adjacent.
static int pool_upload_page_span(pool *p,uint64_t offset,uint64_t size,
    unsigned count,pool_span_piece *out) {
  return count>1 && size && pool_span_resolve(p,offset,size,out) && out->backing->buffer.map;
}
static int pool_spans_copy_changed_page(pool *p,uint64_t offset,uint64_t size,
    const VkBufferCopy *runs,unsigned count) {
  pool_span_piece page;
  if(!pool_upload_page_span(p,offset,size,count,&page))return 0;
  // Scanner runs are exact subsets of this page. Validate all bounds before
  // copying so a malformed run can never partly publish through this proof.
  for(unsigned k=0;k<count;k++)
    if(runs[k].dstOffset<offset || runs[k].dstOffset-offset>size ||
       runs[k].size>size-(runs[k].dstOffset-offset))die("Vulkan changed page run bounds");
  for(unsigned k=0;k<count;k++)
    memcpy(page.backing->buffer.map+page.offset+(runs[k].dstOffset-offset),
           p->guest+runs[k].dstOffset,runs[k].size);
  return 1;
}
static uint64_t pool_spans_bytes(const pool *p) {
  uint64_t bytes=0;for(const pool_backing_span *b=p->backings;b;b=b->next)bytes+=b->end-b->begin;
  return bytes;
}
#if defined(DM_VULKAN_SHARED_POOLS) && defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
// Compare exact selected physical identities with the unchanged linear oracle.
// Synthetic topology tests do not claim GPU byte/lifetime qualification; the
// original lazy alias fixture below separately exercises real imports/content.
unsigned vk_test_shared_lazy_physical_index(void) {
  enum {N=7};pool p={.size=64,.shared_lazy_eligible=1};pool_backing_span b[N]={0};
  const uint64_t bounds[N][2]={{0,8},{0,16},{8,24},{8,32},{0,32},{40,64},{24,56}};
  const int cover[N]={0,1,0,1,1,0,1};unsigned checks=0;
  for(unsigned n=0;n<N;n++) {
    b[n].begin=bounds[n][0];b[n].end=bounds[n][1];b[n].generation=n+1;
    b[n].buffer.handle=(VkBuffer)(uintptr_t)(n+1);b[n].buffer.size=b[n].end-b[n].begin;
    pool_backing_span **at=cover[n]?&p.shared_covers:&p.backings;
    if(!cover[n])while(*at && (*at)->begin<b[n].begin)at=&(*at)->next;
    b[n].next=*at;*at=&b[n];shared_buffer_index_add_locked(&p,&b[n],cover[n]);
    for(uint64_t first=0;first<=65;first++)for(uint64_t size=0;size<=66;size++) {
      pool_span_piece linear,indexed;int a=pool_span_resolve(&p,first,size,&linear);
      int c=pool_span_resolve_recorded(&p,first,size,&indexed);
      if(a!=c || (a && (linear.backing!=indexed.backing || linear.offset!=indexed.offset ||
          linear.logical!=indexed.logical || linear.size!=indexed.size)))die("Lazy physical index differs from original linear resolver");
      checks++;
    }
    if(n==2) {pool_span_piece piece;
      if(!pool_span_resolve_recorded(&p,12,2,&piece) || piece.backing!=&b[1])
        die("Older cover lost priority to newer canonical Buffer");
      checks++;
    }
  }
  pool_span_piece piece;
  if(!pool_span_retained_hint(&p,&b[0],0,4,&piece) || piece.backing!=&b[0])
    die("Lazy physical index changed prior retained handle");
  checks++;
  if(pool_span_resolve_recorded(&p,32,32,&piece))die("Lazy physical index stitched adjacent Buffer coverage");
  checks++;
  if(pool_span_resolve_recorded(&p,1,UINT64_MAX,&piece))die("Lazy physical index overflow accepted");
  checks++;
  if(!pool_span_resolve_recorded(&p,64,0,&piece) || piece.backing!=&b[5])
    die("Lazy physical index changed zero-length Pool tail");
  checks++;
  // This query allocates nothing and must not depend on the canonical index's
  // old OOM state. Maintenance remains the unchanged linear resolver.
  int saved_oom=pool_span_index_test_oom;pool_span_index_test_oom=1;
  if(!pool_span_resolve_locked(&p,12,2,&piece) || piece.backing!=&b[4])
    die("Lazy physical index depends on canonical allocation");
  checks++;
  pool_span_index_test_oom=saved_oom;
  pool_maintenance++;
  if(!pool_span_resolve_recorded(&p,12,2,&piece) || piece.backing!=&b[4])die("Lazy maintenance physical identity changed");
  pool_maintenance--;checks++;
  for(unsigned n=0;n<N;n++) {
    int removed=0;p.shared_buffer_index=alias_range_remove(p.shared_buffer_index,&b[n].shared_coverage,&removed);
    if(!removed)die("Lazy physical index removal mismatch");
  checks++;
  }
  if(p.shared_buffer_index)die("Lazy physical index outlived Buffer nodes");
  checks++;
  return checks;
}
#endif
