#include "../../../gfx/vulkan/nvn_vk_record_owner.h"
// A lifetime oracle, not game execution or a performance measurement. Real
// submissions reuse a secondary while resetting its separate primary pool.
static void retained_record_owner_test(void) {
  mtl_init();
  VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS};
  VkRenderPassCreateInfo rp={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    .subpassCount=1,.pSubpasses=&sub};VkRenderPass pass;
  VK_CHECK(vkCreateRenderPass(V.device,&rp,NULL,&pass));
  VkFramebufferCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
    .renderPass=pass,.width=4,.height=4,.layers=1};VkFramebuffer fb;
  VK_CHECK(vkCreateFramebuffer(V.device,&fi,NULL,&fb));
  VkCommandPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .queueFamilyIndex=V.queue_family};VkCommandPool primary_pool;
  VK_CHECK(vkCreateCommandPool(V.device,&pi,NULL,&primary_pool));
  VkCommandBufferAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool=primary_pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
  VkCommandBuffer primary;VK_CHECK(vkAllocateCommandBuffers(V.device,&ai,&primary));
  vk_record_owner *owner=vk_record_owner_create(2);
  VkCommandBufferInheritanceInfo inherit={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
    .renderPass=pass,.framebuffer=fb};
  CHECK(vk_record_owner_available(owner));
  CHECK(!vk_record_owner_cached(owner,0));
  VkCommandBufferInheritanceInfo unsupported=inherit;unsupported.occlusionQueryEnable=VK_TRUE;
  CHECK(!vk_record_owner_begin(owner,0,&unsupported));
  VkCommandBuffer retained=vk_record_owner_begin(owner,0,&inherit);CHECK(retained);
  CHECK(vk_record_owner_end(owner,0));CHECK(vk_record_owner_cached(owner,0)==retained);
  for(unsigned n=1;n<=3;n++) {
    VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    VK_CHECK(vkBeginCommandBuffer(primary,&bi));
    VkRenderPassBeginInfo begin={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass=pass,.framebuffer=fb,.renderArea={{0,0},{4,4}}};
    vkCmdBeginRenderPass(primary,&begin,VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
    vkCmdExecuteCommands(primary,1,&retained);vkCmdEndRenderPass(primary);
    VK_CHECK(vkEndCommandBuffer(primary));
    VkFenceCreateInfo fci={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;
    VK_CHECK(vkCreateFence(V.device,&fci,NULL,&fence));
    VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&primary};
    vk_record_owner_pending(owner,n);
    pthread_mutex_lock(&V.queue_mu);VkResult queued=vkQueueSubmit(V.queue,1,&submit,fence);
    pthread_mutex_unlock(&V.queue_mu);VK_CHECK(queued);
    CHECK(!vk_record_owner_available(owner));CHECK(!vk_record_owner_cached(owner,0));
    CHECK(!vk_record_owner_begin(owner,0,&inherit));
    VK_CHECK(vkWaitForFences(V.device,1,&fence,VK_TRUE,UINT64_MAX));
    // The original submission pool can reset without invalidating the private
    // retained secondary. Reuse opens only after this explicit completion gate.
    VK_CHECK(vkResetCommandPool(V.device,primary_pool,0));
    vk_record_owner_reclaim(owner,n);CHECK(vk_record_owner_available(owner));
    CHECK(vk_record_owner_cached(owner,0)==retained);
    vkDestroyFence(V.device,fence,NULL);
  }
  vk_buffer temporary={0};vk_buffer_create(&temporary,256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  vk_buffer_destroy(&temporary);CHECK(!vk_record_owner_cached(owner,0));
  // An epoch change while recording is also rejected, even though it may be
  // an unrelated retirement. Conservative misses never justify stale reuse.
  CHECK(vk_record_owner_begin(owner,1,&inherit));
  vk_buffer_create(&temporary,256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);vk_buffer_destroy(&temporary);
  CHECK(!vk_record_owner_end(owner,1));CHECK(!vk_record_owner_cached(owner,1));
  CHECK(vk_record_owner_begin(owner,0,&inherit)==retained);
  CHECK(vk_record_owner_end(owner,0));CHECK(vk_record_owner_cached(owner,0)==retained);
  vk_record_owner_destroy(owner);vkDestroyCommandPool(V.device,primary_pool,NULL);
  vk_record_object_retired();vkDestroyFramebuffer(V.device,fb,NULL);vkDestroyRenderPass(V.device,pass,NULL);
  CHECK(vk_validation_error_count()==0);
  printf("{\"test\":\"retained-secondary-owner\",\"checks\":%u,\"real_submissions\":3,\"game_modules_executed\":false,\"result\":\"passed\"}\n",checks);
}
