#ifdef DM_VULKAN_TEST_EMBED
extern void vk_test_temporary_age_policy(int);
static void temporary_age_copy(vk_buffer *temp,vk_buffer *output,const uint8_t *expected) {
  memcpy(temp->map,expected,256);
  VkBufferMemoryBarrier pre={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,
    .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
    .buffer=temp->handle,.offset=0,.size=256};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,1,&pre,0,NULL);
  VkBufferCopy copy={.size=256};vkCmdCopyBuffer(V.cmd,temp->handle,output->handle,1,&copy);
  pre.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;pre.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
  pre.buffer=output->handle;
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&pre,0,NULL);
}
static void temporary_cache_age_test(void) {
  mtl_init();mtl_finish();CHECK(mtl_trim_idle_resources());
  vk_test_temporary_age_policy(1); // Same policy, small limits and real queue serials.
  vk_buffer output;vk_buffer_create(&output,256,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  uint8_t expected[256];for(unsigned k=0;k<256;k++)expected[k]=(uint8_t)(k*23+11);
  mtl_sub_begin();temporary_age_copy(vk_temp_buffer(8192),&output,expected);
  vk_internal_wait(vk_internal_commit());mtl_finish();CHECK(!memcmp(output.map,expected,256));
  VkBuffer hot=VK_NULL_HANDLE;
  uint64_t initial_bytes=0;unsigned initial_allocs=0;
  for(unsigned round=0;round<8;round++) {
    mtl_sub_begin();vk_buffer *temp=vk_temp_buffer(256);
    if(round)CHECK(temp->handle==hot);else hot=temp->handle;
    expected[round]^=0x5a;temporary_age_copy(temp,&output,expected);
    vk_internal_wait(vk_internal_commit());mtl_finish();CHECK(!memcmp(output.map,expected,256));
    if(!round) {uint32_t blocks;vk_memory_stats(&blocks,&initial_allocs,&initial_bytes);}
  }
  // Trigger the ordinary reclaim boundary, retaining the hot allocation.
  mtl_sub_begin();vk_buffer *temp=vk_temp_buffer(256);CHECK(temp->handle==hot);
  uint64_t idle;unsigned count;vk_test_idle_temporaries(&idle,&count);CHECK(!idle&&!count);
  uint32_t blocks,allocs;uint64_t allocation_bytes;
  vk_memory_stats(&blocks,&allocs,&allocation_bytes);
  CHECK(allocs<initial_allocs&&allocation_bytes+8192<=initial_bytes);
  temporary_age_copy(temp,&output,expected);vk_internal_wait(vk_internal_commit());mtl_finish();
  CHECK(!memcmp(output.map,expected,256));CHECK(mtl_trim_idle_resources());
  if(vk_async_submissions()&&V.timeline_semaphore) {
    // A cold reusable allocation can age out while a different allocation
    // remains blocked on the GPU, and later while reset is deliberately held.
    mtl_sub_begin();vk_temp_buffer(8192);
    vk_internal_wait(vk_internal_commit());mtl_finish();
    mtl_sub_begin();vk_buffer *pending=vk_temp_buffer(16384);
    temporary_age_copy(pending,&output,expected);
    VkSemaphore gate=pool_copy_gate();vk_command_reset_test_gate(1);
    uint64_t serial=vk_internal_commit();
    for(unsigned round=0;round<5;round++) {mtl_sub_begin();vk_internal_commit();}
    CHECK(vk_completed_serial()<serial);
    CHECK(mtl_trim_idle_resources());vk_test_idle_temporaries(&idle,&count);
    CHECK(!idle&&!count);CHECK(!memcmp(pending->map,expected,256));
    pool_copy_signal(&gate);CHECK(vk_command_reset_test_wait_blocked(serial)>=serial);
    CHECK(mtl_trim_idle_resources());vk_test_idle_temporaries(&idle,&count);
    CHECK(!idle&&!count);CHECK(!memcmp(pending->map,expected,256));
    vk_command_reset_test_gate(0);mtl_finish();CHECK(!memcmp(output.map,expected,256));
    vkDestroySemaphore(V.device,gate,NULL);CHECK(mtl_trim_idle_resources());
  }
  vk_buffer_destroy(&output);vk_test_temporary_age_policy(0);
  CHECK(vk_validation_error_count()==0);
}
#else
static void temporary_cache_age_test(void) {}
#endif
