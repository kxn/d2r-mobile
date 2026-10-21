// Real finite imports, the original clear/readback path, and a controlled
// release publisher. The handshake is not a performance sample or timeout.
#if !defined(DM_ANDROID_APP)
extern void vk_test_native_retire_pause(void (*)(void *,void *),void *);
extern void vk_test_native_retire_state(uint64_t,uint64_t *,unsigned *,unsigned *);
extern void vk_test_native_allocation_stats(uint64_t *,uint64_t *);
extern unsigned vk_test_native_initial_general(VkImage);
typedef struct {
  pthread_mutex_t mutex;
  pthread_cond_t cv;
  void *binding;
  VkImage image;
  int entered,resume;
} native_retirement_handshake;
static void native_retirement_pause(void *binding,void *context) {
  native_retirement_handshake *h=context;
  if(binding!=h->binding)return;
  pthread_mutex_lock(&h->mutex);h->entered=1;pthread_cond_signal(&h->cv);
  while(!h->resume)pthread_cond_wait(&h->cv,&h->mutex);
  pthread_mutex_unlock(&h->mutex);
}
static void native_retirement_destroy(void *context) {
  native_retirement_handshake *h=context;
  vkDestroyImage(V.device,h->image,NULL);vk_pool_native_image_release(h->binding);
}
static void *native_retirement_worker(void *context) {native_retirement_destroy(context);return NULL;}
static void native_retirement_state(uint64_t address,uint64_t pending,unsigned live,unsigned nodes) {
  uint64_t actual_pending;unsigned actual_live,actual_nodes;
  vk_test_native_retire_state(address,&actual_pending,&actual_live,&actual_nodes);
  CHECK(actual_pending==pending && actual_live==live && actual_nodes==nodes);
}
static void native_retirement_gpu_test(void) {
  CHECK(vk_pool_shared_mode() && V.options.pool_window_bytes==65536);
  NVNmemoryPool pools[2];NVNmemoryPoolBuilder pb;
  for(unsigned n=0;n<2;n++) {
    CALL(nvnMemoryPoolBuilderSetDefaults,&pb);CALL(nvnMemoryPoolBuilderSetDevice,&pb,&device);
    CALL(nvnMemoryPoolBuilderSetStorage,&pb,bytes+(n<<21),2u<<20);
    CALL(nvnMemoryPoolBuilderSetFlags,&pb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
    CHECK(CALL(nvnMemoryPoolInitialize,&pools[n],&pb));
  }
  VkExternalMemoryImageCreateInfo external={.sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
    .handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
  VkImageCreateInfo create={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.pNext=&external,
    .imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,.extent={32,32,1},.mipLevels=1,.arrayLayers=1,
    .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
    .usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    .sharingMode=VK_SHARING_MODE_EXCLUSIVE};
  uint64_t initial_count,initial_bytes,count,allocated;vk_test_native_allocation_stats(&initial_count,&initial_bytes);
  VkImage images[3];void *bindings[3];uint64_t addresses[3];VkMemoryRequirements needs;
  for(unsigned n=0;n<3;n++) {
    addresses[n]=(uintptr_t)bytes+(n==2?(2u<<20):0)+65536+(n==1?65536:0);
    VK_CHECK(vkCreateImage(V.device,&create,NULL,&images[n]));vkGetImageMemoryRequirements(V.device,images[n],&needs);
    CHECK(needs.size && needs.size<=65536 && !(addresses[n]%needs.alignment));
    mtl_sub_begin();vk_pool_requirement requirement={addresses[n],needs.size,0,VK_PIPELINE_STAGE_TRANSFER_BIT,1,VK_POOL_REQUIRE_NATIVE_IMAGE};
    int operation=vk_pool_operation_begin_ranges(&requirement,1);VkDeviceMemory memory;
    bindings[n]=vk_pool_native_image_bind(addresses[n],needs.size,images[n],&needs,&memory);CHECK(bindings[n] && memory);
    checks+=vk_test_native_initial_general(images[n]);
    VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};VkClearColorValue red={.float32={1,0,0,1}};
    vk_pool_native_image_use(bindings[n],addresses[n],needs.size,VK_PIPELINE_STAGE_TRANSFER_BIT);
    vkCmdClearColorImage(V.cmd,images[n],VK_IMAGE_LAYOUT_GENERAL,&red,1,&range);
    VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT};
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,NULL,0,NULL);
    vk_buffer *pixels=vk_temp_buffer(4096);VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={32,32,1}};
    vkCmdCopyImageToBuffer(V.cmd,images[n],VK_IMAGE_LAYOUT_GENERAL,pixels->handle,1,&copy);
    VkBufferMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,
      .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=pixels->handle,.size=4096};
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&host,0,NULL);
    uint8_t actual[4096];native_lane_readback publication={actual,pixels->map,sizeof actual};
    vk_completion_write(native_lane_readback_done,&publication,actual,sizeof actual);
    vk_pool_operation_end(operation);vk_internal_wait(vk_internal_commit());
    for(unsigned p=0;p<sizeof actual;p+=4)CHECK(actual[p]==255 && !actual[p+1] && !actual[p+2] && actual[p+3]==255);
  }
  vk_test_native_allocation_stats(&count,&allocated);CHECK(count==initial_count+3 && allocated>initial_bytes);
  uint64_t all_bytes=allocated;native_retirement_state(addresses[0],0,2,2);native_retirement_state(addresses[2],0,1,1);
  for(unsigned n=0;n<9;n++)vk_pool_native_image_collect();
  native_retirement_state(addresses[0],0,2,2);native_retirement_state(addresses[2],0,1,1);
  native_retirement_handshake h={.mutex=PTHREAD_MUTEX_INITIALIZER,.cv=PTHREAD_COND_INITIALIZER,.binding=bindings[0],.image=images[0]};
  // Recorder and all previous completion callbacks are idle when installing
  // the owned hook; it is cleared only after the publisher has finished.
  vk_test_native_retire_pause(native_retirement_pause,&h);
  int inflight=vk_async_submissions() && V.timeline_semaphore;VkSemaphore gate=VK_NULL_HANDLE;uint64_t ticket=0;pthread_t worker;
  if(inflight) {
    gate=block_gpu_queue();mtl_sub_begin();
    vk_pool_requirement requirement={addresses[0],needs.size,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0,VK_POOL_REQUIRE_NATIVE_IMAGE};
    int operation=vk_pool_operation_begin_ranges(&requirement,1);
    vk_pool_native_image_use(bindings[0],addresses[0],needs.size,VK_PIPELINE_STAGE_TRANSFER_BIT);
    vk_buffer *pixels=vk_temp_buffer(4096);VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={32,32,1}};
    vkCmdCopyImageToBuffer(V.cmd,images[0],VK_IMAGE_LAYOUT_GENERAL,pixels->handle,1,&copy);
    vk_completion(native_retirement_destroy,&h);vk_pool_operation_end(operation);ticket=vk_internal_commit();
    CHECK(vk_completed_serial()<ticket);vk_pool_native_image_collect();native_retirement_state(addresses[0],0,2,2);
    VkSemaphoreSignalInfoKHR signal={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO_KHR,.semaphore=gate,.value=1};
    VK_CHECK(V.signal_timeline(V.device,&signal));
  } else CHECK(!pthread_create(&worker,NULL,native_retirement_worker,&h));
  pthread_mutex_lock(&h.mutex);while(!h.entered)pthread_cond_wait(&h.cv,&h.mutex);pthread_mutex_unlock(&h.mutex);
  // released=1 is visible, but the node/storage cannot be collected yet. Its
  // pending notification must survive arbitrary owner maintenance calls.
  native_retirement_state(addresses[0],1,1,2);
  for(unsigned n=0;n<9;n++)vk_pool_native_image_collect();
  native_retirement_state(addresses[0],1,1,2);native_retirement_state(addresses[2],0,1,1);
  vk_test_native_allocation_stats(&count,&allocated);CHECK(count==initial_count+3 && allocated==all_bytes);
  pthread_mutex_lock(&h.mutex);h.resume=1;pthread_cond_signal(&h.cv);pthread_mutex_unlock(&h.mutex);
  if(inflight) {vk_internal_wait(ticket);vkDestroySemaphore(V.device,gate,NULL);} else CHECK(!pthread_join(worker,NULL));
  vk_test_native_retire_pause(NULL,NULL);pthread_cond_destroy(&h.cv);pthread_mutex_destroy(&h.mutex);
  vk_pool_native_image_collect();native_retirement_state(addresses[0],0,1,1);native_retirement_state(addresses[2],0,1,1);
  vk_test_native_allocation_stats(&count,&allocated);CHECK(count==initial_count+2 && allocated<all_bytes);
  for(unsigned n=0;n<9;n++)vk_pool_native_image_collect();
  for(unsigned n=1;n<3;n++) {vkDestroyImage(V.device,images[n],NULL);vk_pool_native_image_release(bindings[n]);}
  native_retirement_state(addresses[1],1,0,1);native_retirement_state(addresses[2],1,0,1);
  // Pool close must discharge its pending ready=2 node without relying on a
  // later submission/global collector, and must not collect the other Pool.
  CALL(nvnMemoryPoolFinalize,&pools[0]);native_retirement_state(addresses[2],1,0,1);
  CALL(nvnMemoryPoolFinalize,&pools[1]);vk_test_native_allocation_stats(&count,&allocated);
  CHECK(count==initial_count && allocated==initial_bytes);CHECK(!vk_validation_error_count());
  printf("{\"test\":\"native-retirement-event-gate\",\"result\":\"passed\",\"checks\":%u,\"actual_imports\":3,\"Pools\":2,\"release1_interleave\":true,\"inflight_fence_destroy\":%s,\"actual_RGBA_pixels\":true,\"ASTC_executed\":false}\n",checks,inflight?"true":"false");
}
#else
static void native_retirement_gpu_test(void) {die("Native retirement handshake is host-only");}
#endif
