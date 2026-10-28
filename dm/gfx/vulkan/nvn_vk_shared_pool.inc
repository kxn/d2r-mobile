// Fixed startup shared mode. Whole imports remain the reference; an explicit
// experimental build can reserve canonical imported spans before any borrows.
// Neither form has a shadow/mirror or online change to legacy allocation.
#include "nvn_vk_shared_import.h"
#include "dm_guest_heap.h"
extern int dm_guest_heap_probe_views_create(size_t,size_t,dm_guest_heap_probe_views *) __attribute__((weak));
extern void dm_guest_heap_probe_views_destroy(dm_guest_heap_probe_views *) __attribute__((weak));
static int shared_pool_has_imports_locked(const pool *p) {
  return p->backings!=NULL || p->shared_covers!=NULL
#ifdef DM_VULKAN_NATIVE_RESOURCES
      || p->native_allocations!=NULL
#endif
      ;
}
static VkResult shared_buffer_create_types(void *host_pointer,uint64_t bytes,vk_buffer *fresh,uint32_t image_types) {
  uint64_t address=(uintptr_t)host_pointer;
  const char *stage="external-buffer-properties";
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
  if(result==VK_SUCCESS){stage="create-buffer";result=vkCreateBuffer(V.device,&create,NULL,&fresh->handle);}
  if(result==VK_SUCCESS) {
    VkMemoryDedicatedRequirements dedicated={.sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
    VkMemoryRequirements2 requirements={.sType=VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,.pNext=&dedicated};
    VkBufferMemoryRequirementsInfo2 buffer_info={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,.buffer=fresh->handle};
    vkGetBufferMemoryRequirements2(V.device,&buffer_info,&requirements);
    VkMemoryRequirements needs=requirements.memoryRequirements;
    VkBool32 effective_requires=dedicated.requiresDedicatedAllocation;
    stage="buffer-memory-requirements";
    if(effective_requires)result=VK_ERROR_INVALID_EXTERNAL_HANDLE;
    VkMemoryHostPointerPropertiesEXT host={.sType=VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT};
    if(result==VK_SUCCESS){stage="host-pointer-properties";result=vkGetMemoryHostPointerPropertiesEXT(V.device,VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,host_pointer,&host);}
    uint32_t type=UINT32_MAX;uint64_t atom=V.props.limits.nonCoherentAtomSize;
    for(unsigned pass=0;result==VK_SUCCESS && pass<2 && type==UINT32_MAX;pass++)
      for(uint32_t k=0;k<V.mem.memoryTypeCount;k++) {
        VkMemoryPropertyFlags flags=V.mem.memoryTypes[k].propertyFlags;
        VkMemoryPropertyFlags required=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        if((host.memoryTypeBits&needs.memoryTypeBits&image_types&(1u<<k)) && (flags&required)==required &&
            ((flags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) || (pass && atom && !(atom&(atom-1)) &&
              !(address%atom) && !(bytes%atom)))) {type=k;break;}
      }
    if(result==VK_SUCCESS){stage="memory-type-and-size";if(type==UINT32_MAX || needs.size>bytes)result=VK_ERROR_INVALID_EXTERNAL_HANDLE;}
    if(result==VK_SUCCESS) {
      VkImportMemoryHostPointerInfoEXT import={.sType=VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
        .handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,.pHostPointer=host_pointer};
      VkMemoryAllocateInfo allocation={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.pNext=&import,
        .allocationSize=bytes,.memoryTypeIndex=type};
      stage="allocate-import";result=vkAllocateMemory(V.device,&allocation,NULL,&fresh->memory);
      if(result==VK_SUCCESS)fresh->shared_import=1;
      if(result==VK_SUCCESS){stage="bind-buffer";result=vkBindBufferMemory(V.device,fresh->handle,fresh->memory,0);}
      if(result==VK_SUCCESS) {
        stage="map-import";void *mapped=NULL;result=vkMapMemory(V.device,fresh->memory,0,bytes,0,&mapped);
        if(result==VK_SUCCESS){fresh->shared_import=1;if(!mapped)result=VK_ERROR_MEMORY_MAP_FAILED;}
      }
      fresh->properties=V.mem.memoryTypes[type].propertyFlags;
      fresh->shared_memory_type=type;
    }
  }
  if(result==VK_SUCCESS) {fresh->size=bytes;fresh->map=host_pointer;}
  if(result!=VK_SUCCESS)LOG("[shared pool] import failed stage=%s bytes=%llu pointer=%p pointer_high_byte=%u result=%d",stage,(unsigned long long)bytes,host_pointer,(unsigned)(address>>56),result);
  return result;
}

static VkResult shared_buffer_create(void *host_pointer,uint64_t bytes,vk_buffer *fresh) {
  return shared_buffer_create_types(host_pointer,bytes,fresh,UINT32_MAX);
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
// Caller holds resources; holes mean no imported GPU user, not a need to pin.
// Complete writer/used ranges must be fully backed; public Flush/Invalidate may
// cover untouched holes and visits only their actual imported intersections.
static void shared_pool_cache_locked(pool *p,uint64_t offset,uint64_t bytes,int invalidate,int complete) {
  if(!p->shared || offset>p->size || bytes>p->size-offset)die("Shared cache logical range");
  if(p->shared_lazy_eligible) {
    if(!(p->shared_properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))die("Shared lazy cache must be coherent");
    if(complete) {
#ifdef DM_VULKAN_NATIVE_RESOURCES
      if(!shared_pool_storage_covered_locked(p,offset,bytes))die("Shared lazy cache range lacks physical union coverage");
#else
      pool_span_iter it;pool_span_piece piece;
      if(!pool_span_iter_begin(p,offset,bytes,&it))die("Shared lazy cache union range");
      int next;while((next=pool_span_iter_next(&it,&piece))==1){}
      if(next<0)die("Shared lazy cache range lacks physical union coverage");
#endif
    }
    return; // coherent memory only; no per-import cache calls
  }
#ifdef DM_VULKAN_NATIVE_RESOURCES
  native_allocation_cache_locked(p,offset,bytes,invalidate);
#endif
  if(!shared_window_bytes) {shared_cache(&p->whole.buffer,offset,bytes,invalidate);return;}
#ifdef DM_VULKAN_NATIVE_RESOURCES
  if(complete && !shared_pool_storage_covered_locked(p,offset,bytes))
    die("Shared cache range lacks physical Buffer/native import coverage");
  complete=0; // Complete union proof above; actual Buffer holes stay holes.
#endif
  uint64_t at=offset,end=offset+bytes;
  for(pool_backing_span *b=p->backings;b && b->begin<end;b=b->next) {
    if(b->end<=offset)continue;
    uint64_t a=offset>b->begin?offset:b->begin,z=end<b->end?end:b->end;
    if(complete && a!=at)die("Shared cache range crosses unimported hole");
    shared_cache(&b->buffer,a-b->begin,z-a,invalidate);at=z;
  }
  if(complete && at!=end)die("Shared cache range not imported");
}
static void shared_pool_host_barrier(pool *p,uint64_t offset,uint64_t bytes,VkPipelineStageFlags producers) {
  if(shared_lazy_active) {
    pthread_mutex_lock(&V.resources);
#ifdef DM_VULKAN_NATIVE_RESOURCES
    int covered=shared_pool_storage_covered_locked(p,offset,bytes);
#else
    pool_span_iter it;pool_span_piece piece;int covered=pool_span_iter_begin(p,offset,bytes,&it),next=0;
    if(covered)while((next=pool_span_iter_next(&it,&piece))==1){}
    covered=covered && next==0;
#endif
    pthread_mutex_unlock(&V.resources);
    if(!covered)die("Shared lazy HOST writer lacks physical union coverage");
    VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(V.cmd,producers,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
    return;
  }
#ifdef DM_VULKAN_NATIVE_RESOURCES
  // A named active-buffer barrier cannot publish writes through another
  // VkDeviceMemory import bound to a native VkImage. Preserve the real alias.
  pthread_mutex_lock(&V.resources);
  int native_alias=native_allocation_overlaps_locked(p,offset,bytes);
  pthread_mutex_unlock(&V.resources);
  if(native_alias) {
    VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(V.cmd,producers,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
  }
#endif
  if(!shared_window_bytes) {
    VkBufferMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,
      .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
      .buffer=p->whole.buffer.handle,.offset=offset,.size=bytes};
    vkCmdPipelineBarrier(V.cmd,producers,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&b,0,NULL);return;
  }
  pthread_mutex_lock(&V.resources);
#ifdef DM_VULKAN_NATIVE_RESOURCES
  if(!shared_pool_storage_covered_locked(p,offset,bytes))die("Shared HOST writer lacks physical import coverage");
  uint64_t end=offset+bytes;
  for(pool_backing_span *span=p->backings;span && span->begin<end;span=span->next) {
    if(span->end<=offset)continue;
    uint64_t first=offset>span->begin?offset:span->begin,last=end<span->end?end:span->end;
    VkBufferMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,
      .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
      .buffer=span->buffer.handle,.offset=first-span->begin,.size=last-first};
    vkCmdPipelineBarrier(V.cmd,producers,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&b,0,NULL);
  }
#else
  pool_span_iter it;pool_span_piece piece;
  if(!pool_span_iter_begin(p,offset,bytes,&it))die("Shared HOST barrier range");
  int next;
  while((next=pool_span_iter_next(&it,&piece))==1) {
    VkBufferMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,
      .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
      .buffer=piece.backing->buffer.handle,.offset=piece.offset,.size=piece.size};
    vkCmdPipelineBarrier(V.cmd,producers,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&b,0,NULL);
  }
  if(next<0)die("Shared HOST writer range not imported");
#endif
  pthread_mutex_unlock(&V.resources);
}
static VkResult shared_span_try_allocate(pool *p,pool_backing_span *span,uint64_t begin,uint64_t end) {
  if(!p->shared || p->shared_closing || begin>=end || end>p->size ||
      begin%shared_alignment || (end-begin)%shared_alignment)return VK_ERROR_INVALID_EXTERNAL_HANDLE;
  void *alias=shared_import_host_pointer((uintptr_t)p->guest+begin,end-begin);
  if(!alias)return VK_ERROR_INVALID_EXTERNAL_HANDLE;
  VkResult result=shared_buffer_create(alias,end-begin,&span->buffer);
  if(result!=VK_SUCCESS)return result;
  if(shared_pool_has_imports_locked(p) && span->buffer.properties!=p->shared_properties) {
    vk_buffer_destroy(&span->buffer);return VK_ERROR_INVALID_EXTERNAL_HANDLE;
  }
  span->buffer.map=p->guest+begin;span->begin=begin;span->end=end;return VK_SUCCESS;
}
#include "nvn_vk_shared_lazy.inc"
static void shared_pool_materialize(pool *p) {
  if(p->shared_closing)die("Shared Pool address after closing");
  if(p->shared_lazy_eligible)return; // only an exact actual access imports pages
  if(p->backings)return;
  if(shared_window_bytes)die("Shared window access requires complete operation admission");
  void *import_pointer=shared_import_host_pointer((uintptr_t)p->guest,p->size);
  if(!import_pointer)die("Shared Pool lacks committed RW import alias");
  vk_buffer b={0};VkResult result=shared_buffer_create(import_pointer,p->size,&b);
  if(result!=VK_SUCCESS) {vk_buffer_destroy(&b);die("Shared whole Pool import failed: %d; no online legacy fallback",result);}
  p->shared_properties=b.properties;
  b.map=p->guest; // CPU-facing addresses remain the original protected VMA.
  pool_materialize_metadata(p);p->whole.buffer=b;p->whole.begin=0;p->whole.end=p->size;
  if(p->backing_generation==UINT64_MAX)die("Shared generation exhausted");
  p->whole.generation=++p->backing_generation;p->backings=&p->whole;
}
int vk_pool_shared_mode(void) {mtl_init();return shared_active;}
// Recording-owner read of the immutable result, after device initialization.
// Unlike the startup query this never initializes Vulkan or reads environment.
int vk_pool_shared_active(void) {return shared_active;}
int vk_pool_shared_gpu_gate_mode(void) {return shared_active && shared_gpu_gate_phase;}
int vk_pool_shared_materialized(uint64_t address,uint64_t bytes) {
  if(!shared_active || !bytes)return 0;
  pthread_mutex_lock(&V.resources);pool *p=pool_find(address,bytes);
  pool_span_piece covered;
  int ready=p && p->shared && !p->shared_closing &&
      pool_span_resolve_locked(p,address-(uintptr_t)p->guest,bytes,&covered);
  pthread_mutex_unlock(&V.resources);return ready;
}
#if defined(DM_VULKAN_TEST_EMBED) && !defined(__ANDROID__)
// Only standalone host fixtures without the real memory SVC use this owned
// anonymous mapping. Android must test the production memfd pair below.
static uint8_t *shared_probe_mapping(uint64_t alignment,uint64_t bytes) {
  uint64_t reserve=bytes+alignment;
  void *raw=mmap(NULL,reserve,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
  if(raw==MAP_FAILED)return NULL;
  uintptr_t begin=(uintptr_t)raw,aligned=(begin+alignment-1)&~(alignment-1);
  uint64_t prefix=aligned-begin,suffix=reserve-prefix-bytes;
  if(prefix && munmap(raw,prefix))die("Shared probe prefix unmap failed");
  if(suffix && munmap((void *)(aligned+bytes),suffix))die("Shared probe suffix unmap failed");
  return (uint8_t *)aligned;
}
#endif
static void shared_probe_release(dm_guest_heap_probe_views *views,void *memory,size_t bytes) {
  if(views->guest)dm_guest_heap_probe_views_destroy(views);
  else if(memory && munmap(memory,bytes))die("Shared probe unmap failed");
}
#include "nvn_vk_shared_overlap_probe.inc"
static void shared_device_probe(void) {
  const char *lazy=getenv("DM_VK_SHARED_LAZY_WINDOWS");
  if(lazy && strcmp(lazy,"0") && strcmp(lazy,"1"))die("DM_VK_SHARED_LAZY_WINDOWS expects0or1");
  shared_lazy_requested=lazy && !strcmp(lazy,"1");
  if(!shared_requested) {
    if(shared_lazy_requested)LOG("[shared lazy] unsupported: fixed shared mode required");
    return;
  }
  uint64_t candidate_window=0;
  if(!vk_query_native_supported()) {LOG("[shared pool] startup legacy: native query timestamp format unsupported");return;}
  extern int dm_gfx_shared_owner_ready(void);
  if(!dm_gfx_shared_owner_ready()) {LOG("[shared pool] startup legacy: dedicated NVN owner unavailable");return;}
  if(!shared_extension || !vkGetMemoryHostPointerPropertiesEXT) {LOG("[shared pool] startup legacy: external host import unavailable");return;}
  VkPhysicalDeviceExternalMemoryHostPropertiesEXT host={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT};
  VkPhysicalDeviceProperties2 props={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&host};
  vkGetPhysicalDeviceProperties2(V.physical,&props);shared_alignment=host.minImportedHostPointerAlignment;
  uint64_t page_size=(uint64_t)getpagesize();
  if(!page_size || (page_size&(page_size-1))) {LOG("[shared pool] startup legacy: invalid host page size");return;}
  if(shared_alignment<page_size)shared_alignment=page_size;
  if(shared_alignment<4096)shared_alignment=4096;
  if(!shared_alignment || (shared_alignment&(shared_alignment-1)) || shared_alignment>(1u<<20)) {
    LOG("[shared pool] startup legacy: unsupported import alignment");return;
  }
  // Actual bytes both ways before any NVN Pool exists. This validates only
  // the small allocation/type/API path, not all future addresses or residency.
  size_t bytes=shared_alignment*2;uint8_t *memory=NULL,*import_pointer=NULL;
  dm_guest_heap_probe_views views={0};
  if(dm_guest_heap_probe_views_create && dm_guest_heap_probe_views_destroy) {
    if(!dm_guest_heap_probe_views_create(bytes,shared_alignment,&views)) {
      LOG("[shared pool] startup legacy: memfd pair construction errno=%d",errno);return;
    }
    memory=views.guest;import_pointer=views.import_alias;
    if(mprotect(memory,bytes,PROT_READ|PROT_WRITE)) {
      shared_probe_release(&views,memory,bytes);LOG("[shared pool] startup legacy: probe guest RW");return;
    }
  }
#if defined(DM_VULKAN_TEST_EMBED) && !defined(__ANDROID__)
  else {memory=shared_probe_mapping(shared_alignment,bytes);import_pointer=memory;}
#endif
  if(!memory || !import_pointer) {LOG("[shared pool] startup legacy: memfd pair API unavailable");return;}
  memset(memory,0x31,bytes);
  // Pin through the RW alias after the CPU-facing view has become RO, as in
  // a texture Pool's first later Buffer access. Never relax the guest view.
  if(views.guest && mprotect(memory,bytes,PROT_READ)) {
    shared_probe_release(&views,memory,bytes);LOG("[shared pool] startup legacy: probe guest RO");return;
  }
  LOG("[shared pool] startup probe mapping=%s pointer=%p import=%p bytes=%zu",
      views.guest?"memfd-pair-guest-RO":"host-owned-anonymous",(void *)memory,(void *)import_pointer,bytes);
  vk_buffer b={0};VkResult status=shared_buffer_create(import_pointer,bytes,&b);
  if(status!=VK_SUCCESS) {vk_buffer_destroy(&b);shared_probe_release(&views,memory,bytes);LOG("[shared pool] startup legacy: probe import=%d",status);return;}
  int probe_coherent=!!(b.properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
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
  VK_CHECK(vkQueueSubmit(V.queue,1,&submit,fence));VK_CHECK(vkWaitForFences(V.device,1,&fence,VK_TRUE,30000000000ull));
  shared_cache(&b,shared_alignment,128,1);int valid=1;
  for(unsigned k=0;k<64;k++)if(memory[shared_alignment+k]!=0x31)valid=0;
  for(unsigned k=0;k<64;k+=4) {uint32_t value;memcpy(&value,memory+shared_alignment+64+k,4);if(value!=0x718293a4)valid=0;}
  // Probe success does not guarantee every future host pointer has the same
  // memoryTypeBits. Prepare the NC phase cold if any admissible type needs it.
  int needs_gate=0;
  for(uint32_t k=0;k<V.mem.memoryTypeCount;k++) {
    VkMemoryPropertyFlags f=V.mem.memoryTypes[k].propertyFlags;
    if((f&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT))==
        (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT) &&
        !(f&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))needs_gate=1;
  }
#ifdef DM_VULKAN_TEST_EMBED
  shared_gpu_gate_force=getenv("DM_SHARED_IMAGE_FORCE_PREWRITE_GATE")!=NULL;
  if(shared_gpu_gate_force)needs_gate=1; // admission only, not NC Vk cache calls
#endif
  vkDestroyFence(V.device,fence,NULL);vkDestroyCommandPool(V.device,pool_handle,NULL);vk_buffer_destroy(&b);
#ifdef DM_VULKAN_SHARED_IMPORT_WINDOWS
  uint64_t requested_window=V.options.pool_window_bytes;
  if(valid && requested_window) {
    const char *sync=getenv("DM_VULKAN_POOL_SYNC_REFERENCE");
    if(atomic_load(&pool_sync_requested)==1 || (sync && strcmp(sync,"0")))
      die("Shared import windows do not support full-pool sync reference; cold window bytes 0 required");
    uint64_t alignments[]={shared_alignment,(uint64_t)getpagesize(),V.props.limits.nonCoherentAtomSize,
      V.props.limits.minUniformBufferOffsetAlignment,V.props.limits.minStorageBufferOffsetAlignment,
      V.props.limits.minTexelBufferOffsetAlignment};
    if((requested_window&(requested_window-1)) || requested_window<4096)die("Shared window granularity");
    for(unsigned k=0;k<sizeof alignments/sizeof *alignments;k++)
      if(!alignments[k] || (alignments[k]&(alignments[k]-1)) || alignments[k]>requested_window)
        die("Shared window alignment cannot preserve cache/descriptor prefix");
    if(!shared_overlap_probe(import_pointer,memory,bytes))die("Requested shared import windows require overlapping-import GPU byte proof");
    candidate_window=requested_window;
  }
#endif
  shared_probe_release(&views,memory,bytes);
  if(valid && needs_gate) {
    uint64_t atom=V.props.limits.nonCoherentAtomSize,page=(uint64_t)getpagesize();
    if(!atom || (atom&(atom-1)) || !page || (page&(page-1)) || atom>page) {
      LOG("[shared pool] startup legacy: NC atom exceeds image-page gate");valid=0;
    }
  }
  if(valid && !nvn_dirty_cpu_observation_enable()) {
    LOG("[shared pool] startup legacy: sleeping CPU observation unavailable");valid=0;
  } else if(valid) {
    shared_cpu_observe_phase=1;
    shared_gpu_gate_phase=needs_gate; // coherent GPU submission policy unchanged
  }
  // Only a successful capability/byte probe changes the immutable backing
  // policy. Every unsupported return above keeps the original legacy options,
  // including Android's default windows and the published defer getter.
  shared_window_bytes=valid?candidate_window:0;
  if(valid) {
    V.options.pool_window_bytes=shared_window_bytes;
    V.options.pool_mirror_reference=0;
    V.options.pool_defer_publication=0;
    atomic_store_explicit(&pool_defer_publication_enabled,0,memory_order_release);
  }
  shared_active=valid;LOG("[shared pool] immutable startup %s; import_window_bytes=%llu no online mirror fallback",valid?"shared":"legacy: byte probe mismatch",(unsigned long long)shared_window_bytes);
  if(shared_lazy_requested) {
    static const uint8_t exact_f830[32]={0xf8,0x30,0x28,0x5f,0xe6,0x8f,0x0a,0x74,0x47,0x92,0x26,0x9d,0xa5,0xf1,0x60,0xed,
      0x86,0x36,0xc1,0x72,0xb1,0xf6,0x51,0x1d,0xb9,0xd9,0x94,0x78,0xe1,0xa2,0xac,0xce};
    // Exact WSI/AHB allocation-only derivative. Host pointer imports, range
    // aliases and coherency retain the complete f830 frozen source; the live
    // overlapping-import and CPU-observation admission gates above still run.
    static const uint8_t exact_wsi_ubwc[32]={0xb3,0xfc,0xe5,0xab,0x29,0xc6,0x61,0x7b,0xd2,0xcf,0x33,0x4b,0x3a,0x56,0xc6,0x79,
      0x51,0xde,0x27,0x1b,0xd8,0x70,0x87,0x8c,0x9a,0x31,0x21,0xff,0x60,0x6b,0x1b,0x9f};
    // B3 derivative adding BC3 image format admission only. Buffer aliases and
    // coherency are unchanged; the live overlap/CPU-observation gates above
    // still qualify this launch before enabling lazy import windows.
    static const uint8_t exact_native_bc3[32]={0x0e,0x53,0xff,0xa2,0xbb,0xbf,0xf0,0xa4,0xf5,0x20,0xeb,0x08,0x00,0x39,0xa8,0x48,
      0xf0,0x85,0x32,0x76,0xec,0xe5,0x9c,0xaa,0x8d,0xf9,0xa0,0x29,0xcf,0x2b,0x40,0x24};
    // BC3 image layout-only derivative; Buffer import/window code is identical.
    static const uint8_t exact_native_bc3_tiled[32]={0x3e,0x93,0x76,0xed,0x37,0x59,0xaf,0x2c,0xd0,0x15,0xcf,0x56,0x08,0x27,0x4f,0x66,
      0x1b,0xa3,0xb3,0x3d,0xf6,0xe9,0x5c,0xb6,0xfa,0x25,0x42,0x66,0x6f,0x34,0xee,0x50};
    // Exact BC3 derivative changing only Android's standard queue-extension
    // allowlist. The Buffer import/alias code is unchanged; retain all live
    // overlapping-import, coherent-memory and CPU-observation gates above.
    static const uint8_t exact_native_bc3_queue_sync[32]={0x12,0xb8,0xb5,0x4a,0x25,0x14,0xff,0x1c,0xdf,0x8d,0xa4,0x22,0xc7,0x1d,0xb7,0x9a,
      0x68,0x3d,0x45,0x17,0xeb,0x9a,0xec,0xfb,0xb2,0x0a,0x41,0x25,0x52,0x25,0x59,0xf5};
    // Exact readonly-SSBO compiler-only derivative of 12b8. Frozen source
    // comparison leaves Buffer import, aliases and coherency unchanged;
    // preserve all live byte, coherent-memory and CPU-observation gates.
    static const uint8_t exact_readonly_ssbo_route[32]={0x82,0xf2,0x9f,0xeb,0xf9,0x53,0xf9,0xe3,0xc4,0x89,0x1c,0xe3,0xd7,0x93,0xc1,0x71,
      0x36,0x6a,0xf9,0x65,0xef,0x1b,0x25,0xa2,0xfb,0xdb,0x17,0x38,0xd7,0x25,0xf3,0xcb};
    // Exact compiler scheduling-only derivative. Keep live byte/coherent
    // probes and the strict driver-bound native table contract unchanged.
    static const uint8_t exact_memory_chain_v3[32]={0x82,0xcb,0xd8,0x4e,0xa6,0x64,0x55,0x29,0x2e,0x67,0x61,0xf8,0x66,0x78,0x30,0xfc,
      0x26,0x94,0x5d,0x61,0xa5,0x27,0x4a,0x14,0x5a,0xb1,0x2c,0xb5,0x2f,0x2a,0xe3,0xdd};
    uint8_t digest[32];int provider=vk_loader_native_driver_digest(digest) &&
        (!memcmp(digest,exact_f830,32) || !memcmp(digest,exact_wsi_ubwc,32) ||
         !memcmp(digest,exact_native_bc3,32) || !memcmp(digest,exact_native_bc3_tiled,32) ||
         !memcmp(digest,exact_native_bc3_queue_sync,32) || !memcmp(digest,exact_readonly_ssbo_route,32) ||
         !memcmp(digest,exact_memory_chain_v3,32));
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP) && !defined(__ANDROID__)
    // Owned software-GPU contract oracle only. Never qualifies an APP driver.
    if(getenv("DM_SHARED_LAZY_ALIAS_FIXTURE"))provider=1;
#endif
    shared_lazy_active=valid && shared_window_bytes && probe_coherent && provider;
    LOG("[shared lazy] immutable startup %s; no enumeration/copy/replacement; named-buffer proofs disabled=%d",
        shared_lazy_active?"enabled experimental":"unsupported: original admission retained",shared_lazy_active);
  }
}

void vk_pool_shared_publish(uint64_t address,uint64_t bytes,int invalidate) {
  if(!shared_active || !bytes)die("Shared publish outside fixed mode");
  vk_texture_copy_encoding_access(address,bytes);
  pthread_mutex_lock(&V.resources);pool *p=pool_find(address,bytes);
  if(!p || !p->shared || p->shared_closing)die("Shared publish Pool lifetime/range");
  uint64_t offset=address-(uintptr_t)p->guest,atom=V.props.limits.nonCoherentAtomSize;
  if(!atom || (atom&(atom-1)) || offset+bytes>UINT64_MAX-(atom-1))die("Shared publish atom extent");
  uint64_t first=offset&~(atom-1),end=(offset+bytes+atom-1)&~(atom-1);
  // Explicit cache API never secretly waits an overlapping GPU. Existing
  // consumer bookkeeping is page-conservative, stronger than the atom bound.
  if(pending_gpu_access(p,first,end-first))die("Shared CPU cache publication overlaps pending GPU cacheline/page");
  if(shared_pool_has_imports_locked(p))shared_pool_cache_locked(p,offset,bytes,invalidate,0);
  pthread_mutex_unlock(&V.resources);
}

#ifdef DM_VULKAN_TEST_EMBED
int vk_test_fixed_shared_pool(uint64_t address,int *ready,int *shadow,uint32_t *properties) {
  pthread_mutex_lock(&V.resources);pool *p=pool_find(address,1);
  int valid=p && p->shared;if(valid) {*ready=p->backings!=NULL;*shadow=p->shadow!=NULL;
    *properties=p->shared_properties;}
  pthread_mutex_unlock(&V.resources);return valid;
}
#endif

// A real CPU conversion read, not a Buffer access. Do not pin an entire Pool
// for CPU image conversion; a later Buffer consumer imports its RW alias.
// References and pending CPU-effect visibility remain mandatory.
int vk_pool_shared_image_source(uint64_t address,uint64_t bytes,int prepare) {
  if(!shared_active || !bytes)return 0;
  pthread_mutex_lock(&V.resources);pool *p=pool_find(address,bytes);
  if(!p || !p->shared || p->shared_closing) {pthread_mutex_unlock(&V.resources);return 0;}
  if(prepare)p->references++;
  int recorded=recording_cpu_write(address,bytes);
  uint64_t serial=pending_cpu_write(address,bytes);
  pthread_mutex_unlock(&V.resources);
  if(!prepare)return !recorded && !serial;
  int reopen=V.cmd!=VK_NULL_HANDLE;
  if(recorded) {uint64_t current=vk_internal_commit();if(current>serial)serial=current;}
  if(serial)vk_internal_wait(serial);
  if(recorded && reopen)mtl_sub_begin();
  pthread_mutex_lock(&V.resources);
  if(p->shared_closing || p!=pool_find(address,bytes))die("Shared image source lifetime changed");
  // Original shared writeback completion already performs the required
  // Invalidate before its CPU publication fence. Do not invalidate arbitrary
  // CPU-dirty source bytes here: doing so could discard newer CPU cachelines.
  if(!p->references)die("Shared image source reference underflow");
  p->references--;pthread_cond_broadcast(&pool_cv);pthread_mutex_unlock(&V.resources);
  return 1;
}

#include "nvn_vk_shared_union.inc"
#ifdef DM_VULKAN_NATIVE_RESOURCES
#include "nvn_vk_native_allocation.inc"
#endif
