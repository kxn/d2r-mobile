#include "nvn_vk_loader.h"
#include "../../host/dm.h"
#include <dlfcn.h>
#include <pthread.h>
#include <limits.h>
#ifdef __ANDROID__
#include "../../third_party/libadrenotools/include/adrenotools/driver.h"
#endif

static pthread_mutex_t loader_mutex=PTHREAD_MUTEX_INITIALIZER;
static char driver_directory[PATH_MAX],hook_directory[PATH_MAX];
static int initialized;
// Keep both handles alive for the entire Vulkan instance/device lifetime.
static void *driver_tools,*custom_loader;

static void directory_copy(char *dst,const char *src) {
  if(!src || !*src) { dst[0]=0;return; }
  if(src[0]!='/' || strlen(src)>PATH_MAX-2) die("Vulkan driver requires an absolute directory");
  strcpy(dst,src);
  size_t n=strlen(dst);
  if(dst[n-1]!='/') { dst[n]='/';dst[n+1]=0; }
}
void vk_loader_set_driver(const char *directory,const char *hooks) {
  pthread_mutex_lock(&loader_mutex);
  if(!initialized) {
    directory_copy(driver_directory,directory);
    directory_copy(hook_directory,hooks);
  }
  // Activity recreation must retain the already active device and loader.
  // Driver selection is startup-only; onNewIntent never changes it.
  pthread_mutex_unlock(&loader_mutex);
}
void vk_loader_initialize(void) {
  pthread_mutex_lock(&loader_mutex);
  if(initialized) { pthread_mutex_unlock(&loader_mutex);return; }
  if(!driver_directory[0]) directory_copy(driver_directory,getenv("DM_VULKAN_DRIVER_ROOT"));
  if(!hook_directory[0]) directory_copy(hook_directory,getenv("DM_VULKAN_DRIVER_HOOKS"));
  if(driver_directory[0]) {
#ifdef __ANDROID__
    if(!hook_directory[0]) die("Vulkan custom driver hook directory missing");
    char library[PATH_MAX];
    if(snprintf(library,sizeof library,"%slibadrenotools.so",hook_directory)>=(int)sizeof library)
      die("Vulkan driver tools path too long");
    driver_tools=dlopen(library,RTLD_NOW|RTLD_LOCAL);
    if(!driver_tools) die("Vulkan driver tools load: %s",dlerror());
    typedef void *(*open_fn)(int,int,const char *,const char *,const char *,const char *,const char *,void **);
    open_fn open_driver=(open_fn)dlsym(driver_tools,"adrenotools_open_libvulkan");
    if(!open_driver) die("Vulkan driver tools entrypoint missing");
    custom_loader=open_driver(RTLD_NOW|RTLD_LOCAL,ADRENOTOOLS_DRIVER_CUSTOM,NULL,
        hook_directory,driver_directory,"libvulkan_freedreno.so",NULL,NULL);
    if(!custom_loader) die("Vulkan custom driver load failed: %s",dlerror());
    PFN_vkGetInstanceProcAddr get_proc=(PFN_vkGetInstanceProcAddr)dlsym(custom_loader,"vkGetInstanceProcAddr");
    if(!get_proc) die("Vulkan custom loader entrypoint missing");
    volkInitializeCustom(get_proc);
    LOG("[vulkan loader] custom=%slibvulkan_freedreno.so",driver_directory);
#else
    die("Application-local Adreno driver loading requires Android");
#endif
  } else {
    VkResult result=volkInitialize();
    if(result!=VK_SUCCESS) die("Vulkan system loader unavailable: %d",result);
    LOG("[vulkan loader] system");
  }
  initialized=1;
  pthread_mutex_unlock(&loader_mutex);
}
