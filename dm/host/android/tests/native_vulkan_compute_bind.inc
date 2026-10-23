#ifndef DM_ANDROID_APP
// Cold functional observation of actual original Vulkan calls, never timing.
static PFN_vkCmdBindPipeline compute_bind_original_pipeline;
static PFN_vkCmdBindDescriptorSets compute_bind_original_sets;
static PFN_vkCmdDispatch compute_bind_original_dispatch;
static unsigned compute_bind_pipeline_calls,compute_bind_set_calls,compute_bind_dispatches;
static struct {unsigned pipelines,sets;} compute_bind_dispatch_calls[128];
static VKAPI_ATTR void VKAPI_CALL compute_bind_observe_pipeline(VkCommandBuffer command,
    VkPipelineBindPoint point,VkPipeline pipeline) {
  if(point==VK_PIPELINE_BIND_POINT_COMPUTE)compute_bind_pipeline_calls++;
  compute_bind_original_pipeline(command,point,pipeline);
}
static VKAPI_ATTR void VKAPI_CALL compute_bind_observe_sets(VkCommandBuffer command,
    VkPipelineBindPoint point,VkPipelineLayout layout,uint32_t first,uint32_t count,
    const VkDescriptorSet *sets,uint32_t offsets,const uint32_t *values) {
  if(point==VK_PIPELINE_BIND_POINT_COMPUTE)compute_bind_set_calls++;
  compute_bind_original_sets(command,point,layout,first,count,sets,offsets,values);
}
static VKAPI_ATTR void VKAPI_CALL compute_bind_observe_dispatch(VkCommandBuffer command,
    uint32_t x,uint32_t y,uint32_t z) {
  CHECK(compute_bind_dispatches<128);
  compute_bind_dispatch_calls[compute_bind_dispatches].pipelines=compute_bind_pipeline_calls;
  compute_bind_dispatch_calls[compute_bind_dispatches++].sets=compute_bind_set_calls;
  compute_bind_original_dispatch(command,x,y,z);
}
static void compute_bind_copy_result(vk_buffer *output,vk_buffer *result,
    uint64_t offset,unsigned step) {
  VkMemoryBarrier dependency={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&dependency,0,NULL,0,NULL);
  VkBufferCopy copy={.srcOffset=offset,.dstOffset=step*256,.size=256};
  vkCmdCopyBuffer(V.cmd,output->handle,result->handle,1,&copy);
}
static void compute_bind_direct_test(void) {
  CHECK(!V.cmd && V.props.limits.maxBoundDescriptorSets>=4);
  uint8_t control[0x880];memset(control,2,sizeof control);
  vk_shader *shader=vk_shader_get(control);CHECK(shader->set==2 && shader->nb==2);
  VkDescriptorSetLayoutBinding bindings[2]={{1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL},
    {2,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL}};
  VkDescriptorSetLayoutCreateInfo di={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  VkDescriptorSetLayout empty,storage;VK_CHECK(vkCreateDescriptorSetLayout(V.device,&di,NULL,&empty));
  di.bindingCount=2;di.pBindings=bindings;VK_CHECK(vkCreateDescriptorSetLayout(V.device,&di,NULL,&storage));
  VkDescriptorSetLayout layouts[]={empty,empty,storage,empty};
  VkPipelineLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=4,.pSetLayouts=layouts};
  VkPipelineLayout pipeline_layout[2];VkPipeline pipelines[2];
  for(unsigned n=0;n<2;n++) {
    VK_CHECK(vkCreatePipelineLayout(V.device,&li,NULL,&pipeline_layout[n]));
    VkComputePipelineCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=shader->module,.pName="main"},
      .layout=pipeline_layout[n]};
    VK_CHECK(vkCreateComputePipelines(V.device,V.pipeline_cache,1,&pi,NULL,&pipelines[n]));
  }
  VkDescriptorPoolSize size={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,4};
  VkDescriptorPoolCreateInfo pci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=3,.poolSizeCount=1,.pPoolSizes=&size};
  VkDescriptorPool pool;VK_CHECK(vkCreateDescriptorPool(V.device,&pci,NULL,&pool));
  VkDescriptorSetLayout allocate_layouts[]={storage,storage,empty};
  VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    .descriptorPool=pool,.descriptorSetCount=3,.pSetLayouts=allocate_layouts};
  VkDescriptorSet sets[3];VK_CHECK(vkAllocateDescriptorSets(V.device,&ai,sets));
  uint64_t align=V.props.limits.minStorageBufferOffsetAlignment;
  uint64_t stride=(256+align-1)/align*align;CHECK(stride<=UINT32_MAX);
  vk_buffer input={0},output={0},result={0};
  vk_buffer_create(&input,stride*2,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  vk_buffer_create(&output,stride*2,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
  vk_buffer_create(&result,16*256,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  memset(result.map,0,16*256);
  for(unsigned n=0;n<2;n++)for(unsigned k=0;k<64;k++) {
    uint32_t v=n*1000+k;memcpy(input.map+stride*n+k*4,&v,4);
  }
  for(unsigned n=0;n<2;n++) {
    VkDescriptorBufferInfo values[]={{input.handle,n*stride,256},{output.handle,0,256}};
    VkWriteDescriptorSet writes[2];
    for(unsigned k=0;k<2;k++)writes[k]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet=sets[n],.dstBinding=k+1,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,.pBufferInfo=values+k};
    vkUpdateDescriptorSets(V.device,2,writes,0,NULL);
  }
  VkCommandPoolCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=V.queue_family};
  VkCommandPool commands;VK_CHECK(vkCreateCommandPool(V.device,&ci,NULL,&commands));
  VkCommandBuffer primary[2],secondary;
  VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool=commands,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=2};
  VK_CHECK(vkAllocateCommandBuffers(V.device,&ca,primary));
  ca.level=VK_COMMAND_BUFFER_LEVEL_SECONDARY;ca.commandBufferCount=1;
  VK_CHECK(vkAllocateCommandBuffers(V.device,&ca,&secondary));
  VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
  unsigned expected[16]={0};unsigned step=0;
  for(unsigned round=0;round<3;round++) {
    // round1 switches handles; round2 resets and reuses round0's exact handle.
    V.cmd=primary[round==1];if(round==2)VK_CHECK(vkResetCommandBuffer(V.cmd,0));
    VK_CHECK(vk_command_begin(V.cmd,&begin));
    unsigned p0=compute_bind_pipeline_calls,d0=compute_bind_set_calls;
    VkDescriptorSet vector[]={sets[0],sets[2]};uint32_t offsets[]={0,0};
    vk_compute_bind_pipeline(pipelines[0]);
    vk_compute_bind_descriptors(pipeline_layout[0],2,2,vector,2,offsets);
    CHECK(compute_bind_pipeline_calls==p0+1 && compute_bind_set_calls==d0+1);
    if(round==0) {
      for(unsigned test=0;test<8;test++) {
        unsigned pc=compute_bind_pipeline_calls,dc=compute_bind_set_calls;
        vk_compute_bind_pipeline(pipelines[test>=6]);
        if(test==2)offsets[0]=offsets[1]=(uint32_t)stride;
        if(test>=3)offsets[0]=offsets[1]=0;
        if(test==3)vector[0]=sets[1];if(test==5)vector[0]=sets[0];
        VkPipelineLayout layout=pipeline_layout[test==7];
        if(test==4)vk_compute_bind_descriptors(layout,0,1,&sets[2],0,NULL);
        else vk_compute_bind_descriptors(layout,2,test==3?1:2,vector,2,offsets);
        CHECK(compute_bind_pipeline_calls==pc+(test==6));
        CHECK(compute_bind_set_calls==dc+((test>=2 && test<=5)||test==7));
        VkMemoryBarrier order={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
          .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT,
          .dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
        vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&order,0,NULL,0,NULL);
        vkCmdDispatch(V.cmd,1,1,1);expected[step]=test>=2&&test<=4;
        compute_bind_copy_result(&output,&result,test==2?stride:0,step++);
      }
    } else {
      VkMemoryBarrier order={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT,
        .dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
      vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&order,0,NULL,0,NULL);
      vkCmdDispatch(V.cmd,1,1,1);compute_bind_copy_result(&output,&result,0,step++);
    }
    if(round==2) {
      VkCommandBuffer primary_saved=V.cmd;
      VkCommandBufferInheritanceInfo inherit={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
      VkCommandBufferBeginInfo sb=begin;sb.pInheritanceInfo=&inherit;
      VK_CHECK(vk_command_begin(secondary,&sb));V.cmd=secondary;
      vk_compute_bind_pipeline(pipelines[1]);
      vk_compute_bind_descriptors(pipeline_layout[1],2,1,&sets[1],2,offsets);
      VkMemoryBarrier before={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
      vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&before,0,NULL,0,NULL);
      vkCmdDispatch(V.cmd,1,1,1);expected[step]=1;compute_bind_copy_result(&output,&result,0,step++);
      VK_CHECK(vkEndCommandBuffer(secondary));V.cmd=primary_saved;
      // Returning to a still-recording primary also cannot borrow secondary state.
      unsigned pc=compute_bind_pipeline_calls,dc=compute_bind_set_calls;
      vk_compute_bind_pipeline(pipelines[0]);vk_compute_bind_descriptors(pipeline_layout[0],2,2,vector,2,offsets);
      CHECK(compute_bind_pipeline_calls==pc+1 && compute_bind_set_calls==dc+1);
      vk_command_execute(1,&secondary);
      pc=compute_bind_pipeline_calls;dc=compute_bind_set_calls;
      vk_compute_bind_pipeline(pipelines[0]);vk_compute_bind_descriptors(pipeline_layout[0],2,2,vector,2,offsets);
      CHECK(compute_bind_pipeline_calls==pc+1 && compute_bind_set_calls==dc+1);
      VkMemoryBarrier order={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT,.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT};
      vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&order,0,NULL,0,NULL);
      vkCmdDispatch(V.cmd,1,1,1);compute_bind_copy_result(&output,&result,0,step++);
    }
    VkMemoryBarrier read={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&read,0,NULL,0,NULL);
    VK_CHECK(vkEndCommandBuffer(V.cmd));
    VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&V.cmd};
    VkFence fence;VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VK_CHECK(vkCreateFence(V.device,&fi,NULL,&fence));
    pthread_mutex_lock(&V.queue_mu);VK_CHECK(vkQueueSubmit(V.queue,1,&si,fence));pthread_mutex_unlock(&V.queue_mu);
    VK_CHECK(vkWaitForFences(V.device,1,&fence,VK_TRUE,5000000000ull));vkDestroyFence(V.device,fence,NULL);V.cmd=VK_NULL_HANDLE;
  }
  for(unsigned n=0;n<step;n++)for(unsigned k=0;k<64;k++) {
    uint32_t v;memcpy(&v,result.map+n*256+k*4,4);CHECK(v==(expected[n]*1000+k)*3+7);
  }
  vkDestroyCommandPool(V.device,commands,NULL);vkDestroyDescriptorPool(V.device,pool,NULL);
  for(unsigned n=0;n<2;n++){vkDestroyPipeline(V.device,pipelines[n],NULL);vkDestroyPipelineLayout(V.device,pipeline_layout[n],NULL);}
  vkDestroyDescriptorSetLayout(V.device,storage,NULL);vkDestroyDescriptorSetLayout(V.device,empty,NULL);
  vk_buffer_destroy(&input);vk_buffer_destroy(&output);vk_buffer_destroy(&result);
}
static void compute_bind_test(void) {
  mtl_init();compute_bind_original_pipeline=vkCmdBindPipeline;compute_bind_original_sets=vkCmdBindDescriptorSets;
  compute_bind_original_dispatch=vkCmdDispatch;vkCmdBindPipeline=compute_bind_observe_pipeline;
  vkCmdBindDescriptorSets=compute_bind_observe_sets;vkCmdDispatch=compute_bind_observe_dispatch;
  gpu_event_payload_test();CHECK(compute_bind_dispatches>=4);
  unsigned n=compute_bind_dispatches-4;
  CHECK(compute_bind_dispatch_calls[n+1].pipelines==compute_bind_dispatch_calls[n].pipelines);
  CHECK(compute_bind_dispatch_calls[n+1].sets==compute_bind_dispatch_calls[n].sets);
  for(unsigned k=2;k<4;k++) {
    CHECK(compute_bind_dispatch_calls[n+k].pipelines==compute_bind_dispatch_calls[n+k-1].pipelines+1);
    CHECK(compute_bind_dispatch_calls[n+k].sets==compute_bind_dispatch_calls[n+k-1].sets+1);
  }
  compute_bind_direct_test();vkCmdBindPipeline=compute_bind_original_pipeline;
  vkCmdBindDescriptorSets=compute_bind_original_sets;vkCmdDispatch=compute_bind_original_dispatch;
  CHECK(vk_validation_error_count()==0);
  printf("{\"test\":\"compute-bind-state\",\"checks\":%u,\"result\":\"passed\",\"game_modules_executed\":false}\n",checks);
}
#endif
