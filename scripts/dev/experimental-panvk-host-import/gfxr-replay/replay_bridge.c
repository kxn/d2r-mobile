/* Debug-only loader for the unchanged official GFXR Android replay library.
 * Reuse the already validated DRIVER_CUSTOM/system-loader route. No raw ICD
 * dispatch aliases, GPU_MAPPING_IMPORT, validation logic or new renderer. */
#define _GNU_SOURCE
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <jni.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <fcntl.h>
static char hook_dir[PATH_MAX],driver_path[PATH_MAX],root_dir[PATH_MAX];
static _Atomic int configured;
static pthread_once_t once=PTHREAD_ONCE_INIT;
static PFN_vkGetInstanceProcAddr real_gipa;
static void *tools_handle,*loader_handle;
static _Atomic(PFN_vkEnumeratePhysicalDevices) real_enumerate;
static _Atomic(PFN_vkCreateDevice) real_create_device;
static _Atomic(PFN_vkEnumerateDeviceExtensionProperties) real_extensions;
__attribute__((visibility("default"))) VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance,const char *);
static void fail(const char *text) {fprintf(stderr,"[replay bridge] FATAL %s\n",text);fflush(stderr);abort();}
static void path_join(char *out,const char *base,const char *tail) {
  if(snprintf(out,PATH_MAX,"%s/%s",base,tail)>=PATH_MAX)fail("path overflow");
}
static void load_system(void) {
  if(!atomic_load_explicit(&configured,memory_order_acquire))fail("cold configuration missing");
  char lib[PATH_MAX],dir[PATH_MAX],hooks[PATH_MAX];path_join(lib,hook_dir,"libadrenotools.so");
  tools_handle=dlopen(lib,RTLD_NOW|RTLD_LOCAL);if(!tools_handle)fail(dlerror());
  typedef void *(*open_fn)(int,int,const char *,const char *,const char *,const char *,const char *,void **);
  open_fn open_driver=(open_fn)dlsym(tools_handle,"adrenotools_open_libvulkan");if(!open_driver)fail("adrenotools entry");
  strcpy(dir,driver_path);char *end=strrchr(dir,'/');if(!end)fail("driver path");end[1]=0;
  /* Match the existing project loader directory_copy contract: adrenotools
   * driver.cpp concatenates customDriverDir + customDriverName directly. */
  if(snprintf(hooks,sizeof hooks,"%s/",hook_dir)>=(int)sizeof hooks)fail("hook directory length");
  struct stat input;if(stat(driver_path,&input)||!S_ISREG(input.st_mode))fail("private ICD input absent");
  /* ADRENOTOOLS_DRIVER_CUSTOM=1 only; never GPU_MAPPING_IMPORT. */
  loader_handle=open_driver(RTLD_NOW|RTLD_LOCAL,1,NULL,hooks,dir,"libvulkan_panfrost.so",NULL,NULL);
  if(!loader_handle)fail("custom standard-loader initialization failed");
  real_gipa=(PFN_vkGetInstanceProcAddr)dlsym(loader_handle,"vkGetInstanceProcAddr");
  if(!real_gipa || real_gipa==vkGetInstanceProcAddr)fail("standard loader GIPA absent/recursive");
  fprintf(stderr,"[replay bridge] standard loader initialized; DRIVER_CUSTOM only, GPU_MAPPING_IMPORT=off\n");
}
static VKAPI_ATTR VkResult VKAPI_CALL enumerate(VkInstance instance,uint32_t *count,VkPhysicalDevice *devices) {
  PFN_vkEnumeratePhysicalDevices fn=atomic_load_explicit(&real_enumerate,memory_order_acquire);if(!fn)fail("enumerate dispatch");
  VkResult result=fn(instance,count,devices);
  if((result==VK_SUCCESS||result==VK_INCOMPLETE)&&devices&&count&&*count) {
    PFN_vkGetPhysicalDeviceProperties2 props2=(PFN_vkGetPhysicalDeviceProperties2)real_gipa(instance,"vkGetPhysicalDeviceProperties2");
    if(!props2)fail("physical driver properties2 unavailable");
    for(uint32_t n=0;n<*count;n++) {
      VkPhysicalDeviceDriverProperties driver={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
      VkPhysicalDeviceProperties2 props={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&driver};props2(devices[n],&props);
      if(driver.driverID!=VK_DRIVER_ID_MESA_PANVK)fail("system fallback selected, not PanVK ID20");
      struct stat st;if(stat(driver_path,&st)||!S_ISREG(st.st_mode))fail("private driver disappeared");
      FILE *file=fopen("/proc/self/maps","r");if(!file)fail("maps unavailable");
      char line[8192],mode[5];unsigned ma,mi;unsigned long inode,lo,hi,off;int found=0;
      while(fgets(line,sizeof line,file))if(sscanf(line,"%lx-%lx %4s %lx %x:%x %lu",&lo,&hi,mode,&off,&ma,&mi,&inode)==7&&strchr(mode,'x')&&inode==(unsigned long)st.st_ino&&ma==major(st.st_dev)&&mi==minor(st.st_dev))found=1;
      int error=ferror(file);if(fclose(file)||error||!found)fail("exact private ICD executable inode not mapped");
      fprintf(stderr,"[replay bridge] identity verified driverID=%u device=%s driver=%s info=%s private_inode=%llu\n",driver.driverID,props.properties.deviceName,driver.driverName,driver.driverInfo,(unsigned long long)st.st_ino);
    }
  }
  return result;
}
#include "device_dependency.inc"
static VKAPI_ATTR VkResult VKAPI_CALL create_device(VkPhysicalDevice physical,const VkDeviceCreateInfo *info,const VkAllocationCallbacks *allocator,VkDevice *device) {
  PFN_vkCreateDevice fn=atomic_load_explicit(&real_create_device,memory_order_acquire);if(!fn)fail("create-device dispatch");
  VkDeviceCreateInfo corrected;const char **owned;int change=replay_device_dependency(info,&corrected,&owned);if(change<0)fail("device extension dependency input/allocation");
  if(change) {
    PFN_vkEnumerateDeviceExtensionProperties ex=atomic_load_explicit(&real_extensions,memory_order_acquire);if(!ex)fail("extension dispatch");
    uint32_t count=0;if(ex(physical,NULL,&count,NULL)!=VK_SUCCESS||count>4096)fail("extension query");
    VkExtensionProperties *all=calloc(count,sizeof *all);if(!all)fail("extension query allocation");
    if(ex(physical,NULL,&count,all)!=VK_SUCCESS)fail("extension enumeration");
    int found=0;for(uint32_t n=0;n<count;n++)found|=!strcmp(all[n].extensionName,VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME);free(all);
    if(!found)fail("tool depth-resolve dependency unavailable; no capability fallback");
    fprintf(stderr,"[replay bridge] added GFXR required VK_KHR_create_renderpass2 dependency; captured features/pNext unchanged\n");
  }
  VkResult result=fn(physical,change?&corrected:info,allocator,device);free(owned);return result;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance,const char *name) {
  pthread_once(&once,load_system);if(!name)return real_gipa(instance,name);
  PFN_vkVoidFunction proc=real_gipa(instance,name);if(!proc)return NULL;
  if(!strcmp(name,"vkGetInstanceProcAddr"))return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
  if(!strcmp(name,"vkEnumeratePhysicalDevices")) {atomic_store_explicit(&real_enumerate,(PFN_vkEnumeratePhysicalDevices)proc,memory_order_release);return (PFN_vkVoidFunction)enumerate;}
  if(!strcmp(name,"vkCreateDevice")) {atomic_store_explicit(&real_create_device,(PFN_vkCreateDevice)proc,memory_order_release);atomic_store_explicit(&real_extensions,(PFN_vkEnumerateDeviceExtensionProperties)real_gipa(instance,"vkEnumerateDeviceExtensionProperties"),memory_order_release);return (PFN_vkVoidFunction)create_device;}
  return proc;
}
JNIEXPORT void JNICALL Java_com_d2rmobile_panvkgfxrfixture_ReplayActivity_nativeConfigure(JNIEnv *env,jclass clazz,jstring files,jstring libs,jstring token) {
  (void)clazz;
  const char *f=(*env)->GetStringUTFChars(env,files,NULL),*l=(*env)->GetStringUTFChars(env,libs,NULL),*t=(*env)->GetStringUTFChars(env,token,NULL);
  if(!f||!l||!t||strlen(t)!=32||strspn(t,"0123456789abcdef")!=32)fail("cold token");
  char parent[PATH_MAX],hooks[PATH_MAX],wanted[PATH_MAX],work[64];
  if(!realpath(l,hooks)||!realpath(f,parent))fail("private/native directory");
  snprintf(work,sizeof work,"run-%s",t);path_join(wanted,parent,work);
  if(atomic_load_explicit(&configured,memory_order_acquire)) {
    if(strcmp(wanted,root_dir)||strcmp(hooks,hook_dir))fail("cannot change loaded replay configuration");
    (*env)->ReleaseStringUTFChars(env,files,f);(*env)->ReleaseStringUTFChars(env,libs,l);(*env)->ReleaseStringUTFChars(env,token,t);return;
  }
  strcpy(hook_dir,hooks);strcpy(root_dir,wanted);if(mkdir(root_dir,0700))fail("run directory already exists/missing");
  char path[PATH_MAX];path_join(path,root_dir,"replay.log");int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_APPEND|O_CLOEXEC,0600);if(fd<0||dup2(fd,1)<0||dup2(fd,2)<0)fail("stdio log");if(fd>2)close(fd);setvbuf(stdout,NULL,_IONBF,0);setvbuf(stderr,NULL,_IONBF,0);
  path_join(driver_path,parent,"driver/libvulkan_panfrost.so");path_join(path,hook_dir,"libpanvk_replay_bridge.so");if(setenv("GFXRECON_VULKAN_LIBRARY_PATH",path,1))fail("replay library env");
  path_join(path,root_dir,"cache");if(mkdir(path,0700)||setenv("MESA_SHADER_CACHE_DIR",path,1))fail("private shader cache");
  path_join(path,root_dir,"mesa.log");if(setenv("MESA_LOG","file,android",1)||setenv("MESA_LOG_FILE",path,1)||setenv("TU_DM_HOST_IMPORT","false",1)||setenv("PANVK_DM_HOST_IMPORT","false",1)||setenv("VK_LAYER_ENABLES","VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT",1))fail("cold env");
  fprintf(stderr,"[replay bridge] configured pid=%d uid=%d token=%s official-replay-only no-game-modules sync-validation-requested\n",getpid(),getuid(),t);
  (*env)->ReleaseStringUTFChars(env,files,f);(*env)->ReleaseStringUTFChars(env,libs,l);(*env)->ReleaseStringUTFChars(env,token,t);atomic_store_explicit(&configured,1,memory_order_release);
}
