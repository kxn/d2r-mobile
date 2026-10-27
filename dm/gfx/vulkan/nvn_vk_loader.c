#include "nvn_vk_loader.h"
#include "../../host/dm.h"
#include <dlfcn.h>
#include <pthread.h>
#include <limits.h>
#include <sys/stat.h>
#include <errno.h>
#ifdef DM_VULKAN_NATIVE_RESOURCES
#include "mbedtls/sha256.h"
#include <fcntl.h>
#include <unistd.h>
static uint8_t native_driver_digest[32];
static int native_driver_digest_valid;
static void native_driver_hash(const char *path) {
  int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);struct stat before,after;
  if(fd<0 || fstat(fd,&before))die("Native resource driver ELF open");
  mbedtls_sha256_context hash;mbedtls_sha256_init(&hash);mbedtls_sha256_starts(&hash,0);
  uint8_t data[16384];
  for(;;) {ssize_t n=read(fd,data,sizeof data);if(n<0 && errno==EINTR)continue;
    if(n<0)die("Native resource driver ELF read");if(!n)break;mbedtls_sha256_update(&hash,data,(size_t)n);}
  if(fstat(fd,&after) || before.st_size!=after.st_size || before.st_mtime!=after.st_mtime || close(fd))
    die("Native resource driver ELF changed");
  mbedtls_sha256_finish(&hash,native_driver_digest);mbedtls_sha256_free(&hash);native_driver_digest_valid=1;
}
#endif
int vk_loader_native_driver_digest(uint8_t out[32]) {
#ifdef DM_VULKAN_NATIVE_RESOURCES
  if(native_driver_digest_valid && out) {memcpy(out,native_driver_digest,32);return 1;}
#else
  (void)out;
#endif
  return 0;
}
#ifdef __ANDROID__
#include "../../third_party/libadrenotools/include/adrenotools/driver.h"
#endif

static pthread_mutex_t loader_mutex=PTHREAD_MUTEX_INITIALIZER;
static char driver_directory[PATH_MAX],hook_directory[PATH_MAX];
static int initialized;
enum driver_provider { DRIVER_UNSET,DRIVER_SYSTEM,DRIVER_ADRENO,DRIVER_PANVK };
static enum driver_provider provider;
// Keep both handles alive for the entire Vulkan instance/device lifetime.
static void *driver_tools,*custom_loader;

