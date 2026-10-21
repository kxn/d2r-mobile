// Bounded operation geometry: original Pool API, real VkBuffer reads/writes
// and ordinary completion. No game modules or runtime timing/sampling.
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
extern int vk_test_pool_operation_geometry(uint64_t,uint64_t,vk_pool_span_info *);
extern uint64_t vk_test_pool_operation_geometry_uses(void);
extern void vk_test_pool_operation_pin_counts(size_t *,size_t *);
static void pool_operation_geometry_test(void) {
  enum {W=65536,SIZE=6*W+193};uint8_t *storage,*other;
  CHECK(!posix_memalign((void **)&storage,W,SIZE));CHECK(!posix_memalign((void **)&other,W,W));
  dm_guest_write_prepare(storage,SIZE);dm_guest_write_prepare(other,W);memset(storage,0x31,SIZE);memset(other,0x68,W);
  mtl_init();mtl_finish();uint64_t previous_window=V.options.pool_window_bytes,full,used,protected;
  int reference,lookup_reference;uint64_t hits,ranges,slow;
  vk_pool_sync_policy_stats(&full,&used,&protected,&reference);
  vk_pool_lookup_stats(&hits,&ranges,&slow,&lookup_reference);
  vk_pool_sync_reference(0);vk_pool_lookup_reference(0);vk_test_pool_window_bytes(W);
  mtl_sub_begin();vk_internal_wait(vk_internal_commit());mtl_pool_add(storage,SIZE);
  vk_pool_requirement needs[]={pool_window_need(storage+W+64,16,2,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,0),
    pool_window_need(storage+2*W+128,16,VK_POOL_REQUIRE_INDEX,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,0)};
  vk_pool_span_info a,b,q;uint8_t first[16],second[16],later[16];
  CHECK(!vk_test_pool_operation_geometry(needs[0].address,16,&q));
  uint64_t initialized=vk_pool_initialized_bytes();mtl_sub_begin();int parent=vk_pool_operation_begin_ranges(needs,2);CHECK(parent);
  CHECK(vk_pool_initialized_bytes()==initialized);CHECK(!vk_test_pool_reserve_metadata((uintptr_t)storage));
  CHECK(vk_test_pool_operation_geometry(needs[0].address,16,&a));
  CHECK(vk_test_pool_operation_geometry(needs[1].address,16,&b));
  CHECK(a.handle!=b.handle&&a.local_offset==64&&b.local_offset==128&&a.pool_identity==b.pool_identity);
  CHECK(!vk_test_pool_operation_geometry(needs[0].address,0,&q));
  CHECK(!vk_test_pool_operation_geometry(needs[0].address,8,&q));
  uint64_t uses=vk_test_pool_operation_geometry_uses();
  pool_window_read((void *)(uintptr_t)needs[0].address,16,first);
  CHECK(vk_test_pool_operation_geometry_uses()>uses); // actual consuming lookup
  CHECK(vk_test_pool_reserve_metadata((uintptr_t)storage)==7);
  pool_window_fill((void *)(uintptr_t)needs[1].address,16,0x98765432u);
  pool_window_read((void *)(uintptr_t)needs[1].address,16,second);
  uint64_t first_serial=vk_internal_commit();vk_internal_wait(first_serial);
  for(unsigned i=0;i<16;i++)CHECK(first[i]==0x31);
  for(unsigned i=0;i<4;i++)CHECK(!memcmp(second+4*i,(uint32_t[]){0x98765432u},4));
  CHECK(vk_test_pool_operation_geometry(needs[0].address,16,&q)&&q.handle==a.handle);
  // Independent operation pin survives the completed old recording. The new
  // recording must acquire its OWN access ledger rather than reuse old use*.
  mtl_sub_begin();CHECK(vk_recording_serial()>first_serial);
  pool_window_read((void *)(uintptr_t)needs[1].address,16,later);
  int child=vk_pool_operation_begin_ranges(needs+1,1);CHECK(child==parent+1);
  CHECK(!vk_test_pool_operation_geometry(needs[0].address,16,&q));
  CHECK(vk_test_pool_operation_geometry(needs[1].address,16,&q)&&q.handle==b.handle);
  pool_window_read((void *)(uintptr_t)needs[1].address,16,second);vk_pool_operation_end(child);
  CHECK(vk_test_pool_operation_geometry(needs[0].address,16,&q)&&q.handle==a.handle);
  vk_internal_wait(vk_internal_commit());CHECK(!memcmp(second,later,16));
  // Host edits after admission remain live. Geometry is not a content-ready
  // proof: both new bytes and the later restoring write traverse real upload.
  for(unsigned edit=0;edit<2;edit++) {
    dm_guest_write_prepare((void *)(uintptr_t)needs[0].address,16);
    memset((void *)(uintptr_t)needs[0].address,edit?0x31:0x42,16);
    mtl_sub_begin();pool_window_read((void *)(uintptr_t)needs[0].address,16,first);
    vk_internal_wait(vk_internal_commit());
    for(unsigned i=0;i<16;i++)CHECK(first[i]==(edit?0x31:0x42));
  }
  // A registry generation change invalidates the complete parent's table,
  // even though unrelated registration leaves the pinned old handles alive.
  mtl_pool_add(other,W);CHECK(!vk_test_pool_operation_geometry(needs[0].address,16,&q));
  mtl_sub_begin();pool_window_read((void *)(uintptr_t)needs[1].address,16,later);
  vk_pool_operation_end(parent);CHECK(!vk_test_pool_operation_geometry(needs[1].address,16,&q));
  vk_internal_wait(vk_internal_commit());CHECK(!memcmp(second,later,16));mtl_pool_del(other);
  // Direct-map collisions are exact misses, never a wrong physical offset.
  vk_pool_requirement collision[]={needs[0],needs[0]};collision[1].address+=4096;
  mtl_sub_begin();parent=vk_pool_operation_begin_ranges(collision,2);CHECK(parent);
  CHECK(!vk_test_pool_operation_geometry(collision[0].address,16,&q));
  CHECK(vk_test_pool_operation_geometry(collision[1].address,16,&q));
  pool_window_read((void *)(uintptr_t)collision[0].address,16,first);
  pool_window_read((void *)(uintptr_t)collision[1].address,16,second);
  vk_pool_operation_end(parent);vk_internal_wait(vk_internal_commit());CHECK(!memcmp(first,second,16));
  // All eight depths own independent pins/maps, without searching a parent
  // packet as permission for an unenumerated child's physical access.
  int tokens[8];mtl_sub_begin();for(unsigned d=0;d<8;d++) {
    tokens[d]=vk_pool_operation_begin_ranges(needs,1);CHECK(tokens[d]==(int)d+1);
    CHECK(vk_test_pool_operation_geometry(needs[0].address,16,&q));
  }
  pool_window_read((void *)(uintptr_t)needs[0].address,16,first);
  for(unsigned d=8;d;d--)vk_pool_operation_end(tokens[d-1]);
  vk_internal_wait(vk_internal_commit());for(unsigned i=0;i<16;i++)CHECK(first[i]==0x31);
  // A real prior consumer owns this recording's Pool ref. New operations
  // borrow it without weakening the token's lifetime across an internal
  // commit: upgrade before completion, then consume again on a fresh owner.
  mtl_sub_begin();int prime=vk_pool_operation_begin_ranges(needs,2);CHECK(prime);
  size_t owned,borrowed;vk_test_pool_operation_pin_counts(&owned,&borrowed);CHECK(owned==1&&borrowed==0);
  pool_window_read((void *)(uintptr_t)needs[0].address,16,first);vk_pool_operation_end(prime);
  int borrowed_token=vk_pool_operation_begin_ranges(needs,2);CHECK(borrowed_token);
  vk_test_pool_operation_pin_counts(&owned,&borrowed);
#ifdef DM_VULKAN_POOL_PIN_REFERENCE
  CHECK(owned==1&&borrowed==0);
#else
  CHECK(owned==0&&borrowed==1);
#endif
  CHECK(vk_test_pool_operation_geometry(needs[0].address,16,&a));
  vk_internal_wait(vk_internal_commit());
  vk_test_pool_operation_pin_counts(&owned,&borrowed);CHECK(owned==1&&borrowed==0);
  CHECK(vk_test_pool_operation_geometry(needs[0].address,16,&q)&&q.handle==a.handle);
  mtl_sub_begin();pool_window_read((void *)(uintptr_t)needs[0].address,16,first);
  int borrowed_child=vk_pool_operation_begin_ranges(needs+1,1);CHECK(borrowed_child==borrowed_token+1);
  vk_test_pool_operation_pin_counts(&owned,&borrowed);
#ifdef DM_VULKAN_POOL_PIN_REFERENCE
  CHECK(owned==1&&borrowed==0);
#else
  CHECK(owned==0&&borrowed==1);
#endif
  pool_window_read((void *)(uintptr_t)needs[1].address,16,second);
  vk_internal_wait(vk_internal_commit());vk_test_pool_operation_pin_counts(&owned,&borrowed);CHECK(owned==1&&borrowed==0);
  vk_pool_operation_end(borrowed_child);vk_pool_operation_end(borrowed_token);
  for(unsigned i=0;i<16;i++)CHECK(first[i]==0x31);
  for(unsigned i=0;i<4;i++)CHECK(!memcmp(second+4*i,(uint32_t[]){0x98765432u},4));
  // Mixed owners and a failed first pin attempt: the old Pool borrows a real
  // current access; the new Pool must reserve storage and acquire its own pin.
  mtl_pool_add(other,W);mtl_sub_begin();prime=vk_pool_operation_begin_ranges(needs,1);
  pool_window_read((void *)(uintptr_t)needs[0].address,16,first);vk_pool_operation_end(prime);
  vk_pool_requirement mixed[]={needs[0],pool_window_need(other+64,16,2,VK_PIPELINE_STAGE_TRANSFER_BIT,1)};
  borrowed_token=vk_pool_operation_begin_ranges(mixed,2);CHECK(borrowed_token);
  vk_test_pool_operation_pin_counts(&owned,&borrowed);
#ifdef DM_VULKAN_POOL_PIN_REFERENCE
  CHECK(owned==2&&borrowed==0);
#else
  CHECK(owned==1&&borrowed==1);
#endif
  pool_window_fill(other+64,16,0x12345678u);pool_window_read(other+64,16,second);
  vk_pool_operation_end(borrowed_token);vk_internal_wait(vk_internal_commit());
  for(unsigned i=0;i<4;i++)CHECK(!memcmp(second+4*i,(uint32_t[]){0x12345678u},4));
  mtl_pool_del(other);
  // Pool removal/re-registration at the SAME guest address cannot resurrect
  // the old parent's geometry. Deletion really blocks on its independent pin.
  mtl_sub_begin();parent=vk_pool_operation_begin_ranges(needs,1);CHECK(parent);
  CHECK(vk_test_pool_operation_geometry(needs[0].address,16,&a));vk_internal_wait(vk_internal_commit());
  pool_finite_reuse reuse={.storage=storage,.size=SIZE};CHECK(!pthread_create(&reuse.deleter,NULL,pool_finite_delete,&reuse));
  unsigned tries;for(tries=0;tries<100000;tries++) {
    if(vk_pool_span_query(needs[0].address,16,&q)==VK_POOL_SPAN_NONE)break;sched_yield();
  }
  CHECK(tries<100000);CHECK(!vk_test_pool_operation_geometry(needs[0].address,16,&q));
  mtl_pool_add(storage,SIZE);mtl_sub_begin();child=vk_pool_operation_begin_ranges(needs,1);CHECK(child==parent+1);
  CHECK(vk_test_pool_operation_geometry(needs[0].address,16,&q)&&q.pool_identity!=a.pool_identity);
  pool_window_read((void *)(uintptr_t)needs[0].address,16,first);
  vk_pool_operation_end(child);CHECK(!vk_test_pool_operation_geometry(needs[0].address,16,&q));
  vk_pool_operation_end(parent);CHECK(!pthread_join(reuse.deleter,NULL));
  vk_internal_wait(vk_internal_commit());for(unsigned i=0;i<16;i++)CHECK(first[i]==0x31);
  mtl_pool_del(storage);free(storage);free(other);
  vk_test_pool_window_bytes(previous_window);vk_pool_sync_reference(reference);vk_pool_lookup_reference(lookup_reference);
  LOG("[operation geometry fixture] actual GPU bytes, independent pins across completion/reopen/nesting, exact collisions, registry invalidation and same-address lifetime passed");
}
#endif
