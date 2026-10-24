#include "../nvn_vk_uab.h"
#include "nvn_vk_test_uab_spv.h"
static int uab_parameters_test(void) {
  if(!V.retained_draws) {printf("{\"test\":\"UAB parameters\",\"result\":\"not_enabled\",\"actual_feature_enabled\":false}\n");return 0;}
  unsigned checks_before=checks;
  extern void vk_test_uab_batch_stats(uint64_t *,uint64_t *);
  int batched=getenv("DM_UAB_BATCH_FIXTURE")!=NULL;
  unsigned batch_capacity=getenv("DM_UAB_BATCH_CAPACITY")?atoi(getenv("DM_UAB_BATCH_CAPACITY")):5;
  vk_uab_batch *batch=batched?vk_uab_batch_create(batch_capacity):NULL;
  if(batched){CHECK(batch);CHECK(!vk_uab_batch_create(1025));CHECK(vk_uab_batch_begin(batch));CHECK(!vk_uab_batch_begin(batch));CHECK(!vk_uab_batch_destroy(batch));}

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
  vk_uab_packet *packet=vk_uab_packet_create(&schema,3,1);CHECK(packet);
  vk_uab_packet *packets[2]={packet,vk_uab_packet_create(&schema,3,1)};CHECK(packets[1]);
  vk_uab_schema invalid=schema;invalid.bindings[0].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;CHECK(!vk_uab_packet_create(&invalid,2,2));
  CHECK(!vk_uab_packet_create(&schema,VK_UAB_MAX_DRAWS+1,2));
  VkPipelineLayout explicit_layout;vk_uab_packet *stage_sets[6]={packet};
  CHECK(vk_uab_pipeline_layout_create(stage_sets,NULL,0,&explicit_layout));vkDestroyPipelineLayout(V.device,explicit_layout,NULL);
  vk_shader *program[6]={NULL,NULL,NULL,NULL,NULL,&reflected_shader};
  VkPipelineLayout pipeline_layout=vk_program_layout_mode(program,VK_DESCRIPTOR_LAYOUT_UAB);CHECK(pipeline_layout);
  CHECK(vk_program_uab_layout(program)==pipeline_layout);
  CHECK(vk_shader_uab_layout(&reflected_shader)==vk_uab_packet_layout(packet));
  // The original intern sorts flags with binding keys, independent of reflection order.
  vk_uab_schema reversed=schema;
  for(unsigned i=0;i<4;i++)reversed.bindings[i]=schema.bindings[3-i];
  vk_uab_packet *reordered=vk_uab_packet_create(&reversed,1,1);CHECK(reordered);
  CHECK(vk_uab_packet_layout(reordered)==vk_uab_packet_layout(packet));CHECK(vk_uab_packet_destroy(reordered));
  VkDescriptorSetLayout saved=vk_shader_uab_layout(&reflected_shader);
  // Immutable ABI qualification is performed once, not once per draw. These
  // counters observe real helper entries; the existing compute pixels below
  // still use the resulting Vulkan layout and ordinary UBO/SSBO descriptors.
  extern void vk_test_uab_qualification_stats(uint64_t *,uint64_t *);
  uint64_t qs0,qp0,qs1,qp1;vk_test_uab_qualification_stats(&qs0,&qp0);
  for(unsigned repeat=0;repeat<256;repeat++)CHECK(vk_program_uab_layout(program)==pipeline_layout);
  vk_test_uab_qualification_stats(&qs1,&qp1);CHECK(qs1==qs0 && qp1==qp0);
  // An alternative copied from an already qualified shader must not inherit
  // the old schema/layout, even before the production final-schema reset.
  vk_binding alternative_binding={.binding=199,.kind=1,.count=1};
  vk_shader alternative=reflected_shader;alternative.nb=1;alternative.bindings=&alternative_binding;
  VkDescriptorSetLayout alt_layout=vk_shader_uab_layout(&alternative);CHECK(alt_layout && alt_layout!=saved);
  vk_test_uab_qualification_stats(&qs1,&qp1);CHECK(qs1==qs0+1 && qp1==qp0);
  CHECK(vk_shader_uab_layout(&reflected_shader)==saved);
  // Inject limits only for isolated, never-used shader identities, then restore
  // the device properties. Production properties are immutable for the device.
  VkPhysicalDeviceDescriptorIndexingProperties limits_saved=V.uab_properties;
  vk_binding rejected_binding={.binding=200,.kind=1,.count=1};
  vk_shader rejected={.nb=1,.stage=5,.set=3,.bindings=&rejected_binding};
  V.uab_properties.maxPerStageUpdateAfterBindResources=0;
  vk_test_uab_qualification_stats(&qs0,&qp0);
  for(unsigned repeat=0;repeat<256;repeat++)CHECK(!vk_shader_uab_layout(&rejected));
  vk_test_uab_qualification_stats(&qs1,&qp1);CHECK(qs1==qs0+1 && qp1==qp0);
  V.uab_properties=limits_saved;
  vk_binding aggregate_bindings[2]={{.binding=201,.kind=1,.count=1},{.binding=202,.kind=1,.count=1}};
  vk_shader aggregate_shaders[2]={{.nb=1,.stage=0,.set=0,.bindings=&aggregate_bindings[0]},
    {.nb=1,.stage=1,.set=1,.bindings=&aggregate_bindings[1]}};
  vk_shader *aggregate_program[6]={&aggregate_shaders[0],&aggregate_shaders[1]};
  V.uab_properties.maxDescriptorSetUpdateAfterBindUniformBuffers=1;
  vk_test_uab_qualification_stats(&qs0,&qp0);
  for(unsigned repeat=0;repeat<256;repeat++)CHECK(!vk_program_uab_layout(aggregate_program));
  vk_test_uab_qualification_stats(&qs1,&qp1);CHECK(qs1==qs0+2 && qp1==qp0+1);
  V.uab_properties=limits_saved;
  LOG("[UAB immutable qualification fixture] warm256 no rebuild; alternate isolated; shader caps-negative once; aggregate caps-negative once");

  // Same numeric fields, no UAB creation/binding flags: a distinct compatible-type layout.
  extern VkDescriptorSetLayout vk_test_uab_plain_layout(const vk_uab_schema *);
  reflected_shader.descriptors=vk_test_uab_plain_layout(&schema);VkPipelineLayout ordinary=vk_program_layout_mode(program,0);CHECK(ordinary!=pipeline_layout);

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
    vk_uab_packet *active=packets[v];
    CHECK(vk_uab_packet_begin(active)==0);CHECK(!vk_uab_packet_seal(active,0));
    CHECK(!vk_uab_packet_set(active,0,0));CHECK(!vk_uab_packet_set(active,0,2));
    VkCommandBufferInheritanceInfo inherit={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.pInheritanceInfo=&inherit};VK_CHECK(vkBeginCommandBuffer(cb[v],&begin));
    vkCmdBindPipeline(cb[v],VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
    for(unsigned draw=0;draw<2;draw++) {
      vk_uniform_offsets offsets={.count=1,.offsets={(v*2+draw)*stride}};values[1].offset=(v*2+draw)*stride;
      CHECK(vk_uab_normalize_values(&schema,values,&offsets,complete));CHECK(complete[0].offset==(v*2+draw)*stride);
      CHECK(vk_uab_packet_write_batch(active,0,draw,complete,batch));
      vk_split_descriptor_value duplicate[4];memcpy(duplicate,complete,sizeof duplicate);duplicate[0].offset=((v*2+draw+1)%4)*stride;
      CHECK(!vk_uab_packet_write_batch(active,0,draw,duplicate,batch));
      VkDescriptorSet set=vk_uab_packet_set(active,0,draw);CHECK(set);
      vkCmdBindDescriptorSets(cb[v],VK_PIPELINE_BIND_POINT_COMPUTE,pipeline_layout,0,1,&set,0,NULL);vkCmdDispatch(cb[v],1,1,1);
    }
    CHECK(vk_uab_packet_seal(active,0));CHECK(!vk_uab_packet_set(active,0,2));
    VK_CHECK(vkEndCommandBuffer(cb[v]));vkCmdExecuteCommands(V.cmd,1,&cb[v]);
    CHECK(vk_uab_packet_pending(active,0,vk_recording_serial()));CHECK(!vk_uab_packet_write(active,0,0,complete));CHECK(!vk_uab_packet_abandon(active,0));
  }
  CHECK(vk_uab_packet_begin(packet)==-1);CHECK(!vk_uab_packet_destroy(packet));
  VkMemoryBarrier readback={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&readback,0,NULL,0,NULL);
  if(batch)CHECK(vk_uab_batch_end(batch));
  vk_internal_wait(vk_internal_commit());
  for(unsigned i=0;i<4;i++){uint32_t got;memcpy(&got,output.map+i*stride,4);CHECK(got==numbers[i]+18);}
  CHECK(vk_uab_packet_begin(packets[1])==0);
  CHECK(vk_uab_packet_begin(packet)==0);CHECK(!vk_uab_packet_set(packet,0,0));CHECK(!vk_uab_packet_set(packet,0,1));
  if(batch)CHECK(vk_uab_batch_begin(batch));
  vk_split_descriptor_value bad[4];memcpy(bad,complete,sizeof bad);bad[2].object=(uint64_t)other;
  CHECK(!vk_uab_packet_write_batch(packet,0,0,bad,batch)); // immutable mismatch before any writes
  bad[2].object=(uint64_t)texel;bad[0].offset=1;CHECK(!vk_uab_packet_write_batch(packet,0,0,bad,batch));
  values[0].offset=UINT64_MAX;vk_uniform_offsets overflow={.count=1,.offsets={1}};CHECK(!vk_uab_normalize_values(&schema,values,&overflow,bad));values[0].offset=0;
  uint64_t calls_before,writes_before,calls_now,writes_now;vk_test_uab_batch_stats(&calls_before,&writes_before);
  for(unsigned v=0;v<2;v++)for(unsigned draw=0;draw<2;draw++) {
    vk_uniform_offsets offsets={.count=1,.offsets={(3-(v*2+draw))*stride}};values[1].offset=(4+v*2+draw)*stride;
    CHECK(vk_uab_normalize_values(&schema,values,&offsets,complete));
    CHECK(vk_uab_packet_write_batch(packets[v],0,draw,complete,batch));
    CHECK(vk_uab_packet_set(packets[v],0,draw)); // warm UAB-only update need not flush yet
  }
  vk_test_uab_batch_stats(&calls_now,&writes_now);
  if(batch){CHECK(calls_now-calls_before==7/batch_capacity);CHECK(writes_now-writes_before==(7/batch_capacity)*batch_capacity);}
  for(unsigned v=0;v<2;v++)CHECK(vk_uab_packet_seal(packets[v],0));
  mtl_sub_begin();vkCmdExecuteCommands(V.cmd,2,cb);
  for(unsigned v=0;v<2;v++)CHECK(vk_uab_packet_pending(packets[v],0,vk_recording_serial()));
  if(batch)CHECK(vk_uab_batch_end(batch));
  vk_test_uab_batch_stats(&calls_now,&writes_now);
  if(batch){CHECK(calls_now-calls_before==(8+batch_capacity-1)/batch_capacity);CHECK(writes_now-writes_before==8);}
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&readback,0,NULL,0,NULL);vk_internal_wait(vk_internal_commit());
  for(unsigned draw=0;draw<4;draw++){uint32_t got;memcpy(&got,output.map+(4+draw)*stride,4);CHECK(got==numbers[3-draw]+18);}
  uint64_t warm_calls=calls_now-calls_before,warm_writes=writes_now-writes_before;
  // Same resolved Buffer tuple: touch this frame, but emit no descriptor update.
  CHECK(vk_uab_packet_begin(packet)==0);if(batch)CHECK(vk_uab_batch_begin(batch));
  vk_test_uab_batch_stats(&calls_before,&writes_before);
  for(unsigned draw=0;draw<2;draw++) {
    vk_uniform_offsets offsets={.count=1,.offsets={(3-draw)*stride}};values[1].offset=(4+draw)*stride;
    CHECK(vk_uab_normalize_values(&schema,values,&offsets,complete));CHECK(vk_uab_packet_write_batch(packet,0,draw,complete,batch));
  }
  CHECK(vk_uab_packet_seal(packet,0));mtl_sub_begin();vkCmdExecuteCommands(V.cmd,1,&cb[0]);CHECK(vk_uab_packet_pending(packet,0,vk_recording_serial()));
  if(batch)CHECK(vk_uab_batch_end(batch));vk_test_uab_batch_stats(&calls_now,&writes_now);
  CHECK(calls_now==calls_before);CHECK(writes_now==writes_before);
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&readback,0,NULL,0,NULL);vk_internal_wait(vk_internal_commit());
  for(unsigned draw=0;draw<2;draw++){uint32_t got;memcpy(&got,output.map+(4+draw)*stride,4);CHECK(got==numbers[3-draw]+18);}
  if(batch) {
    // Never leave queued driver payload pointers into a destroyed packet.
    CHECK(vk_uab_batch_begin(batch));vk_uab_packet *temporary=vk_uab_packet_create(&schema,1,1);CHECK(temporary);
    CHECK(vk_uab_packet_begin(temporary)==0);CHECK(vk_uab_packet_write_batch(temporary,0,0,complete,batch));
    CHECK(vk_uab_packet_abandon(temporary,0));CHECK(vk_uab_packet_destroy(temporary));vk_uab_batch_flush(batch);
    temporary=vk_uab_packet_create(&schema,1,1);CHECK(temporary);CHECK(vk_uab_packet_begin(temporary)==0);
    CHECK(vk_uab_packet_write_batch(temporary,0,0,complete,batch));CHECK(vk_uab_packet_destroy(temporary));vk_uab_batch_flush(batch);
    CHECK(vk_uab_batch_end(batch));CHECK(vk_uab_batch_destroy(batch));
  }
  vkDestroyCommandPool(V.device,pool,NULL);CHECK(vk_uab_packet_destroy(packets[1]));vkDestroyPipeline(V.device,pipeline,NULL);vkDestroyShaderModule(V.device,shader,NULL);CHECK(vk_uab_packet_destroy(packet));CHECK(vk_shader_uab_layout(&reflected_shader)==saved);CHECK(vk_program_uab_layout(program)==pipeline_layout);
  vkDestroySampler(V.device,sampler,NULL);vkDestroyImageView(V.device,image_view,NULL);vkDestroyImage(V.device,image,NULL);vk_memory_free(allocation);vkDestroyBufferView(V.device,texel,NULL);vkDestroyBufferView(V.device,other,NULL);vk_buffer_destroy(&input);vk_buffer_destroy(&output);
  CHECK(vk_validation_error_count()==0);
  printf("{\"test\":\"UAB parameters\",\"result\":\"passed\",\"checks\":%u,\"actual_feature_enabled\":true,\"secondary_reexecuted_without_recording\":true,\"mixed_image_texel_shader_reads\":true,\"batched_update_test\":%s,\"batch_capacity\":%u,\"warm_update_calls\":%llu,\"warm_update_writes\":%llu,\"unchanged_update_calls\":0,\"partial_blocks\":true,\"duplicate_slots_refused\":true,\"game_modules_executed\":false}\n",checks-checks_before,batched?"true":"false",batched?batch_capacity:0,(unsigned long long)warm_calls,(unsigned long long)warm_writes);return 0;
}
