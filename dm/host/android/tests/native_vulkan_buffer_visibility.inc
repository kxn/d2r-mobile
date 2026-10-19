// Real compute/transfer writers and graphics consumers exercise literal stage
// coverage, then retain the same history across two gated physical submits.
// The final independent sparse fills deliberately overflow the bounded table.
static void buffer_visibility_mismatch(const uint8_t *actual,const uint8_t *mapped,const uint8_t *guest,
                                       unsigned bytes,unsigned stride,unsigned first,
                                       VkMemoryPropertyFlags readback_properties,uint64_t source_size,
                                       uint64_t source_offset,uint64_t first_submit,uint64_t second_submit) {
  uint8_t *expected=calloc(1,bytes);if(!expected)return;
  unsigned count=0,fill_bad=0,gap_bad=0;
  for(unsigned byte=0;byte<bytes;byte++) {
    unsigned block=byte/stride,lane=byte%stride;uint32_t value=0x91ab0000u+block;
    expected[byte]=lane<4?((uint8_t *)&value)[lane]:0;
    if(actual[byte]!=expected[byte]) {
      if(lane<4)fill_bad++;else gap_bad++;
      if(count<16)LOG("[vulkan buffer visibility mismatch] byte=%u block=%u lane=%u actual=%#x expected=%#x source_map=%#x guest=%#x",
                     byte,block,lane,actual[byte],expected[byte],mapped[byte],guest[byte]);
      count++;
    }
  }
  LOG("[vulkan buffer visibility mismatch] first=%u total=%u fill_bad=%u gap_bad=%u bytes=%u source_offset=%llu source_size=%llu readback_properties=%#x first_submit=%llu second_submit=%llu completed=%llu",
      first,count,fill_bad,gap_bad,bytes,(unsigned long long)source_offset,(unsigned long long)source_size,readback_properties,
      (unsigned long long)first_submit,(unsigned long long)second_submit,(unsigned long long)vk_completed_serial());
  const char *base=getenv("DM_CAPTURE_TEST_DIR");if(!base)base=".";
  const uint8_t *arrays[]={actual,expected,mapped,guest};const char *names[]={"actual","expected","source-map","guest"};
  for(unsigned n=0;n<4;n++) {
    char path[4096];snprintf(path,sizeof path,"%s/buffer-visibility-%s.bin",base,names[n]);
    FILE *file=fopen(path,"wb");if(file) {size_t wrote=fwrite(arrays[n],1,bytes,file);int closed=fclose(file);LOG("[vulkan buffer visibility mismatch] dump=%s bytes=%zu close=%d",path,wrote,closed);}
    else LOG("[vulkan buffer visibility mismatch] cannot write dump=%s",path);
  }
  free(expected);
}
// Failure-only interleaved comparison on the SAME owned VkBuffer/offset.
// A uses explicit barriers without logical history; B uses the exact history
// overflow path. One B omits the current pool declaration, as the original
// raw second submission did. Keep the original failure fatal afterwards.
static void buffer_visibility_fill_references(vk_buffer *buffer,uint64_t offset,uint8_t *guest,unsigned bytes) {
  unsigned stride=16,copies=(bytes-4)/stride+1;
  uint8_t *result=malloc(bytes);if(!result)return;
  for(unsigned phase=0;phase<4;phase++) {
    int history=phase==1||phase==2,declare=phase!=2;
    uint32_t pattern=0x91ab0000u+phase*0x10000u+(phase&1?0x4321u:0);
    VkSemaphore gate=vk_async_submissions()&&V.timeline_semaphore?block_gpu_queue():VK_NULL_HANDLE;
    uint64_t initialized=vk_pool_initialized_bytes();mtl_sub_begin();
    if(declare) {
      vk_buffer *current;uint64_t current_offset;
      CHECK(vk_pool_address_stage((uintptr_t)guest,bytes,(void **)&current,&current_offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
      CHECK(current==buffer&&current_offset==offset);
    }
    uint64_t initial_delta=vk_pool_initialized_bytes()-initialized;
    vk_barrier();
    for(unsigned i=0;i<copies;i++) {
      vkCmdFillBuffer(V.cmd,buffer->handle,offset+i*stride,4,pattern+i);
      if(history)vk_buffer_transfer_written_range((uintptr_t)guest+i*stride,4);
    }
    VkPipelineStageFlags consumers=VK_PIPELINE_STAGE_VERTEX_INPUT_BIT|VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    VkMemoryBarrier dependency={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstAccessMask=VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT|VK_ACCESS_INDEX_READ_BIT|VK_ACCESS_UNIFORM_READ_BIT|VK_ACCESS_SHADER_READ_BIT};
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,consumers,0,1,&dependency,0,NULL,0,NULL);
    if(history) {
      vk_buffer_producers_visible(VK_PIPELINE_STAGE_TRANSFER_BIT,consumers);
      CHECK(vk_buffer_vertex_pending((uintptr_t)guest,4));
      vk_buffer_shader_read((uintptr_t)guest,4,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT|VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);
    } else vk_barrier();
    vk_buffer *copy=vk_temp_buffer(bytes);CHECK(copy->properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkBufferCopy region={.srcOffset=offset,.size=bytes};
    vkCmdCopyBuffer(V.cmd,buffer->handle,copy->handle,1,&region);vk_buffer_transfer_written();
    VkBufferMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,
      .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
      .buffer=copy->handle,.offset=0,.size=bytes};
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&host,0,NULL);
    memset(result,0xcd,bytes);vk_effect_copy(result,copy->map,bytes);uint64_t serial=vk_internal_commit();
    if(gate) {
      CHECK(vk_completed_serial()<serial);
      VkSemaphoreSignalInfoKHR signal={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO_KHR,.semaphore=gate,.value=1};
      VK_CHECK(V.signal_timeline(V.device,&signal));
    }
    vk_internal_wait(serial);if(gate)vkDestroySemaphore(V.device,gate,NULL);
    unsigned bad=0,fill_bad=0,gap_bad=0,first=bytes;
    for(unsigned byte=0;byte<bytes;byte++) {
      unsigned lane=byte%stride;uint32_t value=pattern+byte/stride;
      uint8_t expected=lane<4?((uint8_t *)&value)[lane]:0;
      if(result[byte]!=expected) {if(first==bytes)first=byte;bad++;if(lane<4)fill_bad++;else gap_bad++;}
    }
    uint32_t actual_first,mapped_first;memcpy(&actual_first,result,4);memcpy(&mapped_first,buffer->map+offset,4);
    LOG("[vulkan buffer fill reference] phase=%u history=%d declare=%d initial_delta=%llu pattern=%#x first_word=%#x source_first=%#x bad=%u first_bad=%u fill_bad=%u gap_bad=%u",
        phase,history,declare,(unsigned long long)initial_delta,pattern,actual_first,mapped_first,bad,first,fill_bad,gap_bad);
    if(bad) {
      const char *base=getenv("DM_CAPTURE_TEST_DIR");if(!base)base=".";char path[4096];
      snprintf(path,sizeof path,"%s/buffer-fill-reference-phase%u.bin",base,phase);
      FILE *file=fopen(path,"wb");if(file) {fwrite(result,1,bytes,file);fclose(file);}
    }
  }
  free(result);
}
static void buffer_visibility_intersection_test(const void *vertex,const void *fragment) {
  if(vk_submission_barrier_mode()!=0) {
    LOG("[vulkan buffer visibility fixture] scoped buffer histories unavailable in full-reference mode");return;
  }
  vk_state saved=S;
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,1<<20));memset(storage,0,1<<20);
  const float first[]={.25f,.5f,.75f,1},second[]={.75f,.25f,.5f,1};
  const float positions[]={-1,-1,3,-1,-1,3};
  memcpy(storage+0x10000,first,16);memcpy(storage+0x10100,second,16);memcpy(storage+0x11000,positions,24);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  uint8_t vertex_storage[0x880],compute_copy[0x880];memset(vertex_storage,24,sizeof vertex_storage);memset(compute_copy,26,sizeof compute_copy);
  NVNprogram graphics[2],compute;NVNshaderData shaders[2]={{.control=vertex},{.control=fragment}};
  for(unsigned i=0;i<2;i++) {
    shaders[0].control=i?vertex_storage:vertex;
    CHECK(CALL(nvnProgramInitialize,&graphics[i],&device));CHECK(CALL(nvnProgramSetShaders,&graphics[i],2,shaders));
  }
  NVNshaderData cs={.control=compute_copy};CHECK(CALL(nvnProgramInitialize,&compute,&device));CHECK(CALL(nvnProgramSetShaders,&compute,1,&cs));
  NVNtextureBuilder tb;NVNtexture texture;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&tb,16);CALL(nvnTextureBuilderSetHeight,&tb,4);
  CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);CALL(nvnTextureBuilderSetStorage,&tb,&pool,0x60000);CHECK(CALL(nvnTextureInitialize,&texture,&tb));
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);
  CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);
  VkSemaphore gate=vk_async_submissions()&&V.timeline_semaphore?block_gpu_queue():VK_NULL_HANDLE;
  uint64_t color_address=(uintptr_t)storage+0x20000,geometry_address=(uintptr_t)storage+0x21000;
  VkPipelineStageFlags vertex_stages=VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
  mtl_sub_begin();vk_buffer *buffer;uint64_t base;
  CHECK(vk_pool_address_stage((uintptr_t)storage,1<<20,(void **)&buffer,&base,VK_PIPELINE_STAGE_TRANSFER_BIT));
  vk_barrier();CHECK(!vk_buffer_vertex_pending(color_address,16));
  mtl_sub_copy((uintptr_t)storage+0x10000,color_address,16);
  S=(vk_state){.program=(const prog_t *)&compute,.depth_range={0,1},.chmask={15}};
  S.ssbo[5][0]=(p_bind_buf){5,0,(uintptr_t)storage+0x11000,24};S.ssbo[5][1]=(p_bind_buf){5,1,geometry_address,24};
  vk_dispatch(&(p_dispatch){1,1,1});
  CHECK(vk_buffer_vertex_pending(color_address,16));CHECK(vk_buffer_vertex_pending(geometry_address,24));
  S.program=(const prog_t *)&graphics[0];S.poly=*(const poly_st *)&polygon;S.ds=*(const ds_st *)&ds;S.color=*(const color_st *)&color;
  S.targets=(p_set_rt){.n=1,.color={(const tex_t *)&texture}};
  S.has_viewport=S.has_scissor=1;S.viewport[0][2]=16;S.viewport[0][3]=4;
  S.scissor=(p_scissor){0,0,16,1};S.ssbo[1][0]=(p_bind_buf){1,0,color_address,16};
  vk_pipeline_state_changed();p_draw draw={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};vk_draw(&draw,NULL);
  // TRANSFER is visible to fragments, while the independent COMPUTE writer
  // is not. The summary must be intersection, never a union of these proofs.
  CHECK(vk_buffer_vertex_pending(color_address,16));CHECK(vk_buffer_vertex_pending(geometry_address,24));
  S.scissor.y=1;S.ssbo[1][0]=(p_bind_buf){1,0,geometry_address,24};vk_draw(&draw,NULL);
  // Both producers now cover FRAGMENT only, so repeated FS queries can skip
  // history searches but neither VERTEX bit is implied.
  for(unsigned i=0;i<4;i++)vk_buffer_shader_read(color_address,16,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  CHECK(vk_buffer_vertex_pending(color_address,16));CHECK(vk_buffer_vertex_pending(geometry_address,24));
  S.program=(const prog_t *)&graphics[1];S.scissor.y=2;
  S.ssbo[0][0]=(p_bind_buf){0,0,geometry_address,24};S.ssbo[1][0]=(p_bind_buf){1,0,color_address,16};
  vk_pipeline_state_changed();vk_draw(&draw,NULL);
  CHECK(!vk_buffer_vertex_pending(geometry_address,24));CHECK(vk_buffer_vertex_pending(color_address,16));
  // A real barrier covers the remaining TRANSFER producer for the exact two
  // vertex stages; all retained writers now carry the common proof.
  vk_buffer_shader_read(color_address,16,vertex_stages);
  CHECK(!vk_buffer_vertex_pending(color_address,16));CHECK(!vk_buffer_vertex_pending(geometry_address,24));
  CHECK(!vk_buffer_vertex_pending((uintptr_t)storage+0x70000,16));
  // A later GPU write must immediately invalidate that global negative proof.
  mtl_sub_copy((uintptr_t)storage+0x10100,color_address,16);
  CHECK(vk_buffer_vertex_pending(color_address,16));CHECK(!vk_buffer_vertex_pending(geometry_address,24));
  S.program=(const prog_t *)&graphics[0];S.scissor.y=3;vk_pipeline_state_changed();vk_draw(&draw,NULL);
  CHECK(vk_buffer_vertex_pending(color_address,16));
  uint64_t first_submit=vk_internal_commit();
  if(gate)CHECK(vk_completed_serial()<first_submit);
  mtl_sub_begin();
  // CPU completion and a physical command-buffer boundary do not discard
  // unresolved producer history or widen a fragment-only destination scope.
  CHECK(vk_buffer_vertex_pending(color_address,16));CHECK(!vk_buffer_vertex_pending(geometry_address,24));
  vk_buffer_shader_read(color_address,16,vertex_stages);CHECK(!vk_buffer_vertex_pending(color_address,16));
  vk_barrier();CHECK(!vk_buffer_vertex_pending(color_address,16));
  enum { sparse_offset=0x40000,copies=257,stride=16,bytes=(copies-1)*stride+4 };
  for(unsigned i=0;i<copies;i++) {
    vkCmdFillBuffer(V.cmd,buffer->handle,base+sparse_offset+i*stride,4,0x91ab0000u+i);
    vk_buffer_transfer_written_range((uintptr_t)storage+sparse_offset+i*stride,4);
  }
  // The physical dependency covers ALL real fills, but the incomplete
  // logical history must keep its old conservative overflow result even
  // when every retained entry's visible mask would satisfy the fast proof.
  VkPipelineStageFlags consumers=vertex_stages|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
  VkMemoryBarrier dependency={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT|VK_ACCESS_INDEX_READ_BIT|VK_ACCESS_UNIFORM_READ_BIT|VK_ACCESS_SHADER_READ_BIT};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,consumers,0,1,&dependency,0,NULL,0,NULL);
  vk_buffer_producers_visible(VK_PIPELINE_STAGE_TRANSFER_BIT,consumers);
  CHECK(vk_buffer_vertex_pending((uintptr_t)storage+sparse_offset,4));
  CHECK(vk_buffer_vertex_pending((uintptr_t)storage+0x70000,16));
  CHECK(!vk_buffer_vertex_pending(0,4));CHECK(!vk_buffer_vertex_pending(color_address,0));
  vk_buffer_shader_read((uintptr_t)storage+sparse_offset,4,vertex_stages);
  CHECK(!vk_buffer_vertex_pending((uintptr_t)storage+sparse_offset,4));
  CHECK(!vk_buffer_vertex_pending((uintptr_t)storage+0x70000,16));
  uint8_t result[bytes];memset(result,0xcd,sizeof result);vk_buffer *readback=vk_temp_buffer(bytes);
  CHECK(base+sparse_offset+bytes<=buffer->size);CHECK(readback->size>=bytes);
  VkMemoryPropertyFlags readback_properties=readback->properties;
  CHECK(readback_properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VkBufferCopy copy={.srcOffset=base+sparse_offset,.size=bytes};
  vkCmdCopyBuffer(V.cmd,buffer->handle,readback->handle,1,&copy);vk_buffer_transfer_written();
  // Keep the owned raw readback's memory dependency explicit, independent of
  // the backend's additional unconditional HOST barrier at physical commit.
  VkBufferMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,
    .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
    .buffer=readback->handle,.offset=0,.size=bytes};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&host,0,NULL);
  vk_effect_copy(result,readback->map,bytes);
  uint64_t second_submit=vk_internal_commit();
  if(gate) {
    CHECK(vk_completed_serial()<second_submit);CHECK(vk_completed_serial()<first_submit);
    VkSemaphoreSignalInfoKHR signal={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO_KHR,.semaphore=gate,.value=1};
    VK_CHECK(V.signal_timeline(V.device,&signal));
  }
  vk_internal_wait(second_submit);CHECK(vk_completed_serial()>=second_submit);
  if(gate)vkDestroySemaphore(V.device,gate,NULL);
  for(unsigned byte=0;byte<bytes;byte++) {
    unsigned block=byte/stride,lane=byte%stride;uint32_t value=0x91ab0000u+block;
    uint8_t expected=lane<4?((uint8_t *)&value)[lane]:0;
    if(result[byte]!=expected) {
      buffer_visibility_mismatch(result,buffer->map+base+sparse_offset,storage+sparse_offset,bytes,stride,byte,
                                 readback_properties,buffer->size,base+sparse_offset,first_submit,second_submit);
      buffer_visibility_fill_references(buffer,base+sparse_offset,storage+sparse_offset,bytes);
    }
    CHECK(result[byte]==expected);
  }
  CHECK(!memcmp(storage+0x20000,second,16));CHECK(!memcmp(storage+0x21000,positions,24));
  NVNcopyRegion region={0,0,0,16,4,1};uint8_t pixels[256];CALL(nvnTextureReadTexels,&texture,NULL,&region,pixels);
  const uint8_t colors[4][4]={{64,128,191,255},{0,0,255,0},{64,128,191,255},{191,64,128,255}};
  for(unsigned y=0;y<4;y++)for(unsigned x=0;x<16;x++)for(unsigned c=0;c<4;c++)CHECK(abs((int)pixels[(y*16+x)*4+c]-colors[y][c])<=1);
  S=saved;vk_pipeline_state_changed();CALL(nvnTextureFinalize,&texture);
  for(unsigned i=0;i<2;i++)CALL(nvnProgramFinalize,&graphics[i]);
  CALL(nvnProgramFinalize,&compute);CALL(nvnMemoryPoolFinalize,&pool);free(storage);
  LOG("[vulkan buffer visibility fixture] real transfer/compute producers, separate FS/VS scopes, repeated negative reads, fresh writer invalidation, two gated submits, overflow fallback and exact GPU fills/pixels passed");
}
