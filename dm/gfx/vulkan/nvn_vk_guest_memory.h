// An allocation made before guest pointers are published. This is not an
// import/remap of existing anonymous pages. Callers serialize owner changes,
// retain the owner for the guest lifetime and retire GPU work before release.
#pragma once
#include "nvn_vk_internal.h"
typedef struct {
  VkBuffer reservation;
  VkDeviceMemory memory;
  uint8_t *map, *base;
  VkDeviceSize allocation_size, capacity, offset;
  VkMemoryPropertyFlags properties;
  uint32_t memory_type, views;
} vk_guest_memory;
typedef struct {
  VkBuffer handle;
  uint8_t *map;
  VkDeviceSize size, offset;
  vk_guest_memory *owner;
} vk_guest_memory_buffer;
// Contents are initially unspecified: opening guest pages must initialize
// them before first CPU access. CACHED is mandatory for a CPU game heap;
// coherent=0 additionally requires explicit cache maintenance and ownership.
VkResult vk_guest_memory_allocate(vk_guest_memory *owner,VkDeviceSize capacity,
                                 VkDeviceSize alignment,int coherent);
VkResult vk_guest_memory_bind(vk_guest_memory *owner,VkDeviceSize offset,
                             VkDeviceSize size,VkBufferUsageFlags usage,
                             vk_guest_memory_buffer *view);
// Neither API waits for GPU work. The existing retirement owner must prove
// completion. Releasing an owner with live borrowed buffers returns NOT_READY.
void vk_guest_memory_unbind(vk_guest_memory_buffer *view);
VkResult vk_guest_memory_release(vk_guest_memory *owner);
// Caller owns every cache atom touched by these operations. Coherence never
// replaces GPU completion; invalidating concurrent CPU-dirty neighbors is wrong.
VkResult vk_guest_memory_flush(vk_guest_memory *owner,VkDeviceSize offset,VkDeviceSize size);
VkResult vk_guest_memory_invalidate(vk_guest_memory *owner,VkDeviceSize offset,VkDeviceSize size);
