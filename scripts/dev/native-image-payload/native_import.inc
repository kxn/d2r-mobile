/* Owned DM native-image producer v1. PanVK-specific layout contract.
 * No vendor tiling algorithm: physical offsets are the exact Mesa query
 * outputs, while encoding/export use its existing host-image-copy path.
 */
typedef struct {
  uint64_t offset, size, row_pitch, array_pitch, depth_pitch;
} native_slice;
typedef struct {
  pack_header common;
  uint32_t contract, pattern, external_handle, producer_layout, producer_family;
  uint32_t reserved;
  uint64_t image_size, image_alignment, data_offset;
  native_slice slices[LEVELS];
  uint8_t payload_sha256[32];
} native_header;
enum { NATIVE_CONTRACT = 1 };

static uint64_t native_align(uint64_t n, uint64_t alignment) {
  CHECK(alignment && !(alignment & (alignment - 1)) &&
        n <= UINT64_MAX - alignment + 1);
  return (n + alignment - 1) & ~(alignment - 1);
}
static void native_sha(const void *data, size_t size, uint8_t digest[32]) {
  mbedtls_sha256(data, size, digest, 0);
}
static native_slice native_layout(image *i, uint32_t level) {
  VkSubresourceHostMemcpySizeEXT bytes = {
      .sType = VK_STRUCTURE_TYPE_SUBRESOURCE_HOST_MEMCPY_SIZE_EXT};
  VkSubresourceLayout2EXT layout = {
      .sType = VK_STRUCTURE_TYPE_SUBRESOURCE_LAYOUT_2_EXT, .pNext = &bytes};
  VkImageSubresource2EXT sub = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_SUBRESOURCE_2_EXT,
      .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0}};
  vkGetImageSubresourceLayout2EXT(device, i->image, &sub, &layout);
  CHECK(bytes.size && bytes.size < MAX_FILE);
  // Additional defined behavior of this exact opt-in PanVK producer contract;
  // these physical offset fields are not portable optimal-tiling Vulkan data.
  return (native_slice){layout.subresourceLayout.offset, bytes.size,
                        layout.subresourceLayout.rowPitch,
                        layout.subresourceLayout.arrayPitch,
                        layout.subresourceLayout.depthPitch};
}
static void native_header_init(native_header *h, VkFormat format,
                               unsigned pattern) {
  memset(h, 0, sizeof *h);
  header_init(&h->common, format);
  memcpy(h->common.magic, "DMNATV1", 8);
  h->common.version = 2;
  h->common.header_bytes = sizeof *h;
  h->contract = NATIVE_CONTRACT;
  h->pattern = pattern;
  h->external_handle = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
  h->producer_layout = VK_IMAGE_LAYOUT_GENERAL;
  h->producer_family = VK_QUEUE_FAMILY_FOREIGN_EXT;
  h->data_offset = (sizeof *h + 63) & ~UINT64_C(63);
}
static int native_validate(const uint8_t *data, size_t bytes, VkFormat format,
                           unsigned pattern) {
  if (bytes < sizeof(native_header) || bytes > MAX_FILE)
    return 0;
  const native_header *h = (const void *)data;
  native_header expected_header;
  native_header_init(&expected_header, format, pattern);
  if (memcmp(h->common.magic, expected_header.common.magic, 8) ||
      h->common.version != 2 || h->common.header_bytes != sizeof *h ||
      h->common.file_bytes != bytes ||
      memcmp(&h->common.format, &expected_header.common.format,
             offsetof(pack_header, parts) - offsetof(pack_header, format)) ||
      memcmp(&h->contract, &expected_header.contract,
             offsetof(native_header, image_size) -
                 offsetof(native_header, contract)) ||
      h->data_offset != expected_header.data_offset || !h->image_size ||
      !h->image_alignment || h->image_alignment > MAX_FILE ||
      (h->image_alignment & (h->image_alignment - 1)) ||
      h->data_offset > bytes || h->image_size != bytes - h->data_offset)
    return 0;
  uint64_t previous = 0;
  for (unsigned n = 0; n < LEVELS; n++) {
    const native_slice *s = &h->slices[n];
    const part *p = &h->common.parts[n];
    if (p->level != n || p->layer || p->width != mip(FIXTURE_WIDTH, n) ||
        p->height != mip(FIXTURE_HEIGHT, n) || p->offset != s->offset ||
        p->size != s->size || !s->size || s->offset < previous ||
        s->offset > h->image_size || s->size > h->image_size - s->offset)
      return 0;
    previous = s->offset + s->size;
  }
  uint8_t digest[32];
  file_digest(data, bytes, digest);
  if (memcmp(h->common.digest, digest, 32))
    return 0;
  native_sha(data + h->data_offset, h->image_size, digest);
  return !memcmp(h->payload_sha256, digest, 32);
}
static void native_path(char path[4096], const char *directory, VkFormat format,
                        unsigned pattern) {
  CHECK(snprintf(path, 4096, "%s/native-import-%u-%u.bin", directory, format,
                 pattern) < 4096);
}
static void native_bake(const char *directory, VkFormat format) {
  for (unsigned version = 0; version < 2; version++) {
    pattern_version = version;
    image i = create_image(format);
    transition(&i, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    native_header h;
    native_header_init(&h, format, version);
    h.image_size = i.requirements.size;
    h.image_alignment = i.requirements.alignment;
    CHECK(h.image_size && h.image_size < MAX_FILE - h.data_offset);
    h.common.file_bytes = h.data_offset + h.image_size;
    uint8_t *output = calloc(1, h.common.file_bytes);
    CHECK(output);
    uint64_t previous = 0, opaque_bytes = 0;
    for (unsigned level = 0; level < LEVELS; level++) {
      native_slice slice = native_layout(&i, level);
      CHECK(slice.offset >= previous && slice.offset <= h.image_size &&
            slice.size <= h.image_size - slice.offset);
      previous = slice.offset + slice.size;
      h.slices[level] = slice;
      h.common.parts[level] = (part){
          level, 0, mip(FIXTURE_WIDTH, level), mip(FIXTURE_HEIGHT, level), slice.offset, slice.size};
      size_t bytes;
      uint8_t *linear = linear_source(format, level, 0, &bytes);
      (void)bytes;
      VkMemoryToImageCopyEXT copy = {
          .sType = VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY_EXT,
          .pHostPointer = linear,
          .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1},
          .imageExtent = {mip(FIXTURE_WIDTH, level), mip(FIXTURE_HEIGHT, level), 1}};
      VkCopyMemoryToImageInfoEXT info = {
          .sType = VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INFO_EXT,
          .dstImage = i.image,
          .dstImageLayout = VK_IMAGE_LAYOUT_GENERAL,
          .regionCount = 1,
          .pRegions = &copy};
      VK(vkCopyMemoryToImageEXT(device, &info));
#ifdef DM_NATIVE_LINEAR_MATRIX
      uint8_t *plain=malloc(bytes);CHECK(plain);
      VkImageToMemoryCopyEXT plain_region={.sType=VK_STRUCTURE_TYPE_IMAGE_TO_MEMORY_COPY_EXT,
        .pHostPointer=plain,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level,0,1},
        .imageExtent={mip(FIXTURE_WIDTH,level),mip(FIXTURE_HEIGHT,level),1}};
      VkCopyImageToMemoryInfoEXT plain_copy={.sType=VK_STRUCTURE_TYPE_COPY_IMAGE_TO_MEMORY_INFO_EXT,
        .srcImage=i.image,.srcImageLayout=VK_IMAGE_LAYOUT_GENERAL,.regionCount=1,.pRegions=&plain_region};
      VK(vkCopyImageToMemoryEXT(device,&plain_copy));CHECK(!memcmp(plain,linear,bytes));free(plain);
#endif
      free(linear);
      VkImageToMemoryCopyEXT back = {
          .sType = VK_STRUCTURE_TYPE_IMAGE_TO_MEMORY_COPY_EXT,
          .pHostPointer = output + h.data_offset + slice.offset,
          .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1},
          .imageExtent = {mip(FIXTURE_WIDTH, level), mip(FIXTURE_HEIGHT, level), 1}};
      VkCopyImageToMemoryInfoEXT exported = {
          .sType = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_MEMORY_INFO_EXT,
          .flags = VK_HOST_IMAGE_COPY_MEMCPY_EXT,
          .srcImage = i.image,
          .srcImageLayout = VK_IMAGE_LAYOUT_GENERAL,
          .regionCount = 1,
          .pRegions = &back};
      VK(vkCopyImageToMemoryEXT(device, &exported));
      opaque_bytes += slice.size;
    }
