// Event payloads in registered NVN pools are GPU commands, not CPU completion
// stores. This preserves their position relative to later buffer readers and
// writers. Signal location/flags and general WaitEvent are unchanged upstream
// limitations; a VkEvent is not a uint32 NVN event payload.
#include "nvn_vk_shader.h"
#include "nvn_vk_event_add_spv.h"

static VkDescriptorSetLayout event_descriptors;
static VkPipelineLayout event_layout;
static VkPipeline event_add_pipeline;
static void event_add_create(void) {
  VkDescriptorSetLayoutBinding binding={.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
  VkDescriptorSetLayoutCreateInfo di={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .bindingCount=1,.pBindings=&binding};
  VK_CHECK(vkCreateDescriptorSetLayout(V.device,&di,NULL,&event_descriptors));
  VkPushConstantRange push={VK_SHADER_STAGE_COMPUTE_BIT,0,2*sizeof(uint32_t)};
  VkPipelineLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount=1,.pSetLayouts=&event_descriptors,.pushConstantRangeCount=1,.pPushConstantRanges=&push};
  VK_CHECK(vkCreatePipelineLayout(V.device,&li,NULL,&event_layout));
  VkShaderModuleCreateInfo mi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize=sizeof dm_event_add_spv,.pCode=dm_event_add_spv};
  VkShaderModule module;VK_CHECK(vkCreateShaderModule(V.device,&mi,NULL,&module));
  VkComputePipelineCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"},.layout=event_layout};
  VK_CHECK(vkCreateComputePipelines(V.device,V.pipeline_cache,1,&pi,NULL,&event_add_pipeline));
  vkDestroyShaderModule(V.device,module,NULL);
}
int vk_event_signal(uint64_t address,uint32_t mode,uint32_t value) {
  if(!V.cmd || mode>1)die("Vulkan event signal outside recording/mode");
  vk_pool_span_info info;
  if(vk_pool_span_query(address,1,&info)==VK_POOL_SPAN_NONE)return 0;
  if(address%sizeof(uint32_t))die("Vulkan event payload alignment");
  // An existing Pool must never silently take the non-pool CPU-post fallback.
  if(vk_pool_span_query(address,sizeof(uint32_t),&info)==VK_POOL_SPAN_NONE)
    die("Vulkan event payload outside Pool bounds");
  VkPipelineStageFlags stage=mode?VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT:VK_PIPELINE_STAGE_TRANSFER_BIT;
  vk_pool_requirement requirement={address,sizeof(uint32_t),0,stage,1,0};
  int operation=vk_pool_operation_begin_ranges(&requirement,1);
  vk_buffer *buffer;uint64_t offset;
  int found=mode?vk_pool_address_stage(address,4,(void **)&buffer,&offset,stage):
    vk_pool_address_overwrite(address,4,(void **)&buffer,&offset,stage);
  if(!found)die("Vulkan event Pool retired during recording");
  vk_buffer_use use={address,sizeof(uint32_t),1};
  // Match the original transfer path: the lookup drains deferred-mip alias
  // accesses, then dependencies recheck histories after pass-end replay. The
  // admitted backing is retained across callbacks; descriptors are not yet
  // allocated. The final writer/WB records the actual current submission.
  if(mode)vk_compute_buffer_dependencies(&use,1,0);
  else vk_transfer_buffer_dependencies(&use,1);
  // The common recorder currently discards NVN signal location/flags. Retain
  // the old completion-post lower bound: publishing an event must not pass
  // earlier unrelated raster work. This is one real event release boundary,
  // not a per-draw or per-resource global barrier.
  VkMemoryBarrier release={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT,
    .dstAccessMask=mode?VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT:VK_ACCESS_TRANSFER_WRITE_BIT};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,stage,0,1,&release,0,NULL,0,NULL);
  if(mode) {
    uint64_t alignment=V.props.limits.minStorageBufferOffsetAlignment;
    if(!alignment || (alignment&(alignment-1)))die("Vulkan event storage alignment");
    uint64_t base=offset&~(alignment-1),range=offset-base+sizeof(uint32_t);
    if(range>V.props.limits.maxStorageBufferRange || range>buffer->size-base)
      die("Vulkan event storage range");
    if(!event_add_pipeline)event_add_create();
    VkDescriptorSet set=vk_descriptor_allocate(event_descriptors);
    VkDescriptorBufferInfo binding={buffer->handle,base,range};
    VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet=set,.dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo=&binding};
    vkUpdateDescriptorSets(V.device,1,&write,0,NULL);
    uint32_t push[]={(uint32_t)((offset-base)/sizeof(uint32_t)),value};
    vk_compute_bind_pipeline(event_add_pipeline);
    vk_compute_bind_descriptors(event_layout,0,1,&set,0,NULL);
    vkCmdPushConstants(V.cmd,event_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof push,push);
    vkCmdDispatch(V.cmd,1,1,1);
    vk_buffer_shader_written(address,sizeof(uint32_t),stage);
  } else {
    vkCmdFillBuffer(V.cmd,buffer->handle,offset,sizeof(uint32_t),value);
    vk_buffer_transfer_written_range(address,sizeof(uint32_t));
    vk_pool_overwritten(address,sizeof(uint32_t));
  }
  vk_buffer_writeback_stage(address,sizeof(uint32_t),stage);
  vk_pool_operation_end(operation);
  return 1;
}
