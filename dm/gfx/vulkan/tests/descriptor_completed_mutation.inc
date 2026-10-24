// Actual GPU oracle over the production context writer. No replacement encoder.
#include "nvn_vk_test_db_mutation_spv.h"
extern unsigned vk_validation_error_count(void);
unsigned vk_test_descriptor_completed_mutation(void) {
  unsigned checks=0;
#define DB_CHECK(x) do {checks++;if(!(x))die("Descriptor completed mutation check %u: %s",checks,#x);}while(0)
  DB_CHECK(V.descriptor_buffers && V.retained_draws);
  VkDeviceSize stride=V.props.limits.minUniformBufferOffsetAlignment;
  if(stride<V.props.limits.minStorageBufferOffsetAlignment)stride=V.props.limits.minStorageBufferOffsetAlignment;
  if(stride<256)stride=256;
  vk_buffer input={0},output={0};
  vk_buffer_create(&input,4*stride,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT);
  vk_buffer_create(&output,4*stride,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  uint32_t numbers[]={11,23,37,51};
  for(unsigned i=0;i<4;i++)memcpy(input.map+i*stride,numbers+i,4);
  memset(output.map,0,4*stride);
  VkBufferView texels[3];
  for(unsigned i=0;i<3;i++) {
    VkBufferViewCreateInfo ci={.sType=VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,.buffer=input.handle,
      .format=i==2?VK_FORMAT_R16_UINT:VK_FORMAT_R32_UINT,.offset=i*stride,.range=i==2?2:4};
    VK_CHECK(vkCreateBufferView(V.device,&ci,NULL,&texels[i]));
  }
  VkImage images[2];VkDeviceMemory memories[2];void *allocations[2];VkImageView views[2];VkSampler samplers[2];
  VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  for(unsigned i=0;i<2;i++) {
    VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R32_UINT,
      .extent={1,1,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
      .usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    VK_CHECK(vkCreateImage(V.device,&ci,NULL,&images[i]));VK_CHECK(vk_memory_image(images[i],&memories[i],&allocations[i]));
    VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=images[i],.viewType=VK_IMAGE_VIEW_TYPE_2D,
      .format=VK_FORMAT_R32_UINT,.subresourceRange=range};VK_CHECK(vkCreateImageView(V.device,&vi,NULL,&views[i]));
    VkSamplerCreateInfo si={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=VK_FILTER_NEAREST,.minFilter=VK_FILTER_NEAREST,
      .mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,.maxLod=1,.borderColor=VK_BORDER_COLOR_INT_OPAQUE_WHITE,
      .addressModeU=i?VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeV=i?VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};VK_CHECK(vkCreateSampler(V.device,&si,NULL,&samplers[i]));
  }
  mtl_sub_begin();
  for(unsigned i=0;i<2;i++) {
    VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
      .oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_GENERAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=images[i],.subresourceRange=range};
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&b);
    VkClearColorValue color={.uint32={i?19:7,0,0,0}};vkCmdClearColorImage(V.cmd,images[i],VK_IMAGE_LAYOUT_GENERAL,&color,1,&range);
    b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;b.oldLayout=VK_IMAGE_LAYOUT_GENERAL;
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,NULL,0,NULL,1,&b);
  }
  vk_internal_wait(vk_internal_commit());
  vk_binding bindings[4]={{.binding=0,.kind=1,.reg=3,.count=1},{.binding=1,.kind=2,.count=1},
    {.binding=2,.kind=7,.count=1},{.binding=3,.kind=3,.count=1}};
  for(unsigned i=0;i<2;i++){vk_binding swap=bindings[i];bindings[i]=bindings[3-i];bindings[3-i]=swap;}
  vk_shader shader={.nb=4,.stage=5,.set=0,.bindings=bindings};
  vk_shader *program[6]={NULL,NULL,NULL,NULL,NULL,&shader};
  VkPipelineLayout layout=vk_program_descriptor_buffer_layout(program);DB_CHECK(layout);
  VkShaderModule module;VkShaderModuleCreateInfo sm={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize=sizeof dm_test_db_mutation_spv,.pCode=dm_test_db_mutation_spv};VK_CHECK(vkCreateShaderModule(V.device,&sm,NULL,&module));
  VkComputePipelineCreateInfo pc={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.flags=VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
    .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"},.layout=layout};
  VkPipeline pipeline;VK_CHECK(vkCreateComputePipelines(V.device,V.pipeline_cache,1,&pc,NULL,&pipeline));
  VkCommandPool pool;VkCommandPoolCreateInfo cp={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=V.queue_family};
  VK_CHECK(vkCreateCommandPool(V.device,&cp,NULL,&pool));
  VkCommandBuffer secondary;VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_SECONDARY,.commandBufferCount=1};VK_CHECK(vkAllocateCommandBuffers(V.device,&ca,&secondary));
  VkBufferDeviceAddressInfo ai={.sType=VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,.buffer=input.handle};
  VkDeviceAddress input_address=vkGetBufferDeviceAddressKHR(V.device,&ai);DB_CHECK(input_address);
  draw_parameters *parameters=NULL;vk_descriptor_buffer_binding fixed[2]={{0}};
  unsigned submits=0,pending_rejects=0,atomic_rejects=0;
  VkImageLayout image_layout=VK_IMAGE_LAYOUT_GENERAL;
  for(unsigned round=0;round<8;round++) {
    // An even number of rounds would miss a two-context alternating-resource
    // bug; this private completed owner deliberately always reuses the same arena.
    mtl_sub_begin();draw_parameters_begin(parameters);
    VkImageLayout next_layout=round&1?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_GENERAL;
    if(next_layout!=image_layout)for(unsigned i=0;i<2;i++) {
      VkImageMemoryBarrier transition={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_SHADER_READ_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT,
        .oldLayout=image_layout,.newLayout=next_layout,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=images[i],.subresourceRange=range};
      vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,NULL,0,NULL,1,&transition);
    }
    image_layout=next_layout;
    vk_descriptor_buffer_binding current[2];
    for(unsigned slot=0;slot<2;slot++) {
      unsigned u=(round+slot)%4,t=(round+slot)%3,img=(round/2+slot)%2,samp=(round/3+slot)%2;
      vk_shader_prepared_values values={.shader=&shader,.count=4};
      values.values[0]=(vk_split_descriptor_value){(uint64_t)input.handle,u*stride,16};
      values.values[1]=(vk_split_descriptor_value){(uint64_t)output.handle,slot*stride,4};
      values.values[2]=(vk_split_descriptor_value){(uint64_t)texels[t],0,0};
      values.values[3]=(vk_split_descriptor_value){(uint64_t)views[img],(uint64_t)samplers[samp],image_layout};
      values.texel_addresses[2]=(VkDescriptorAddressInfoEXT){.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT,
        .address=input_address+t*stride,.range=t==2?2:4,.format=t==2?VK_FORMAT_R16_UINT:VK_FORMAT_R32_UINT};
      // Reflection order intentionally places the changed image first and UBO
      // last, to exercise atomic rejection after earlier valid changed fields.
      for(unsigned i=0;i<2;i++){vk_split_descriptor_value swap=values.values[i];values.values[i]=values.values[3-i];values.values[3-i]=swap;}
      values.texel_addresses[1]=values.texel_addresses[2];
      if(round && !slot) {
        draw_parameter_block *block=parameters->schemas->blocks;
        size_t bytes=block->db_schema->stride;uint8_t *before=malloc(bytes);DB_CHECK(before);
        memcpy(before,block->db_buffer.map,bytes);
        vk_split_descriptor_value old_values[4];memcpy(old_values,block->db_values,sizeof old_values);
        uint32_t initialized=block->db_initialized;
        vk_shader_prepared_values invalid=values;invalid.values[0].object=(uint64_t)views[1-img];invalid.values[3].range=0;
        vk_descriptor_buffer_binding rejected={0};
        DB_CHECK(!draw_descriptor_block_write(block,0,&invalid,&rejected));
        DB_CHECK(!rejected.address && !memcmp(before,block->db_buffer.map,bytes));
        DB_CHECK(!memcmp(old_values,block->db_values,sizeof old_values) && initialized==block->db_initialized);
        free(before);atomic_rejects++;
      }
      draw_parameters_write(&parameters,&values,&current[slot]);
      DB_CHECK(current[slot].address && !parameters->discard);
      if(round)DB_CHECK(current[slot].address==fixed[slot].address && current[slot].offset==fixed[slot].offset);
      else fixed[slot]=current[slot];
    }
    if(!round) {
      VkCommandBufferInheritanceInfo inherit={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
      VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.pInheritanceInfo=&inherit};
      VK_CHECK(vkBeginCommandBuffer(secondary,&bi));vkCmdBindPipeline(secondary,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
      for(unsigned slot=0;slot<2;slot++) {
        VkDescriptorBufferBindingInfoEXT db={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT,
          .address=fixed[slot].address,.usage=VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT|VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT};
        vkCmdBindDescriptorBuffersEXT(secondary,1,&db);uint32_t index=0;
        vkCmdSetDescriptorBufferOffsetsEXT(secondary,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&index,&fixed[slot].offset);
        vkCmdDispatch(secondary,1,1,1);
      }
      VK_CHECK(vkEndCommandBuffer(secondary));
    }
    vkCmdExecuteCommands(V.cmd,1,&secondary);
    VkMemoryBarrier mb={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&mb,0,NULL,0,NULL);
    draw_parameters_end(parameters);
    vk_graphics_submission owner={.parameters=parameters};
    DB_CHECK(parameters->last_serial>vk_completed_serial());
    DB_CHECK(!draw_parameters_reset_completed(&owner) && !vk_graphics_parameters_discard(&owner));pending_rejects++;
    vk_internal_wait(vk_internal_commit());submits++;
    for(unsigned slot=0;slot<2;slot++) {
      unsigned u=(round+slot)%4,t=(round+slot)%3,img=(round/2+slot)%2,samp=(round/3+slot)%2;
      uint32_t got;memcpy(&got,output.map+slot*stride,4);
      DB_CHECK(got==numbers[u]+numbers[t]+(samp?1:img?19:7));
    }
  }
  // Destroy the sole recording referencing the arena before its normal safe destruction.
  vkDestroyCommandPool(V.device,pool,NULL);
  vk_graphics_submission owner={.parameters=parameters};DB_CHECK(vk_graphics_parameters_discard(&owner));
  vkDestroyPipeline(V.device,pipeline,NULL);vkDestroyShaderModule(V.device,module,NULL);
  for(unsigned i=0;i<3;i++)vkDestroyBufferView(V.device,texels[i],NULL);
  for(unsigned i=0;i<2;i++) {vkDestroySampler(V.device,samplers[i],NULL);
    vkDestroyImageView(V.device,views[i],NULL);vkDestroyImage(V.device,images[i],NULL);vk_memory_free(allocations[i]);}
  vk_buffer_destroy(&input);vk_buffer_destroy(&output);free(shader.descriptor_buffer_schema);free(shader.uab_qualification);
  DB_CHECK(vk_validation_error_count()==0);
  LOG("[descriptor completed mutation GPU] records=1 executions=%u stable_arena_slots=2 changed_image_sampler_texel_ubo=1 pending_rejects=%u invalid_tail_atomic=%u",submits,pending_rejects,atomic_rejects);
#undef DB_CHECK
  return checks;
}
