// Explicit experiment: a fixed host shader presents a full-size RGBA8 image.
// Pipeline/layout/sampler live with the device, targets with their swapchain.
#include "nvn_vk_present_shader.h"
#include "nvn_vk_shader.h"
#include "nvn_vk_present_vert_spv.h"
#include "nvn_vk_present_frag_spv.h"
static VkDescriptorSetLayout present_shader_descriptors;
static VkPipelineLayout present_shader_layout;
static VkRenderPass present_shader_pass;
static VkPipeline present_shader_pipeline;
static VkSampler present_shader_sampler;
int vk_present_shader_eligible(VkFormat source,VkSampleCountFlagBits samples,
    VkExtent3D size,VkImageUsageFlags source_usage,VkFormat destination,
    VkExtent2D output,VkImageUsageFlags destination_usage,int full_source) {
  return full_source && source==VK_FORMAT_R8G8B8A8_UNORM && destination==source &&
      samples==VK_SAMPLE_COUNT_1_BIT && size.depth==1 && size.width && size.height &&
      size.width==output.width && size.height==output.height &&
      (source_usage&VK_IMAGE_USAGE_SAMPLED_BIT) &&
      (destination_usage&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
}
static void present_shader_initialize(void) {
  if(present_shader_pipeline)return;
  VkDescriptorSetLayoutBinding binding={0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,
      VK_SHADER_STAGE_FRAGMENT_BIT,NULL};
  VkDescriptorSetLayoutCreateInfo di={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount=1,.pBindings=&binding};
  VK_CHECK(vkCreateDescriptorSetLayout(V.device,&di,NULL,&present_shader_descriptors));
  VkPipelineLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount=1,.pSetLayouts=&present_shader_descriptors};
  VK_CHECK(vkCreatePipelineLayout(V.device,&li,NULL,&present_shader_layout));
  VkSamplerCreateInfo si={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter=VK_FILTER_NEAREST,.minFilter=VK_FILTER_NEAREST,
      .mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,
      .addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
  VK_CHECK(vkCreateSampler(V.device,&si,NULL,&present_shader_sampler));
  VkAttachmentDescription attachment={.format=VK_FORMAT_R8G8B8A8_UNORM,
      .samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .storeOp=VK_ATTACHMENT_STORE_OP_STORE,.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .initialLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference reference={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,
      .colorAttachmentCount=1,.pColorAttachments=&reference};
  // Explicit external scopes avoid implicit ALL_COMMANDS -> ALL_GRAPHICS
  // dependencies. The caller handles acquire and image transitions separately.
  VkSubpassDependency dependencies[]={
    {VK_SUBPASS_EXTERNAL,0,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,0},
    {0,VK_SUBPASS_EXTERNAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,0,0}};
  VkRenderPassCreateInfo ri={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount=1,.pAttachments=&attachment,.subpassCount=1,.pSubpasses=&subpass,
      .dependencyCount=2,.pDependencies=dependencies};
  VK_CHECK(vkCreateRenderPass(V.device,&ri,NULL,&present_shader_pass));
  VkShaderModule modules[2];const uint32_t *code[]={dm_present_vert_spv,dm_present_frag_spv};
  size_t bytes[]={sizeof dm_present_vert_spv,sizeof dm_present_frag_spv};
  VkPipelineShaderStageCreateInfo stages[2];
  for(unsigned n=0;n<2;n++) {
    VkShaderModuleCreateInfo mi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize=bytes[n],.pCode=code[n]};
    VK_CHECK(vkCreateShaderModule(V.device,&mi,NULL,&modules[n]));
    stages[n]=(VkPipelineShaderStageCreateInfo){.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage=n?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT,.module=modules[n],.pName="main"};
  }
  VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
  VkPipelineViewportStateCreateInfo vp={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount=1,.scissorCount=1};
  VkPipelineRasterizationStateCreateInfo raster={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE,.lineWidth=1};
  VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
  VkPipelineColorBlendAttachmentState blend={.colorWriteMask=15};
  VkPipelineColorBlendStateCreateInfo bs={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount=1,.pAttachments=&blend};
  VkDynamicState dynamic[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo ds={.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount=2,.pDynamicStates=dynamic};
  VkGraphicsPipelineCreateInfo pi={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount=2,.pStages=stages,.pVertexInputState=&vi,.pInputAssemblyState=&ia,
      .pViewportState=&vp,.pRasterizationState=&raster,.pMultisampleState=&ms,
      .pColorBlendState=&bs,.pDynamicState=&ds,.layout=present_shader_layout,.renderPass=present_shader_pass};
  VK_CHECK(vkCreateGraphicsPipelines(V.device,V.pipeline_cache,1,&pi,NULL,&present_shader_pipeline));
  for(unsigned n=0;n<2;n++)vkDestroyShaderModule(V.device,modules[n],NULL);
}
void vk_present_shader_target_create(vk_present_shader_target *target,VkImage image,VkExtent2D extent) {
  present_shader_initialize();
  if(target->view || target->framebuffer)die("Vulkan present target already initialized");
  target->image=image;target->extent=extent;
  VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image=image,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,
      .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
  VK_CHECK(vkCreateImageView(V.device,&vi,NULL,&target->view));
  VkFramebufferCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass=present_shader_pass,.attachmentCount=1,.pAttachments=&target->view,
      .width=extent.width,.height=extent.height,.layers=1};
  VK_CHECK(vkCreateFramebuffer(V.device,&fi,NULL,&target->framebuffer));
}
void vk_present_shader_target_destroy(vk_present_shader_target *target) {
  if(target->framebuffer)vkDestroyFramebuffer(V.device,target->framebuffer,NULL);
  if(target->view)vkDestroyImageView(V.device,target->view,NULL);
  *target=(vk_present_shader_target){0};
}
void vk_present_shader_render(const vk_present_sample *source,const vk_present_shader_target *target) {
  if(!V.cmd || !target->framebuffer || source->image==target->image || source->format!=VK_FORMAT_R8G8B8A8_UNORM ||
      source->extent.width!=target->extent.width || source->extent.height!=target->extent.height)
    die("Vulkan present shader precondition");
  vk_render_end();
  VkDescriptorSet set=vk_descriptor_allocate(present_shader_descriptors);
  VkDescriptorImageInfo image={present_shader_sampler,source->view,source->layout};
  VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,
      .dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&image};
  vkUpdateDescriptorSets(V.device,1,&write,0,NULL);
  VkRenderPassBeginInfo begin={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass=present_shader_pass,.framebuffer=target->framebuffer,
      .renderArea={{0,0},target->extent}};
  vkCmdBeginRenderPass(V.cmd,&begin,VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(V.cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,present_shader_pipeline);
  vkCmdBindDescriptorSets(V.cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,present_shader_layout,0,1,&set,0,NULL);
  VkViewport viewport={0,0,(float)target->extent.width,(float)target->extent.height,0,1};
  VkRect2D scissor={{0,0},target->extent};
  vkCmdSetViewport(V.cmd,0,1,&viewport);vkCmdSetScissor(V.cmd,0,1,&scissor);
  vkCmdDraw(V.cmd,3,1,0,0);vkCmdEndRenderPass(V.cmd);
  V.draws++;
}
