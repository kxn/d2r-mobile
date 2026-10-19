// Real GPU data oracle for per-page mapping reuse. No production hit counter,
// custom timing or persistent mapping cache is introduced by this fixture.
#ifdef DM_VULKAN_TEST_EMBED
extern int vk_test_pool_upload_page_span(uint64_t,uint64_t,unsigned);
static void upload_page_span_test(void) {
  mtl_init();if(V.options.pool_mirror_reference)return;
  enum {W=65536,SIZE=4*W+193,P=W+4096};uint8_t *storage;
  CHECK(!posix_memalign((void **)&storage,W,SIZE));dm_guest_write_prepare(storage,SIZE);memset(storage,0x31,SIZE);
  uint64_t prior_window=V.options.pool_window_bytes,full,used,protected;int reference;
  int prior_scan=vk_upload_scan_reference_mode();vk_upload_scan_reference(0);
  vk_pool_sync_policy_stats(&full,&used,&protected,&reference);vk_pool_sync_reference(0);
  mtl_finish();vk_test_pool_window_bytes(W);mtl_pool_add(storage,SIZE);
  vk_pool_requirement needs[]={pool_window_need(storage+P+64,4,2,VK_PIPELINE_STAGE_TRANSFER_BIT,0),
    pool_window_need(storage+2*W+64,4,2,VK_PIPELINE_STAGE_TRANSFER_BIT,0),
    pool_window_need(storage+4*W+64,4,2,VK_PIPELINE_STAGE_TRANSFER_BIT,0)};
  vk_pool_reserve_result reserve;CHECK(vk_pool_reserve_ranges(needs,3,W,&reserve)==VK_POOL_RESERVE_OK && reserve.added_spans==3);
  CHECK(vk_test_pool_upload_page_span((uintptr_t)storage+P,4096,2));
  CHECK(!vk_test_pool_upload_page_span((uintptr_t)storage+P,4096,1));
  CHECK(!vk_test_pool_upload_page_span((uintptr_t)storage+P,0,2));
  // These are real independent VkBuffers, not one concatenated allocation.
  vk_pool_span_info a,b;CHECK(vk_pool_span_query((uintptr_t)storage+P,4,&a)==VK_POOL_SPAN_SINGLE);
  CHECK(vk_pool_span_query((uintptr_t)storage+2*W,4,&b)==VK_POOL_SPAN_SINGLE && a.handle!=b.handle);
  CHECK(!vk_test_pool_upload_page_span((uintptr_t)storage+2*W-4,8,2)); // Adjacent nodes.
  CHECK(!vk_test_pool_upload_page_span((uintptr_t)storage+3*W-4,8,2)); // Into a hole.
  CHECK(!vk_test_pool_upload_page_span((uintptr_t)storage+3*W+4,8,2)); // Entirely a hole.
  CHECK(!vk_test_pool_upload_page_span((uintptr_t)storage+SIZE-4,8,2)); // Pool tail.
  uint8_t expected[4096],actual[4096];memset(expected,0x31,sizeof expected);
  mtl_sub_begin();pool_window_read(storage+P,4096,actual);vk_internal_wait(vk_internal_commit());CHECK(!memcmp(actual,expected,4096));
  int sparse=vk_async_submissions() && vk_submission_barrier_mode()==0;
  if(sparse) {
    // The GPU word remains newer than both guest and shadow. CPU edits on
    // either side must never make the mapping proof authorize a page memcpy.
    uint32_t gpu=0x87654321;mtl_sub_begin();pool_window_fill(storage+P+128,4,gpu);vk_internal_wait(vk_internal_commit());
    CHECK(memcmp(storage+P+128,&gpu,4));memcpy(expected+128,&gpu,4);
  }
  const unsigned offsets[]={4,64,252,1020,4092};
  for(unsigned i=0;i<sizeof offsets/sizeof *offsets;i++) {
    uint32_t value=0xabc01200+i;dm_guest_write_prepare(storage+P+offsets[i],4);
    memcpy(storage+P+offsets[i],&value,4);memcpy(expected+offsets[i],&value,4);
  }
  mtl_sub_begin();pool_window_read(storage+P,4096,actual);vk_internal_wait(vk_internal_commit());CHECK(!memcmp(actual,expected,4096));
  if(sparse)CHECK(!memcmp(storage+P+128,(uint8_t[]){0x31,0x31,0x31,0x31},4));
  // A second real page in another nonzero-origin backing has unrelated bytes.
  // No persistent last-node/local-offset may survive between these scans.
  uint32_t value=0x1122aabb;dm_guest_write_prepare(storage+2*W+16,4);memcpy(storage+2*W+16,&value,4);
  dm_guest_write_prepare(storage+2*W+4088,4);memcpy(storage+2*W+4088,&value,4);
  mtl_sub_begin();pool_window_read(storage+2*W,4096,actual);vk_internal_wait(vk_internal_commit());
  CHECK(!memcmp(actual+16,&value,4)&&!memcmp(actual+4088,&value,4));CHECK(actual[0]==0x31&&actual[4095]==0x31);
  mtl_sub_begin();pool_window_read(storage+P,4096,actual);vk_internal_wait(vk_internal_commit());CHECK(!memcmp(actual,expected,4096));
  // Keep the original scalar scanner and per-run physical lookup as an
  // independent reference, while preserving the same newer GPU-only gap.
  vk_upload_scan_reference(1);value=0x77aabbcc;
  dm_guest_write_prepare(storage+P+8,4);memcpy(storage+P+8,&value,4);memcpy(expected+8,&value,4);
  dm_guest_write_prepare(storage+P+4000,4);memcpy(storage+P+4000,&value,4);memcpy(expected+4000,&value,4);
  mtl_sub_begin();pool_window_read(storage+P,4096,actual);vk_internal_wait(vk_internal_commit());CHECK(!memcmp(actual,expected,4096));
  vk_upload_scan_reference(0);
  // The unaligned real pool tail retains its existing full-copy fallback.
  uint8_t tail[193];memset(tail,0x31,sizeof tail);tail[4]=0x77;tail[180]=0xa9;
  dm_guest_write_prepare(storage+4*W,193);memcpy(storage+4*W,tail,193);
  mtl_sub_begin();pool_window_read(storage+4*W,193,actual);vk_internal_wait(vk_internal_commit());CHECK(!memcmp(actual,tail,193));
  mtl_pool_del(storage);free(storage);vk_test_pool_window_bytes(prior_window);vk_pool_sync_reference(reference);vk_upload_scan_reference(prior_scan);
  LOG("[vulkan upload page span] exact multi-run nonzero-origin GPU bytes, GPU-only unchanged gap, distinct next backing, adjacent/hole proof rejection and193B tail passed; sparse=%d",sparse);
}
#endif
