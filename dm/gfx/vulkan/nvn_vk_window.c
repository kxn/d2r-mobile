// NVN 交换链纹理显示到 Android Surface；GPU 命令完成与游戏栅栏编号分开。
// Present NVN swapchain textures to Android Surface; GPU completion stays
// separate from guest fence numbering.
#include "nvn_vk_window.h"
#include "nvn_vk_system_trace.h"
#include "nvn_vk_internal.h"
#include "nvn_vk_renderdoc.h"
#include <math.h>
#include <stdatomic.h>
#include "nvn_vk_present_shader.inc"
// Automatic policy is limited to the GPU family with actual counter and WSI
// validation. Explicit requests still override it; image eligibility is
// checked separately for every present, including resize/crop fallbacks.
static atomic_int shader_present_requested=-1,shader_present_available;
static atomic_int shader_present_automatic;
static atomic_uint_fast64_t shader_present_selected,shader_present_fallback;
static int shader_present_mode(void) {
  int requested=atomic_load(&shader_present_requested);
  if(requested>=0)return requested;
  return atomic_load(&shader_present_automatic);
}
void vk_window_shader_present(int enabled) { atomic_store(&shader_present_requested,!!enabled); }
void vk_window_shader_present_stats(uint64_t *selected,uint64_t *fallback,int *requested,int *available) {
  *selected=atomic_load(&shader_present_selected);*fallback=atomic_load(&shader_present_fallback);
  *requested=shader_present_mode();*available=atomic_load(&shader_present_available);
}
static _Atomic int present_worker_mode,present_worker_active;
static _Atomic uint64_t present_worker_queued,present_worker_finished;
void vk_window_present_worker_mode(int mode) {
  if(mode<0 || mode>2)die("Vulkan WSI worker mode %d",mode);
  atomic_store(&present_worker_mode,mode);
}
int vk_window_present_worker_requested_mode(void) { return atomic_load(&present_worker_mode); }
void vk_window_present_worker_stats(uint64_t *queued,uint64_t *finished,int *active,int *reference) {
  *queued=atomic_load(&present_worker_queued);*finished=atomic_load(&present_worker_finished);
  *active=atomic_load(&present_worker_active);*reference=V.device?!V.present_worker:!atomic_load(&present_worker_mode);
}
// Startup selection only; FIFO stays the reference/default. MAILBOX is tried
// only when this exact Surface advertises it, never inferred from the GPU name.
static atomic_int mailbox_requested,present_mode_actual=VK_PRESENT_MODE_FIFO_KHR;
static atomic_uint present_mode_supported;
void vk_window_mailbox_present(int enabled) { atomic_store(&mailbox_requested,!!enabled); }
void vk_window_presentation_mode_stats(int *requested,int *actual,unsigned *supported) {
  *requested=atomic_load(&mailbox_requested);*actual=atomic_load(&present_mode_actual);
  *supported=atomic_load(&present_mode_supported);
}
static _Atomic int discard;
static _Atomic uint64_t game_output_size, surface_output_size;
// 记录已成功提交显示的源裁剪与 Surface 尺寸，不把前端请求冒充实际输出。
// Record source crop and Surface dimensions after successful presentation, not requested sizes.
void vk_window_output_sizes(uint32_t *game_w, uint32_t *game_h,
                            uint32_t *surface_w, uint32_t *surface_h) {
  uint64_t game = atomic_load(&game_output_size), surface = atomic_load(&surface_output_size);
  *game_w = (uint32_t)game; *game_h = (uint32_t)(game >> 32);
  *surface_w = (uint32_t)surface; *surface_h = (uint32_t)(surface >> 32);
}
void vk_window_discard(int on) { atomic_store(&discard, on != 0); }
int vk_window_discarding(void) { return atomic_load(&discard); }
static uint64_t present_sequence;
static pthread_mutex_t timing_mu = PTHREAD_MUTEX_INITIALIZER;
static struct {
  uint64_t sequence, ns;
  int dropped;
} timings[256];
// 每次原出图调用都计数，后台丢弃也不能让恢复后的时序编号落后。
// Count every original present call, including discarded background frames.
uint64_t vk_window_begin_present(void) {
  pthread_mutex_lock(&timing_mu);
  uint64_t sequence = ++present_sequence;
  memset(&timings[sequence % 256], 0, sizeof timings[0]);
  pthread_mutex_unlock(&timing_mu);
  return sequence;
}
void vk_window_drop_present(uint64_t sequence) {
  pthread_mutex_lock(&timing_mu);
  timings[sequence % 256].sequence = sequence;
  timings[sequence % 256].ns = 0;
  timings[sequence % 256].dropped = 1;
  pthread_mutex_unlock(&timing_mu);
}
double vk_window_presented_time(uint64_t sequence) {
  pthread_mutex_lock(&timing_mu);
  unsigned slot = sequence % 256;
  double result = timings[slot].sequence == sequence && sequence
                      ? (timings[slot].dropped ? -1 : timings[slot].ns * 1e-9)
                      : 0;
  pthread_mutex_unlock(&timing_mu);
  return result;
}
#ifdef __ANDROID__
#include <android/native_window.h>
static ANativeWindow *native_window;
static double refresh_rate = 60;
static VkSwapchainKHR swapchain;
static VkImage *swap_images;
static uint32_t swap_count;
static VkExtent2D extent;
// Fixed at process startup. Buffer geometry is local to this Surface; the
// Android display mode and the game's render targets are never changed.
static int native_geometry_policy=-1,native_geometry_failed;
static VkExtent2D native_geometry_requested,view_extent;
static int native_geometry_enabled(void) {
  if(native_geometry_policy<0) {
    const char *v=getenv("DM_VULKAN_NATIVE_BUFFER_GEOMETRY");
    if(v && strcmp(v,"0") && strcmp(v,"1"))
      die("DM_VULKAN_NATIVE_BUFFER_GEOMETRY expects 0 or 1");
    native_geometry_policy=v && !strcmp(v,"1");
    LOG("[vulkan present] native buffer geometry enabled=%d (fixed for this process)",native_geometry_policy);
  }
  return native_geometry_policy;
}
static VkFormat swap_format;
static VkImageUsageFlags swap_usage;
static int shader_present_reserved=-1;
static vk_present_shader_target *swap_shader_targets;
static uint8_t *swap_shader_presented;
static uint32_t swap_shader_target_count;
static void swap_shader_targets_destroy(void) {
  for(uint32_t n=0;n<swap_shader_target_count;n++)vk_present_shader_target_destroy(&swap_shader_targets[n]);
  free(swap_shader_targets);swap_shader_targets=NULL;swap_shader_target_count=0;
  free(swap_shader_presented);swap_shader_presented=NULL;
  atomic_store(&shader_present_available,0);
}
static VkFence acquired;
static VkSemaphore *present_finished;
static VkSemaphore *present_bridge;
static uint32_t present_finished_count;
static struct { VkSemaphore semaphore; uint64_t serial; } acquire_slots[3];
static VkImage crop_image;
static VkDeviceMemory crop_memory;
static VkExtent2D crop_extent;
static VkFormat crop_format;
static float content_rect[4];
static void swap_sync_destroy(void) {
  for (uint32_t i = 0; i < present_finished_count; i++)
    vkDestroySemaphore(V.device, present_finished[i], NULL);
  if(present_bridge) {
    for(uint32_t i=0;i<present_finished_count;i++)
      vkDestroySemaphore(V.device,present_bridge[i],NULL);
    free(present_bridge);present_bridge=NULL;
  }
  free(present_finished);
  present_finished = NULL;
  present_finished_count = 0;
  for (unsigned i = 0; i < 3; i++) {
    if (acquire_slots[i].semaphore)
      vkDestroySemaphore(V.device, acquire_slots[i].semaphore, NULL);
    memset(&acquire_slots[i], 0, sizeof acquire_slots[i]);
  }
}
static unsigned acquire_slot(void) {
  uint64_t done = vk_completed_serial();
  unsigned oldest = 0;
  for (unsigned i = 0; i < 3; i++) {
    if (acquire_slots[i].serial <= done) return i;
    if (acquire_slots[i].serial < acquire_slots[oldest].serial) oldest = i;
  }
  // 仅槽位耗尽时等其真实提交完成，保证上次 GPU wait 已消费二进制信号。
  // At slot exhaustion wait for its actual submit, proving the prior GPU wait
  // consumed this binary semaphore before acquisition signals it again.
  vk_internal_wait(acquire_slots[oldest].serial);
  return oldest;
}
typedef struct { VkImage image; VkDeviceMemory memory; } crop_retired;
static void crop_destroy(void *context) {
  crop_retired *old = context;
  vkDestroyImage(V.device, old->image, NULL);
  vkFreeMemory(V.device, old->memory, NULL);
  free(old);
}
int vk_window_surface_point(float world_x, float world_y, float *x, float *y) {
  pthread_mutex_lock(&timing_mu);
  int ready = content_rect[2] > 0 && content_rect[3] > 0;
  if (ready) {
    *x = content_rect[0] + world_x * content_rect[2];
    *y = content_rect[1] + world_y * content_rect[3];
  }
  pthread_mutex_unlock(&timing_mu);
  return ready;
}
int vk_window_world_point(float x, float y, float *world_x, float *world_y) {
  pthread_mutex_lock(&timing_mu);
  int inside = content_rect[2] > 0 && content_rect[3] > 0 &&
               x >= content_rect[0] && y >= content_rect[1] &&
               x < content_rect[0] + content_rect[2] &&
               y < content_rect[1] + content_rect[3];
  if (inside) {
    *world_x = (x - content_rect[0]) / content_rect[2];
    *world_y = (y - content_rect[1]) / content_rect[3];
  }
  pthread_mutex_unlock(&timing_mu);
  return inside;
}
static PFN_vkGetPastPresentationTimingGOOGLE get_past_timings;
static VkResult present_timing_collect(void) {
  if (!get_past_timings)
    return VK_SUCCESS;
  VkPastPresentationTimingGOOGLE past[256];
  uint32_t count = 256;
  VkResult result = get_past_timings(V.device, swapchain, &count, past);
  if (result != VK_SUCCESS && result != VK_INCOMPLETE)
    return result;
  pthread_mutex_lock(&timing_mu);
  for (unsigned i = 0; i < count; i++) {
    // API 的 32 位 ID 对应持续递增的原出图编号，跨交换链仍不重编号。
    // Expand API 32-bit IDs to the original continuous presentation sequence,
    // which must not restart when the swapchain is recreated.
    uint64_t sequence =
        (present_sequence & ~UINT64_C(0xffffffff)) | past[i].presentID;
    if (sequence > present_sequence && sequence >= (UINT64_C(1) << 32))
      sequence -= UINT64_C(1) << 32;
    if (sequence > present_sequence || present_sequence - sequence >= 256 ||
        !past[i].actualPresentTime)
      continue;
    unsigned slot = sequence % 256;
    timings[slot].sequence = sequence;
    timings[slot].ns = past[i].actualPresentTime;
    timings[slot].dropped = 0;
    static unsigned logged;
    if (logged < 3) {
      LOG("[vulkan] presented sequence=%llu actual_time_ns=%llu",
          (unsigned long long)sequence,
          (unsigned long long)past[i].actualPresentTime);
      logged++;
    }
  }
  pthread_mutex_unlock(&timing_mu);
  return VK_SUCCESS;
}
#include "nvn_vk_present_worker.inc"
static VkImage crop_source(VkImage source, VkImageLayout source_layout, const tex_t *t, int x, int y, int w,
                           int h) {
  VkFormat format = vk_nvn_format(t->d.format);
  if (!crop_image || crop_extent.width != (uint32_t)w ||
      crop_extent.height != (uint32_t)h || crop_format != format) {
    if (crop_image) {
      crop_retired *old = malloc(sizeof *old);
      if (!old) die("Vulkan crop retirement allocation");
      *old = (crop_retired){crop_image, crop_memory};
      // 同一队列的当前拷贝完成后才回收；此前在途出图仍可引用旧尺寸图像。
      // Retire after this queue's current copy completes; older pending
      // presents may still reference the previous-size intermediate.
      vk_completion(crop_destroy, old);
    }
    VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                            .imageType = VK_IMAGE_TYPE_2D,
                            .format = format,
                            .extent = {w, h, 1},
                            .mipLevels = 1,
                            .arrayLayers = 1,
                            .samples = VK_SAMPLE_COUNT_1_BIT,
                            .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                     VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    VK_CHECK(vkCreateImage(V.device, &ci, NULL, &crop_image));
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(V.device, crop_image, &requirements);
    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = vk_memory_type(requirements.memoryTypeBits,
                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    VK_CHECK(vkAllocateMemory(V.device, &ai, NULL, &crop_memory));
    VK_CHECK(vkBindImageMemory(V.device, crop_image, crop_memory, 0));
    crop_extent = (VkExtent2D){w, h};
    crop_format = format;
  }
  // 先裁到独立图像，线性滤波才会在裁剪边缘钳位，不读原纹理未绘制的区域。
  // Crop into a separate image so linear filtering clamps at the crop boundary
  // instead of reading undrawn pixels outside it in the original texture.
  VkImageMemoryBarrier barrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = crop_image,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
      .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT};
  vk_transfer_barrier();
  vkCmdPipelineBarrier(V.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
                       &barrier);
  VkImageCopy copy = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                      .srcOffset = {x, y, 0},
                      .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                      .extent = {w, h, 1}};
  vkCmdCopyImage(V.cmd, source, source_layout, crop_image,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  vkCmdPipelineBarrier(V.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
                       &barrier);
  return crop_image;
}
void vk_android_window(void *window, float refresh) {
  pthread_mutex_lock(&timing_mu);
  memset(content_rect, 0, sizeof content_rect);
  pthread_mutex_unlock(&timing_mu);
  // 调用方先用原 dm_gfx_suspend 清空翻译队列与 GPU，再替换 Surface。
  // The caller drains the original translation queue and GPU with
  // dm_gfx_suspend before replacing the Surface.
  if (V.instance) {
    mtl_init();
    window_present_drain();
    mtl_finish();
    pthread_mutex_lock(&V.queue_mu);
    VK_CHECK(vkQueueWaitIdle(V.queue));
    pthread_mutex_unlock(&V.queue_mu);
    window_present_queue_idle();
    if (acquired)
      vkDestroyFence(V.device, acquired, NULL);
    acquired = VK_NULL_HANDLE;
    swap_shader_targets_destroy();
    if (swapchain)
      vkDestroySwapchainKHR(V.device, swapchain, NULL);
    swap_sync_destroy();
    swapchain = VK_NULL_HANDLE;
    free(swap_images);
    swap_images = NULL;
    if (V.surface)
      vkDestroySurfaceKHR(V.instance, V.surface, NULL);
    V.surface = VK_NULL_HANDLE;
  }
  if (native_window) {
    // Release the experimental override only after old GPU/WSI work drained.
    // Surface recreation must start from the View's default dimensions.
    if(native_geometry_requested.width)
      ANativeWindow_setBuffersGeometry(native_window,0,0,0);
    ANativeWindow_release(native_window);
  }
  native_window = window;
  native_geometry_requested=(VkExtent2D){0};native_geometry_failed=0;
  view_extent=window?(VkExtent2D){ANativeWindow_getWidth(window),ANativeWindow_getHeight(window)}:(VkExtent2D){0};
  refresh_rate = refresh > 0 ? refresh : 60;
  if (V.instance && native_window)
    vk_window_surface_create();
}
int vk_window_attached(void) { return native_window != NULL; }
double vk_window_refresh(void) { return native_window ? refresh_rate : 0; }
void vk_window_surface_create(void) {
  if (!native_window)
    return;
  VkAndroidSurfaceCreateInfoKHR ci = {
      .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
      .window = native_window};
  VK_CHECK(vkCreateAndroidSurfaceKHR(V.instance, &ci, NULL, &V.surface));
}
static void swapchain_create(void) {
  window_present_drain();
  // 不把后台/旧 Surface 的尾帧与新 Surface 首帧连成掉帧间隔。
  // Never bridge a background/old-Surface frame to the first new-Surface frame.
  pthread_mutex_lock(&timing_mu);
  memset(timings, 0, sizeof timings);
  pthread_mutex_unlock(&timing_mu);
  if (V.display_timing && !get_past_timings) {
    get_past_timings = (void *)vkGetDeviceProcAddr(
        V.device, "vkGetPastPresentationTimingGOOGLE");
    if (!get_past_timings)
      die("Android presentation timing entry missing");
  }
  VkSurfaceCapabilitiesKHR cap;
  VK_CHECK(
      vkGetPhysicalDeviceSurfaceCapabilitiesKHR(V.physical, V.surface, &cap));
  uint32_t n = 0;
  VK_CHECK(
      vkGetPhysicalDeviceSurfaceFormatsKHR(V.physical, V.surface, &n, NULL));
  if (!n)
    die("Android surface has no formats");
  VkSurfaceFormatKHR *formats = calloc(n, sizeof *formats);
  if (!formats)
    die("Vulkan surface formats allocation");
  VK_CHECK(
      vkGetPhysicalDeviceSurfaceFormatsKHR(V.physical, V.surface, &n, formats));
  VkSurfaceFormatKHR format = formats[0];
  for (uint32_t i = 0; i < n; i++)
    if (formats[i].format == VK_FORMAT_R8G8B8A8_UNORM ||
        formats[i].format == VK_FORMAT_B8G8R8A8_UNORM) {
      format = formats[i];
      break;
    }
  free(formats);
  swap_format = format.format;
  extent = cap.currentExtent;
  if (extent.width == UINT32_MAX) {
    extent.width = ANativeWindow_getWidth(native_window);
    extent.height = ANativeWindow_getHeight(native_window);
    if (extent.width < cap.minImageExtent.width)
      extent.width = cap.minImageExtent.width;
    if (extent.width > cap.maxImageExtent.width)
      extent.width = cap.maxImageExtent.width;
    if (extent.height < cap.minImageExtent.height)
      extent.height = cap.minImageExtent.height;
    if (extent.height > cap.maxImageExtent.height)
      extent.height = cap.maxImageExtent.height;
  }
  if (!extent.width || !extent.height)
    die("Android surface has zero extent");
  uint32_t mode_count=0;VkPresentModeKHR *modes=NULL;VkResult mode_result=VK_INCOMPLETE;
  for(unsigned attempt=0;attempt<4 && mode_result==VK_INCOMPLETE;attempt++) {
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(V.physical,V.surface,&mode_count,NULL));
    if(!mode_count)die("Android Surface has no presentation modes");
    free(modes);modes=calloc(mode_count,sizeof *modes);
    if(!modes)die("Vulkan presentation modes allocation");
    mode_result=vkGetPhysicalDeviceSurfacePresentModesKHR(V.physical,V.surface,&mode_count,modes);
  }
  VK_CHECK(mode_result);
  unsigned supported=0;
  for(unsigned k=0;k<mode_count;k++)if(modes[k]>=0 && modes[k]<=VK_PRESENT_MODE_FIFO_RELAXED_KHR)
    supported|=1u<<modes[k];
  free(modes);
  int requested=atomic_load(&mailbox_requested);
  VkPresentModeKHR present_mode=requested && (supported&(1u<<VK_PRESENT_MODE_MAILBOX_KHR))?
      VK_PRESENT_MODE_MAILBOX_KHR:VK_PRESENT_MODE_FIFO_KHR;
  atomic_store(&present_mode_supported,supported);
  uint32_t count = cap.minImageCount > 3 ? cap.minImageCount : 3;
  if (cap.maxImageCount && count > cap.maxImageCount)
    count = cap.maxImageCount;
  if (!(cap.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
    die("Android surface does not support transfer destination");
  if(shader_present_reserved<0) {
    atomic_store(&shader_present_automatic,V.props.vendorID==0x13b5 &&
        !strncmp(V.props.deviceName,"Mali-G710",9));
    shader_present_reserved=shader_present_mode();
  }
  VkImageUsageFlags next_usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  if(shader_present_reserved && format.format==VK_FORMAT_R8G8B8A8_UNORM &&
      (cap.supportedUsageFlags&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
    next_usage|=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  if (!(cap.supportedCompositeAlpha & alpha))
    alpha = cap.supportedCompositeAlpha & -cap.supportedCompositeAlpha;
  VkSwapchainCreateInfoKHR ci = {
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface = V.surface,
      .minImageCount = count,
      .imageFormat = format.format,
      .imageColorSpace = format.colorSpace,
      .imageExtent = extent,
      .imageArrayLayers = 1,
      .imageUsage = next_usage,
      .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .preTransform =
          (cap.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
              ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
              : cap.currentTransform,
      .compositeAlpha = alpha,
      .presentMode = present_mode,
      .clipped = VK_TRUE,
      .oldSwapchain = swapchain};
  VkSwapchainKHR next;
  VK_CHECK(vkCreateSwapchainKHR(V.device, &ci, NULL, &next));
  atomic_store(&present_mode_actual,present_mode);
  LOG("[vulkan WSI] supported_modes=%#x mailbox_requested=%d present_mode=%d image_count_requested=%u",
      supported,requested,present_mode,count);
  if (swapchain) {
    pthread_mutex_lock(&V.queue_mu);
    VK_CHECK(vkQueueWaitIdle(V.queue));
    pthread_mutex_unlock(&V.queue_mu);
    window_present_queue_idle();
    swap_shader_targets_destroy();
    vkDestroySwapchainKHR(V.device, swapchain, NULL);
  }
  swap_sync_destroy();
  swapchain = next;
  swap_usage=next_usage;
  VK_CHECK(vkGetSwapchainImagesKHR(V.device, swapchain, &swap_count, NULL));
  free(swap_images);
  swap_images = calloc(swap_count, sizeof *swap_images);
  if (!swap_images)
    die("Vulkan swapchain image allocation");
  VK_CHECK(
      vkGetSwapchainImagesKHR(V.device, swapchain, &swap_count, swap_images));
  if(swap_usage&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
    swap_shader_targets=calloc(swap_count,sizeof *swap_shader_targets);
    swap_shader_presented=calloc(swap_count,sizeof *swap_shader_presented);
    if(!swap_shader_targets || !swap_shader_presented)die("Vulkan present shader target allocation");
    swap_shader_target_count=swap_count;
    for(uint32_t n=0;n<swap_count;n++)vk_present_shader_target_create(&swap_shader_targets[n],swap_images[n],extent);
    atomic_store(&shader_present_available,1);
  }
  if (vk_async_submissions()) {
    present_finished = calloc(swap_count, sizeof *present_finished);
    if (!present_finished) die("Vulkan present semaphore allocation");
    present_finished_count = swap_count;
    VkSemaphoreCreateInfo info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    // present 的 wait 信号量按交换链图像持有；提交 fence 完成不代表 WSI 已消费 wait。
    // Present wait semaphores belong to swapchain images. Submit completion
    // alone does not prove WSI has consumed its wait.
    for (uint32_t i = 0; i < swap_count; i++)
      VK_CHECK(vkCreateSemaphore(V.device, &info, NULL, &present_finished[i]));
    if(V.present_worker && V.present_queue!=V.queue) {
      present_bridge=calloc(swap_count,sizeof *present_bridge);
      if(!present_bridge)die("Vulkan present bridge semaphore allocation");
      for(uint32_t i=0;i<swap_count;i++)
        VK_CHECK(vkCreateSemaphore(V.device,&info,NULL,&present_bridge[i]));
    }
    for (unsigned i = 0; i < 3; i++)
      VK_CHECK(vkCreateSemaphore(V.device, &info, NULL, &acquire_slots[i].semaphore));
  }
  atomic_store(&window_present_error,VK_SUCCESS);
  LOG("[vulkan] presentation overlap: %s", vk_async_submissions() ? "enabled" : "serial reference");
  if (!acquired) {
    VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK_CHECK(vkCreateFence(V.device, &fi, NULL, &acquired));
  }
  LOG("[vulkan] Android Surface %ux%u images=%u format=%u refresh=%.2f "
      "transform=%u/%u",
      extent.width, extent.height, swap_count, swap_format, refresh_rate,
      ci.preTransform, cap.currentTransform);
}
static VkResult acquire_image_immediate(uint32_t *index, VkSemaphore semaphore) {
  // 后台存盘期间须继续游戏拍；不能永远卡在隐藏 Surface 的取图上。
  // Background saving needs game ticks to continue. Never wait indefinitely
  // for an image from a hidden Surface.
  int acquire_trace=vk_system_trace_begin("Vulkan swapchain acquire");
  while (!vk_window_discarding()) {
    VkResult result = vkAcquireNextImageKHR(V.device, swapchain, 16000000,
                                           semaphore, semaphore ? VK_NULL_HANDLE : acquired, index);
    if (result != VK_TIMEOUT && result != VK_NOT_READY) { vk_system_trace_end(acquire_trace);return result; }
  }
  vk_system_trace_end(acquire_trace);return VK_NOT_READY;
}
uint64_t vk_window_present(const void *texture, const int crop[4], uint64_t sequence) {
  if (vk_window_discarding()) { vk_renderdoc_present(V.instance, 1); vk_window_drop_present(sequence); return 0; }
  const tex_t *t = texture;
  int x = crop ? crop[0] : 0, y = crop ? crop[1] : 0,
      w = crop && crop[2] > 0 ? crop[2] : t->d.width,
      h = crop && crop[3] > 0 ? crop[3] : t->d.height;
  if (x < 0 || y < 0 || w <= 0 || h <= 0 || (uint64_t)x + w > t->d.width ||
      (uint64_t)y + h > t->d.height)
    die("Android present crop invalid");
  int geometry_changed=0;
  if(native_geometry_enabled() && !native_geometry_failed &&
      (native_geometry_requested.width!=(uint32_t)w || native_geometry_requested.height!=(uint32_t)h)) {
    int result=ANativeWindow_setBuffersGeometry(native_window,w,h,0);
    if(result) {
      // A platform rejection keeps the ordinary View buffer path. Do not
      // retry the same rejected operation on every present.
      native_geometry_failed=1;
      if(ANativeWindow_setBuffersGeometry(native_window,0,0,0))
        die("Android native buffer geometry default restoration failed");
    } else native_geometry_requested=(VkExtent2D){w,h};
    LOG("[vulkan present] native buffer geometry request=%dx%d result=%d view=%ux%u",w,h,result,view_extent.width,view_extent.height);
    geometry_changed=1;
  }
  if (!swapchain || geometry_changed)
    swapchain_create();
  int overlap = vk_async_submissions();
  unsigned slot = overlap ? acquire_slot() : 0;
  uint32_t index;
  VkResult result = window_acquire(&index, overlap ? acquire_slots[slot].semaphore : VK_NULL_HANDLE);
  if (vk_window_discarding() && (result == VK_NOT_READY ||
      result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR)) {
    vk_renderdoc_present(V.instance, 1); vk_window_drop_present(sequence); return 0;
  }
  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    if (vk_window_discarding()) { vk_renderdoc_present(V.instance, 1); vk_window_drop_present(sequence); return 0; }
    swapchain_create();
    slot = overlap ? acquire_slot() : 0;
    result = window_acquire(&index, overlap ? acquire_slots[slot].semaphore : VK_NULL_HANDLE);
  }
  if (vk_window_discarding() && (result == VK_NOT_READY ||
      result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR)) {
    vk_renderdoc_present(V.instance, 1); vk_window_drop_present(sequence); return 0;
  }
  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    die("Android image acquire: %d", result);
  if (!overlap) {
    VK_CHECK(vkWaitForFences(V.device, 1, &acquired, VK_TRUE, UINT64_MAX));
    VK_CHECK(vkResetFences(V.device, 1, &acquired));
  }
  mtl_sub_begin();
  VkImageLayout source_layout=VK_IMAGE_LAYOUT_UNDEFINED;
  VkImage source=VK_NULL_HANDLE;
  VkImage dst = swap_images[index];
  double scale = fmin((double)extent.width / w, (double)extent.height / h);
  int dw = lround(w * scale), dh = lround(h * scale),
      dx = ((int)extent.width - dw) / 2, dy = ((int)extent.height - dh) / 2;
  if(native_geometry_requested.width && !native_geometry_failed && view_extent.width && view_extent.height) {
    // Composition scales the whole buffer to the View. Keep the original
    // View-space letterbox and normalized touch mapping, even if game and
    // View aspect ratios differ. This changes buffer pixels, not layout.
    double visual=fmin((double)view_extent.width/w,(double)view_extent.height/h);
    int vw=lround(w*visual),vh=lround(h*visual);
    // Output dimensions rounded by the guest can leave a one-pixel margin
    // on each View edge. Allow that negligible aspect correction in the
    // native-size candidate: otherwise a two-row border clears the ENTIRE
    // swap image and adds an overlapping transfer barrier every frame.
    // Larger letterboxes keep the original aspect and input rectangle.
    if((int)view_extent.width-vw<=2 && (int)view_extent.height-vh<=2) {
      vw=view_extent.width;vh=view_extent.height;
    }
    int vx=((int)view_extent.width-vw)/2,vy=((int)view_extent.height-vh)/2;
    dx=lround((double)vx*extent.width/view_extent.width);
    dy=lround((double)vy*extent.height/view_extent.height);
    dw=lround((double)vw*extent.width/view_extent.width);
    dh=lround((double)vh*extent.height/view_extent.height);
    if(dx+dw>(int)extent.width)dw=extent.width-dx;
    if(dy+dh>(int)extent.height)dh=extent.height-dy;
  }
  if(geometry_changed)
    LOG("[vulkan present] native buffer applied=%ux%u content=%d,%d %dx%d",extent.width,extent.height,dx,dy,dw,dh);
  int full=!x && !y && w==t->d.width && h==t->d.height &&
      !dx && !dy && dw==w && dh==h && (uint32_t)w==extent.width && (uint32_t)h==extent.height;
  vk_present_sample sampled;
  int shader=shader_present_mode() && swap_shader_targets &&
      (swap_usage&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) && full &&
      vk_texture_present_sample(t,swap_format,extent,&sampled);
  if(shader)atomic_fetch_add(&shader_present_selected,1);
  else {
    if(shader_present_mode())atomic_fetch_add(&shader_present_fallback,1);
    source=vk_texture_present_source(t,&source_layout);
    // Partial crops need an independent clamp boundary for linear filtering.
    if(x || y || w!=t->d.width || h!=t->d.height) {
      source=crop_source(source,source_layout,t,x,y,w,h);
      source_layout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }
  }
  VkPipelineStageFlags destination_stage=shader?VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT:VK_PIPELINE_STAGE_TRANSFER_BIT;
  VkImageLayout destination_layout=shader?VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  VkAccessFlags destination_access=shader?VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT:VK_ACCESS_TRANSFER_WRITE_BIT;
  // 输入只用真实出图矩形换算，不能把两侧黑边当成世界坐标。
  // Input uses the actual blit rectangle; letterbox margins are not world.
  pthread_mutex_lock(&timing_mu);
  content_rect[0] = (float)dx / extent.width;
  content_rect[1] = (float)dy / extent.height;
  content_rect[2] = (float)dw / extent.width;
  content_rect[3] = (float)dh / extent.height;
  pthread_mutex_unlock(&timing_mu);
  VkImageMemoryBarrier barrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .oldLayout = shader && swap_shader_presented[index]?VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:VK_IMAGE_LAYOUT_UNDEFINED,
      .newLayout = destination_layout,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = dst,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
      .dstAccessMask = destination_access};
  // Acquire is waited at the actual destination stage. The transition must be
  // ordered after that wait too; TOP_OF_PIPE relied on the old global begin
  // barrier and can otherwise run before acquisition has completed.
  vkCmdPipelineBarrier(V.cmd, destination_stage,
                       destination_stage, 0, 0, NULL, 0, NULL, 1,
                       &barrier);
  // 完整覆盖时 blit 会写出全部像素；黑边仍须初始化，不能省略。
  // A full blit overwrites every destination pixel; letterbox margins still
  // require initialization.
  if (dx || dy || dw != (int)extent.width || dh != (int)extent.height) {
    VkClearColorValue black = {.float32 = {0, 0, 0, 1}};
    vkCmdClearColorImage(V.cmd, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black,
                         1, &barrier.subresourceRange);
    // The source dependency was established by the typed image lookup. Only
    // overlapping clear/blit writes to this swap image need ordering here.
    VkImageMemoryBarrier cleared=barrier;
    cleared.oldLayout=cleared.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    cleared.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,0,NULL,0,NULL,1,&cleared);
  }
  if(shader)vk_present_shader_render(&sampled,&swap_shader_targets[index]);
  else {
  VkImageBlit blit = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                      .srcOffsets = {{0, 0, 0}, {w, h, 1}},
                      .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                      .dstOffsets = {{dx, dy, 0}, {dx + dw, dy + dh, 1}}};
  vkCmdBlitImage(V.cmd, source, source_layout, dst,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                 VK_FILTER_LINEAR);
  }
  barrier.oldLayout = destination_layout;
  barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  barrier.srcAccessMask = destination_access;
  barrier.dstAccessMask = 0;
  vkCmdPipelineBarrier(V.cmd, destination_stage,
                       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0,
                       NULL, 1, &barrier);
  uint64_t ticket;
  if (overlap) {
    ticket = vk_internal_commit_semaphores(acquire_slots[slot].semaphore,
        destination_stage, present_finished[index]);
    acquire_slots[slot].serial = ticket;
  } else {
    ticket = vk_internal_commit();
    vk_internal_wait(ticket);
  }
  // Publish the future layout only after successful queue submission. The next
  // acquired use waits on WSI before transitioning this same image again.
  if(swap_shader_presented)swap_shader_presented[index]=1;
  window_wsi_request request={.kind=WINDOW_WSI_PRESENT,.chain=swapchain,.index=index,
    .semaphore=overlap?present_finished[index]:VK_NULL_HANDLE,.sequence=sequence,
    .game_width=(uint32_t)w,.game_height=(uint32_t)h,.surface_width=extent.width,.surface_height=extent.height};
  if(V.present_worker) {
    window_wsi_enqueue(request);
    return ticket; // NVN source texture's blit is already submitted in queue order.
  }
  result=window_present_immediate(&request,V.queue);
  if (result == VK_ERROR_OUT_OF_DATE_KHR ||
      (result == VK_ERROR_SURFACE_LOST_KHR && vk_window_discarding())) {
    if (!vk_window_discarding()) swapchain_create();
    vk_window_drop_present(sequence);
  } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    die("Android present: %d", result);
  vk_renderdoc_present(V.instance, vk_window_discarding());
  return overlap ? ticket : 0;
}
#else
void vk_android_window(void *window, float refresh) {
  (void)window;
  (void)refresh;
  die("Android window on non-Android host");
}
int vk_window_attached(void) { return 0; }
double vk_window_refresh(void) { return 0; }
int vk_window_surface_point(float world_x, float world_y, float *x, float *y) {
  (void)x;
  (void)y;
  (void)world_x;
  (void)world_y;
  return 0;
}
int vk_window_world_point(float x, float y, float *world_x, float *world_y) {
  (void)x;
  (void)y;
  (void)world_x;
  (void)world_y;
  return 0;
}
void vk_window_surface_create(void) {}
uint64_t vk_window_present(const void *texture, const int crop[4], uint64_t sequence) {
  (void)sequence;
  (void)texture;
  (void)crop;
  die("Android present on non-Android host");
}
#endif
