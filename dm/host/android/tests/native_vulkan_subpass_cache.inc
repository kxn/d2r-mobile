// Host-only owned shaders exercise the production immutable object caches.
// This checks real GPU semantics, not whether a mobile driver merges tiles.
#ifdef DM_VULKAN_SUBPASS_TEST_SHADERS
#include "nvn_vk_test_subpass_spv.h"
#include "nvn_vk_present_vert_spv.h"
extern void *vk_test_render_pass_acquire(const VkRenderPassCreateInfo *,VkRenderPass *);
extern void vk_test_render_pass_release(void *);
extern int vk_test_render_pass_supported(const VkRenderPassCreateInfo *);
extern void *vk_test_framebuffer_acquire(void *,const VkFramebufferCreateInfo *,VkFramebuffer *);
extern void vk_test_framebuffer_release(void *);
static VkShaderModule subpass_fixture_shader(const uint32_t *words,size_t size) {
  VkShaderModuleCreateInfo ci={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=size,.pCode=words};
  VkShaderModule module;VK_CHECK(vkCreateShaderModule(V.device,&ci,NULL,&module));return module;
}
static void subpass_render_cache_test(void) {
  const VkFormat formats[]={VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_R8G8B8A8_UNORM,
    VK_FORMAT_B10G11R11_UFLOAT_PACK32,VK_FORMAT_R8G8B8A8_UNORM};
  VkImage images[4];VkDeviceMemory allocations[4];VkImageView views[4];
  for(unsigned k=0;k<4;k++) {
    VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
      .format=formats[k],.extent={16,16,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,
      .tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|
        (k<3?VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT:0)};
    VK_CHECK(vkCreateImage(V.device,&ci,NULL,&images[k]));VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(V.device,images[k],&requirements);
    VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=requirements.size,
      .memoryTypeIndex=vk_memory_type(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    VK_CHECK(vkAllocateMemory(V.device,&ai,NULL,&allocations[k]));
    VK_CHECK(vkBindImageMemory(V.device,images[k],allocations[k],0));
    VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=images[k],
      .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=formats[k],.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    VK_CHECK(vkCreateImageView(V.device,&vi,NULL,&views[k]));
  }
  VkAttachmentDescription attachments[5]={0};
  for(unsigned k=0;k<5;k++)attachments[k]=(VkAttachmentDescription){.format=formats[k%4],
    .samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=k<3?VK_ATTACHMENT_LOAD_OP_CLEAR:VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    .storeOp=VK_ATTACHMENT_STORE_OP_STORE,.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    .stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,
    .finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
  VkAttachmentReference colors[]={{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
    {1,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},{2,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
    {4,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
  VkAttachmentReference inputs[]={{0,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
    {1,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{2,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
  VkAttachmentReference output={3,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpasses[]={
    {.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=3,.pColorAttachments=colors},
    {.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.inputAttachmentCount=3,.pInputAttachments=inputs,
      .colorAttachmentCount=1,.pColorAttachments=&output}};
  VkSubpassDependency dependencies[]={
    {VK_SUBPASS_EXTERNAL,0,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      0,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_DEPENDENCY_BY_REGION_BIT},
    {0,1,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_INPUT_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      VK_DEPENDENCY_BY_REGION_BIT},
    {1,VK_SUBPASS_EXTERNAL,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_INPUT_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      VK_ACCESS_TRANSFER_READ_BIT,VK_DEPENDENCY_BY_REGION_BIT}};
  VkRenderPassCreateInfo rp={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=4,
    .pAttachments=attachments,.subpassCount=2,.pSubpasses=subpasses,.dependencyCount=3,.pDependencies=dependencies};
  CHECK(vk_test_render_pass_supported(&rp));
  VkDescriptorSetLayoutBinding bindings[3];
  for(unsigned k=0;k<3;k++)bindings[k]=(VkDescriptorSetLayoutBinding){.binding=k,
    .descriptorType=VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT};
  VkDescriptorSetLayoutCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .bindingCount=3,.pBindings=bindings};VkDescriptorSetLayout descriptor_layout;
  VK_CHECK(vkCreateDescriptorSetLayout(V.device,&dci,NULL,&descriptor_layout));
  VkPipelineLayoutCreateInfo lci={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount=1,.pSetLayouts=&descriptor_layout};VkPipelineLayout layout;
  VK_CHECK(vkCreatePipelineLayout(V.device,&lci,NULL,&layout));
  VkDescriptorPoolSize pool_size={VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,3};
  VkDescriptorPoolCreateInfo pci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    .maxSets=1,.poolSizeCount=1,.pPoolSizes=&pool_size};VkDescriptorPool pool;
  VK_CHECK(vkCreateDescriptorPool(V.device,&pci,NULL,&pool));
  VkDescriptorSetAllocateInfo dai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    .descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&descriptor_layout};VkDescriptorSet set;
  VK_CHECK(vkAllocateDescriptorSets(V.device,&dai,&set));
  VkShaderModule modules[]={subpass_fixture_shader(dm_present_vert_spv,sizeof dm_present_vert_spv),
    subpass_fixture_shader(dm_test_subpass_spv,sizeof dm_test_subpass_spv)};
  VkPipelineShaderStageCreateInfo stages[]={
    {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=modules[0],.pName="main"},
    {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=modules[1],.pName="main"}};
  VkPipelineVertexInputStateCreateInfo vertex={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo assembly={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    .topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
  VkViewport viewport={0,0,16,16,0,1};VkRect2D area={{0,0},{16,16}};
  VkPipelineViewportStateCreateInfo viewport_state={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    .viewportCount=1,.pViewports=&viewport,.scissorCount=1,.pScissors=&area};
  VkPipelineRasterizationStateCreateInfo raster={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    .polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,.lineWidth=1};
  VkPipelineMultisampleStateCreateInfo samples={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
  VkPipelineColorBlendAttachmentState blend={.colorWriteMask=15};
  VkPipelineColorBlendStateCreateInfo blend_state={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    .attachmentCount=1,.pAttachments=&blend};
  VkGraphicsPipelineCreateInfo pipeline_ci={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .stageCount=2,.pStages=stages,.pVertexInputState=&vertex,.pInputAssemblyState=&assembly,
    .pViewportState=&viewport_state,.pRasterizationState=&raster,.pMultisampleState=&samples,
    .pColorBlendState=&blend_state,.layout=layout,.subpass=1};
  VkRenderPass previous=VK_NULL_HANDLE;
  for(unsigned variant=0;variant<2;variant++) {
    inputs[0].attachment=variant?2:0;inputs[2].attachment=variant?0:2;
    VkRenderPass pass,again;void *owner=vk_test_render_pass_acquire(&rp,&pass);
    void *duplicate=vk_test_render_pass_acquire(&rp,&again);CHECK(pass==again);
    vk_test_render_pass_release(duplicate);if(previous)CHECK(pass!=previous);previous=pass;
    VkFramebufferCreateInfo fci={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=pass,
      .attachmentCount=4,.pAttachments=views,.width=16,.height=16,.layers=1};
    VkFramebuffer framebuffer,fb_again;void *fb_owner=vk_test_framebuffer_acquire(owner,&fci,&framebuffer);
    void *fb_duplicate=vk_test_framebuffer_acquire(owner,&fci,&fb_again);CHECK(framebuffer==fb_again);
    vk_test_framebuffer_release(fb_duplicate);
    VkDescriptorImageInfo values[3];VkWriteDescriptorSet writes[3];
    for(unsigned k=0;k<3;k++) {
      values[k]=(VkDescriptorImageInfo){.imageView=views[inputs[k].attachment],.imageLayout=inputs[k].layout};
      writes[k]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,
        .dstBinding=k,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,.pImageInfo=&values[k]};
    }
    vkUpdateDescriptorSets(V.device,3,writes,0,NULL);
    pipeline_ci.renderPass=pass;VkPipeline pipeline;
    VK_CHECK(vkCreateGraphicsPipelines(V.device,VK_NULL_HANDLE,1,&pipeline_ci,NULL,&pipeline));
    const VkClearValue clear[]={ {.color={{.25f,0,0,1}}},{.color={{0,.5f,0,1}}},
      {.color={{0,0,.75f,1}}},{.color={{0,0,0,1}}} };
    mtl_sub_begin();VkRenderPassBeginInfo begin={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass=pass,.framebuffer=framebuffer,.renderArea=area,.clearValueCount=4,.pClearValues=clear};
    vkCmdBeginRenderPass(V.cmd,&begin,VK_SUBPASS_CONTENTS_INLINE);
    vkCmdNextSubpass(V.cmd,VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(V.cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
    vkCmdBindDescriptorSets(V.cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&set,0,NULL);
    vkCmdDraw(V.cmd,3,1,0,0);vkCmdEndRenderPass(V.cmd);
    vk_buffer *readback=vk_temp_buffer(1024);
    VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={16,16,1}};
    vkCmdCopyImageToBuffer(V.cmd,images[3],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback->handle,1,&copy);
    VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
    uint8_t result[1024];vk_effect_copy(result,readback->map,sizeof result);
    vk_internal_wait(vk_internal_commit());mtl_finish();
    const uint8_t expected[]={variant?0:64,128,variant?0:191,255};
    for(unsigned pixel=0;pixel<256;pixel++)CHECK(!memcmp(result+pixel*4,expected,4));
    vkDestroyPipeline(V.device,pipeline,NULL);vk_test_framebuffer_release(fb_owner);vk_test_render_pass_release(owner);
  }
  // Full immutable topology participates in identity, including dependencies
  // and input layouts. These descriptions are valid but need no extra draw.
  VkRenderPass base,changed;void *owner=vk_test_render_pass_acquire(&rp,&base),*other;
  dependencies[1].dependencyFlags=0;other=vk_test_render_pass_acquire(&rp,&changed);CHECK(base!=changed);
  vk_test_render_pass_release(other);dependencies[1].dependencyFlags=VK_DEPENDENCY_BY_REGION_BIT;
  inputs[0].layout=VK_IMAGE_LAYOUT_GENERAL;other=vk_test_render_pass_acquire(&rp,&changed);CHECK(base!=changed);
  vk_test_render_pass_release(other);inputs[0].layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  uint32_t keep=4;rp.attachmentCount=5;subpasses[0].colorAttachmentCount=4;
  subpasses[1].preserveAttachmentCount=1;subpasses[1].pPreserveAttachments=&keep;
  VkRenderPass preserved;void *preserved_owner=vk_test_render_pass_acquire(&rp,&preserved);
  subpasses[1].preserveAttachmentCount=0;other=vk_test_render_pass_acquire(&rp,&changed);CHECK(preserved!=changed);
  vk_test_render_pass_release(other);vk_test_render_pass_release(preserved_owner);
  rp.attachmentCount=4;subpasses[0].colorAttachmentCount=3;rp.subpassCount=1;rp.dependencyCount=0;
  other=vk_test_render_pass_acquire(&rp,&changed);CHECK(base!=changed);vk_test_render_pass_release(other);
  rp.subpassCount=2;rp.dependencyCount=3;
  rp.subpassCount=3;CHECK(!vk_test_render_pass_supported(&rp));rp.subpassCount=2;
  inputs[0].attachment=4;CHECK(!vk_test_render_pass_supported(&rp));inputs[0].attachment=2;
  subpasses[1].pInputAttachments=NULL;CHECK(!vk_test_render_pass_supported(&rp));subpasses[1].pInputAttachments=inputs;
  dependencies[0].dstSubpass=2;CHECK(!vk_test_render_pass_supported(&rp));dependencies[0].dstSubpass=0;
  vk_test_render_pass_release(owner);
  vkDestroyDescriptorPool(V.device,pool,NULL);vkDestroyPipelineLayout(V.device,layout,NULL);
  vkDestroyDescriptorSetLayout(V.device,descriptor_layout,NULL);
  for(unsigned k=0;k<2;k++)vkDestroyShaderModule(V.device,modules[k],NULL);
  for(unsigned k=0;k<4;k++) {
    vk_render_view_retired(views[k]);vkDestroyImageView(V.device,views[k],NULL);
    vkDestroyImage(V.device,images[k],NULL);vkFreeMemory(V.device,allocations[k],NULL);
  }
  LOG("[vulkan multi-subpass cache fixture] three real mixed-format input attachments, source permutation GPU pixels, topology/dependency/layout/preserve keys and framebuffer reuse passed");
}
#else
static void subpass_render_cache_test(void) {}
#endif
