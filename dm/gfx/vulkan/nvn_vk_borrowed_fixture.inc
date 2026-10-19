#ifdef DM_VULKAN_TEST_EMBED
static void borrowed_close_notify(uint64_t address);
#endif
// Bounded, cold-selected borrowed-storage experiment. Legal NVN caller keeps
// the complete mapping accessible through Finalize. Not an allocator hook.
void vk_test_pool_borrow_enable(void) {
  if(V.instance || owned_fixture_requested)die("Vulkan borrowed selection must be cold and exclusive");
  borrowed_fixture_requested=1;owned_fixture_requested=1;
  V.owned_fixture_active=1;vk_test_texture_owned_scopes(1);
}
void mtl_pool_borrow_registered(const void *mem,uint64_t size) {
  if(!borrowed_fixture_requested || size!=65536 || (uintptr_t)mem%65536)return;
  pthread_mutex_lock(&V.resources);pool *p=pool_find((uintptr_t)mem,size);
  if(p && p->guest==mem && p->size==size && p->nvn_flags_known &&
      p->nvn_flags==(NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED) &&
      !p->backings && !p->shadow && !p->references && !p->borrowed_lease.lifetime)
    nvn_dirty_pool_lease_observe(mem,size,&p->borrowed_lease);
  pthread_mutex_unlock(&V.resources);
}
// Slow lifecycle/alias path only; not a new per-draw registry walk. Record even
// before first GPU use so a finalized event/image cannot later be mistaken for
// a historically pure buffer pool. No history clears during a logical lease.
uint64_t mtl_pool_borrow_mark(uint64_t address,uint64_t size,int closing) {
  if(!borrowed_fixture_requested || !size)return 0;
  if(size>UINT64_MAX-address)die("Vulkan borrowed history extent overflow");
  uint64_t identity=0;pthread_mutex_lock(&V.resources);
  for(pool *p=pools;p;p=p->next)if(p->borrowed_lease.lifetime &&
      address<(uintptr_t)p->guest+p->size && (uintptr_t)p->guest<address+size) {
    p->borrowed_history|=1;
    if(closing) {
      if(address!=(uintptr_t)p->guest || size!=p->size || p->borrowed_closing)
        die("Vulkan borrowed duplicate/nonexact Finalize");
      p->borrowed_closing=1;identity=p->identity;
    }
  }
  pthread_mutex_unlock(&V.resources);
#ifdef DM_VULKAN_TEST_EMBED
  if(identity)borrowed_close_notify(address);
#endif
  return identity;
}
void mtl_pool_borrow_owner_close(uint64_t address,uint64_t size,uint64_t identity) {
  pthread_mutex_lock(&V.resources);pool *p=pool_find(address,size);
  int valid=p && p->identity==identity && p->guest==(void *)(uintptr_t)address &&
    p->size==size && p->borrowed_lease.lifetime && p->borrowed_closing;
  pthread_mutex_unlock(&V.resources);
  if(!valid)die("Vulkan borrowed owner close lost lease");
  if(owned_fixture_retire(address,size,identity)<0)die("Vulkan borrowed close retirement changed identity");
  // Executed unconditionally on the recording owner, after older queued work,
  // even when no import was present when Finalize first marked closing.
  nvn_dirty_remove_range((void *)(uintptr_t)address);
  mtl_pool_del((void *)(uintptr_t)address);
}
#ifdef DM_VULKAN_TEST_EMBED
static pthread_mutex_t borrowed_gate_mu=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t borrowed_gate_cv=PTHREAD_COND_INITIALIZER;
static uint64_t borrowed_gate_address;
static int borrowed_gate_blocked,borrowed_gate_released;
static uint64_t borrowed_closed_address;
void vk_test_pool_borrow_gate(uint64_t address,int release) {
  pthread_mutex_lock(&borrowed_gate_mu);
  if(release)borrowed_gate_released=1;
  else {borrowed_gate_address=address;borrowed_gate_blocked=0;borrowed_gate_released=0;borrowed_closed_address=0;}
  pthread_cond_broadcast(&borrowed_gate_cv);pthread_mutex_unlock(&borrowed_gate_mu);
}
void vk_test_pool_borrow_gate_wait(void) {
  pthread_mutex_lock(&borrowed_gate_mu);
  while(!borrowed_gate_blocked)pthread_cond_wait(&borrowed_gate_cv,&borrowed_gate_mu);
  pthread_mutex_unlock(&borrowed_gate_mu);
}
static void borrowed_close_notify(uint64_t address) {
  pthread_mutex_lock(&borrowed_gate_mu);borrowed_closed_address=address;
  pthread_cond_broadcast(&borrowed_gate_cv);pthread_mutex_unlock(&borrowed_gate_mu);
}
void vk_test_pool_borrow_wait_closing(uint64_t address) {
  pthread_mutex_lock(&borrowed_gate_mu);
  while(borrowed_closed_address!=address)pthread_cond_wait(&borrowed_gate_cv,&borrowed_gate_mu);
  pthread_mutex_unlock(&borrowed_gate_mu);
}
static void borrowed_before_import(uint64_t address) {
  pthread_mutex_lock(&borrowed_gate_mu);
  if(address==borrowed_gate_address && !borrowed_gate_released) {
    borrowed_gate_blocked=1;pthread_cond_broadcast(&borrowed_gate_cv);
    while(!borrowed_gate_released)pthread_cond_wait(&borrowed_gate_cv,&borrowed_gate_mu);
    borrowed_gate_address=0;
  }
  pthread_mutex_unlock(&borrowed_gate_mu);
}
#else
static void borrowed_before_import(uint64_t address) {(void)address;}
#endif
// Called ONLY by the original registry-locked resolver on a missing physical
// span. Already-backed draws add no search/scan/atomic load or new hot loop.
// On this cold miss inspect the complete existing bounded operation plan.
static int borrowed_missing_admit(pool *p,const vk_pool_requirement *ranges,size_t count,
    uint64_t *address,uint64_t *size,uint64_t *identity) {
  if(!borrowed_fixture_requested || !p->borrowed_lease.lifetime || p->borrowed_closing ||
      p->borrowed_history || p->borrowed_attempted || p->backings || p->references || p->shadow)return 0;
  uint64_t base=(uintptr_t)p->guest;
  for(size_t j=0;j<count;j++)if(ranges[j].address<base+p->size && base<ranges[j].address+ranges[j].size &&
      (ranges[j].address<base || ranges[j].size>p->size-(ranges[j].address-base) ||
       ranges[j].writable || (ranges[j].binding_kind!=1 && ranges[j].binding_kind!=VK_POOL_REQUIRE_VERTEX)))
    p->borrowed_history|=2;
  if(p->borrowed_history)return 0;
  p->borrowed_attempted=1;*address=base;*size=p->size;*identity=p->identity;return 1;
}
unsigned vk_test_pool_borrow_state(uint64_t address) {
  pthread_mutex_lock(&V.resources);pool *p=pool_find(address,1);
  unsigned state=p && p->borrowed_lease.lifetime?
    1u|(p->borrowed_history?2u:0)|(p->borrowed_closing?4u:0)|(p->borrowed_attempted?8u:0):0;
  pthread_mutex_unlock(&V.resources);return state;
}
