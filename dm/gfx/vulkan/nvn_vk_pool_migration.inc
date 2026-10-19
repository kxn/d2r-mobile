// Cold recording-owner primitive used by explicit window-policy maintenance
// (default allocation remains whole). The caller must relinquish all raw VkBuffer/map/view
// borrows before entry; active operation scopes are rejected rather than ended.
// An isolated ordinary internal submission preserves device bytes, even when
// they differ from guest/shadow. New CPU uploads occur only after it completes.
#ifdef DM_VULKAN_TEST_EMBED
static uint64_t pool_migration_test_copy_bytes;
uint64_t vk_test_pool_migration_copy_bytes(void) { return pool_migration_test_copy_bytes; }
static void (*pool_migration_test_before_publish)(void *);
static void (*pool_migration_test_before_copy)(void *);
static void *pool_migration_test_context;
void vk_test_pool_migration_copy_hook(void (*fn)(void *),void *context) {
  pool_migration_test_before_copy=fn;pool_migration_test_context=context;
}
void vk_test_pool_migration_publish_hook(void (*fn)(void *),void *context) {
  pool_migration_test_before_publish=fn;pool_migration_test_context=context;
}
#endif
// Initialized pages have been exposed through an actual nonempty pool buffer
// lookup/writeback, including overwrite/GPU-only writers. Dirty generations or
// CPU-shadow authority cannot substitute: GPU results need not match either.
// Reserved untouched pages need no preservation; their first lookup will still
// initialize from guest in the replacement. Do not change the original bitmap.
static void pool_migration_copy_initialized(const pool *p,const pool_backing_span *b,
    VkBuffer destination,uint64_t destination_begin) {
  if(!p->initialized_pages)return;
  enum {BATCH=64};VkBufferCopy copies[BATCH];unsigned count=0;uint64_t copy_bytes=0;
  uint64_t first=b->begin>>GPU_ACCESS_SHIFT,last=(b->end-1)>>GPU_ACCESS_SHIFT;
  for(uint64_t word=first/64;word<=last/64;word++) {
    unsigned low=word==first/64?first%64:0,high=word==last/64?last%64:63;
    uint64_t bits=p->initialized_pages[word]&((UINT64_MAX<<low)&(UINT64_MAX>>(63-high)));
    while(bits) {
      unsigned bit=__builtin_ctzll(bits),end=bit;
      while(end<63 && (bits&(UINT64_C(1)<<(end+1))))end++;
      uint64_t lo=(word*64+bit)<<GPU_ACCESS_SHIFT,hi=(word*64+end+1)<<GPU_ACCESS_SHIFT;
      if(lo<b->begin)lo=b->begin;
      if(hi>b->end)hi=b->end; // includes the final non-page-sized pool tail
      if(b->buffer.imported_host)pool_host_flush_read(b,lo,hi-lo);
      if(V.options.pool_copy_accounting)copy_bytes+=hi-lo;
      VkBufferCopy copy={.srcOffset=lo-b->begin,.dstOffset=lo-destination_begin,.size=hi-lo};
      if(count && copies[count-1].srcOffset+copies[count-1].size==copy.srcOffset)
        copies[count-1].size+=copy.size;
      else {
        if(count==BATCH) {
          vkCmdCopyBuffer(V.cmd,b->buffer.handle,destination,count,copies);count=0;
        }
        copies[count++]=copy;
      }
#ifdef DM_VULKAN_TEST_EMBED
      pool_migration_test_copy_bytes+=copy.size;
#endif
      bits&=~((UINT64_MAX<<bit)&(UINT64_MAX>>(63-end)));
    }
  }
  if(count)vkCmdCopyBuffer(V.cmd,b->buffer.handle,destination,count,copies);
  POOL_COPY_ADD(migration_gpu_recorded_bytes,copy_bytes);
}
// Keep the exact declared binding contiguous, including its descriptor prefix.
// Only intersecting old nodes join this component; adjacent nodes retain their
// exact physical identity. This is not eviction or a smaller shader binding.
static int pool_migration_bounds(const pool *p,const vk_pool_requirement *r,uint64_t window,
    uint64_t *begin,uint64_t *end) {
  if(!r) {*begin=0;*end=p->size;return 1;}
  if(!pool_window_power_of_two(window) || window<4096 || !r->size ||
      r->address<(uintptr_t)p->guest || r->address-(uintptr_t)p->guest>=p->size ||
      r->size>p->size-(r->address-(uintptr_t)p->guest) ||
      (r->prefix_alignment && (!pool_window_power_of_two(r->prefix_alignment) || r->prefix_alignment>window)))return 0;
  uint64_t logical=r->address-(uintptr_t)p->guest;
  uint64_t prefix=r->prefix_alignment?logical&~(r->prefix_alignment-1):logical;
  *begin=prefix&~(window-1);*end=logical+r->size;
  uint64_t pad=(-*end)&(window-1);
  *end=p->size-*end<pad?p->size:*end+pad;
  int changed;
  do {
    changed=0;
    for(const pool_backing_span *b=p->backings;b;b=b->next)if(b->begin<*end && *begin<b->end) {
      if(b->begin<*begin) {*begin=b->begin;changed=1;}
      if(b->end>*end) {*end=b->end;changed=1;}
    }
  } while(changed);
  return *begin<*end && *end<=p->size;
}
static int pool_migrate_quiescent(uint64_t address,uint64_t expected_identity,
    const vk_pool_requirement *trigger,uint64_t window) {
  if(V.cmd || recording_resources || stream_recording || vk_render_active() ||
     vk_counter_active() || vk_shader_operation_active() || vk_texture_binding_scope_active() ||
     vk_texture_shader_collecting() || vk_mip_clear_pending())return 0;
  mtl_finish();reclaim_submissions();
  pthread_mutex_lock(&V.resources);
  pool *p=pool_find(address,1);
  if(!p || (uintptr_t)p->guest!=address || (expected_identity && p->identity!=expected_identity) ||
      p->references || !p->backings || pool_window_whole(p) || !pool_window_chain_valid(p)) {
    pthread_mutex_unlock(&V.resources);return 0;
  }
  uint64_t begin,end;
  if(!pool_migration_bounds(p,trigger,window,&begin,&end) || p->backing_generation==UINT64_MAX) {
    pthread_mutex_unlock(&V.resources);return 0;
  }
  pool_backing_span **link=&p->backings;
  while(*link && (*link)->end<=begin)link=&(*link)->next;
  pool_backing_span *first=*link,*after=first;
  while(after && after->begin<end)after=after->next;
  // Snapshot selected immutable nodes while holding the registry lock. New
  // disjoint reservations may append nodes during the isolated copy; walking
  // their mutable next links outside the lock could escape the destination.
  size_t selected_count=0;
  for(pool_backing_span *b=first;b!=after;b=b->next)selected_count++;
  pool_backing_span **selected=selected_count?calloc(selected_count,sizeof *selected):NULL;
  if(selected_count && !selected) {pthread_mutex_unlock(&V.resources);return 0;}
  size_t selected_index=0;
  for(pool_backing_span *b=first;b!=after;b=b->next)selected[selected_index++]=b;
  // Heap ownership is necessary: an outside node may still use p->whole.
  pool_backing_span *replacement=calloc(1,sizeof *replacement);
  VkResult allocated=replacement?VK_SUCCESS:VK_ERROR_OUT_OF_HOST_MEMORY;
#ifdef DM_VULKAN_TEST_EMBED
  if(allocated==VK_SUCCESS && pool_reserve_fail_after==0)allocated=VK_ERROR_OUT_OF_DEVICE_MEMORY;
#endif
  if(allocated==VK_SUCCESS)allocated=pool_span_try_allocate(replacement,begin,end);
  if(allocated!=VK_SUCCESS) {
    free(replacement);free(selected);pthread_mutex_unlock(&V.resources);return 0;
  }
  replacement->generation=p->backing_generation+1;
  pool_backing_span *original=p->backings;
  uint64_t identity=p->identity,generation=p->backing_generation;
  // Non-recording lifetime pin. No raw physical borrow or queued old effect
  // may survive the prior finish/recycle boundary. It also keeps link alive.
  p->references++;pthread_mutex_unlock(&V.resources);
#ifdef DM_VULKAN_TEST_EMBED
  pool_migration_test_copy_bytes=0;
  if(pool_migration_test_before_copy)pool_migration_test_before_copy(pool_migration_test_context);
#endif
  begin_submission(1);
  VkMemoryBarrier source={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
                       0,1,&source,0,NULL,0,NULL);
  for(size_t k=0;k<selected_count;k++) {
    pool_backing_span *b=selected[k];
    pthread_mutex_lock(&V.resources);
    record_pool_use(p,b->begin,b->end-b->begin,VK_PIPELINE_STAGE_TRANSFER_BIT,0);
    pthread_mutex_unlock(&V.resources);
    pool_migration_copy_initialized(p,b,replacement->buffer.handle,begin);
  }
  VkBufferMemoryBarrier destination={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,
    .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
    .buffer=replacement->buffer.handle,.offset=0,.size=VK_WHOLE_SIZE};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       0,0,NULL,1,&destination,0,NULL);
  uint64_t serial=vk_internal_commit();vk_internal_wait(serial);
  // A fence alone is insufficient: retire effects, views and command buffers.
  mtl_finish();reclaim_submissions();
