// Finite fresh image: validates allocation/Buffer-union lifetime, not an ASTC
// producer recipe. Original full/tail/ASTC family test remains separate.
typedef struct {void *destination,*source;size_t bytes;} native_lane_readback;
static void native_lane_readback_done(void *context) {native_lane_readback *r=context;memcpy(r->destination,r->source,r->bytes);}
static void native_pending_acquire_gpu_cases(VkImage image,void *binding,uint64_t address,uint64_t image_bytes,NVNmemoryPool *pool) {
  uint8_t controls[2][0x880];memset(controls[0],0,sizeof controls[0]);memset(controls[1],1,sizeof controls[1]);
  NVNprogram program;NVNshaderData stages[]={{.control=controls[0]},{.control=controls[1]}};
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,stages));
  float *color=(void *)(bytes+(3u<<20));color[0]=0;color[1]=1;color[2]=0.25f;color[3]=1;
  CALL(nvnMemoryPoolFlushMappedRange,pool,3u<<20,16);
  NVNtexture targets[2];
  for(unsigned n=0;n<2;n++) {
    NVNtextureBuilder tb;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
    CALL(nvnTextureBuilderSetWidth,&tb,16);CALL(nvnTextureBuilderSetHeight,&tb,16);CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);
    CALL(nvnTextureBuilderSetStorage,&tb,pool,n?(ptrdiff_t)(address-(uintptr_t)bytes):2u<<20);
    CHECK(CALL(nvnTextureInitialize,&targets[n],&tb));
  }
  NVNcolorState cs;NVNpolygonState ps;NVNdepthStencilState ds;
  CALL(nvnColorStateSetDefaults,&cs);CALL(nvnPolygonStateSetDefaults,&ps);CALL(nvnPolygonStateSetCullFace,&ps,NVN_FACE_NONE);
  CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);
  CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  S=(vk_state){.program=(const prog_t *)&program,.depth_range={0,1},.chmask={~0u},.has_viewport=1,.has_scissor=1};
  S.color=*(const color_st *)&cs;S.poly=*(const poly_st *)&ps;S.ds=*(const ds_st *)&ds;
  S.viewport[0][2]=S.viewport[0][3]=16;S.scissor=(p_scissor){0,0,16,16};
  // First case is an independent pending pass. Real uniform reads of the
  // native bytes, an explicit order boundary, and a real aliased attachment
  // writer must each retain the old split. No history is injected by a stub.
  // The fifth pass also rejects the prior chunk's retained private optimal
  // writer. Its contents live in another VkImage, unlike native shared bytes;
  // completion or an older pass cannot manufacture canonical payload authority.
  for(unsigned test=0;test<5;test++) {
    S.targets=(p_set_rt){.n=1,.color={(const tex_t *)&targets[test==3]}};
    S.ubo[1][0]=(p_bind_buf){.addr=test==1?address:(uintptr_t)color,.size=16};
    vk_pipeline_state_changed();mtl_sub_begin();vk_buffer *out=vk_temp_buffer(4096);
    p_draw draw={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};vk_draw(&draw,NULL);CHECK(vk_render_active());
    if(test==2)V.nvn_barrier=NVN_BARRIER_ORDER_PRIMITIVES_BIT;
    extern int vk_test_native_pending_acquire(VkImage,void *,uint64_t,uint64_t,VkBuffer);
    CHECK(vk_test_native_pending_acquire(image,binding,address,image_bytes,out->handle)==(test==0 && !getenv("DM_NATIVE_ONLY_WRITER_PENDING_FIXTURE")));
    if(test==2)vk_barrier();
    VkBufferMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,
      .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
      .buffer=out->handle,.offset=0,.size=4096};
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&host,0,NULL);
    uint8_t actual[4096];native_lane_readback publication={actual,out->map,sizeof actual};
    vk_completion_write(native_lane_readback_done,&publication,actual,sizeof actual);
    vk_internal_wait(vk_internal_commit());
    for(unsigned n=0;n<sizeof actual;n+=4)CHECK(actual[n]==255 && !actual[n+1] && !actual[n+2] && actual[n+3]==255);
    // The positive/order/image-alias passes also rendered the original mock
    // shader's green output correctly; alias-UBO values are intentionally opaque.
    if(test!=1) {
      uint8_t rendered[1024];NVNcopyRegion region={0,0,0,16,16,1};
      CALL(nvnTextureReadTexels,&targets[test==3],NULL,&region,rendered);
      for(unsigned n=0;n<sizeof rendered;n+=4)CHECK(!rendered[n] && rendered[n+1]==255 && rendered[n+2]==64 && rendered[n+3]==255);
    }
  }
  for(unsigned n=0;n<2;n++)CALL(nvnTextureFinalize,&targets[n]);
  CALL(nvnProgramFinalize,&program);CHECK(!vk_validation_error_count());
}
static void native_lane_gpu_test(void) {
  CHECK(vk_pool_shared_mode() && V.options.pool_window_bytes==65536);
  extern unsigned vk_test_native_import_index(void);checks+=vk_test_native_import_index();
  NVNmemoryPoolBuilder pb;NVNmemoryPool memory;
  CALL(nvnMemoryPoolBuilderSetDefaults,&pb);CALL(nvnMemoryPoolBuilderSetDevice,&pb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&pb,bytes,4u<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&pb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&memory,&pb));
  VkPhysicalDeviceExternalImageFormatInfo external_query={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
    .handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
  VkPhysicalDeviceImageFormatInfo2 format={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,.pNext=&external_query,
    .format=VK_FORMAT_R8G8B8A8_UNORM,.type=VK_IMAGE_TYPE_2D,.tiling=VK_IMAGE_TILING_OPTIMAL,
    .usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT};
  VkExternalImageFormatProperties external_properties={.sType=VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
  VkImageFormatProperties2 properties={.sType=VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,.pNext=&external_properties};
  VkResult supported=vkGetPhysicalDeviceImageFormatProperties2(V.physical,&format,&properties);
  if(supported!=VK_SUCCESS || !(external_properties.externalMemoryProperties.externalMemoryFeatures&VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) ||
      (external_properties.externalMemoryProperties.externalMemoryFeatures&VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT)) {
    CALL(nvnMemoryPoolFinalize,&memory);
    printf("{\"test\":\"native-lane-window-lifetime\",\"result\":\"unsupported\",\"external_image_result\":%d,\"external_features\":%u}\n",supported,external_properties.externalMemoryProperties.externalMemoryFeatures);return;
  }
  VkExternalMemoryImageCreateInfo external={.sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,.handleTypes=external_query.handleType};
  VkImageCreateInfo create={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.pNext=&external,
    .imageType=VK_IMAGE_TYPE_2D,.format=format.format,.extent={32,32,1},.mipLevels=1,.arrayLayers=1,
    .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=format.usage,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
  VkImage image;VK_CHECK(vkCreateImage(V.device,&create,NULL,&image));
  VkMemoryRequirements needs;vkGetImageMemoryRequirements(V.device,image,&needs);
  CHECK(needs.size && needs.size<=32768 && needs.alignment<=65536);
  uint64_t address=(uintptr_t)bytes+65536;CHECK(!(address%needs.alignment));
  uint64_t initial_count,initial_bytes,count,allocated;
  extern void vk_test_native_allocation_stats(uint64_t *,uint64_t *);
  vk_test_native_allocation_stats(&initial_count,&initial_bytes);
  mtl_sub_begin();vk_pool_requirement requirement={address,needs.size,0,VK_PIPELINE_STAGE_TRANSFER_BIT,1,VK_POOL_REQUIRE_NATIVE_IMAGE};
  int operation=vk_pool_operation_begin_ranges(&requirement,1);
  vk_pool_span_info absent;CHECK(vk_pool_span_query(address,needs.size,&absent)==VK_POOL_SPAN_UNBACKED);
  CHECK(!vk_pool_shared_materialized(address,needs.size));
  extern void vk_test_native_allocation_fail(int);
  extern void *vk_test_native_image_try_bind(uint64_t,uint64_t,VkImage,const VkMemoryRequirements *,VkDeviceMemory *,VkResult *);
  VkDeviceMemory failure_memory=VK_NULL_HANDLE;VkResult allocation_failure=VK_SUCCESS;
  vk_test_native_allocation_fail(1);
  CHECK(!vk_test_native_image_try_bind(address,needs.size,image,&needs,&failure_memory,&allocation_failure));
  CHECK(allocation_failure==VK_ERROR_OUT_OF_DEVICE_MEMORY && !failure_memory);
  vk_test_native_allocation_fail(0);vk_test_native_allocation_stats(&count,&allocated);
  CHECK(count==initial_count && allocated==initial_bytes);
  VkDeviceMemory immutable_memory;void *binding=vk_pool_native_image_bind(address,needs.size,image,&needs,&immutable_memory);
  vk_test_native_allocation_stats(&count,&allocated);CHECK(count==initial_count+1 && allocated>initial_bytes && allocated-initial_bytes<=65536);
  VkImageMemoryBarrier general={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,
    .newLayout=VK_IMAGE_LAYOUT_GENERAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
    .image=image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT};
  extern unsigned vk_test_native_initial_general(VkImage);
  checks+=vk_test_native_initial_general(image);
  int pending_fixture=getenv("DM_NATIVE_PENDING_ACQUIRE_FIXTURE")!=NULL;
  int native_writer_pending=getenv("DM_NATIVE_ONLY_WRITER_PENDING_FIXTURE")!=NULL;
  VkClearColorValue red={.float32={1,0,0,1}};
  vk_pool_native_image_use(binding,address,needs.size,VK_PIPELINE_STAGE_TRANSFER_BIT);
  vkCmdClearColorImage(V.cmd,image,VK_IMAGE_LAYOUT_GENERAL,&red,1,&general.subresourceRange);
  if(!pending_fixture || native_writer_pending) {
    vk_buffer_transfer_written_range(address,needs.size);
    vk_pool_native_image_writeback(binding,address,needs.size,VK_PIPELINE_STAGE_TRANSFER_BIT);
  }
  VkMemoryBarrier alias={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&alias,0,NULL,0,NULL);
  void *raw;uint64_t offset;vk_buffer *source;
  void *saved_raw;uint64_t saved_offset;uint64_t saved_address=(uintptr_t)bytes+524288;
  if(pending_fixture) {
    VkImageMemoryBarrier release=general;release.oldLayout=release.newLayout=VK_IMAGE_LAYOUT_GENERAL;
    release.srcQueueFamilyIndex=V.queue_family;release.dstQueueFamilyIndex=VK_QUEUE_FAMILY_FOREIGN_EXT;
    release.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;release.dstAccessMask=0;
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,NULL,0,NULL,1,&release);
  }
  vk_pool_operation_end(operation);vk_internal_wait(vk_internal_commit());
  CHECK(vk_pool_span_query(address,needs.size,&absent)==VK_POOL_SPAN_UNBACKED);
  CHECK(!vk_pool_shared_materialized(address,needs.size));
  extern int vk_test_native_import_coverage(uint64_t,uint64_t);
  CHECK(vk_test_native_import_coverage(address,needs.size));
  // Adjacent actual native imports share no VkBuffer. Flush/Invalidate must
  // visit each immutable VkMemory, including a complete joined GPU-use run.
  if(!pending_fixture) {
    uint64_t second_address=address+(allocated-initial_bytes);
    CHECK(second_address+needs.size<(uintptr_t)bytes+(2u<<20));
    VkImage second;VK_CHECK(vkCreateImage(V.device,&create,NULL,&second));
    mtl_sub_begin();vk_pool_requirement adjacent[2]={{address,needs.size,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0,VK_POOL_REQUIRE_NATIVE_IMAGE},
      {second_address,needs.size,0,VK_PIPELINE_STAGE_TRANSFER_BIT,1,VK_POOL_REQUIRE_NATIVE_IMAGE}};
    operation=vk_pool_operation_begin_ranges(adjacent,2);
    VkDeviceMemory second_memory;void *second_binding=vk_pool_native_image_bind(second_address,needs.size,second,&needs,&second_memory);
    CHECK(second_memory!=immutable_memory);CHECK(vk_test_native_import_coverage(address,second_address+needs.size-address));
    CHECK(!vk_test_native_import_coverage(address-1,second_address+needs.size-address+1));
    VkImageMemoryBarrier second_general=general;second_general.image=second;
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&second_general);
    VkClearColorValue green={.float32={0,1,0,1}};vk_pool_native_image_use(second_binding,second_address,needs.size,VK_PIPELINE_STAGE_TRANSFER_BIT);
    vkCmdClearColorImage(V.cmd,second,VK_IMAGE_LAYOUT_GENERAL,&green,1,&general.subresourceRange);
    vk_buffer_transfer_written_range(second_address,needs.size);
    vk_pool_native_image_writeback(second_binding,second_address,needs.size,VK_PIPELINE_STAGE_TRANSFER_BIT);
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&alias,0,NULL,0,NULL);
    vk_buffer *pixels=vk_temp_buffer(8192);VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={32,32,1}};
    vk_pool_native_image_use(binding,address,needs.size,VK_PIPELINE_STAGE_TRANSFER_BIT);
    vkCmdCopyImageToBuffer(V.cmd,image,VK_IMAGE_LAYOUT_GENERAL,pixels->handle,1,&copy);copy.bufferOffset=4096;
    vkCmdCopyImageToBuffer(V.cmd,second,VK_IMAGE_LAYOUT_GENERAL,pixels->handle,1,&copy);
    VkBufferMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,
      .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=pixels->handle,.size=8192};
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&host,0,NULL);
    uint8_t both[8192];native_lane_readback publication={both,pixels->map,sizeof both};
    vk_completion_write(native_lane_readback_done,&publication,both,sizeof both);
    vk_pool_operation_end(operation);vk_internal_wait(vk_internal_commit());
    CHECK(vk_pool_span_query(address,second_address+needs.size-address,&absent)==VK_POOL_SPAN_UNBACKED);
    for(unsigned n=0;n<sizeof both;n+=4)CHECK(both[n]==(n<4096?255:0) && both[n+1]==(n<4096?0:255) && !both[n+2] && both[n+3]==255);
    CALL(nvnMemoryPoolInvalidateMappedRange,&memory,address-(uintptr_t)bytes,second_address+needs.size-address);
    CALL(nvnMemoryPoolFlushMappedRange,&memory,address-(uintptr_t)bytes,second_address+needs.size-address);
    vkDestroyImage(V.device,second,NULL);vk_pool_native_image_release(second_binding);vk_pool_native_image_collect();
    CHECK(!vk_test_native_import_coverage(second_address,needs.size));
    CHECK(vk_test_native_import_coverage(address,needs.size));
  }
  if(pending_fixture) {
    native_pending_acquire_gpu_cases(image,binding,address,needs.size,&memory);
    vkDestroyImage(V.device,image,NULL);vk_pool_native_image_release(binding);vk_pool_native_image_collect();
    vk_test_native_allocation_stats(&count,&allocated);CHECK(count==initial_count && allocated==initial_bytes);
    CALL(nvnMemoryPoolFinalize,&memory);CHECK(!vk_validation_error_count());
    if(native_writer_pending)printf("{\"test\":\"native-only-writer-conservative-split\",\"result\":\"passed\",\"checks\":%u,\"no_fake_Buffer_history\":true,\"actual_RGBA_pixels_correct\":true}\n",checks);
    else printf("{\"test\":\"native-pending-acquire-real-RGBA\",\"result\":\"passed\",\"checks\":%u,\"independent_preserved\":true,\"buffer_read_alias_split\":true,\"image_writer_alias_split\":true,\"explicit_order_split\":true,\"ASTC_executed\":false}\n",checks);return;
  }
  uint8_t *opaque=malloc(needs.size);CHECK(opaque);
  // Use a standard readback to obtain bytes rather than reading device tiling.
  mtl_sub_begin();vk_pool_requirement copy_ranges[3]={{address,needs.size,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0,VK_POOL_REQUIRE_NATIVE_IMAGE},
    {address,needs.size,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0},
    {saved_address,needs.size,0,VK_PIPELINE_STAGE_TRANSFER_BIT,1,0}};
  operation=vk_pool_operation_begin_ranges(copy_ranges,3);
  CHECK(vk_pool_shared_materialized(address,needs.size));
  CHECK(vk_pool_address_stage(address,needs.size,&raw,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT));source=raw;
  CHECK(vk_pool_address_stage(saved_address,needs.size,&saved_raw,&saved_offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
  VkBufferCopy keep={.srcOffset=offset,.dstOffset=saved_offset,.size=needs.size};
  vk_transfer_buffer_dependencies((vk_buffer_use[]){{address,needs.size,0},{saved_address,needs.size,1}},2);
  vkCmdCopyBuffer(V.cmd,source->handle,((vk_buffer *)saved_raw)->handle,1,&keep);
  vk_buffer_transfer_written_range(saved_address,needs.size);vk_buffer_writeback_stage(saved_address,needs.size,VK_PIPELINE_STAGE_TRANSFER_BIT);
  vk_pool_operation_end(operation);vk_internal_wait(vk_internal_commit());
  memcpy(opaque,(void *)(uintptr_t)saved_address,needs.size);
  // Two distinct crossing unions while binding stays live, no TextureFinalize.
  uint64_t generations[3];vk_pool_span_info span;CHECK(vk_pool_span_query(address,needs.size,&span)==VK_POOL_SPAN_SINGLE);generations[0]=span.backing_generation;
  for(unsigned pass=0;pass<2;pass++) {
    mtl_sub_begin();vk_pool_requirement cross={address-(pass?49152:32768),needs.size+(pass?131072:65536),0,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0};
    operation=vk_pool_operation_begin_ranges(&cross,1);
    CHECK(vk_pool_span_query(address,needs.size,&span)==VK_POOL_SPAN_SINGLE);generations[pass+1]=span.backing_generation;
    CHECK(generations[pass+1]>generations[pass]);
    extern void vk_test_native_allocation_binding(void *,VkDeviceMemory *,uint64_t *);
    VkDeviceMemory still_bound;uint64_t binding_offset;
    vk_test_native_allocation_binding(binding,&still_bound,&binding_offset);
    CHECK(still_bound==immutable_memory && binding_offset<needs.alignment);

    vk_pool_native_image_use(binding,address,needs.size,VK_PIPELINE_STAGE_TRANSFER_BIT);
    vk_pool_operation_end(operation);vk_internal_wait(vk_internal_commit());
  }
  // Change actual image pixels, then restore its old opaque bytes via the NEW
  // active VkBuffer and check image readback. Bound VkDeviceMemory never moves.
  mtl_sub_begin();vk_pool_requirement swap=copy_ranges[0];copy_ranges[0]=copy_ranges[1];copy_ranges[1]=swap;
  operation=vk_pool_operation_begin_ranges(copy_ranges,3);
  VkClearColorValue blue={.float32={0,0,1,1}};
  vk_pool_native_image_use(binding,address,needs.size,VK_PIPELINE_STAGE_TRANSFER_BIT);
  vkCmdClearColorImage(V.cmd,image,VK_IMAGE_LAYOUT_GENERAL,&blue,1,&general.subresourceRange);
  alias.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;alias.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&alias,0,NULL,0,NULL);
  CHECK(vk_pool_address_stage(address,needs.size,&raw,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
  CHECK(vk_pool_address_stage(saved_address,needs.size,&saved_raw,&saved_offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
  VkBufferCopy restore={.srcOffset=saved_offset,.dstOffset=offset,.size=needs.size};
  vk_transfer_buffer_dependencies((vk_buffer_use[]){{saved_address,needs.size,0},{address,needs.size,1}},2);
  vkCmdCopyBuffer(V.cmd,((vk_buffer *)saved_raw)->handle,((vk_buffer *)raw)->handle,1,&restore);
  vk_buffer_transfer_written_range(address,needs.size);
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&alias,0,NULL,0,NULL);
  vk_buffer *pixels=vk_temp_buffer(4096);
  VkBufferImageCopy read={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={32,32,1}};
  vkCmdCopyImageToBuffer(V.cmd,image,VK_IMAGE_LAYOUT_GENERAL,pixels->handle,1,&read);
  CHECK(pixels->properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VkBufferMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,
    .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
    .buffer=pixels->handle,.offset=0,.size=4096};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&host,0,NULL);
  uint8_t actual[4096];native_lane_readback publication={actual,pixels->map,sizeof actual};
  vk_completion_write(native_lane_readback_done,&publication,actual,sizeof actual);
  vk_pool_operation_end(operation);vk_internal_wait(vk_internal_commit());
  for(unsigned i=0;i<sizeof actual;i+=4)CHECK(actual[i]==255 && !actual[i+1] && !actual[i+2] && actual[i+3]==255);
  vk_test_native_allocation_stats(&count,&allocated);CHECK(count==initial_count+1);
  vkDestroyImage(V.device,image,NULL);vk_pool_native_image_release(binding);vk_pool_native_image_collect();
  vk_test_native_allocation_stats(&count,&allocated);CHECK(count==initial_count && allocated==initial_bytes);
  CHECK(!memcmp(opaque,(void *)(uintptr_t)saved_address,needs.size));free(opaque);
  CALL(nvnMemoryPoolFinalize,&memory);CHECK(!vk_validation_error_count());
  printf("{\"test\":\"native-lane-window-lifetime\",\"result\":\"passed\",\"checks\":%u,\"native_only_no_Buffer_then_late_Buffer\":true,\"adjacent_native_memories_GPU_pixels\":true,\"two_unions\":true,\"fresh_RGBA_not_ASTC\":true,\"last_release_collected\":true}\n",checks);
}
