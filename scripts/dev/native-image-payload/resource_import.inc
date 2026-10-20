// Real-resource GPU sampling reuses original sample.comp and pipeline_create.
static void resource_sample(image *i, const dm_resource_input *h,
                            uint8_t *pixels) {
  VkFormat format = h->vk_format;
  size_t pixel_offset = 0;
  pipeline_create();
  VkImageView view;
  VkSampler sampler;
  VkBuffer output;
  VkDeviceMemory output_memory;
  void *mapped;
  VkImageViewCreateInfo vi = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = i->image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
      .format = format,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, h->levels, 0, 1}};
  VK(vkCreateImageView(device, &vi, NULL, &view));
  VkSamplerCreateInfo si = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter = VK_FILTER_NEAREST,
      .minFilter = VK_FILTER_NEAREST,
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .maxLod = h->levels - 1};
  VK(vkCreateSampler(device, &si, NULL, &sampler));
  VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                           .size = h->width * h->height * 4,
                           .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
  VK(vkCreateBuffer(device, &bi, NULL, &output));
  VkMemoryRequirements needs;
  vkGetBufferMemoryRequirements(device, output, &needs);
  uint32_t type =
      pick_type(needs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
  VkMemoryAllocateInfo ma = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                             .allocationSize = needs.size,
                             .memoryTypeIndex = type};
  VK(vkAllocateMemory(device, &ma, NULL, &output_memory));
  VK(vkBindBufferMemory(device, output, output_memory, 0));
  VK(vkMapMemory(device, output_memory, 0, VK_WHOLE_SIZE, 0, &mapped));
  VkDescriptorPoolSize sizes[] = {
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
  VkDescriptorPoolCreateInfo dpi = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1,
      .poolSizeCount = 2,
      .pPoolSizes = sizes};
  VkDescriptorPool pool;
  VK(vkCreateDescriptorPool(device, &dpi, NULL, &pool));
  VkDescriptorSetAllocateInfo da = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = pool,
      .descriptorSetCount = 1,
      .pSetLayouts = &descriptor_layout};
  VkDescriptorSet set;
  VK(vkAllocateDescriptorSets(device, &da, &set));
  VkDescriptorImageInfo img = {sampler, view,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkDescriptorBufferInfo buf = {output, 0, bi.size};
  VkWriteDescriptorSet writes[] = {
      {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
       .dstSet = set,
       .dstBinding = 0,
       .descriptorCount = 1,
       .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
       .pImageInfo = &img},
      {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
       .dstSet = set,
       .dstBinding = 1,
       .descriptorCount = 1,
       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
       .pBufferInfo = &buf}};
  vkUpdateDescriptorSets(device, 2, writes, 0, NULL);
  VkCommandBufferAllocateInfo ca = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = commands,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1};
  VkCommandBuffer command;
  VK(vkAllocateCommandBuffers(device, &ca, &command));
  VkFenceCreateInfo fc = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  VkFence fence;
  VK(vkCreateFence(device, &fc, NULL, &fence));
  int coherent = !!(memory.memoryTypes[type].propertyFlags &
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VkMappedMemoryRange whole = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                               .memory = output_memory,
                               .size = VK_WHOLE_SIZE};
  for (uint32_t layer = 0; layer < 1; layer++)
    for (uint32_t level = 0; level < h->levels; level++) {
      memset(mapped, 0xcc, bi.size);
      if (!coherent)
        VK(vkFlushMappedMemoryRanges(device, 1, &whole));
      VkCommandBufferBeginInfo cb = {
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
      VK(vkBeginCommandBuffer(command, &cb));
      VkMemoryBarrier ready = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                               .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
                               .dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
                                                VK_ACCESS_SHADER_WRITE_BIT};
      vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_HOST_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &ready,
                           0, NULL, 0, NULL);
      vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
      vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                              pipeline_layout, 0, 1, &set, 0, NULL);
      uint32_t push[] = {mip(h->width, level), mip(h->height, level), layer,
                         level};
      vkCmdPushConstants(command, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                         0, sizeof push, push);
      vkCmdDispatch(command, (push[0] + 7) / 8, (push[1] + 7) / 8, 1);
      VkMemoryBarrier done = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                              .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                              .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
      vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &done, 0, NULL, 0,
                           NULL);
      VK(vkEndCommandBuffer(command));
      VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                             .commandBufferCount = 1,
                             .pCommandBuffers = &command};
      VK(vkQueueSubmit(queue, 1, &submit, fence));
      VK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
      if (!coherent)
        VK(vkInvalidateMappedMemoryRanges(device, 1, &whole));
      size_t mip_bytes = (size_t)push[0] * push[1] * 4;
      memcpy(pixels + pixel_offset, mapped, mip_bytes);
      pixel_offset += mip_bytes;
      for (uint32_t n = push[0] * push[1]; n < h->width * h->height; n++)
        CHECK(((uint32_t *)mapped)[n] == 0xccccccccu);
      VK(vkResetFences(device, 1, &fence));
      VK(vkResetCommandPool(device, commands, 0));
    }
  vkDestroyFence(device, fence, NULL);
  vkFreeCommandBuffers(device, commands, 1, &command);
  vkDestroyDescriptorPool(device, pool, NULL);
  vkUnmapMemory(device, output_memory);
  vkDestroyBuffer(device, output, NULL);
  vkFreeMemory(device, output_memory, NULL);
  vkDestroySampler(device, sampler, NULL);
  vkDestroyImageView(device, view, NULL);
}
static VkCommandBuffer resource_command_begin(void) {
  VkCommandBufferAllocateInfo a = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = commands,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1};
  VkCommandBuffer cb;
  VK(vkAllocateCommandBuffers(device, &a, &cb));
  VkCommandBufferBeginInfo b = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  VK(vkBeginCommandBuffer(cb, &b));
  return cb;
}
static void resource_command_wait(VkCommandBuffer cb) {
  VK(vkEndCommandBuffer(cb));
  VkFenceCreateInfo f = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  VkFence fence;
  VK(vkCreateFence(device, &f, NULL, &fence));
  VkSubmitInfo s = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                    .commandBufferCount = 1,
                    .pCommandBuffers = &cb};
  VK(vkQueueSubmit(queue, 1, &s, fence));
  VK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
  vkDestroyFence(device, fence, NULL);
  vkFreeCommandBuffers(device, commands, 1, &cb);
}
static VkImageCreateInfo resource_recipe(const dm_resource_input *h,
                                         int imported) {
  return (VkImageCreateInfo){.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                             .pNext = imported ? &native_external : NULL,
                             .imageType = VK_IMAGE_TYPE_2D,
                             .format = h->vk_format,
                             .extent = {h->width, h->height, 1},
                             .mipLevels = h->levels,
                             .arrayLayers = 1,
                             .samples = VK_SAMPLE_COUNT_1_BIT,
                             .tiling = VK_IMAGE_TILING_OPTIMAL,
                             .usage = imported
                                          ? usage
                                          : VK_IMAGE_USAGE_SAMPLED_BIT |
                                                VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                             .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                             .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
}
static void resource_recipe_check(const VkImageCreateInfo *ci, int imported) {
  VkPhysicalDeviceExternalImageFormatInfo e = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
      .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
  VkPhysicalDeviceImageFormatInfo2 q = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
      .pNext = imported ? &e : NULL,
      .format = ci->format,
      .type = ci->imageType,
      .tiling = ci->tiling,
      .usage = ci->usage,
      .flags = ci->flags};
  VkExternalImageFormatProperties external = {
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
  VkImageFormatProperties2 p = {.sType =
                                    VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
                                .pNext = imported ? &external : NULL};
  VK(vkGetPhysicalDeviceImageFormatProperties2(physical, &q, &p));
  CHECK(ci->extent.width <= p.imageFormatProperties.maxExtent.width &&
        ci->extent.height <= p.imageFormatProperties.maxExtent.height &&
        ci->mipLevels <= p.imageFormatProperties.maxMipLevels &&
        ci->arrayLayers <= p.imageFormatProperties.maxArrayLayers &&
        (ci->samples & p.imageFormatProperties.sampleCounts));
  if (imported)
    CHECK(external.externalMemoryProperties.externalMemoryFeatures ==
              VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT &&
          external.externalMemoryProperties.compatibleHandleTypes ==
              e.handleType &&
          !external.externalMemoryProperties.exportFromImportedHandleTypes);
}
static image resource_reference(const resource_input *r) {
  const dm_resource_input *h = r->header;
  VkImageCreateInfo ci = resource_recipe(h, 0);
  resource_recipe_check(&ci, 0);
  image i = {0};
  VK(vkCreateImage(device, &ci, NULL, &i.image));
  vkGetImageMemoryRequirements(device, i.image, &i.requirements);
  VkMemoryAllocateInfo ma = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                             .allocationSize = i.requirements.size,
                             .memoryTypeIndex =
                                 pick_type(i.requirements.memoryTypeBits, 0)};
  VK(vkAllocateMemory(device, &ma, NULL, &i.allocation));
  VK(vkBindImageMemory(device, i.image, i.allocation, 0));
  VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                           .size = r->bytes,
                           .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
  VkBuffer staging;
  VkDeviceMemory memory_handle;
  VK(vkCreateBuffer(device, &bi, NULL, &staging));
  VkMemoryRequirements needs;
  vkGetBufferMemoryRequirements(device, staging, &needs);
  uint32_t type =
      pick_type(needs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
  ma.allocationSize = needs.size;
  ma.memoryTypeIndex = type;
  VK(vkAllocateMemory(device, &ma, NULL, &memory_handle));
  VK(vkBindBufferMemory(device, staging, memory_handle, 0));
  void *mapped;
  VK(vkMapMemory(device, memory_handle, 0, VK_WHOLE_SIZE, 0, &mapped));
  memcpy(mapped, r->data, r->bytes);
  VkMappedMemoryRange flush = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                               .memory = memory_handle,
                               .size = VK_WHOLE_SIZE};
  if (!(memory.memoryTypes[type].propertyFlags &
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
    VK(vkFlushMappedMemoryRanges(device, 1, &flush));
  VkCommandBuffer cb = resource_command_begin();
  VkBufferMemoryBarrier src = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                               .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
                               .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                               .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                               .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                               .buffer = staging,
                               .size = VK_WHOLE_SIZE};
  VkImageMemoryBarrier dst = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = i.image,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, h->levels, 0, 1}};
  vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_HOST_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1, &src, 1,
                       &dst);
  VkBufferImageCopy regions[DM_RESOURCE_MIPS] = {0};
  for (uint32_t n = 0; n < h->levels; n++)
    regions[n] = (VkBufferImageCopy){
        .bufferOffset = h->mips[n].offset,
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, n, 0, 1},
        .imageExtent = {h->mips[n].width, h->mips[n].height, 1}};
  vkCmdCopyBufferToImage(cb, staging, i.image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, h->levels,
                         regions);
  dst.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  dst.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  dst.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  dst.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0,
                       NULL, 1, &dst);
  resource_command_wait(cb);
  vkUnmapMemory(device, memory_handle);
  vkDestroyBuffer(device, staging, NULL);
  vkFreeMemory(device, memory_handle, NULL);
  return i;
}
static void resource_ownership(image *i, uint32_t levels, int acquire) {
  VkCommandBuffer cb = resource_command_begin();
  VkImageMemoryBarrier b = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = acquire ? 0 : VK_ACCESS_SHADER_READ_BIT,
      .dstAccessMask = acquire ? VK_ACCESS_SHADER_READ_BIT : 0,
      .oldLayout = acquire ? VK_IMAGE_LAYOUT_GENERAL
                           : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      .newLayout = acquire ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                           : VK_IMAGE_LAYOUT_GENERAL,
      .srcQueueFamilyIndex = acquire ? VK_QUEUE_FAMILY_FOREIGN_EXT : family,
      .dstQueueFamilyIndex = acquire ? family : VK_QUEUE_FAMILY_FOREIGN_EXT,
      .image = i->image,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1}};
  vkCmdPipelineBarrier(cb,
                       acquire ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                               : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       acquire ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                               : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                       0, 0, NULL, 0, NULL, 1, &b);
  resource_command_wait(cb);
}
static void resource_import(const char *directory) {
  resource_input r = resource_open(directory);
  const dm_resource_input *h = r.header;
  // This first ordinary-UID transaction is deliberately two exact real assets.
  CHECK(h->width == 256 && h->height == 256 && h->levels == 9 &&
        (h->vk_format == VK_FORMAT_ASTC_4x4_SRGB_BLOCK ||
         h->vk_format == VK_FORMAT_ASTC_6x6_SRGB_BLOCK));
  size_t wire_bytes;
  uint8_t *wire_data =
      resource_read(directory, "native-resource-layout.bin", &wire_bytes);
  CHECK(wire_bytes == sizeof(dm_resource_native));
  dm_resource_native wire;
  memcpy(&wire, wire_data, sizeof wire);
  free(wire_data);
  uint8_t digest[32], recorded[32];
  memcpy(recorded, wire.digest, 32);
  memset(wire.digest, 0, 32);
  native_sha(&wire, sizeof wire, digest);
  CHECK(!memcmp(digest, recorded, 32));
  size_t payload_bytes;
  uint8_t *payload =
      resource_read(directory, "native-resource.bin", &payload_bytes);
  CHECK(payload_bytes == wire.image_size);
  uint8_t payload_sha[32];
  native_sha(payload, payload_bytes, payload_sha);
  CHECK(!memcmp(payload_sha, wire.payload_sha256, 32));
  size_t json_bytes;
  uint8_t *json = resource_read(directory, "native-resource.json", &json_bytes);
  uint8_t metadata_sha[32];
  native_sha(json, json_bytes, metadata_sha);
  free(json);
  CHECK(!memcmp(metadata_sha, wire.metadata_sha256, 32));
  image imported = {0};
  VkImageCreateInfo ci = resource_recipe(h, 1);
  resource_recipe_check(&ci, 1);
  VK(vkCreateImage(device, &ci, NULL, &imported.image));
  vkGetImageMemoryRequirements(device, imported.image, &imported.requirements);
  CHECK(imported.requirements.size == wire.image_size &&
        imported.requirements.alignment == wire.image_alignment);
  native_slice slices[DM_RESOURCE_MIPS] = {0};
  for (uint32_t n = 0; n < h->levels; n++)
    slices[n] = native_layout(&imported, n);
  dm_resource_native expected;
  resource_native_header(&expected, &r, &imported, slices, payload_sha);
  memcpy(expected.metadata_sha256, metadata_sha, 32);
  // Exact full struct compares
  // every recipe/UUID/source/physical-offset/pitch and zero reserved byte.
  CHECK(!memcmp(&wire, &expected, sizeof wire));
  uint64_t alignment = wire.image_alignment;
  if (alignment < native_host.minImportedHostPointerAlignment)
    alignment = native_host.minImportedHostPointerAlignment;
  if (alignment < properties.limits.nonCoherentAtomSize)
    alignment = properties.limits.nonCoherentAtomSize;
  CHECK(alignment && alignment <= MAX_FILE / 4);
  uint64_t offset = alignment * 2,
           bytes =
               offset + native_align(wire.image_size, alignment) + alignment;
  CHECK(bytes <= MAX_FILE);
  uint8_t *pool = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  CHECK(pool != MAP_FAILED && !((uintptr_t)pool & (alignment - 1)));
  memset(pool, 0xa5, bytes);
  memcpy(pool + offset, payload, payload_bytes);
  free(payload);
  VkMemoryHostPointerPropertiesEXT hp = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT};
  VK(vkGetMemoryHostPointerPropertiesEXT(
      device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, pool,
      &hp));
  uint32_t type = pick_type(
      hp.memoryTypeBits & imported.requirements.memoryTypeBits,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
  VkImportMemoryHostPointerInfoEXT host_pointer = {
      .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
      .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
      .pHostPointer = pool};
  VkMemoryAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                .pNext = &host_pointer,
                                .allocationSize = bytes,
                                .memoryTypeIndex = type};
  VK(vkAllocateMemory(device, &alloc, NULL, &imported.allocation));
  VK(vkBindImageMemory(device, imported.image, imported.allocation, offset));
  void *mapped;
  VK(vkMapMemory(device, imported.allocation, 0, VK_WHOLE_SIZE, 0, &mapped));
  CHECK(mapped == pool);
  int coherent = !!(memory.memoryTypes[type].propertyFlags &
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VkMappedMemoryRange flush = {
      .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
      .memory = imported.allocation,
      .offset = offset,
      .size =
          native_align(wire.image_size, properties.limits.nonCoherentAtomSize)};
  if (!coherent)
    VK(vkFlushMappedMemoryRanges(device, 1, &flush));
  resource_ownership(&imported, h->levels, 1);
  image reference = resource_reference(&r);
  size_t pixel_bytes = 0;
  for (uint32_t n = 0; n < h->levels; n++)
    pixel_bytes += (size_t)h->mips[n].width * h->mips[n].height * 4;
  CHECK(pixel_bytes <= 1 << 20);
  uint8_t *native_pixels = malloc(pixel_bytes),
          *reference_pixels = malloc(pixel_bytes);
  CHECK(native_pixels && reference_pixels);
  resource_sample(&reference, h, reference_pixels);
  resource_sample(&imported, h, native_pixels);
  CHECK(!memcmp(native_pixels, reference_pixels, pixel_bytes));
  int varied = 0;
  for (size_t n = 4; n < pixel_bytes; n += 4)
    if (memcmp(reference_pixels + n, reference_pixels, 4)) {
      varied = 1;
      break;
    }
  CHECK(varied);
  uint8_t pixel_sha[32];
  native_sha(native_pixels, pixel_bytes, pixel_sha);
  free(native_pixels);
  free(reference_pixels);
  destroy_image(&reference);
  resource_ownership(&imported, h->levels, 0);
  native_sha(pool + offset, wire.image_size, digest);
  CHECK(!memcmp(digest, wire.payload_sha256, 32));
  for (uint64_t n = 0; n < offset; n++)
    CHECK(pool[n] == 0xa5);
  for (uint64_t n = offset + wire.image_size; n < bytes; n++)
    CHECK(pool[n] == 0xa5);
  vkDestroyImage(device, imported.image, NULL);
  vkUnmapMemory(device, imported.allocation);
  vkFreeMemory(device, imported.allocation, NULL);
  CHECK(pool[0] == 0xa5);
  pool[0] = 0x3c;
  CHECK(pool[0] == 0x3c);
  CHECK(!munmap(pool, bytes));
  printf("{\"native_resource_import_passed\":true,\"format\":%u,\"width\":%u,"
         "\"height\":%u,\"mips\":%u,\"sampled_texels\":%zu,\"sample_SHA256\":",
         h->vk_format, h->width, h->height, h->levels, pixel_bytes / 4);
  resource_hex(stdout, pixel_sha, 32);
  printf(
      ",\"reference_upload\":\"vkCmdCopyBufferToImage\",\"all_mips_exact_GPU_"
      "reference\":true,\"reference_varied\":true,\"memory_flags\":%u,\"atom\":"
      "%llu,\"pool_bytes\":%llu,\"image_offset\":%llu,\"NC_Flush_offset\":%llu,"
      "\"NC_Flush_size\":%llu,\"native_backing_extra_image_allocation\":false,"
      "\"reference_allocation_is_test_only\":true,\"original_mapping_survived_"
      "Free\":true,\"neighbors_exact\":true,\"game_modules_executed\":false}\n",
      memory.memoryTypes[type].propertyFlags,
      (unsigned long long)properties.limits.nonCoherentAtomSize,
      (unsigned long long)bytes, (unsigned long long)offset,
      (unsigned long long)flush.offset,
      (unsigned long long)(coherent ? 0 : flush.size));
  resource_close(&r);
}
static void resource_pair(const char *directory, int bake) {
  for (unsigned n = 0; n < 2; n++) {
    char path[4096];
    resource_path(path, directory, n ? "resource6" : "resource4");
    resource_input input = resource_open(path);
    CHECK(input.header->vk_format ==
          (uint32_t)(n ? VK_FORMAT_ASTC_6x6_SRGB_BLOCK
                       : VK_FORMAT_ASTC_4x4_SRGB_BLOCK));
    CHECK(input.header->width == 256 && input.header->height == 256 &&
          input.header->levels == 9);
    resource_close(&input);
    if (bake)
      resource_bake(path);
    else
      resource_import(path);
  }
}
