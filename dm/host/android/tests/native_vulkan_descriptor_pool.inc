#ifndef DM_ANDROID_APP
// Inspect the real Vulkan create call, then preserve its implementation and
// the original two-owner pending-submission/content oracle without emulation.
static PFN_vkCreateDescriptorPool descriptor_pool_block_create;
static PFN_vkAllocateDescriptorSets descriptor_pool_block_allocate;
static PFN_vkResetDescriptorPool descriptor_pool_block_reset;
static PFN_vkDestroyDescriptorPool descriptor_pool_block_destroy;
static unsigned descriptor_pool_block_creates;
static unsigned descriptor_pool_block_out,descriptor_pool_block_fragmented;
static struct {VkDescriptorPool pool;unsigned used,limit;} descriptor_pool_block_pools[32];
static VKAPI_ATTR VkResult VKAPI_CALL descriptor_pool_block_create_checked(
    VkDevice device,const VkDescriptorPoolCreateInfo *info,
    const VkAllocationCallbacks *allocator,VkDescriptorPool *pool) {
  CHECK(info->maxSets==512);
  CHECK(info->poolSizeCount==10u-!vk_pixel_local_enabled());
  descriptor_pool_block_creates++;
  VkResult result=descriptor_pool_block_create(device,info,allocator,pool);
  if(result==VK_SUCCESS) {
    unsigned n=0;while(n<32 && descriptor_pool_block_pools[n].pool)n++;
    CHECK(n<32);descriptor_pool_block_pools[n].pool=*pool;
    descriptor_pool_block_pools[n].limit=info->maxSets;
  }
  return result;
}
// Lavapipe does not enforce maxSets. Explicitly inject the two specified
// exhaustion results at that actual create-info boundary; never fabricate a
// successful set or GPU result. This is not a f830 natural-exhaustion test.
static VKAPI_ATTR VkResult VKAPI_CALL descriptor_pool_block_allocate_checked(
    VkDevice device,const VkDescriptorSetAllocateInfo *info,VkDescriptorSet *sets) {
  unsigned n=0;while(n<32 && descriptor_pool_block_pools[n].pool!=info->descriptorPool)n++;
  CHECK(n<32);
  if(info->descriptorSetCount>descriptor_pool_block_pools[n].limit-descriptor_pool_block_pools[n].used) {
    if(info->descriptorSetCount>1){descriptor_pool_block_out++;return VK_ERROR_OUT_OF_POOL_MEMORY;}
    descriptor_pool_block_fragmented++;return VK_ERROR_FRAGMENTED_POOL;
  }
  VkResult result=descriptor_pool_block_allocate(device,info,sets);
  if(result==VK_SUCCESS)descriptor_pool_block_pools[n].used+=info->descriptorSetCount;
  return result;
}
static VKAPI_ATTR VkResult VKAPI_CALL descriptor_pool_block_reset_checked(
    VkDevice device,VkDescriptorPool pool,VkDescriptorPoolResetFlags flags) {
  unsigned n=0;while(n<32 && descriptor_pool_block_pools[n].pool!=pool)n++;
  CHECK(n<32);VkResult result=descriptor_pool_block_reset(device,pool,flags);
  if(result==VK_SUCCESS)descriptor_pool_block_pools[n].used=0;
  return result;
}
static VKAPI_ATTR void VKAPI_CALL descriptor_pool_block_destroy_checked(
    VkDevice device,VkDescriptorPool pool,const VkAllocationCallbacks *allocator) {
  unsigned n=0;while(n<32 && descriptor_pool_block_pools[n].pool!=pool)n++;
  CHECK(n<32);descriptor_pool_block_destroy(device,pool,allocator);
  memset(&descriptor_pool_block_pools[n],0,sizeof descriptor_pool_block_pools[n]);
}
static void descriptor_pool_block_test(void) {
  mtl_init();
  CHECK(V.options.descriptor_sets==2048 && V.timeline_semaphore);
  uint8_t control[0x880];memset(control,2,sizeof control);
  NVNprogram program;NVNshaderData shader={.control=control};
  CHECK(CALL(nvnProgramInitialize,&program,&device));
  CHECK(CALL(nvnProgramSetShaders,&program,1,&shader));
  CALL(nvnQueueFinish,&queue);
  vk_shader *stages[6];vk_program_shaders((const prog_t *)&program,stages);
  CHECK(stages[5] && vk_compute_prewarm(stages[5]));
  vk_descriptor_reuse_reference(0);vk_descriptor_batch_reference(0);
  descriptor_pool_block_create=vkCreateDescriptorPool;
  descriptor_pool_block_allocate=vkAllocateDescriptorSets;
  descriptor_pool_block_reset=vkResetDescriptorPool;
  descriptor_pool_block_destroy=vkDestroyDescriptorPool;
  vkCreateDescriptorPool=descriptor_pool_block_create_checked;
  vkAllocateDescriptorSets=descriptor_pool_block_allocate_checked;
  vkResetDescriptorPool=descriptor_pool_block_reset_checked;
  vkDestroyDescriptorPool=descriptor_pool_block_destroy_checked;
  uint64_t hits0,hits1,resets0,resets1;int reference;
  vk_descriptor_reuse_stats(&hits0,&resets0,&reference);CHECK(!reference);
  descriptor_inflight_test((const prog_t *)&program,0,768);
  vk_descriptor_reuse_stats(&hits1,&resets1,&reference);
  CHECK(hits1-hits0>=768 && resets1==resets0);
  descriptor_inflight_test((const prog_t *)&program,0,2304);
  vk_descriptor_reuse_stats(&hits1,&resets1,&reference);
  CHECK(resets1>resets0 && descriptor_pool_block_creates>=4);
  CHECK(descriptor_pool_block_out && descriptor_pool_block_fragmented);
  for(unsigned n=0;n<32;n++)CHECK(!descriptor_pool_block_pools[n].pool);
  vkCreateDescriptorPool=descriptor_pool_block_create;
  vkAllocateDescriptorSets=descriptor_pool_block_allocate;
  vkResetDescriptorPool=descriptor_pool_block_reset;
  vkDestroyDescriptorPool=descriptor_pool_block_destroy;
  CALL(nvnProgramFinalize,&program);
  LOG("[descriptor pool block fixture] physical=512 retention=2048: injected exhaustion, retained reuse, completed reset and real independent pending GPU contents passed");
}
#endif
