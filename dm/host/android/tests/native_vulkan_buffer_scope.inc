// Real pool mirrors with adjacent guest addresses, aliases, retained producer
// history, and pool destruction/recreation at the same guest address.
static void buffer_scope_fill(uint64_t address,uint64_t size,uint32_t value) {
  vk_buffer *b;uint64_t offset;
  CHECK(vk_pool_address_stage(address,size,(void **)&b,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
  vkCmdFillBuffer(V.cmd,b->handle,offset,size,value);
  vk_buffer_transfer_written_range(address,size);
}
static void buffer_scope_pixels(uint64_t a,uint64_t b,uint32_t first,uint32_t second,uint32_t third) {
  vk_buffer *source_a,*source_b;uint64_t offset_a,offset_b;
  CHECK(vk_pool_address_stage(a,128,(void **)&source_a,&offset_a,VK_PIPELINE_STAGE_TRANSFER_BIT));
  CHECK(vk_pool_address_stage(b,64,(void **)&source_b,&offset_b,VK_PIPELINE_STAGE_TRANSFER_BIT));
  vk_buffer_shader_read(a,128,VK_PIPELINE_STAGE_TRANSFER_BIT);
  vk_buffer_shader_read(b,64,VK_PIPELINE_STAGE_TRANSFER_BIT);
  vk_buffer *output=vk_temp_buffer(192);
  VkBufferCopy ca={.srcOffset=offset_a,.dstOffset=0,.size=128};
  VkBufferCopy cb={.srcOffset=offset_b,.dstOffset=128,.size=64};
  vkCmdCopyBuffer(V.cmd,source_a->handle,output->handle,1,&ca);
  vkCmdCopyBuffer(V.cmd,source_b->handle,output->handle,1,&cb);
  vk_barrier();vk_internal_wait(vk_internal_commit());
  const uint32_t *words=(const uint32_t *)output->map;
  for(unsigned k=0;k<48;k++) CHECK(words[k]==(k<16?first:k<32?second:third));
}
static void buffer_scope_test(void) {
  int saved_reference=vk_buffer_memory_reference_state();
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,131072));
  uint64_t a=(uintptr_t)storage+65536-128,b=(uintptr_t)storage+65536;
  VkPipelineStageFlags vertex=VK_PIPELINE_STAGE_VERTEX_INPUT_BIT|VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
  for(int reference=0;reference<2;reference++) {
    memset(storage,0,131072);mtl_pool_add(storage,65536);mtl_pool_add(storage+65536,65536);
    vk_buffer_memory_reference(reference);mtl_sub_begin();vk_barrier();
    // A's last64bytes and B's first64bytes are logically adjacent. They are
    // different physical buffers, so synchronizing A must not retire B.
    buffer_scope_fill(a,64,0x11223344);buffer_scope_fill(a+64,64,0x55667788);
    buffer_scope_fill(b,64,0x99aabbcc);
    CHECK(vk_buffer_vertex_pending(a,64));CHECK(vk_buffer_vertex_pending(a+64,64));
    CHECK(vk_buffer_vertex_pending(b,64));
    vk_buffer_shader_read(a,64,vertex);
    CHECK(!vk_buffer_vertex_pending(a,64));
    CHECK(!vk_buffer_vertex_pending(a+64,64)); // Same physical pool alias.
    CHECK(vk_buffer_vertex_pending(b,64)==!reference);
    buffer_scope_pixels(a,b,0x11223344,0x55667788,0x99aabbcc);
    mtl_pool_del(storage);mtl_pool_del(storage+65536);
  }
  // Leave live producer metadata across true fence completion, then destroy
  // its pool. Registering the identical guest range creates a new mirror.
  memset(storage,0,131072);mtl_pool_add(storage,65536);mtl_pool_add(storage+65536,65536);
  vk_buffer_memory_reference(0);mtl_sub_begin();vk_barrier();
  buffer_scope_fill(a,64,0x01020304);buffer_scope_fill(a+64,64,0x05060708);
  buffer_scope_fill(b,64,0x090a0b0c);
  vk_internal_wait(vk_internal_commit());
  mtl_pool_del(storage);mtl_pool_add(storage,65536);
  mtl_sub_begin();vk_buffer *new_a,*retained_b;uint64_t offset;
  CHECK(vk_pool_address_stage(a,128,(void **)&new_a,&offset,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT));
  CHECK(vk_pool_address_stage(b,64,(void **)&retained_b,&offset,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT));
  CHECK(vk_buffer_vertex_pending(a,64));CHECK(vk_buffer_vertex_pending(b,64));
  vk_buffer_shader_read(a,64,vertex);
  // Old A history has the wrong physical identity. It must retain the global
  // fallback (which also covers B), even if the driver reuses a handle value.
  CHECK(!vk_buffer_vertex_pending(a,64));CHECK(!vk_buffer_vertex_pending(b,64));
  buffer_scope_fill(a,64,0x10203040);buffer_scope_fill(a+64,64,0x50607080);
  buffer_scope_fill(b,64,0x90a0b0c0);
  vk_buffer_shader_read(a,64,vertex);
  CHECK(!vk_buffer_vertex_pending(b,64)); // Overlapping old/new identities stay unknown.
  buffer_scope_pixels(a,b,0x10203040,0x50607080,0x90a0b0c0);
  mtl_pool_del(storage);mtl_pool_del(storage+65536);free(storage);
  vk_buffer_memory_reference(saved_reference);
  LOG("[vulkan buffer scope] adjacent physical pools, same-pool aliases and recreated guest range passed");
}