#ifdef DM_NATIVE_LINEAR_MATRIX
    printf("{\"native_plain_hostcopy_roundtrip\":true,\"format\":%u,\"pattern\":%u,\"levels\":%u,\"every_mip_compressed_blocks_exact\":true}\n",format,version,LEVELS);
#endif
    native_sha(output + h.data_offset, h.image_size, h.payload_sha256);
    memcpy(output, &h, sizeof h);
    file_digest(output, h.common.file_bytes,
                ((native_header *)output)->common.digest);
    CHECK(native_validate(output, h.common.file_bytes, format, version));
    char path[4096];
    native_path(path, directory, format, version);
    FILE *f = fopen(path, "wb");
    CHECK(f);
    CHECK(fwrite(output, 1, h.common.file_bytes, f) == h.common.file_bytes);
    CHECK(!fclose(f));
    printf(
        "{\"native_producer_bake\":true,\"format\":%u,\"pattern\":%u,"
        "\"physical_image_bytes\":%llu,\"declared_mip_bytes\":%llu,\"producer_"
        "layout\":\"GENERAL\",\"algorithm\":\"existing Mesa host-copy\"}\n",
        format, version, (unsigned long long)h.image_size,
        (unsigned long long)opaque_bytes);
    free(output);
    destroy_image(&i);
  }
  pattern_version = 0;
}
typedef struct {
  const uint8_t *data;
  size_t bytes;
  native_header header;
} native_file;
static native_file native_open(const char *directory, VkFormat format,
                               unsigned pattern) {
  char path[4096];
  native_path(path, directory, format, pattern);
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  CHECK(fd >= 0);
  struct stat st;
  CHECK(!fstat(fd, &st) && st.st_size > 0 && st.st_size <= MAX_FILE);
  native_file file = {.bytes = st.st_size};
  file.data = mmap(NULL, file.bytes, PROT_READ, MAP_PRIVATE, fd, 0);
  CHECK(file.data != MAP_FAILED);
  CHECK(!close(fd));
  CHECK(native_validate(file.data, file.bytes, format, pattern));
  memcpy(&file.header, file.data, sizeof file.header);
  return file;
}
static void native_close(native_file *file) {
  CHECK(!munmap((void *)file->data, file->bytes));
  file->data = NULL;
}
static image native_unbound(VkFormat format, const native_header *h) {
  image i = {0};
  VkImageCreateInfo ci = recipe(format);
  VK(vkCreateImage(device, &ci, NULL, &i.image));
  vkGetImageMemoryRequirements(device, i.image, &i.requirements);
  CHECK(i.requirements.size == h->image_size &&
        i.requirements.alignment == h->image_alignment);
  for (unsigned n = 0; n < LEVELS; n++) {
    native_slice layout = native_layout(&i, n);
    CHECK(!memcmp(&layout, &h->slices[n], sizeof layout));
  }
  return i;
}
static int native_binding_valid(const image *i, uint64_t offset,
                                uint64_t pool_size) {
  return !(offset & (i->requirements.alignment - 1)) && offset <= pool_size &&
         i->requirements.size <= pool_size - offset;
}
static void native_ownership(image *i, int acquire) {
  VkCommandBufferAllocateInfo ca = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = commands,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1};
  VkCommandBuffer cb;
  VK(vkAllocateCommandBuffers(device, &ca, &cb));
  VkCommandBufferBeginInfo begin = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  VK(vkBeginCommandBuffer(cb, &begin));
  VkImageMemoryBarrier barrier = {
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
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, LEVELS, 0, 1}};
  // The explicit producer contract owns GENERAL before initial acquire.
  // No UNDEFINED transition and no acquireUnmodified=true assertion.
  vkCmdPipelineBarrier(cb,
                       acquire ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                               : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       acquire ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                               : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                       0, 0, NULL, 0, NULL, 1, &barrier);
  VK(vkEndCommandBuffer(cb));
  VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  VkFence fence;
  VK(vkCreateFence(device, &fi, NULL, &fence));
  VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                         .commandBufferCount = 1,
                         .pCommandBuffers = &cb};
  VK(vkQueueSubmit(queue, 1, &submit, fence));
  VK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
  vkDestroyFence(device, fence, NULL);
  vkFreeCommandBuffers(device, commands, 1, &cb);
}
static void native_query_negatives(VkFormat format) {
  VkPhysicalDeviceExternalImageFormatInfo ext = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
      .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
  VkPhysicalDeviceImageFormatInfo2 query = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
      .pNext = &ext,
      .format = format,
      .type = VK_IMAGE_TYPE_2D,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = usage};
  VkImageFormatProperties2 out = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};
  const VkImageUsageFlagBits forbidden[] = {VK_IMAGE_USAGE_STORAGE_BIT,
                                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
#ifndef DM_NATIVE_TURNIP_HOST_ASTC
                                            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
#endif
  };
  for (unsigned n = 0; n < sizeof forbidden / sizeof *forbidden; n++) {
    query.usage = usage | forbidden[n];
    CHECK(vkGetPhysicalDeviceImageFormatProperties2(physical, &query, &out) ==
          VK_ERROR_FORMAT_NOT_SUPPORTED);
  }
