// Physical mapping hint contract and GPU bytes, no timing or hit-rate sampler.
#ifdef DM_VULKAN_TEST_EMBED
extern VkBuffer vk_test_pool_locked_span_hint(uint64_t);
static void pool_span_cache_test(void) {
  mtl_init();if(V.options.pool_mirror_reference)return;
  enum {W=65536,SIZE=8*W+193};uint8_t *storage;
  CHECK(!posix_memalign((void **)&storage,W,SIZE));dm_guest_write_prepare(storage,SIZE);memset(storage,0x31,SIZE);
  uint64_t previous_window=V.options.pool_window_bytes,full,used,protected;int reference;
  vk_pool_sync_policy_stats(&full,&used,&protected,&reference);vk_pool_sync_reference(0);mtl_finish();vk_test_pool_window_bytes(W);
  mtl_pool_add(storage,SIZE);vk_pool_requirement needs[5];vk_pool_span_info original[5],q;
  const unsigned pages[]={0,1,3,6,8};vk_pool_reserve_result result;
  needs[0]=pool_window_need(storage+64,4,2,VK_PIPELINE_STAGE_TRANSFER_BIT,0);
  CHECK(vk_pool_reserve_ranges(needs,1,W,&result)==VK_POOL_RESERVE_OK);
  CHECK(vk_pool_span_query(needs[0].address,4,&original[0])==VK_POOL_SPAN_SINGLE);
  CHECK(vk_test_pool_locked_span_hint((uintptr_t)storage)==VK_NULL_HANDLE); // Single-node stays direct.
  for(unsigned i=1;i<5;i++)needs[i]=pool_window_need(storage+pages[i]*W+64,4,2,VK_PIPELINE_STAGE_TRANSFER_BIT,0);
  CHECK(vk_pool_reserve_ranges(needs+1,4,W,&result)==VK_POOL_RESERVE_OK);
  for(unsigned i=0;i<5;i++) {
    CHECK(vk_pool_span_query(needs[i].address,4,&original[i])==VK_POOL_SPAN_SINGLE);
    CHECK(vk_test_pool_locked_span_hint((uintptr_t)storage)==original[i].handle);
  }
  // Warm the later node, then go backwards, repeat, and cross an adjacent edge.
  for(unsigned n=0;n<3;n++)for(int i=4;i>=0;i--) {
    CHECK(vk_pool_span_query(needs[i].address+4,8,&q)==VK_POOL_SPAN_SINGLE);
    CHECK(q.handle==original[i].handle&&q.local_offset==68);
    CHECK(vk_test_pool_locked_span_hint((uintptr_t)storage)==q.handle);
  }
  CHECK(vk_pool_span_query((uintptr_t)storage+W,4,&q)==VK_POOL_SPAN_SINGLE&&q.handle==original[1].handle&&q.local_offset==0);
  CHECK(vk_pool_span_query((uintptr_t)storage+W-4,8,&q)==VK_POOL_SPAN_CROSSING);
  CHECK(vk_pool_span_query((uintptr_t)storage+2*W,4,&q)==VK_POOL_SPAN_CROSSING); // hole, not cached left end
  CHECK(vk_pool_span_query((uintptr_t)storage+SIZE-4,4,&q)==VK_POOL_SPAN_SINGLE&&q.local_offset==189);
  CHECK(vk_pool_span_query((uintptr_t)storage+SIZE-4,5,&q)==VK_POOL_SPAN_NONE);
  uint32_t words[5];uint8_t pixels[20];mtl_sub_begin();
  for(unsigned i=0;i<5;i++) {words[i]=0x98230000+i;pool_window_fill((uint8_t *)(uintptr_t)needs[i].address,4,words[i]);}
  vk_internal_wait(vk_internal_commit());
  for(unsigned i=0;i<5;i++)CHECK(memcmp((void *)(uintptr_t)needs[i].address,&words[i],4)); // GPU-only, guest remains old.
  // A genuine finite replacement retires exactly the node currently hinted.
  CHECK(vk_pool_span_query(needs[1].address,4,&q)==VK_POOL_SPAN_SINGLE);
  CHECK(vk_test_pool_locked_span_hint((uintptr_t)storage)==original[1].handle);
  vk_pool_requirement cross=pool_window_need(storage+2*W-16,32,2,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,64);
  CHECK(vk_test_pool_consolidate((uintptr_t)storage,original[0].pool_identity,&cross,W));
  CHECK(vk_test_pool_locked_span_hint((uintptr_t)storage)==VK_NULL_HANDLE);
  CHECK(vk_pool_span_query(needs[1].address,4,&q)==VK_POOL_SPAN_SINGLE&&q.handle!=original[1].handle);
  CHECK(vk_test_pool_locked_span_hint((uintptr_t)storage)==q.handle);
  CHECK(vk_pool_span_query(needs[3].address,4,&q)==VK_POOL_SPAN_SINGLE&&q.handle==original[3].handle);
  CHECK(vk_pool_span_query((uintptr_t)storage+4*W,4,&q)==VK_POOL_SPAN_CROSSING);
  mtl_sub_begin();for(int i=4;i>=0;i--)pool_window_read((uint8_t *)(uintptr_t)needs[i].address,4,pixels+4*i);
  vk_internal_wait(vk_internal_commit());CHECK(!memcmp(pixels,words,sizeof words));
  // A new disjoint reservation leaves an existing hinted node alive.
  CHECK(vk_pool_span_query(needs[3].address,4,&q)==VK_POOL_SPAN_SINGLE);
  VkBuffer retained=q.handle;vk_pool_requirement extra=pool_window_need(storage+5*W+64,4,2,VK_PIPELINE_STAGE_TRANSFER_BIT,0);
  CHECK(vk_pool_reserve_ranges(&extra,1,W,&result)==VK_POOL_RESERVE_OK&&result.added_spans==1);
  CHECK(vk_test_pool_locked_span_hint((uintptr_t)storage)==retained);
  // A CPU edit still uploads: mapping reuse proves nothing about dirty content.
  dm_guest_write_prepare((void *)(uintptr_t)needs[3].address,4);memcpy((void *)(uintptr_t)needs[3].address,(uint32_t[]){0xabcdef12},4);
  words[3]=0xabcdef12;mtl_sub_begin();for(unsigned i=0;i<5;i++)pool_window_read((uint8_t *)(uintptr_t)needs[i].address,4,pixels+4*i);
  vk_internal_wait(vk_internal_commit());CHECK(!memcmp(pixels,words,sizeof words));
  // Full replacement also invalidates; same-address new logical owner starts cold.
  CHECK(vk_pool_span_query(needs[4].address,4,&q)==VK_POOL_SPAN_SINGLE);
  CHECK(vk_test_pool_migrate_whole((uintptr_t)storage));CHECK(vk_test_pool_locked_span_hint((uintptr_t)storage)==VK_NULL_HANDLE);
  mtl_sub_begin();for(int i=4;i>=0;i--)pool_window_read((uint8_t *)(uintptr_t)needs[i].address,4,pixels+4*i);
  vk_internal_wait(vk_internal_commit());CHECK(!memcmp(pixels,words,sizeof words));
  mtl_pool_del(storage);dm_guest_write_prepare(storage,SIZE);memset(storage,0x47,SIZE);mtl_pool_add(storage,SIZE);
  CHECK(vk_test_pool_locked_span_hint((uintptr_t)storage)==VK_NULL_HANDLE);
  CHECK(vk_pool_reserve_ranges(needs,5,W,&result)==VK_POOL_RESERVE_OK);
  CHECK(vk_pool_span_query(needs[0].address,4,&q)==VK_POOL_SPAN_SINGLE&&q.pool_identity!=original[0].pool_identity);
  mtl_sub_begin();for(unsigned i=0;i<5;i++)pool_window_read((uint8_t *)(uintptr_t)needs[i].address,4,pixels+4*i);
  vk_internal_wait(vk_internal_commit());for(unsigned i=0;i<sizeof pixels;i++)CHECK(pixels[i]==0x47);
  mtl_pool_del(storage);free(storage);vk_test_pool_window_bytes(previous_window);vk_pool_sync_reference(reference);
}
#endif
