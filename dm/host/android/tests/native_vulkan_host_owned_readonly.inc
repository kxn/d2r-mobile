// Separate owned-only READONLY import mode. The original standard oracle and
// its dirty-cycle failures are preserved. No NVN pool, guest or allocator hook.
static void host_owned_readonly_protected_negative(VkDeviceSize alignment) {
  size_t size=(size_t)alignment*2;
  void *allocation=mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
  CHECK(allocation!=MAP_FAILED);
  uint8_t *host=(void *)(((uintptr_t)allocation+alignment-1)&~(alignment-1));
  memset(host,0,(size_t)alignment);nvn_dirty_add_range(host,(size_t)alignment);
  nvn_dirty_protect(host,(size_t)alignment);
  uint64_t stamp=nvn_dirty_now(),faults,preps,faults2,preps2;
  nvn_dirty_stats(&faults,&preps);nvn_dirty_owned_exclusion token={0};
  CHECK(!nvn_dirty_owned_exclude(host,(size_t)alignment,&token));CHECK(!token.identity);
  nvn_dirty_stats(&faults2,&preps2);
  CHECK(stamp==nvn_dirty_now() && faults==faults2 && preps==preps2);
  // No allocation/import attempt is issued after rejection. Original tracker
  // destruction may unprotect only for final CPU allocation teardown, not retry.
  printf("{\"test_details\":\"owned-readonly-protected-negative\",\"rejected\":true,\"import_attempts\":0,\"retry_imports\":0,\"unprotect_for_retry\":false}\n");
  nvn_dirty_remove_range(host);CHECK(munmap(allocation,size)==0);
}
static void host_owned_readonly_round(host_import_context *context,VkDeviceSize alignment,
                                     unsigned round,PFN_vkGetMemoryHostPointerPropertiesEXT properties) {
  const size_t extent=(size_t)alignment*2,mapping_size=(size_t)alignment*3;
  void *mapping=mmap(NULL,mapping_size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
  CHECK(mapping!=MAP_FAILED);
  uint32_t *host=(void *)(((uintptr_t)mapping+alignment-1)&~(alignment-1));
  memset(host,0,extent);nvn_dirty_add_range(host,extent);
  nvn_dirty_owned_exclusion token={0};
  CHECK(nvn_dirty_owned_exclude(host,extent,&token));CHECK(nvn_dirty_change_epoch()==0);
  nvn_dirty_range_proof proof;CHECK(!nvn_dirty_range_observe(host,extent,&proof));
  VkMemoryHostPointerPropertiesEXT host_properties={.sType=VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT};
  CHECK(properties(context->device,VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,host,&host_properties)==VK_SUCCESS);
  CHECK(host_properties.memoryTypeBits!=0);
  VkExternalMemoryBufferCreateInfo external={.sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
    .handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
  VkBufferCreateInfo create={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.pNext=&external,
    .size=alignment,.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
  VkBuffer imported[2];uint32_t bits=host_properties.memoryTypeBits;
  for(unsigned i=0;i<2;i++) {
    CHECK(vkCreateBuffer(context->device,&create,NULL,&imported[i])==VK_SUCCESS);
    VkMemoryRequirements needs;vkGetBufferMemoryRequirements(context->device,imported[i],&needs);
    CHECK(needs.size<=alignment && alignment%needs.alignment==0);bits&=needs.memoryTypeBits;
  }
  VkImportMemoryHostPointerInfoEXT import={.sType=VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
    .handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,.pHostPointer=host};
  VkMemoryAllocateInfo allocate={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.pNext=&import,
    .allocationSize=extent,.memoryTypeIndex=host_import_memory_type(context->physical,bits,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT,VK_TRUE)};
  VkPhysicalDeviceMemoryProperties types;vkGetPhysicalDeviceMemoryProperties(context->physical,&types);
  VkMemoryPropertyFlags flags=types.memoryTypes[allocate.memoryTypeIndex].propertyFlags;
  context->imported_host_coherent=!!(flags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  CHECK(context->imported_host_coherent || alignment%context->noncoherent_atom==0);
  VkDeviceMemory memory;CHECK(vkAllocateMemory(context->device,&allocate,NULL,&memory)==VK_SUCCESS);
  for(unsigned i=0;i<2;i++)CHECK(vkBindBufferMemory(context->device,imported[i],memory,alignment*i)==VK_SUCCESS);
  void *mapped=NULL;CHECK(vkMapMemory(context->device,memory,0,extent,0,&mapped)==VK_SUCCESS);CHECK(mapped!=NULL);
  printf("{\"test_details\":\"owned-readonly-memory\",\"round\":%u,\"memory_type\":%u,\"flags\":%u,\"host_coherent\":%s,\"atom\":%llu,\"bytes\":%zu,\"GPU_write_to_import\":false}\n",
    round,allocate.memoryTypeIndex,flags,context->imported_host_coherent?"true":"false",
    (unsigned long long)context->noncoherent_atom,extent);

  create.pNext=NULL;create.size=extent;create.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  VkBuffer readback;CHECK(vkCreateBuffer(context->device,&create,NULL,&readback)==VK_SUCCESS);
  VkMemoryRequirements needs;vkGetBufferMemoryRequirements(context->device,readback,&needs);
  allocate.pNext=NULL;allocate.allocationSize=needs.size;
  allocate.memoryTypeIndex=host_import_memory_type(context->physical,needs.memoryTypeBits,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,VK_FALSE);
  VkDeviceMemory read_memory;CHECK(vkAllocateMemory(context->device,&allocate,NULL,&read_memory)==VK_SUCCESS);
  CHECK(vkBindBufferMemory(context->device,readback,read_memory,0)==VK_SUCCESS);
  uint8_t *received;CHECK(vkMapMemory(context->device,read_memory,0,extent,0,(void **)&received)==VK_SUCCESS);
  const size_t words=(size_t)alignment/sizeof(uint32_t);
  for(unsigned pass=0;pass<3;pass++) {
    uint64_t faults,preps,faults2,preps2;nvn_dirty_stats(&faults,&preps);
    nvn_dirty_protect(host,extent); // held token must keep both source pages RW
    CHECK(nvn_dirty_since(host,extent,UINT64_MAX));
    CHECK(nvn_dirty_change_epoch()==0 && !nvn_dirty_range_observe(host,extent,&proof));
    for(unsigned slot=0;slot<2;slot++) {
      if(pass!=1)dm_guest_write_prepare(host+slot*words,(size_t)alignment);
      for(size_t i=0;i<words;i++)host[slot*words+i]=host_import_pattern(round*3+pass,slot,i);
      printf("{\"test_details\":\"owned-readonly-publication\",\"round\":%u,\"pass\":%u,\"slot\":%u,\"offset\":%llu,\"size\":%llu,\"Flush_required\":%s}\n",
        round,pass,slot,(unsigned long long)alignment*slot,(unsigned long long)alignment,
        context->imported_host_coherent?"false":"true");
      host_import_cache_range(context,memory,alignment*slot,alignment,VK_FALSE);
      host_import_begin(context,slot);
      host_import_barrier(context->command[slot],VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT);
      if(!context->implicit_host_dependency)
        host_import_barrier(context->command[slot],VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_ACCESS_HOST_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
      VkBufferCopy copy={.dstOffset=alignment*slot,.size=alignment};
      vkCmdCopyBuffer(context->command[slot],imported[slot],readback,1,&copy);
      host_import_barrier(context->command[slot],VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT);
      host_import_submit(context,slot);
    }
    host_import_wait(context); // original two-submission timeline/fence helper
    nvn_dirty_stats(&faults2,&preps2);CHECK(faults2==faults && preps2==preps);
    // Byte oracle, not a hash or stale CPU mirror. Log first mismatch before
    // retaining the exact assertion; never continue a failed round.
    for(unsigned slot=0;slot<2;slot++)for(size_t byte=0;byte<(size_t)alignment;byte++) {
      uint32_t word=host_import_pattern(round*3+pass,slot,byte/4);
      uint8_t expected=(uint8_t)(word>>(8*(byte%4))),actual=received[(size_t)alignment*slot+byte];
      if(actual!=expected)LOG("[owned readonly mismatch] round=%u pass=%u slot=%u byte=%zu CPU=%u GPU=%u expected=%u",
        round,pass,slot,byte,((uint8_t *)host)[(size_t)alignment*slot+byte],actual,expected);
      CHECK(actual==expected);
    }
    printf("{\"test_details\":\"owned-readonly-cycle\",\"round\":%u,\"pass\":%u,\"write_path\":\"%s\",\"CPU_first\":%u,\"GPU_first\":%u,\"faults\":0,\"prepares\":0}\n",
      round,pass,pass==1?"direct-store":"original-write-prepare",((uint8_t *)host)[0],received[0]);
  }
  // All actual source accesses are complete before freeing buffer/memory and
  // releasing the exclusion; no device-idle/global wait is added.
  vkUnmapMemory(context->device,read_memory);
  vkDestroyBuffer(context->device,readback,NULL);vkFreeMemory(context->device,read_memory,NULL);
  for(unsigned i=0;i<2;i++)vkDestroyBuffer(context->device,imported[i],NULL);
  vkUnmapMemory(context->device,memory);vkFreeMemory(context->device,memory,NULL);
  CHECK(nvn_dirty_owned_release(&token));CHECK(nvn_dirty_change_epoch()!=0);
  uint64_t faults,preps,faults2,preps2;nvn_dirty_stats(&faults,&preps);
  uint64_t stamp=nvn_dirty_now();nvn_dirty_protect(host,extent);
  dm_guest_write_prepare(host,(size_t)alignment);host[0]=UINT32_C(0x37b219ab);
  CHECK(nvn_dirty_since(host,extent,stamp));nvn_dirty_stats(&faults2,&preps2);CHECK(preps2>preps);
  nvn_dirty_protect(host,extent);stamp=nvn_dirty_now();
  ((volatile uint8_t *)host)[extent-1]=0xa6;
  CHECK(nvn_dirty_since(host,extent,stamp));nvn_dirty_stats(&faults2,&preps2);CHECK(faults2>faults);
  CHECK(host[0]==UINT32_C(0x37b219ab) && ((uint8_t *)host)[extent-1]==0xa6);
  nvn_dirty_remove_range(host);CHECK(munmap(mapping,mapping_size)==0);
  printf("{\"test_details\":\"owned-readonly-retirement\",\"round\":%u,\"fences_completed\":true,\"import_freed_before_release\":true,\"original_prepare_restored\":true,\"original_fault_restored\":true}\n",round);
}
