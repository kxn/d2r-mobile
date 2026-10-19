// Original NVN-owned guest pool, two real VkBuffers with distinct coordinate
// origins. The split installer is test-only and refuses previously used pools;
// all lookups, dependencies, dirty uploads and completion effects are production.
#ifdef DM_VULKAN_TEST_EMBED
extern int vk_test_pool_split_backings(uint64_t address,uint64_t split);
extern int vk_test_pool_gap_backings(uint64_t address,uint64_t first_end,uint64_t tail_begin);
extern int vk_test_pool_migrate_whole(uint64_t address);
static void pool_span_gpu_read(uint8_t *guest,unsigned size,uint8_t *result) {
  vk_buffer *source;uint64_t offset;
  CHECK(vk_pool_address_stage((uintptr_t)guest,size,(void **)&source,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
  CHECK(offset<=source->size && size<=source->size-offset);
  vk_buffer *readback=vk_temp_buffer(size);
  const vk_buffer_use use={(uintptr_t)guest,size,0};vk_transfer_buffer_dependencies(&use,1);
  VkBufferCopy copy={.srcOffset=offset,.size=size};
  vkCmdCopyBuffer(V.cmd,source->handle,readback->handle,1,&copy);
  VkBufferMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,
    .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
    .buffer=readback->handle,.offset=0,.size=size};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&host,0,NULL);
  vk_effect_copy(result,readback->map,size);
}
static void pool_span_gpu_fill(uint8_t *guest,unsigned size,uint32_t value) {
  vk_buffer *destination;uint64_t offset;
  CHECK(vk_pool_address_overwrite((uintptr_t)guest,size,(void **)&destination,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
  const vk_buffer_use use={(uintptr_t)guest,size,1};vk_transfer_buffer_dependencies(&use,1);
  vkCmdFillBuffer(V.cmd,destination->handle,offset,size,value);
  vk_pool_overwritten((uintptr_t)guest,size);
  vk_buffer_transfer_written_range((uintptr_t)guest,size);
}
static void pool_backing_span_test(void) {
  enum {size=65536,split=8192};uint8_t *storage;
  CHECK(!posix_memalign((void **)&storage,65536,size));
  dm_guest_write_prepare(storage,size);
  for(unsigned i=0;i<size;i++)storage[i]=(uint8_t)(0x31+i*11+(i/4096)*13);
  NVNmemoryPoolBuilder builder;NVNmemoryPool owned;
  CALL(nvnMemoryPoolBuilderSetDefaults,&builder);CALL(nvnMemoryPoolBuilderSetDevice,&builder,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&builder,storage,size);
  CALL(nvnMemoryPoolBuilderSetFlags,&builder,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&owned,&builder));
  vk_pool_span_info unbacked,a,b,again;
  int query=vk_pool_span_query((uintptr_t)storage+split+16,64,&unbacked);
  if(V.options.pool_mirror_reference) {
    CHECK(query==VK_POOL_SPAN_SINGLE);CHECK(unbacked.logical_begin==0&&unbacked.logical_end==size);
    CHECK(!vk_test_pool_split_backings((uintptr_t)storage,split));
    CALL(nvnMemoryPoolFinalize,&owned);free(storage);
    LOG("[vulkan canonical pool spans fixture] eager whole reference retained; fresh-only split correctly refused");return;
  }
  CHECK(query==VK_POOL_SPAN_UNBACKED);CHECK(!unbacked.handle&&unbacked.pool_size==size);
  uint64_t initialized=vk_pool_initialized_bytes();
  CHECK(!vk_test_pool_split_backings((uintptr_t)storage,split+1));
  CHECK(vk_test_pool_split_backings((uintptr_t)storage,split));
  CHECK(!vk_test_pool_split_backings((uintptr_t)storage,split));
  CHECK(vk_pool_initialized_bytes()==initialized);
  CHECK(vk_pool_span_query((uintptr_t)storage+16,64,&a)==VK_POOL_SPAN_SINGLE);
  CHECK(vk_pool_span_query((uintptr_t)storage+split+16,64,&b)==VK_POOL_SPAN_SINGLE);
  CHECK(a.handle!=b.handle&&a.pool_identity==b.pool_identity);
  CHECK(a.local_offset==16&&b.local_offset==16&&a.logical_end==split&&b.logical_begin==split);
  CHECK(a.backing_generation&&b.backing_generation&&a.backing_generation!=b.backing_generation);
  CHECK(vk_pool_span_query((uintptr_t)storage+split-4,8,&again)==VK_POOL_SPAN_CROSSING);
  CHECK(again.pool_identity==a.pool_identity&&!again.handle);
  CHECK(vk_pool_span_query((uintptr_t)storage+size-1,2,&again)==VK_POOL_SPAN_NONE);
  CHECK(vk_pool_span_query((uintptr_t)storage,0,&again)==VK_POOL_SPAN_NONE);
  CHECK(vk_pool_span_query(UINT64_MAX-7,16,&again)==VK_POOL_SPAN_NONE);
  CHECK(vk_pool_initialized_bytes()==initialized);
  uint8_t result[1024],expected[1024];
  memcpy(expected,storage+split-64,64);memcpy(expected+64,storage+split,64);
  mtl_sub_begin();pool_span_gpu_read(storage+split-64,64,result);
  pool_span_gpu_read(storage+split,64,result+64);
  uint64_t serial=vk_internal_commit();vk_internal_wait(serial);
  for(unsigned i=0;i<128;i++)CHECK(result[i]==expected[i]);
  // Two physically separate producers become one contiguous logical writeback.
  // This must split the copies/effects without copying a gap or a stale prefix.
  memcpy(expected,storage+split-32,64);
  uint32_t lo=0x92736455,hi=0xa1b2c3d4;
  memcpy(expected+24,&lo,4);memcpy(expected+28,&lo,4);
  memcpy(expected+32,&hi,4);memcpy(expected+36,&hi,4);
  mtl_sub_begin();pool_span_gpu_fill(storage+split-8,8,lo);pool_span_gpu_fill(storage+split,8,hi);
  vk_buffer_writeback_stage((uintptr_t)storage+split-8,16,VK_PIPELINE_STAGE_TRANSFER_BIT);
  serial=vk_internal_commit();vk_internal_wait(serial);
  for(unsigned i=0;i<64;i++)CHECK(storage[split-32+i]==expected[i]);
  // Re-enter after effects publication and alter CPU bytes in the nonzero-base
  // backing. Its own VkBuffer offset, dirty shadow and guest address must agree.
  dm_guest_write_prepare(storage+split+32,32);memset(storage+split+32,0x6d,32);
  memcpy(expected,storage+split,96);
  mtl_sub_begin();pool_span_gpu_read(storage+split,96,result);
  serial=vk_internal_commit();vk_internal_wait(serial);
  for(unsigned i=0;i<96;i++)CHECK(result[i]==expected[i]);
  // The original known-copy path retains logical addresses and publication
  // ranges while its two VkBuffer offsets have different backing origins.
  uint8_t known_expected[32];memcpy(known_expected,storage+split+32,32);
  mtl_sub_begin();mtl_sub_copy((uintptr_t)storage+split+32,(uintptr_t)storage+64,32);
  serial=vk_internal_commit();vk_internal_wait(serial);
  for(unsigned i=0;i<32;i++)CHECK(storage[64+i]==known_expected[i]);
  mtl_sub_begin();pool_span_gpu_read(storage+64,32,result);
  serial=vk_internal_commit();vk_internal_wait(serial);
  for(unsigned i=0;i<32;i++)CHECK(result[i]==known_expected[i]);
  if(vk_async_submissions()&&V.timeline_semaphore) {
    uint8_t old[512],newer[512],old_expected[512],new_expected[512];
    uint8_t *region=storage+split+4096;memcpy(old_expected,region,512);
    VkSemaphoreTypeCreateInfoKHR type={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR,
      .semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE_KHR};
    VkSemaphoreCreateInfo create={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,.pNext=&type};VkSemaphore gate;
    VK_CHECK(vkCreateSemaphore(V.device,&create,NULL,&gate));uint64_t value=1;
    VkTimelineSemaphoreSubmitInfoKHR timeline={.sType=VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR,
      .waitSemaphoreValueCount=1,.pWaitSemaphoreValues=&value};VkPipelineStageFlags stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo block={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.pNext=&timeline,
      .waitSemaphoreCount=1,.pWaitSemaphores=&gate,.pWaitDstStageMask=&stage};
    pthread_mutex_lock(&V.queue_mu);VK_CHECK(vkQueueSubmit(V.queue,1,&block,VK_NULL_HANDLE));pthread_mutex_unlock(&V.queue_mu);
    mtl_sub_begin();pool_span_gpu_read(region,512,old);
    vk_buffer *physical;uint64_t local;
    CHECK(vk_pool_address_stage((uintptr_t)region,512,(void **)&physical,&local,VK_PIPELINE_STAGE_TRANSFER_BIT));
    CHECK(local==4096);
    uint64_t first=vk_internal_commit();CHECK(!mtl_wait(first,0));
    dm_guest_write_prepare(region+20,4);memset(region+20,0x97,4);
    dm_guest_write_prepare(region+96,4);memset(region+96,0xc6,4);
    memcpy(new_expected,region,512);
    mtl_sub_begin();pool_span_gpu_read(region,512,newer);
    serial=vk_internal_commit();CHECK(!mtl_wait(first,0));
    // Queued changed bytes may not race the older reader, and both physical
    // source offsets must remain those captured for this exact backing.
    for(unsigned i=0;i<512;i++)CHECK(physical->map[local+i]==old_expected[i]);
    VkSemaphoreSignalInfoKHR signal={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO_KHR,.semaphore=gate,.value=1};
    VK_CHECK(V.signal_timeline(V.device,&signal));vk_internal_wait(serial);
    for(unsigned i=0;i<512;i++){CHECK(old[i]==old_expected[i]);CHECK(newer[i]==new_expected[i]);}
    vkDestroySemaphore(V.device,gate,NULL);
  }
  // No migration may end/alter a caller's active operation, even in the
  // internal-submit gap where V.cmd is null. No raw physical borrow survives
  // these scope tests; all real prior work was explicitly waited above.
  vk_shader_operation_begin();CHECK(!vk_test_pool_migrate_whole((uintptr_t)storage));vk_shader_writebacks();
  vk_texture_binding_scope_begin();CHECK(!vk_test_pool_migrate_whole((uintptr_t)storage));vk_texture_binding_scope_end();
  vk_texture_shader_begin();CHECK(!vk_test_pool_migrate_whole((uintptr_t)storage));vk_texture_shader_record();
  mtl_sub_begin();CHECK(!vk_test_pool_migrate_whole((uintptr_t)storage));
  serial=vk_internal_commit();vk_internal_wait(serial);
  // Page3 is cold in the synchronous path (the gated subcase above is async
  // only). Establish its original device bytes explicitly before the pending
  // CPU edit, so the later pre-upload assertion tests migration, not first use.
  uint8_t edit_baseline[4];memcpy(edit_baseline,storage+split+4096+64,4);
  mtl_sub_begin();pool_span_gpu_read(storage+split+4096+64,4,result);
  serial=vk_internal_commit();vk_internal_wait(serial);
  for(unsigned i=0;i<4;i++)CHECK(result[i]==edit_baseline[i]);
  uint32_t gpu_only=0xd6c3a57b;uint8_t untouched[16];memcpy(untouched,storage+split+128,16);
  mtl_sub_begin();pool_span_gpu_fill(storage+split+128,16,gpu_only);
  // A real old-backing view belongs to the normal submission recycler. The
  // migration must finish reset/recycle before destroying its source buffer.
  vk_buffer *view_source;uint64_t view_offset;
  CHECK(vk_pool_address_stage((uintptr_t)storage+split,256,(void **)&view_source,&view_offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
  VkBufferViewCreateInfo old_view_info={.sType=VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
    .buffer=view_source->handle,.format=VK_FORMAT_R32_UINT,.offset=view_offset,.range=256};VkBufferView old_view;
  VK_CHECK(vkCreateBufferView(V.device,&old_view_info,NULL,&old_view));vk_texture_own_buffer_view(old_view);
  serial=vk_internal_commit();vk_internal_wait(serial);
  CHECK(!memcmp(storage+split+128,untouched,16));
  // CPU bytes changed after the last upload must not be copied over old GPU
  // storage during migration. They still become inputs on the next real read.
  uint8_t *edit=storage+split+4096+64;uint8_t previous[4];memcpy(previous,edit,4);
  dm_guest_write_prepare(edit,4);memset(edit,0xb9,4);
  uint64_t before_migration=vk_pool_initialized_bytes(),guest_fence=mtl_last_fence();
  uint64_t full_bytes,used_bytes,protected_bytes;int saved_sync;
  vk_pool_sync_policy_stats(&full_bytes,&used_bytes,&protected_bytes,&saved_sync);
  vk_pool_sync_reference(1); // Both begin-time and commit-time reference uploads must be suppressed.
  CHECK(vk_test_pool_migrate_whole((uintptr_t)storage));vk_pool_sync_reference(saved_sync);
  CHECK(mtl_last_fence()==guest_fence);CHECK(vk_pool_initialized_bytes()==before_migration);
  CHECK(vk_pool_span_query((uintptr_t)storage+split-8,16,&again)==VK_POOL_SPAN_SINGLE);
  CHECK(again.pool_identity==a.pool_identity&&again.logical_begin==0&&again.logical_end==size);
  CHECK(again.handle!=a.handle&&again.handle!=b.handle);
  CHECK(again.backing_generation>a.backing_generation&&again.backing_generation>b.backing_generation);
  CHECK(again.local_offset==split-8);CHECK(!vk_test_pool_migrate_whole((uintptr_t)storage));
  mtl_sub_begin();vk_buffer *migrated;uint64_t local;
  CHECK(vk_pool_address_stage((uintptr_t)edit,4,(void **)&migrated,&local,VK_PIPELINE_STAGE_TRANSFER_BIT));
  CHECK(local==(uint64_t)(edit-storage));for(unsigned i=0;i<4;i++)CHECK(migrated->map[local+i]==previous[i]);
  pool_span_gpu_read(storage+split+128,16,result);pool_span_gpu_read(edit,4,result+16);
  serial=vk_internal_commit();vk_internal_wait(serial);
  for(unsigned i=0;i<4;i++){uint32_t word;memcpy(&word,result+i*4,4);CHECK(word==gpu_only);}
  for(unsigned i=16;i<20;i++)CHECK(result[i]==0xb9);
  CHECK(!memcmp(storage+split+128,untouched,16));
  uint64_t identity=a.pool_identity;CALL(nvnMemoryPoolFinalize,&owned);

  CHECK(vk_pool_span_query((uintptr_t)storage,4,&again)==VK_POOL_SPAN_NONE);
  CHECK(CALL(nvnMemoryPoolInitialize,&owned,&builder));
  CHECK(vk_pool_span_query((uintptr_t)storage,4,&again)==VK_POOL_SPAN_UNBACKED);
  CHECK(again.pool_identity!=identity);
  // A second fresh lifetime has no initialized logical pages at all. A cold
  // GPU copy may carry undefined allocation bytes, but must not mark them as
  // initialized or publish them to guest/shadow. First use reads guest truth.
  uint64_t cold_initialized=vk_pool_initialized_bytes();
  CHECK(vk_test_pool_gap_backings((uintptr_t)storage,split,split+4096));
  CHECK(vk_pool_span_query((uintptr_t)storage+split+16,64,&b)==VK_POOL_SPAN_CROSSING);
  CHECK(!b.handle&&b.pool_identity==again.pool_identity);
  vk_pool_sync_reference(1);CHECK(vk_test_pool_migrate_whole((uintptr_t)storage));vk_pool_sync_reference(saved_sync);
  CHECK(vk_pool_initialized_bytes()==cold_initialized);
  CHECK(vk_pool_span_query((uintptr_t)storage+size-32,32,&b)==VK_POOL_SPAN_SINGLE);
  CHECK(b.pool_identity==again.pool_identity);
  memcpy(expected,storage+size-32,32);memcpy(expected+32,storage+split+16,64);
  mtl_sub_begin();pool_span_gpu_read(storage+size-32,32,result);pool_span_gpu_read(storage+split+16,64,result+32);
  serial=vk_internal_commit();vk_internal_wait(serial);
  for(unsigned i=0;i<96;i++)CHECK(result[i]==expected[i]);
  CALL(nvnMemoryPoolFinalize,&owned);free(storage);
  LOG("[vulkan canonical pool spans fixture] metadata-only statuses, nonzero physical offsets, two-buffer logical writeback, CPU edits, queued upload ownership, quiescent GPU migration/dirty deferral/cold holes and pool address reuse passed");
}
#else
static void pool_backing_span_test(void) {
  LOG("[vulkan canonical pool spans fixture] test-only split installer unavailable in this standalone target");
}
#endif
