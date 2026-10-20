// Finite fresh image: validates allocation/Buffer-union lifetime, not an ASTC
// producer recipe. Original full/tail/ASTC family test remains separate.
typedef struct {void *destination,*source;size_t bytes;} native_lane_readback;
static void native_lane_readback_done(void *context) {native_lane_readback *r=context;memcpy(r->destination,r->source,r->bytes);}
static void native_lane_gpu_test(void) {
  CHECK(vk_pool_shared_mode() && V.options.pool_window_bytes==65536);
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
  mtl_sub_begin();vk_pool_requirement requirement={address,needs.size,0,VK_PIPELINE_STAGE_TRANSFER_BIT,1,0};
  int operation=vk_pool_operation_begin_ranges(&requirement,1);
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
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&general);
  VkClearColorValue red={.float32={1,0,0,1}};
  vk_pool_native_image_use(binding,address,needs.size,VK_PIPELINE_STAGE_TRANSFER_BIT);
  vkCmdClearColorImage(V.cmd,image,VK_IMAGE_LAYOUT_GENERAL,&red,1,&general.subresourceRange);
  VkMemoryBarrier alias={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&alias,0,NULL,0,NULL);
  void *raw;uint64_t offset;vk_buffer *source;
  void *saved_raw;uint64_t saved_offset;uint64_t saved_address=(uintptr_t)bytes+524288;
  vk_pool_operation_end(operation);vk_internal_wait(vk_internal_commit());
  uint8_t *opaque=malloc(needs.size);CHECK(opaque);
  // Use a standard readback to obtain bytes rather than reading device tiling.
  mtl_sub_begin();vk_pool_requirement copy_ranges[2]={{address,needs.size,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0},
    {saved_address,needs.size,0,VK_PIPELINE_STAGE_TRANSFER_BIT,1,0}};
  operation=vk_pool_operation_begin_ranges(copy_ranges,2);
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
  mtl_sub_begin();operation=vk_pool_operation_begin_ranges(copy_ranges,2);
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
  printf("{\"test\":\"native-lane-window-lifetime\",\"result\":\"passed\",\"checks\":%u,\"two_unions\":true,\"fresh_RGBA_not_ASTC\":true,\"last_release_collected\":true}\n",checks);
}
