// Explicit policy API only: no production draw/dispatch has windowing enabled.
// Checks real GPU bytes, logical/physical offsets and allocation rollback, never
// process RSS, elapsed time or a claim that the 6GiB-device problem is solved.
#ifdef DM_VULKAN_TEST_EMBED
extern void vk_test_pool_reserve_fail_after(int);
extern int vk_test_pool_reserve_metadata(uint64_t);
extern int vk_test_pool_migrate_whole(uint64_t);
static vk_pool_requirement pool_window_need(uint8_t *address,uint64_t size,int kind,
    VkPipelineStageFlags stage,uint64_t alignment) {
  return (vk_pool_requirement){.address=(uintptr_t)address,.size=size,
    .prefix_alignment=alignment,.stages=stage,.binding_kind=kind};
}
static void pool_window_read(uint8_t *guest,unsigned size,uint8_t *result) {
  vk_buffer *source;uint64_t offset;
  CHECK(vk_pool_address_stage((uintptr_t)guest,size,(void **)&source,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
  CHECK(offset<=source->size&&size<=source->size-offset);
  vk_buffer *readback=vk_temp_buffer(size);vk_buffer_use use={(uintptr_t)guest,size,0};
  vk_transfer_buffer_dependencies(&use,1);
  VkBufferCopy copy={.srcOffset=offset,.size=size};
  vkCmdCopyBuffer(V.cmd,source->handle,readback->handle,1,&copy);
  VkBufferMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,
    .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
    .buffer=readback->handle,.offset=0,.size=size};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,
    0,0,NULL,1,&host,0,NULL);vk_effect_copy(result,readback->map,size);
}
static void pool_window_fill(uint8_t *guest,unsigned size,uint32_t word) {
  vk_buffer *destination;uint64_t offset;
  CHECK(vk_pool_address_overwrite((uintptr_t)guest,size,(void **)&destination,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
  vk_buffer_use use={(uintptr_t)guest,size,1};vk_transfer_buffer_dependencies(&use,1);
  vkCmdFillBuffer(V.cmd,destination->handle,offset,size,word);
  vk_pool_overwritten((uintptr_t)guest,size);vk_buffer_transfer_written_range((uintptr_t)guest,size);
}
static void pool_window_reservation_test(void) {
  enum {pool_bytes=4<<20,window=65536};
  uint64_t full,used,protected;int reference;
  vk_pool_sync_policy_stats(&full,&used,&protected,&reference);
  // Deliberately select the ordinary range-sync policy for this fixture and
  // restore its caller's reference after the last real GPU operation.
  vk_pool_sync_reference(0);mtl_sub_begin();vk_internal_wait(vk_internal_commit());
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,pool_bytes));
  dm_guest_write_prepare(storage,pool_bytes);
  for(unsigned i=0;i<pool_bytes;i++)storage[i]=(uint8_t)(i*13+(i/4096)*7+0x31);
  NVNmemoryPoolBuilder builder;NVNmemoryPool owned;
  CALL(nvnMemoryPoolBuilderSetDefaults,&builder);CALL(nvnMemoryPoolBuilderSetDevice,&builder,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&builder,storage,pool_bytes);
  CALL(nvnMemoryPoolBuilderSetFlags,&builder,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&owned,&builder));
  vk_pool_requirement needs[]={
    pool_window_need(storage+2*window+128,4096,1,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,256),
    pool_window_need(storage+2*window+8192,2048,2,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,256),
    pool_window_need(storage+10*window+512,8192,VK_POOL_REQUIRE_VERTEX,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,0),
    pool_window_need(storage+10*window+32768,12,VK_POOL_REQUIRE_INDEX,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,0),
    pool_window_need(storage+2*window+128,4096,1,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,256)};
  vk_pool_reserve_result result;vk_pool_span_info a,b,query;
  CHECK(vk_pool_reserve_ranges(NULL,0,window,&result)==VK_POOL_RESERVE_OK&&!result.added_spans);
  CHECK(vk_pool_reserve_ranges(NULL,1,window,&result)==VK_POOL_RESERVE_INVALID);
  CHECK(vk_pool_reserve_ranges(needs,VK_POOL_RESERVE_MAX_REQUIREMENTS+1,window,&result)==VK_POOL_RESERVE_INVALID);
  uint64_t initialized=vk_pool_initialized_bytes();
  CHECK(vk_pool_reserve_ranges(needs,5,window,&result)==VK_POOL_RESERVE_OK);
  if(V.options.pool_mirror_reference) {
    CHECK(!result.added_spans);
    CHECK(vk_pool_span_query(needs[0].address,needs[0].size,&a)==VK_POOL_SPAN_SINGLE);
    CHECK(a.logical_begin==0&&a.logical_end==pool_bytes);
    CALL(nvnMemoryPoolFinalize,&owned);free(storage);vk_pool_sync_reference(reference);
    LOG("[vulkan pool window fixture] eager whole reference retained; explicit reservation did not replace it");return;
  }
  CHECK(result.added_spans==2&&result.added_buffer_bytes==2*window);
  CHECK(result.added_buffer_bytes<pool_bytes/8);
  CHECK(!vk_test_pool_reserve_metadata((uintptr_t)storage));
  CHECK(vk_pool_initialized_bytes()==initialized);
  CHECK(vk_pool_span_query(needs[0].address,needs[0].size,&a)==VK_POOL_SPAN_SINGLE);
  CHECK(vk_pool_span_query(needs[2].address,needs[2].size,&b)==VK_POOL_SPAN_SINGLE);
  CHECK(a.logical_begin==2*window&&a.logical_end==3*window&&a.local_offset==128);
  CHECK(b.logical_begin==10*window&&b.logical_end==11*window&&b.local_offset==512);
  CHECK(a.handle!=b.handle&&a.pool_identity==b.pool_identity);
  CHECK(vk_pool_reserve_ranges(needs,5,window,&result)==VK_POOL_RESERVE_OK&&!result.added_spans);
  vk_pool_requirement edges[]={
    pool_window_need(storage+2*window+32,16,2,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,window),
    pool_window_need(storage+3*window-16,16,7,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,0)};
  CHECK(vk_pool_reserve_ranges(edges,2,window,&result)==VK_POOL_RESERVE_OK&&!result.added_spans);
  CHECK(vk_pool_span_query(edges[0].address,edges[0].size,&query)==VK_POOL_SPAN_SINGLE);
  CHECK(query.local_offset==32&&query.handle==a.handle&&query.backing_generation==a.backing_generation);
  // Every binding's bytes, not just metadata, pass through real production
  // address lookup, CPU initialization, dependency and GPU copy machinery.
  uint8_t pixels[5][8192];mtl_sub_begin();
  for(unsigned k=0;k<5;k++)pool_window_read((uint8_t *)(uintptr_t)needs[k].address,needs[k].size,pixels[k]);
  vk_internal_wait(vk_internal_commit());
  for(unsigned k=0;k<5;k++)for(unsigned i=0;i<needs[k].size;i++)
    CHECK(pixels[k][i]==((const uint8_t *)(uintptr_t)needs[k].address)[i]);
  CHECK(vk_test_pool_reserve_metadata((uintptr_t)storage)==7);
  // A batch requesting a new valid node first still may not partially commit
  // when a later invalid requirement or necessary promotion is discovered.
  vk_pool_requirement batch[]={
    pool_window_need(storage+12*window+64,64,1,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,0),
    pool_window_need(storage+3*window-4,8,2,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0)};
  CHECK(vk_pool_reserve_ranges(batch,2,window,&result)==VK_POOL_RESERVE_NEED_WHOLE);
  CHECK(result.pool_base==(uintptr_t)storage&&result.pool_identity==a.pool_identity&&!result.added_spans);
  CHECK(vk_pool_span_query(batch[0].address,batch[0].size,&query)==VK_POOL_SPAN_CROSSING);
  batch[1].address=UINT64_MAX-7;batch[1].size=16;
  CHECK(vk_pool_reserve_ranges(batch,2,window,&result)==VK_POOL_RESERVE_INVALID);
  CHECK(!result.added_spans&&!result.pool_identity);
  batch[1]=pool_window_need(storage+13*window+64,64,2,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,3);
  CHECK(vk_pool_reserve_ranges(batch,2,window,&result)==VK_POOL_RESERVE_INVALID);
  CHECK(vk_pool_reserve_ranges(needs,5,window-1,&result)==VK_POOL_RESERVE_INVALID);
  CHECK(vk_pool_reserve_ranges(needs,5,2048,&result)==VK_POOL_RESERVE_INVALID);
  CHECK(vk_pool_span_query(needs[0].address,needs[0].size,&query)==VK_POOL_SPAN_SINGLE);
  CHECK(query.handle==a.handle&&query.backing_generation==a.backing_generation);
  // Deterministic OOM after one successful real allocation must free that
  // private node, preserve old handles/generations and publish no range.
  batch[1].prefix_alignment=0;
  uint32_t blocks0,allocs0,blocks1,allocs1;uint64_t bytes0,bytes1;
  vk_memory_stats(&blocks0,&allocs0,&bytes0);
  vk_test_pool_reserve_fail_after(1);
  CHECK(vk_pool_reserve_ranges(batch,2,window,&result)==VK_POOL_RESERVE_OOM);
  vk_test_pool_reserve_fail_after(-1);
  CHECK(result.allocation_error==VK_ERROR_OUT_OF_DEVICE_MEMORY&&!result.added_spans&&!result.added_buffer_bytes);
  vk_memory_stats(&blocks1,&allocs1,&bytes1);CHECK(allocs0==allocs1&&bytes0==bytes1);
  CHECK(vk_pool_span_query(batch[0].address,batch[0].size,&query)==VK_POOL_SPAN_CROSSING);
  CHECK(vk_pool_span_query(batch[1].address,batch[1].size,&query)==VK_POOL_SPAN_CROSSING);
  CHECK(vk_pool_reserve_ranges(batch,2,window,&result)==VK_POOL_RESERVE_OK&&result.added_spans==2);
  CHECK(vk_pool_span_query(batch[0].address,batch[0].size,&query)==VK_POOL_SPAN_SINGLE);
  uint64_t previous_max=a.backing_generation>b.backing_generation?a.backing_generation:b.backing_generation;
  CHECK(query.backing_generation==previous_max+1);
  CHECK(vk_pool_span_query(needs[0].address,needs[0].size,&query)==VK_POOL_SPAN_SINGLE);
  CHECK(query.handle==a.handle&&query.backing_generation==a.backing_generation);
  if(vk_async_submissions()&&V.timeline_semaphore) {
    uint8_t old[64],newer[64],old_expected[64],new_expected[64];
    memcpy(old_expected,storage+2*window+128,64);memcpy(new_expected,storage+20*window+16,64);
    VkSemaphoreTypeCreateInfoKHR type={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR,
      .semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE_KHR};
    VkSemaphoreCreateInfo create={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,.pNext=&type};VkSemaphore gate;
    VK_CHECK(vkCreateSemaphore(V.device,&create,NULL,&gate));uint64_t value=1;
    VkTimelineSemaphoreSubmitInfoKHR timeline={.sType=VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR,
      .waitSemaphoreValueCount=1,.pWaitSemaphoreValues=&value};VkPipelineStageFlags stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo block={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.pNext=&timeline,
      .waitSemaphoreCount=1,.pWaitSemaphores=&gate,.pWaitDstStageMask=&stage};
    pthread_mutex_lock(&V.queue_mu);VK_CHECK(vkQueueSubmit(V.queue,1,&block,VK_NULL_HANDLE));pthread_mutex_unlock(&V.queue_mu);
    mtl_sub_begin();pool_window_read(storage+2*window+128,64,old);
    uint64_t first=vk_internal_commit();CHECK(vk_completed_serial()<first);
    vk_pool_requirement fresh=pool_window_need(storage+20*window+16,64,1,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,0);
    initialized=vk_pool_initialized_bytes();
    CHECK(vk_pool_reserve_ranges(&fresh,1,window,&result)==VK_POOL_RESERVE_OK&&result.added_spans==1);
    CHECK(vk_pool_initialized_bytes()==initialized);
    CHECK(vk_pool_span_query(needs[0].address,needs[0].size,&query)==VK_POOL_SPAN_SINGLE);
    CHECK(query.handle==a.handle&&query.backing_generation==a.backing_generation);
    mtl_sub_begin();pool_window_read(storage+20*window+16,64,newer);uint64_t second=vk_internal_commit();
    CHECK(vk_completed_serial()<first);
    VkSemaphoreSignalInfoKHR signal={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO_KHR,.semaphore=gate,.value=1};
    VK_CHECK(V.signal_timeline(V.device,&signal));vk_internal_wait(second);
    CHECK(!memcmp(old,old_expected,64)&&!memcmp(newer,new_expected,64));vkDestroySemaphore(V.device,gate,NULL);
  }
  // An eager-sync request requires an explicit whole fallback, never an
  // iteration over holes. The separately verified quiescent migration copies
  // real GPU-only bytes and retains their difference from CPU guest/shadow.
  uint32_t word=0x89b7d3e1;uint8_t guest_before[16];memcpy(guest_before,storage+2*window+128,16);
  mtl_sub_begin();pool_window_fill(storage+2*window+128,16,word);
  vk_internal_wait(vk_internal_commit());CHECK(!memcmp(storage+2*window+128,guest_before,16));
  vk_pool_sync_reference(1);
  if(mtl_pool_defer_publication_mode()) {
    // The experimental visibility contract rejects this debug hot switch;
    // verify the actual mode and unchanged reservation rather than pretending
    // the unsupported full-reference request took effect.
    mtl_sub_begin();uint64_t rf,ru,rp;int effective_reference;
    vk_pool_sync_policy_stats(&rf,&ru,&rp,&effective_reference);CHECK(effective_reference==0);
    vk_internal_wait(vk_internal_commit());
    CHECK(vk_pool_reserve_ranges(needs,5,window,&result)==VK_POOL_RESERVE_OK);
  } else CHECK(vk_pool_reserve_ranges(needs,5,window,&result)==VK_POOL_RESERVE_NEED_WHOLE);
  initialized=vk_pool_initialized_bytes();CHECK(vk_test_pool_migrate_whole((uintptr_t)storage));
  CHECK(vk_pool_initialized_bytes()==initialized);
  CHECK(vk_pool_reserve_ranges(needs,5,window,&result)==VK_POOL_RESERVE_OK&&!result.added_spans);
  CHECK(vk_pool_span_query((uintptr_t)storage+3*window-4,8,&query)==VK_POOL_SPAN_SINGLE);
  CHECK(query.logical_begin==0&&query.logical_end==pool_bytes&&query.local_offset==3*window-4);
  // Restore ordinary sync before consumption so the no-CPU-upload migration
  // assertion specifically checks preserved device bytes, not the reference.
  vk_pool_sync_reference(0);mtl_sub_begin();pool_window_read(storage+2*window+128,16,pixels[0]);
  pool_window_read(storage+pool_bytes-64,64,pixels[1]);vk_internal_wait(vk_internal_commit());
  for(unsigned i=0;i<4;i++){uint32_t got;memcpy(&got,pixels[0]+i*4,4);CHECK(got==word);}
  CHECK(!memcmp(pixels[1],storage+pool_bytes-64,64));
  CALL(nvnMemoryPoolFinalize,&owned);
  // Fresh crossing bindings choose one whole backing; a clipped last window
  // still exposes only its actual tail extent. Neither initializes CPU pages.
  CALL(nvnMemoryPoolBuilderSetStorage,&builder,storage,pool_bytes);
  CHECK(CALL(nvnMemoryPoolInitialize,&owned,&builder));
  vk_pool_requirement crossing[]={
    pool_window_need(storage+2*window+32,16,1,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,0),
    pool_window_need(storage+window-4,8,VK_POOL_REQUIRE_INDEX,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,0)};
  initialized=vk_pool_initialized_bytes();
  CHECK(vk_pool_reserve_ranges(crossing,2,window,&result)==VK_POOL_RESERVE_OK);
  CHECK(result.added_spans==1&&result.added_buffer_bytes==pool_bytes&&!vk_test_pool_reserve_metadata((uintptr_t)storage));
  CHECK(vk_pool_initialized_bytes()==initialized);
  CHECK(vk_pool_span_query(crossing[1].address,crossing[1].size,&query)==VK_POOL_SPAN_SINGLE&&query.local_offset==window-4);
  mtl_sub_begin();pool_window_read(storage+window-4,8,pixels[0]);vk_internal_wait(vk_internal_commit());
  CHECK(!memcmp(pixels[0],storage+window-4,8));CALL(nvnMemoryPoolFinalize,&owned);
  CALL(nvnMemoryPoolBuilderSetStorage,&builder,storage,3*window+4096);
  CHECK(CALL(nvnMemoryPoolInitialize,&owned,&builder));
  vk_pool_requirement tail=pool_window_need(storage+3*window+4096-16,16,7,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0);
  CHECK(vk_pool_reserve_ranges(&tail,1,window,&result)==VK_POOL_RESERVE_OK);
  CHECK(result.added_spans==1&&result.added_buffer_bytes==4096);
  CHECK(vk_pool_span_query(tail.address,tail.size,&query)==VK_POOL_SPAN_SINGLE);
  CHECK(query.logical_begin==3*window&&query.logical_end==3*window+4096&&query.local_offset==4080);
  mtl_sub_begin();pool_window_read((uint8_t *)(uintptr_t)tail.address,16,pixels[0]);vk_internal_wait(vk_internal_commit());
  CHECK(!memcmp(pixels[0],(const void *)(uintptr_t)tail.address,16));
  CALL(nvnMemoryPoolFinalize,&owned);free(storage);vk_pool_sync_reference(reference);
  LOG("[vulkan pool window reservation fixture] small allocated extents, full mixed-role ranges, exact offsets/prefix/tails, whole fallback, invalid/OOM atomicity, retained async readers and quiescent GPU migration passed");
}
#else
static void pool_window_reservation_test(void) {
  LOG("[vulkan pool window reservation fixture] test-only fault injection unavailable in standalone target");
}
#endif
