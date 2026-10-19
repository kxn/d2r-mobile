// OWNED functional prototype only. The only caller owns a fresh, independent
// complete allocation and records no image/descriptor/CPU-effect before attach.
// There is deliberately no game startup option, malloc hook or guest selector.
void vk_test_pool_owned_enable(void) {
  if(V.instance)die("Vulkan owned fixture selection after device initialization");
  owned_fixture_requested=1;V.owned_fixture_active=1;vk_test_texture_owned_scopes(1);
}
int vk_test_pool_owned_supported(void) {return owned_fixture_supported;}
int vk_test_pool_owned_attach(uint64_t address,uint64_t bytes) {
  if(!owned_fixture_requested || !owned_fixture_supported || !owned_fixture_alignment ||
      !bytes || bytes>(16u<<20) || address%owned_fixture_alignment || bytes%owned_fixture_alignment ||
      address%4096 || bytes%4096 || bytes>UINTPTR_MAX-address || V.cmd || pool_operation_depth)return 0;
  pthread_mutex_lock(&V.resources);pool *p=pool_find(address,bytes);
  if(!p || (uintptr_t)p->guest!=address || p->size!=bytes || p->references || p->backings || p->shadow ||
      p->owned_readonly || V.options.pool_mirror_reference || !V.options.pool_window_bytes) {
    pthread_mutex_unlock(&V.resources);return 0;
  }
  pool_owned_readonly *certificate=calloc(1,sizeof *certificate);
  if(!certificate){pthread_mutex_unlock(&V.resources);return 0;}
  // Reject protected/busy/untracked pages BEFORE GUP, never unprotect/retry.
  if(!nvn_dirty_owned_exclude(p->guest,bytes,&certificate->exclusion)) {
    free(certificate);pthread_mutex_unlock(&V.resources);return 0;
  }
  vk_buffer fresh={.size=bytes,.map=p->guest,.owned_host_import=1};
  VkExternalMemoryBufferCreateInfo external={.sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
    .handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
  VkBufferCreateInfo create={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.pNext=&external,.size=bytes,
    .usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT};
  VkPhysicalDeviceExternalBufferInfo info={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO,
    .usage=create.usage,.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
  VkExternalBufferProperties props={.sType=VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES};
  vkGetPhysicalDeviceExternalBufferProperties(V.physical,&info,&props);
  VkResult result=props.externalMemoryProperties.externalMemoryFeatures&VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT?
    VK_SUCCESS:VK_ERROR_INVALID_EXTERNAL_HANDLE;
  if(result==VK_SUCCESS)result=vkCreateBuffer(V.device,&create,NULL,&fresh.handle);
  if(result==VK_SUCCESS) {
    VkMemoryRequirements needs;vkGetBufferMemoryRequirements(V.device,fresh.handle,&needs);
    VkMemoryHostPointerPropertiesEXT host={.sType=VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT};
    result=vkGetMemoryHostPointerPropertiesEXT(V.device,VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,p->guest,&host);
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
        .handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,.pHostPointer=p->guest};
      VkMemoryAllocateInfo allocation={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.pNext=&import,
        .allocationSize=bytes,.memoryTypeIndex=type};
      owned_fixture_imports++;
      result=vkAllocateMemory(V.device,&allocation,NULL,&fresh.memory);
      if(result==VK_SUCCESS)result=vkBindBufferMemory(V.device,fresh.handle,fresh.memory,0);
      if(result==VK_SUCCESS) {
        void *mapped=NULL;result=vkMapMemory(V.device,fresh.memory,0,bytes,0,&mapped);
        if(result==VK_SUCCESS){fresh.owned_host_mapped=1;if(!mapped)result=VK_ERROR_MEMORY_MAP_FAILED;}
      }
      fresh.properties=V.mem.memoryTypes[type].propertyFlags;
    }
  }
  if(result!=VK_SUCCESS) {
    vk_buffer_destroy(&fresh); // unpin before exclusion release
    if(!nvn_dirty_owned_release(&certificate->exclusion))die("Vulkan owned failed import exclusion release");
    certificate->retired=1;p->owned_readonly=certificate; // sticky, no second GUP
    pthread_mutex_unlock(&V.resources);return 0;
  }
  pool_materialize_metadata(p);p->owned_readonly=certificate;
  p->whole=(pool_backing_span){.begin=0,.end=bytes,.generation=++p->backing_generation,.buffer=fresh};
  p->backings=&p->whole;
  if(atomic_fetch_add_explicit(&pool_registry_epoch,1,memory_order_release)==UINT64_MAX)die("Vulkan owned registry overflow");
  LOG("[owned NVN fixture] imported bytes=%llu flags=%#x atom=%llu",(unsigned long long)bytes,
    fresh.properties,(unsigned long long)V.props.limits.nonCoherentAtomSize);
  pthread_mutex_unlock(&V.resources);return 1;
}
static void owned_fixture_flush(pool *p,uint64_t first,uint64_t end) {
  vk_buffer *b=&p->whole.buffer;uint64_t offset=first<<GPU_ACCESS_SHIFT,limit=end<<GPU_ACCESS_SHIFT;
  if(limit>p->size)limit=p->size;
  if(!b->owned_host_import || !b->owned_host_mapped || offset>=limit)die("Vulkan owned mapped read bounds");
  if(b->properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)return;
  uint64_t atom=V.props.limits.nonCoherentAtomSize;
  if(!atom || (atom&(atom-1)) || b->size%atom || limit>UINT64_MAX-(atom-1))die("Vulkan owned flush atom");
  offset&=~(atom-1);limit=(limit+atom-1)&~(atom-1);
  if(limit>b->size)die("Vulkan owned flush extent");
  VkMappedMemoryRange range={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=b->memory,.offset=offset,.size=limit-offset};
  VK_CHECK(vkFlushMappedMemoryRanges(V.device,1,&range));
}
// Explicit owner maintenance only, before any physical borrow. Readonly GPU
// use cannot make guest contents stale. Allocate privately first, retire ALL
// original submissions/effects/refs, rebuild mirror+shadow, free/unpin, release.
static int owned_fixture_retire(uint64_t address,uint64_t size,uint64_t expected) {
  if(!owned_fixture_requested || !size || size>UINT64_MAX-address)return 0;
  pthread_mutex_lock(&V.resources);pool *p=pool_find(address,size);
  if(!p && !expected)for(pool *q=pools;q;q=q->next)
    if(q->owned_readonly && !q->owned_readonly->retired &&
        address<(uintptr_t)q->guest+q->size && (uintptr_t)q->guest<address+size) {p=q;break;}
  if(!p || (expected && p->identity!=expected)) {pthread_mutex_unlock(&V.resources);return expected?-1:0;}
  if(!p->owned_readonly || p->owned_readonly->retired) {pthread_mutex_unlock(&V.resources);return 0;}
  uint64_t base=(uintptr_t)p->guest,bytes=p->size,identity=p->identity;
  pthread_mutex_unlock(&V.resources);
  if(pool_operation_depth || pool_maintenance || vk_shader_operation_active() ||
      vk_texture_binding_scope_active() || vk_texture_shader_collecting())die("Vulkan owned missed outer admission");
  pool_backing_span replacement={0};VK_CHECK(pool_span_try_allocate(&replacement,0,bytes));
  int reopen=V.cmd!=VK_NULL_HANDLE,was_stream=stream_recording;pool_maintenance=1;
  if(V.cmd){vk_render_end_preserve();vk_internal_wait(vk_internal_commit());}
  stream_recording=0;mtl_finish();reclaim_submissions();
  pthread_mutex_lock(&V.resources);p=pool_find(base,bytes);
  if(!p || p->identity!=identity || p->references || !p->owned_readonly || p->owned_readonly->retired ||
      p->backings!=&p->whole || !p->whole.buffer.owned_host_import)die("Vulkan owned retirement lifetime");
  if(p->backing_generation==UINT64_MAX || V.buffer_epoch==UINT64_MAX)die("Vulkan owned physical generation overflow");
  pthread_mutex_lock(&pool_publication_mu);
  memcpy(replacement.buffer.map,p->guest,bytes);memcpy(p->shadow,p->guest,bytes);
  memset(p->page_upload_stamps,0,((bytes+4095)>>12)*sizeof *p->page_upload_stamps);
  p->initialized_first_page=p->initialized_end_page=0;
  // Leave initialized bitmap unset: original per-page initialization and
  // authority publication run again after this physical ownership change.
  memset(p->initialized_pages,0,(((bytes-1)>>(GPU_ACCESS_SHIFT+6))+1)*sizeof *p->initialized_pages);
  pthread_mutex_unlock(&pool_publication_mu);
  vk_buffer prior=p->whole.buffer;p->whole.buffer=replacement.buffer;
  p->whole.generation=++p->backing_generation;p->locked_span_hint=NULL;V.buffer_epoch++;
  if(atomic_fetch_add_explicit(&pool_registry_epoch,1,memory_order_release)==UINT64_MAX)die("Vulkan owned registry overflow");
  vk_buffer_destroy(&prior);
  if(!nvn_dirty_owned_release(&p->owned_readonly->exclusion))die("Vulkan owned retirement exclusion release");
  p->owned_readonly->retired=1;owned_fixture_retirements++;
  pthread_mutex_unlock(&V.resources);stream_recording=was_stream;
  if(was_stream){stream_last_draw=V.draws;stream_head_pending=0;}
  pool_maintenance=0;if(reopen){mtl_sub_begin();vk_ghost_flush();}return 1;
}
int vk_test_pool_owned_retire(uint64_t address,uint64_t size) {return owned_fixture_retire(address,size,0);}
void vk_test_pool_owned_stats(uint64_t *imports,uint64_t *retirements) {
  *imports=owned_fixture_imports;*retirements=owned_fixture_retirements;
}
int vk_test_pool_owned_properties(uint64_t address) {
  pthread_mutex_lock(&V.resources);pool *p=pool_find(address,1);
  int result=p && p->owned_readonly && !p->owned_readonly->retired?p->whole.buffer.properties:0;
  pthread_mutex_unlock(&V.resources);return result;
}

void vk_test_pool_owned_destination_check(uint64_t address,uint64_t size) {
  if(!owned_fixture_requested || !size)return;
  if(size>UINT64_MAX-address)die("Vulkan owned completion extent overflow");
  pthread_mutex_lock(&V.resources);
  for(pool *p=pools;p;p=p->next)if(p->owned_readonly && !p->owned_readonly->retired &&
      address<(uintptr_t)p->guest+p->size && (uintptr_t)p->guest<address+size)
    die("Vulkan owned CPU effect missed outer retirement");
  pthread_mutex_unlock(&V.resources);
}
int vk_test_pool_owned_shadow_matches(uint64_t address,const void *data,size_t bytes) {
  pthread_mutex_lock(&V.resources);pool *p=pool_find(address,bytes);
  int result=p && p->shadow && !memcmp(p->shadow+address-(uintptr_t)p->guest,data,bytes);
  pthread_mutex_unlock(&V.resources);return result;
}
