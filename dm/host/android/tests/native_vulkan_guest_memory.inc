#include "../../../gfx/vulkan/nvn_vk_guest_memory.h"
// Exercise production allocation/binding/cache APIs and the original dirty
// tracker on genuine mapped VkDeviceMemory, never on an anonymous stand-in.
// This is an owned GPU oracle, not original-game heap acceptance.
static void guest_memory_test(void) {
  mtl_init();
  VkPhysicalDeviceMemoryProperties real_memory=V.mem;
  for(unsigned i=0;i<V.mem.memoryHeapCount;i++)V.mem.memoryHeaps[i].size=4096;
  vk_guest_memory small_heap={0};
  CHECK(vk_guest_memory_allocate(&small_heap,16u<<20,65536,0)==VK_ERROR_OUT_OF_DEVICE_MEMORY);
  CHECK(!small_heap.memory && !small_heap.base);
  V.mem=real_memory;
  unsigned supported=0;
  for(int coherent=0;coherent<=1;coherent++) {
    vk_guest_memory h={0};
    VkResult result=vk_guest_memory_allocate(&h,16u<<20,65536,coherent);
    if(result==VK_ERROR_FEATURE_NOT_PRESENT) {
      LOG("[guest memory fixture] cached coherent_required=%d unsupported",coherent);continue;
    }
    CHECK(result==VK_SUCCESS);supported++;
    CHECK(!((uintptr_t)h.base&65535));
    CHECK((h.properties&VK_MEMORY_PROPERTY_HOST_CACHED_BIT)!=0);
    if(coherent)CHECK((h.properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)!=0);
    // Explicit initialization is part of the contract, not a driver-zero claim.
    memset(h.base,0,16u<<20);
    vk_guest_memory_buffer a={0},b={0},bad={0};
    VkBufferUsageFlags usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    CHECK(vk_guest_memory_bind(&h,65536,65536,usage,&a)==VK_SUCCESS);
    CHECK(vk_guest_memory_bind(&h,131072,65536,usage,&b)==VK_SUCCESS);
    CHECK(a.map==h.base+65536 && b.map==h.base+131072 && h.views==2);
    CHECK(vk_guest_memory_bind(&h,h.capacity,1,usage,&bad)!=VK_SUCCESS);
    CHECK(vk_guest_memory_bind(&h,UINT64_MAX,2,usage,&bad)!=VK_SUCCESS);
    CHECK(vk_guest_memory_bind(&h,1,65536,usage,&bad)!=VK_SUCCESS);
    CHECK(vk_guest_memory_flush(&h,h.capacity,1)!=VK_SUCCESS);
    CHECK(vk_guest_memory_release(&h)==VK_NOT_READY && h.memory && h.views==2);
    uint32_t *src=(void *)a.map,*dst=(void *)b.map;
    for(unsigned i=0;i<1024;i++)src[i]=i*UINT32_C(0x1020305)^UINT32_C(0x8badf00d);
    // Preserve full-atom ownership; there are no CPU writes during GPU use.
    CHECK(vk_guest_memory_flush(&h,65536,65536)==VK_SUCCESS);
    CHECK(vk_guest_memory_flush(&h,131072,65536)==VK_SUCCESS);
    nvn_dirty_add_range(h.base,h.capacity);nvn_dirty_protect(a.map,65536);
    uint64_t stamp=nvn_dirty_now();
    src[15]^=UINT32_C(0x12345678); // original write-fault/generation machinery
    CHECK(nvn_dirty_since(src+15,4,stamp));
    CHECK(vk_guest_memory_flush(&h,65536,65536)==VK_SUCCESS);
    nvn_dirty_protect(a.map,65536);
    mtl_sub_begin();
    VkBufferCopy copy={.size=4096};vkCmdCopyBuffer(V.cmd,a.handle,b.handle,1,&copy);
    VkMemoryBarrier reuse={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT};
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&reuse,0,NULL,0,NULL);
    // CPU RO protection must not change the actual GPU mapping.
    vkCmdFillBuffer(V.cmd,a.handle,8192,4096,UINT32_C(0x13579bdf));
    VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
    vk_internal_wait(vk_internal_commit());
    CHECK(vk_guest_memory_invalidate(&h,65536,65536)==VK_SUCCESS);
    CHECK(vk_guest_memory_invalidate(&h,131072,65536)==VK_SUCCESS);
    for(unsigned i=0;i<1024;i++) {
      uint32_t expected=i*UINT32_C(0x1020305)^UINT32_C(0x8badf00d);
      if(i==15)expected^=UINT32_C(0x12345678);
      CHECK(dst[i]==expected);CHECK(src[2048+i]==UINT32_C(0x13579bdf));
    }
    CHECK(src[1024]==0 && src[3072]==0 && dst[1024]==0);
    nvn_dirty_remove_range(h.base);
    // Borrowed NVN-like buffers can retire without freeing the guest owner.
    vk_guest_memory_unbind(&a);CHECK(h.views==1 && h.base[0]==0);
    vk_guest_memory_unbind(&b);CHECK(h.views==0 && h.base[0]==0);
    LOG("[guest memory fixture] cached coherent_required=%d actual_flags=%#x allocation=%llu guest=%llu: two borrowed buffers, true dirty fault, protected GPU fill, CPU/GPU bytes passed",coherent,h.properties,(unsigned long long)h.allocation_size,(unsigned long long)h.capacity);
    CHECK(vk_guest_memory_release(&h)==VK_SUCCESS && !h.memory && !h.base);
  }
  CHECK(supported>0);CHECK(vk_validation_error_count()==0);
}
