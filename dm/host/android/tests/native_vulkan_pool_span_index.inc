// Existing original Pool API creates the Vulkan backings; compare indexed
// lookup with the original range resolver, then consume real GPU bytes.
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
extern int vk_test_pool_span_search(uint64_t,uint64_t,uint64_t,unsigned,unsigned *,uint64_t *);
static void pool_span_search_index_test(void) {
  enum {W=65536,N=24,SIZE=(2*N+2)*W+193};
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,W,SIZE));
  dm_guest_write_prepare(storage,SIZE);memset(storage,0x59,SIZE);
  mtl_init();mtl_finish();uint64_t previous=V.options.pool_window_bytes;
  vk_test_pool_window_bytes(W);mtl_pool_add(storage,SIZE);
  vk_pool_requirement needs[N];
  for(unsigned i=0;i<N;i++)needs[i]=pool_window_need(storage+(2*i+1)*W+64,16,2,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,0);
  vk_pool_reserve_result reserve;CHECK(vk_pool_reserve_ranges(needs,N,W,&reserve)==VK_POOL_RESERVE_OK);
  CHECK(reserve.added_spans==N);unsigned nodes;uint64_t bytes;
  for(unsigned i=0;i<2*N+2;i++)for(unsigned j=0;j<3;j++) {
    uint64_t offset=(uint64_t)i*W+(j?j==1?W-1:W:0);
    for(unsigned length=0;length<3;length++)
      (void)vk_test_pool_span_search((uintptr_t)storage,offset,length,0,&nodes,&bytes);
  }
  CHECK(nodes==N && bytes==32*sizeof(void *));
  CHECK(!vk_test_pool_span_search((uintptr_t)storage,W+W-1,2,0,&nodes,&bytes));
  CHECK(!vk_test_pool_span_search((uintptr_t)storage,SIZE,1,0,&nodes,&bytes));
  CHECK(!vk_test_pool_span_search((uintptr_t)storage,UINT64_MAX,2,0,&nodes,&bytes));
  // Metadata OOM preserves the old lookup and suppresses hot allocation
  // retries for this topology generation. A fresh metadata reset can retry.
  CHECK(vk_test_pool_span_search((uintptr_t)storage,W+64,16,1,&nodes,&bytes));CHECK(!nodes&&!bytes);
  CHECK(vk_test_pool_span_search((uintptr_t)storage,W+64,16,0,&nodes,&bytes));CHECK(!nodes&&!bytes);
  CHECK(vk_test_pool_span_search((uintptr_t)storage,W+64,16,2,&nodes,&bytes));CHECK(nodes==N);
  // Add a previously empty hole after the sorted index has been built.
  vk_pool_requirement added=pool_window_need(storage+2*W+64,16,2,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,0);
  CHECK(vk_pool_reserve_ranges(&added,1,W,&reserve)==VK_POOL_RESERVE_OK);CHECK(reserve.added_spans==1);
  CHECK(vk_test_pool_span_search((uintptr_t)storage,2*W+64,16,0,&nodes,&bytes));CHECK(nodes==N+1);
  // Adjacent physical nodes still cannot masquerade as one VkBuffer range.
  CHECK(!vk_test_pool_span_search((uintptr_t)storage,2*W-1,2,0,&nodes,&bytes));
  uint8_t out[16];
  for(unsigned i=0;i<N;i++) {
    mtl_sub_begin();int token=vk_pool_operation_begin_ranges(needs+i,1);CHECK(token);
    pool_window_fill((void *)(uintptr_t)needs[i].address,16,0x12340000u+i);
    pool_window_read((void *)(uintptr_t)needs[i].address,16,out);
    vk_pool_operation_end(token);vk_internal_wait(vk_internal_commit());
    for(unsigned word=0;word<4;word++)CHECK(!memcmp(out+4*word,(uint32_t[]){0x12340000u+i},4));
  }
  // Whole-pool recreation at the identical guest address cannot reuse the
  // former index. Zero-sized lookup retains original tail semantics.
  mtl_pool_del(storage);vk_test_pool_window_bytes(0);mtl_pool_add(storage,SIZE);
  mtl_sub_begin();pool_window_read(storage+W+64,16,out);vk_internal_wait(vk_internal_commit());
  CHECK(vk_test_pool_span_search((uintptr_t)storage,SIZE,0,0,&nodes,&bytes));CHECK(!nodes&&!bytes);
  mtl_pool_del(storage);free(storage);vk_test_pool_window_bytes(previous);
  LOG("[pool span index] original resolver parity, holes, adjacency, tail/overflow, append, metadata OOM, real GPU bytes and same-address recreation passed");
}
#endif
