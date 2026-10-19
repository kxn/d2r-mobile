#include "nvn_vk_guest_memory.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const VkBufferUsageFlags guest_usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|
    VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|
    VK_BUFFER_USAGE_INDEX_BUFFER_BIT|VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT|
    VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
static VkDeviceSize buffer_limit(void) {
  int supported=V.props.apiVersion>=VK_API_VERSION_1_3;
  if(!supported) {
    uint32_t count=0;
    if(vkEnumerateDeviceExtensionProperties(V.physical,NULL,&count,NULL)==VK_SUCCESS) {
      VkExtensionProperties *extensions=count?calloc(count,sizeof *extensions):NULL;
      if(extensions && vkEnumerateDeviceExtensionProperties(V.physical,NULL,&count,extensions)==VK_SUCCESS)
        for(uint32_t n=0;n<count;n++)
          if(!strcmp(extensions[n].extensionName,VK_KHR_MAINTENANCE_4_EXTENSION_NAME))supported=1;
      free(extensions);
    }
  }
  if(!supported) {
    // Stay inside an already reported buffer capability on older drivers;
    // this experimental allocator does not infer multi-GiB buffer support.
    return V.props.limits.maxStorageBufferRange;
  }
  VkPhysicalDeviceMaintenance4Properties p={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_PROPERTIES};
  VkPhysicalDeviceProperties2 properties={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&p};
  vkGetPhysicalDeviceProperties2(V.physical,&properties);return p.maxBufferSize;
}
static void requirements(VkBuffer buffer,VkMemoryRequirements2 *r,VkMemoryDedicatedRequirements *d) {
  *d=(VkMemoryDedicatedRequirements){.sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
  *r=(VkMemoryRequirements2){.sType=VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,.pNext=d};
  VkBufferMemoryRequirementsInfo2 i={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,.buffer=buffer};
  vkGetBufferMemoryRequirements2(V.device,&i,r);
}
VkResult vk_guest_memory_allocate(vk_guest_memory *out,VkDeviceSize capacity,
                                 VkDeviceSize alignment,int coherent) {
  if(!out || out->memory || !V.device || !capacity || !alignment ||
     (alignment&(alignment-1)) || alignment<(VkDeviceSize)sysconf(_SC_PAGESIZE) ||
     capacity>UINT64_MAX-alignment || capacity+alignment>SIZE_MAX)
    return VK_ERROR_INITIALIZATION_FAILED;
  if(capacity+alignment>buffer_limit())return VK_ERROR_FEATURE_NOT_PRESENT;
  vk_guest_memory h={.capacity=capacity};
  VkBufferCreateInfo ci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size=capacity+alignment,.usage=guest_usage};
  VkResult result=vkCreateBuffer(V.device,&ci,NULL,&h.reservation);
  if(result!=VK_SUCCESS)return result;
  VkMemoryRequirements2 r;VkMemoryDedicatedRequirements d;requirements(h.reservation,&r,&d);
  if(d.requiresDedicatedAllocation) {result=VK_ERROR_FEATURE_NOT_PRESENT;goto fail;}
  VkMemoryPropertyFlags wanted=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT|
      (coherent?VK_MEMORY_PROPERTY_HOST_COHERENT_BIT:0);
  VkPhysicalDeviceMaintenance3Properties limit={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES};
  VkPhysicalDeviceProperties2 properties={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&limit};
  vkGetPhysicalDeviceProperties2(V.physical,&properties);
  if(r.memoryRequirements.size>limit.maxMemoryAllocationSize) {
    result=VK_ERROR_OUT_OF_DEVICE_MEMORY;goto fail;
  }
  int compatible=0;
  uint32_t t;
  for(t=0;t<V.mem.memoryTypeCount;t++) {
    if((r.memoryRequirements.memoryTypeBits&(1u<<t)) &&
       (V.mem.memoryTypes[t].propertyFlags&wanted)==wanted) {
      compatible=1;
      // Allocation size exceeding its advertised heap is invalid Vulkan use,
      // not merely a request for the driver to return OOM. Try later types too.
      uint32_t heap=V.mem.memoryTypes[t].heapIndex;
      if(heap<V.mem.memoryHeapCount && r.memoryRequirements.size<=V.mem.memoryHeaps[heap].size)break;
    }
  }
  if(t==V.mem.memoryTypeCount){result=compatible?VK_ERROR_OUT_OF_DEVICE_MEMORY:VK_ERROR_FEATURE_NOT_PRESENT;goto fail;}
  h.memory_type=t;h.properties=V.mem.memoryTypes[t].propertyFlags;
  h.allocation_size=r.memoryRequirements.size;
  VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize=h.allocation_size,.memoryTypeIndex=t};
  result=vkAllocateMemory(V.device,&ai,NULL,&h.memory);if(result!=VK_SUCCESS)goto fail;
  result=vkBindBufferMemory(V.device,h.reservation,h.memory,0);if(result!=VK_SUCCESS)goto fail;
  void *map=NULL;result=vkMapMemory(V.device,h.memory,0,VK_WHOLE_SIZE,0,&map);
  if(result!=VK_SUCCESS)goto fail;
  h.map=map;
  h.offset=(-(uintptr_t)map)&(alignment-1);
  // VA alignment alone need not satisfy the buffer's binding alignment.
  // Reject here rather than publish an owner whose ordinary views cannot bind.
  if(h.offset%r.memoryRequirements.alignment || h.offset>h.allocation_size || capacity>h.allocation_size-h.offset) {
    result=VK_ERROR_MEMORY_MAP_FAILED;goto fail;
  }
  h.base=h.map+h.offset;*out=h;return VK_SUCCESS;
