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
static int pool_span_iter_begin(pool *p,uint64_t offset,uint64_t size,pool_span_iter *it) {
  if(offset>p->size || size>p->size-offset)return 0;
  *it=(pool_span_iter){p,offset,offset+size};return 1;
}
// 1 = a fully backed prefix, 0 = end, -1 = a hole/invalid canonical backing.
// Never jump over a hole and never concatenate it into a physical range.
static int pool_span_iter_next(pool_span_iter *it,pool_span_piece *out) {
  if(it->at==it->end)return 0;
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
  pool_span_iter it;if(!pool_span_iter_begin(p,offset,size,&it))return 0;
  return pool_span_iter_next(&it,out)==1 && it.at==it.end;
}
static pool_span_piece pool_span_require(pool *p,uint64_t offset,uint64_t size) {
  pool_span_piece out;
  if(!pool_span_resolve(p,offset,size,&out))
    die("Vulkan pool range requires operation preflight: identity=%llu offset=%llu bytes=%llu",
        (unsigned long long)p->identity,(unsigned long long)offset,(unsigned long long)size);
  return out;
}
static void pool_span_create(pool *p,pool_backing_span *b,uint64_t begin,uint64_t end) {
  // Keep logical and physical page/alignment origins consistent. The final
  // tail need not be page sized; its real buffer extent remains exact.
  if(begin>=end || end>p->size || (begin&((UINT64_C(1)<<GPU_ACCESS_SHIFT)-1)) ||
     p->backing_generation==UINT64_MAX)die("Vulkan canonical pool backing bounds");
  b->begin=begin;b->end=end;b->generation=++p->backing_generation;
  vk_buffer_create(&b->buffer,end-begin,
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT|
      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|
      VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT|
      VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
}
static void pool_span_chain_destroy(pool *p,pool_backing_span *b) {
  while(b) {
    pool_backing_span *next=b->next;vk_buffer_destroy(&b->buffer);
    if(b!=&p->whole)free(b);
    b=next;
  }
}
static void pool_spans_destroy(pool *p) {
  pool_span_chain_destroy(p,p->backings);p->backings=NULL;
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
static uint64_t pool_spans_bytes(const pool *p) {
  uint64_t bytes=0;for(const pool_backing_span *b=p->backings;b;b=b->next)bytes+=b->end-b->begin;
  return bytes;
}
