static image *native_acquired_images;
// The table is a layout/provenance declaration, never read-only authority.
// Imported images have no vk_memory allocation: their original shared Pool
// owns storage, existing submission refs retain uses, and Pool close destroys
// all bound VkImages before freeing the imported VkDeviceMemory.
static const nvn_native_entry *native_image_entry(const tex_t *t) {
  if(!t || !t->has_layout || !nvn_native_layout_is_marker(t->layout))return NULL;
  const nvn_native_entry *entry=nvn_native_layout_resolve(t->layout,&t->d);
  if(!entry)die("Native image layout/description is not in the startup table");
  return entry;
}
static void native_image_use(image *i) {
  if(!i->native_entry)return;
  if(!i->native_acquired) {
    vk_render_end();
    // GENERAL is the real external producer state, not an UNDEFINED discard.
    VkImageMemoryBarrier acquire={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .oldLayout=VK_IMAGE_LAYOUT_GENERAL,.newLayout=VK_IMAGE_LAYOUT_GENERAL,
      .srcQueueFamilyIndex=VK_QUEUE_FAMILY_FOREIGN_EXT,.dstQueueFamilyIndex=V.queue_family,
      .image=i->handle,.subresourceRange={i->aspect,0,i->levels,0,1},
      .dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT};
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
      0,0,NULL,0,NULL,1,&acquire);
    i->layout=VK_IMAGE_LAYOUT_GENERAL;i->layout_scope=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    // A new external publication invalidates all previous content ordering
    // proofs even when the CPU wrote identical bytes. The acquire orders it.
    memset(i->access,0,(size_t)i->levels*3*sizeof *i->access);i->access_generation++;
    for(unsigned n=0;n<i->levels;n++)image_cell(i,0,n,0)->mip_layout=VK_IMAGE_LAYOUT_GENERAL;
    i->native_acquired=1;i->native_next=native_acquired_images;native_acquired_images=i;
  }

}
static void native_image_create(image *i,const nvn_native_entry *entry) {
  const nvn_native_recipe *r=&entry->recipe;
  VkExternalMemoryImageCreateInfo external={.sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
    .handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
  i->description=(VkImageCreateInfo){.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .imageType=r->image_type,.format=r->format,.extent={r->width,r->height,r->depth},
    .mipLevels=r->levels,.arrayLayers=r->layers,.samples=r->samples,.tiling=r->tiling,
    .usage=r->usage,.flags=r->flags,.sharingMode=r->sharing,.initialLayout=r->initial_layout};
  VkPhysicalDeviceExternalImageFormatInfo import={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
    .handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
  VkPhysicalDeviceImageFormatInfo2 query={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
    .pNext=&import,.format=r->format,.type=r->image_type,.tiling=r->tiling,.usage=r->usage,.flags=r->flags};
  VkExternalImageFormatProperties features={.sType=VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
  VkImageFormatProperties2 properties={.sType=VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,.pNext=&features};
  VK_CHECK(vkGetPhysicalDeviceImageFormatProperties2(V.physical,&query,&properties));
  VkExternalMemoryFeatureFlags bits=features.externalMemoryProperties.externalMemoryFeatures;
  const VkImageFormatProperties *limit=&properties.imageFormatProperties;
  if(!(bits&VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) || (bits&VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT) ||
      limit->maxExtent.width<r->width || limit->maxExtent.height<r->height || limit->maxMipLevels<r->levels ||
      !limit->maxArrayLayers || !(limit->sampleCounts&1))die("Native image exact recipe not importable");
  // A stack pNext lives only for this API call, never in cached descriptions.
  VkImageCreateInfo create=i->description;create.pNext=&external;
  VK_CHECK(vkCreateImage(V.device,&create,NULL,&i->handle));
  VkMemoryDedicatedRequirements dedicated={.sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
  VkMemoryRequirements2 needs={.sType=VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,.pNext=&dedicated};
  VkImageMemoryRequirementsInfo2 info={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,.image=i->handle};
  vkGetImageMemoryRequirements2(V.device,&info,&needs);
  if(dedicated.requiresDedicatedAllocation || needs.memoryRequirements.size!=r->image_size ||
     needs.memoryRequirements.alignment!=r->image_alignment)die("Native image storage recipe changed");
  for(unsigned n=0;n<i->levels;n++) {
    VkImageSubresource2EXT sub={.sType=VK_STRUCTURE_TYPE_IMAGE_SUBRESOURCE_2_EXT,
      .imageSubresource={i->aspect,n,0}};
    VkSubresourceHostMemcpySizeEXT size={.sType=VK_STRUCTURE_TYPE_SUBRESOURCE_HOST_MEMCPY_SIZE_EXT};
    VkSubresourceLayout2EXT layout={.sType=VK_STRUCTURE_TYPE_SUBRESOURCE_LAYOUT_2_EXT,.pNext=&size};
    vkGetImageSubresourceLayout2EXT(V.device,i->handle,&sub,&layout);
    nvn_native_mip actual={layout.subresourceLayout.offset,size.size,layout.subresourceLayout.rowPitch,
      layout.subresourceLayout.arrayPitch,layout.subresourceLayout.depthPitch};
    if(memcmp(&actual,&r->mips[n],sizeof actual))die("Native image actual mip layout changed");
  }
  i->native_pool=vk_pool_native_image_bind((uintptr_t)i->tex.mem,r->image_size,i->handle,
    &needs.memoryRequirements,&i->memory);
  i->native_entry=entry;i->initialized=1;i->physical_traits=image_physical_traits(i);
  i->access=calloc((size_t)i->levels*3,sizeof *i->access);
  if(!i->access)die("Native image access allocation");
  i->access_generation++;native_image_use(i);
}
void vk_native_images_release(void) {
  // Called at the original end of every command chunk, after render/query
  // closure. CPU production still requires the original NVN fence/Flush rules;
  // releasing is not a CPU wait and does not authorize racing host writes.
  while(native_acquired_images) {
    image *i=native_acquired_images;native_acquired_images=i->native_next;i->native_next=NULL;
    image_layout(i,VK_IMAGE_LAYOUT_GENERAL);
    VkImageMemoryBarrier release={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,
      .oldLayout=VK_IMAGE_LAYOUT_GENERAL,.newLayout=VK_IMAGE_LAYOUT_GENERAL,
      .srcQueueFamilyIndex=V.queue_family,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_FOREIGN_EXT,
      .image=i->handle,.subresourceRange={i->aspect,0,i->levels,0,1}};
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
      0,0,NULL,0,NULL,1,&release);i->native_acquired=0;
  }
}

static void native_image_record(image *i,VkImageSubresourceRange range,VkPipelineStageFlags stages,VkAccessFlags access) {
  native_image_use(i);
  if(i->native_use_serial!=vk_recording_serial()) {
    i->native_use_serial=vk_recording_serial();i->native_recorded_stages=0;
  }
  // Reuse the existing pool dependency engine for linear/image aliases. The
  // same canonical bytes and actual shader/transfer stages participate in
  // WAR/RAW/WAW ordering; image-only histories would miss VkBuffer writers.
  const nvn_native_recipe *recipe=&i->native_entry->recipe;
  const nvn_native_mip *first=&recipe->mips[range.baseMipLevel];
  const nvn_native_mip *last=&recipe->mips[range.baseMipLevel+range.levelCount-1];
  uint64_t address=(uintptr_t)i->tex.mem+first->offset;
  uint64_t bytes=last->offset+last->size-first->offset;
  if(access&(VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT)) {
    vk_buffer_use use={address,bytes,!!image_writes(access)};
    vk_transfer_buffer_dependencies(&use,1);
  } else vk_buffer_shader_read(address,bytes,stages);
  if(stages&~i->native_recorded_stages) {
    vk_pool_native_image_use(i->native_pool,(uintptr_t)i->tex.mem,i->tex.L.size,stages);
    i->native_recorded_stages|=stages;
  }
  if(access&VK_ACCESS_TRANSFER_READ_BIT) {
    if(!(i->description.usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT))die("Native image missing transfer source recipe");
  }
  if(image_writes(access)) {
    if((access&~VK_ACCESS_TRANSFER_WRITE_BIT) || !(i->description.usage&VK_IMAGE_USAGE_TRANSFER_DST_BIT))
      die("Native image writer not in baked transfer recipe");
    for(unsigned n=range.baseMipLevel;n<range.baseMipLevel+range.levelCount;n++) {
      const nvn_native_mip *m=&i->native_entry->recipe.mips[n];
      uint64_t address=(uintptr_t)i->tex.mem+m->offset;
      // bind/materialize has already pinned the complete shared Pool. Acquire
      // only a mip that is actually written, never every sampled-only asset.
      // The independent NC commit gate protects these pages through fence +
      // Invalidate. A missing gate is a hard experimental capability failure.
      if(!vk_pool_native_image_write_ready(i->native_pool))
        die("Native image NC writer requires the shared prewrite commit gate");
      if(!i->native_write_scopes[n]) {
        i->native_write_scopes[n]=nvn_dirty_scope_acquire((void *)(uintptr_t)address,m->size);
        if(!i->native_write_scopes[n])die("Native image writer scope admission failed");
      }
      vk_buffer_transfer_written_range(address,m->size);
      vk_buffer_writeback_stage(address,m->size,stages);
    }
  }
}
