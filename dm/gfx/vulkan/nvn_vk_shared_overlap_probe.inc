// Extend the existing cold byte gate, with both VkDeviceMemory imports alive.
// Same memfd guest-RO/RW-import pair; no private ioctl or runtime instrumentation.
static int shared_overlap_probe(void *alias,uint8_t *guest,uint64_t bytes) {
  vk_buffer old={0},united={0};VkCommandPool commands=VK_NULL_HANDLE;VkFence fence=VK_NULL_HANDLE;
  int valid=0;
  if(bytes!=2*shared_alignment || shared_alignment<4096)return 0;
  if(shared_buffer_create((uint8_t *)alias+shared_alignment,shared_alignment,&old)!=VK_SUCCESS)goto done;
  if(shared_buffer_create(alias,bytes,&united)!=VK_SUCCESS)goto done;
  if(old.properties!=united.properties)goto done;
  VkCommandPoolCreateInfo pool={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=V.queue_family};
  if(vkCreateCommandPool(V.device,&pool,NULL,&commands)!=VK_SUCCESS)goto done;
  VkCommandBuffer cmd=VK_NULL_HANDLE;
  VkCommandBufferAllocateInfo alloc={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool=commands,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
  if(vkAllocateCommandBuffers(V.device,&alloc,&cmd)!=VK_SUCCESS)goto done;
  VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  if(vkBeginCommandBuffer(cmd,&begin)!=VK_SUCCESS)goto done;
  // Old narrow-handle writer -> new wider-handle reader.
  vkCmdFillBuffer(cmd,old.handle,0,64,0x14253647);
  VkMemoryBarrier alias_read={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT};
  vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&alias_read,0,NULL,0,NULL);
  VkBufferCopy forward={.srcOffset=shared_alignment,.dstOffset=0,.size=64};
  vkCmdCopyBuffer(cmd,united.handle,united.handle,1,&forward);
  // New wider-handle writer -> old narrow-handle reader. Destinations never
  // overlap their physical sources, even though the buffer handles alias.
  vkCmdFillBuffer(cmd,united.handle,shared_alignment+64,64,0x8a9bacbd);
  vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&alias_read,0,NULL,0,NULL);
  VkBufferCopy reverse={.srcOffset=64,.dstOffset=128,.size=64};
  vkCmdCopyBuffer(cmd,old.handle,united.handle,1,&reverse);
  VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
  vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
  if(vkEndCommandBuffer(cmd)!=VK_SUCCESS)goto done;
  VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  if(vkCreateFence(V.device,&fi,NULL,&fence)!=VK_SUCCESS)goto done;
  VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};
  if(vkQueueSubmit(V.queue,1,&submit,fence)!=VK_SUCCESS)goto done;
  if(vkWaitForFences(V.device,1,&fence,VK_TRUE,30000000000ull)!=VK_SUCCESS)
    die("Shared overlap probe queue did not finish; do not free pending imports");
  shared_cache(&united,0,bytes,1);shared_cache(&old,0,shared_alignment,1);
  valid=1;
  for(unsigned k=0;k<64;k+=4) {
    uint32_t a,b,c,d;memcpy(&a,guest+k,4);memcpy(&b,guest+128+k,4);
    memcpy(&c,guest+shared_alignment+k,4);memcpy(&d,guest+shared_alignment+64+k,4);
    if(a!=0x14253647 || b!=0x8a9bacbd || c!=0x14253647 || d!=0x8a9bacbd)valid=0;
  }
 done:
  if(fence)vkDestroyFence(V.device,fence,NULL);
  if(commands)vkDestroyCommandPool(V.device,commands,NULL);
  vk_buffer_destroy(&united);vk_buffer_destroy(&old);
  LOG("[shared pool] cold overlapping-import bidirectional GPU byte gate: %s",valid?"passed":"unsupported/failed");
  return valid;
}
