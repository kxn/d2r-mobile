#include "../nvn_vk_uab.h"
#include "nvn_vk_test_uab_spv.h"
static int uab_parameters_test(void) {
  if(!V.retained_draws) {printf("{\"test\":\"UAB parameters\",\"result\":\"not_enabled\",\"actual_feature_enabled\":false}\n");return 0;}
  unsigned checks_before=checks;
  VkDeviceSize stride=V.props.limits.minUniformBufferOffsetAlignment;
  if(stride<V.props.limits.minStorageBufferOffsetAlignment)stride=V.props.limits.minStorageBufferOffsetAlignment;
  if(stride<256)stride=256;
  vk_buffer input={0},output={0};vk_buffer_create(&input,4*stride,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT);
  vk_buffer_create(&output,8*stride,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  uint32_t numbers[]={11,23,37,51};for(unsigned i=0;i<4;i++)memcpy(input.map+i*stride,&numbers[i],4);
  memset(output.map,0,8*stride);
  VkBufferView texel,other;
  VkBufferViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,.buffer=input.handle,.format=VK_FORMAT_R32_UINT,.range=4};
  VK_CHECK(vkCreateBufferView(V.device,&vi,NULL,&texel));vi.offset=stride;VK_CHECK(vkCreateBufferView(V.device,&vi,NULL,&other));
  VkImage image;VkDeviceMemory memory;void *allocation;VkImageView image_view;VkSampler sampler;
  VkImageCreateInfo ii={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R32_UINT,
    .extent={1,1,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT};
  VK_CHECK(vkCreateImage(V.device,&ii,NULL,&image));VK_CHECK(vk_memory_image(image,&memory,&allocation));
  VkImageViewCreateInfo iv={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=image,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=VK_FORMAT_R32_UINT,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
  VK_CHECK(vkCreateImageView(V.device,&iv,NULL,&image_view));
  VkSamplerCreateInfo si={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=VK_FILTER_NEAREST,.minFilter=VK_FILTER_NEAREST,.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,.maxLod=1};
  VK_CHECK(vkCreateSampler(V.device,&si,NULL,&sampler));
  vk_uab_schema schema={.set=0,.count=4,.dynamic_ordinal={1}};
  VkDescriptorType types[]={VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER};
  for(unsigned i=0;i<4;i++)schema.bindings[i]=(VkDescriptorSetLayoutBinding){.binding=i,.descriptorType=types[i],.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
  vk_binding reflected[4]={{.binding=0,.kind=1,.reg=3,.count=1},{.binding=1,.kind=2,.count=1},{.binding=2,.kind=7,.count=1},{.binding=3,.kind=3,.count=1}};
  vk_shader reflected_shader={.nb=4,.stage=5,.set=0,.bindings=reflected};reflected_shader.dynamic_uniform_ordinals[0][0]=1;
  vk_uab_schema actual;CHECK(vk_uab_schema_from_shader(&reflected_shader,1,&actual));
  for(unsigned n=0;n<4;n++)CHECK(actual.bindings[n].descriptorType==schema.bindings[n].descriptorType && actual.bindings[n].binding==n);
  reflected[2].count=2;CHECK(!vk_uab_schema_from_shader(&reflected_shader,1,&actual));reflected[2].count=1;
  vk_split_descriptor_value high[4]={{1,UINT64_C(0x100000000),16}},normalized[4];vk_uniform_offsets high_offsets={.count=1,.offsets={0x123400}};
  CHECK(vk_uab_normalize_values(&schema,high,&high_offsets,normalized));CHECK(normalized[0].offset==UINT64_C(0x100123400));
  vk_uab_packet *packet=vk_uab_packet_create(&schema,2,2);CHECK(packet);
  vk_uab_schema invalid=schema;invalid.bindings[0].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;CHECK(!vk_uab_packet_create(&invalid,2,2));
  CHECK(!vk_uab_packet_create(&schema,VK_UAB_MAX_DRAWS+1,2));
  VkPipelineLayout pipeline_layout;vk_uab_packet *stage_sets[6]={packet};
  CHECK(vk_uab_pipeline_layout_create(stage_sets,NULL,0,&pipeline_layout));
  VkShaderModule shader;VkShaderModuleCreateInfo sci={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=sizeof dm_test_uab_spv,.pCode=dm_test_uab_spv};VK_CHECK(vkCreateShaderModule(V.device,&sci,NULL,&shader));
  VkComputePipelineCreateInfo pci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=shader,.pName="main"},.layout=pipeline_layout};
  VkPipeline pipeline;VK_CHECK(vkCreateComputePipelines(V.device,V.pipeline_cache,1,&pci,NULL,&pipeline));
  VkCommandPool pool;VkCommandPoolCreateInfo cp={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=V.queue_family};VK_CHECK(vkCreateCommandPool(V.device,&cp,NULL,&pool));
  VkCommandBuffer cb[2];VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_SECONDARY,.commandBufferCount=2};VK_CHECK(vkAllocateCommandBuffers(V.device,&ca,cb));
  mtl_sub_begin();
  VkImageMemoryBarrier ib={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_GENERAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=image,.subresourceRange=iv.subresourceRange};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&ib);
  VkClearColorValue color={.uint32={7,0,0,0}};vkCmdClearColorImage(V.cmd,image,VK_IMAGE_LAYOUT_GENERAL,&color,1,&iv.subresourceRange);
  ib.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;ib.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;ib.oldLayout=VK_IMAGE_LAYOUT_GENERAL;
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,NULL,0,NULL,1,&ib);
  vk_split_descriptor_value values[4]={{(uint64_t)input.handle,0,16},{(uint64_t)output.handle,0,4},{(uint64_t)texel,0,0},{(uint64_t)image_view,(uint64_t)sampler,VK_IMAGE_LAYOUT_GENERAL}},complete[4];
  for(unsigned v=0;v<2;v++) {
    CHECK(vk_uab_packet_begin(packet)==(int)v);CHECK(!vk_uab_packet_seal(packet,v));
    for(unsigned draw=0;draw<2;draw++) {
      vk_uniform_offsets offsets={.count=1,.offsets={(v*2+draw)*stride}};values[1].offset=(v*2+draw)*stride;
      CHECK(vk_uab_normalize_values(&schema,values,&offsets,complete));CHECK(complete[0].offset==(v*2+draw)*stride);
      CHECK(vk_uab_packet_write(packet,v,draw,complete));
    }
    CHECK(vk_uab_packet_seal(packet,v));
    VkCommandBufferInheritanceInfo inherit={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.pInheritanceInfo=&inherit};VK_CHECK(vkBeginCommandBuffer(cb[v],&begin));
    vkCmdBindPipeline(cb[v],VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
    for(unsigned draw=0;draw<2;draw++) {VkDescriptorSet set=vk_uab_packet_set(packet,v,draw);CHECK(set);vkCmdBindDescriptorSets(cb[v],VK_PIPELINE_BIND_POINT_COMPUTE,pipeline_layout,0,1,&set,0,NULL);vkCmdDispatch(cb[v],1,1,1);}
    VK_CHECK(vkEndCommandBuffer(cb[v]));vkCmdExecuteCommands(V.cmd,1,&cb[v]);
    CHECK(vk_uab_packet_pending(packet,v,vk_recording_serial()));CHECK(!vk_uab_packet_write(packet,v,0,complete));CHECK(!vk_uab_packet_abandon(packet,v));
  }
  CHECK(vk_uab_packet_begin(packet)==-1);CHECK(!vk_uab_packet_destroy(packet));
  VkMemoryBarrier readback={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&readback,0,NULL,0,NULL);
  vk_internal_wait(vk_internal_commit());
  for(unsigned i=0;i<4;i++){uint32_t got;memcpy(&got,output.map+i*stride,4);CHECK(got==numbers[i]+18);}
  CHECK(vk_uab_packet_begin(packet)==0);
  vk_split_descriptor_value bad[4];memcpy(bad,complete,sizeof bad);bad[2].object=(uint64_t)other;
  CHECK(!vk_uab_packet_write(packet,0,0,bad)); // immutable mismatch before any writes
  bad[2].object=(uint64_t)texel;bad[0].offset=1;CHECK(!vk_uab_packet_write(packet,0,0,bad));
  values[0].offset=UINT64_MAX;vk_uniform_offsets overflow={.count=1,.offsets={1}};CHECK(!vk_uab_normalize_values(&schema,values,&overflow,bad));values[0].offset=0;
  for(unsigned draw=0;draw<2;draw++) {vk_uniform_offsets offsets={.count=1,.offsets={(3-draw)*stride}};values[1].offset=(4+draw)*stride;CHECK(vk_uab_normalize_values(&schema,values,&offsets,complete));CHECK(vk_uab_packet_write(packet,0,draw,complete));}
  CHECK(vk_uab_packet_seal(packet,0));mtl_sub_begin();vkCmdExecuteCommands(V.cmd,1,&cb[0]);CHECK(vk_uab_packet_pending(packet,0,vk_recording_serial()));
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&readback,0,NULL,0,NULL);vk_internal_wait(vk_internal_commit());
  for(unsigned draw=0;draw<2;draw++){uint32_t got;memcpy(&got,output.map+(4+draw)*stride,4);CHECK(got==numbers[3-draw]+18);}
  vkDestroyCommandPool(V.device,pool,NULL);vkDestroyPipeline(V.device,pipeline,NULL);vkDestroyPipelineLayout(V.device,pipeline_layout,NULL);vkDestroyShaderModule(V.device,shader,NULL);CHECK(vk_uab_packet_destroy(packet));
  vkDestroySampler(V.device,sampler,NULL);vkDestroyImageView(V.device,image_view,NULL);vkDestroyImage(V.device,image,NULL);vk_memory_free(allocation);vkDestroyBufferView(V.device,texel,NULL);vkDestroyBufferView(V.device,other,NULL);vk_buffer_destroy(&input);vk_buffer_destroy(&output);
  CHECK(vk_validation_error_count()==0);
  printf("{\"test\":\"UAB parameters\",\"result\":\"passed\",\"checks\":%u,\"actual_feature_enabled\":true,\"secondary_reexecuted_without_recording\":true,\"mixed_image_texel_shader_reads\":true,\"game_modules_executed\":false}\n",checks-checks_before);return 0;
}
