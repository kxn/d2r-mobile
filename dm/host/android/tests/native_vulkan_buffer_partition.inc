// Real copies retain producer intervals. Queries change address/size/stages
// independently; expected GPU bytes come from the copy operations, never the
// search/cache implementation. No private counter or timing hook is needed.
static void buffer_partition_copy(uint8_t *storage,uint8_t *expected,
                                  unsigned destination,unsigned source,unsigned size) {
  mtl_sub_copy((uintptr_t)storage+0x10000+source,
               (uintptr_t)storage+0x20000+destination,size);
  memcpy(expected+destination,storage+0x10000+source,size);
}
// Deliberately warm the old partition AFTER copy preparation, immediately
// before publishing a real writer. Ordinary copy preparation can itself run
// searches, so warming only before mtl_sub_copy would miss this stale-index
// failure. This query observes history only; no GPU read is submitted early.
static void buffer_partition_extend(uint8_t *storage,uint8_t *expected) {
  uint64_t src=(uintptr_t)storage+0x10000+128,dst=(uintptr_t)storage+0x20000+8;
  vk_buffer *source,*destination;uint64_t source_offset,destination_offset;
  CHECK(vk_pool_address_stage(src,16,(void **)&source,&source_offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
  CHECK(vk_pool_address_overwrite(dst,16,(void **)&destination,&destination_offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
  const vk_buffer_use uses[]={{src,16,0},{dst,16,1}};
  vk_transfer_buffer_dependencies(uses,2);
  VkBufferCopy copy={.srcOffset=source_offset,.dstOffset=destination_offset,.size=16};
  vkCmdCopyBuffer(V.cmd,source->handle,destination->handle,1,&copy);
  CHECK(!vk_buffer_vertex_pending((uintptr_t)storage+0x20000+16,1));
  vk_buffer_transfer_written_range(dst,16);
  vk_pool_overwritten(dst,16);
  vk_buffer_writeback_stage(dst,16,VK_PIPELINE_STAGE_TRANSFER_BIT);
  memcpy(expected+8,storage+0x10000+128,16);
}
static void buffer_partition_gpu_read(uint8_t *storage,const uint8_t *expected,unsigned size) {
  vk_buffer *source;uint64_t offset;
  CHECK(vk_pool_address_stage((uintptr_t)storage+0x20000,size,(void **)&source,&offset,
                             VK_PIPELINE_STAGE_TRANSFER_BIT));
  vk_buffer_shader_read((uintptr_t)storage+0x20000,size,VK_PIPELINE_STAGE_TRANSFER_BIT);
  vk_buffer *copy=vk_temp_buffer(size);
  CHECK(copy->properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VkBufferCopy region={.srcOffset=offset,.size=size};
  vkCmdCopyBuffer(V.cmd,source->handle,copy->handle,1,&region);
  VkBufferMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,
    .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
    .buffer=copy->handle,.offset=0,.size=size};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,
                       0,0,NULL,1,&host,0,NULL);
  uint8_t *actual=malloc(size);CHECK(actual!=NULL);memset(actual,0xcd,size);
  vk_effect_copy(actual,copy->map,size);vk_internal_wait(vk_internal_commit());
  for(unsigned n=0;n<size;n++)CHECK(actual[n]==expected[n]);
  free(actual);
}
static void buffer_partition_test(void) {
  if(vk_submission_barrier_mode()!=0) {
    LOG("[vulkan buffer partition fixture] skipped: full submission barrier reference");return;
  }
  enum { bytes_to_check=8192 };
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,1<<20));
  memset(storage,0xa5,1<<20);
  for(unsigned n=0;n<bytes_to_check;n++)storage[0x10000+n]=(uint8_t)(n*29+(n>>4)+7);
  uint8_t expected[bytes_to_check];memset(expected,0xa5,sizeof expected);
  NVNmemoryPoolBuilder builder;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&builder);CALL(nvnMemoryPoolBuilderSetDevice,&builder,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&builder,storage,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&builder,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&builder));
  uint64_t base=(uintptr_t)storage+0x20000;
  mtl_sub_begin();vk_barrier();
  CHECK(!vk_buffer_vertex_pending(base,1));
  CHECK(!vk_buffer_vertex_pending(0,1));CHECK(!vk_buffer_vertex_pending(base,0));
  for(unsigned n=0;n<3;n++)buffer_partition_copy(storage,expected,n*64,n*32,16);
  // Same partition, different size: an earlier negative answer must not hide
  // the next interval when the read grows across its start.
  CHECK(!vk_buffer_vertex_pending(base-1,1));
  CHECK(vk_buffer_vertex_pending(base-1,2));
  CHECK(vk_buffer_vertex_pending(base,1));CHECK(vk_buffer_vertex_pending(base+15,1));
  CHECK(!vk_buffer_vertex_pending(base+16,1));
  CHECK(!vk_buffer_vertex_pending(base+63,1));CHECK(vk_buffer_vertex_pending(base+63,2));
  CHECK(vk_buffer_vertex_pending(base+64,16));
  CHECK(!vk_buffer_vertex_pending(base+80,48));CHECK(vk_buffer_vertex_pending(base+80,49));
  CHECK(vk_buffer_vertex_pending(base+143,1));CHECK(!vk_buffer_vertex_pending(base+144,1));
  CHECK(!vk_buffer_vertex_pending(UINT64_MAX,1));CHECK(!vk_buffer_vertex_pending(base+144,16));
  // Same interval count, changed endpoint: warm the old middle partition,
  // extend the first real write through it, then require its new overlap.
  CHECK(!vk_buffer_vertex_pending(base+16,1));
  buffer_partition_extend(storage,expected);
  CHECK(vk_buffer_vertex_pending(base+16,1));CHECK(!vk_buffer_vertex_pending(base+24,1));
  // Visibility may change without geometry invalidation. The returned index
  // cannot stand in for the old overlap/stage predicate.
  CHECK(vk_buffer_vertex_pending(base+64,1));
  vk_buffer_producers_visible(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);
  CHECK(vk_buffer_vertex_pending(base+64,1)); // VERTEX_INPUT is still uncovered.
  vk_buffer_producers_visible(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT);
  CHECK(!vk_buffer_vertex_pending(base+64,1));
  // Reset and reconstruct the same count/sizes at different addresses.
  CHECK(!vk_buffer_vertex_pending(base+144,1));
  vk_barrier();CHECK(!vk_buffer_vertex_pending(base,256));
  for(unsigned n=0;n<3;n++)buffer_partition_copy(storage,expected,32+n*64,256+n*32,16);
  CHECK(vk_buffer_vertex_pending(base+32,1));CHECK(!vk_buffer_vertex_pending(base+176,1));
  // Bridge three disjoint histories into one. A stale tail index would skip
  // this newly written former gap (and the following real GPU read).
  buffer_partition_copy(storage,expected,32,512,144);
  CHECK(vk_buffer_vertex_pending(base+80,1));CHECK(vk_buffer_vertex_pending(base+175,1));
  CHECK(!vk_buffer_vertex_pending(base+176,1));
  buffer_partition_gpu_read(storage,expected,bytes_to_check);

  mtl_sub_begin();vk_barrier();CHECK(!vk_buffer_vertex_pending(base,1));
  // The bounded table must remain conservative on its257th separate writer;
  // the partition cache must not bypass overflow or survive the full barrier.
  for(unsigned n=0;n<257;n++) {
    buffer_partition_copy(storage,expected,1024+n*16,4096+n*4,4);
    if(!n)CHECK(!vk_buffer_vertex_pending(base+7000,1)); // Warm a tail proof.
  }
  CHECK(vk_buffer_vertex_pending(base+7000,1));
  vk_buffer_shader_read(base+7000,1,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);
  CHECK(!vk_buffer_vertex_pending(base+7000,1));
  buffer_partition_copy(storage,expected,7000,6000,4);
  CHECK(vk_buffer_vertex_pending(base+7000,1));
  buffer_partition_gpu_read(storage,expected,bytes_to_check);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);
  LOG("[vulkan buffer partition fixture] empty/first/middle/tail boundaries, changed read sizes, visibility, endpoint/count/merge/clear/overflow mutations and16384 actual GPU bytes passed");
}
