// Experimental CPU visibility exclusions. Metadata only, not measurement.
// Sticky page coverage is conservative; raw GPU data remains authoritative on
// unexposed pages. Writer/completion authority is tracked separately.
struct pool_cpu_exposure { uint64_t words;uint64_t pages[]; };
static _Atomic int pool_defer_publication_enabled;
static _Atomic int pool_defer_publication_requested=-1;
void mtl_pool_defer_publication(int enabled) {
  if(enabled!=0 && enabled!=1)die("Vulkan deferred pool publication expects 0 or 1");
  if(V.instance || V.device)die("Vulkan deferred pool publication is startup-only");
  atomic_store_explicit(&pool_defer_publication_requested,enabled,memory_order_release);
}
int mtl_pool_defer_publication_mode(void) {
  return atomic_load_explicit(&pool_defer_publication_enabled,memory_order_acquire);
}
static int pool_publication_candidate(const pool *p) {
  return V.options.pool_defer_publication && p->nvn_flags_known &&
      (p->nvn_flags&7u)==NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS &&
      !((uintptr_t)p->guest%4096) && !(p->size%4096);
}
static int pool_cpu_exposed(pool *p,uint64_t offset,uint64_t size) {
  if(p->cpu_exposure_full)return 1;
  if(!p->cpu_exposure || !size)return 0;
  uint64_t first=offset>>12,last=(offset+size-1)>>12;
  for(uint64_t word=first/64;word<=last/64;word++) {
    uint64_t bits=UINT64_MAX;
    if(word==first/64)bits&=UINT64_MAX<<(first%64);
    if(word==last/64)bits&=UINT64_MAX>>(63-last%64);
    if(p->cpu_exposure->pages[word]&bits)return 1;
  }
  return 0;
}
static int pool_cpu_fully_exposed(pool *p,uint64_t offset,uint64_t size) {
  if(p->cpu_exposure_full)return 1;
  if(!p->cpu_exposure || !size)return 0;
  uint64_t first=offset>>12,last=(offset+size-1)>>12;
  for(uint64_t word=first/64;word<=last/64;word++) {
    uint64_t bits=UINT64_MAX;
    if(word==first/64)bits&=UINT64_MAX<<(first%64);
    if(word==last/64)bits&=UINT64_MAX>>(63-last%64);
    if((p->cpu_exposure->pages[word]&bits)!=bits)return 0;
  }
  return 1;
}
// V.resources protects publication of this lifetime's immutable/bounded map.
static void pool_cpu_expose(pool *p,uint64_t offset,uint64_t size) {
  if(!size || !pool_publication_candidate(p))return;
  if(offset>p->size || size>p->size-offset)die("Vulkan CPU exposure bounds");
  if(p->cpu_exposure_full)return;
  if(!p->cpu_exposure) {
    uint64_t pages=p->size/4096+!!(p->size%4096),words=pages/64+!!(pages%64);
    if(words>(1u<<17) || !(p->cpu_exposure=calloc(1,sizeof *p->cpu_exposure+words*sizeof(uint64_t)))) {
      p->cpu_exposure_full=1;return; // metadata limit/OOM never allows elision
    }
    p->cpu_exposure->words=words;
  }
  uint64_t first=offset>>12,last=(offset+size-1)>>12;
  for(uint64_t word=first/64;word<=last/64;word++) {
    uint64_t bits=UINT64_MAX;
    if(word==first/64)bits&=UINT64_MAX<<(first%64);
    if(word==last/64)bits&=UINT64_MAX>>(63-last%64);
    p->cpu_exposure->pages[word]|=bits;
  }
}
static int pool_publication_can_defer(pool *p,uint64_t offset,uint64_t size) {
  return pool_publication_candidate(p) && !pool_cpu_exposed(p,offset,size);
}
