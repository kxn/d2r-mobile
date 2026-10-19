// Recording-owner only. Tokens borrow physical storage pinned by last_use;
// generation is checked before looking up an owner after any callback.
#pragma once
#include "nvn_vk_shader.h"
typedef struct {
  uint64_t generation,serial,recording,stamp;
  VkImage image;
  VkImageUsageFlags usage;
  vk_attachment attachment;
  int local;
} vk_pixel_local_image;
int vk_texture_pixel_local_prepare(const tex_t *texture,int local,vk_pixel_local_image *out);
int vk_texture_pixel_local_valid(const vk_pixel_local_image *token);
// Caller has established a COLOR_WRITE -> INPUT_READ subpass dependency.
// Updates only mip0/layer0 to match that real implicit transition.
int vk_texture_pixel_local_enter(const vk_pixel_local_image *token);
int vk_pixel_local_target_boundary(const void *op);
int vk_pixel_local_replay(const uint8_t *op);

int vk_counter_counting(void);

// Functional work counts, published at existing physical submission end.
void vk_pixel_local_stats(uint64_t *planned,uint64_t *draws,uint64_t *aborts);
