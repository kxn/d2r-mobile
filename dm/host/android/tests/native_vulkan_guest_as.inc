#include "../../guest_address_space.h"
// Attempt the production layout around a genuine existing Vulkan mapping.
// Neighbor VMA collisions are a real prerequisite failure, not permission to
// relocate/remap/overwrite the allocation. GPU bytes still test the same owner.
static void guest_as_test(void) {
  mtl_init();vk_guest_memory h={0};
  CHECK(vk_guest_memory_allocate(&h,16u<<20,65536,0)==VK_SUCCESS);
  size_t page=(size_t)sysconf(_SC_PAGESIZE);
  CHECK(h.allocation_size<=SIZE_MAX);
  dm_guest_as space={0};size_t offset=128u<<20,opened=offset+8*page;
  int error=dm_guest_as_reserve(&space,h.map,(size_t)h.allocation_size,
                               h.base,(size_t)h.capacity,offset,opened);
  CHECK(!space.base || space.ready);
  if(error) {
    CHECK(!space.ready && !space.base);
    LOG("[guest AS fixture] actual mapped neighborhood refused errno=%d raw=%p bytes=%llu usable=%p heap_offset=%zu; preserved mapping, no placement fallback",error,h.map,(unsigned long long)h.allocation_size,h.base,offset);
  }else {
    CHECK(space.ready && space.heap+offset==h.base && space.heap==space.base+DM_GUEST_HEAP_OFFSET);
    CHECK(space.raw_map==h.map && space.raw_size==h.allocation_size);
    CHECK(space.prefix+space.prefix_size==h.map && space.tail==h.map+h.allocation_size);
    for(size_t k=0;k<page;k++)CHECK(h.base[k]==0);
    CHECK(space.heap[0]==0 && space.heap[offset-page]==0);
    LOG("[guest AS fixture] actual mapped neighborhood reserved AS=%p heap=%p raw=%p capacity=%llu opened=%zu; only raw opened intersection explicitly zeroed",space.base,space.heap,h.map,(unsigned long long)h.capacity,opened);
  }
  // Exactly the same Vulkan allocation/buffer pair on success or refusal. No
  // anonymous stand-in, guest AS address rewrite or completion memcpy change.
  vk_guest_memory_buffer a={0},b={0};
  CHECK(vk_guest_memory_bind(&h,0,65536,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,&a)==VK_SUCCESS);
  CHECK(vk_guest_memory_bind(&h,65536,65536,VK_BUFFER_USAGE_TRANSFER_DST_BIT,&b)==VK_SUCCESS);
  uint32_t *src=(void *)a.map;
  for(unsigned k=0;k<1024;k++)src[k]=UINT32_C(0x8badf00d)^(k*UINT32_C(0x1020305));
  CHECK(vk_guest_memory_flush(&h,0,4096)==VK_SUCCESS);
  // The original dirty tracker can still protect/fault genuine mapped pages.
  nvn_dirty_add_range(h.base,page);nvn_dirty_protect(h.base,page);
  uint64_t stamp=nvn_dirty_now();src[37]^=UINT32_C(0x12345678);
  CHECK(nvn_dirty_since(src+37,4,stamp));CHECK(vk_guest_memory_flush(&h,0,4096)==VK_SUCCESS);
  mtl_sub_begin();VkBufferCopy copy={.size=4096};vkCmdCopyBuffer(V.cmd,a.handle,b.handle,1,&copy);
  VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
  vk_internal_wait(vk_internal_commit());
  CHECK(vk_guest_memory_invalidate(&h,65536,4096)==VK_SUCCESS);
  nvn_dirty_remove_range(h.base);
  // Destination may have remained CPU PROT_NONE throughout actual GPU use.
  // Restore owner's RW only after GPU completion, then inspect its exact bytes.
  if(!error)CHECK(dm_guest_as_release(&space)==0 && !space.base);
  uint32_t *dst=(void *)b.map;
  for(unsigned k=0;k<1024;k++) {
    uint32_t expected=UINT32_C(0x8badf00d)^(k*UINT32_C(0x1020305));
    if(k==37)expected^=UINT32_C(0x12345678);
    CHECK(src[k]==expected && dst[k]==expected);
  }
  CHECK(h.map && h.memory && h.views==2);
  vk_guest_memory_unbind(&a);vk_guest_memory_unbind(&b);
  CHECK(vk_guest_memory_release(&h)==VK_SUCCESS && !h.map && !h.memory);
  CHECK(vk_validation_error_count()==0);
  printf("{\"actual_mapped_as_reservation\":%s,\"reservation_errno\":%d,\"actual_mapped_gpu_bytes\":true}\n",error?"false":"true",error);
}
