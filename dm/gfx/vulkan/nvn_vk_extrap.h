// 原运行时外推的 Vulkan 图像/绘制边界；不接管客体逻辑。
// Vulkan image/draw boundary of original runtime extrapolation; no guest logic.
#pragma once
#include "nvn_vk_internal.h"
typedef struct {
  VkImage image;
  VkDeviceMemory memory; // 仅历史副本持有内存。 | Only historical copies own memory.
  VkImageView view, stencil, attachment;
  VkFormat format;
  VkExtent3D extent;
  VkImageAspectFlags aspect;
} vk_rt_image;
int vk_rt_texture(const tex_t *texture,vk_rt_image *out);
vk_rt_image *vk_rt_copy(const vk_rt_image *source,vk_rt_image *previous);
void vk_rt_release(vk_rt_image *history); // 提交内延迟释放。 | Deferred within a submit.
void vk_rt_shift(const vk_rt_image *source,const vk_rt_image *destination,int dx,int dy);
int vk_extrap_setup(VkFormat color,VkFormat depth);
int vk_extrap_encode(const vk_rt_image *hdr,const vk_rt_image *depth,
    const vk_rt_image *previous_hdr,const vk_rt_image *previous_depth,
    const float previous[16],const float current[16],uint32_t unit_bits,uint32_t ui_bits);
void vk_extrap_shift(const float previous[16],const float current[16],double width,double height,int *dx,int *dy);
int vk_runtime_op(uint32_t op,const void *payload,int window,int frozen);
void vk_runtime_force_full(int frames);
void vk_runtime_apply_stencil(VkPipelineDepthStencilStateCreateInfo *state);
typedef struct { int enabled,disabled,state; uint64_t full,extrapolated,encoded,missed,jumps,camera_reads,camera_waits; } vk_runtime_stats;
void vk_runtime_snapshot(vk_runtime_stats *out);
void vk_runtime_tick(void);
int vk_runtime_preserves_ops(void);
int vk_runtime_stencil_override_active(void);
