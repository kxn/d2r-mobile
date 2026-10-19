#ifdef DM_VULKAN_TEST_EMBED
extern void dm_gfx_trim_memory_request(void),dm_gfx_suspend(int);
extern int nvn_test_idle_trim_pending(void),nvn_test_idle_trim_supported(void);
extern void vk_test_idle_temporaries(uint64_t *,unsigned *);
extern unsigned vk_test_idle_driver_arenas(void);
extern void vk_test_idle_driver_store(const uint8_t *,size_t,vk_buffer *,uint64_t *);
static void idle_cache_trim_test(void) {
  // The actual HLE control queue consumes an early request, not the UI thread.
  dm_gfx_trim_memory_request();dm_gfx_trim_memory_request();
  CHECK(nvn_test_idle_trim_pending());
  CALL(nvnQueueSubmitCommands,&queue,0,NULL);CALL(nvnQueueFinish,&queue);
  int supported=nvn_test_idle_trim_supported();
  dm_gfx_suspend(1);CHECK(nvn_test_idle_trim_pending()==!supported);dm_gfx_suspend(0);
  uint64_t idle;unsigned count;vk_test_idle_temporaries(&idle,&count);
  CHECK(!idle && !count && !vk_test_idle_driver_arenas());
  if(supported){CHECK(mtl_trim_idle_resources());CHECK(mtl_trim_idle_resources());}
  vk_buffer output;vk_buffer_create(&output,256,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  uint8_t expected[256];for(unsigned k=0;k<sizeof expected;k++)expected[k]=(uint8_t)(k*19+7);
  for(unsigned round=0;round<2;round++) {
    mtl_sub_begin();vk_buffer *temp=vk_temp_buffer(sizeof expected);
    memcpy(temp->map,expected,sizeof expected);
    vk_buffer arena;uint64_t arena_offset;
    vk_test_idle_driver_store(expected,sizeof expected,&arena,&arena_offset);
    // An active recording is borrowed. Neither list may be reclaimed here.
    CHECK(!mtl_trim_idle_resources());CHECK(!memcmp(temp->map,expected,sizeof expected));
    CHECK(!memcmp(arena.map+arena_offset,expected,sizeof expected));
    VkBufferMemoryBarrier pre={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,
      .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
      .buffer=temp->handle,.offset=0,.size=sizeof expected};
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,1,&pre,0,NULL);
    VkBufferCopy copy={.size=sizeof expected};vkCmdCopyBuffer(V.cmd,temp->handle,output.handle,1,&copy);
    VkBufferMemoryBarrier post=pre;post.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    post.dstAccessMask=VK_ACCESS_HOST_READ_BIT;post.buffer=output.handle;
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&post,0,NULL);
    int gated=vk_async_submissions()&&V.timeline_semaphore;
    VkSemaphore gate=VK_NULL_HANDLE;
    if(gated) {gate=pool_copy_gate();vk_command_reset_test_gate(1);}
    uint64_t serial=vk_internal_commit();
    if(gated) {
      CHECK(vk_completed_serial()<serial);CHECK(mtl_trim_idle_resources());
      // GPU is still blocked: pending temporary+arena must retain exact bytes.
      CHECK(!memcmp(temp->map,expected,sizeof expected));
      CHECK(!memcmp(arena.map+arena_offset,expected,sizeof expected));
      pool_copy_signal(&gate);CHECK(vk_command_reset_test_wait_blocked(serial)>=serial);
      uint64_t done,ready;unsigned sets;vk_command_reset_test_snapshot(&done,&ready,&sets);
      CHECK(done>=serial && ready<serial);CHECK(mtl_trim_idle_resources());
      vk_test_idle_temporaries(&idle,&count);CHECK(!idle&&!count);
      CHECK(!vk_test_idle_driver_arenas());
      // Completion effects alone must not expose these for recycling.
      CHECK(!memcmp(temp->map,expected,sizeof expected));
      CHECK(!memcmp(arena.map+arena_offset,expected,sizeof expected));
      vk_command_reset_test_gate(0);
    }
    mtl_finish();CHECK(!memcmp(output.map,expected,sizeof expected));
    if(gated)vkDestroySemaphore(V.device,gate,NULL);
    // First normal begin really recycles reset resources into idle lists.
    mtl_sub_begin();vk_internal_wait(vk_internal_commit());mtl_finish();
    vk_test_idle_temporaries(&idle,&count);CHECK(idle>=sizeof expected&&count);
    unsigned arena_count=vk_test_idle_driver_arenas();CHECK(arena_count>0);
    uint32_t blocks0,allocs0,blocks1,allocs1;uint64_t bytes0,bytes1;
    vk_memory_stats(&blocks0,&allocs0,&bytes0);
    dm_gfx_trim_memory_request();
    if(!round && supported) {
      CALL(nvnQueueSubmitCommands,&queue,0,NULL);CALL(nvnQueueFinish,&queue);
      CHECK(!nvn_test_idle_trim_pending());
    } else {
      dm_gfx_suspend(1);CHECK(nvn_test_idle_trim_pending()==!supported);dm_gfx_suspend(0);
    }
    vk_memory_stats(&blocks1,&allocs1,&bytes1);
    if(supported) {CHECK(allocs1<allocs0 && bytes1<bytes0);
      CHECK(allocs0-allocs1>=count+arena_count);CHECK(bytes0-bytes1>=idle);}
    else CHECK(allocs1==allocs0 && bytes1==bytes0); // no unsafe sync fallback trim
    LOG("[idle trim fixture] VMA allocations %u->%u logical bytes %llu->%llu blocks %u->%u",allocs0,allocs1,(unsigned long long)bytes0,(unsigned long long)bytes1,blocks0,blocks1);
    vk_test_idle_temporaries(&idle,&count);
    if(supported) {CHECK(!idle&&!count);CHECK(!vk_test_idle_driver_arenas());}
    else {CHECK(idle>=sizeof expected&&count);CHECK(vk_test_idle_driver_arenas()>0);}
    CHECK(!memcmp(output.map,expected,sizeof expected));
    for(unsigned k=0;k<sizeof expected;k++)expected[k]^=0x5a;
  }
  vk_buffer_destroy(&output);CHECK(vk_validation_error_count()==0);
}
#else
static void idle_cache_trim_test(void) {}
#endif
