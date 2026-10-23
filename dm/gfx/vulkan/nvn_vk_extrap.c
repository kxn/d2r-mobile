// 原 nvn_rtex.inc 的 Vulkan 对应；只读历史颜色/深度/模板，写本帧附件。
// Vulkan counterpart of nvn_rtex.inc; reads history, writes current attachments.
#include "nvn_vk_extrap.h"
#include "nvn_vk_shader.h"
#include "nvn_vk_extrap_vert_spv.h"
#include "nvn_vk_extrap_frag_spv.h"
#include "nvn_vk_extrap_masked_spv.h"
#include "nvn_vk_extrap_mask_spv.h"
#include <math.h>
#include "nvn_vk_extrap_math.inc"
typedef struct rx_pipeline {
  struct rx_pipeline *next;
  VkFormat color,depth;
  VkRenderPass pass;
  VkPipeline pipeline;
  int masked;
  VkRenderPass mask_pass;
  VkPipeline mask_pipeline;
} rx_pipeline;
static rx_pipeline *pipelines,*active;
static VkDescriptorSetLayout descriptors;
static VkPipelineLayout layout;
static VkSampler linear_sampler,nearest_sampler;
static int failed;
static vk_rt_image *hole_image;
typedef struct { float matrix[16]; uint32_t hole; float amount,pad[2]; } rx_constants;
static int matrix_between(const float previous[16],const float current[16],double m[16]) {
  double a[16],b[16],inverse[16];
  for (int i=0;i<16;i++) { a[i]=previous[i];b[i]=current[i]; }
  rx_inv4(a,inverse);
  for (int r=0;r<4;r++) for (int c=0;c<4;c++) {
    double sum=0;for (int k=0;k<4;k++) sum+=inverse[r*4+k]*b[k*4+c];
    if (!isfinite(sum)) return 0;
    m[r*4+c]=sum;
  }
  return fabs(m[15])>1e-12;
}
void vk_extrap_shift(const float previous[16],const float current[16],double width,double height,int *dx,int *dy) {
  double m[16]; *dx=*dy=1000000;
  if (matrix_between(previous,current,m)) {
    double x=m[12]/m[15]/2*width,y=-m[13]/m[15]/2*height;
    if (isfinite(x)&&isfinite(y)&&fabs(x)<1000000&&fabs(y)<1000000) {
      *dx=(int)lround(x);*dy=(int)lround(y);
    }
  }
}
static void destroy_copy(void *context) {
  vk_rt_image *i=context;
  vkDestroyImageView(V.device,i->attachment,NULL);
  vkDestroyImageView(V.device,i->view,NULL);
  vkDestroyImageView(V.device,i->stencil,NULL);
  vkDestroyImage(V.device,i->image,NULL);vkFreeMemory(V.device,i->memory,NULL);free(i);
}
void vk_rt_release(vk_rt_image *history) {
  if (history) vk_completion(destroy_copy,history);
}
static VkImageView view_create(VkImage image,VkFormat format,VkImageAspectFlags aspect) {
  VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=image,
      .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=format,.subresourceRange={aspect,0,1,0,1}};
  VkImageView view;VK_CHECK(vkCreateImageView(V.device,&vi,NULL,&view));return view;
}
static vk_rt_image *image_prepare(const vk_rt_image *source,vk_rt_image *previous) {
  if (!previous || source->format!=previous->format || memcmp(&source->extent,&previous->extent,sizeof source->extent)) {
    if (previous) vk_completion(destroy_copy,previous);
    previous=calloc(1,sizeof *previous);if (!previous) die("Vulkan extrapolation history allocation");
    previous->format=source->format;previous->extent=source->extent;previous->aspect=source->aspect;
    VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
        .format=source->format,.extent=source->extent,.mipLevels=1,.arrayLayers=1,
        .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|
          (source->aspect==VK_IMAGE_ASPECT_COLOR_BIT?VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT:VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)};
    VK_CHECK(vkCreateImage(V.device,&ci,NULL,&previous->image));
    VkMemoryRequirements mr;vkGetImageMemoryRequirements(V.device,previous->image,&mr);
    VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=mr.size,
        .memoryTypeIndex=vk_memory_type(mr.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    VK_CHECK(vkAllocateMemory(V.device,&ai,NULL,&previous->memory));
    VK_CHECK(vkBindImageMemory(V.device,previous->image,previous->memory,0));
    previous->attachment=view_create(previous->image,previous->format,previous->aspect);
    previous->view=view_create(previous->image,previous->format,
        source->aspect&VK_IMAGE_ASPECT_DEPTH_BIT?VK_IMAGE_ASPECT_DEPTH_BIT:source->aspect);
    if (source->aspect&VK_IMAGE_ASPECT_STENCIL_BIT)
      previous->stencil=view_create(previous->image,previous->format,VK_IMAGE_ASPECT_STENCIL_BIT);
    VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .image=previous->image,.subresourceRange={previous->aspect,0,1,0,1},
        .dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT};
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0,0,NULL,0,NULL,1,&barrier);
  }
  return previous;
}
vk_rt_image *vk_rt_copy(const vk_rt_image *source,vk_rt_image *previous) {
  if (!source || !source->image) return NULL;
  vk_barrier();previous=image_prepare(source,previous);
  for (unsigned bit=1;bit<=VK_IMAGE_ASPECT_STENCIL_BIT;bit<<=1) if (source->aspect&bit) {
    VkImageCopy copy={.srcSubresource={bit,0,0,1},.dstSubresource={bit,0,0,1},.extent=source->extent};
    vkCmdCopyImage(V.cmd,source->image,VK_IMAGE_LAYOUT_GENERAL,previous->image,VK_IMAGE_LAYOUT_GENERAL,1,&copy);
  }
  return previous;
}
void vk_rt_shift(const vk_rt_image *source,const vk_rt_image *destination,int dx,int dy) {
  if (!source || !destination || source->format!=destination->format ||
      memcmp(&source->extent,&destination->extent,sizeof source->extent)) return;
  int width=(int)source->extent.width-abs(dx),height=(int)source->extent.height-abs(dy);
  if (width<=0 || height<=0) return;
  vk_barrier();
  VkImageCopy copy={.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},
      .dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},
      .srcOffset={dx<0?-dx:0,dy<0?-dy:0,0},.dstOffset={dx>0?dx:0,dy>0?dy:0,0},
      .extent={width,height,1}};
  vkCmdCopyImage(V.cmd,source->image,VK_IMAGE_LAYOUT_GENERAL,destination->image,VK_IMAGE_LAYOUT_GENERAL,1,&copy);
}
int vk_extrap_setup(VkFormat color,VkFormat depth) {
  // Metal 将 NVN D24S8 升为 D32S8；Vulkan 保留原精度，两者都需要同一外推语义。
  // Metal promotes NVN D24S8 to D32S8. Vulkan retains both native formats.
  if (failed || V.options.extrap_no_pipeline ||
      (depth!=VK_FORMAT_D32_SFLOAT_S8_UINT&&depth!=VK_FORMAT_D24_UNORM_S8_UINT)) return 0;
  int masked=!V.stencil_export||V.options.extrap_mask;
  for (active=pipelines;active;active=active->next) if (active->color==color&&active->depth==depth&&active->masked==masked) return 1;
  VkFormatProperties props;vk_format_properties(color,&props);
  if ((props.optimalTilingFeatures&(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))!=
      (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) return 0;
  if (masked) {
    vk_format_properties(VK_FORMAT_R8_UINT,&props);
    if ((props.optimalTilingFeatures&(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))!=
        (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) return 0;
  }
  if (!layout) {
    VkDescriptorSetLayoutBinding bindings[3];
    for (unsigned i=0;i<3;i++) bindings[i]=(VkDescriptorSetLayoutBinding){i,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,NULL};
    VkDescriptorSetLayoutCreateInfo di={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=3,.pBindings=bindings};
    VK_CHECK(vkCreateDescriptorSetLayout(V.device,&di,NULL,&descriptors));
    VkPushConstantRange push={VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(rx_constants)};
    VkPipelineLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount=1,.pSetLayouts=&descriptors,.pushConstantRangeCount=1,.pPushConstantRanges=&push};
    VK_CHECK(vkCreatePipelineLayout(V.device,&li,NULL,&layout));
    VkSamplerCreateInfo si={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=VK_FILTER_LINEAR,
        .minFilter=VK_FILTER_LINEAR,.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    VK_CHECK(vkCreateSampler(V.device,&si,NULL,&linear_sampler));
    si.magFilter=si.minFilter=VK_FILTER_NEAREST;VK_CHECK(vkCreateSampler(V.device,&si,NULL,&nearest_sampler));
  }
  rx_pipeline *p=calloc(1,sizeof *p);if (!p) die("Vulkan extrapolation pipeline allocation");
  p->color=color;p->depth=depth;p->masked=masked;
  VkAttachmentDescription attachments[3]={
    {0,color,VK_SAMPLE_COUNT_1_BIT,VK_ATTACHMENT_LOAD_OP_LOAD,VK_ATTACHMENT_STORE_OP_STORE,
      VK_ATTACHMENT_LOAD_OP_DONT_CARE,VK_ATTACHMENT_STORE_OP_DONT_CARE,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL},
    {0,depth,VK_SAMPLE_COUNT_1_BIT,VK_ATTACHMENT_LOAD_OP_LOAD,VK_ATTACHMENT_STORE_OP_STORE,
      VK_ATTACHMENT_LOAD_OP_LOAD,VK_ATTACHMENT_STORE_OP_STORE,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL}};
  if (masked) {
    attachments[2]=attachments[1];
    attachments[1]=(VkAttachmentDescription){0,VK_FORMAT_R8_UINT,VK_SAMPLE_COUNT_1_BIT,
      VK_ATTACHMENT_LOAD_OP_CLEAR,VK_ATTACHMENT_STORE_OP_STORE,VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      VK_ATTACHMENT_STORE_OP_DONT_CARE,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL};
  }
  VkAttachmentReference c[2]={{0,VK_IMAGE_LAYOUT_GENERAL},{1,VK_IMAGE_LAYOUT_GENERAL}},d={masked?2:1,VK_IMAGE_LAYOUT_GENERAL};
  VkSubpassDescription subpass={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,
      .colorAttachmentCount=masked?2:1,.pColorAttachments=c,.pDepthStencilAttachment=&d};
  VkRenderPassCreateInfo ri={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount=masked?3:2,.pAttachments=attachments,.subpassCount=1,.pSubpasses=&subpass};
  VK_CHECK(vkCreateRenderPass(V.device,&ri,NULL,&p->pass));
  VkShaderModule modules[2];const uint32_t *code[]={dm_extrap_vert_spv,masked?dm_extrap_masked_spv:dm_extrap_frag_spv};
  size_t sizes[]={sizeof dm_extrap_vert_spv,masked?sizeof dm_extrap_masked_spv:sizeof dm_extrap_frag_spv};
  VkPipelineShaderStageCreateInfo stages[2];
  for (unsigned i=0;i<2;i++) {
    VkShaderModuleCreateInfo mi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=sizes[i],.pCode=code[i]};
    VK_CHECK(vkCreateShaderModule(V.device,&mi,NULL,&modules[i]));
    stages[i]=(VkPipelineShaderStageCreateInfo){.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT,.module=modules[i],.pName="main"};
  }
  VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
  VkPipelineViewportStateCreateInfo vp={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.scissorCount=1};
  VkPipelineRasterizationStateCreateInfo raster={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.polygonMode=VK_POLYGON_MODE_FILL,.lineWidth=1};
  VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
  VkPipelineDepthStencilStateCreateInfo ds={.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable=1,.depthWriteEnable=1,.depthCompareOp=VK_COMPARE_OP_ALWAYS,.stencilTestEnable=1};
  ds.front=ds.back=(VkStencilOpState){VK_STENCIL_OP_KEEP,VK_STENCIL_OP_REPLACE,VK_STENCIL_OP_KEEP,VK_COMPARE_OP_EQUAL,2,0x80,0};
  VkPipelineColorBlendAttachmentState blend[2]={{.colorWriteMask=15},{.colorWriteMask=15}};
  VkPipelineColorBlendStateCreateInfo bs={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=masked?2:1,.pAttachments=blend};
  VkDynamicState dynamic[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dy={.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,.dynamicStateCount=2,.pDynamicStates=dynamic};
  VkGraphicsPipelineCreateInfo ci={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount=2,.pStages=stages,.pVertexInputState=&vi,.pInputAssemblyState=&ia,.pViewportState=&vp,
      .pRasterizationState=&raster,.pMultisampleState=&ms,.pDepthStencilState=&ds,.pColorBlendState=&bs,
      .pDynamicState=&dy,.layout=layout,.renderPass=p->pass};
  VkResult result=vkCreateGraphicsPipelines(V.device,V.pipeline_cache,1,&ci,NULL,&p->pipeline);
  if (result==VK_SUCCESS&&masked) {
    // 第一趟清掉旧 0x80 并写洞纹理；第二趟只在洞处写 0x80，其余模板位保留。
    // First pass clears old 0x80 and writes holes. Second writes 0x80 only
    // for holes, preserving all other stencil bits and current UI coverage.
    VkAttachmentDescription attachment=attachments[2];VkAttachmentReference mask_depth={0,VK_IMAGE_LAYOUT_GENERAL};
    VkSubpassDescription mask_subpass={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.pDepthStencilAttachment=&mask_depth};
    VkRenderPassCreateInfo mask_ri={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount=1,.pAttachments=&attachment,.subpassCount=1,.pSubpasses=&mask_subpass};
    VK_CHECK(vkCreateRenderPass(V.device,&mask_ri,NULL,&p->mask_pass));
    VkShaderModule mask_module;VkShaderModuleCreateInfo mi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize=sizeof dm_extrap_mask_spv,.pCode=dm_extrap_mask_spv};
    VK_CHECK(vkCreateShaderModule(V.device,&mi,NULL,&mask_module));
    stages[1].module=mask_module;ci.renderPass=p->mask_pass;
    ds.depthTestEnable=ds.depthWriteEnable=0;ds.front.reference=ds.back.reference=0x80;
    bs.attachmentCount=0;
    result=vkCreateGraphicsPipelines(V.device,V.pipeline_cache,1,&ci,NULL,&p->mask_pipeline);
    vkDestroyShaderModule(V.device,mask_module,NULL);
  }
  for (unsigned i=0;i<2;i++) vkDestroyShaderModule(V.device,modules[i],NULL);
  if (result!=VK_SUCCESS) {
    vkDestroyPipeline(V.device,p->pipeline,NULL);vkDestroyRenderPass(V.device,p->mask_pass,NULL);
    vkDestroyRenderPass(V.device,p->pass,NULL);free(p);failed=1;
    LOG("[vulkan] original extrapolation pipeline unavailable: %d",result);return 0;
  }
  LOG("[vulkan] original extrapolation pipeline: %s",masked?"GPU hole-mask + stencil pass":"fragment stencil export");
  p->next=pipelines;pipelines=p;active=p;return 1;
}
int vk_extrap_encode(const vk_rt_image *hdr,const vk_rt_image *depth,
    const vk_rt_image *phdr,const vk_rt_image *pdepth,const float previous[16],
    const float current[16],uint32_t unit_bits,uint32_t ui_bits) {
  if (!V.cmd||!hdr||!depth||!phdr||!pdepth||hdr->extent.width<2||hdr->extent.height<2) return 0;
  if (memcmp(&hdr->extent,&depth->extent,sizeof hdr->extent)||memcmp(&hdr->extent,&phdr->extent,sizeof hdr->extent)||
      memcmp(&hdr->extent,&pdepth->extent,sizeof hdr->extent)||!vk_extrap_setup(hdr->format,depth->format)) return 0;
  double m[16];if (!matrix_between(previous,current,m)) return 0;
  double dx=m[12]/m[15]/2*hdr->extent.width,dy=-m[13]/m[15]/2*hdr->extent.height;
  double fx=-dx-floor(-dx),fy=-dy-floor(-dy);
  rx_constants constants={.hole=unit_bits|ui_bits,.amount=(float)(fx*(1-fx)+fy*(1-fy))};
  for (int i=0;i<16;i++) constants.matrix[i]=(float)m[i];
  vk_barrier();
  VkDescriptorSet set=vk_descriptor_allocate(descriptors);
  VkDescriptorImageInfo images[]={{linear_sampler,phdr->view,VK_IMAGE_LAYOUT_GENERAL},
      {nearest_sampler,pdepth->view,VK_IMAGE_LAYOUT_GENERAL},{nearest_sampler,pdepth->stencil,VK_IMAGE_LAYOUT_GENERAL}};
  VkWriteDescriptorSet writes[3];for (unsigned i=0;i<3;i++) writes[i]=(VkWriteDescriptorSet){
      .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=i,.descriptorCount=1,
      .descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&images[i]};
  vkUpdateDescriptorSets(V.device,3,writes,0,NULL);
  if (active->masked) {
    vk_rt_image description={.format=VK_FORMAT_R8_UINT,.extent=hdr->extent,.aspect=VK_IMAGE_ASPECT_COLOR_BIT};
    hole_image=image_prepare(&description,hole_image);
  }
  VkImageView views[3]={hdr->attachment,depth->attachment,VK_NULL_HANDLE};
  if (active->masked) { views[1]=hole_image->attachment;views[2]=depth->attachment; }
  VkFramebuffer framebuffer;
  VkFramebufferCreateInfo fb={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=active->pass,
      .attachmentCount=active->masked?3:2,.pAttachments=views,.width=hdr->extent.width,.height=hdr->extent.height,.layers=1};
  VK_CHECK(vkCreateFramebuffer(V.device,&fb,NULL,&framebuffer));
  VkRenderPassBeginInfo begin={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=active->pass,
      .framebuffer=framebuffer,.renderArea={{0,0},{hdr->extent.width,hdr->extent.height}}};
  VkClearValue clears[3]={0};if (active->masked) { begin.clearValueCount=3;begin.pClearValues=clears; }
  unsigned region=vk_gpu_region_begin("render","upstream-extrap",hdr->extent.width,hdr->extent.height);
  vkCmdBeginRenderPass(V.cmd,&begin,VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(V.cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,active->pipeline);
  VkViewport viewport={0,0,hdr->extent.width,hdr->extent.height,0,1};
  vkCmdSetViewport(V.cmd,0,1,&viewport);vkCmdSetScissor(V.cmd,0,1,&begin.renderArea);
  vkCmdBindDescriptorSets(V.cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&set,0,NULL);
  vkCmdPushConstants(V.cmd,layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof constants,&constants);
  vkCmdDraw(V.cmd,3,1,0,0);vkCmdEndRenderPass(V.cmd);vk_gpu_region_end(region);
  vk_defer_pass(VK_NULL_HANDLE,framebuffer);vk_ubo_plan_break();V.passes++;V.draws++;vk_barrier();
  if (active->masked) {
    VkDescriptorSet mask_set=vk_descriptor_allocate(descriptors);
    VkDescriptorImageInfo mask_image={nearest_sampler,hole_image->view,VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet mask_write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=mask_set,
        .dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&mask_image};
    vkUpdateDescriptorSets(V.device,1,&mask_write,0,NULL);
    fb.renderPass=active->mask_pass;fb.attachmentCount=1;fb.pAttachments=&depth->attachment;
    VK_CHECK(vkCreateFramebuffer(V.device,&fb,NULL,&framebuffer));
    begin.renderPass=active->mask_pass;begin.framebuffer=framebuffer;begin.clearValueCount=0;
    region=vk_gpu_region_begin("render","upstream-mask",hdr->extent.width,hdr->extent.height);
    vkCmdBeginRenderPass(V.cmd,&begin,VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(V.cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,active->mask_pipeline);
    vkCmdSetViewport(V.cmd,0,1,&viewport);vkCmdSetScissor(V.cmd,0,1,&begin.renderArea);
    vkCmdBindDescriptorSets(V.cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&mask_set,0,NULL);
    vkCmdDraw(V.cmd,3,1,0,0);vkCmdEndRenderPass(V.cmd);vk_gpu_region_end(region);
    vk_defer_pass(VK_NULL_HANDLE,framebuffer);vk_ubo_plan_break();V.passes++;V.draws++;vk_barrier();
  }
  return 1;
}
