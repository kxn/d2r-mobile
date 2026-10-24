// 通用 Vulkan 分块分配；资源的 GPU 完成与退役时序由原后端保持。
// Generic Vulkan suballocation. Existing GPU completion/retirement ownership
// remains responsible for when each resource may be freed.
#pragma once
#include "nvn_vk_loader.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
VkResult vk_memory_init(VkInstance instance, VkPhysicalDevice physical, VkDevice device,int device_address);
VkResult vk_memory_buffer(VkBuffer buffer, VkDeviceMemory *memory, uint8_t **map, void **allocation);
VkResult vk_memory_buffer_readback(VkBuffer buffer,VkDeviceMemory *memory,uint8_t **map,void **allocation);
VkMemoryPropertyFlags vk_memory_properties(void *allocation);
VkResult vk_memory_invalidate(void *allocation);
// Bounded completion batches retain VMA's allocation/atom-range handling.
enum { VK_MEMORY_INVALIDATE_BATCH = 16 };
VkResult vk_memory_invalidate_batch(void *const *allocations, uint32_t count);
VkResult vk_memory_image(VkImage image, VkDeviceMemory *memory, void **allocation);
void vk_memory_free(void *allocation);
uint64_t vk_memory_allocation_bytes(void *allocation);
void vk_memory_stats(uint32_t *blocks, uint32_t *allocations, uint64_t *bytes);
char *vk_memory_build_stats_string(void);
void vk_memory_free_stats_string(char *data);
#ifdef __cplusplus
}
#endif
