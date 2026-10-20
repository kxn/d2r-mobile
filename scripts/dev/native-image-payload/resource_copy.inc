// Functional native-storage copy contract, not a timing/profile tool. All CPU
// changes are to private reference/staging allocations before GPU submission.
static VkImageMemoryBarrier resource_copy_barrier(image *i, uint32_t levels,
    VkImageLayout old, VkImageLayout next, VkAccessFlags src, VkAccessFlags dst) {
  return (VkImageMemoryBarrier){.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    .srcAccessMask=src,.dstAccessMask=dst,.oldLayout=old,.newLayout=next,
    .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
    .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=i->image,
    .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,levels,0,1}};
}
static void resource_copy_t2t(image *source, image *destination,
    const dm_resource_input *h, int roi, unsigned bx, unsigned by) {
  VkCommandBuffer cb=resource_command_begin();
  VkImageMemoryBarrier barriers[]={
    resource_copy_barrier(source,h->levels,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_READ_BIT),
    resource_copy_barrier(destination,h->levels,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT)};
  vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
    0,0,NULL,0,NULL,2,barriers);
  VkImageCopy regions[DM_RESOURCE_MIPS]={0};
  uint32_t count=roi?1:h->levels;
  for(uint32_t n=0;n<count;n++)regions[n]=(VkImageCopy){
    .srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,n,0,1},
    .dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,n,0,1},
    .srcOffset={roi?bx*h->block:0,roi?by*h->block:0,0},
    .dstOffset={roi?bx*h->block:0,roi?by*h->block:0,0},
    .extent={roi?h->block:h->mips[n].width,roi?h->block:h->mips[n].height,1}};
  vkCmdCopyImage(cb,source->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    destination->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,count,regions);
  for(unsigned n=0;n<2;n++) {
    barriers[n].srcAccessMask=n?VK_ACCESS_TRANSFER_WRITE_BIT:VK_ACCESS_TRANSFER_READ_BIT;
    barriers[n].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    barriers[n].oldLayout=n?VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[n].newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  }
  vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    0,0,NULL,0,NULL,2,barriers);
  resource_command_wait(cb);
}
static void resource_copy_b2t(image *destination,const dm_resource_input *h,
    unsigned bx,unsigned by,const uint8_t block[16]) {
  VkBuffer buffer;VkDeviceMemory allocation;void *map;
  VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size=16,.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
  VK(vkCreateBuffer(device,&bi,NULL,&buffer));
  VkMemoryRequirements needs;vkGetBufferMemoryRequirements(device,buffer,&needs);
  uint32_t type=pick_type(needs.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
  VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize=needs.size,.memoryTypeIndex=type};
  VK(vkAllocateMemory(device,&ai,NULL,&allocation));
  VK(vkBindBufferMemory(device,buffer,allocation,0));
  VK(vkMapMemory(device,allocation,0,VK_WHOLE_SIZE,0,&map));memcpy(map,block,16);
  VkMappedMemoryRange flush={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
    .memory=allocation,.size=VK_WHOLE_SIZE};
  if(!(memory.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
    VK(vkFlushMappedMemoryRanges(device,1,&flush));
  VkCommandBuffer cb=resource_command_begin();
  VkBufferMemoryBarrier src={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,
    .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
    .buffer=buffer,.size=16};
  VkImageMemoryBarrier dst=resource_copy_barrier(destination,h->levels,
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
  vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,1,&src,1,&dst);
  VkBufferImageCopy region={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},
    .imageOffset={bx*h->block,by*h->block,0},.imageExtent={h->block,h->block,1}};
  vkCmdCopyBufferToImage(cb,buffer,destination->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
  dst.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;dst.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
  dst.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;dst.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    0,0,NULL,0,NULL,1,&dst);
  resource_command_wait(cb);
  vkUnmapMemory(device,allocation);vkDestroyBuffer(device,buffer,NULL);vkFreeMemory(device,allocation,NULL);
}
// Select an existing valid ASTC block with a different GPU-decoded corner.
// This avoids an ASTC encoder/decoder and proves the intended ROI changes.
static uint8_t *resource_copy_patch(resource_input *r,const uint8_t *pixels,
    unsigned bx,unsigned by) {
  const dm_resource_input *h=r->header;
  uint32_t columns=(h->width+h->block-1)/h->block;
  const uint8_t *old=pixels+((size_t)by*h->block*h->width+bx*h->block)*4;
  for(uint32_t y=0;y*h->block<h->height;y++)for(uint32_t x=0;x*h->block<h->width;x++) {
    const uint8_t *p=pixels+((size_t)y*h->block*h->width+x*h->block)*4;
    if(memcmp(old,p,4)) {
      uint8_t *dst=r->data+h->mips[0].offset+((size_t)by*columns+bx)*16;
      memmove(dst,r->data+h->mips[0].offset+((size_t)y*columns+x)*16,16);
      return dst;
    }
  }
  CHECK(0);return NULL;
}
static void resource_copy_tests(image *imported,const resource_input *original,
    uint8_t *actual,const uint8_t *original_pixels,size_t pixel_bytes) {
  resource_input changed=*original;changed.data=malloc(original->bytes);CHECK(changed.data);
  memcpy(changed.data,original->data,original->bytes);
  changed.header=(const void *)changed.data;
  uint8_t *expected=malloc(pixel_bytes);CHECK(expected);
  uint8_t *block=resource_copy_patch(&changed,original_pixels,1,1);
  image reference=resource_reference(&changed);
  resource_sample(&reference,changed.header,expected);
  CHECK(memcmp(expected,original_pixels,pixel_bytes));
  resource_copy_b2t(imported,changed.header,1,1,block);
  resource_sample(imported,changed.header,actual);CHECK(!memcmp(actual,expected,pixel_bytes));
  destroy_image(&reference);
  resource_copy_patch(&changed,original_pixels,2,1);
  reference=resource_reference(&changed);resource_sample(&reference,changed.header,expected);
  CHECK(memcmp(expected,actual,pixel_bytes));
  resource_copy_t2t(&reference,imported,changed.header,1,2,1);
  resource_sample(imported,changed.header,actual);CHECK(!memcmp(actual,expected,pixel_bytes));
  destroy_image(&reference);
  reference=resource_reference(original);
  resource_copy_t2t(imported,&reference,changed.header,0,0,0);
  resource_sample(&reference,changed.header,actual);CHECK(!memcmp(actual,expected,pixel_bytes));
  destroy_image(&reference);
  // Make actual GPU writes available to the HOST domain before caller releases
  // FOREIGN ownership, waits, invalidates, and inspects original backing guards.
  VkCommandBuffer cb=resource_command_begin();
  VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
  vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_HOST_BIT,
    0,1,&host,0,NULL,0,NULL);resource_command_wait(cb);
  free(expected);free(changed.data);
}
static void resource_copy_reference_selftest(void) {
  // Lavapipe has BC3 but not native ASTC import. Exercise the very same copy,
  // reference patch, sampling and untouched-mip checks with standard images.
  size_t bytes=(sizeof(dm_resource_input)+63)&~(size_t)63;
  for(unsigned n=0;n<6;n++) {
    uint32_t w=mip(32,n),h=mip(32,n);
    bytes+=(size_t)((w+3)/4)*((h+3)/4)*16;
  }
  resource_input r={.data=calloc(1,bytes),.bytes=bytes};CHECK(r.data);
  dm_resource_input *h=(void *)r.data;r.header=h;
  h->vk_format=VK_FORMAT_BC3_UNORM_BLOCK;h->width=h->height=32;h->levels=6;h->block=4;
  size_t at=(sizeof *h+63)&~(size_t)63;
  for(unsigned n=0;n<h->levels;n++) {
    dm_resource_mip *m=&h->mips[n];m->width=mip(h->width,n);m->height=mip(h->height,n);
    m->offset=at;m->size=(size_t)((m->width+3)/4)*((m->height+3)/4)*16;
    for(size_t k=0;k<m->size;k+=16) {
      uint8_t *b=r.data+at+k;b[0]=255;
      uint16_t color=((k/16+n)%3)==0?0xf800:((k/16+n)%3)==1?0x07e0:0x001f;
      memcpy(b+8,&color,2);memcpy(b+10,&color,2);
    }
    at+=m->size;
  }
  CHECK(at==bytes);
  size_t pixel_bytes=0;for(unsigned n=0;n<h->levels;n++)pixel_bytes+=(size_t)h->mips[n].width*h->mips[n].height*4;
  uint8_t *pixels=malloc(pixel_bytes),*original=malloc(pixel_bytes);CHECK(pixels && original);
  image standard=resource_reference(&r);resource_sample(&standard,h,original);
  resource_copy_tests(&standard,&r,pixels,original,pixel_bytes);
  destroy_image(&standard);free(pixels);free(original);free(r.data);
  printf("{\"copy_reference_selftest_passed\":true,\"format\":137,"
    "\"native_import_executed\":false,\"game_modules_executed\":false}\n");
}
