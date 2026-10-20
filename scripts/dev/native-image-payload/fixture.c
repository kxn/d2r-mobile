// Owned API fixture; optional explicit PanVK native producer contract.
// No vendor swizzle implementation, game modules or profiling/timing tools.
#include "mbedtls/sha256.h"
#include "native_sample_spv.h"
#include "volk.h"
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned checks, errors;
#define CHECK(c)                                                               \
  do {                                                                         \
    checks++;                                                                  \
    if (!(c)) {                                                                \
      fprintf(stderr, "CHECK line%d: %s\n", __LINE__, #c);                     \
      exit(2);                                                                 \
    }                                                                          \
  } while (0)
#define VK(c) CHECK((c) == VK_SUCCESS)
enum { LEVELS = 4, LAYERS = 1, REGIONS = LEVELS * LAYERS, MAX_FILE = 16 << 20 };
static VkInstance instance;
static VkPhysicalDevice physical;
static VkDevice device;
static VkQueue queue;
static uint32_t family;
static VkPhysicalDeviceProperties properties;
static VkPhysicalDeviceIDProperties ids;
static VkPhysicalDeviceMemoryProperties memory;
static VkPhysicalDeviceHostImageCopyPropertiesEXT host;
static VkCommandPool commands;
static VkPipeline pipeline;
static VkPipelineLayout pipeline_layout;
static VkDescriptorSetLayout descriptor_layout;
static VkDebugUtilsMessengerEXT messenger;
static int bc, astc;
static int native_mode;
static PFN_vkGetInstanceProcAddr fixture_get_proc;
static int fixture_validation = -1;
#ifdef DM_NATIVE_IMPORTED_IMAGE
static VkPhysicalDeviceDriverProperties native_driver;
static VkPhysicalDeviceExternalMemoryHostPropertiesEXT native_host;
static const VkExternalMemoryImageCreateInfo native_external = {
    .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
    .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
#endif
static const VkImageUsageFlags usage =
    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT;
static VKAPI_ATTR VkBool32 VKAPI_CALL
debug(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
      VkDebugUtilsMessageTypeFlagsEXT type,
      const VkDebugUtilsMessengerCallbackDataEXT *data, void *user) {
  (void)type;
  (void)user;
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    errors++;
    fprintf(stderr, "VVL %s\n", data->pMessage);
  }
  return VK_FALSE;
}
static uint32_t mip(uint32_t v, uint32_t n) {
  v >>= n;
  return v ? v : 1;
}
static uint32_t pick_type(uint32_t bits, VkMemoryPropertyFlags required) {
  for (uint32_t n = 0; n < memory.memoryTypeCount; n++)
    if ((bits & (1u << n)) &&
        (memory.memoryTypes[n].propertyFlags & required) == required)
      return n;
  CHECK(0);
  return 0;
}
static int has_extension(VkExtensionProperties *p, uint32_t n,
                         const char *name) {
  for (uint32_t k = 0; k < n; k++)
    if (!strcmp(p[k].extensionName, name))
      return 1;
  return 0;
}
static int init(void) {
  if (fixture_get_proc)
    volkInitializeCustom(fixture_get_proc);
  else
    VK(volkInitialize());
  VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                           .pApplicationName = "owned-native-image-payload",
                           .apiVersion = VK_API_VERSION_1_1};
  const char *layers[] = {"VK_LAYER_KHRONOS_validation"},
             *exts[] = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
                        VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME};
  VkValidationFeatureEnableEXT sync =
      VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
  VkValidationFeaturesEXT validation = {
      .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
      .enabledValidationFeatureCount = 1,
      .pEnabledValidationFeatures = &sync};
  VkDebugUtilsMessengerCreateInfoEXT debug_info = {
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
      .pfnUserCallback = debug};
  validation.pNext = &debug_info;
  VkInstanceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                             .pApplicationInfo = &app};
  if (fixture_validation >= 0 ? fixture_validation
                              : !getenv("DM_NATIVE_NO_VALIDATION")) {
    ci.pNext = &validation;
    ci.enabledLayerCount = 1;
    ci.ppEnabledLayerNames = layers;
    ci.enabledExtensionCount = 2;
    ci.ppEnabledExtensionNames = exts;
  }
  VK(vkCreateInstance(&ci, NULL, &instance));
  volkLoadInstance(instance);
  if (ci.enabledLayerCount) {
    VK(vkCreateDebugUtilsMessengerEXT(instance, &debug_info, NULL, &messenger));
  }
  uint32_t count = 0;
  VK(vkEnumeratePhysicalDevices(instance, &count, NULL));
  CHECK(count);
  VkPhysicalDevice *devices = calloc(count, sizeof *devices);
  CHECK(devices);
  VK(vkEnumeratePhysicalDevices(instance, &count, devices));
  physical = devices[0];
  free(devices);
  uint32_t ext_count = 0;
  VK(vkEnumerateDeviceExtensionProperties(physical, NULL, &ext_count, NULL));
  VkExtensionProperties *available = calloc(ext_count, sizeof *available);
  CHECK(available);
  VK(vkEnumerateDeviceExtensionProperties(physical, NULL, &ext_count,
                                          available));
  const char *wanted[] = {VK_EXT_HOST_IMAGE_COPY_EXTENSION_NAME,
                          VK_KHR_COPY_COMMANDS_2_EXTENSION_NAME,
                          VK_KHR_FORMAT_FEATURE_FLAGS_2_EXTENSION_NAME
#ifdef DM_NATIVE_IMPORTED_IMAGE
                          ,
                          VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME,
                          VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME,
                          VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME
#endif
  };
  unsigned wanted_count = native_mode ? 6 : 3;
  for (unsigned n = 0; n < wanted_count; n++)
    if (!has_extension(available, ext_count, wanted[n])) {
      printf("{\"result\":\"unsupported\",\"reason\":\"%s\"}\n", wanted[n]);
      free(available);
      return 0;
    }
  free(available);
  VkPhysicalDeviceHostImageCopyFeaturesEXT feature = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES_EXT};
  VkPhysicalDeviceFeatures2 features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &feature};
  vkGetPhysicalDeviceFeatures2(physical, &features);
  if (!feature.hostImageCopy) {
    puts("{\"result\":\"unsupported\",\"reason\":\"hostImageCopy=false\"}");
    return 0;
  }
  bc = features.features.textureCompressionBC;
  astc = features.features.textureCompressionASTC_LDR;
  ids = (VkPhysicalDeviceIDProperties){
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
#ifdef DM_NATIVE_IMPORTED_IMAGE
  if (native_mode) {
    native_host = (VkPhysicalDeviceExternalMemoryHostPropertiesEXT){
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT};
    native_driver = (VkPhysicalDeviceDriverProperties){
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES,
        .pNext = &native_host};
    ids.pNext = &native_driver;
  }
#endif
  host = (VkPhysicalDeviceHostImageCopyPropertiesEXT){
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_PROPERTIES_EXT,
      .pNext = &ids};
  VkPhysicalDeviceProperties2 p = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &host};
  vkGetPhysicalDeviceProperties2(physical, &p);
  properties = p.properties;
