#pragma once
#include "nvn_vk_shader.h"

// Resolved Vulkan encoding values only: no guest addresses, resource lookup
// keys, or ownership authority. Immediate and pending consumers share the
// original recording's lifetimes; this payload grants no cross-frame ownership.
typedef struct {
  VkBuffer buffer;
  VkDeviceSize offset;
} vk_draw_vertex_emission;
typedef struct {
  VkCommandBuffer command;
  VkPipeline pipeline;
  VkPipelineLayout layout;
  vk_draw_vertex_emission vertices[16];
  uint16_t vertex_mask;
  uint8_t geometry_complete,index_bound;
  VkBuffer index_buffer;
  VkDeviceSize index_offset;
  VkIndexType index_type;
  // Stage order is retained for the original reference emission path.
  VkDescriptorSet stage_sets[6];
  uint8_t stage_present_mask,stage_set[6];
  vk_uniform_offsets uniform_offsets[6];
  int bias_enabled;
  uint32_t bias_words[32];
  VkViewport viewport;
  VkRect2D scissor;
  float depth_bias[3];
  uint32_t count,instances,first,first_instance;
  int32_t vertex_offset;
  uint8_t indexed,state_reference,retained_parameters;
} vk_draw_emission;
