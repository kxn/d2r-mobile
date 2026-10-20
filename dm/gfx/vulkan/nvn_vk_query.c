// GPU-native publication of the original NVN two-word counter ABI.
#include "nvn_vk_shader.h"
#ifdef DM_VULKAN_SHARED_POOLS
#include "nvn_vk_query_publish_spv.h"
static VkDescriptorSetLayout query_descriptors;
static VkPipelineLayout query_layout;
static VkPipeline query_pipeline;
static int clock_ready,clock_supported;
static uint32_t clock_multiplier;
static int32_t clock_shift;

int vk_query_clock_parameters(float period,uint32_t bits,uint32_t *multiplier,int32_t *shift) {
  uint32_t value;memcpy(&value,&period,sizeof value);
  uint32_t exponent=(value>>23)&255u,mantissa=value&0x7fffffu;
  if(!bits || bits>64 || (value>>31) || exponent==255 || (!exponent&&!mantissa))return 0;
  if(exponent)mantissa|=0x800000u;
  int32_t power=exponent?(int32_t)exponent-150:-149;
  uint32_t factor=mantissa*12u;
  uint64_t maximum=bits==64?UINT64_MAX:((UINT64_C(1)<<bits)-1);
  __uint128_t product=(__uint128_t)maximum*factor;
  if(power>=0) {
    if(power>=128 || product>(~(__uint128_t)0>>power))return 0;
    if((product<<power)>=((__uint128_t)1<<64)*625u)return 0;
  } else {
    product/=625u;
    if(-power>=128)product=0;else product>>=-power;
    if(product>UINT64_MAX)return 0;
  }
  *multiplier=factor;*shift=power;return 1;
}
int vk_query_native_supported(void) {
  if(!clock_ready) {clock_ready=1;clock_supported=vk_query_clock_parameters(
      V.props.limits.timestampPeriod,V.timestamp_bits,&clock_multiplier,&clock_shift);}
  return clock_supported;
}
static void query_pipeline_create(void) {
  if(query_pipeline)return;
  VkDescriptorSetLayoutBinding bindings[2]={
    {0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL},
    {1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL}};
  VkDescriptorSetLayoutCreateInfo descriptors={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .bindingCount=2,.pBindings=bindings};
  VK_CHECK(vkCreateDescriptorSetLayout(V.device,&descriptors,NULL,&query_descriptors));
  VkPushConstantRange push={VK_SHADER_STAGE_COMPUTE_BIT,0,24};
  VkPipelineLayoutCreateInfo layout={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount=1,.pSetLayouts=&query_descriptors,.pushConstantRangeCount=1,.pPushConstantRanges=&push};
  VK_CHECK(vkCreatePipelineLayout(V.device,&layout,NULL,&query_layout));
  VkShaderModuleCreateInfo module_info={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize=sizeof dm_query_publish_spv,.pCode=dm_query_publish_spv};VkShaderModule module;
  VK_CHECK(vkCreateShaderModule(V.device,&module_info,NULL,&module));
  VkComputePipelineCreateInfo pipeline={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,
      .module=module,.pName="main"},.layout=query_layout};
  VK_CHECK(vkCreateComputePipelines(V.device,V.pipeline_cache,1,&pipeline,NULL,&query_pipeline));
  vkDestroyShaderModule(V.device,module,NULL);
}
static void query_dispatch(vk_buffer *source,uint64_t bytes,vk_buffer *destination,uint64_t offset,
    uint32_t count,uint32_t samples,uint32_t multiplier,int32_t shift,uint32_t bits) {
  uint64_t alignment=V.props.limits.minStorageBufferOffsetAlignment;
  if(!alignment || (alignment&(alignment-1)) || (offset&3))die("Native query descriptor alignment");
  uint64_t base=offset&~(alignment-1),range=offset-base+16;
  if(bytes>V.props.limits.maxStorageBufferRange || range>V.props.limits.maxStorageBufferRange)
    die("Native query prefix exceeds storage range");
  query_pipeline_create();
  VkDescriptorSet set=vk_descriptor_allocate(query_descriptors);
  VkDescriptorBufferInfo buffers[2]={{source->handle,0,bytes},{destination->handle,base,range}};
  VkWriteDescriptorSet writes[2];
  for(unsigned k=0;k<2;k++)writes[k]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet=set,.dstBinding=k,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&buffers[k]};
  vkUpdateDescriptorSets(V.device,2,writes,0,NULL);
  struct {uint32_t multiplier;int32_t shift;uint32_t bits,count,word,samples;} push={multiplier,shift,bits,count,(uint32_t)((offset-base)/4),samples};
  vkCmdBindPipeline(V.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,query_pipeline);
  vkCmdBindDescriptorSets(V.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,query_layout,0,1,&set,0,NULL);
  vkCmdPushConstants(V.cmd,query_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof push,&push);
  vkCmdDispatch(V.cmd,1,1,1);
}
void vk_query_native_publish(uint64_t address,VkQueryPool timestamp,uint32_t slot,
    const vk_query_native_source *prefix,uint32_t count,uint32_t samples) {
  if(!V.cmd || vk_pool_shared_destination(address,16)!=1 || !vk_query_native_supported())
    die("Native shared query publication unavailable");
  if(count>(UINT32_MAX/8)-1 || (count&&!prefix))die("Native query prefix bounds");
  uint64_t bytes=((uint64_t)count+1)*8;
  if(bytes>V.props.limits.maxStorageBufferRange)die("Native query prefix exceeds device limit");
  vk_pool_requirement need={address,16,0,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,1,0};
  int operation=vk_pool_operation_begin_ranges(&need,1);
  vk_buffer *target;uint64_t offset;
  if(!vk_pool_address_overwrite(address,16,(void **)&target,&offset,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT))
    die("Native query registered destination disappeared");
  vk_buffer_use use={address,16,1};vk_compute_buffer_dependencies(&use,1,0);
  // All alias/deferred callbacks precede allocation/copy/descriptor borrowing.
  vk_buffer *gather=vk_temp_buffer(bytes);
  vkCmdCopyQueryPoolResults(V.cmd,timestamp,slot,1,gather->handle,0,8,
      VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT);
  for(uint32_t k=0;k<count;k++)
    vkCmdCopyQueryPoolResults(V.cmd,prefix[k].pool,prefix[k].index,1,gather->handle,8+8*(uint64_t)k,8,
        VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT);
  VkBufferMemoryBarrier ready={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT,
    .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
    .buffer=gather->handle,.offset=0,.size=bytes};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      0,0,NULL,1,&ready,0,NULL);
  query_dispatch(gather,bytes,target,offset,count,samples,clock_multiplier,clock_shift,V.timestamp_bits);
  vk_pool_overwritten(address,16);
  vk_buffer_shader_written(address,16,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  vk_buffer_writeback_stage(address,16,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  vk_pool_operation_end(operation);
}
#ifdef DM_VULKAN_TEST_EMBED
int vk_query_native_test_dispatch(vk_buffer *source,vk_buffer *destination,uint64_t offset,
    float period,uint32_t bits,uint32_t count,uint32_t samples) {
  uint32_t multiplier;int32_t shift;
  if(!vk_query_clock_parameters(period,bits,&multiplier,&shift))return 0;
  if(!V.cmd || count>(UINT32_MAX/8)-1 || ((uint64_t)count+1)*8>source->size ||
      offset>destination->size || 16>destination->size-offset)die("Native query math fixture bounds");
  query_dispatch(source,((uint64_t)count+1)*8,destination,offset,count,samples,multiplier,shift,bits);
  return 1;
}
#endif

#else
int vk_query_native_supported(void) {return 0;}
#endif
