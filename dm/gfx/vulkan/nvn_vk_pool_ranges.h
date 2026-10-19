// Logical buffer requirements, collected before an operation acquires physical
// buffers/descriptors. A sink only accumulates ranges: it must not submit,
// promote backing, mutate NVN state or re-enter rendering while visiting.
// A failed enumeration invalidates every accumulated range, including those
// already delivered to the sink; callers must discard the entire result.
#pragma once
#include <stdint.h>
#include <vulkan/vulkan.h>
enum { VK_POOL_REQUIRE_VERTEX=-1,VK_POOL_REQUIRE_INDEX=-2 };

typedef struct {
  uint64_t address,size;
  // Optional aligned descriptor prefix. A planner must resolve this relative
  // to the owning logical pool, never by rounding the guest pointer itself.
  uint64_t prefix_alignment;
  VkPipelineStageFlags stages;
  int writable,binding_kind;
} vk_pool_requirement;
typedef int (*vk_pool_requirement_sink)(void *context,const vk_pool_requirement *range);
