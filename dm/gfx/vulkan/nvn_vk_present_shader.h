// Optional, format-preserving host presentation. No guest shader or game key.
#pragma once
#include "nvn_vk_loader.h"
typedef struct {
  VkImage image;
  VkImageView view;
  VkImageLayout layout;
  VkFormat format;
  VkExtent2D extent;
} vk_present_sample;
typedef struct {
  VkImage image;
  VkImageView view;
  VkFramebuffer framebuffer;
  VkExtent2D extent;
} vk_present_shader_target;
int vk_present_shader_eligible(VkFormat source,VkSampleCountFlagBits samples,
    VkExtent3D source_extent,VkImageUsageFlags source_usage,VkFormat destination,
    VkExtent2D destination_extent,VkImageUsageFlags destination_usage,int full_source);
int vk_texture_present_sample(const void *texture,VkFormat destination,
    VkExtent2D destination_extent,vk_present_sample *sample);
void vk_present_shader_target_create(vk_present_shader_target *target,VkImage image,VkExtent2D extent);
// The caller must first wait for every GPU use of the target to finish.
void vk_present_shader_target_destroy(vk_present_shader_target *target);
void vk_present_shader_render(const vk_present_sample *source,const vk_present_shader_target *target);
