#ifdef DM_VULKAN_TEST_EMBED
extern void dm_gfx_trim_memory_request(void),dm_gfx_suspend(int);
extern int nvn_test_idle_trim_pending(void),nvn_test_idle_trim_supported(void);
extern int dm_gfx_vma_dump_request(void),nvn_test_vma_dump_pending(void);
extern void vk_test_idle_temporaries(uint64_t *,unsigned *);
extern unsigned vk_test_idle_driver_arenas(void);
extern void vk_test_idle_driver_store(const uint8_t *,size_t,vk_buffer *,uint64_t *);
// Observe real Vulkan pool objects, not a backend cache getter. Handles may
// be reused after destruction, so records have allocation-instance identity.
static PFN_vkCreateDescriptorPool idle_trim_create_pool;
static PFN_vkDestroyDescriptorPool idle_trim_destroy_pool;
static PFN_vkAllocateDescriptorSets idle_trim_allocate_sets;
static struct { VkDescriptorPool pool;int destroyed; } idle_trim_pools[256];
static unsigned idle_trim_pool_count,idle_trim_pool_destroys,idle_trim_last_pool;
static VKAPI_ATTR VkResult VKAPI_CALL idle_trim_create(VkDevice d,const VkDescriptorPoolCreateInfo *ci,
    const VkAllocationCallbacks *a,VkDescriptorPool *pool) {
  VkResult result=idle_trim_create_pool(d,ci,a,pool);
  if(result==VK_SUCCESS) {
    CHECK(idle_trim_pool_count<256);
    idle_trim_pools[idle_trim_pool_count++]=(typeof(*idle_trim_pools)){.pool=*pool};
  }
  return result;
}
static VKAPI_ATTR void VKAPI_CALL idle_trim_destroy(VkDevice d,VkDescriptorPool pool,
    const VkAllocationCallbacks *a) {
  idle_trim_pool_destroys++;
  for(unsigned i=idle_trim_pool_count;i;i--)if(idle_trim_pools[i-1].pool==pool && !idle_trim_pools[i-1].destroyed) {
    idle_trim_pools[i-1].destroyed=1;break;
  }
  idle_trim_destroy_pool(d,pool,a);
}
static VKAPI_ATTR VkResult VKAPI_CALL idle_trim_allocate(VkDevice d,const VkDescriptorSetAllocateInfo *ai,
    VkDescriptorSet *sets) {
  VkResult result=idle_trim_allocate_sets(d,ai,sets);
  if(result==VK_SUCCESS) {
    idle_trim_last_pool=0;
    for(unsigned i=idle_trim_pool_count;i;i--)if(idle_trim_pools[i-1].pool==ai->descriptorPool && !idle_trim_pools[i-1].destroyed) {
      idle_trim_last_pool=i;break;
    }
    // A preexisting context may allocate from a pool created before wrapping.
    // Allocation itself supplies the real pool handle; never infer ownership
    // from the value of a descriptor-set handle.
    if(!idle_trim_last_pool) {
      CHECK(idle_trim_pool_count<256);
      idle_trim_pools[idle_trim_pool_count++]=(typeof(*idle_trim_pools)){.pool=ai->descriptorPool};
      idle_trim_last_pool=idle_trim_pool_count;
    }
  }
  return result;
}
static void idle_vma_completed(void *context) {*(unsigned *)context=1;}
static void idle_cache_trim_test(void) {
  const char *dump_root=getenv("DM_TEMP");CHECK(dump_root&&dump_root[0]=='/');
  char dump_path[1024],part_path[1056],saved_path[1056];
  CHECK(snprintf(dump_path,sizeof dump_path,"%s/vulkan-vma.json",dump_root)<(int)sizeof dump_path);
  snprintf(part_path,sizeof part_path,"%s.part",dump_path);snprintf(saved_path,sizeof saved_path,"%s.success",dump_path);
  CHECK(access(dump_path,F_OK)!=0 && access(part_path,F_OK)!=0);

  // The actual HLE control queue consumes an early request, not the UI thread.
  dm_gfx_trim_memory_request();dm_gfx_trim_memory_request();
  CHECK(nvn_test_idle_trim_pending());
  CALL(nvnQueueSubmitCommands,&queue,0,NULL);CALL(nvnQueueFinish,&queue);
  int supported=nvn_test_idle_trim_supported();
  dm_gfx_suspend(1);CHECK(nvn_test_idle_trim_pending()==!supported);dm_gfx_suspend(0);
  uint64_t idle;unsigned count;vk_test_idle_temporaries(&idle,&count);
  CHECK(!idle && !count && !vk_test_idle_driver_arenas());
  if(supported){CHECK(mtl_trim_idle_resources());CHECK(mtl_trim_idle_resources());}
  // Use the original compute binding path to populate content history and
  // retained sets. Round2 changes data/offsets after pressure-driven rebuild.
  vk_state saved=S;uint8_t *storage;
  CHECK(!posix_memalign((void **)&storage,65536,65536));memset(storage,0,65536);
  NVNmemoryPoolBuilder builder;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&builder);CALL(nvnMemoryPoolBuilderSetDevice,&builder,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&builder,storage,65536);
  CALL(nvnMemoryPoolBuilderSetFlags,&builder,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&builder));
  uint8_t control[0x880];memset(control,2,sizeof control);NVNprogram program;
  NVNshaderData shader={.control=control};CHECK(CALL(nvnProgramInitialize,&program,&device));
  CHECK(CALL(nvnProgramSetShaders,&program,1,&shader));
  idle_trim_create_pool=vkCreateDescriptorPool;idle_trim_destroy_pool=vkDestroyDescriptorPool;
  idle_trim_allocate_sets=vkAllocateDescriptorSets;
  idle_trim_pool_count=idle_trim_pool_destroys=idle_trim_last_pool=0;
  vkCreateDescriptorPool=idle_trim_create;vkDestroyDescriptorPool=idle_trim_destroy;
  vkAllocateDescriptorSets=idle_trim_allocate;
  vk_buffer output;vk_buffer_create(&output,256,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  uint8_t expected[256];for(unsigned k=0;k<sizeof expected;k++)expected[k]=(uint8_t)(k*19+7);
  for(unsigned round=0;round<2;round++) {
    uint32_t *input=(void *)(storage+4096+round*512),*result=(void *)(storage+8192+round*512);
    dm_guest_write_prepare(storage,65536);
    for(unsigned i=0;i<64;i++){input[i]=round*1000+i;result[i]=0;}
    mtl_sub_begin();
    S=(vk_state){.program=(const prog_t *)&program};
    S.ssbo[5][0]=(p_bind_buf){.stage=5,.index=0,.addr=(uintptr_t)input,.size=256};
    S.ssbo[5][1]=(p_bind_buf){.stage=5,.index=1,.addr=(uintptr_t)result,.size=256};
    idle_trim_last_pool=0;p_dispatch dispatch={1,1,1,0};vk_dispatch(&dispatch);vk_dispatch(&dispatch);
    unsigned descriptor_pool=idle_trim_last_pool;
    if(supported)CHECK(descriptor_pool && !idle_trim_pools[descriptor_pool-1].destroyed);
    uint64_t hh,hm,history_bytes;int history_reference;
    vk_descriptor_history_stats(&hh,&hm,&history_bytes,&history_reference);CHECK(history_bytes>0);
    vk_buffer *temp=vk_temp_buffer(sizeof expected);
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
    unsigned vma_cpu_effect_complete=0;
    if(round && supported) {
      vk_completion(idle_vma_completed,&vma_cpu_effect_complete);
      CHECK(dm_gfx_vma_dump_request());CHECK(!dm_gfx_vma_dump_request());
      CHECK(nvn_test_vma_dump_pending());CHECK(access(dump_path,F_OK)!=0);
    }
    uint64_t serial=vk_internal_commit();
    if(gated) {
      CHECK(vk_completed_serial()<serial);CHECK(mtl_trim_idle_resources());
      if(round && supported) {CHECK(!mtl_memory_dump());CHECK(nvn_test_vma_dump_pending());CHECK(access(dump_path,F_OK)!=0);}
      // GPU is still blocked: descriptor pools and data must remain live.
      if(supported)CHECK(!idle_trim_pools[descriptor_pool-1].destroyed);
      CHECK(!memcmp(temp->map,expected,sizeof expected));
      CHECK(!memcmp(arena.map+arena_offset,expected,sizeof expected));
      pool_copy_signal(&gate);CHECK(vk_command_reset_test_wait_blocked(serial)>=serial);
      uint64_t done,ready;unsigned sets;vk_command_reset_test_snapshot(&done,&ready,&sets);
      CHECK(done>=serial && ready<serial);CHECK(mtl_trim_idle_resources());
      if(round && supported) {CHECK(!mtl_memory_dump());CHECK(nvn_test_vma_dump_pending());CHECK(access(dump_path,F_OK)!=0);CHECK(vma_cpu_effect_complete);}
      if(supported)CHECK(!idle_trim_pools[descriptor_pool-1].destroyed);
      vk_test_idle_temporaries(&idle,&count);CHECK(!idle&&!count);
      CHECK(!vk_test_idle_driver_arenas());
      // Completion effects alone must not expose these for recycling.
      CHECK(!memcmp(temp->map,expected,sizeof expected));
      CHECK(!memcmp(arena.map+arena_offset,expected,sizeof expected));
      vk_command_reset_test_gate(0);
    }
    mtl_finish();CHECK(!memcmp(output.map,expected,sizeof expected));
    for(unsigned i=0;i<64;i++)CHECK(result[i]==(round*1000+i)*3+7);
    if(gated)vkDestroySemaphore(V.device,gate,NULL);
    // First normal begin really recycles reset resources into idle lists.
    mtl_sub_begin();vk_internal_wait(vk_internal_commit());mtl_finish();
    vk_test_idle_temporaries(&idle,&count);CHECK(idle>=sizeof expected&&count);
    unsigned arena_count=vk_test_idle_driver_arenas();CHECK(arena_count>0);
    uint32_t blocks0,allocs0,blocks1,allocs1;uint64_t bytes0,bytes1;
    vk_memory_stats(&blocks0,&allocs0,&bytes0);
    unsigned destroys_before=idle_trim_pool_destroys;
    dm_gfx_trim_memory_request();
    if(!round && supported) {
      CALL(nvnQueueSubmitCommands,&queue,0,NULL);CALL(nvnQueueFinish,&queue);
      CHECK(!nvn_test_idle_trim_pending());
    } else {
      dm_gfx_suspend(1);CHECK(nvn_test_idle_trim_pending()==!supported);
      if(round && supported) {
        CHECK(!nvn_test_vma_dump_pending());CHECK(access(dump_path,F_OK)==0);CHECK(vma_cpu_effect_complete);
        LOG("[VMA dump fixture] pre-trim allocation_count=%u allocation_bytes=%llu",allocs0,(unsigned long long)bytes0);
      }
      dm_gfx_suspend(0);
    }
    vk_memory_stats(&blocks1,&allocs1,&bytes1);
    if(supported) {
      CHECK(idle_trim_pools[descriptor_pool-1].destroyed);
      CHECK(idle_trim_pool_destroys>destroys_before);
      vk_descriptor_history_stats(&hh,&hm,&history_bytes,&history_reference);CHECK(history_bytes==0);
    } else CHECK(idle_trim_pool_destroys==destroys_before); // inline HLE retains request
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
  if(supported) {
    // Preserve the untouched builtin JSON for the host's standard JSON parser.
    CHECK(!rename(dump_path,saved_path));
    CHECK(!mkdir(part_path,0700)); // fixed-output failure, never retry implicitly
    CHECK(dm_gfx_vma_dump_request());dm_gfx_trim_memory_request();
    dm_gfx_suspend(1);CHECK(!nvn_test_vma_dump_pending());CHECK(!nvn_test_idle_trim_pending());dm_gfx_suspend(0);
    CHECK(access(dump_path,F_OK)!=0);CHECK(!rmdir(part_path));
    dm_gfx_suspend(1);dm_gfx_suspend(0);CHECK(access(dump_path,F_OK)!=0);
    LOG("[VMA dump fixture] existing post-drain owner, no pre-pause dump, failure consumed without retry; original trim and pixels retained");
  }
  vkCreateDescriptorPool=idle_trim_create_pool;vkDestroyDescriptorPool=idle_trim_destroy_pool;
  vkAllocateDescriptorSets=idle_trim_allocate_sets;
  CALL(nvnProgramFinalize,&program);CALL(nvnMemoryPoolFinalize,&pool);free(storage);
  S=saved;vk_pipeline_state_changed();
  vk_buffer_destroy(&output);CHECK(vk_validation_error_count()==0);
}
#else
static void idle_vma_completed(void *context) {*(unsigned *)context=1;}
static void idle_cache_trim_test(void) {
  const char *dump_root=getenv("DM_TEMP");CHECK(dump_root&&dump_root[0]=='/');
  char dump_path[1024],part_path[1056],saved_path[1056];
  CHECK(snprintf(dump_path,sizeof dump_path,"%s/vulkan-vma.json",dump_root)<(int)sizeof dump_path);
  snprintf(part_path,sizeof part_path,"%s.part",dump_path);snprintf(saved_path,sizeof saved_path,"%s.success",dump_path);
  CHECK(access(dump_path,F_OK)!=0 && access(part_path,F_OK)!=0);
}
#endif