#ifdef DM_NATIVE_TURNIP_HOST_ASTC
  // The explicit Turnip profile permits transfer aliases; default PanVK keeps
  // its original narrower rejection. Pixel/byte oracles below are unchanged.
  query.usage = usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  CHECK(vkGetPhysicalDeviceImageFormatProperties2(physical, &query, &out) == VK_SUCCESS);
  ext.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_MAPPED_FOREIGN_MEMORY_BIT_EXT;
  CHECK(vkGetPhysicalDeviceImageFormatProperties2(physical, &query, &out) == VK_ERROR_FORMAT_NOT_SUPPORTED);
  ext.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
#endif
  query.usage = usage;
  query.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
  CHECK(vkGetPhysicalDeviceImageFormatProperties2(physical, &query, &out) ==
        VK_ERROR_FORMAT_NOT_SUPPORTED);
  query.flags = 0;
  query.type = VK_IMAGE_TYPE_3D;
  CHECK(vkGetPhysicalDeviceImageFormatProperties2(physical, &query, &out) ==
        VK_ERROR_FORMAT_NOT_SUPPORTED);
  query.type = VK_IMAGE_TYPE_2D;
  query.format = VK_FORMAT_BC3_UNORM_BLOCK;
  CHECK(vkGetPhysicalDeviceImageFormatProperties2(physical, &query, &out) ==
        VK_ERROR_FORMAT_NOT_SUPPORTED);
}
static void native_metadata_negatives(const native_file *file,
                                      VkFormat format) {
  CHECK(!native_validate(file->data, sizeof(native_header) - 1, format, 0));
  CHECK(!native_validate(file->data, file->bytes - 1, format, 0));
  uint8_t *bad = malloc(file->bytes);
  CHECK(bad);
  memcpy(bad, file->data, file->bytes);
  native_header *h = (void *)bad;
  h->common.layout_uuid[0] ^= 1;
  file_digest(bad, file->bytes, h->common.digest);
  CHECK(!native_validate(bad, file->bytes, format, 0));
  memcpy(bad, file->data, file->bytes);
#ifdef DM_NATIVE_LINEAR_MATRIX
  static const uint8_t old_AA[16]={0x45,0x29,0xbb,0xb8,0x70,0xcd,0xbd,0xd0,0x2d,0xb2,0x14,0x74,0x2b,0x3e,0x41,0x70};
  memcpy(h->common.layout_uuid,old_AA,16);file_digest(bad,file->bytes,h->common.digest);
  CHECK(!native_validate(bad,file->bytes,format,0));
  memcpy(bad,file->data,file->bytes);
#endif
  h->common.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
  file_digest(bad, file->bytes, h->common.digest);
  CHECK(!native_validate(bad, file->bytes, format, 0));
  memcpy(bad, file->data, file->bytes);
  h->slices[LEVELS-1].offset = h->image_size;
  h->common.parts[LEVELS-1].offset = h->image_size;
  file_digest(bad, file->bytes, h->common.digest);
  CHECK(!native_validate(bad, file->bytes, format, 0));
  memcpy(bad, file->data, file->bytes);
  bad[h->data_offset] ^= 1;
  CHECK(!native_validate(bad, file->bytes, format, 0));
  free(bad);
#ifdef DM_NATIVE_LINEAR_MATRIX
  printf("{\"native_old_AA_opaque_identity_refused\":true,\"format\":%u,\"production_table_rejection_proven\":false}\n",format);
#endif
}
#ifdef DM_NATIVE_LINEAR_MATRIX
#include "native_linear_roundtrip.inc"
#endif
static void native_import(const char *directory, VkFormat format) {
  native_query_negatives(format);
  native_file file = native_open(directory, format, 0);
  native_metadata_negatives(&file, format);
  native_header h = file.header;
  uint64_t alignment = h.image_alignment;
  if (alignment < native_host.minImportedHostPointerAlignment)
    alignment = native_host.minImportedHostPointerAlignment;
  if (alignment < properties.limits.nonCoherentAtomSize)
    alignment = properties.limits.nonCoherentAtomSize;
  CHECK(alignment && alignment <= MAX_FILE / 4);
  uint64_t offset = 2 * alignment,
           padded = native_align(h.image_size, alignment),
           pool_bytes = offset + padded + alignment;
  CHECK(pool_bytes <= MAX_FILE);
  uint8_t *pool_memory = mmap(NULL, pool_bytes, PROT_READ | PROT_WRITE,
                              MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  CHECK(pool_memory != MAP_FAILED &&
        !((uintptr_t)pool_memory & (alignment - 1)));
  memset(pool_memory, 0xa5, pool_bytes);
  memcpy(pool_memory + offset, file.data + h.data_offset, h.image_size);
  native_close(&file);
  for (unsigned lifecycle = 0; lifecycle < 2; lifecycle++) {
    if (lifecycle) {
      file = native_open(directory, format, 0);
      memcpy(pool_memory + offset, file.data + h.data_offset, h.image_size);
      native_close(&file);
    }
    image i = native_unbound(format, &h);
    CHECK(native_binding_valid(&i, offset, pool_bytes));
    CHECK(!native_binding_valid(&i, offset + 1, pool_bytes));
    CHECK(!native_binding_valid(&i, pool_bytes, pool_bytes));
    VkMemoryHostPointerPropertiesEXT host_properties = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT};
    VK(vkGetMemoryHostPointerPropertiesEXT(
        device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
        pool_memory, &host_properties));
    uint32_t bits =
        host_properties.memoryTypeBits & i.requirements.memoryTypeBits;
    uint32_t type = pick_type(bits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                        VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    int coherent = !!(memory.memoryTypes[type].propertyFlags &
                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
#ifdef DM_NATIVE_TURNIP_HOST_ASTC
    CHECK(coherent);
#endif
    VkImportMemoryHostPointerInfoEXT host_pointer = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
        .pHostPointer = pool_memory};
    VkMemoryAllocateInfo alloc = {.sType =
                                      VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                  .pNext = &host_pointer,
                                  .allocationSize = pool_bytes,
                                  .memoryTypeIndex = type};
    VK(vkAllocateMemory(device, &alloc, NULL, &i.allocation));
#ifdef DM_NATIVE_TURNIP_HOST_ASTC
    image invalid = native_unbound(format, &h);
    CHECK(vkBindImageMemory(device, invalid.image, i.allocation, offset+1) == VK_ERROR_INVALID_EXTERNAL_HANDLE);
    CHECK(vkBindImageMemory(device, invalid.image, i.allocation, pool_bytes) == VK_ERROR_INVALID_EXTERNAL_HANDLE);
    vkDestroyImage(device,invalid.image,NULL);
#endif
    VK(vkBindImageMemory(device, i.image, i.allocation, offset));
    void *mapped;
    VK(vkMapMemory(device, i.allocation, 0, VK_WHOLE_SIZE, 0, &mapped));
    CHECK(mapped == pool_memory);
    uint64_t atom = properties.limits.nonCoherentAtomSize;
    VkMappedMemoryRange flush = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                                 .memory = i.allocation,
                                 .offset = offset,
                                 .size = native_align(h.image_size, atom)};
    for (unsigned version = 0; version < 2; version++) {
      if (version) {
        file = native_open(directory, format, version);
        CHECK(file.header.image_size == h.image_size &&
              file.header.image_alignment == h.image_alignment &&
              !memcmp(file.header.slices, h.slices, sizeof h.slices));
        memcpy(pool_memory + offset, file.data + file.header.data_offset,
               h.image_size);
        h = file.header;
        native_close(&file);
      }
      if (!coherent)
        VK(vkFlushMappedMemoryRanges(device, 1, &flush));
      native_ownership(&i, 1);
      pattern_version = version;
      // Prove imported host-produced bytes BEFORE any GPU upload can repair
      // them. Then independently verify B2T/T2B and sample all views again.
      sample_all(&i, format);
#ifdef DM_NATIVE_LINEAR_MATRIX
      native_linear_transfer_roundtrip(&i,format);
      sample_all(&i,format);
#endif
      native_ownership(&i, 0);
      uint8_t digest[32];
      native_sha(pool_memory + offset, h.image_size, digest);
      CHECK(!memcmp(digest, h.payload_sha256, 32));
      for (uint64_t n = 0; n < offset; n++)
        CHECK(pool_memory[n] == 0xa5);
      for (uint64_t n = offset + h.image_size; n < pool_bytes; n++)
        CHECK(pool_memory[n] == 0xa5);
      printf("{\"native_import_sampled\":true,\"format\":%u,\"lifecycle\":%u,"
             "\"pattern\":%u,\"pool_bytes\":%llu,\"image_offset\":%llu,"
             "\"memory_flags\":%u,\"atom\":%llu,\"NC_Flush_offset\":%llu,\"NC_"
             "Flush_size\":%llu,\"extra_image_allocation\":false,\"game_"
             "modules_executed\":false}\n",
             format, lifecycle, version, (unsigned long long)pool_bytes,
             (unsigned long long)offset, memory.memoryTypes[type].propertyFlags,
             (unsigned long long)atom, (unsigned long long)flush.offset,
             (unsigned long long)(coherent ? 0 : flush.size));
    }
    vkDestroyImage(device, i.image, NULL);
    vkUnmapMemory(device, i.allocation);
    vkFreeMemory(device, i.allocation, NULL);
    // Vulkan must not unmap the original caller's shared mapping.
    CHECK(pool_memory[0] == 0xa5);
    pool_memory[0] = 0x3c;
    CHECK(pool_memory[0] == 0x3c);
    pool_memory[0] = 0xa5;
    // Reset metadata to the next cold external-producer version0 publication.
    native_header_init(&h, format, 0);
    file = native_open(directory, format, 0);
    h = file.header;
    native_close(&file);
  }
  CHECK(!munmap(pool_memory, pool_bytes));
  pattern_version = 0;
}
