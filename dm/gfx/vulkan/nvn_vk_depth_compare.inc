// NVN supports comparison sampling of normalized single-channel color data.
// Vulkan requires the format's depth-comparison feature. R16_UNORM and D16
// encode the same UNORM16 values; a GPU buffer copy preserves every bit while
// changing the physical aspect legally. Never reinterpret a color image view
// as depth, disable comparison, or replace its data with a dummy value.
static image *comparison_source(int id,texture_descriptor *descriptor,VkShaderStageFlagBits stage) {
  pthread_mutex_lock(&V.resources);
  if(id<0 || id>=texture_capacity || id>=texture_limit || !texture_descriptors[id].valid)
    die("Vulkan comparison texture descriptor %d",id);
  *descriptor=texture_descriptors[id];pthread_mutex_unlock(&V.resources);
  vk_render_texture_read(&descriptor->texture,stage);
  image *source=image_get(&descriptor->texture);image_upload(source);return source;
}
static image *comparison_prepare(int id,texture_descriptor *descriptor,VkShaderStageFlagBits stage) {
  image *source=comparison_source(id,descriptor,stage);
  const view_t *v=descriptor->has_view?&descriptor->view:NULL;
  VkFormat format=image_binding_format(source,v,0);
  if(source->aspect&VK_IMAGE_ASPECT_DEPTH_BIT)return source;
  if(format!=VK_FORMAT_R16_UNORM || source->format!=VK_FORMAT_R16_UNORM ||
      source->description.imageType!=VK_IMAGE_TYPE_2D || source->description.samples!=VK_SAMPLE_COUNT_1_BIT)
    die("Vulkan comparison color format unsupported: logical=%#x physical=%u view=%u",source->tex.d.format,source->format,format);
  if(source->comparison && source->comparison_stamp==source->comparison_version)return source;
  // Only an actual color-to-depth refresh ends the pass. Native depth views
  // and immutable-content hits retain their original pass batching.
  vk_render_end();
  source=comparison_source(id,descriptor,stage);
  if(source->aspect&VK_IMAGE_ASPECT_DEPTH_BIT)return source;
  v=descriptor->has_view?&descriptor->view:NULL;
  if(image_binding_format(source,v,0)!=VK_FORMAT_R16_UNORM || source->format!=VK_FORMAT_R16_UNORM)
    die("Vulkan comparison source changed during replay");
  if(!source->comparison) {
    VkFormatProperties props;vk_format_properties(VK_FORMAT_D16_UNORM,&props);
    VkFormatFeatureFlags required=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT|VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if((props.optimalTilingFeatures&required)!=required)die("Vulkan comparison D16 capabilities unavailable");
    VkFormatProperties3 p3={.sType=VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3};
    VkFormatProperties2 p2={.sType=VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,.pNext=&p3};
    vkGetPhysicalDeviceFormatProperties2(V.physical,VK_FORMAT_D16_UNORM,&p2);
    if(!(p3.optimalTilingFeatures&VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT))
      die("Vulkan D16 comparison feature unavailable");
    image *depth=calloc(1,sizeof *depth);if(!depth)die("Vulkan comparison image allocation");
    depth->tex.d=source->tex.d;depth->tex.serial=source->tex.serial;
    depth->format=VK_FORMAT_D16_UNORM;depth->aspect=VK_IMAGE_ASPECT_DEPTH_BIT;depth->bpp=2;
    depth->extent=source->extent;depth->levels=source->levels;depth->layers=source->layers;
    depth->description=source->description;depth->description.format=depth->format;
    depth->description.flags&=VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    depth->description.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    image_allocate(depth);source->comparison=depth;source->comparison_version=1;
    LOG("[vulkan comparison] descriptor=%d NVN serial=%llu fmt=%#x %ux%u layers=%u mips=%u packaged=%d mem=%p: R16_UNORM GPU copy to D16_UNORM",
        id,(unsigned long long)source->tex.serial,source->tex.d.format,source->extent.width,source->extent.height,source->layers,source->levels,source->tex.has_layout,source->tex.mem);
  }
  image *depth=source->comparison;
  if(source->comparison_stamp!=source->comparison_version) {
    uint64_t size=0;unsigned count=source->levels*source->layers;
    VkBufferImageCopy *regions=calloc(count,sizeof *regions);if(!regions)die("Vulkan comparison copy regions");
    unsigned at=0;
    for(unsigned l=0;l<source->levels;l++)for(unsigned layer=0;layer<source->layers;layer++) {
      uint64_t n=(uint64_t)mip(source->extent.width,l)*mip(source->extent.height,l)*2;
      if(n>UINT64_MAX-size-3)die("Vulkan comparison copy size overflow");
      size=(size+3)&~UINT64_C(3);
      regions[at++]=(VkBufferImageCopy){.bufferOffset=size,
        .imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,l,layer,1},
        .imageExtent={mip(source->extent.width,l),mip(source->extent.height,l),1}};size+=n;
    }
    vk_buffer *buffer=vk_temp_buffer(size);
    VkImageSubresourceRange sr={source->aspect,0,source->levels,0,source->layers};
    VkImageSubresourceRange dr={depth->aspect,0,depth->levels,0,depth->layers};
    image_general_transfer(source);image_transfer(source,sr,0);
    image_general_transfer(depth);image_transfer(depth,dr,1);
    vkCmdCopyImageToBuffer(V.cmd,source->handle,VK_IMAGE_LAYOUT_GENERAL,buffer->handle,count,regions);
    VkBufferMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,
      .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
      .buffer=buffer->handle,.offset=0,.size=size};
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,1,&barrier,0,NULL);
    for(unsigned n=0;n<count;n++)regions[n].imageSubresource.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;
    vkCmdCopyBufferToImage(V.cmd,buffer->handle,depth->handle,VK_IMAGE_LAYOUT_GENERAL,count,regions);
    depth->initialized=1;source->comparison_stamp=source->comparison_version;free(regions);
  }
  return source;
}
void vk_texture_prepare_comparison(int id,VkShaderStageFlagBits stage) {
  texture_descriptor d;comparison_prepare(id,&d,stage);
}
VkImageView vk_texture_view_comparison(int id,VkShaderStageFlagBits stage,VkImageLayout *layout) {
  texture_descriptor d;image *source=comparison_source(id,&d,stage);
  if(source->aspect&VK_IMAGE_ASPECT_DEPTH_BIT)return texture_view_binding_impl(id,0,stage,layout,0);
  if(source->format!=VK_FORMAT_R16_UNORM || image_binding_format(source,d.has_view?&d.view:NULL,0)!=VK_FORMAT_R16_UNORM)
    die("Vulkan comparison source/view requires supported normalized color values");
  if(!source->comparison || source->comparison_stamp!=source->comparison_version)
    source=comparison_prepare(id,&d,stage);
  if(source->aspect&VK_IMAGE_ASPECT_DEPTH_BIT)return texture_view_binding_impl(id,0,stage,layout,0);
  image *depth=source->comparison;const view_t *v=d.has_view?&d.view:NULL;
  VkImageSubresourceRange r=range(source,v);
  r.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;
  r.levelCount=v&&v->set_levels&&v->nlevels?v->nlevels:depth->levels-r.baseMipLevel;
  r.layerCount=v&&v->set_layers&&v->nlayers?v->nlayers:depth->layers-r.baseArrayLayer;
  VkImageViewType type=view_type(v&&v->set_target?v->target:source->tex.d.target);
  if(type==VK_IMAGE_VIEW_TYPE_2D)r.layerCount=1;
  VkImageLayout current=image_use_range_normalized(depth,r,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,image_shader_stages(stage));
  image_request_add_normalized(depth,r,image_shader_stages(stage),VK_ACCESS_SHADER_READ_BIT);
  if(layout)*layout=current;
  VkImageViewCreateInfo view={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=depth->handle,
    .viewType=type,.format=depth->format,.subresourceRange=r};
  return cached_view(depth,&view,VK_IMAGE_USAGE_SAMPLED_BIT);
}
