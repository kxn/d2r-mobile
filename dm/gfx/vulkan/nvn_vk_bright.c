// 对应原 nvn_mtl.m bright_lut；只重写原调色表，不改游戏 binary 或 UI。
// Vulkan counterpart of original bright_lut: rewrite its LUT, not game code/UI.
#include "nvn_vk_shader.h"
#include "nvn_vk_bright_spv.h"

__attribute__((weak)) float dm_bright_k(void) { return 1; }
static VkDescriptorSetLayout descriptors;
static VkPipelineLayout pipeline_layout;
static VkPipeline pipeline;
static VkSampler linear_sampler;
typedef struct {
  VkImage image;
  VkDeviceMemory memory;
  VkImageView view;
  VkFormat format;
  VkExtent3D extent;
} lut_copy;
static lut_copy *source;
static void copy_destroy(void *context) {
  lut_copy *copy = context;
  vkDestroyImageView(V.device, copy->view, NULL);
  vkDestroyImage(V.device, copy->image, NULL);
  vkFreeMemory(V.device, copy->memory, NULL);
  free(copy);
}
static void pipeline_create(void) {
  VkDescriptorSetLayoutBinding bindings[] = {
    {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
    {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}};
  VkDescriptorSetLayoutCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 2, .pBindings = bindings};
  VK_CHECK(vkCreateDescriptorSetLayout(V.device, &di, NULL, &descriptors));
  // Push constants preserve each dispatch's exposure without rotating mutable uniforms.
  // Push 常量逐派发保留曝光，不复用仍在 GPU 上读取的可变 uniform。
  VkPushConstantRange push = {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(float)};
  VkPipelineLayoutCreateInfo li = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1, .pSetLayouts = &descriptors,
      .pushConstantRangeCount = 1, .pPushConstantRanges = &push};
  VK_CHECK(vkCreatePipelineLayout(V.device, &li, NULL, &pipeline_layout));
  VkShaderModuleCreateInfo mi = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = sizeof dm_bright_spv, .pCode = dm_bright_spv};
  VkShaderModule module;
  VK_CHECK(vkCreateShaderModule(V.device, &mi, NULL, &module));
  VkComputePipelineCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = module, .pName = "main"},
      .layout = pipeline_layout};
  VK_CHECK(vkCreateComputePipelines(V.device, V.pipeline_cache, 1, &pi, NULL, &pipeline));
  vkDestroyShaderModule(V.device, module, NULL);
  VkSamplerCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter = VK_FILTER_LINEAR, .minFilter = VK_FILTER_LINEAR,
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
  VK_CHECK(vkCreateSampler(V.device, &si, NULL, &linear_sampler));
}
static void source_prepare(const vk_storage_image *lut) {
  if (source && source->format == lut->format &&
      !memcmp(&source->extent, &lut->extent, sizeof lut->extent)) return;
  if (source) vk_completion(copy_destroy, source);
  source = calloc(1, sizeof *source);
  if (!source) die("Vulkan LUT copy allocation");
  source->format = lut->format; source->extent = lut->extent;
  VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_3D, .format = lut->format, .extent = lut->extent,
      .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT};
  VK_CHECK(vkCreateImage(V.device, &ci, NULL, &source->image));
  VkMemoryRequirements mr;
  vkGetImageMemoryRequirements(V.device, source->image, &mr);
  VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mr.size,
      .memoryTypeIndex = vk_memory_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
  VK_CHECK(vkAllocateMemory(V.device, &ai, NULL, &source->memory));
  VK_CHECK(vkBindImageMemory(V.device, source->image, source->memory, 0));
  VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = source->image, .viewType = VK_IMAGE_VIEW_TYPE_3D, .format = lut->format,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
  VK_CHECK(vkCreateImageView(V.device, &vi, NULL, &source->view));
  VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_GENERAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = source->image, .subresourceRange = vi.subresourceRange,
      .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT};
  vkCmdPipelineBarrier(V.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       0, 0, NULL, 0, NULL, 1, &barrier);
  LOG("[vulkan] original bright_lut copy format=%u size=%ux%ux%u", lut->format,
      lut->extent.width, lut->extent.height, lut->extent.depth);
}
static int float_storage_format(VkFormat format) {
  // Vulkan 1.1 无格式 storage 写的浮点格式白名单；整数 LUT 不做 float 重解释。
  // Vulkan 1.1 formatless-write float formats; never reinterpret integer LUTs.
  switch (format) {
  case VK_FORMAT_R8G8B8A8_UNORM: case VK_FORMAT_R8G8B8A8_SNORM:
  case VK_FORMAT_R32_SFLOAT: case VK_FORMAT_R32G32_SFLOAT:
  case VK_FORMAT_R32G32B32A32_SFLOAT: case VK_FORMAT_R16G16_SFLOAT:
  case VK_FORMAT_R16G16B16A16_SFLOAT: case VK_FORMAT_B10G11R11_UFLOAT_PACK32: return 1;
  default: return 0;
  }
}
int vk_bright_apply(int id, float k) {
  if (!(k > 1.005f)) return 0;
  if (k > 4) k = 4;
  if (!V.cmd) die("Vulkan LUT enhancement outside submission");
  vk_storage_image lut;
  if (!V.features.shaderStorageImageWriteWithoutFormat ||
      !vk_texture_storage_volume(id, &lut) || !float_storage_format(lut.format)) return 0;
  VkFormatProperties properties;
  vk_format_properties(lut.format, &properties);
  if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) return 0;
  vk_texture_storage_access(&lut,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
  if (!pipeline) pipeline_create();
  source_prepare(&lut);
  // This private snapshot is reused across submissions. Only its previous
  // copy and compute readers precede the next overwrite; unrelated graphics
  // stages are not part of this dependency.
  VkImageMemoryBarrier snapshot={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .oldLayout=VK_IMAGE_LAYOUT_GENERAL,.newLayout=VK_IMAGE_LAYOUT_GENERAL,
      .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
      .image=source->image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},
      .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT,
      .dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&snapshot);
  VkImageCopy copy = {.srcSubresource = lut.subresource,
      .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}, .extent = lut.extent};
  vkCmdCopyImage(V.cmd, lut.image, VK_IMAGE_LAYOUT_GENERAL, source->image,
                 VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
  vk_texture_storage_access(&lut,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT);
  snapshot.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
  snapshot.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      0,0,NULL,0,NULL,1,&snapshot);
  VkDescriptorSet set = vk_descriptor_allocate(descriptors);
  VkDescriptorImageInfo images[] = {
    {linear_sampler, source->view, VK_IMAGE_LAYOUT_GENERAL},
    {VK_NULL_HANDLE, lut.view, VK_IMAGE_LAYOUT_GENERAL}};
  VkWriteDescriptorSet writes[] = {
    {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 0,
     .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = images},
    {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 1,
     .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .pImageInfo = images+1}};
  vkUpdateDescriptorSets(V.device, 2, writes, 0, NULL);
  unsigned region = vk_gpu_region_begin("compute", "upstream-bright", lut.extent.width, lut.extent.height);
  vkCmdBindPipeline(V.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
  vkCmdBindDescriptorSets(V.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1, &set, 0, NULL);
  vkCmdPushConstants(V.cmd, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof k, &k);
  vkCmdDispatch(V.cmd, (lut.extent.width+3)/4, (lut.extent.height+3)/4, (lut.extent.depth+3)/4);
  vk_texture_storage_written(&lut,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  vk_gpu_region_end(region);
  V.draws++;
  return 1;
}
void vk_bright_lut(vk_shader *shader) {
  if (strcmp(shader->key, "552f45c348a61b86")) return;
  float k = dm_bright_k();
  if (!(k > 1.005f)) return;
  int id = 0;
  for (unsigned i = 0; i < shader->nb && !id; i++)
    if (shader->bindings[i].kind == 4 && shader->bindings[i].reg >= 0 && shader->bindings[i].reg < 32)
      id = S.img[5][shader->bindings[i].reg] & 0xfffff;
  int applied = id && vk_bright_apply(id, k);
  static int logged, warned;
  if (applied && !logged++) LOG("[vulkan] original bright_lut applied id=%d k=%.3f", id, k > 4 ? 4 : k);
  if (!applied && !warned++) LOG("[vulkan] original bright_lut unavailable for texture %d; retaining original LUT", id);
}
