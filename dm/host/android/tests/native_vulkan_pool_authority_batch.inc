// Real sparse CPU publication and GPU bytes: no timing/counters or synthetic
// authority mutation. Serial reference is covered by the unchanged fullsuite.
#ifdef DM_VULKAN_TEST_EMBED
static void pool_authority_batch_bytes(uint8_t *source,uint8_t *dest,unsigned size,const void *expected) {
  mtl_sub_begin();mtl_sub_copy((uintptr_t)source,(uintptr_t)dest,size);
  vk_internal_wait(vk_internal_commit());CHECK(!memcmp(dest,expected,size));
}
static void pool_authority_batch_test(void) {
  mtl_init();if(!vk_async_submissions() || V.options.pool_mirror_reference)return;
  mtl_finish();mtl_sub_begin();vk_internal_wait(vk_internal_commit());
  if(vk_submission_barrier_mode()!=0)return;
  enum {W=65536};uint64_t previous_window=V.options.pool_window_bytes,full,used,protected;int reference;
  vk_pool_sync_policy_stats(&full,&used,&protected,&reference);vk_pool_sync_reference(0);mtl_finish();vk_test_pool_window_bytes(W);
  uint8_t *source,*dest;CHECK(!posix_memalign((void **)&source,W,W));CHECK(!posix_memalign((void **)&dest,W,W));
  for(unsigned test=0;test<3;test++) {
    if(getenv("DM_POOL_AUTHORITY_BATCH_CAPACITY_ONLY") && test!=1)continue;
    dm_guest_write_prepare(source,W);dm_guest_write_prepare(dest,W);memset(source,0x29,W);memset(dest,0xa5,W);
    mtl_pool_add(source,W);mtl_pool_add(dest,W);
    uint8_t baseline[4096];mtl_sub_begin();pool_window_read(source,4096,baseline);vk_internal_wait(vk_internal_commit());
    const uint32_t gpu=0x11223344,cpu0=0x87654321,cpu1=0xabcd3210;
    uint8_t expected[4096];for(unsigned k=0;k<4096;k++)expected[k]=((uint8_t *)&gpu)[k%4];
    if(test==0) {
      // A first 16B unknown plus255 separate unknowns exactly fills the map.
      // Clearing its interior4B alone would split to257. Clearing another
      // complete4B entry in the SAME PAGE leaves a representable256-entry
      // result: only the exact union, never the envelope, becomes CPU-known.
      mtl_sub_begin();pool_window_fill(source,16,gpu);
      for(unsigned k=0;k<255;k++)pool_window_fill(source+64+k*8,4,0x77770000+k);
      vk_internal_wait(vk_internal_commit());
      dm_guest_write_prepare(source+4,4);memcpy(source+4,&cpu0,4);
      dm_guest_write_prepare(source+64,4);memcpy(source+64,&cpu1,4);
      pool_authority_copy_case(source+4,dest,4,&cpu0,0); // Before this commit's upload.
      pool_authority_copy_case(source+4,dest,4,&cpu0,1); // Full batch fits, interior is known.
      pool_authority_copy_case(source+64,dest+16,4,&cpu1,1);
      memcpy(expected+4,&cpu0,4);pool_authority_batch_bytes(source+8,dest,8,expected+8);
      pool_authority_batch_bytes(source,dest,16,expected);
      pool_authority_copy_case(source,dest,16,expected,0);
      uint32_t last=0x77770000+254;pool_authority_copy_case(source+64+254*8,dest,4,&last,0);
    } else if(test==1) {
      // 512 exact CPU runs would split one GPU-only page into512 unknown
      // pieces. Overflow must retain the original scalar known-prefix result,
      // not discard1024B of recovered CPU authority or any late GPU neighbor.
      mtl_sub_begin();pool_window_fill(source,4096,gpu);vk_internal_wait(vk_internal_commit());
      dm_guest_write_prepare(source,4096);
      for(unsigned k=0;k<512;k++) {uint32_t value=0x88990000+k;memcpy(source+k*8,&value,4);memcpy(expected+k*8,&value,4);}
      pool_authority_copy_case(source,dest,4096,expected,0);
      pool_authority_copy_case(source,dest,4,expected,1); // Original scalar prefix remains known.
      pool_authority_batch_bytes(source+4092,dest,4,&gpu);
      pool_authority_copy_case(source+4092,dest,4,&gpu,0); // Unchanged GPU-only tail must remain unknown.
      // Check every exact changed4B, using original copy/snapshot counters.
      // Queue all512 original copies in one CB; the first256 have CPU proof,
      // later words remain conservative GPU reads, while all output bytes agree.
      mtl_sub_begin();uint64_t snapshots,published,fallbacks,next_snapshots,next_published,next_fallbacks;int copy_reference,next_reference;
      for(unsigned k=0;k<512;k++) {
        vk_cpu_copy_stats(&snapshots,&published,&fallbacks,&copy_reference);
        mtl_sub_copy((uintptr_t)source+k*8,(uintptr_t)dest+k*4,4);
        vk_cpu_copy_stats(&next_snapshots,&next_published,&next_fallbacks,&next_reference);
        CHECK(copy_reference==next_reference);
        if(!copy_reference) {
          CHECK(next_snapshots-snapshots==(k<256?4u:0u));
          CHECK(next_fallbacks-fallbacks==(k<256?0u:4u));
        }
      }
      vk_internal_wait(vk_internal_commit());
      for(unsigned k=0;k<512;k++)CHECK(!memcmp(dest+k*4,expected+k*8,4));
      mtl_sub_begin();vk_buffer_writeback_stage((uintptr_t)source+2048,4,VK_PIPELINE_STAGE_TRANSFER_BIT);vk_internal_wait(vk_internal_commit());
      pool_authority_copy_case(source+2048,dest,4,expected+2048,1); // A true later publication recovers the unproved suffix.
    } else {
      // Both CPU changes are uploaded BEFORE the current MAIN CB. A newer
      // same-CB GPU writer must survive this page's older upload cutoff.
      dm_guest_write_prepare(source,16);memcpy(source,&cpu0,4);memcpy(source+8,&cpu1,4);
      memcpy(expected,baseline,16);memcpy(expected,&gpu,4);memcpy(expected+8,&cpu1,4);
      mtl_sub_begin();pool_window_fill(source,4,gpu);mtl_sub_copy((uintptr_t)source,(uintptr_t)dest,16);
      vk_internal_wait(vk_internal_commit());CHECK(!memcmp(dest,expected,16));
      CHECK(!memcmp(source,&cpu0,4)&&!memcmp(source+8,&cpu1,4)); // Shadow/guest retain CPU values.
      pool_authority_batch_bytes(source,dest,16,expected);
      pool_authority_copy_case(source,dest,16,expected,0); // Must fetch current GPU values, not CPU snapshot.
    }
    mtl_pool_del(source);mtl_pool_del(dest);
  }
  free(source);free(dest);vk_test_pool_window_bytes(previous_window);vk_pool_sync_reference(reference);
}
#endif
