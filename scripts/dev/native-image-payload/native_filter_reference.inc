// GPU-reference-only sampling oracle. Native data stays untouched; the private
// reference uses original linear_source + standard host-image-copy upload.
#include "native_filter_spv.h"
static VkPipeline filter_pipeline;
static VkPipelineLayout filter_layout;
static VkDescriptorSetLayout filter_descriptor;
static void filter_pipeline_create(void) {
  if(filter_pipeline)return;
  VkDescriptorSetLayoutBinding binding[]={
    {0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL},
    {1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL},
    {2,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL}};
  VkDescriptorSetLayoutCreateInfo dl={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=3,.pBindings=binding};
  VK(vkCreateDescriptorSetLayout(device,&dl,NULL,&filter_descriptor));
  VkPushConstantRange push={VK_SHADER_STAGE_COMPUTE_BIT,0,16};
  VkPipelineLayoutCreateInfo pl={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&filter_descriptor,.pushConstantRangeCount=1,.pPushConstantRanges=&push};
  VK(vkCreatePipelineLayout(device,&pl,NULL,&filter_layout));
  VkShaderModuleCreateInfo sm={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=sizeof dm_native_filter_spv,.pCode=dm_native_filter_spv};
  VkShaderModule shader;VK(vkCreateShaderModule(device,&sm,NULL,&shader));
  VkComputePipelineCreateInfo cp={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=shader,.pName="main"},.layout=filter_layout};
  VK(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&cp,NULL,&filter_pipeline));vkDestroyShaderModule(device,shader,NULL);
}
static void filter_compare_words(const uint32_t *words,uint32_t pixels,uint32_t capacity) {
  for(uint32_t n=0;n<pixels;n++) {
    CHECK(words[n*9+8]==0xf17e0000u+n);
    CHECK(!memcmp(words+n*9,words+n*9+4,4*sizeof(uint32_t)));
  }
  for(uint32_t n=pixels*9;n<capacity;n++)CHECK(words[n]==0xccccccccu);
}
static void native_filter_reference(image *native,VkFormat format,const char *phase) {
  VkFormatProperties3 f3={.sType=VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3};
  VkFormatProperties2 f2={.sType=VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,.pNext=&f3};
  vkGetPhysicalDeviceFormatProperties2(physical,format,&f2);
  int supported_linear=!!(f3.optimalTilingFeatures&VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
  printf("{\"native_filter_capability\":true,\"format\":%u,\"linear_filter_supported\":%s,\"phase\":\"%s\"}\n",format,supported_linear?"true":"false",phase);
  CHECK(supported_linear); // Unsupported is explicit, never silently a pass.
  filter_pipeline_create();
  int was_native=native_mode;native_mode=0;
  image reference=create_image(format); // pNext NULL: ordinary private optimal.
  native_mode=was_native;
  transition(&reference,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL);
  for(uint32_t level=0;level<LEVELS;level++) {
    size_t bytes;uint8_t *linear=linear_source(format,level,0,&bytes);
    CHECK(bytes);
    VkMemoryToImageCopyEXT copy={.sType=VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY_EXT,.pHostPointer=linear,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level,0,1},.imageExtent={mip(FIXTURE_WIDTH,level),mip(FIXTURE_HEIGHT,level),1}};
    VkCopyMemoryToImageInfoEXT upload={.sType=VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INFO_EXT,.dstImage=reference.image,.dstImageLayout=VK_IMAGE_LAYOUT_GENERAL,.regionCount=1,.pRegions=&copy};
    VK(vkCopyMemoryToImageEXT(device,&upload));free(linear);
  }
  transition(&reference,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  enum {CAPACITY=(FIXTURE_WIDTH+2)*(FIXTURE_HEIGHT+2)*9+32};
  VkBuffer output;VkDeviceMemory allocation;
  VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=CAPACITY*sizeof(uint32_t),.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
  VK(vkCreateBuffer(device,&bi,NULL,&output));VkMemoryRequirements needs;vkGetBufferMemoryRequirements(device,output,&needs);
  uint32_t type=pick_type(needs.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
  VkMemoryAllocateInfo ma={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=needs.size,.memoryTypeIndex=type};
  VK(vkAllocateMemory(device,&ma,NULL,&allocation));VK(vkBindBufferMemory(device,output,allocation,0));uint32_t *mapped;
  VK(vkMapMemory(device,allocation,0,VK_WHOLE_SIZE,0,(void **)&mapped));
  int coherent=!!(memory.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VkMappedMemoryRange whole={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=allocation,.size=VK_WHOLE_SIZE};
  VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=commands,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
  VkCommandBuffer cb;VK(vkAllocateCommandBuffers(device,&ca,&cb));
  VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;VK(vkCreateFence(device,&fi,NULL,&fence));
  unsigned dispatches=0;uint64_t compared_pixels=0;
  for(uint32_t base=0;base<LEVELS;base++) {
    VkImageView views[2];image *images[]={native,&reference};
    for(unsigned n=0;n<2;n++) {
      VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=images[n]->image,.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY,.format=format,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,base,LEVELS-base,0,1}};
      VK(vkCreateImageView(device,&vi,NULL,&views[n]));
    }
    for(unsigned mode=0;mode<2;mode++) {
      VkSamplerCreateInfo si={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=VK_FILTER_LINEAR,.minFilter=VK_FILTER_LINEAR,.mipmapMode=mode?VK_SAMPLER_MIPMAP_MODE_LINEAR:VK_SAMPLER_MIPMAP_MODE_NEAREST,.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.maxLod=LEVELS-base-1};
      VkSampler sampler;VK(vkCreateSampler(device,&si,NULL,&sampler));
      VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};
      VkDescriptorPoolCreateInfo dp={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=2,.pPoolSizes=sizes};
      VkDescriptorPool pool;VK(vkCreateDescriptorPool(device,&dp,NULL,&pool));
      VkDescriptorSetAllocateInfo da={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&filter_descriptor};
      VkDescriptorSet set;VK(vkAllocateDescriptorSets(device,&da,&set));
      VkDescriptorImageInfo im[2]={{sampler,views[0],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{sampler,views[1],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
      VkDescriptorBufferInfo bo={output,0,bi.size};
      VkWriteDescriptorSet writes[3];memset(writes,0,sizeof writes);
      for(unsigned n=0;n<3;n++){writes[n]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=n,.descriptorCount=1,.descriptorType=n<2?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pImageInfo=n<2?&im[n]:NULL,.pBufferInfo=n==2?&bo:NULL};}
      vkUpdateDescriptorSets(device,3,writes,0,NULL);
      for(uint32_t level=base;level<LEVELS;level++) {
        memset(mapped,0xcc,bi.size);if(!coherent)VK(vkFlushMappedMemoryRanges(device,1,&whole));
        VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};VK(vkBeginCommandBuffer(cb,&begin));
        VkMemoryBarrier ready={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
        vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&ready,0,NULL,0,NULL);
        vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,filter_pipeline);vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,filter_layout,0,1,&set,0,NULL);
        struct {uint32_t width,height;float lod,phase;} push={mip(FIXTURE_WIDTH,level)+2,mip(FIXTURE_HEIGHT,level)+2,(float)(level-base)+(mode?0.375f:0.0f),mode?0.625f:0.375f};
        vkCmdPushConstants(cb,filter_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof push,&push);vkCmdDispatch(cb,(push.width+7)/8,(push.height+7)/8,1);
        VkMemoryBarrier done={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
        vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&done,0,NULL,0,NULL);VK(vkEndCommandBuffer(cb));
        VkSubmitInfo sub={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cb};VK(vkQueueSubmit(queue,1,&sub,fence));VK(vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX));
        if(!coherent)VK(vkInvalidateMappedMemoryRanges(device,1,&whole));filter_compare_words(mapped,push.width*push.height,CAPACITY);compared_pixels+=push.width*push.height;
        VK(vkResetFences(device,1,&fence));VK(vkResetCommandPool(device,commands,0));dispatches++;
      }
      vkDestroyDescriptorPool(device,pool,NULL);vkDestroySampler(device,sampler,NULL);
    }
    for(unsigned n=0;n<2;n++)vkDestroyImageView(device,views[n],NULL);
  }
  CHECK(dispatches==LEVELS*(LEVELS+1));
  vkDestroyFence(device,fence,NULL);vkFreeCommandBuffers(device,commands,1,&cb);vkUnmapMemory(device,allocation);vkDestroyBuffer(device,output,NULL);vkFreeMemory(device,allocation,NULL);destroy_image(&reference);
  printf("{\"native_filter_GPU_reference\":true,\"format\":%u,\"width\":%u,\"height\":%u,\"levels\":%u,\"base_views\":%u,\"dispatches\":%u,\"compared_pixels\":%llu,\"every_mip_pixel_plus_edges\":true,\"phase\":\"%s\",\"bilinear_and_trilinear\":true,\"fractional_UV_and_LOD\":true,\"private_optimal_reference\":true,\"same_sampler\":true,\"RGBA_float_bits_exact\":true,\"unused_output_guard\":true,\"CPU_filter_or_decode\":false}\n",format,FIXTURE_WIDTH,FIXTURE_HEIGHT,LEVELS,LEVELS,dispatches,(unsigned long long)compared_pixels,phase);
}
