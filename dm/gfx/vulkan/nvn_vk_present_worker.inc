// Android WSI has blocking host calls. Keep acquire, present and presentation
// feedback on one owner thread (including swapchain external synchronization),
// while the original NVN queue keeps all rendering/compute/transfer operations.
// The second queue belongs to the same family, so no ownership transfer occurs.
typedef struct {
  VkResult result;
  uint32_t index;
  int done;
} window_acquire_result;
enum { WINDOW_WSI_PRESENT,WINDOW_WSI_ACQUIRE };
typedef struct {
  int kind;
  VkSwapchainKHR chain;
  uint32_t index;
  VkSemaphore semaphore;
  uint64_t sequence;
  uint32_t game_width,game_height,surface_width,surface_height;
  window_acquire_result *acquire;
} window_wsi_request;
static struct {
  pthread_mutex_t mutex;
  pthread_cond_t changed;
  window_wsi_request requests[4];
  unsigned read,count;
  int started,running;
} window_wsi={.mutex=PTHREAD_MUTEX_INITIALIZER,.changed=PTHREAD_COND_INITIALIZER};
static _Atomic int window_present_error;
static VkResult acquire_image_immediate(uint32_t *index,VkSemaphore semaphore);
static VkResult window_present_immediate(const window_wsi_request *r,VkQueue queue) {
  VkPresentInfoKHR present={.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
    .waitSemaphoreCount=r->semaphore?1:0,.pWaitSemaphores=r->semaphore?&r->semaphore:NULL,
    .swapchainCount=1,.pSwapchains=&r->chain,.pImageIndices=&r->index};
  VkPresentTimeGOOGLE time={.presentID=(uint32_t)r->sequence};
  VkPresentTimesInfoGOOGLE timing={.sType=VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE,
    .swapchainCount=1,.pTimes=&time};
  if(V.display_timing)present.pNext=&timing;
  pthread_mutex_t *mutex=queue==V.queue?&V.queue_mu:&V.present_queue_mu;
  // KHR internally synchronized queues permit concurrent queue API calls.
  // The driver protects its submission prefix, while Android's subsequent
  // BufferQueue throttle may sleep without blocking the next rendering submit.
  // Swapchain acquire/present/feedback remain serialized on this WSI worker;
  // recreation/destruction still drains it. Ordinary queues retain the lock.
  int external_lock=!V.queue_internal_sync;
  if(external_lock) {
    int lock_trace=vk_system_trace_begin("Vulkan present queue lock");
    pthread_mutex_lock(mutex);vk_system_trace_end(lock_trace);
  }
  VkSemaphore bridged=VK_NULL_HANDLE;
  if(queue!=V.queue && r->semaphore) {
    // Ordinary Vulkan submission resolves the cross-queue wait before the
    // Android release-image operation consumes its same-queue binary signal.
    // This isolates the failing WSI release path without a CPU fence wait.
    // Both semaphores remain per-image and retire only after both queues idle.
    bridged=present_bridge[r->index];
    VkPipelineStageFlags stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo bridge={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .waitSemaphoreCount=1,.pWaitSemaphores=&r->semaphore,.pWaitDstStageMask=&stage,
      .signalSemaphoreCount=1,.pSignalSemaphores=&bridged};
    int bridge_trace=vk_system_trace_begin("Vulkan presentation semaphore bridge");
    VkResult submitted=vkQueueSubmit(queue,1,&bridge,VK_NULL_HANDLE);
    vk_system_trace_end(bridge_trace);
    if(submitted!=VK_SUCCESS) { if(external_lock)pthread_mutex_unlock(mutex);return submitted; }
    present.pWaitSemaphores=&bridged;
  }
  int trace=vk_system_trace_begin("Vulkan present call");
  VkResult result=vkQueuePresentKHR(queue,&present);
  vk_system_trace_end(trace);
  if(result==VK_SUCCESS || result==VK_SUBOPTIMAL_KHR) {
    atomic_store(&game_output_size,((uint64_t)r->game_height<<32)|r->game_width);
    atomic_store(&surface_output_size,((uint64_t)r->surface_height<<32)|r->surface_width);
    trace=vk_system_trace_begin("Vulkan presentation feedback");
    // Surface changes can invalidate the timing query after Present succeeded.
    // Let the submission owner perform the same drained swapchain recovery as
    // for an out-of-date Present; the WSI worker cannot recreate it itself.
    result=present_timing_collect();vk_system_trace_end(trace);
  }
  if(external_lock)pthread_mutex_unlock(mutex);return result;
}
extern void crash_thread_init(void);
static void *window_wsi_thread(void *unused) {
  (void)unused;crash_thread_init();pthread_setname_np(pthread_self(),"dm-present");
  for(;;) {
    pthread_mutex_lock(&window_wsi.mutex);
    while(!window_wsi.count)pthread_cond_wait(&window_wsi.changed,&window_wsi.mutex);
    window_wsi_request r=window_wsi.requests[window_wsi.read];
    window_wsi.read=(window_wsi.read+1)%4;window_wsi.count--;window_wsi.running=1;
    pthread_cond_broadcast(&window_wsi.changed);pthread_mutex_unlock(&window_wsi.mutex);
    VkResult result;uint32_t index=0;
    if(r.kind==WINDOW_WSI_ACQUIRE) {
      result=atomic_load(&window_present_error);
      if(result==VK_SUCCESS)result=acquire_image_immediate(&index,r.semaphore);
    } else {
      result=window_present_immediate(&r,V.present_queue);
      if(result==VK_ERROR_OUT_OF_DATE_KHR ||
          (result==VK_ERROR_SURFACE_LOST_KHR && vk_window_discarding())) {
        // Keep the first failure until the submission owner drains this worker
        // and recreates the swapchain. Never destroy a surface on this thread.
        int expected=VK_SUCCESS;atomic_compare_exchange_strong(&window_present_error,&expected,result);
        vk_window_drop_present(r.sequence);
      } else if(result!=VK_SUCCESS && result!=VK_SUBOPTIMAL_KHR)die("Android worker present: %d",result);
      atomic_fetch_add(&present_worker_finished,1);
      vk_renderdoc_present(V.instance,vk_window_discarding());
    }
    pthread_mutex_lock(&window_wsi.mutex);
    if(r.acquire) { r.acquire->result=result;r.acquire->index=index;r.acquire->done=1; }
    window_wsi.running=0;pthread_cond_broadcast(&window_wsi.changed);pthread_mutex_unlock(&window_wsi.mutex);
  }
  return NULL;
}
static void window_wsi_enqueue(window_wsi_request request) {
  pthread_mutex_lock(&window_wsi.mutex);
  if(!window_wsi.started) {
    pthread_t thread;
    if(pthread_create(&thread,NULL,window_wsi_thread,NULL))die("Vulkan WSI worker creation");
    pthread_detach(thread);window_wsi.started=1;atomic_store(&present_worker_active,1);
  }
  int trace=window_wsi.count==4?vk_system_trace_begin("Vulkan WSI request backpressure"):0;
  while(window_wsi.count==4)pthread_cond_wait(&window_wsi.changed,&window_wsi.mutex);
  vk_system_trace_end(trace);
  window_wsi.requests[(window_wsi.read+window_wsi.count)%4]=request;window_wsi.count++;
  if(request.kind==WINDOW_WSI_PRESENT)atomic_fetch_add(&present_worker_queued,1);
  pthread_cond_broadcast(&window_wsi.changed);pthread_mutex_unlock(&window_wsi.mutex);
}
static VkResult window_acquire(uint32_t *index,VkSemaphore semaphore) {
  if(!V.present_worker)return acquire_image_immediate(index,semaphore);
  window_acquire_result acquired={0};
  window_wsi_enqueue((window_wsi_request){.kind=WINDOW_WSI_ACQUIRE,.chain=swapchain,
    .semaphore=semaphore,.acquire=&acquired});
  int trace=vk_system_trace_begin("Vulkan waits WSI acquisition");
  pthread_mutex_lock(&window_wsi.mutex);
  while(!acquired.done)pthread_cond_wait(&window_wsi.changed,&window_wsi.mutex);
  pthread_mutex_unlock(&window_wsi.mutex);vk_system_trace_end(trace);
  *index=acquired.index;return acquired.result;
}
static void window_present_drain(void) {
  if(!V.present_worker)return;
  pthread_mutex_lock(&window_wsi.mutex);
  int trace=window_wsi.count||window_wsi.running?vk_system_trace_begin("Vulkan drains WSI worker"):0;
  while(window_wsi.count || window_wsi.running)pthread_cond_wait(&window_wsi.changed,&window_wsi.mutex);
  vk_system_trace_end(trace);pthread_mutex_unlock(&window_wsi.mutex);
}
static void window_present_queue_idle(void) {
  if(!V.present_worker || V.present_queue==V.queue)return;
  // Caller has drained WSI host requests and the main rendering queue before
  // retiring swapchain images/semaphores. This also consumes outstanding waits
  // on the separate presentation queue; a rendering fence alone is not enough.
  pthread_mutex_lock(&V.present_queue_mu);VK_CHECK(vkQueueWaitIdle(V.present_queue));pthread_mutex_unlock(&V.present_queue_mu);
}
