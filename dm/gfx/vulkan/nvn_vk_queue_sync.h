// Public KHR declarations for builds using older Vulkan SDK headers.
// Registry extension 505, revision 1; no private driver ABI or replacement lock.
#pragma once
#ifndef VK_KHR_internally_synchronized_queues
#define VK_KHR_internally_synchronized_queues 1
#define VK_KHR_INTERNALLY_SYNCHRONIZED_QUEUES_SPEC_VERSION 1
#define VK_KHR_INTERNALLY_SYNCHRONIZED_QUEUES_EXTENSION_NAME "VK_KHR_internally_synchronized_queues"
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INTERNALLY_SYNCHRONIZED_QUEUES_FEATURES_KHR ((VkStructureType)1000504000)
#define VK_DEVICE_QUEUE_CREATE_INTERNALLY_SYNCHRONIZED_BIT_KHR ((VkDeviceQueueCreateFlagBits)0x00000004)
typedef struct VkPhysicalDeviceInternallySynchronizedQueuesFeaturesKHR {
  VkStructureType sType;
  void *pNext;
  VkBool32 internallySynchronizedQueues;
} VkPhysicalDeviceInternallySynchronizedQueuesFeaturesKHR;
#endif
