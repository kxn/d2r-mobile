// 明确指定 Vulkan 实例的官方 RenderDoc 捕获接口，不包含计时器或采样器。
// Official RenderDoc capture API bound to our Vulkan instance; no profiler.
#include "nvn_vk_internal.h"
#include "nvn_vk_renderdoc.h"
#ifdef DM_RENDERDOC_API
#include <stdatomic.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <unistd.h>
#include <renderdoc_app.h>
static pthread_mutex_t request_mu = PTHREAD_MUTEX_INITIALIZER;
static _Atomic int requested;
static char capture_directory[1024];
static RENDERDOC_API_1_6_0 *api;
static RENDERDOC_DevicePointer capture_device;
static int capturing;
static unsigned sequence;

int vk_renderdoc_request(const char *directory) {
  if (!directory || directory[0] != '/' || strlen(directory) >= sizeof capture_directory) return 0;
  pthread_mutex_lock(&request_mu);
  int accepted = !atomic_load(&requested);
  if (accepted) {
    strlcpy(capture_directory, directory, sizeof capture_directory);
    atomic_store(&requested, 1);
  }
  pthread_mutex_unlock(&request_mu);
  return accepted;
}

void vk_renderdoc_present(VkInstance instance, int discarded) {
  // Called only by the presentation worker, after its queue lock is released.
  if (capturing) {
    if (discarded) {
      api->DiscardFrameCapture(capture_device, NULL);
      LOG("[renderdoc] capture discarded: presentation suspended");
    } else {
      LOG("[renderdoc] capture ending api=Vulkan");
      uint32_t ok = api->EndFrameCapture(capture_device, NULL);
      uint32_t count = api->GetNumCaptures();
      char path[2048] = {0}; uint32_t size = sizeof path;
      if (count) api->GetCapture(count - 1, path, &size, NULL);
      LOG("[renderdoc] capture ended=%u captures=%u file=%s", ok, count, path);
    }
    capturing = 0;
  }
  if (discarded || !atomic_load(&requested)) return;
  pthread_mutex_lock(&request_mu);
  char path[1200];
  snprintf(path, sizeof path, "%s/native-%d-%u", capture_directory, getpid(), ++sequence);
  atomic_store(&requested, 0);
  pthread_mutex_unlock(&request_mu);
  if (!api) {
    void *library = dlopen("libVkLayer_GLES_RenderDoc.so", RTLD_NOW | RTLD_NOLOAD);
    pRENDERDOC_GetAPI get = library ? (pRENDERDOC_GetAPI)dlsym(library, "RENDERDOC_GetAPI") : NULL;
    if (!get || !get(eRENDERDOC_API_Version_1_6_0, (void **)&api)) {
      LOG("[renderdoc] capture rejected: official injected layer is absent"); return;
    }
  }
  if (api->IsFrameCapturing()) {
    LOG("[renderdoc] capture rejected: another capture is active"); return;
  }
  api->SetCaptureOptionU32(eRENDERDOC_Option_APIValidation, 0);
  api->SetCaptureOptionU32(eRENDERDOC_Option_RefAllResources, 0);
  api->SetCaptureOptionU32(eRENDERDOC_Option_SoftMemoryLimit, 256);
  api->SetCaptureFilePathTemplate(path);
  capture_device = RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(instance);
  LOG("[renderdoc] capture starting api=Vulkan explicit_instance=1 file_template=%s", path);
  api->StartFrameCapture(capture_device, NULL);
  capturing = api->IsFrameCapturing();
  LOG("[renderdoc] capture started=%d api=Vulkan explicit_instance=1 file_template=%s", capturing, path);
}
#else
int vk_renderdoc_request(const char *directory) { (void)directory; return 0; }
void vk_renderdoc_present(VkInstance instance, int discarded) { (void)instance; (void)discarded; }
#endif
