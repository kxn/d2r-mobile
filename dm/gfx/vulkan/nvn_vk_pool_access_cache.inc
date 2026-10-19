// Recording-owner metadata reuse only. Retired publication already follows
// GPU fence, all CPU effects, pool-reference release and command-pool reset.
// The former owner can have been deleted: never dereference use->owner here.
enum { POOL_ACCESS_CACHE_BYTES=512u<<10,POOL_ACCESS_CACHE_ENTRIES=16 };
static uint64_t pool_access_cache_bytes(const pool_gpu_access *use) {
  if(use->allocated_stages&~((1u<<POOL_STAGE_COUNT)-1))die("Vulkan pool access stage mask");
  unsigned planes=3+__builtin_popcount(use->allocated_stages);
  if(use->bitmap_words>(POOL_ACCESS_CACHE_BYTES-sizeof *use)/(planes*sizeof(uint64_t)))return 0;
  return sizeof *use+use->bitmap_words*planes*sizeof(uint64_t);
}
static void pool_access_free(pool_gpu_access *use) {
  free(use->pages);
  for(unsigned k=0;k<POOL_STAGE_COUNT;k++)free(use->stage_pages[k]);
  free(use);
}
static pool_gpu_access *pool_access_cache_take(submit_resources *r,uint64_t words) {
  if(V.options.pool_access_cache_reference)return NULL;
  pool_gpu_access **at=&r->cached_gpu_accesses;
  while(*at) {
    pool_gpu_access *use=*at;
    if(use->bitmap_words!=words) {at=&use->next;continue;}
    uint64_t bytes=pool_access_cache_bytes(use);
    if(use->owner || !bytes || !r->cached_gpu_access_count || r->cached_gpu_access_bytes<bytes)
      die("Vulkan cached pool access ownership");
    *at=use->next;r->cached_gpu_access_count--;r->cached_gpu_access_bytes-=bytes;
    use->next=NULL;return use;
  }
  return NULL;
}
static void pool_access_cache_retire(submit_resources *r,pool_gpu_access *use) {
  uint64_t bytes=pool_access_cache_bytes(use);
  if(V.options.pool_access_cache_reference || !bytes ||
      r->cached_gpu_access_count==POOL_ACCESS_CACHE_ENTRIES ||
      bytes>POOL_ACCESS_CACHE_BYTES-r->cached_gpu_access_bytes) {pool_access_free(use);return;}
  // Every upload, overwrite and stage bit is a subset of the pages union:
  // record_pool_range marks pages before any subset; vk_pool_overwritten calls
  // record_pool_use before its own writes. No other bitmap writers exist.
  // Scan one plane, clear only its nonzero words across all allocated planes.
  // Dense ranges add this read; sparse ranges avoid whole-plane zero stores.
  for(uint64_t word=0;word<use->bitmap_words;word++)if(use->pages[word]) {
    use->pages[word]=use->upload_pages[word]=use->overwritten_pages[word]=0;
    unsigned stages=use->allocated_stages;
    while(stages) {unsigned k=__builtin_ctz(stages);stages&=stages-1;use->stage_pages[k][word]=0;}
  }
  use->owner=NULL;use->last_first_page=use->last_end_page=0;
  use->last_stages=0;use->last_initial=0;
  use->next=r->cached_gpu_accesses;r->cached_gpu_accesses=use;
  r->cached_gpu_access_count++;r->cached_gpu_access_bytes+=bytes;
}
static void pool_access_cache_trim(submit_resources *r) {
  while(r->cached_gpu_accesses) {
    pool_gpu_access *use=r->cached_gpu_accesses;r->cached_gpu_accesses=use->next;
    uint64_t bytes=pool_access_cache_bytes(use);
    if(use->owner || !bytes || !r->cached_gpu_access_count || r->cached_gpu_access_bytes<bytes)
      die("Vulkan pool access trim ownership");
    r->cached_gpu_access_count--;r->cached_gpu_access_bytes-=bytes;pool_access_free(use);
  }
  if(r->cached_gpu_access_count || r->cached_gpu_access_bytes)die("Vulkan pool access trim accounting");
}
#ifdef DM_VULKAN_TEST_EMBED
uintptr_t vk_test_pool_access_bitmap(uint64_t address) {
  if(!recording_resources)return 0;
  for(pool_gpu_access *use=recording_resources->gpu_accesses;use;use=use->next)
    if(address>=(uintptr_t)use->owner->guest && address-(uintptr_t)use->owner->guest<use->owner->size)
      return (uintptr_t)use->pages;
  return 0;
}
int vk_test_pool_access_cached(uintptr_t bitmap,uint64_t *bytes,unsigned *count) {
  int found=0;*bytes=0;*count=0;
  for(submit_resources *r=available_resources;r;r=r->next) {
    *bytes+=r->cached_gpu_access_bytes;*count+=r->cached_gpu_access_count;
    for(pool_gpu_access *use=r->cached_gpu_accesses;use;use=use->next)found|=(uintptr_t)use->pages==bitmap;
  }
  if(recording_resources) {
    *bytes+=recording_resources->cached_gpu_access_bytes;*count+=recording_resources->cached_gpu_access_count;
    for(pool_gpu_access *use=recording_resources->cached_gpu_accesses;use;use=use->next)found|=(uintptr_t)use->pages==bitmap;
  }
  return found;
}
#endif
