// Bounded API oracle extension, using the existing generated ASTC vectors.
// No alternate tiler, codec, shader, renderer, measurement or game state.
static void native_linear_transfer_roundtrip(image *i,VkFormat format) {
  VkBufferImageCopy regions[LEVELS];uint64_t bytes=0;
  for(unsigned level=0;level<LEVELS;level++) {
    size_t size;uint8_t *linear=linear_source(format,level,0,&size);free(linear);
    regions[level]=(VkBufferImageCopy){.bufferOffset=bytes,
      .imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level,0,1},
      .imageExtent={mip(FIXTURE_WIDTH,level),mip(FIXTURE_HEIGHT,level),1}};
    bytes+=size;
  }
  CHECK(bytes && bytes<MAX_FILE/2 && !(bytes&15));
  VkBuffer buffer;VkDeviceMemory allocation;
  VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=bytes*2,
    .usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT};
  VK(vkCreateBuffer(device,&bi,NULL,&buffer));VkMemoryRequirements needs;
  vkGetBufferMemoryRequirements(device,buffer,&needs);
  uint32_t type=pick_type(needs.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
  VkMemoryAllocateInfo ma={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=needs.size,.memoryTypeIndex=type};
  VK(vkAllocateMemory(device,&ma,NULL,&allocation));VK(vkBindBufferMemory(device,buffer,allocation,0));
  uint8_t *mapped;VK(vkMapMemory(device,allocation,0,VK_WHOLE_SIZE,0,(void **)&mapped));memset(mapped,0xcc,bytes*2);
  for(unsigned level=0;level<LEVELS;level++) {
    size_t size;uint8_t *linear=linear_source(format,level,0,&size);
    memcpy(mapped+regions[level].bufferOffset,linear,size);free(linear);
  }
  VkMappedMemoryRange whole={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=allocation,.size=VK_WHOLE_SIZE};
  int coherent=!!(memory.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if(!coherent)VK(vkFlushMappedMemoryRanges(device,1,&whole));
  VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=commands,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
  VkCommandBuffer cb;VK(vkAllocateCommandBuffers(device,&ca,&cb));
  VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};VK(vkBeginCommandBuffer(cb,&begin));
  VkMemoryBarrier input={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT};
  VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_SHADER_READ_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
    .oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,.newLayout=VK_IMAGE_LAYOUT_GENERAL,
    .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
    .image=i->image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,LEVELS,0,1}};
  vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&input,0,NULL,1,&barrier);
  vkCmdCopyBufferToImage(cb,buffer,i->image,VK_IMAGE_LAYOUT_GENERAL,LEVELS,regions);
  barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
  barrier.oldLayout=barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
  vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);
  for(unsigned level=0;level<LEVELS;level++)regions[level].bufferOffset+=bytes;
  vkCmdCopyImageToBuffer(cb,i->image,VK_IMAGE_LAYOUT_GENERAL,buffer,LEVELS,regions);
  barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
  barrier.oldLayout=VK_IMAGE_LAYOUT_GENERAL;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkMemoryBarrier output={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
  vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_HOST_BIT,0,1,&output,0,NULL,1,&barrier);
  VK(vkEndCommandBuffer(cb));VkFence fence;VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VK(vkCreateFence(device,&fi,NULL,&fence));
  VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cb};
  VK(vkQueueSubmit(queue,1,&submit,fence));VK(vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX));
  if(!coherent)VK(vkInvalidateMappedMemoryRanges(device,1,&whole));
  CHECK(!memcmp(mapped,mapped+bytes,bytes));
  // Recheck against canonical vectors, so a symmetric wrong B2T/T2B pair
  // cannot turn an altered input Buffer into the expected reference.
  for(unsigned level=0;level<LEVELS;level++) {
    size_t size;uint8_t *linear=linear_source(format,level,0,&size);
    CHECK(!memcmp(mapped+regions[level].bufferOffset,linear,size));free(linear);
  }
  vkDestroyFence(device,fence,NULL);vkFreeCommandBuffers(device,commands,1,&cb);
  vkUnmapMemory(device,allocation);vkDestroyBuffer(device,buffer,NULL);vkFreeMemory(device,allocation,NULL);
  printf("{\"native_all_mip_buffer_copy\":true,\"format\":%u,\"width\":%u,\"height\":%u,\"levels\":%u,\"linear_bytes\":%llu,\"B2T_T2B_blocks_exact\":true}\n",format,FIXTURE_WIDTH,FIXTURE_HEIGHT,LEVELS,(unsigned long long)bytes);
}
