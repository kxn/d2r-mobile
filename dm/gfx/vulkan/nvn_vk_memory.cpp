// 使用固定版本 GPUOpen VMA 管理 Vulkan 对齐、专用分配要求与线程安全。
// Pinned GPUOpen VMA handles Vulkan alignment, dedicated-allocation requirements
// and synchronization between host threads. No defragmentation or resource moves.
#include "nvn_vk_loader.h"
#define VMA_IMPLEMENTATION
#define VMA_VULKAN_VERSION 1001000
#include "../../third_party/vulkan-memory-allocator/vk_mem_alloc.h"
#include "nvn_vk_memory.h"
static VmaAllocator allocator;
VkResult vk_memory_init(VkInstance instance, VkPhysicalDevice physical, VkDevice device,int device_address) {
  VmaAllocatorCreateInfo create = {};
  if(device_address)create.flags|=VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
  create.instance = instance;
  create.physicalDevice = physical;
  create.device = device;
  create.vulkanApiVersion = VK_API_VERSION_1_1;
  create.preferredLargeHeapBlockSize = 32u << 20;
  VmaVulkanFunctions functions={};
  functions.vkGetInstanceProcAddr=vkGetInstanceProcAddr;
  functions.vkGetDeviceProcAddr=vkGetDeviceProcAddr;
  create.pVulkanFunctions=&functions;
  return vmaCreateAllocator(&create, &allocator);
}
static VkResult buffer_allocate(VkBuffer buffer, VkDeviceMemory *memory, uint8_t **map, void **allocation,int readback) {
  VmaAllocationCreateInfo create = {};
  create.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
  create.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
      (readback ? 0 : VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if(readback) {
    // The VkBuffer already exists; explicit memory preferences are required
    // by vmaAllocateMemoryForBuffer (AUTO needs vmaCreateBuffer).
    create.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
  }
  VmaAllocation handle;
  VmaAllocationInfo info;
  VkResult result = vmaAllocateMemoryForBuffer(allocator, buffer, &create, &handle, &info);
  if (result != VK_SUCCESS) return result;
  result = vmaBindBufferMemory(allocator, handle, buffer);
  if (result != VK_SUCCESS) { vmaFreeMemory(allocator, handle); return result; }
  *memory = info.deviceMemory;
  *map = static_cast<uint8_t *>(info.pMappedData);
  *allocation = handle;
  return VK_SUCCESS;
}
VkResult vk_memory_buffer(VkBuffer buffer,VkDeviceMemory *memory,uint8_t **map,void **allocation) {
  return buffer_allocate(buffer,memory,map,allocation,0);
}
VkResult vk_memory_buffer_readback(VkBuffer buffer,VkDeviceMemory *memory,uint8_t **map,void **allocation) {
  return buffer_allocate(buffer,memory,map,allocation,1);
}
VkMemoryPropertyFlags vk_memory_properties(void *allocation) {
  VkMemoryPropertyFlags flags;
  vmaGetAllocationMemoryProperties(allocator,static_cast<VmaAllocation>(allocation),&flags);
  return flags;
}
VkResult vk_memory_invalidate(void *allocation) {
  return vmaInvalidateAllocation(allocator,static_cast<VmaAllocation>(allocation),0,VK_WHOLE_SIZE);
}
VkResult vk_memory_invalidate_batch(void *const *allocations, uint32_t count) {
  if (count > VK_MEMORY_INVALIDATE_BATCH) return VK_ERROR_INITIALIZATION_FAILED;
  VmaAllocation handles[VK_MEMORY_INVALIDATE_BATCH];
  for (uint32_t i = 0; i < count; ++i)
    handles[i] = static_cast<VmaAllocation>(allocations[i]);
  return vmaInvalidateAllocations(allocator, count, handles, nullptr, nullptr);
}
VkResult vk_memory_image(VkImage image, VkDeviceMemory *memory, void **allocation) {
  VmaAllocationCreateInfo create = {};
  create.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  VmaAllocation handle;
  VmaAllocationInfo info;
  VkResult result = vmaAllocateMemoryForImage(allocator, image, &create, &handle, &info);
  if (result != VK_SUCCESS) return result;
  result = vmaBindImageMemory(allocator, handle, image);
  if (result != VK_SUCCESS) { vmaFreeMemory(allocator, handle); return result; }
  *memory = info.deviceMemory;
  *allocation = handle;
  return VK_SUCCESS;
}
void vk_memory_free(void *allocation) {
  if (allocation) vmaFreeMemory(allocator, static_cast<VmaAllocation>(allocation));
}
uint64_t vk_memory_allocation_bytes(void *allocation) {
  if (!allocation) return 0;
  VmaAllocationInfo info;
  vmaGetAllocationInfo(allocator, static_cast<VmaAllocation>(allocation), &info);
  return info.size;
}
void vk_memory_stats(uint32_t *blocks, uint32_t *allocations, uint64_t *bytes) {
  VmaTotalStatistics stats;
  vmaCalculateStatistics(allocator, &stats);
  *blocks = stats.total.statistics.blockCount;
  *allocations = stats.total.statistics.allocationCount;
  *bytes = stats.total.statistics.allocationBytes;
}

// Original VMA JSON, including its detailed map; no duplicate statistics.
char *vk_memory_build_stats_string(void) {
  char *json=nullptr;
  if(allocator)vmaBuildStatsString(allocator,&json,VK_TRUE);
  return json;
}
void vk_memory_free_stats_string(char *data) {
  if(data)vmaFreeStatsString(allocator,data);
}
