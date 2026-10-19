// Exact physical lookup reuse, real GPU bytes and fail-closed admission. No
// timestamps, profiler replacement or extra production counters/state.
#ifdef DM_VULKAN_TEST_EMBED
#include <signal.h>
extern const _Atomic uint64_t *vk_test_pool_initialized_counter(uint64_t);
static _Atomic(const _Atomic uint64_t *) pool_address_death_counter;
static _Atomic uint64_t pool_address_death_expected;
static void pool_address_death(int signal) {
  const _Atomic uint64_t *counter=atomic_load(&pool_address_death_counter);
  // Exit86 is accepted only together with the exact production admission
  // diagnostic. Exit87 means it initialized CPU pages before failing closed.
  _exit(signal==SIGABRT && counter && atomic_load(counter)==atomic_load(&pool_address_death_expected)?86:87);
}
static void pool_address_death_test(const char *kind) {
  enum {W=65536,SIZE=6*W+193};uint8_t *storage;
  CHECK(!posix_memalign((void **)&storage,W,SIZE));dm_guest_write_prepare(storage,SIZE);memset(storage,0x47,SIZE);
  mtl_init();mtl_finish();vk_pool_sync_reference(0);vk_test_pool_window_bytes(W);mtl_pool_add(storage,SIZE);
  if(!strcmp(kind,"registration-overflow") || !strcmp(kind,"registration-overlap")) {
    // Exercise the real registration boundary. Neither malformed registry
    // must initialize CPU pages or become available to operation pin reuse.
    const _Atomic uint64_t *counter=vk_test_pool_initialized_counter((uintptr_t)storage);
    CHECK(counter&&atomic_is_lock_free(counter)&&atomic_is_lock_free(&pool_address_death_counter)&&atomic_is_lock_free(&pool_address_death_expected));
    atomic_store(&pool_address_death_counter,counter);atomic_store(&pool_address_death_expected,atomic_load(counter));
    struct sigaction action={.sa_handler=pool_address_death};sigemptyset(&action.sa_mask);CHECK(!sigaction(SIGABRT,&action,NULL));
    if(!strcmp(kind,"registration-overflow"))mtl_pool_add((const void *)(UINTPTR_MAX-4095),8192);
    else mtl_pool_add(storage+W,W);
    _exit(88);
  }
  int unbacked=!strcmp(kind,"unbacked"),fast=!strncmp(kind,"fast-",5);
  CHECK(unbacked || !strcmp(kind,"slow-hole") || !strcmp(kind,"slow-crossing") ||
        !strcmp(kind,"fast-hole") || !strcmp(kind,"fast-crossing"));
  if(!unbacked) {
    vk_pool_requirement needs[]={pool_window_need(storage+W+64,4,2,VK_PIPELINE_STAGE_TRANSFER_BIT,0),
      pool_window_need(storage+2*W+64,4,2,VK_PIPELINE_STAGE_TRANSFER_BIT,0)};
    vk_pool_reserve_result result;CHECK(vk_pool_reserve_ranges(needs,2,W,&result)==VK_POOL_RESERVE_OK&&result.added_spans==2);
  }
  mtl_sub_begin();
  if(fast) {vk_buffer *buffer;uint64_t offset;CHECK(vk_pool_address_stage((uintptr_t)storage+W+64,4,(void **)&buffer,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT));}
  int token=unbacked?vk_pool_operation_begin_ranges(NULL,0):0;CHECK(!unbacked||token>0);
  const _Atomic uint64_t *counter=vk_test_pool_initialized_counter((uintptr_t)storage);
  CHECK(counter&&atomic_is_lock_free(counter)&&atomic_is_lock_free(&pool_address_death_counter)&&atomic_is_lock_free(&pool_address_death_expected));
  atomic_store(&pool_address_death_counter,counter);atomic_store(&pool_address_death_expected,atomic_load(counter));
  struct sigaction action={.sa_handler=pool_address_death};sigemptyset(&action.sa_mask);CHECK(!sigaction(SIGABRT,&action,NULL));
  uint64_t address=(uintptr_t)storage+(unbacked?64:strstr(kind,"hole")?3*W+64:2*W-4);
  vk_buffer *buffer;uint64_t offset;
  (void)vk_pool_address_stage(address,unbacked?4:8,(void **)&buffer,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT);
  _exit(88); // An invalid request must never return or materialize whole storage.
}
static void pool_address_proof_test(void) {
  mtl_init();if(V.options.pool_mirror_reference)return;
  enum {W=65536,SIZE=6*W+193};uint8_t *storage,*other;uint8_t pixels[16];
  CHECK(!posix_memalign((void **)&storage,W,SIZE));CHECK(!posix_memalign((void **)&other,W,W));
  dm_guest_write_prepare(storage,SIZE);dm_guest_write_prepare(other,W);memset(storage,0x31,SIZE);memset(other,0x62,W);
  uint64_t previous_window=V.options.pool_window_bytes,full,used,protected;int reference,lookup_reference,saved_lookup_reference;
  uint64_t h0,r0,s0,h1,r1,s1;vk_pool_lookup_stats(&h0,&r0,&s0,&lookup_reference);saved_lookup_reference=lookup_reference;
  vk_pool_sync_policy_stats(&full,&used,&protected,&reference);vk_pool_sync_reference(0);vk_pool_lookup_reference(0);
  mtl_finish();vk_test_pool_window_bytes(W);mtl_sub_begin();vk_internal_wait(vk_internal_commit());
  // The only guarded whole materialization remains a first depth-zero use.
  mtl_pool_add(storage,SIZE);vk_pool_span_info q;CHECK(vk_pool_span_query((uintptr_t)storage+64,4,&q)==VK_POOL_SPAN_UNBACKED);
  vk_pool_lookup_stats(&h0,&r0,&s0,&lookup_reference);mtl_sub_begin();
  vk_buffer *buffer,*second;uint64_t offset,second_offset;
  CHECK(vk_pool_address_stage((uintptr_t)storage+64,4,(void **)&buffer,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT)&&offset==64);
  CHECK(vk_pool_address_stage((uintptr_t)storage+64,4,(void **)&second,&second_offset,VK_PIPELINE_STAGE_TRANSFER_BIT)&&second==buffer&&second_offset==offset);
  void *untouched=(void *)(uintptr_t)0x1234;uint64_t untouched_offset=0xabcdef;
  CHECK(!vk_pool_address_stage((uintptr_t)other,4,&untouched,&untouched_offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
  CHECK(untouched==(void *)(uintptr_t)0x1234&&untouched_offset==0xabcdef);
  CHECK(vk_pool_address_stage((uintptr_t)storage+SIZE,0,(void **)&second,&second_offset,VK_PIPELINE_STAGE_TRANSFER_BIT)&&second==buffer&&second_offset==SIZE);
  vk_internal_wait(vk_internal_commit());vk_pool_lookup_stats(&h1,&r1,&s1,&lookup_reference);
  CHECK(h1-h0==1&&s1-s0==2); // Unknown guard remains outside the slow count.
  CHECK(vk_pool_span_query((uintptr_t)storage+64,4,&q)==VK_POOL_SPAN_SINGLE&&q.logical_begin==0&&q.logical_end==SIZE);
  mtl_pool_del(storage);dm_guest_write_prepare(storage,SIZE);memset(storage,0x47,SIZE);mtl_pool_add(storage,SIZE);
  vk_pool_requirement needs[4];const unsigned pages[]={1,2,4,6};vk_pool_reserve_result result;
  for(unsigned i=0;i<4;i++)needs[i]=pool_window_need(storage+pages[i]*W+64,4,2,VK_PIPELINE_STAGE_TRANSFER_BIT,0);
  CHECK(vk_pool_reserve_ranges(needs,4,W,&result)==VK_POOL_RESERVE_OK&&result.added_spans==4);
  CHECK(!vk_test_pool_reserve_metadata((uintptr_t)storage));
  uint64_t identity;CHECK(vk_pool_span_query(needs[0].address,4,&q)==VK_POOL_SPAN_SINGLE);identity=q.pool_identity;
  mtl_sub_begin();CHECK(vk_pool_address_stage(needs[0].address,4,(void **)&buffer,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT)&&offset==64);
  // A registry change invalidates retained lookup slots even though the exact
  // original submission reference remains alive through its completion.
  vk_pool_lookup_stats(&h0,&r0,&s0,&lookup_reference);mtl_pool_add(other,W);
  CHECK(vk_pool_address_stage(needs[0].address,4,(void **)&second,&second_offset,VK_PIPELINE_STAGE_TRANSFER_BIT)&&second==buffer&&second_offset==64);
  for(int i=3;i>=0;i--)pool_window_read((uint8_t *)(uintptr_t)needs[i].address,4,pixels+4*i);
  vk_internal_wait(vk_internal_commit());for(unsigned i=0;i<sizeof pixels;i++)CHECK(pixels[i]==0x47);
  vk_pool_lookup_stats(&h1,&r1,&s1,&lookup_reference);CHECK(s1-s0==2&&h1-h0==4);
  uint32_t words[4];mtl_sub_begin();
  for(unsigned i=0;i<4;i++) {words[i]=0x99880000+i;pool_window_fill((uint8_t *)(uintptr_t)needs[i].address,4,words[i]);}
  vk_internal_wait(vk_internal_commit());
  for(unsigned i=0;i<4;i++)CHECK(memcmp((void *)(uintptr_t)needs[i].address,words+i,4));
  dm_guest_write_prepare((void *)(uintptr_t)needs[1].address,4);words[1]=0x1234abcd;memcpy((void *)(uintptr_t)needs[1].address,words+1,4);
  // Both retained and forced original locked paths must preserve GPU-only
  // neighbors while uploading the exact CPU change, at nonzero local origins.
  for(unsigned mode=0;mode<2;mode++) {
    uint32_t neighbors[4],again[4];
    vk_pool_lookup_reference(mode);mtl_sub_begin();
    for(int i=3;i>=0;i--) {
      uint8_t *address=(uint8_t *)(uintptr_t)needs[i].address;
      pool_window_read(address,4,pixels+4*i);
      // Reuse one physical node at a different local offset, then return to
      // GPU-only data. Alternating nodes must not reuse its predecessor's
      // handle/offset or mistake unchanged CPU bytes for the GPU value.
      pool_window_read(address+4,4,(uint8_t *)&neighbors[i]);
      pool_window_read(address,4,(uint8_t *)&again[i]);
    }
    vk_internal_wait(vk_internal_commit());CHECK(!memcmp(pixels,words,sizeof words));
    CHECK(!memcmp(again,words,sizeof words));
    for(unsigned i=0;i<4;i++)CHECK(neighbors[i]==0x47474747);
  }
  CHECK(vk_pool_span_query((uintptr_t)storage+2*W-4,8,&q)==VK_POOL_SPAN_CROSSING);
  CHECK(vk_pool_span_query((uintptr_t)storage+3*W+64,4,&q)==VK_POOL_SPAN_CROSSING);
  mtl_pool_del(other);mtl_pool_del(storage);dm_guest_write_prepare(storage,SIZE);memset(storage,0x71,SIZE);mtl_pool_add(storage,SIZE);
  CHECK(vk_pool_reserve_ranges(needs,4,W,&result)==VK_POOL_RESERVE_OK&&result.added_spans==4);
  CHECK(vk_pool_span_query(needs[0].address,4,&q)==VK_POOL_SPAN_SINGLE&&q.pool_identity!=identity);
  vk_pool_lookup_reference(0);mtl_sub_begin();for(unsigned i=0;i<4;i++)pool_window_read((uint8_t *)(uintptr_t)needs[i].address,4,pixels+4*i);
  vk_internal_wait(vk_internal_commit());for(unsigned i=0;i<sizeof pixels;i++)CHECK(pixels[i]==0x71);
  mtl_pool_del(storage);free(storage);free(other);vk_test_pool_window_bytes(previous_window);vk_pool_sync_reference(reference);vk_pool_lookup_reference(saved_lookup_reference);
}
#endif