#ifdef DM_VULKAN_TEST_EMBED
  if(pool_migration_test_before_publish)pool_migration_test_before_publish(pool_migration_test_context);
#endif
  pthread_mutex_lock(&V.resources);
  if(pool_find(address,1)!=p || p->identity!=identity || p->backings!=original ||
      p->backing_generation!=generation || *link!=first || p->references!=1) {
    vk_buffer_destroy(&replacement->buffer);free(replacement);free(selected);
    if(!p->references)die("Vulkan migration pool pin underflow");
    p->references--;pthread_cond_broadcast(&pool_cv);pthread_mutex_unlock(&V.resources);return 0;
  }
  // All hint readers hold this lock; no retired pointer can survive publish.
  p->locked_span_hint=NULL;
  replacement->next=after;*link=replacement;
  for(size_t k=0;k<selected_count;k++) {
    pool_backing_span *b=selected[k];vk_buffer_destroy(&b->buffer);
    if(b!=&p->whole)free(b);
  }
  free(selected);
  p->backing_generation=replacement->generation;pool_cpu_migrated(p);
  p->access_serial=0;p->recording_access=NULL;
  if(atomic_fetch_add_explicit(&pool_registry_epoch,1,memory_order_release)==UINT64_MAX)
    die("Vulkan migration pool registry epoch overflow");
  if(V.buffer_epoch==UINT64_MAX)die("Vulkan migration buffer epoch overflow");
  V.buffer_epoch++;
  p->references--;pthread_cond_broadcast(&pool_cv);pthread_mutex_unlock(&V.resources);
  // Only the new physical component loses named-buffer dependency proofs.
  // Logical dirty pages, shadow, GPU authority and initialized bitmap survive.
  pool_cpu_preserving++;
  vertex_buffer_produced(address+begin,end-begin,VK_PIPELINE_STAGE_TRANSFER_BIT);
  pool_cpu_preserving--;
  return 1;
}
static int pool_migrate_whole_quiescent(uint64_t address) {
  return pool_migrate_quiescent(address,0,NULL,0);
}
#ifdef DM_VULKAN_TEST_EMBED
int vk_test_pool_migrate_whole(uint64_t address) {
  return pool_migrate_whole_quiescent(address);
}
int vk_test_pool_consolidate(uint64_t address,uint64_t identity,
    const vk_pool_requirement *trigger,uint64_t window) {
  return pool_migrate_quiescent(address,identity,trigger,window);
}
#endif
