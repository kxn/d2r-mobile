// Real compressed resource producer; original driver Host Image Copy owns
// tiling.
#include "resource_format.h"
typedef struct {
  uint8_t *data;
  size_t bytes;
  const dm_resource_input *header;
} resource_input;
static void resource_digest(const uint8_t *data, size_t bytes,
                            uint8_t out[32]) {
  const size_t at = offsetof(dm_resource_input, digest);
  uint8_t zero[32] = {0};
  mbedtls_sha256_context c;
  mbedtls_sha256_init(&c);
  mbedtls_sha256_starts(&c, 0);
  mbedtls_sha256_update(&c, data, at);
  mbedtls_sha256_update(&c, zero, sizeof zero);
  mbedtls_sha256_update(&c, data + at + sizeof zero, bytes - at - sizeof zero);
  mbedtls_sha256_finish(&c, out);
  mbedtls_sha256_free(&c);
}
static void resource_path(char path[4096], const char *directory,
                          const char *name) {
  CHECK(snprintf(path, 4096, "%s/%s", directory, name) < 4096);
}
static uint8_t *resource_read(const char *directory, const char *name,
                              size_t *size) {
  char path[4096];
  resource_path(path, directory, name);
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  CHECK(fd >= 0);
  struct stat st;
  CHECK(!fstat(fd, &st) && st.st_size > 0 && st.st_size <= MAX_FILE);
  *size = st.st_size;
  // One bounded immutable owned snapshot; validation and host-copy consume
  // exactly these bytes even if the input pathname changes afterward.
  uint8_t *data = malloc(*size);
  CHECK(data);
  size_t done = 0;
  while (done < *size) {
    ssize_t n = read(fd, data + done, *size - done);
    CHECK(n > 0);
    done += n;
  }
  CHECK(!close(fd));
  return data;
}
static resource_input resource_open(const char *directory) {
  resource_input r = {0};
  r.data = resource_read(directory, "resource-input.bin", &r.bytes);
  CHECK(r.bytes >= sizeof(dm_resource_input));
  r.header = (const void *)r.data;
  const dm_resource_input *h = r.header;
  CHECK(!memcmp(h->magic, "DMRSRC1\0", 8) && h->version == 1 &&
        h->header_bytes == sizeof *h && h->file_bytes == r.bytes &&
        !h->reserved);
  CHECK(h->width && h->height && h->width <= 4096 && h->height <= 4096 &&
        h->levels && h->levels <= DM_RESOURCE_MIPS);
  uint32_t max = h->width > h->height ? h->width : h->height, levels = 0;
  while (max) {
    levels++;
    max >>= 1;
  }
  CHECK(h->levels <= levels);
  CHECK((h->game_format == 95 && h->nvn_format == 0x79 &&
         h->vk_format == VK_FORMAT_ASTC_4x4_UNORM_BLOCK && h->block == 4) ||
        (h->game_format == 81 && h->nvn_format == 0x87 &&
         h->vk_format == VK_FORMAT_ASTC_4x4_SRGB_BLOCK && h->block == 4) ||
        (h->game_format == 99 && h->nvn_format == 0x7d &&
         h->vk_format == VK_FORMAT_ASTC_6x6_UNORM_BLOCK && h->block == 6) ||
        (h->game_format == 85 && h->nvn_format == 0x8b &&
         h->vk_format == VK_FORMAT_ASTC_6x6_SRGB_BLOCK && h->block == 6));
  uint8_t digest[32];
  resource_digest(r.data, r.bytes, digest);
  CHECK(!memcmp(digest, h->digest, 32));
  uint64_t previous = (sizeof *h + 63) & ~UINT64_C(63);
  CHECK(previous <= r.bytes);
  for (uint64_t n = sizeof *h; n < previous; n++)
    CHECK(!r.data[n]);
  for (uint32_t n = 0; n < DM_RESOURCE_MIPS; n++) {
    const dm_resource_mip *m = &h->mips[n];
    if (n >= h->levels) {
      const dm_resource_mip zero = {0};
      CHECK(!memcmp(m, &zero, sizeof zero));
      continue;
    }
    CHECK(m->width == mip(h->width, n) && m->height == mip(h->height, n));
    uint64_t bytes = (uint64_t)((m->width + h->block - 1) / h->block) *
                     ((m->height + h->block - 1) / h->block) * 16;
    CHECK(m->offset == previous && m->offset <= r.bytes && m->size == bytes &&
          m->size <= r.bytes - m->offset);
    native_sha(r.data + m->offset, m->size, digest);
    CHECK(!memcmp(digest, m->sha256, 32));
    previous += m->size;
  }
  CHECK(previous == r.bytes);
  size_t json_size;
  uint8_t *json = resource_read(directory, "resource-source.json", &json_size);
  native_sha(json, json_size, digest);
  CHECK(!memcmp(digest, h->declaration_sha256, 32));
  free(json);
  printf("{\"native_resource_input_verified\":true,\"width\":%u,\"height\":%u,"
         "\"levels\":%u,\"format\":%u,\"source_bytes\":%zu,\"game_readonly_"
         "authority\":false}\n",
         h->width, h->height, h->levels, h->vk_format, r.bytes);
  return r;
}
static void resource_close(resource_input *r) {
  free(r->data);
  memset(r, 0, sizeof *r);
}
static void resource_hex(FILE *f, const uint8_t *bytes, size_t size) {
  fputc('"', f);
  for (size_t n = 0; n < size; n++)
    fprintf(f, "%02x", bytes[n]);
  fputc('"', f);
}
static void resource_write(const char *directory, const char *name,
                           const void *data, size_t bytes) {
  char path[4096];
  resource_path(path, directory, name);
  int fd = open(path, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
  CHECK(fd >= 0);
  const uint8_t *src = data;
  size_t done = 0;
  while (done < bytes) {
    ssize_t n = write(fd, src + done, bytes - done);
    CHECK(n > 0);
    done += n;
  }
  CHECK(!fsync(fd));
  CHECK(!close(fd));
}
static void resource_source_digest(uint8_t out[32]) {
  const char *hex = DM_NATIVE_RESOURCE_PRODUCER_SHA;
  CHECK(strlen(hex) == 64);
  for (unsigned n = 0; n < 32; n++) {
    unsigned value;
    CHECK(sscanf(hex + n * 2, "%2x", &value) == 1);
    out[n] = value;
  }
}
static void resource_native_header(dm_resource_native *o,
                                   const resource_input *r, const image *i,
                                   const native_slice *slices,
                                   const uint8_t payload_sha[32]) {
  memset(o, 0, sizeof *o);
  memcpy(o->magic, "DMNRES1", 8);
  o->version = 1;
  o->header_bytes = sizeof *o;
  const dm_resource_input *h = r->header;
  o->image_size = i->requirements.size;
  o->image_alignment = i->requirements.alignment;
  o->format = h->vk_format;
  o->width = h->width;
  o->height = h->height;
  o->depth = 1;
  o->levels = h->levels;
  o->layers = 1;
  o->image_type = VK_IMAGE_TYPE_2D;
  o->samples = VK_SAMPLE_COUNT_1_BIT;
  o->tiling = VK_IMAGE_TILING_OPTIMAL;
  o->usage = usage;
  o->sharing = VK_SHARING_MODE_EXCLUSIVE;
  o->initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  o->external_handle = native_external.handleTypes;
  o->producer_layout = VK_IMAGE_LAYOUT_GENERAL;
  o->producer_family = VK_QUEUE_FAMILY_FOREIGN_EXT;
  o->vendor = properties.vendorID;
  o->device_id = properties.deviceID;
  o->driver_version = properties.driverVersion;
  o->driver_id = native_driver.driverID;
  memcpy(o->layout_uuid, host.optimalTilingLayoutUUID, 16);
  memcpy(o->device_uuid, ids.deviceUUID, 16);
  memcpy(o->driver_uuid, ids.driverUUID, 16);
  native_sha(r->data, r->bytes, o->input_sha256);
  memcpy(o->payload_sha256, payload_sha, 32);
  memcpy(o->declaration_sha256, h->declaration_sha256, 32);
  resource_source_digest(o->producer_source_sha256);
  for (unsigned n = 0; n < h->levels; n++) {
    o->mips[n] = (dm_resource_native_mip){
        slices[n].offset, slices[n].size, slices[n].row_pitch,
        slices[n].array_pitch, slices[n].depth_pitch};
  }
}
static dm_resource_native resource_bake(const char *directory,
                                         const uint8_t *expected_input,
                                         const uint8_t *expected_declaration) {
  resource_input r = resource_open(directory);
  const dm_resource_input *h = r.header;
  if (expected_input) {
    uint8_t actual[32];
    native_sha(r.data, r.bytes, actual);
    CHECK(!memcmp(actual, expected_input, 32));
    CHECK(expected_declaration &&
          !memcmp(h->declaration_sha256, expected_declaration, 32));
  }
  VkPhysicalDeviceExternalImageFormatInfo external = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
      .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
  VkPhysicalDeviceImageFormatInfo2 query = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
      .pNext = &external,
      .format = h->vk_format,
      .type = VK_IMAGE_TYPE_2D,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = usage};
  VkExternalImageFormatProperties ext = {
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
  VkImageFormatProperties2 out = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2, .pNext = &ext};
  VK(vkGetPhysicalDeviceImageFormatProperties2(physical, &query, &out));
  CHECK(ext.externalMemoryProperties.externalMemoryFeatures ==
            VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT &&
        ext.externalMemoryProperties.compatibleHandleTypes ==
            external.handleType &&
        !ext.externalMemoryProperties.exportFromImportedHandleTypes);
  CHECK(h->width <= out.imageFormatProperties.maxExtent.width &&
        h->height <= out.imageFormatProperties.maxExtent.height &&
        h->levels <= out.imageFormatProperties.maxMipLevels &&
        out.imageFormatProperties.maxArrayLayers == 1 &&
        out.imageFormatProperties.sampleCounts == 1);
  VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                          .pNext = &native_external,
                          .imageType = VK_IMAGE_TYPE_2D,
                          .format = h->vk_format,
                          .extent = {h->width, h->height, 1},
                          .mipLevels = h->levels,
                          .arrayLayers = 1,
                          .samples = VK_SAMPLE_COUNT_1_BIT,
                          .tiling = VK_IMAGE_TILING_OPTIMAL,
                          .usage = usage,
                          .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                          .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
  image i = {0};
  VK(vkCreateImage(device, &ci, NULL, &i.image));
  vkGetImageMemoryRequirements(device, i.image, &i.requirements);
  CHECK(i.requirements.size && i.requirements.size <= MAX_FILE);
  VkMemoryAllocateInfo alloc = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = i.requirements.size,
      .memoryTypeIndex = pick_type(i.requirements.memoryTypeBits, 0)};
  VK(vkAllocateMemory(device, &alloc, NULL, &i.allocation));
  VK(vkBindImageMemory(device, i.image, i.allocation, 0));
  VkHostImageLayoutTransitionInfoEXT transition = {
      .sType = VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO_EXT,
      .image = i.image,
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .newLayout = VK_IMAGE_LAYOUT_GENERAL,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, h->levels, 0, 1}};
  VK(vkTransitionImageLayoutEXT(device, 1, &transition));
  uint8_t *payload = calloc(1, i.requirements.size);
  CHECK(payload);
  native_slice slices[DM_RESOURCE_MIPS] = {0};
  uint64_t previous = 0, opaque = 0;
  for (uint32_t n = 0; n < h->levels; n++) {
    const dm_resource_mip *m = &h->mips[n];
    native_slice s = native_layout(&i, n);
    CHECK(s.offset >= previous && s.offset <= i.requirements.size &&
          s.size <= i.requirements.size - s.offset);
    slices[n] = s;
    previous = s.offset + s.size;
    opaque += s.size;
    VkMemoryToImageCopyEXT copy = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY_EXT,
        .pHostPointer = r.data + m->offset,
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, n, 0, 1},
        .imageExtent = {m->width, m->height, 1}};
    VkCopyMemoryToImageInfoEXT upload = {
        .sType = VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INFO_EXT,
        .dstImage = i.image,
        .dstImageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .regionCount = 1,
        .pRegions = &copy};
    // Canonicalize opaque padding using the existing native MEMCPY path,
    // before normal linear ASTC upload overwrites every useful block. This
    // exact sampled-only PanVK contract has no AFBC/CRC plane metadata. It is
    // offline work, not a runtime image-copy policy or new tiling algorithm.
    uint8_t *zero = calloc(1, s.size);
    CHECK(zero);
    copy.pHostPointer = zero;
    upload.flags = VK_HOST_IMAGE_COPY_MEMCPY_EXT;
    VK(vkCopyMemoryToImageEXT(device, &upload));
    copy.pHostPointer = r.data + m->offset;
    upload.flags = 0;
    free(zero);
    VK(vkCopyMemoryToImageEXT(device, &upload));
    VkImageToMemoryCopyEXT back = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_TO_MEMORY_COPY_EXT,
        .pHostPointer = payload + s.offset,
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, n, 0, 1},
        .imageExtent = {m->width, m->height, 1}};
    VkCopyImageToMemoryInfoEXT exported = {
        .sType = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_MEMORY_INFO_EXT,
        .flags = VK_HOST_IMAGE_COPY_MEMCPY_EXT,
        .srcImage = i.image,
        .srcImageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .regionCount = 1,
        .pRegions = &back};
    VK(vkCopyImageToMemoryEXT(device, &exported));
    // Same original host-copy API back to compact ASTC proves the actual
    // compressed blocks survive tiling; not an RGBA codec or GPU sample oracle.
    uint8_t *roundtrip = malloc(m->size);
    CHECK(roundtrip);
    back.pHostPointer = roundtrip;
    exported.flags = 0;
    VK(vkCopyImageToMemoryEXT(device, &exported));
    CHECK(!memcmp(roundtrip, r.data + m->offset, m->size));
    free(roundtrip);
  }
  uint8_t input_sha[32], payload_sha[32];
  native_sha(r.data, r.bytes, input_sha);
  native_sha(payload, i.requirements.size, payload_sha);
  resource_write(directory, "native-resource.bin", payload,
                 i.requirements.size);
  char path[4096];
  resource_path(path, directory, "native-resource.json");
  int fd = open(path, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
  CHECK(fd >= 0);
  FILE *f = fdopen(fd, "w");
  CHECK(f);
  fprintf(f, "{\n\"schema\":1,\"contract\":\"DM native image producer "
             "v1\",\"complete\":true,\"native_payload\":\"native-resource."
             "bin\",\"input\":\"resource-input.bin\",\"source_declaration\":"
             "\"resource-source.json\",\n\"native_payload_sha256\":");
  resource_hex(f, payload_sha, 32);
  fprintf(f, ",\"producer_source_sha256\":\"%s\"",
          DM_NATIVE_RESOURCE_PRODUCER_SHA);
  fprintf(f, ",\"input_sha256\":");
  resource_hex(f, input_sha, 32);
  fprintf(f, ",\"declaration_sha256\":");
  resource_hex(f, h->declaration_sha256, 32);
  fprintf(f, ",\"texture_sha256\":");
  resource_hex(f, h->texture_sha256, 32);
  fprintf(f, ",\"descriptor_sha256\":");
  resource_hex(f, h->descriptor_sha256, 32);
  fprintf(f, ",\n\"layout_UUID\":");
  resource_hex(f, host.optimalTilingLayoutUUID, 16);
  fprintf(f, ",\"device_UUID\":");
  resource_hex(f, ids.deviceUUID, 16);
  fprintf(f, ",\"driver_UUID\":");
  resource_hex(f, ids.driverUUID, 16);
  fprintf(
      f,
      ",\"driverID\":%u,\"vendorID\":%u,\"deviceID\":%u,\"driverVersion\":%u,"
      "\n\"recipe\":{\"VkFormat\":%u,\"width\":%u,\"height\":%u,\"depth\":1,"
      "\"mipLevels\":%u,\"arrayLayers\":1,\"imageType\":%u,\"samples\":1,"
      "\"tiling\":%u,\"usage\":%u,\"flags\":0,\"sharingMode\":%u,"
      "\"queueFamilyIndexCount\":0,\"initialLayout\":%u,"
      "\"externalHandleTypes\":%u},\n\"physical_image_bytes\":%llu,"
      "\"alignment\":%llu,\"declared_mip_bytes\":%llu,\"producer_layout\":%u,"
      "\"producer_family\":%u,\"mips\":[",
      native_driver.driverID, properties.vendorID, properties.deviceID,
      properties.driverVersion, h->vk_format, h->width, h->height, h->levels,
      ci.imageType, ci.tiling, ci.usage, ci.sharingMode, ci.initialLayout,
      native_external.handleTypes, (unsigned long long)i.requirements.size,
      (unsigned long long)i.requirements.alignment, (unsigned long long)opaque,
      VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_FOREIGN_EXT);
  for (uint32_t n = 0; n < h->levels; n++) {
    native_slice s = slices[n];
    fprintf(f,
            "%s{\"level\":%u,\"width\":%u,\"height\":%u,\"offset\":%llu,"
            "\"size\":%llu,\"rowPitch\":%llu,\"arrayPitch\":%llu,"
            "\"depthPitch\":%llu,\"linear_ASTC_sha256\":",
            n ? "," : "", n, h->mips[n].width, h->mips[n].height,
            (unsigned long long)s.offset, (unsigned long long)s.size,
            (unsigned long long)s.row_pitch, (unsigned long long)s.array_pitch,
            (unsigned long long)s.depth_pitch);
    resource_hex(f, h->mips[n].sha256, 32);
    fputc('}', f);
  }
  fprintf(
      f, "],\n\"opaque_padding_zero_initialized_via_driver_MEMCPY\":true,"
         "\"original_driver_roundtrip_compressed_blocks_exact\":true,\"GPU_"
         "sampling_executed\":false,\"game_modules_executed\":false,\"game_"
         "readonly_authority\":false,\"new_codec_or_vendor_swizzle\":false}\n");
  CHECK(!fflush(f));
  CHECK(!fsync(fd));
  CHECK(!fclose(f));
  dm_resource_native wire;
  resource_native_header(&wire, &r, &i, slices, payload_sha);
  size_t metadata_bytes;
  uint8_t *metadata =
      resource_read(directory, "native-resource.json", &metadata_bytes);
  native_sha(metadata, metadata_bytes, wire.metadata_sha256);
  free(metadata);
  native_sha(&wire, sizeof wire, wire.digest);
  resource_write(directory, "native-resource-layout.bin", &wire, sizeof wire);
  printf("{\"native_resource_bake_complete\":true,\"format\":%u,\"width\":%u,"
         "\"height\":%u,\"levels\":%u,\"physical_image_bytes\":%llu,\"GPU_"
         "sampling_executed\":false,\"game_readonly_authority\":false}\n",
         h->vk_format, h->width, h->height, h->levels,
         (unsigned long long)i.requirements.size);
  free(payload);
  destroy_image(&i);
  resource_close(&r);
  return wire;
}
