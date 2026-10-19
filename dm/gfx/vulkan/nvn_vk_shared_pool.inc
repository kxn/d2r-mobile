// Fixed startup mode, whole-pool lazy import. No borrowed-window admission,
// mirror demotion or legacy content proof is used by a shared Pool.
static VkResult shared_buffer_create(void *host_pointer,uint64_t bytes,vk_buffer *fresh) {
  uint64_t address=(uintptr_t)host_pointer;
  VkExternalMemoryBufferCreateInfo external={.sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
    .handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
  VkBufferCreateInfo create={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.pNext=&external,.size=bytes,
    .usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT|VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT|VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT};
  VkPhysicalDeviceExternalBufferInfo info={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO,
    .usage=create.usage,.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
  VkExternalBufferProperties props={.sType=VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES};
  vkGetPhysicalDeviceExternalBufferProperties(V.physical,&info,&props);
  VkExternalMemoryFeatureFlags external_features=props.externalMemoryProperties.externalMemoryFeatures;
  VkResult result=(external_features&VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) &&
    !(external_features&VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT)?VK_SUCCESS:VK_ERROR_INVALID_EXTERNAL_HANDLE;
  if(result==VK_SUCCESS)result=vkCreateBuffer(V.device,&create,NULL,&fresh->handle);
  if(result==VK_SUCCESS) {
    VkMemoryDedicatedRequirements dedicated={.sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
    VkMemoryRequirements2 requirements={.sType=VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,.pNext=&dedicated};
    VkBufferMemoryRequirementsInfo2 buffer_info={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,.buffer=fresh->handle};
    vkGetBufferMemoryRequirements2(V.device,&buffer_info,&requirements);
    VkMemoryRequirements needs=requirements.memoryRequirements;
    VkBool32 effective_requires=dedicated.requiresDedicatedAllocation;
    if(effective_requires)result=VK_ERROR_INVALID_EXTERNAL_HANDLE;
    VkMemoryHostPointerPropertiesEXT host={.sType=VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT};
    if(result==VK_SUCCESS)result=vkGetMemoryHostPointerPropertiesEXT(V.device,VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,host_pointer,&host);
    uint32_t type=UINT32_MAX;uint64_t atom=V.props.limits.nonCoherentAtomSize;
    for(unsigned pass=0;result==VK_SUCCESS && pass<2 && type==UINT32_MAX;pass++)
      for(uint32_t k=0;k<V.mem.memoryTypeCount;k++) {
        VkMemoryPropertyFlags flags=V.mem.memoryTypes[k].propertyFlags;
        VkMemoryPropertyFlags required=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        if((host.memoryTypeBits&needs.memoryTypeBits&(1u<<k)) && (flags&required)==required &&
            ((flags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) || (pass && atom && !(atom&(atom-1)) &&
              !(address%atom) && !(bytes%atom)))) {type=k;break;}
      }
    if(result==VK_SUCCESS && (type==UINT32_MAX || needs.size>bytes))result=VK_ERROR_INVALID_EXTERNAL_HANDLE;
    if(result==VK_SUCCESS) {
      VkImportMemoryHostPointerInfoEXT import={.sType=VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
        .handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,.pHostPointer=host_pointer};
      VkMemoryAllocateInfo allocation={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.pNext=&import,
        .allocationSize=bytes,.memoryTypeIndex=type};
      result=vkAllocateMemory(V.device,&allocation,NULL,&fresh->memory);
      if(result==VK_SUCCESS)fresh->shared_import=1;
      if(result==VK_SUCCESS)result=vkBindBufferMemory(V.device,fresh->handle,fresh->memory,0);
      if(result==VK_SUCCESS) {
        void *mapped=NULL;result=vkMapMemory(V.device,fresh->memory,0,bytes,0,&mapped);
        if(result==VK_SUCCESS){fresh->shared_import=1;if(!mapped)result=VK_ERROR_MEMORY_MAP_FAILED;}
      }
      fresh->properties=V.mem.memoryTypes[type].propertyFlags;
    }
  }
  if(result==VK_SUCCESS) {fresh->size=bytes;fresh->map=host_pointer;}
  return result;
}

static void shared_cache(vk_buffer *b,uint64_t offset,uint64_t bytes,int invalidate) {
  if(!b->shared_import || !bytes || offset>b->size || bytes>b->size-offset)
    die("Shared cache range/lifetime violation");
  if(b->properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)return;
  uint64_t atom=V.props.limits.nonCoherentAtomSize,end=offset+bytes;
  if(!atom || (atom&(atom-1)) || end>UINT64_MAX-(atom-1))die("Shared cache atom");
  offset&=~(atom-1);end=(end+atom-1)&~(atom-1);
  if(end>b->size)die("Shared cache atom crosses allocation");
  VkMappedMemoryRange range={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
    .memory=b->memory,.offset=offset,.size=end-offset};
  if(invalidate)VK_CHECK(vkInvalidateMappedMemoryRanges(V.device,1,&range));
  else VK_CHECK(vkFlushMappedMemoryRanges(V.device,1,&range));
}
static void shared_pool_materialize(pool *p) {
  if(p->shared_closing)die("Shared Pool address after closing");
  if(p->backings)return;
  vk_buffer b={0};VkResult result=shared_buffer_create(p->guest,p->size,&b);
  if(result!=VK_SUCCESS) {vk_buffer_destroy(&b);die("Shared whole Pool import failed: %d; no online legacy fallback",result);}
  pool_materialize_metadata(p);p->whole.buffer=b;p->whole.begin=0;p->whole.end=p->size;
  if(p->backing_generation==UINT64_MAX)die("Shared generation exhausted");
  p->whole.generation=++p->backing_generation;p->backings=&p->whole;
}
int vk_pool_shared_mode(void) {mtl_init();return shared_active;}
static void shared_device_probe(void) {
  if(!shared_requested)return;
  extern int dm_gfx_shared_owner_ready(void);
  if(!dm_gfx_shared_owner_ready()) {LOG("[shared pool] startup legacy: dedicated NVN owner unavailable");return;}
  if(V.options.pool_window_bytes || V.options.pool_mirror_reference || V.options.pool_defer_publication)
    die("Fixed shared mode conflicts with legacy window/mirror/defer options");
  if(!shared_extension || !vkGetMemoryHostPointerPropertiesEXT) {LOG("[shared pool] startup legacy: external host import unavailable");return;}
  VkPhysicalDeviceExternalMemoryHostPropertiesEXT host={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT};
  VkPhysicalDeviceProperties2 props={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&host};
  vkGetPhysicalDeviceProperties2(V.physical,&props);shared_alignment=host.minImportedHostPointerAlignment;
  if(shared_alignment<4096)shared_alignment=4096;
  if(!shared_alignment || (shared_alignment&(shared_alignment-1)) || shared_alignment>(1u<<20)) {
    LOG("[shared pool] startup legacy: unsupported import alignment");return;
  }
  // Actual bytes both ways before any NVN Pool exists. This validates only
  // the small allocation/type/API path, not all future addresses or residency.
  size_t bytes=shared_alignment*2;uint8_t *memory=NULL;
  if(posix_memalign((void **)&memory,shared_alignment,bytes))return;
  memset(memory,0x31,bytes);vk_buffer b={0};VkResult status=shared_buffer_create(memory,bytes,&b);
  if(status!=VK_SUCCESS) {vk_buffer_destroy(&b);free(memory);LOG("[shared pool] startup legacy: probe import=%d",status);return;}
  VkCommandPool pool_handle;VkCommandBuffer cmd;VkFence fence;
  VkCommandPoolCreateInfo pc={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=V.queue_family};
  VK_CHECK(vkCreateCommandPool(V.device,&pc,NULL,&pool_handle));
  VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool_handle,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
  VK_CHECK(vkAllocateCommandBuffers(V.device,&ca,&cmd));
  VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};VK_CHECK(vkBeginCommandBuffer(cmd,&begin));
  shared_cache(&b,0,bytes,0);
  VkBufferCopy copy={.srcOffset=0,.dstOffset=shared_alignment,.size=64};vkCmdCopyBuffer(cmd,b.handle,b.handle,1,&copy);
  vkCmdFillBuffer(cmd,b.handle,shared_alignment+64,64,0x718293a4);
  VkMemoryBarrier host_read={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
  vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host_read,0,NULL,0,NULL);
  VK_CHECK(vkEndCommandBuffer(cmd));VkFenceCreateInfo fc={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VK_CHECK(vkCreateFence(V.device,&fc,NULL,&fence));
  VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};
  VK_CHECK(vkQueueSubmit(V.queue,1,&submit,fence));VK_CHECK(vkWaitForFences(V.device,1,&fence,VK_TRUE,UINT64_MAX));
  shared_cache(&b,shared_alignment,128,1);int valid=1;
  for(unsigned k=0;k<64;k++)if(memory[shared_alignment+k]!=0x31)valid=0;
  for(unsigned k=0;k<64;k+=4) {uint32_t value;memcpy(&value,memory+shared_alignment+64+k,4);if(value!=0x718293a4)valid=0;}
  vkDestroyFence(V.device,fence,NULL);vkDestroyCommandPool(V.device,pool_handle,NULL);vk_buffer_destroy(&b);free(memory);
  shared_active=valid;LOG("[shared pool] immutable startup %s; lazy whole Pool, no online mirror fallback",valid?"shared":"legacy: byte probe mismatch");
}

void vk_pool_shared_publish(uint64_t address,uint64_t bytes,int invalidate) {
  if(!shared_active || !bytes)die("Shared publish outside fixed mode");
  pthread_mutex_lock(&V.resources);pool *p=pool_find(address,bytes);
  if(!p || !p->shared || p->shared_closing)die("Shared publish Pool lifetime/range");
  uint64_t offset=address-(uintptr_t)p->guest,atom=V.props.limits.nonCoherentAtomSize;
  if(!atom || (atom&(atom-1)) || offset+bytes>UINT64_MAX-(atom-1))die("Shared publish atom extent");
  uint64_t first=offset&~(atom-1),end=(offset+bytes+atom-1)&~(atom-1);
  // Explicit cache API never secretly waits an overlapping GPU. Existing
  // consumer bookkeeping is page-conservative, stronger than the atom bound.
  if(pending_gpu_access(p,first,end-first))die("Shared CPU cache publication overlaps pending GPU cacheline/page");
  if(p->backings)shared_cache(&p->whole.buffer,offset,bytes,invalidate);
  pthread_mutex_unlock(&V.resources);
}

#ifdef DM_VULKAN_TEST_EMBED
int vk_test_fixed_shared_pool(uint64_t address,int *ready,int *shadow,uint32_t *properties) {
  pthread_mutex_lock(&V.resources);pool *p=pool_find(address,1);
  int valid=p && p->shared;if(valid) {*ready=p->backings!=NULL;*shadow=p->shadow!=NULL;
    *properties=p->whole.buffer.properties;}
  pthread_mutex_unlock(&V.resources);return valid;
}
#endif