#ifdef DM_NATIVE_IMPORTED_IMAGE
  if (native_mode && native_driver.driverID != VK_DRIVER_ID_MESA_PANVK) {
    puts("{\"result\":\"unsupported\",\"reason\":\"DM native image producer v1 "
         "requires exact PanVK driver contract\"}");
    return 0;
  }
#endif
  CHECK(host.copySrcLayoutCount <= 64 && host.copyDstLayoutCount <= 64);
  VkImageLayout src[64], dst[64];
  host.pCopySrcLayouts = src;
  host.pCopyDstLayouts = dst;
  vkGetPhysicalDeviceProperties2(physical, &p);
  int src_general = 0, dst_general = 0;
  for (uint32_t n = 0; n < host.copySrcLayoutCount; n++)
    src_general |= src[n] == VK_IMAGE_LAYOUT_GENERAL;
  for (uint32_t n = 0; n < host.copyDstLayoutCount; n++)
    dst_general |= dst[n] == VK_IMAGE_LAYOUT_GENERAL;
  CHECK(src_general && dst_general);
  vkGetPhysicalDeviceMemoryProperties(physical, &memory);
  uint32_t qcount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &qcount, NULL);
  VkQueueFamilyProperties *q = calloc(qcount, sizeof *q);
  CHECK(q);
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &qcount, q);
  family = UINT32_MAX;
  for (uint32_t n = 0; n < qcount; n++)
    if (q[n].queueFlags & VK_QUEUE_COMPUTE_BIT) {
      family = n;
      break;
    }
  free(q);
  CHECK(family != UINT32_MAX);
  float priority = 1;
  VkDeviceQueueCreateInfo qc = {.sType =
                                    VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                .queueFamilyIndex = family,
                                .queueCount = 1,
                                .pQueuePriorities = &priority};
  VkPhysicalDeviceFeatures enabled = {.textureCompressionBC = bc,
                                      .textureCompressionASTC_LDR = astc};
  VkDeviceCreateInfo dc = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                           .pNext = &feature,
                           .queueCreateInfoCount = 1,
                           .pQueueCreateInfos = &qc,
                           .enabledExtensionCount = wanted_count,
                           .ppEnabledExtensionNames = wanted,
                           .pEnabledFeatures = &enabled};
  VK(vkCreateDevice(physical, &dc, NULL, &device));
  volkLoadDevice(device);
  vkGetDeviceQueue(device, family, 0, &queue);
  CHECK(vkCopyMemoryToImageEXT && vkCopyImageToMemoryEXT &&
        vkTransitionImageLayoutEXT && vkGetImageSubresourceLayout2EXT);
  VkCommandPoolCreateInfo pc = {.sType =
                                    VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                .queueFamilyIndex = family};
  VK(vkCreateCommandPool(device, &pc, NULL, &commands));
  printf("{\"device\":\"%s\",\"hostImageCopy\":true,\"validation\":%s,\"is_"
         "zero_copy\":false}\n",
         properties.deviceName, ci.enabledLayerCount ? "true" : "false");
  return 1;
}
typedef struct {
  VkImage image;
  VkDeviceMemory allocation;
  VkMemoryRequirements requirements;
} image;
static VkImageCreateInfo recipe(VkFormat format) {
  return (VkImageCreateInfo){.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
#ifdef DM_NATIVE_IMPORTED_IMAGE
                             .pNext = native_mode ? &native_external : NULL,
#endif
                             .imageType = VK_IMAGE_TYPE_2D,
                             .format = format,
                             .extent = {13, 9, 1},
                             .mipLevels = LEVELS,
                             .arrayLayers = LAYERS,
                             .samples = VK_SAMPLE_COUNT_1_BIT,
                             .tiling = VK_IMAGE_TILING_OPTIMAL,
                             .usage = usage,
                             .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
}
static int supported(VkFormat format) {
  if ((format == VK_FORMAT_BC3_UNORM_BLOCK && !bc) ||
      ((format == VK_FORMAT_ASTC_4x4_UNORM_BLOCK ||
        format == VK_FORMAT_ASTC_6x6_UNORM_BLOCK ||
        format == VK_FORMAT_ASTC_4x4_SRGB_BLOCK ||
        format == VK_FORMAT_ASTC_6x6_SRGB_BLOCK) &&
       !astc))
    return 0;
  VkFormatProperties3 p3 = {.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3};
  VkFormatProperties2 p2 = {.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
                            .pNext = &p3};
  vkGetPhysicalDeviceFormatProperties2(physical, format, &p2);
  VkFormatFeatureFlags2 needed =
      VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT |
      VK_FORMAT_FEATURE_2_HOST_IMAGE_TRANSFER_BIT_EXT;
  if ((p3.optimalTilingFeatures & needed) != needed)
    return 0;
  VkPhysicalDeviceImageFormatInfo2 i = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
      .format = format,
      .type = VK_IMAGE_TYPE_2D,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = usage};
#ifdef DM_NATIVE_IMPORTED_IMAGE
  VkPhysicalDeviceExternalImageFormatInfo external = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
      .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
  VkExternalImageFormatProperties external_properties = {
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
  if (native_mode)
    i.pNext = &external;
#endif
  VkHostImageCopyDevicePerformanceQueryEXT performance = {
      .sType = VK_STRUCTURE_TYPE_HOST_IMAGE_COPY_DEVICE_PERFORMANCE_QUERY_EXT};
  VkImageFormatProperties2 out = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
      .pNext = &performance};
#ifdef DM_NATIVE_IMPORTED_IMAGE
  if (native_mode)
    performance.pNext = &external_properties;
#endif
  VkResult result =
      vkGetPhysicalDeviceImageFormatProperties2(physical, &i, &out);
  if (result != VK_SUCCESS)
    return 0;
#ifdef DM_NATIVE_IMPORTED_IMAGE
  if (native_mode) {
    VkExternalMemoryProperties *e =
        &external_properties.externalMemoryProperties;
    CHECK(e->externalMemoryFeatures ==
          VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT);
    CHECK(e->compatibleHandleTypes ==
          VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT);
    CHECK(!e->exportFromImportedHandleTypes &&
          out.imageFormatProperties.maxArrayLayers == 1 &&
          out.imageFormatProperties.maxMipLevels >= LEVELS);
  }
#endif
  printf("{\"format\":%u,\"host_transfer\":true,\"optimal_device_access\":%s,"
         "\"identical_memory_layout\":%s}\n",
         format, performance.optimalDeviceAccess ? "true" : "false",
         performance.identicalMemoryLayout ? "true" : "false");
  return 1;
}
static image create_image(VkFormat format) {
  image out = {0};
  VkImageCreateInfo ci = recipe(format);
  VK(vkCreateImage(device, &ci, NULL, &out.image));
  vkGetImageMemoryRequirements(device, out.image, &out.requirements);
  VkMemoryAllocateInfo a = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                            .allocationSize = out.requirements.size,
                            .memoryTypeIndex =
                                pick_type(out.requirements.memoryTypeBits, 0)};
  VK(vkAllocateMemory(device, &a, NULL, &out.allocation));
  VK(vkBindImageMemory(device, out.image, out.allocation, 0));
  return out;
}
static void destroy_image(image *i) {
  vkDestroyImage(device, i->image, NULL);
  vkFreeMemory(device, i->allocation, NULL);
  memset(i, 0, sizeof *i);
}
static void transition(image *i, VkImageLayout old, VkImageLayout next) {
  VkHostImageLayoutTransitionInfoEXT t = {
      .sType = VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO_EXT,
      .image = i->image,
      .oldLayout = old,
      .newLayout = next,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, LEVELS, 0, LAYERS}};
  VK(vkTransitionImageLayoutEXT(device, 1, &t));
}
static unsigned pattern_version;
static void expected(VkFormat format, uint32_t level, uint32_t layer,
                     uint32_t x, uint32_t y, uint8_t rgba[4]) {
  if (format == VK_FORMAT_R8G8B8A8_UNORM) {
    rgba[0] = (x * 17 + y * 3 + layer * 31 + level * 19) & 255;
    rgba[1] = (x * 5 + y * 23 + level * 41) & 255;
    rgba[2] = (x * 13 + y * 7 + layer * 79) & 255;
    rgba[3] = 255;
    return;
  }
  uint32_t block = (format == VK_FORMAT_ASTC_6x6_UNORM_BLOCK ||
                    format == VK_FORMAT_ASTC_6x6_SRGB_BLOCK)
                       ? 6
                       : 4,
           n = (x / block + y / block + layer + level + pattern_version) % 3;
  rgba[0] = n == 0 ? 255 : 0;
  rgba[1] = n == 1 ? 255 : 0;
  rgba[2] = n == 2 ? 255 : 0;
  rgba[3] = 255;
}
static uint8_t *linear_source(VkFormat format, uint32_t level, uint32_t layer,
                              size_t *size) {
  uint32_t w = mip(13, level), h = mip(9, level),
           block = (format == VK_FORMAT_ASTC_6x6_UNORM_BLOCK ||
                    format == VK_FORMAT_ASTC_6x6_SRGB_BLOCK)
                       ? 6
                       : 4;
  if (format == VK_FORMAT_R8G8B8A8_UNORM) {
    *size = w * h * 4;
    uint8_t *out = malloc(*size);
    CHECK(out);
    for (uint32_t y = 0; y < h; y++)
      for (uint32_t x = 0; x < w; x++)
        expected(format, level, layer, x, y, out + (y * w + x) * 4);
    return out;
  }
  uint32_t cols = (w + block - 1) / block, rows = (h + block - 1) / block;
  *size = cols * rows * 16;
  uint8_t *out = calloc(1, *size);
  CHECK(out);
  for (uint32_t y = 0; y < rows; y++)
    for (uint32_t x = 0; x < cols; x++) {
      uint8_t rgba[4], *b = out + (y * cols + x) * 16;
      expected(format, level, layer, x * block, y * block, rgba);
      if (format == VK_FORMAT_BC3_UNORM_BLOCK) {
        b[0] = 255;
        uint16_t color = rgba[0] ? 0xf800 : rgba[1] ? 0x07e0 : 0x001f;
        memcpy(b + 8, &color, 2);
        memcpy(b + 10, &color, 2);
      } else { // Same owned U16 void-extent vector as original astc_metadata
               // test.
        const uint8_t constant[8] = {0xfc, 0xfd, 0xff, 0xff,
                                     0xff, 0xff, 0xff, 0xff};
        memcpy(b, constant, 8);
        for (unsigned c = 0; c < 4; c++) {
          b[8 + 2 * c] = rgba[c];
          b[9 + 2 * c] = rgba[c];
        }
      }
    }
  return out;
}
typedef struct {
  uint32_t level, layer, width, height;
  uint64_t offset, size;
} part;
typedef struct {
  uint8_t magic[8];
  uint32_t version, header_bytes;
  uint64_t file_bytes;
  uint32_t format, width, height, depth, levels, layers, type, flags, usage,
      tiling, samples, sharing, queue_count, initial_layout,
      immutable_owned_fixture, part_count;
  uint32_t vendor, device_id, driver_version;
  uint8_t layout_uuid[16], device_uuid[16], driver_uuid[16];
  part parts[REGIONS];
  uint8_t digest[32];
} pack_header;
_Static_assert(sizeof(part) == 32, "fixed little-endian payload record");
static void header_init(pack_header *h, VkFormat format) {
  memset(h, 0, sizeof *h);
  memcpy(h->magic, "DMHIMG1", 7);
  h->version = 1;
  h->header_bytes = sizeof *h;
  VkImageCreateInfo c = recipe(format);
  h->format = format;
  h->width = c.extent.width;
  h->height = c.extent.height;
  h->depth = c.extent.depth;
  h->levels = c.mipLevels;
  h->layers = c.arrayLayers;
  h->type = c.imageType;
  h->flags = c.flags;
  h->usage = c.usage;
  h->tiling = c.tiling;
  h->samples = c.samples;
  h->sharing = c.sharingMode;
  h->queue_count = c.queueFamilyIndexCount;
  h->initial_layout = c.initialLayout;
  h->immutable_owned_fixture = 1;
  h->part_count = REGIONS;
  h->vendor = properties.vendorID;
  h->device_id = properties.deviceID;
  h->driver_version = properties.driverVersion;
  memcpy(h->layout_uuid, host.optimalTilingLayoutUUID, 16);
  memcpy(h->device_uuid, ids.deviceUUID, 16);
  memcpy(h->driver_uuid, ids.driverUUID, 16);
}
static void file_digest(const uint8_t *data, size_t bytes, uint8_t out[32]) {
  const size_t at = offsetof(pack_header, digest);
  uint8_t zero[32] = {0};
  mbedtls_sha256_context c;
  mbedtls_sha256_init(&c);
  mbedtls_sha256_starts(&c, 0);
  mbedtls_sha256_update(&c, data, at);
  mbedtls_sha256_update(&c, zero, 32);
  mbedtls_sha256_update(&c, data + at + 32, bytes - at - 32);
  mbedtls_sha256_finish(&c, out);
  mbedtls_sha256_free(&c);
}
static uint64_t opaque_size(image *i, uint32_t level, uint32_t layer) {
  VkSubresourceHostMemcpySizeEXT bytes = {
      .sType = VK_STRUCTURE_TYPE_SUBRESOURCE_HOST_MEMCPY_SIZE_EXT};
  VkSubresourceLayout2EXT layout = {
      .sType = VK_STRUCTURE_TYPE_SUBRESOURCE_LAYOUT_2_EXT, .pNext = &bytes};
  VkImageSubresource2EXT sub = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_SUBRESOURCE_2_EXT,
      .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, layer}};
  vkGetImageSubresourceLayout2EXT(device, i->image, &sub, &layout);
  CHECK(bytes.size && bytes.size < MAX_FILE);
  return bytes.size;
}
static void bake(const char *directory, VkFormat format) {
  image i = create_image(format);
  transition(&i, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
  pack_header h;
  header_init(&h, format);
  uint64_t total = (sizeof h + 63) & ~UINT64_C(63);
  uint64_t linear_bytes = 0, native_bytes = 0;
  uint64_t image_bytes = i.requirements.size;
  for (uint32_t layer = 0; layer < LAYERS; layer++)
    for (uint32_t level = 0; level < LEVELS; level++) {
      part *p = &h.parts[layer * LEVELS + level];
      *p = (part){level,         layer, mip(13, level),
                  mip(9, level), total, opaque_size(&i, level, layer)};
      total = (total + p->size + 63) & ~UINT64_C(63);
      size_t bytes;
      uint8_t *linear = linear_source(format, level, layer, &bytes);
      linear_bytes += bytes;
      native_bytes += p->size;
      VkMemoryToImageCopyEXT copy = {
          .sType = VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY_EXT,
          .pHostPointer = linear,
          .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, layer, 1},
          .imageExtent = {p->width, p->height, 1}};
      VkCopyMemoryToImageInfoEXT info = {
          .sType = VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INFO_EXT,
          .dstImage = i.image,
          .dstImageLayout = VK_IMAGE_LAYOUT_GENERAL,
          .regionCount = 1,
          .pRegions = &copy};
      VK(vkCopyMemoryToImageEXT(device, &info));
      free(linear);
    }
  CHECK(total < MAX_FILE);
  h.file_bytes = total;
  uint8_t *output = calloc(1, total);
  CHECK(output);
  memcpy(output, &h, sizeof h);
  for (unsigned n = 0; n < REGIONS; n++) {
    part *p = &h.parts[n];
    VkImageToMemoryCopyEXT copy = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_TO_MEMORY_COPY_EXT,
        .pHostPointer = output + p->offset,
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, p->level, p->layer, 1},
        .imageExtent = {p->width, p->height, 1}};
    VkCopyImageToMemoryInfoEXT info = {
        .sType = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_MEMORY_INFO_EXT,
        .flags = VK_HOST_IMAGE_COPY_MEMCPY_EXT,
        .srcImage = i.image,
        .srcImageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .regionCount = 1,
        .pRegions = &copy};
    VK(vkCopyImageToMemoryEXT(device, &info));
  }
  file_digest(output, total, ((pack_header *)output)->digest);
  char path[4096];
  CHECK(snprintf(path, sizeof path, "%s/native-%u.bin", directory, format) <
        (int)sizeof path);
  FILE *f = fopen(path, "wb");
  CHECK(f);
  CHECK(fwrite(output, 1, total, f) == total);
  CHECK(!fclose(f));
  free(output);
  destroy_image(&i);
  printf(
      "{\"baked_format\":%u,\"file_bytes\":%llu,\"linear_source_bytes\":%llu,"
      "\"declared_opaque_region_bytes\":%llu,\"VkMemoryRequirements_size\":%"
      "llu,"
      "\"opaque_copy_is_not_zero_copy\":true}\n",
      format, (unsigned long long)total, (unsigned long long)linear_bytes,
      (unsigned long long)native_bytes, (unsigned long long)image_bytes);
}
static int validate_pack(const uint8_t *data, size_t bytes, VkFormat format) {
  if (bytes < sizeof(pack_header) || bytes > MAX_FILE)
    return 0;
  const pack_header *h = (const void *)data;
  pack_header wanted;
  header_init(&wanted, format);
  if (memcmp(h->magic, wanted.magic, 8) || h->version != 1 ||
      h->header_bytes != sizeof *h || h->file_bytes != bytes)
    return 0;
  if (memcmp(&h->format, &wanted.format,
             offsetof(pack_header, parts) - offsetof(pack_header, format)))
    return 0;
  uint8_t digest[32];
  file_digest(data, bytes, digest);
  if (memcmp(h->digest, digest, 32))
    return 0;
  uint64_t previous = (sizeof *h + 63) & ~UINT64_C(63);
  for (unsigned n = 0; n < REGIONS; n++) {
    const part *p = &h->parts[n];
    if (p->level != n % LEVELS || p->layer != n / LEVELS ||
        p->width != mip(13, p->level) || p->height != mip(9, p->level) ||
        p->offset != previous || !p->size || p->offset > bytes ||
        p->size > bytes - p->offset)
      return 0;
    previous = (p->offset + p->size + 63) & ~UINT64_C(63);
  }
  return previous == bytes;
}
static void pipeline_create(void) {
  if (pipeline)
    return;
  VkDescriptorSetLayoutBinding bindings[] = {
      {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
       VK_SHADER_STAGE_COMPUTE_BIT, NULL},
      {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT,
       NULL}};
  VkDescriptorSetLayoutCreateInfo dl = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 2,
      .pBindings = bindings};
  VK(vkCreateDescriptorSetLayout(device, &dl, NULL, &descriptor_layout));
  VkPushConstantRange push = {VK_SHADER_STAGE_COMPUTE_BIT, 0, 16};
  VkPipelineLayoutCreateInfo pl = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &descriptor_layout,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &push};
  VK(vkCreatePipelineLayout(device, &pl, NULL, &pipeline_layout));
  VkShaderModuleCreateInfo sm = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = sizeof dm_native_sample_spv,
      .pCode = dm_native_sample_spv};
  VkShaderModule shader;
  VK(vkCreateShaderModule(device, &sm, NULL, &shader));
  VkComputePipelineCreateInfo cp = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = shader,
                .pName = "main"},
      .layout = pipeline_layout};
  VK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cp, NULL, &pipeline));
  vkDestroyShaderModule(device, shader, NULL);
}
static void sample_all(image *i, VkFormat format) {
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
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, LEVELS, 0, LAYERS}};
  VK(vkCreateImageView(device, &vi, NULL, &view));
  VkSamplerCreateInfo si = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter = VK_FILTER_NEAREST,
      .minFilter = VK_FILTER_NEAREST,
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .maxLod = LEVELS - 1};
  VK(vkCreateSampler(device, &si, NULL, &sampler));
  VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                           .size = 13 * 9 * 4,
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
  for (uint32_t layer = 0; layer < LAYERS; layer++)
    for (uint32_t level = 0; level < LEVELS; level++) {
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
      uint32_t push[] = {mip(13, level), mip(9, level), layer, level};
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
      for (uint32_t y = 0; y < push[1]; y++)
        for (uint32_t x = 0; x < push[0]; x++) {
          uint8_t rgba[4];
          expected(format, level, layer, x, y, rgba);
          CHECK(!memcmp((uint8_t *)mapped + (y * push[0] + x) * 4, rgba, 4));
        }
      for (uint32_t n = push[0] * push[1]; n < 13 * 9; n++)
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
static void restore(const char *directory, VkFormat format) {
  char path[4096];
  CHECK(snprintf(path, sizeof path, "%s/native-%u.bin", directory, format) <
        (int)sizeof path);
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  CHECK(fd >= 0);
  struct stat st;
  CHECK(!fstat(fd, &st) && st.st_size > 0 && st.st_size <= MAX_FILE);
  size_t size = st.st_size;
  const uint8_t *data = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  CHECK(data != MAP_FAILED);
  CHECK(!close(fd));
  CHECK(validate_pack(data, size, format));
  // Negative provenance/metadata tests act only on a finite owned copy. They
  // reject BEFORE API upload; no relax-to-linear or fake layout fallback.
  CHECK(!validate_pack(data, sizeof(pack_header) - 1, format));
  CHECK(!validate_pack(data, size - 1, format));
  uint8_t *bad = malloc(size);
  CHECK(bad);
  memcpy(bad, data, size);
  bad[((pack_header *)bad)->parts[0].offset] ^= 1;
  CHECK(!validate_pack(bad, size, format));
  memcpy(bad, data, size);
  ((pack_header *)bad)->layout_uuid[0] ^= 1;
  file_digest(bad, size, ((pack_header *)bad)->digest);
  CHECK(!validate_pack(bad, size, format));
  memcpy(bad, data, size);
  ((pack_header *)bad)->immutable_owned_fixture = 0;
  file_digest(bad, size, ((pack_header *)bad)->digest);
  CHECK(!validate_pack(bad, size, format));
  memcpy(bad, data, size);
  ((pack_header *)bad)->width++;
  file_digest(bad, size, ((pack_header *)bad)->digest);
  CHECK(!validate_pack(bad, size, format));
  free(bad);
  pack_header h;
  memcpy(&h, data, sizeof h);
  image i = create_image(format);
  // Validate the complete target recipe before the first upload.
  for (unsigned n = 0; n < REGIONS; n++)
    CHECK(opaque_size(&i, h.parts[n].level, h.parts[n].layer) ==
          h.parts[n].size);
  transition(&i, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
  uint64_t restored_bytes = 0;
  for (unsigned n = 0; n < REGIONS; n++) {
    part *p = &h.parts[n];
    restored_bytes += p->size;
    VkMemoryToImageCopyEXT copy = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY_EXT,
        .pHostPointer = data + p->offset,
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, p->level, p->layer, 1},
        .imageExtent = {p->width, p->height, 1}};
    VkCopyMemoryToImageInfoEXT info = {
        .sType = VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INFO_EXT,
        .flags = VK_HOST_IMAGE_COPY_MEMCPY_EXT,
        .dstImage = i.image,
        .dstImageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .regionCount = 1,
        .pRegions = &copy};
    VK(vkCopyMemoryToImageEXT(device, &info));
  }
  CHECK(!munmap((void *)data,
                size)); // File/CPU payload gone before first sample.
  transition(&i, VK_IMAGE_LAYOUT_GENERAL,
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  sample_all(&i, format);
  destroy_image(&i);
  printf("{\"restored_format\":%u,\"sampled_mips\":%u,\"sampled_layers\":%u,"
         "\"CPU_payload_unmapped_before_GPU_sample\":true,"
         "\"restore_declared_opaque_region_bytes\":%llu,\"checks\":%u}\n",
         format, LEVELS, LAYERS, (unsigned long long)restored_bytes, checks);
}
#ifdef DM_NATIVE_IMPORTED_IMAGE
#include "native_import.inc"
#include "resource_bake.inc"
#include "resource_import.inc"
#endif
static int fixture_main(int argc, char **argv) {
  uint16_t endian = 1;
  CHECK(*(uint8_t *)&endian == 1);
  CHECK(argc == 3 &&
        (!strcmp(argv[1], "--bake") || !strcmp(argv[1], "--restore")
#ifdef DM_NATIVE_IMPORTED_IMAGE
         || !strcmp(argv[1], "--native-bake") ||
         !strcmp(argv[1], "--native-import") ||
         !strcmp(argv[1], "--resource-bake") ||
         !strcmp(argv[1], "--resources-bake") ||
         !strcmp(argv[1], "--resources-import") ||
         !strcmp(argv[1], "--verify-resource-input")
#endif
             ));
#ifdef DM_NATIVE_IMPORTED_IMAGE
  native_mode = !strcmp(argv[1], "--native-bake") ||
                !strcmp(argv[1], "--native-import") ||
                !strcmp(argv[1], "--resource-bake") ||
                !strcmp(argv[1], "--resources-bake") ||
                !strcmp(argv[1], "--resources-import");
  if (!strcmp(argv[1], "--verify-resource-input")) {
    resource_input r = resource_open(argv[2]);
    resource_close(&r);
    printf("{\"test\":\"native-resource-input\",\"result\":\"passed\","
           "\"checks\":%u,\"GPU_executed\":false}\n",
           checks);
    return 0;
  }
#endif
  if (!init()) {
    if (messenger)
      vkDestroyDebugUtilsMessengerEXT(instance, messenger, NULL);
    if (instance)
      vkDestroyInstance(instance, NULL);
    return 77;
  }
  const VkFormat formats[] = {VK_FORMAT_R8G8B8A8_UNORM,
                              VK_FORMAT_BC3_UNORM_BLOCK,
                              VK_FORMAT_ASTC_4x4_UNORM_BLOCK,
                              VK_FORMAT_ASTC_6x6_UNORM_BLOCK
#ifdef DM_NATIVE_IMPORTED_IMAGE
                              ,
                              VK_FORMAT_ASTC_4x4_SRGB_BLOCK,
                              VK_FORMAT_ASTC_6x6_SRGB_BLOCK
#endif
  };
  unsigned used = 0;
#ifdef DM_NATIVE_IMPORTED_IMAGE
  if (!strcmp(argv[1], "--resources-bake") ||
      !strcmp(argv[1], "--resources-import")) {
    resource_pair(argv[2], !strcmp(argv[1], "--resources-bake"));
    used = 2;
  } else if (!strcmp(argv[1], "--resource-bake")) {
    resource_bake(argv[2]);
    used = 1;
  } else
#endif
    for (unsigned n = 0; n < sizeof formats / sizeof *formats; n++) {
#ifdef DM_NATIVE_IMPORTED_IMAGE
      if (native_mode && formats[n] != VK_FORMAT_ASTC_4x4_UNORM_BLOCK &&
          formats[n] != VK_FORMAT_ASTC_6x6_UNORM_BLOCK &&
          formats[n] != VK_FORMAT_ASTC_4x4_SRGB_BLOCK &&
          formats[n] != VK_FORMAT_ASTC_6x6_SRGB_BLOCK)
        continue;
#endif
      if (!supported(formats[n])) {
        printf("{\"format\":%u,\"result\":\"unsupported_format\"}\n",
               formats[n]);
        continue;
      }
#ifdef DM_NATIVE_IMPORTED_IMAGE
      if (native_mode) {
        if (!strcmp(argv[1], "--native-bake"))
          native_bake(argv[2], formats[n]);
        else
          native_import(argv[2], formats[n]);
      } else
#endif
          if (!strcmp(argv[1], "--bake"))
        bake(argv[2], formats[n]);
      else
        restore(argv[2], formats[n]);
      used++;
    }
  CHECK((used || native_mode) && !errors);
  VK(vkDeviceWaitIdle(device));
  if (pipeline)
    vkDestroyPipeline(device, pipeline, NULL);
  if (pipeline_layout)
    vkDestroyPipelineLayout(device, pipeline_layout, NULL);
  if (descriptor_layout)
    vkDestroyDescriptorSetLayout(device, descriptor_layout, NULL);
  vkDestroyCommandPool(device, commands, NULL);
  vkDestroyDevice(device, NULL);
  if (messenger)
    vkDestroyDebugUtilsMessengerEXT(instance, messenger, NULL);
  vkDestroyInstance(instance, NULL);
  CHECK(!errors);
  printf(
      "{\"test\":\"native-host-image-payload\",\"mode\":\"%s\",\"formats\":%u,"
      "\"checks\":%u,\"result\":\"%s\",\"game_modules_executed\":false}\n",
      argv[1], used, checks, used ? "passed" : "unsupported");
  return used ? 0 : 77;
}

#ifndef DM_NATIVE_IMAGE_EMBED
int main(int argc, char **argv) { return fixture_main(argc, argv); }
#else
int dm_native_image_fixture_run(const char *mode, const char *directory,
                                PFN_vkGetInstanceProcAddr proc,
                                int validation) {
  // One call per cold owned-fixture process. Never share with a running game.
  fixture_get_proc = proc;
  fixture_validation = !!validation;
  char *argv[] = {"owned-native-image-fixture", (char *)mode,
                  (char *)directory};
  return fixture_main(3, argv);
}
#endif