static void directory_copy(char *dst,const char *src) {
  if(!src || !*src) { dst[0]=0;return; }
  if(src[0]!='/' || strlen(src)>PATH_MAX-2) die("Vulkan driver requires an absolute directory");
  strcpy(dst,src);
  size_t n=strlen(dst);
  if(dst[n-1]!='/') { dst[n]='/';dst[n+1]=0; }
}
static enum driver_provider parse_provider(const char *name) {
  if(!name) return DRIVER_UNSET;
  if(!strcmp(name,"system")) return DRIVER_SYSTEM;
  if(!strcmp(name,"adreno")) return DRIVER_ADRENO;
  if(!strcmp(name,"panvk")) return DRIVER_PANVK;
  die("Vulkan unknown driver provider: %s",name);
}
void vk_loader_set_driver(const char *name,const char *directory,const char *hooks) {
  pthread_mutex_lock(&loader_mutex);
  if(!initialized) {
    provider=parse_provider(name);
    directory_copy(driver_directory,directory);
    directory_copy(hook_directory,hooks);
  }
  // Activity recreation must retain the already active device and loader.
  // Driver selection is startup-only; onNewIntent never changes it.
  pthread_mutex_unlock(&loader_mutex);
}
// Existing cold-state accessor also used by the frozen ordinary-UID fixtures.
int vk_loader_is_initialized(void) {
  pthread_mutex_lock(&loader_mutex);int active=initialized;
  pthread_mutex_unlock(&loader_mutex);return active;
}
static void library_path(char *out,const char *directory,const char *name) {
  char canonical[PATH_MAX];
  struct stat st;
  if(!directory[0] || !realpath(directory,canonical) || stat(canonical,&st) || !S_ISDIR(st.st_mode))
    die("Vulkan driver directory unavailable: %s",directory);
  if(snprintf(out,PATH_MAX,"%s/%s",canonical,name)>=PATH_MAX)
    die("Vulkan driver library path too long");
  if(lstat(out,&st) || !S_ISREG(st.st_mode))
    die("Vulkan driver requires regular library: %s",out);
}
static void panvk_shader_cache(const char *library) {
  const char *configured=getenv("MESA_SHADER_CACHE_DIR");
  if(configured) {
    LOG("[vulkan loader] PanVK shader cache explicit=%s",configured);
    return;
  }
  const char *name=strrchr(library,'/');
  char path[PATH_MAX];
  if(!name || snprintf(path,sizeof path,"%.*s/shader-cache",(int)(name-library),library)>=(int)sizeof path)
    die("Vulkan PanVK shader cache path too long");
  if(mkdir(path,0700) && errno!=EEXIST)
    LOG("[vulkan loader] PanVK shader cache directory creation failed: %s; Mesa will evaluate the private path",strerror(errno));
  if(setenv("MESA_SHADER_CACHE_DIR",path,0)) die("Vulkan PanVK shader cache environment allocation");
  LOG("[vulkan loader] PanVK shader cache driver-private default=%s",path);
}
void vk_loader_initialize(void) {
  pthread_mutex_lock(&loader_mutex);
  if(initialized) { pthread_mutex_unlock(&loader_mutex);return; }
  if(provider==DRIVER_UNSET) {
    provider=parse_provider(getenv("DM_VULKAN_DRIVER_KIND"));
    if(provider!=DRIVER_SYSTEM && !driver_directory[0])
      directory_copy(driver_directory,getenv("DM_VULKAN_DRIVER_ROOT"));
    if(provider==DRIVER_UNSET) provider=driver_directory[0]?DRIVER_ADRENO:DRIVER_SYSTEM;
  }
  if(provider==DRIVER_ADRENO || provider==DRIVER_PANVK) {
#ifdef __ANDROID__
    char library[PATH_MAX];
    if(provider==DRIVER_PANVK) {
      if(getenv("DM_VULKAN_VALIDATE"))
        die("Vulkan direct ICD does not compose loader validation layers");
      library_path(library,driver_directory,"libvulkan_panfrost.so");
      panvk_shader_cache(library);
      custom_loader=dlopen(library,RTLD_NOW|RTLD_LOCAL);
      if(!custom_loader) die("Vulkan PanVK ICD load: %s",dlerror());
      PFN_vkGetInstanceProcAddr get_proc=(PFN_vkGetInstanceProcAddr)dlsym(custom_loader,"vk_icdGetInstanceProcAddr");
      if(!get_proc) get_proc=(PFN_vkGetInstanceProcAddr)dlsym(custom_loader,"vkGetInstanceProcAddr");
      if(!get_proc) die("Vulkan PanVK ICD entrypoint missing");
      Dl_info owner;
      char owner_path[PATH_MAX];
      if(!dladdr((const void *)get_proc,&owner) || !owner.dli_fname ||
         !realpath(owner.dli_fname,owner_path) || strcmp(owner_path,library))
        die("Vulkan PanVK entrypoint belongs to a different library");
      if(!get_proc(VK_NULL_HANDLE,"vkCreateInstance") ||
         !get_proc(VK_NULL_HANDLE,"vkEnumerateInstanceExtensionProperties"))
        die("Vulkan PanVK global dispatch incomplete");
      volkInitializeCustom(get_proc);
#ifdef DM_VULKAN_NATIVE_RESOURCES
      if(getenv("DM_VULKAN_NATIVE_RESOURCE_TABLE"))native_driver_hash(library);
#endif
      LOG("[vulkan loader] provider=panvk direct ICD=%s (dispatch only; GPU not validated)",library);
    } else {
      if(!hook_directory[0]) directory_copy(hook_directory,getenv("DM_VULKAN_DRIVER_HOOKS"));
      if(!hook_directory[0]) die("Vulkan custom driver hook directory missing");
      library_path(library,hook_directory,"libadrenotools.so");
      char driver_path[PATH_MAX];
      library_path(driver_path,driver_directory,"libvulkan_freedreno.so");
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
#ifdef DM_VULKAN_NATIVE_RESOURCES
      if(getenv("DM_VULKAN_NATIVE_RESOURCE_TABLE"))native_driver_hash(driver_path);
#endif
      LOG("[vulkan loader] custom=%slibvulkan_freedreno.so",driver_directory);
      const char *swapchain_ubwc=getenv("TU_UBWC_SWAPCHAIN");
      LOG("[vulkan loader] Turnip swapchain UBWC startup=%s",swapchain_ubwc?swapchain_ubwc:"external");
    }
#else
    die("Application-local Vulkan driver loading requires Android");
#endif
  } else {
    VkResult result=volkInitialize();
    if(result!=VK_SUCCESS) die("Vulkan system loader unavailable: %d",result);
    LOG("[vulkan loader] system");
  }
  initialized=1;
  pthread_mutex_unlock(&loader_mutex);
}
void vk_loader_check_instance(VkInstance instance,int android_surface) {
  if(provider!=DRIVER_PANVK) return;
  if(!vkGetInstanceProcAddr(instance,"vkGetPhysicalDeviceProperties2") ||
     !vkGetInstanceProcAddr(instance,"vkGetPhysicalDeviceQueueFamilyProperties"))
    die("Vulkan PanVK instance dispatch incomplete");
  if(android_surface && (!vkGetInstanceProcAddr(instance,"vkCreateAndroidSurfaceKHR") ||
      !vkGetInstanceProcAddr(instance,"vkDestroySurfaceKHR") ||
      !vkGetInstanceProcAddr(instance,"vkGetPhysicalDeviceSurfaceSupportKHR") ||
      !vkGetInstanceProcAddr(instance,"vkGetPhysicalDeviceSurfaceCapabilitiesKHR") ||
      !vkGetInstanceProcAddr(instance,"vkGetPhysicalDeviceSurfaceFormatsKHR") ||
      !vkGetInstanceProcAddr(instance,"vkGetPhysicalDeviceSurfacePresentModesKHR")))
    die("Vulkan PanVK Android surface dispatch incomplete");
  LOG("[vulkan loader] PanVK instance dispatch present; Android surface requested=%d",android_surface);
}
void vk_loader_check_device(VkPhysicalDevice physical,int driver_properties) {
  if(provider!=DRIVER_PANVK) return;
  if(!driver_properties) die("Vulkan PanVK driver identity properties unavailable");
  VkPhysicalDeviceDriverProperties driver={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
  VkPhysicalDeviceProperties2 props={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&driver};
  vkGetPhysicalDeviceProperties2(physical,&props);
  if(driver.driverID!=VK_DRIVER_ID_MESA_PANVK) die("Vulkan selected ICD is not Mesa PanVK (driverID=%u)",driver.driverID);
  LOG("[vulkan loader] provider=panvk device=%s vendor=%#x driverID=%u driverVersion=%u name=%s info=%s",
      props.properties.deviceName,props.properties.vendorID,driver.driverID,
      props.properties.driverVersion,driver.driverName,driver.driverInfo);
}
