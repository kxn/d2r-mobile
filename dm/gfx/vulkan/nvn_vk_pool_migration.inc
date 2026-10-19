// Cold recording-owner primitive used by explicit window-policy maintenance
// (default allocation remains whole). The caller must relinquish all raw VkBuffer/map/view
// borrows before entry; active operation scopes are rejected rather than ended.
// An isolated ordinary internal submission preserves device bytes, even when
// they differ from guest/shadow. New CPU uploads occur only after it completes.
#ifdef DM_VULKAN_TEST_EMBED
static uint64_t pool_migration_test_copy_bytes;
uint64_t vk_test_pool_migration_copy_bytes(void) { return pool_migration_test_copy_bytes; }
#endif
// Initialized pages have been exposed through an actual nonempty pool buffer
// lookup/writeback, including overwrite/GPU-only writers. Dirty generations or
// CPU-shadow authority cannot substitute: GPU results need not match either.
// Reserved untouched pages need no preservation; their first lookup will still
// initialize from guest in the replacement. Do not change the original bitmap.
static void pool_migration_copy_initialized(const pool *p,const pool_backing_span *b,
    VkBuffer destination) {
  if(!p->initialized_pages)return;
  enum {BATCH=64};VkBufferCopy copies[BATCH];unsigned count=0;
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
      VkBufferCopy copy={.srcOffset=lo-b->begin,.dstOffset=lo,.size=hi-lo};
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
}
static int pool_migrate_whole_quiescent(uint64_t address) {
  if(V.cmd || recording_resources || stream_recording || vk_render_active() ||
     vk_counter_active() || vk_shader_operation_active() || vk_texture_binding_scope_active() ||
     vk_texture_shader_collecting() || vk_mip_clear_pending())return 0;
  mtl_finish();reclaim_submissions();
  pthread_mutex_lock(&V.resources);
  pool *p=pool_find(address,1);
  if(!p || (uintptr_t)p->guest!=address || p->references || !p->backings ||
     (!p->backings->next && p->backings->begin==0 && p->backings->end==p->size)) {
    pthread_mutex_unlock(&V.resources);return 0;
  }
  // Refuse malformed ownership before allocating or changing any backing.
  // Holes are legal: unmaterialized pages stay uninitialized in the new buffer.
  uint64_t last=0;unsigned count=0;
  for(pool_backing_span *b=p->backings;b;b=b->next) {
    if(!b->buffer.handle || !b->generation || b->begin<last || b->begin>=b->end ||
       b->end>p->size || b->buffer.size!=b->end-b->begin) {
      pthread_mutex_unlock(&V.resources);return 0;
    }
    last=b->end;count++;
  }
  if(!count) { pthread_mutex_unlock(&V.resources);return 0; }
  if(p->backing_generation==UINT64_MAX) {pthread_mutex_unlock(&V.resources);return 0;}
  pool_backing_span replacement={0};
  if(pool_span_try_allocate(&replacement,0,p->size)!=VK_SUCCESS) {
    pthread_mutex_unlock(&V.resources);return 0;
  }
  // Publish generation only with the successful replacement, not on OOM or
  // after the old logical pool was removed while its isolated copy completed.
  replacement.generation=p->backing_generation+1;
  pool_backing_span *original=p->backings;
  uint64_t identity=p->identity;
  // This non-recording pin also protects p if another registry caller removes
  // it while the isolated GPU submission runs. It is not a physical borrower.
  p->references++;
  pthread_mutex_unlock(&V.resources);

#ifdef DM_VULKAN_TEST_EMBED
  pool_migration_test_copy_bytes=0;
#endif
  begin_submission(1);
  VkMemoryBarrier source={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
                       0,1,&source,0,NULL,0,NULL);
  for(pool_backing_span *b=original;b;b=b->next) {
    pthread_mutex_lock(&V.resources);
    // GPU-only read: preserve ordinary refs/access metadata but never request
    // CPU initial contents. Migration is not a guest overwrite or publication.
    record_pool_use(p,b->begin,b->end-b->begin,VK_PIPELINE_STAGE_TRANSFER_BIT,0);
    pthread_mutex_unlock(&V.resources);
    pool_migration_copy_initialized(p,b,replacement.buffer.handle);
  }
  VkBufferMemoryBarrier destination={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,
    .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
    .buffer=replacement.buffer.handle,.offset=0,.size=VK_WHOLE_SIZE};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       0,0,NULL,1,&destination,0,NULL);
  uint64_t serial=vk_internal_commit();vk_internal_wait(serial);
  // GPU+effects completion alone is insufficient: retained VkBufferViews and
  // command buffers may still refer to old storage until reset AND recycle.
  mtl_finish();reclaim_submissions();
  pthread_mutex_lock(&V.resources);
  if(pool_find(address,1)!=p || p->identity!=identity || p->backings!=original || p->references!=1) {
    // Never publish against a removed/replaced logical pool. An explicit pin
    // kept both the old storage and p alive; release it only after new storage.
    vk_buffer_destroy(&replacement.buffer);
    if(!p->references)die("Vulkan migration pool pin underflow");
    p->references--;pthread_cond_broadcast(&pool_cv);pthread_mutex_unlock(&V.resources);return 0;
  }
  pool_span_chain_destroy(p,original);
  p->whole=replacement;p->backings=&p->whole;p->backing_generation=replacement.generation;
  pool_cpu_migrated(p);
  p->access_serial=0;p->recording_access=NULL;
  if(atomic_fetch_add_explicit(&pool_registry_epoch,1,memory_order_release)==UINT64_MAX)
    die("Vulkan migration pool registry epoch overflow");
  if(V.buffer_epoch==UINT64_MAX)die("Vulkan migration buffer epoch overflow");
  V.buffer_epoch++;
  uint64_t migrated_size=p->size;
  p->references--;pthread_cond_broadcast(&pool_cv);pthread_mutex_unlock(&V.resources);
  // Old physical histories cannot prove a named dependency on the new buffer.
  // Retain a conservative logical transfer producer (no recording owner), not
  // stale VkBuffer identity. Other pools' histories remain fully represented.
  pool_cpu_preserving++;
  vertex_buffer_produced(address,migrated_size,VK_PIPELINE_STAGE_TRANSFER_BIT);
  pool_cpu_preserving--;
  return 1;
}
#ifdef DM_VULKAN_TEST_EMBED
int vk_test_pool_migrate_whole(uint64_t address) {
  return pool_migrate_whole_quiescent(address);
}
#endif
