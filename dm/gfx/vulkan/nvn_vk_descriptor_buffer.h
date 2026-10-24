#pragma once
#include "nvn_vk_uab.h"
// Device-lifetime immutable reflected layout, no resource or content authority.
typedef struct vk_descriptor_buffer_schema {
  const vk_shader *owner;
  vk_uab_schema schema;
  VkDescriptorSetLayout layout;
  VkDeviceSize bytes,stride,offsets[128];
  size_t sizes[128];
} vk_descriptor_buffer_schema;
const vk_descriptor_buffer_schema *vk_descriptor_buffer_schema_get(vk_shader *);
typedef struct {VkDeviceAddress address;VkDeviceSize offset;} vk_descriptor_buffer_binding;
