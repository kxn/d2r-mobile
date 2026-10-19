// Read original pool bytes with a real GPU copy and explicit CPU visibility.
// Expected values are supplied independently by the fixture, not by a second
// implementation of the initialization cache.
static void initialized_span_gpu_read(uint8_t *guest,unsigned size,uint8_t *result) {
  vk_buffer *source;uint64_t offset;
  CHECK(vk_pool_address_stage((uintptr_t)guest,size,(void **)&source,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
  CHECK(offset+size<=source->size);
  vk_buffer *readback=vk_temp_buffer(size);CHECK(readback->properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  vk_barrier();VkBufferCopy region={.srcOffset=offset,.size=size};
  vkCmdCopyBuffer(V.cmd,source->handle,readback->handle,1,&region);vk_buffer_transfer_written();
  VkBufferMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,
    .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
    .buffer=readback->handle,.offset=0,.size=size};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&host,0,NULL);
  vk_effect_copy(result,readback->map,size);
}
static void initialized_span_test(void) {
  enum { page=4096,pool_bytes=70*page+193,allocation_bytes=pool_bytes+65536,capacity=65536 };
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,allocation_bytes));
  uint8_t *result=malloc(capacity),*expected=malloc(capacity);CHECK(result&&expected);
  for(unsigned lifetime=0;lifetime<2;lifetime++) {
    dm_guest_write_prepare(storage,allocation_bytes);
    for(unsigned i=0;i<allocation_bytes;i++)storage[i]=(uint8_t)(0x21+lifetime*29+(i/page)*7+i*13);
    uint8_t tail_guard[32];memcpy(tail_guard,storage+pool_bytes,sizeof tail_guard);
    uint64_t baseline=vk_pool_initialized_bytes();
    NVNmemoryPoolBuilder builder;NVNmemoryPool pool;
    CALL(nvnMemoryPoolBuilderSetDefaults,&builder);CALL(nvnMemoryPoolBuilderSetDevice,&builder,&device);
    CALL(nvnMemoryPoolBuilderSetStorage,&builder,storage,pool_bytes);
    CALL(nvnMemoryPoolBuilderSetFlags,&builder,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
    CHECK(CALL(nvnMemoryPoolInitialize,&pool,&builder));
    int eager=V.options.pool_mirror_reference;
    CHECK(vk_pool_initialized_bytes()==baseline+(eager?pool_bytes:0));
    // Disjoint pages1 and3 must not prove the untouched page2 between them.
    // Partial reads initialize complete logical pages; the odd final page
    // initializes only its193 valid bytes, without touching the allocation guard.
    const struct { unsigned off,size,initialized; } reads[]={
      {page+32,64,page},{3*page+16,128,2*page},{page+40,8,2*page},
      {2*page+24,96,3*page},{2*page+12,2*page+64,4*page},{3*page+80,32,4*page},
      {63*page+4048,96,6*page},{64*page+8,32,6*page},
      {70*page,193,6*page+193},{70*page+177,16,6*page+193}
    };
    mtl_sub_begin();unsigned cursor=0;
    for(unsigned n=0;n<sizeof reads/sizeof *reads;n++) {
      CHECK(cursor+reads[n].size<=capacity);
      memcpy(expected+cursor,storage+reads[n].off,reads[n].size);
      initialized_span_gpu_read(storage+reads[n].off,reads[n].size,result+cursor);
      CHECK(vk_pool_initialized_bytes()==baseline+(eager?pool_bytes:reads[n].initialized));
      cursor+=reads[n].size;
    }
    uint64_t serial=vk_internal_commit();vk_internal_wait(serial);
    for(unsigned i=0;i<cursor;i++)CHECK(result[i]==expected[i]);
    CHECK(!memcmp(storage+pool_bytes,tail_guard,sizeof tail_guard));
    // The cached tail survives a physical submission boundary. CPU edits to
    // these initialized pages must still be uploaded through dirty tracking.
    dm_guest_write_prepare(storage+70*page,193);memset(storage+70*page,0x5b+lifetime,193);
    dm_guest_write_prepare(storage+page,page);memset(storage+page,0x62+lifetime,page);
    dm_guest_write_prepare(storage+4*page,page);memset(storage+4*page,0x64+lifetime,page);
    dm_guest_write_prepare(storage+8*page,page);memset(storage+8*page,0x88+lifetime,page);
    mtl_sub_begin();cursor=0;
    const unsigned edit_off[]={70*page,page,4*page,8*page,8*page};
    const unsigned edit_size[]={193,page,page,317,page};
    for(unsigned n=0;n<5;n++) {
      memcpy(expected+cursor,storage+edit_off[n],edit_size[n]);
      initialized_span_gpu_read(storage+edit_off[n],edit_size[n],result+cursor);cursor+=edit_size[n];
      unsigned total=6*page+193+(n>=3?page:0);
      CHECK(vk_pool_initialized_bytes()==baseline+(eager?pool_bytes:total));
    }
    serial=vk_internal_commit();vk_internal_wait(serial);
    for(unsigned i=0;i<cursor;i++)CHECK(result[i]==expected[i]);
    // Complete a GPU-only partial write while guest/shadow still contain old
    // CPU bytes, then enter another submission through the cached page span.
    mtl_sub_begin();vk_buffer *destination;uint64_t offset;
    CHECK(vk_pool_address_overwrite((uintptr_t)storage+8*page+64,16,(void **)&destination,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
    vk_buffer *upload=vk_temp_buffer(16);uint8_t gpu_value[16];
    for(unsigned i=0;i<16;i++)gpu_value[i]=0xd0+lifetime+i;
    memcpy(upload->map,gpu_value,16);
    const vk_buffer_use use={(uintptr_t)storage+8*page+64,16,1};vk_transfer_buffer_dependencies(&use,1);
    VkBufferCopy write={.dstOffset=offset,.size=16};vkCmdCopyBuffer(V.cmd,upload->handle,destination->handle,1,&write);
    vk_buffer_transfer_written_range((uintptr_t)storage+8*page+64,16);
    serial=vk_internal_commit();vk_internal_wait(serial);
    for(unsigned i=0;i<16;i++)CHECK(storage[8*page+64+i]==0x88+lifetime);
    // A cached lookup must preserve completed GPU bytes even though the guest
    // still holds their old values. This check applies to both upload modes.
    memcpy(expected,storage+8*page+32,96);memcpy(expected+32,gpu_value,16);
    mtl_sub_begin();initialized_span_gpu_read(storage+8*page+32,96,result);
    CHECK(vk_pool_initialized_bytes()==baseline+(eager?pool_bytes:7*page+193));
    uint64_t full,used,protected_bytes;int pool_reference;
    vk_pool_sync_policy_stats(&full,&used,&protected_bytes,&pool_reference);
    int partial_upload=vk_async_submissions()&&!pool_reference&&vk_submission_barrier_mode()==0;
    // The serial/reference uploader intentionally copies a complete dirty
    // page. Publish GPU bytes through the normal writeback API before making
    // that page CPU-dirty; an unreported raw write has no preservation contract
    // in that mode. The production partial uploader keeps the stronger case:
    // the GPU-only bytes remain absent from guest/shadow across the CPU edit.
    if(!partial_upload)
      vk_buffer_writeback_stage((uintptr_t)storage+8*page+64,16,VK_PIPELINE_STAGE_TRANSFER_BIT);
    serial=vk_internal_commit();vk_internal_wait(serial);
    for(unsigned i=0;i<96;i++)CHECK(result[i]==expected[i]);
    for(unsigned i=0;i<16;i++)
      CHECK(storage[8*page+64+i]==(partial_upload?0x88+lifetime:gpu_value[i]));
    // New CPU bytes in the SAME page must coexist with the completed GPU result.
    dm_guest_write_prepare(storage+8*page+48,8);memset(storage+8*page+48,0xe1+lifetime,8);
    memcpy(expected,storage+8*page+32,96);memcpy(expected+32,gpu_value,16);
    mtl_sub_begin();initialized_span_gpu_read(storage+8*page+32,96,result);
    CHECK(vk_pool_initialized_bytes()==baseline+(eager?pool_bytes:7*page+193));
    serial=vk_internal_commit();vk_internal_wait(serial);
    for(unsigned i=0;i<96;i++)CHECK(result[i]==expected[i]);
    CHECK(!memcmp(storage+pool_bytes,tail_guard,sizeof tail_guard));
    CALL(nvnMemoryPoolFinalize,&pool);CHECK(vk_pool_initialized_bytes()==baseline);
    // Next iteration re-registers this exact guest address, with new contents.
    // A cache from the destroyed pool must not suppress its first snapshot.
  }
  free(result);free(expected);free(storage);
  LOG("[vulkan initialized span fixture] real GPU bytes: disjoint gap, partial/adjacent/subranges, bitmap word boundary,193-byte tail/guard, cross-submit CPU edits, cached GPU-only result, same-page CPU edit (raw async/published reference), and same-address pool recreation passed");
}
