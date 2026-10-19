// 在全新进程中经原 NVN 提交，避免混用测试直接提交与 HLE 栅栏编号。
// Submit through original NVN in a fresh process, keeping HLE and backend
// fence numbers in the same domain instead of mixing direct fixture submits.
static int acquire_sync_scope_test(void) {
  mtl_init();
  CHECK(vk_async_submissions() && V.timeline_semaphore);
  CHECK(!setenv("DM_NVN_SYNC", "1", 1));
  NVNmemoryPoolBuilder mb = {0};
  CALL(nvnMemoryPoolBuilderSetDefaults, &mb);
  CALL(nvnMemoryPoolBuilderSetDevice, &mb, &device);
  CALL(nvnMemoryPoolBuilderSetStorage, &mb, bytes, 4 << 20);
  CALL(nvnMemoryPoolBuilderSetFlags, &mb,
       NVN_MEMORY_POOL_FLAGS_CPU_CACHED | NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize, &memory, &mb));
  CHECK(CALL(nvnCommandBufferInitialize, &cmd, &device));
  CALL(nvnCommandBufferAddCommandMemory, &cmd, &memory, 0, 1 << 20);
  NVNtexture textures[2];
  NVNtexture *pointers[] = {&textures[0], &textures[1]};
  for (unsigned i = 0; i < 2; i++) {
    NVNtextureBuilder tb = {0};
    CALL(nvnTextureBuilderSetDefaults, &tb);
    CALL(nvnTextureBuilderSetDevice, &tb, &device);
    CALL(nvnTextureBuilderSetTarget, &tb, NVN_TEXTURE_TARGET_2D);
    CALL(nvnTextureBuilderSetWidth, &tb, 16);
    CALL(nvnTextureBuilderSetHeight, &tb, 16);
    CALL(nvnTextureBuilderSetFormat, &tb, NVN_FORMAT_RGBA8);
    CALL(nvnTextureBuilderSetStorage, &tb, &memory, (1 << 20) + i * 65536);
    CHECK(CALL(nvnTextureInitialize, &textures[i], &tb));
  }
  NVNwindowBuilder wb = {0};
  NVNwindow window;
  CALL(nvnWindowBuilderSetDefaults, &wb);
  CALL(nvnWindowBuilderSetDevice, &wb, &device);
  CALL(nvnWindowBuilderSetPresentInterval, &wb, 0);
  CALL(nvnWindowBuilderSetTextures, &wb, 2, pointers);
  CHECK(CALL(nvnWindowInitialize, &window, &wb));
  NVNsync acquired[2], queue_done;
  CHECK(CALL(nvnSyncInitialize, &queue_done, &device));
  for (unsigned i = 0; i < 2; i++)
    CHECK(CALL(nvnSyncInitialize, &acquired[i], &device));
  CALL(nvnCommandBufferBeginRecording, &cmd);
  NVNcommandHandle handle = end();
  VkSemaphoreTypeCreateInfoKHR type = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR,
      .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE_KHR};
  VkSemaphoreCreateInfo create = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                                 .pNext = &type};
  VkSemaphore gate;
  VK_CHECK(vkCreateSemaphore(V.device, &create, NULL, &gate));
  uint64_t value = 1;
  VkTimelineSemaphoreSubmitInfoKHR timeline = {
      .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR,
      .waitSemaphoreValueCount = 1, .pWaitSemaphoreValues = &value};
  VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  VkSubmitInfo blocker = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .pNext = &timeline,
      .waitSemaphoreCount = 1, .pWaitSemaphores = &gate, .pWaitDstStageMask = &stage};
  pthread_mutex_lock(&V.queue_mu);
  VK_CHECK(vkQueueSubmit(V.queue, 1, &blocker, VK_NULL_HANDLE));
  pthread_mutex_unlock(&V.queue_mu);
  CALL(nvnQueueSubmitCommands, &queue, 1, &handle);
  CALL(nvnQueueFenceSync, &queue, &queue_done,
       NVN_SYNC_CONDITION_ALL_GPU_COMMANDS_COMPLETE, 0);
  NVNsyncWaitResult queued = CALL(nvnSyncWait, &queue_done, 0), available[2];
  NVNwindowAcquireTextureResult result[2];
  int indices[2];
  for (unsigned i = 0; i < 2; i++) {
    result[i] = CALL(nvnWindowAcquireTexture, &window, &acquired[i], &indices[i]);
    available[i] = CALL(nvnSyncWait, &acquired[i], 0);
  }
  // 先释放真实 GPU 阻塞再断言；失败也不能留下永不完成的提交。
  // Release the actual GPU blocker before assertions, even on expected failure.
  VkSemaphoreSignalInfoKHR signal = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO_KHR,
                                     .semaphore = gate, .value = 1};
  VK_CHECK(V.signal_timeline(V.device, &signal));
  CALL(nvnQueueFinish, &queue);
  vkDestroySemaphore(V.device, gate, NULL);
  LOG("[vulkan acquire scope fixture] queue=%d acquire=%d,%d indices=%d,%d",
      queued, available[0], available[1], indices[0], indices[1]);
  CHECK(queued == NVN_SYNC_WAIT_RESULT_TIMEOUT_EXPIRED);
  for (unsigned i = 0; i < 2; i++) {
    CHECK(result[i] == NVN_WINDOW_ACQUIRE_TEXTURE_RESULT_SUCCESS);
    CHECK(indices[i] == (int)i);
    CHECK(available[i] == NVN_SYNC_WAIT_RESULT_ALREADY_SIGNALED);
  }
  CHECK(CALL(nvnSyncWait, &queue_done, 0) == NVN_SYNC_WAIT_RESULT_ALREADY_SIGNALED);
  CHECK(vk_validation_error_count() == 0);
  printf("{\"backend\":\"vulkan\",\"test\":\"acquire-sync-scope\",\"checks\":%u,\"result\":\"passed\"}\n", checks);
  fflush(NULL);
  _exit(0);
}
