// Startup-only alternative physical Buffer representation. Sparse virtual
// coverage is NOT guest-byte freshness, GPU access or native-image ownership.
#define SPARSE_POOL_USAGE (VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|\
  VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|\
  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT|\
  VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT|VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
#define SPARSE_POOL_FLAGS (VK_BUFFER_CREATE_SPARSE_BINDING_BIT|VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT)
static VkResult sparse_pool_buffer_create(uint64_t bytes,VkBuffer *buffer,VkMemoryRequirements *needs) {
  VkExternalMemoryBufferCreateInfo external={.sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
    .handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
  VkBufferCreateInfo ci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.pNext=&external,
    .flags=SPARSE_POOL_FLAGS,.size=bytes,.usage=SPARSE_POOL_USAGE};
  VkResult result=vkCreateBuffer(V.device,&ci,NULL,buffer);if(result!=VK_SUCCESS)return result;
  VkMemoryDedicatedRequirements dedicated={.sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
  VkMemoryRequirements2 r={.sType=VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,.pNext=&dedicated};
  VkBufferMemoryRequirementsInfo2 info={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,.buffer=*buffer};
  vkGetBufferMemoryRequirements2(V.device,&info,&r);*needs=r.memoryRequirements;
  if(dedicated.requiresDedicatedAllocation || !needs->alignment ||
      needs->alignment>shared_alignment || shared_alignment%needs->alignment ||
      bytes%needs->alignment || needs->size!=bytes)return VK_ERROR_INVALID_EXTERNAL_HANDLE;
  return VK_SUCCESS;
}
static int sparse_pool_startup_probe(void) {
  if(!sparse_queue_supported || !V.features.sparseBinding || !V.features.sparseResidencyBuffer ||
      !vkQueueBindSparse || !shared_alignment || !shared_window_bytes ||
      shared_window_bytes%shared_alignment || !V.props.limits.sparseAddressSpaceSize)return 0;
  VkPhysicalDeviceExternalBufferInfo info={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO,
    .flags=SPARSE_POOL_FLAGS,.usage=SPARSE_POOL_USAGE,.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
  VkExternalBufferProperties props={.sType=VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES};
  vkGetPhysicalDeviceExternalBufferProperties(V.physical,&info,&props);
  const VkExternalMemoryProperties *p=&props.externalMemoryProperties;
  if(!(p->externalMemoryFeatures&VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) ||
      p->externalMemoryFeatures&VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT ||
      !(p->compatibleHandleTypes&VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT))return 0;
  // Legal properties alone did not prove HOST import + sparse interoperability
  // on the host software ICD. Reuse the original memfd-view startup facility
  // and actual GPU byte contract, before any guest Pool is registered.
  uint64_t bytes=2*shared_alignment;dm_guest_heap_probe_views views={0};
  if(!dm_guest_heap_probe_views_create || !dm_guest_heap_probe_views_destroy ||
      !dm_guest_heap_probe_views_create(bytes,shared_alignment,&views))return 0;
  uint8_t *memory=views.guest;
  if(mprotect(memory,bytes,PROT_READ|PROT_WRITE)) {
    shared_probe_release(&views,memory,bytes);return 0;
  }
  memset(memory,0x2d,shared_alignment);memset(memory+shared_alignment,0x56,shared_alignment);
  if(mprotect(memory,bytes,PROT_READ)) {shared_probe_release(&views,memory,bytes);return 0;}
  VkBuffer test=VK_NULL_HANDLE;VkMemoryRequirements needs={0};vk_buffer child={0};
  VkResult result=sparse_pool_buffer_create(bytes,&test,&needs);
  if(result==VK_SUCCESS)result=shared_buffer_create(views.import_alias,bytes,&child);
  int valid=result==VK_SUCCESS && (child.properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) &&
    (needs.memoryTypeBits&(1u<<child.shared_memory_type));
  if(valid) {
    VkCommandPool commands;VkCommandBuffer cmd;VkFence fence;VkSemaphore ready;
    VkCommandPoolCreateInfo pc={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=V.queue_family};
    VK_CHECK(vkCreateCommandPool(V.device,&pc,NULL,&commands));
    VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool=commands,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VK_CHECK(vkAllocateCommandBuffers(V.device,&ca,&cmd));
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_CHECK(vkBeginCommandBuffer(cmd,&begin));
    VkBufferCopy copy={.srcOffset=0,.dstOffset=shared_alignment,.size=64};
    vkCmdCopyBuffer(cmd,test,test,1,&copy);vkCmdFillBuffer(cmd,test,shared_alignment+64,64,0x718293a4);
    VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
    VK_CHECK(vkEndCommandBuffer(cmd));
    VkSemaphoreCreateInfo sc={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};VK_CHECK(vkCreateSemaphore(V.device,&sc,NULL,&ready));
    VkFenceCreateInfo fc={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VK_CHECK(vkCreateFence(V.device,&fc,NULL,&fence));
    VkSparseMemoryBind bind={.resourceOffset=0,.size=bytes,.memory=child.memory};
    VkSparseBufferMemoryBindInfo bi={.buffer=test,.bindCount=1,.pBinds=&bind};
    VkBindSparseInfo map={.sType=VK_STRUCTURE_TYPE_BIND_SPARSE_INFO,
      .bufferBindCount=1,.pBufferBinds=&bi,.signalSemaphoreCount=1,.pSignalSemaphores=&ready};
    VkPipelineStageFlags stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.waitSemaphoreCount=1,.pWaitSemaphores=&ready,
      .pWaitDstStageMask=&stage,.commandBufferCount=1,.pCommandBuffers=&cmd};
    pthread_mutex_lock(&V.queue_mu);
    VK_CHECK(vkQueueBindSparse(V.queue,1,&map,VK_NULL_HANDLE));
    VK_CHECK(vkQueueSubmit(V.queue,1,&submit,fence));pthread_mutex_unlock(&V.queue_mu);
    VK_CHECK(vkWaitForFences(V.device,1,&fence,VK_TRUE,30000000000ull));
    for(unsigned k=0;k<64;k++)if(memory[shared_alignment+k]!=0x2d)valid=0;
    for(unsigned k=0;k<64;k+=4) {uint32_t value;memcpy(&value,memory+shared_alignment+64+k,4);if(value!=0x718293a4)valid=0;}
    // Mirror production lifetime ordering even on a failed byte qualification.
    bind.memory=VK_NULL_HANDLE;map.signalSemaphoreCount=0;map.pSignalSemaphores=NULL;
    VK_CHECK(vkResetFences(V.device,1,&fence));pthread_mutex_lock(&V.queue_mu);
    VK_CHECK(vkQueueBindSparse(V.queue,1,&map,fence));pthread_mutex_unlock(&V.queue_mu);
    VK_CHECK(vkWaitForFences(V.device,1,&fence,VK_TRUE,30000000000ull));
    vkDestroyFence(V.device,fence,NULL);vkDestroySemaphore(V.device,ready,NULL);
    vkDestroyCommandPool(V.device,commands,NULL);
  }
  if(test)vkDestroyBuffer(V.device,test,NULL);vk_buffer_destroy(&child);
  shared_probe_release(&views,memory,bytes);
  LOG("[sparse Pool] startup complete-usage query/create=%d alignment=%llu GPU copy/fill bytes=%s",result,
    (unsigned long long)needs.alignment,valid?"passed":"rejected");
  return valid;
}
static uint64_t sparse_pool_chunk_count(const pool *p) {
  return (p->size-1)/shared_window_bytes+1;
}
static int sparse_pool_chunk_present(const pool *p,uint64_t chunk) {
  return p->sparse_chunks && (p->sparse_chunks[chunk/64]&(UINT64_C(1)<<(chunk%64)))!=0;
}
static int sparse_pool_covered(const pool *p,uint64_t offset,uint64_t bytes) {
  if(!p->sparse_backing || !p->whole.buffer.handle || offset>p->size || bytes>p->size-offset)return 0;
  // Unknown fetch length is the original whole remainder, not a one-byte
  // admission. Exact tail retains its last mapped chunk and returns offset=size.
  uint64_t first=offset,last;
  if(!bytes) {if(first==p->size)first--;last=p->size-1;}
  else last=offset+bytes-1;
  first/=shared_window_bytes;last/=shared_window_bytes;
  for(uint64_t word=first/64;word<=last/64;word++) {
    uint64_t mask=(UINT64_MAX<<(word==first/64?first%64:0))&
      (UINT64_MAX>>(word==last/64?63-last%64:0));
    if(!p->sparse_chunks || (p->sparse_chunks[word]&mask)!=mask)return 0;
  }
  return 1;
}
static void sparse_pool_create_locked(pool *p) {
  if(p->whole.buffer.handle)return;
  if(!p->sparse_backing || !p->shared_lazy_eligible || !p->size || p->size%shared_alignment ||
      p->size>V.props.limits.sparseAddressSpaceSize)die("Sparse Pool immutable extent unsupported");
  VkResult result=sparse_pool_buffer_create(p->size,&p->whole.buffer.handle,&p->sparse_requirements);
  if(result!=VK_SUCCESS)die("Sparse Pool create failed=%d; no online backing fallback",result);
  p->sparse_chunks=calloc((sparse_pool_chunk_count(p)+63)/64,sizeof *p->sparse_chunks);
  if(!p->sparse_chunks)die("Sparse Pool residency metadata allocation");
  if(p->backing_generation==UINT64_MAX)die("Sparse Pool generation overflow");
  p->whole.begin=0;p->whole.end=p->size;p->whole.generation=++p->backing_generation;
  p->whole.buffer.size=p->size;p->whole.buffer.map=p->guest;
  // No VkDeviceMemory belongs to this virtual Buffer wrapper. Child imports
  // own allocations. Shared lazy permits only coherent memory.
  p->whole.buffer.properties=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
}
static pool_backing_span *sparse_pool_ensure_locked(pool *p,uint64_t offset,uint64_t bytes) {
  if(!recording_resources || !V.cmd || !p->sparse_backing || !bytes ||
      offset>p->size || bytes>p->size-offset || p->shared_closing)
    die("Sparse Pool map outside current finite recorded consumer");
  sparse_pool_create_locked(p);
  if(sparse_pool_covered(p,offset,bytes))return &p->whole;
  uint64_t first=offset/shared_window_bytes,last=(offset+bytes-1)/shared_window_bytes;
  unsigned missing=0;uint64_t added=0;
  for(uint64_t k=first;k<=last;k++)if(!sparse_pool_chunk_present(p,k)) {
    missing++;uint64_t begin=k*shared_window_bytes,end=begin+shared_window_bytes;
    if(end>p->size)end=p->size;added+=end-begin;
  }
  uint64_t total=atomic_load_explicit(&shared_lazy_import_bytes,memory_order_relaxed);
  if(!missing || p->backing_generation>UINT64_MAX-missing || p->shared_import_nodes>512 ||
      missing>512-p->shared_import_nodes || p->shared_import_bytes>(UINT64_C(512)<<20) ||
      added>(UINT64_C(512)<<20)-p->shared_import_bytes ||
      total>(UINT64_C(2)<<30) || added>(UINT64_C(2)<<30)-total)
    die("Sparse Pool import budget exhausted; no online replacement/fallback");
  VkSparseMemoryBind *binds=calloc(missing,sizeof *binds);
  pool_backing_span **children=calloc(missing,sizeof *children);
  sparse_bind_ticket *ticket=calloc(1,sizeof *ticket);
  if(!binds || !children || !ticket)die("Sparse Pool missing-chunk batch allocation");
  unsigned n=0;
  for(uint64_t k=first;k<=last;k++)if(!sparse_pool_chunk_present(p,k)) {
    uint64_t begin=k*shared_window_bytes,end=begin+shared_window_bytes;if(end>p->size)end=p->size;
    pool_backing_span *b=calloc(1,sizeof *b);if(!b)die("Sparse Pool child allocation");
    VkResult result=shared_span_try_allocate(p,b,begin,end);
    if(result!=VK_SUCCESS || !(b->buffer.properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) ||
        !(p->sparse_requirements.memoryTypeBits&(1u<<b->buffer.shared_memory_type)) ||
        begin%p->sparse_requirements.alignment || (end-begin)%p->sparse_requirements.alignment)
      die("Sparse Pool host child mismatch result=%d; no online fallback",result);
    children[n]=b;binds[n++]=(VkSparseMemoryBind){.resourceOffset=begin,.size=end-begin,.memory=b->buffer.memory};
  }
  VkSemaphoreCreateInfo sem={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VK_CHECK(vkCreateSemaphore(V.device,&sem,NULL,&ticket->ready));
  VkSparseBufferMemoryBindInfo buffer={.buffer=p->whole.buffer.handle,.bindCount=n,.pBinds=binds};
  VkBindSparseInfo info={.sType=VK_STRUCTURE_TYPE_BIND_SPARSE_INFO,.bufferBindCount=1,.pBufferBinds=&buffer,
    .signalSemaphoreCount=1,.pSignalSemaphores=&ticket->ready};
  pthread_mutex_lock(&V.queue_mu);VkResult result=vkQueueBindSparse(V.queue,1,&info,VK_NULL_HANDLE);pthread_mutex_unlock(&V.queue_mu);
  if(result!=VK_SUCCESS)die("Sparse Pool bind failed=%d; pending resources retained",result);
  ticket->next=recording_resources->sparse_binds;recording_resources->sparse_binds=ticket;
  for(unsigned j=0;j<n;j++) {
    pool_backing_span *b=children[j];uint64_t k=b->begin/shared_window_bytes;
    if(sparse_pool_chunk_present(p,k))die("Sparse Pool remap of retained child");
    b->generation=++p->backing_generation;
    pool_backing_span **at=&p->backings;while(*at && (*at)->begin<b->begin)at=&(*at)->next;
    b->next=*at;*at=b;shared_buffer_index_add_locked(p,b,0);
    p->sparse_chunks[k/64]|=UINT64_C(1)<<(k%64);p->shared_properties=b->buffer.properties;
  }
  p->shared_import_nodes+=n;p->shared_import_bytes+=added;p->shared_unique_bytes+=added;
  atomic_fetch_add(&shared_lazy_import_nodes,n);atomic_fetch_add(&shared_lazy_import_bytes,added);
  atomic_fetch_add(&shared_lazy_unique_bytes,added);free(children);free(binds);
  static int reported;if(!reported) {reported=1;LOG("[sparse Pool] first actual append mapped_chunks=%u stable_buffer=%p",n,(void *)p->whole.buffer.handle);}
  return &p->whole;
}
static void sparse_pool_destroy_locked(pool *p) {
  if(p->references)die("Sparse Pool destruction before original owner drain");
  if(p->whole.buffer.handle) {
    VkSparseMemoryBind unbind={.resourceOffset=0,.size=p->size,.memory=VK_NULL_HANDLE};
    VkSparseBufferMemoryBindInfo buffer={.buffer=p->whole.buffer.handle,.bindCount=1,.pBinds=&unbind};
    VkBindSparseInfo info={.sType=VK_STRUCTURE_TYPE_BIND_SPARSE_INFO,.bufferBindCount=1,.pBufferBinds=&buffer};
    VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;
    VK_CHECK(vkCreateFence(V.device,&fi,NULL,&fence));
    pthread_mutex_lock(&V.queue_mu);VkResult result=vkQueueBindSparse(V.queue,1,&info,fence);pthread_mutex_unlock(&V.queue_mu);
    VK_CHECK(result);VK_CHECK(vkWaitForFences(V.device,1,&fence,VK_TRUE,UINT64_MAX));
    vkDestroyFence(V.device,fence,NULL);vkDestroyBuffer(V.device,p->whole.buffer.handle,NULL);
    memset(&p->whole,0,sizeof p->whole);
  }
  free(p->sparse_chunks);p->sparse_chunks=NULL;
}
#undef SPARSE_POOL_USAGE
#undef SPARSE_POOL_FLAGS

#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
// Pure physical representation oracle: no GPU execution/support claim. Exercise
// the actual resolver rather than duplicate its bitmap arithmetic in the test.
unsigned vk_test_sparse_pool_ranges(void) {
  uint64_t saved_window=shared_window_bytes;shared_window_bytes=4096;
  enum { CHUNKS=131 };uint64_t bits[3]={0};
  pool p={.size=(CHUNKS-1)*4096+256,.sparse_backing=1,.shared_lazy_eligible=1,.sparse_chunks=bits};
  p.whole=(pool_backing_span){.begin=0,.end=p.size,.generation=1,
    .buffer={.handle=(VkBuffer)(uintptr_t)123,.size=p.size}};
  unsigned checks=0;
  // Sparse holes, word transitions 63/64 and 127/128, unaligned requests,
  // partial final child and exact end/overflow are independent of guest pages.
  for(unsigned pattern=0;pattern<4;pattern++) {
    for(unsigned k=0;k<CHUNKS;k++) {
      int present=pattern==0 || (pattern==1?k!=64:pattern==2?k%3!=1:k>=128);
      if(present)bits[k/64]|=UINT64_C(1)<<(k%64);else bits[k/64]&=~(UINT64_C(1)<<(k%64));
    }
    for(uint64_t start=0;start<=p.size+1;start+=127)for(unsigned n=0;n<6;n++) {
      const uint64_t lengths[]={0,1,4095,8193,262145,UINT64_MAX};uint64_t bytes=lengths[n];
      int expected=start<=p.size && bytes<=p.size-start;
      if(expected) {
        uint64_t lo=start,hi=bytes?start+bytes:p.size;
        if(lo==hi)lo--; // exact zero-length tail retains final child
        for(uint64_t k=lo/4096;k<=(hi-1)/4096;k++)
          if(!(bits[k/64]&(UINT64_C(1)<<(k%64))))expected=0;
      }
      pool_span_piece span;
      int actual=pool_span_resolve_recorded(&p,start,bytes,&span);
      if(actual!=expected || (actual && (span.backing!=&p.whole || span.offset!=start || span.size!=bytes)))
        die("Sparse Pool residency/stable coordinate contract");
      checks++;
    }
    pool_span_piece span;int end=pool_span_resolve_locked(&p,p.size,0,&span);
    int expected=!!(bits[(CHUNKS-1)/64]&(UINT64_C(1)<<((CHUNKS-1)%64)));
    if(end!=expected || (end && span.offset!=p.size))die("Sparse Pool exact end contract");
    if(pool_span_resolve_recorded(&p,p.size,1,&span) ||
       pool_span_resolve_recorded(&p,UINT64_MAX,0,&span))die("Sparse Pool extent overflow contract");
    checks+=3;
  }
  p.whole.buffer.handle=VK_NULL_HANDLE;
  if(sparse_pool_covered(&p,0,1))die("Sparse Pool bitmap is not a live handle");checks++;
  shared_window_bytes=saved_window;return checks;
}
#endif

int vk_pool_sparse_mode(void) {return sparse_pool_active;}