fail:
  if(h.map)vkUnmapMemory(V.device,h.memory);
  if(h.reservation)vkDestroyBuffer(V.device,h.reservation,NULL);
  if(h.memory)vkFreeMemory(V.device,h.memory,NULL);
  return result;
}
VkResult vk_guest_memory_bind(vk_guest_memory *h,VkDeviceSize offset,
                             VkDeviceSize size,VkBufferUsageFlags usage,
                             vk_guest_memory_buffer *out) {
  if(!h || !h->memory || !out || out->handle || !size || !usage ||
     (usage&~guest_usage) || offset>h->capacity || size>h->capacity-offset || h->views==UINT32_MAX)
    return VK_ERROR_INITIALIZATION_FAILED;
  VkBufferCreateInfo ci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=size,.usage=usage};
  VkBuffer buffer=VK_NULL_HANDLE;VkResult result=vkCreateBuffer(V.device,&ci,NULL,&buffer);
  if(result!=VK_SUCCESS)return result;
  VkMemoryRequirements2 r;VkMemoryDedicatedRequirements d;requirements(buffer,&r,&d);
  VkDeviceSize physical=h->offset+offset;
  if(d.requiresDedicatedAllocation || !(r.memoryRequirements.memoryTypeBits&(1u<<h->memory_type)) ||
     physical%r.memoryRequirements.alignment || physical>h->allocation_size ||
     r.memoryRequirements.size>h->allocation_size-physical)result=VK_ERROR_FEATURE_NOT_PRESENT;
  else result=vkBindBufferMemory(V.device,buffer,h->memory,physical);
  if(result!=VK_SUCCESS){vkDestroyBuffer(V.device,buffer,NULL);return result;}
  *out=(vk_guest_memory_buffer){buffer,h->base+offset,size,offset,h};h->views++;
  return VK_SUCCESS;
}
void vk_guest_memory_unbind(vk_guest_memory_buffer *v) {
  if(!v || !v->handle)return;
  vkDestroyBuffer(V.device,v->handle,NULL);v->owner->views--;
  *v=(vk_guest_memory_buffer){0};
}
VkResult vk_guest_memory_release(vk_guest_memory *h) {
  if(!h)return VK_ERROR_INITIALIZATION_FAILED;
  if(h->views)return VK_NOT_READY;
  if(h->map)vkUnmapMemory(V.device,h->memory);
  if(h->reservation)vkDestroyBuffer(V.device,h->reservation,NULL);
  if(h->memory)vkFreeMemory(V.device,h->memory,NULL);
  *h=(vk_guest_memory){0};return VK_SUCCESS;
}
static VkResult cache(vk_guest_memory *h,VkDeviceSize offset,VkDeviceSize size,int invalidate) {
  if(!h || !h->memory || offset>h->capacity || size>h->capacity-offset)
    return VK_ERROR_INITIALIZATION_FAILED;
  if(!size || (h->properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))return VK_SUCCESS;
  VkDeviceSize atom=V.props.limits.nonCoherentAtomSize;
  VkDeviceSize first=h->offset+offset,last=first+size;
  first-=first%atom;
  // WHOLE_SIZE includes any required terminal partial atom without overflow.
  VkDeviceSize tail=last%atom?atom-last%atom:0;
  VkMappedMemoryRange r={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=h->memory,
    .offset=first,.size=tail>h->allocation_size-last?VK_WHOLE_SIZE:last+tail-first};
  return invalidate?vkInvalidateMappedMemoryRanges(V.device,1,&r):vkFlushMappedMemoryRanges(V.device,1,&r);
}
VkResult vk_guest_memory_flush(vk_guest_memory *h,VkDeviceSize o,VkDeviceSize n){return cache(h,o,n,0);}
VkResult vk_guest_memory_invalidate(vk_guest_memory *h,VkDeviceSize o,VkDeviceSize n){return cache(h,o,n,1);}
