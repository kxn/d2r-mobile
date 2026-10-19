// Explicit experiment: shared buffer semantics, as upstream Metal NoCopy.
// CPU edits to bytes in GPU use require the original NVN completion ordering.
// This mode does NOT snapshot concurrent illegal CPU/GPU races. Default VMA
// mirrors, their stronger snapshot fixtures, texture dirty tracking and all
// writer/readback paths are unchanged. Imported nodes are GPU-readonly only.
enum { POOL_HOST_SINGLE_LIMIT=16<<20,POOL_HOST_TOTAL_LIMIT=32<<20,
       POOL_HOST_REJECT_LIMIT=128 };
static uint64_t pool_host_single_limit=POOL_HOST_SINGLE_LIMIT,pool_host_total_limit=POOL_HOST_TOTAL_LIMIT;
struct pool_host_reject {struct pool_host_reject *next;uint64_t begin,end;};
static void pool_host_reject_destroy(pool *p) {
  while(p->host_reject) {struct pool_host_reject *q=p->host_reject;
    p->host_reject=q->next;free(q);}
}
static int pool_host_rejected(pool *p,uint64_t begin,uint64_t end) {
  if(p->host_reject_all)return 1;
  for(struct pool_host_reject *q=p->host_reject;q;q=q->next)
    if(begin<q->end && q->begin<end)return 1;
  return 0;
}
// Registry locked, per logical lifetime. Rejection never expires on VkBuffer
// destruction or memory pressure, so a failed GUP is not retried every draw.
static void pool_host_reject_add(pool *p,uint64_t begin,uint64_t end) {
  if(begin>=end || p->host_reject_all)return;
  for(struct pool_host_reject *q=p->host_reject;q;q=q->next)
    if(q->begin<=begin && q->end>=end)return;
  struct pool_host_reject *q=NULL;
  if(p->host_reject_count==POOL_HOST_REJECT_LIMIT || !(q=malloc(sizeof *q))) {
    p->host_reject_all=1;return;
  }
  *q=(struct pool_host_reject){p->host_reject,begin,end};p->host_reject=q;p->host_reject_count++;
}
static int pool_host_overlap(const pool *p,uint64_t begin,uint64_t size) {
  if(!size)return 0;
  for(const pool_backing_span *b=p->backings;b;b=b->next)
    if(b->buffer.imported_host && begin<b->end && b->begin<begin+size)return 1;
  return 0;
}
int vk_pool_host_imported_range(uint64_t address,uint64_t size) {
  if(!V.options.pool_host_readonly || !size || size>UINT64_MAX-address)return 0;
  int found=0;pthread_mutex_lock(&V.resources);
  for(pool *p=pools;p;p=p->next) {
    uint64_t base=(uintptr_t)p->guest,lo=address>base?address:base;
    uint64_t hi=address+size<base+p->size?address+size:base+p->size;
    if(lo<hi && pool_host_overlap(p,lo-base,hi-lo)){found=1;break;}
  }
  pthread_mutex_unlock(&V.resources);return found;
}
uint64_t vk_pool_host_pinned_bytes(void) {return atomic_load(&pool_host_pinned);}
static int pool_host_try_span(pool *p,pool_backing_span *b,uint64_t begin,uint64_t end,
    const vk_pool_requirement *requirements,size_t count) {
  if(!V.options.pool_host_readonly || !V.options.pool_window_bytes || !pool_host_supported ||
      V.options.pool_mirror_reference || (recording_resources && recording_resources->pool_sync_reference) ||
      !pool_host_alignment || begin>=end || end>p->size || end-begin>pool_host_single_limit ||
      ((uintptr_t)(p->guest+begin)%pool_host_alignment) || ((end-begin)%pool_host_alignment) ||
      pool_host_rejected(p,begin,end) || pool_host_overlap(p,begin,end-begin))return 0;
  // A first buffer use may follow image uploads which already protected the
  // CPU VMA. Do not alter any protection; normal writable-import GUP is the
  // actual permission check, and one failure is sticky for this window.
  if(p->initialized_pages)for(uint64_t page=begin>>12;page<(end+4095)>>12;page++)
    if(p->initialized_pages[page/64]&(UINT64_C(1)<<(page%64)))return 0;
  uint64_t base=(uintptr_t)p->guest;
  for(size_t k=0;k<count;k++)if(requirements[k].writable &&
      requirements[k].address<base+end && base+begin<requirements[k].address+requirements[k].size)
    return 0;
  uint64_t bytes=end-begin,used=atomic_load(&pool_host_pinned);
  do {if(used>pool_host_total_limit || bytes>pool_host_total_limit-used)return 0;}
  while(!atomic_compare_exchange_weak(&pool_host_pinned,&used,used+bytes));
#ifdef DM_VULKAN_TEST_EMBED
  pool_host_test_attempts++;
#endif
  vk_buffer fresh={.size=bytes,.map=p->guest+begin,.imported_host=1};
  VkExternalMemoryBufferCreateInfo external={.sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
    .handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
  VkBufferCreateInfo create={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.pNext=&external,.size=bytes,
    .usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT|
      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|
      VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT|
      VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT};
  VkPhysicalDeviceExternalBufferInfo external_info={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO,
    .usage=create.usage,.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
  VkExternalBufferProperties external_properties={.sType=VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES};
  vkGetPhysicalDeviceExternalBufferProperties(V.physical,&external_info,&external_properties);
  VkResult result=(external_properties.externalMemoryProperties.externalMemoryFeatures&
    VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT)?VK_SUCCESS:VK_ERROR_INVALID_EXTERNAL_HANDLE;
  if(result==VK_SUCCESS)result=vkCreateBuffer(V.device,&create,NULL,&fresh.handle);
  if(result==VK_SUCCESS) {
    VkMemoryRequirements needs;vkGetBufferMemoryRequirements(V.device,fresh.handle,&needs);
    VkMemoryHostPointerPropertiesEXT host={.sType=VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT};
    result=vkGetMemoryHostPointerPropertiesEXT(V.device,VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
      fresh.map,&host);
    VkPhysicalDeviceMemoryProperties types;vkGetPhysicalDeviceMemoryProperties(V.physical,&types);
    uint32_t type=UINT32_MAX;VkMemoryPropertyFlags required=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|
      VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    uint64_t atom=V.props.limits.nonCoherentAtomSize;
    int allow_noncoherent=V.options.pool_host_noncoherent && atom && !(atom&(atom-1)) &&
      !((uintptr_t)fresh.map%atom) && !(bytes%atom);
    if(result==VK_SUCCESS)for(unsigned pass=0;pass<(allow_noncoherent?2u:1u)&&type==UINT32_MAX;pass++)
      for(uint32_t k=0;k<types.memoryTypeCount;k++)
        if((host.memoryTypeBits&needs.memoryTypeBits&(1u<<k)) &&
            (types.memoryTypes[k].propertyFlags&required)==required &&
            (pass || (types.memoryTypes[k].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
          type=k;break;
        }
    if(result==VK_SUCCESS && (type==UINT32_MAX || needs.size>bytes))result=VK_ERROR_INVALID_EXTERNAL_HANDLE;
    if(result==VK_SUCCESS) {
      VkImportMemoryHostPointerInfoEXT import={.sType=VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
        .handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,.pHostPointer=fresh.map};
      VkMemoryAllocateInfo allocation={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.pNext=&import,
        .allocationSize=bytes,.memoryTypeIndex=type};
#ifdef DM_VULKAN_TEST_EMBED
      if(pool_host_test_fail_import) {pool_host_test_fail_import=0;result=VK_ERROR_INVALID_EXTERNAL_HANDLE;}
      else
#endif
      result=vkAllocateMemory(V.device,&allocation,NULL,&fresh.memory);
      if(result==VK_SUCCESS)result=vkBindBufferMemory(V.device,fresh.handle,fresh.memory,0);
      fresh.properties=types.memoryTypes[type].propertyFlags;
      if(result==VK_SUCCESS && !(fresh.properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        void *mapped=NULL;result=vkMapMemory(V.device,fresh.memory,0,bytes,0,&mapped);
        if(result==VK_SUCCESS) {
          fresh.imported_host_mapped=1;
          if(!mapped)result=VK_ERROR_MEMORY_MAP_FAILED;
        }
      }
    }
  }
  if(result!=VK_SUCCESS) {
    if(fresh.handle)vkDestroyBuffer(V.device,fresh.handle,NULL);
    if(fresh.imported_host_mapped)vkUnmapMemory(V.device,fresh.memory);
    if(fresh.memory)vkFreeMemory(V.device,fresh.memory,NULL);
    atomic_fetch_sub(&pool_host_pinned,bytes);pool_host_reject_add(p,begin,end);
    LOG("[vulkan readonly host] rejected window identity=%llu bytes=%llu VkResult=%d; sticky mirror",
      (unsigned long long)p->identity,(unsigned long long)bytes,result);return 0;
  }
  b->begin=begin;b->end=end;b->buffer=fresh;return 1;
}
// Publish before the original QueueSubmit, while its resource reference owns
// this immutable backing. A shared readonly range needs no CPU read/invalidate.
// Every used page is flushed: dirty generations do not prove a prior flush.
static void pool_host_flush_read(const pool_backing_span *b,uint64_t offset,uint64_t size) {
  const vk_buffer *buffer=&b->buffer;
  if(buffer->properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)return;
  uint64_t atom=V.props.limits.nonCoherentAtomSize;
  if(!V.options.pool_host_noncoherent || !buffer->imported_host || !buffer->imported_host_mapped ||
      !size || offset<b->begin || offset>=b->end || size>b->end-offset ||
      !atom || (atom&(atom-1)) || buffer->size%atom)
    die("Vulkan readonly host noncoherent publication bounds");
  uint64_t first=(offset-b->begin)&~(atom-1),end=offset-b->begin+size;
  if(end>UINT64_MAX-(atom-1))die("Vulkan readonly host noncoherent range overflow");
  end=(end+atom-1)&~(atom-1);
  if(end>buffer->size)die("Vulkan readonly host noncoherent mapped range");
  VkMappedMemoryRange range={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
    .memory=buffer->memory,.offset=first,.size=end-first};
  // Failure cannot replace an already borrowed handle or submit stale bytes.
  VK_CHECK(vkFlushMappedMemoryRanges(V.device,1,&range));
  POOL_COPY_ADD(host_flush_bytes,end-first);POOL_COPY_ADD(host_flush_calls,1);
}
// All producer and CPU-completion destinations pass this before any physical
// handle borrow. Demotion is cold and sticky, never in the dirty fault handler.
// The readonly node has no GPU writer, so guest is authoritative at handoff.
static int pool_host_demote(uint64_t address,uint64_t size) {
  if(!V.options.pool_host_readonly || !size)return 0;
  uint64_t identity=0,begin=0,end=0,base=0;
  pthread_mutex_lock(&V.resources);pool *owner=pool_find(address,size);
  if(owner) {
    base=(uintptr_t)owner->guest;
    pool_host_reject_add(owner,address-base,address-base+size);
    for(pool_backing_span *b=owner->backings;b;b=b->next)
      if(b->buffer.imported_host && address<base+b->end && base+b->begin<address+size) {
        identity=owner->identity;begin=b->begin;end=b->end;break;
      }
  }
  pthread_mutex_unlock(&V.resources);if(!identity)return 0;
  if(pool_operation_depth || pool_maintenance || vk_shader_operation_active() ||
      vk_texture_binding_scope_active() || vk_texture_shader_collecting())
    die("Vulkan readonly host writer missed preborrow admission");
  int reopen=V.cmd!=VK_NULL_HANDLE,was_stream=stream_recording;pool_maintenance=1;
  if(V.cmd){vk_render_end_preserve();vk_internal_wait(vk_internal_commit());}
  stream_recording=0;mtl_finish();reclaim_submissions();
  pthread_mutex_lock(&V.resources);owner=pool_find(base,1);
  if(!owner || owner->identity!=identity || owner->references)die("Vulkan readonly host handoff lifetime changed");
  pool_span_piece old=pool_span_require_locked(owner,begin,end-begin);
  if(old.backing->begin!=begin || old.backing->end!=end || !old.backing->buffer.imported_host)
    die("Vulkan readonly host handoff mapping changed");
  if(owner->backing_generation==UINT64_MAX || V.buffer_epoch==UINT64_MAX ||
      atomic_load(&pool_registry_epoch)==UINT64_MAX)die("Vulkan readonly host generation overflow");
  pool_backing_span replacement={0};VK_CHECK(pool_span_try_allocate(&replacement,begin,end));
  pthread_mutex_lock(&pool_publication_mu);
  memcpy(replacement.buffer.map,owner->guest+begin,end-begin);
  POOL_COPY_ADD(mirror_bytes,end-begin);
  pool_materialize_metadata(owner);memcpy(owner->shadow+begin,owner->guest+begin,end-begin);
  POOL_COPY_ADD(shadow_bytes,end-begin);
  pthread_mutex_unlock(&pool_publication_mu);
  vk_buffer prior=old.backing->buffer;old.backing->buffer=replacement.buffer;
  old.backing->generation=++owner->backing_generation;
  owner->locked_span_hint=NULL;owner->initialized_first_page=owner->initialized_end_page=0;
  for(uint64_t page=begin>>12;page<end>>12;page++)owner->page_upload_stamps[page]=0;
  pool_host_reject_add(owner,begin,end);
  atomic_fetch_add_explicit(&pool_registry_epoch,1,memory_order_release);V.buffer_epoch++;
  vk_buffer_destroy(&prior);pthread_mutex_unlock(&V.resources);
  stream_recording=was_stream;if(was_stream){stream_last_draw=V.draws;stream_head_pending=0;}
  pool_maintenance=0;if(reopen){mtl_sub_begin();vk_ghost_flush();}
  return 1;
}
static int pool_host_prepare_writers(const vk_pool_requirement *ranges,size_t count) {
  if(!V.options.pool_host_readonly)return 0;
  for(size_t k=0;k<count;k++)if(ranges[k].writable)
    if(pool_host_demote(ranges[k].address,ranges[k].size))return 1;
  return 0;
}
void vk_pool_host_cpu_write_prepare(uint64_t address,uint64_t size) {
  if(!V.options.pool_host_readonly || !size)return;
  if(size>UINT64_MAX-address)die("Vulkan readonly host CPU destination overflow");
  // Most effects target a single pool. For a strided external destination,
  // clip each registered lifetime without interpreting unrelated CPU storage.
  for(;;) {
    uint64_t found=0,bytes=0;
    pthread_mutex_lock(&V.resources);
    for(pool *p=pools;p;p=p->next) {
      uint64_t base=(uintptr_t)p->guest,lo=address>base?address:base;
      uint64_t hi=address+size<base+p->size?address+size:base+p->size;
      if(lo>=hi)continue;
      pool_host_reject_add(p,lo-base,hi-base);
      if(pool_host_overlap(p,lo-base,hi-lo)){found=lo;bytes=hi-lo;break;}
    }
    pthread_mutex_unlock(&V.resources);if(!bytes)return;
    if(!pool_host_demote(found,bytes))die("Vulkan readonly host CPU handoff did not progress");
  }
}

#ifdef DM_VULKAN_TEST_EMBED
void vk_test_pool_host_limits(uint64_t single,uint64_t total) {
  if(V.cmd || atomic_load(&pool_host_pinned))die("Vulkan host limits fixture requires cold ownership");
  pool_host_single_limit=single;pool_host_total_limit=total;
}
void vk_test_pool_host_fail_once(void) {pool_host_test_fail_import=pool_host_test_fail_mirror=1;}
uint64_t vk_test_pool_host_attempts(void) {return pool_host_test_attempts;}
VkMemoryPropertyFlags vk_test_pool_host_properties(uint64_t address) {
  VkMemoryPropertyFlags flags=0;pthread_mutex_lock(&V.resources);pool *p=pool_find(address,1);
  pool_span_piece span;
  if(p && pool_span_resolve_locked(p,address-(uintptr_t)p->guest,1,&span) && span.backing->buffer.imported_host)
    flags=span.backing->buffer.properties;
  pthread_mutex_unlock(&V.resources);return flags;
}
#endif
