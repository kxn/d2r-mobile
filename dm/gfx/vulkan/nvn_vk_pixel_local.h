// DPL1 immutable offline proof/module ABI. Presence never authorizes a pass:
// the renderer proves actual geometry/samplers/views/dependencies before use.
#pragma once
#include <stdint.h>
#include "../../third_party/volk/volk.h"
struct vk_shader;
#define VK_PIXEL_LOCAL_MAX_INPUTS 8
#define VK_PIXEL_LOCAL_MAX_UV 16
// Runtime-only binding kind; never emitted in original NVN .meta reflection.
#define VK_BINDING_PIXEL_LOCAL 11
typedef struct { uint32_t set,binding,offset; } vk_pixel_local_uniform;
typedef struct {
  uint32_t location,attribute,mode; // 0 direct; 1 exact float32 fma
  vk_pixel_local_uniform scale[2],bias[2];
} vk_pixel_local_uv;
typedef struct {
  uint32_t binding,input_index,lod_mask; // bit0 implicit requires effective LOD0; bit1 explicit0
} vk_pixel_local_binding;
typedef struct vk_pixel_local_proof {
  struct vk_shader *source_shader; // exact source descriptor representation
  struct vk_shader *shader; // independent input descriptor schema; VS retains original
  VkShaderModule module;   // exact converted actual module (VS retains original)
  uint8_t source_sha256[32],variant_sha256[32];
  uint32_t set,count,uv_location,position_attribute,uv_count;
  vk_pixel_local_binding bindings[VK_PIXEL_LOCAL_MAX_INPUTS];
  vk_pixel_local_uv uv[VK_PIXEL_LOCAL_MAX_UV];
} vk_pixel_local_proof;
// Default OFF; startup environment DM_VULKAN_PIXEL_LOCAL=1, sampled once.
int vk_pixel_local_enabled(void);
// Recording owner only. Missing/mismatched/unproven artifacts return NULL.
// Each actual base/spec/pred/tssbo module has its own source-bound proof.
const vk_pixel_local_proof *vk_shader_pixel_local(struct vk_shader *shader,VkShaderModule actual_module);
// See nvn_vk_shader.h for descriptor binding API (its offsets type is defined there).
