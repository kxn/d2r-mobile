/* API36 functional interop only. No game, profiler, CPU sync_wait, or fake API identity. */
#include "volk.h"
#include <android/native_window_jni.h>
#include <android/hardware_buffer.h>
#include <android/surface_control.h>
#include <android/data_space.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "performance_hint.h" /* exact cached official API36 header, source-hashed by builder */
#pragma clang diagnostic pop
#include "adrenotools/driver.h"
#include <jni.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static atomic_int stopping, selected_color;
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr,"SCAHB_FAILURE line=%d %s\n",__LINE__,#x); abort(); } } while(0)
#define CHECK(x) do { VkResult r_=(x); if(r_!=VK_SUCCESS) { fprintf(stderr,"SCAHB_FAILURE line=%d %s result=%d\n",__LINE__,#x,r_); abort(); } } while(0)
#define N 3
struct ring;
typedef struct slot {
  struct ring *owner;
  AHardwareBuffer *ahb;
  VkImage image;
  VkDeviceMemory memory;
  VkCommandPool pool;
  VkCommandBuffer cmd;
  VkFence completed;
  VkSemaphore release_wait, production;
  int pending, release_fd, used;
  unsigned submitted, callbacks, import_waits;
} slot;
typedef struct ring { slot slots[N]; unsigned callbacks, valid_release_fds; int callback_error; } ring;

JNIEXPORT void JNICALL Java_org_kxn_scahb_ProbeActivity_prepare(JNIEnv *e,jclass c) {
  (void)e;(void)c;atomic_store(&stopping,0);
}
JNIEXPORT void JNICALL Java_org_kxn_scahb_ProbeActivity_stop(JNIEnv *e,jclass c) {
  (void)e;(void)c;atomic_store(&stopping,1);pthread_mutex_lock(&mutex);pthread_cond_broadcast(&changed);pthread_mutex_unlock(&mutex);
}
JNIEXPORT void JNICALL Java_org_kxn_scahb_ProbeActivity_color(JNIEnv *e,jclass c,jint v) {
  (void)e;(void)c;atomic_store(&selected_color,v&3);
}
static void release_callback(void *opaque,int fd) {
  slot *s=opaque;
  pthread_mutex_lock(&mutex);
  if(!s->pending || s->release_fd!=-1) { s->owner->callback_error=1;if(fd>=0)close(fd); }
  else { s->pending=0;s->release_fd=fd;s->callbacks++;s->owner->callbacks++;s->owner->valid_release_fds+=(fd>=0); }
  pthread_cond_broadcast(&changed);pthread_mutex_unlock(&mutex);
}
static uint32_t memory_type(VkPhysicalDeviceMemoryProperties *p,uint32_t bits) {
  for(uint32_t n=0;n<p->memoryTypeCount;n++) if((bits&(1u<<n))&&(p->memoryTypes[n].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) return n;
  return UINT32_MAX;
}
static void uuid_print(const char *name,const uint8_t *bytes) {
  fprintf(stderr," %s=",name);for(int n=0;n<16;n++)fprintf(stderr,"%02x",bytes[n]);
}
static void import_release(VkDevice device,slot *s,int fd) {
  if(fd<0)return;
  VkImportSemaphoreFdInfoKHR info={.sType=VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
    .semaphore=s->release_wait,.flags=VK_SEMAPHORE_IMPORT_TEMPORARY_BIT,
    .handleType=VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,.fd=fd};
  VkResult result=vkImportSemaphoreFdKHR(device,&info);
  if(result!=VK_SUCCESS)close(fd);
  CHECK(result);s->import_waits++;
}
static void cold_adpf_query(void) {
  void *lib=dlopen("libandroid.so",RTLD_NOW|RTLD_LOCAL);REQUIRE(lib);
  bool (*feature)(APerformanceHintFeature)=(bool(*)(APerformanceHintFeature))dlsym(lib,"APerformanceHint_isFeatureSupported");
  fprintf(stderr,"SCAHB_ADPF official_feature_query_available=%d no_session_created=1\n",feature!=NULL);
  if(feature) {
    const APerformanceHintFeature values[]={APERF_HINT_SESSIONS,APERF_HINT_SURFACE_BINDING,APERF_HINT_GRAPHICS_PIPELINE,APERF_HINT_AUTO_CPU,APERF_HINT_AUTO_GPU};
    const char *names[]={"SESSIONS","SURFACE_BINDING","GRAPHICS_PIPELINE","AUTO_CPU","AUTO_GPU"};
    for(unsigned n=0;n<sizeof values/sizeof *values;n++) fprintf(stderr,"SCAHB_ADPF feature=%s enum=%d supported=%d\n",names[n],(int)values[n],feature(values[n]));
  }
  dlclose(lib);
}

JNIEXPORT void JNICALL Java_org_kxn_scahb_ProbeActivity_render(JNIEnv *env,jclass klass,jobject surface,jstring directory,jstring hooks,jboolean query_only,jint color) {
  (void)klass;
  const char *dir=(*env)->GetStringUTFChars(env,directory,NULL),*hook=(*env)->GetStringUTFChars(env,hooks,NULL);REQUIRE(dir&&hook);
  char path[4096],driver_root[4096],hook_root[4096];
  REQUIRE(snprintf(path,sizeof path,"%s/sc-ahb.log",dir)<(int)sizeof path);
  REQUIRE(freopen(path,"a",stderr));setvbuf(stderr,NULL,_IONBF,0);
  fprintf(stderr,"SCAHB_BEGIN pid=%d uid=%d ring=%d game_modules_executed=0 no_profiler=1\n",getpid(),getuid(),N);
  cold_adpf_query();
  void *android=dlopen("libandroid.so",RTLD_NOW|RTLD_LOCAL);REQUIRE(android);
  typedef void (*set_buffer_fn)(ASurfaceTransaction *,ASurfaceControl *,AHardwareBuffer *,int,void *,ASurfaceTransaction_OnBufferRelease);
  set_buffer_fn set_buffer=(set_buffer_fn)dlsym(android,"ASurfaceTransaction_setBufferWithRelease");
  if(!set_buffer) { fprintf(stderr,"SCAHB_UNAVAILABLE release_callback_API36=0\n");goto done; }
  REQUIRE(snprintf(driver_root,sizeof driver_root,"%s/",dir)<(int)sizeof driver_root);
  REQUIRE(snprintf(hook_root,sizeof hook_root,"%s/",hook)<(int)sizeof hook_root);
  REQUIRE(snprintf(path,sizeof path,"%s/libadrenotools.so",hook)<(int)sizeof path);
  void *tools=dlopen(path,RTLD_NOW|RTLD_LOCAL);REQUIRE(tools);
  typedef void *(*open_fn)(int,int,const char *,const char *,const char *,const char *,const char *,void **);
  open_fn open_driver=(open_fn)dlsym(tools,"adrenotools_open_libvulkan");REQUIRE(open_driver);
  void *loader=open_driver(RTLD_NOW|RTLD_LOCAL,ADRENOTOOLS_DRIVER_CUSTOM,NULL,hook_root,driver_root,"libvulkan_freedreno.so",NULL,NULL);REQUIRE(loader);
  PFN_vkGetInstanceProcAddr get=(PFN_vkGetInstanceProcAddr)dlsym(loader,"vkGetInstanceProcAddr");REQUIRE(get);volkInitializeCustom(get);
  VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="SurfaceControl AHB functional fixture",.apiVersion=VK_API_VERSION_1_1};
  VkInstanceCreateInfo ii={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
  VkInstance instance;CHECK(vkCreateInstance(&ii,NULL,&instance));volkLoadInstance(instance);
  VkPhysicalDevice physical;uint32_t physical_count=1;CHECK(vkEnumeratePhysicalDevices(instance,&physical_count,&physical));REQUIRE(physical_count==1);
  VkPhysicalDeviceIDProperties id={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
  VkPhysicalDeviceProperties2 props={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&id};vkGetPhysicalDeviceProperties2(physical,&props);
  fprintf(stderr,"SCAHB_DEVICE name=%s api=%u",props.properties.deviceName,props.properties.apiVersion);uuid_print("driverUUID",id.driverUUID);uuid_print("deviceUUID",id.deviceUUID);fprintf(stderr,"\n");
  uint32_t ec=0;CHECK(vkEnumerateDeviceExtensionProperties(physical,NULL,&ec,NULL));VkExtensionProperties *ep=calloc(ec,sizeof *ep);REQUIRE(ep);CHECK(vkEnumerateDeviceExtensionProperties(physical,NULL,&ec,ep));
  const char *required[]={VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME,VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME};
  int qualified=1;
  for(unsigned n=0;n<3;n++) { int present=0;for(uint32_t j=0;j<ec;j++)present|=!strcmp(ep[j].extensionName,required[n]);fprintf(stderr,"SCAHB_EXTENSION name=%s advertised=%d\n",required[n],present);qualified&=present; }
  int internal_ext=0;
  for(uint32_t j=0;j<ec;j++)internal_ext|=!strcmp(ep[j].extensionName,"VK_KHR_internally_synchronized_queues");
  /* ABI copied from the frozen Mesa-generated Khronos header; no enable/request here. */
  struct internal_features { VkStructureType sType; void *pNext; VkBool32 internallySynchronizedQueues; } internal={.sType=(VkStructureType)1000504000};
  if(internal_ext) { VkPhysicalDeviceFeatures2 features={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&internal};vkGetPhysicalDeviceFeatures2(physical,&features); }
  fprintf(stderr,"SCAHB_INTERNAL_QUEUE extension=%d feature=%u official_queue_flag=4 feature_stype=1000504000 query_only=1\n",internal_ext,internal.internallySynchronizedQueues);
  free(ep);
  VkPhysicalDeviceExternalSemaphoreInfo es={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO,.handleType=VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT};
  VkExternalSemaphoreProperties esp={.sType=VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES};vkGetPhysicalDeviceExternalSemaphoreProperties(physical,&es,&esp);
  fprintf(stderr,"SCAHB_SYNC_FD features=%u compatible=%u exportFromImported=%u\n",esp.externalSemaphoreFeatures,esp.compatibleHandleTypes,esp.exportFromImportedHandleTypes);
  qualified&=(esp.externalSemaphoreFeatures&(VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT|VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT))==(VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT|VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT);
  if(query_only) { fprintf(stderr,"SCAHB_QUERY_TERMINAL interop_qualified=%d no_session_created=1 no_swapchain_created=1\n",qualified);vkDestroyInstance(instance,NULL);goto done; }
  if(!qualified) { fprintf(stderr,"SCAHB_UNAVAILABLE required_public_interop=0\n");vkDestroyInstance(instance,NULL);goto done; }
  uint32_t qc=0;vkGetPhysicalDeviceQueueFamilyProperties(physical,&qc,NULL);VkQueueFamilyProperties *qp=calloc(qc,sizeof *qp);REQUIRE(qp);vkGetPhysicalDeviceQueueFamilyProperties(physical,&qc,qp);
  uint32_t family=UINT32_MAX;for(uint32_t n=0;n<qc;n++)if(qp[n].queueFlags&VK_QUEUE_GRAPHICS_BIT){family=n;break;}free(qp);REQUIRE(family!=UINT32_MAX);
  float priority=1;VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
  VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi,.enabledExtensionCount=3,.ppEnabledExtensionNames=required};
  VkDevice device;CHECK(vkCreateDevice(physical,&di,NULL,&device));volkLoadDevice(device);VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
  REQUIRE(vkGetAndroidHardwareBufferPropertiesANDROID&&vkGetSemaphoreFdKHR&&vkImportSemaphoreFdKHR);
  ANativeWindow *window=ANativeWindow_fromSurface(env,surface);REQUIRE(window);
  uint32_t width=ANativeWindow_getWidth(window),height=ANativeWindow_getHeight(window);REQUIRE(width&&height);
  ASurfaceControl *control=ASurfaceControl_createFromWindow(window,"SCAHB public interop fixture");REQUIRE(control);
  ASurfaceTransaction *config=ASurfaceTransaction_create();REQUIRE(config);
  ASurfaceTransaction_setVisibility(config,control,ASURFACE_TRANSACTION_VISIBILITY_SHOW);
  ASurfaceTransaction_setBufferDataSpace(config,control,ADATASPACE_SRGB);
  ASurfaceTransaction_setBufferTransparency(config,control,ASURFACE_TRANSACTION_TRANSPARENCY_OPAQUE);
  ASurfaceTransaction_setEnableBackPressure(config,control,true);
  ASurfaceTransaction_apply(config);ASurfaceTransaction_delete(config);
  VkPhysicalDeviceMemoryProperties memory;vkGetPhysicalDeviceMemoryProperties(physical,&memory);
  ring *r=calloc(1,sizeof *r);REQUIRE(r);
  for(unsigned n=0;n<N;n++) {
    slot *s=&r->slots[n];s->owner=r;s->release_fd=-1;
    AHardwareBuffer_Desc desc={.width=width,.height=height,.layers=1,.format=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
      .usage=AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT|AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE|AHARDWAREBUFFER_USAGE_COMPOSER_OVERLAY};
    REQUIRE(AHardwareBuffer_allocate(&desc,&s->ahb)==0);
    VkAndroidHardwareBufferFormatPropertiesANDROID fp={.sType=VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID};
    VkAndroidHardwareBufferPropertiesANDROID ap={.sType=VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID,.pNext=&fp};CHECK(vkGetAndroidHardwareBufferPropertiesANDROID(device,s->ahb,&ap));
    fprintf(stderr,"SCAHB_AHB slot=%u width=%u height=%u bytes=%llu memoryBits=%u format=%d features=%u\n",n,width,height,(unsigned long long)ap.allocationSize,ap.memoryTypeBits,fp.format,fp.formatFeatures);
    REQUIRE(fp.format==VK_FORMAT_R8G8B8A8_UNORM);REQUIRE(fp.formatFeatures&VK_FORMAT_FEATURE_BLIT_DST_BIT);
    VkExternalMemoryImageCreateInfo external={.sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,.handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID};
    VkImageCreateInfo image={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.pNext=&external,.imageType=VK_IMAGE_TYPE_2D,.format=fp.format,.extent={width,height,1},.mipLevels=1,.arrayLayers=1,
      .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
    CHECK(vkCreateImage(device,&image,NULL,&s->image));
    VkMemoryDedicatedAllocateInfo dedicated={.sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,.image=s->image};
    VkImportAndroidHardwareBufferInfoANDROID import={.sType=VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,.pNext=&dedicated,.buffer=s->ahb};
    VkMemoryAllocateInfo allocate={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.pNext=&import,.allocationSize=ap.allocationSize,.memoryTypeIndex=memory_type(&memory,ap.memoryTypeBits)};REQUIRE(allocate.memoryTypeIndex!=UINT32_MAX);
    CHECK(vkAllocateMemory(device,&allocate,NULL,&s->memory));CHECK(vkBindImageMemory(device,s->image,s->memory,0));
    VkCommandPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};CHECK(vkCreateCommandPool(device,&pi,NULL,&s->pool));
    VkCommandBufferAllocateInfo ci={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=s->pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};CHECK(vkAllocateCommandBuffers(device,&ci,&s->cmd));
    VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,.flags=VK_FENCE_CREATE_SIGNALED_BIT};CHECK(vkCreateFence(device,&fi,NULL,&s->completed));
    VkSemaphoreCreateInfo si={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};CHECK(vkCreateSemaphore(device,&si,NULL,&s->release_wait));
    VkExportSemaphoreCreateInfo export={.sType=VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,.handleTypes=VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT};si.pNext=&export;CHECK(vkCreateSemaphore(device,&si,NULL,&s->production));
  }
  /* A tiny ordinary source exercises the same GPU blit type as the game's final copy. */
  VkImageCreateInfo source_info={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,.extent={64,64,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
  VkImage source;CHECK(vkCreateImage(device,&source_info,NULL,&source));VkMemoryRequirements mr;vkGetImageMemoryRequirements(device,source,&mr);
  VkMemoryAllocateInfo ma={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=mr.size,.memoryTypeIndex=memory_type(&memory,mr.memoryTypeBits)};REQUIRE(ma.memoryTypeIndex!=UINT32_MAX);VkDeviceMemory sm;CHECK(vkAllocateMemory(device,&ma,NULL,&sm));CHECK(vkBindImageMemory(device,source,sm,0));
  atomic_store(&selected_color,color&3);unsigned frames=0,next=0;int last=-1,source_used=0;
  fprintf(stderr,"SCAHB_READY width=%u height=%u ring=3 backpressure=1 no_swapchain=1 no_cpu_sync_wait=1\n",width,height);
  while(!atomic_load(&stopping)) {
    pthread_mutex_lock(&mutex);slot *s=NULL;
    while(!atomic_load(&stopping)&&!s) { for(unsigned n=0;n<N;n++){unsigned k=(next+n)%N;if(!r->slots[k].pending){s=&r->slots[k];next=(k+1)%N;break;}}if(!s)pthread_cond_wait(&changed,&mutex); }
    if(!s){pthread_mutex_unlock(&mutex);break;}int release_fd=s->release_fd;s->release_fd=-1;pthread_mutex_unlock(&mutex);
    /* This fence protects command-pool reuse, never polls or waits an Android release fd. */
    CHECK(vkWaitForFences(device,1,&s->completed,VK_TRUE,UINT64_MAX));CHECK(vkResetFences(device,1,&s->completed));CHECK(vkResetCommandPool(device,s->pool,0));import_release(device,s,release_fd);
    VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(s->cmd,&bi));
    VkImageMemoryBarrier barriers[2]={
      {.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.oldLayout=s->used?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_FOREIGN_EXT,.dstQueueFamilyIndex=family,.image=s->image,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}},
      {.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.oldLayout=source_used?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=source,.srcAccessMask=source_used?VK_ACCESS_TRANSFER_READ_BIT:0,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}}};
    vkCmdPipelineBarrier(s->cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,2,barriers);
    int selected=atomic_load(&selected_color)&3;VkClearColorValue value={.float32={selected==0||selected==3,selected==1||selected==3,selected==2||selected==3,1}};
    vkCmdClearColorImage(s->cmd,source,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&value,1,&barriers[1].subresourceRange);
    barriers[1].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barriers[1].newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;barriers[1].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barriers[1].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(s->cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barriers[1]);
    VkImageBlit blit={.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.srcOffsets={{0,0,0},{64,64,1}},.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.dstOffsets={{0,0,0},{width,height,1}}};
    vkCmdBlitImage(s->cmd,source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,s->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_NEAREST);
    barriers[0].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barriers[0].newLayout=VK_IMAGE_LAYOUT_GENERAL;barriers[0].srcQueueFamilyIndex=family;barriers[0].dstQueueFamilyIndex=VK_QUEUE_FAMILY_FOREIGN_EXT;barriers[0].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barriers[0].dstAccessMask=0;
    vkCmdPipelineBarrier(s->cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,NULL,0,NULL,1,&barriers[0]);CHECK(vkEndCommandBuffer(s->cmd));
    VkPipelineStageFlags stage=VK_PIPELINE_STAGE_TRANSFER_BIT;VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.waitSemaphoreCount=release_fd>=0?1:0,.pWaitSemaphores=&s->release_wait,.pWaitDstStageMask=&stage,.commandBufferCount=1,.pCommandBuffers=&s->cmd,.signalSemaphoreCount=1,.pSignalSemaphores=&s->production};CHECK(vkQueueSubmit(queue,1,&submit,s->completed));
    VkSemaphoreGetFdInfoKHR fd_info={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,.semaphore=s->production,.handleType=VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT};int production_fd=-1;CHECK(vkGetSemaphoreFdKHR(device,&fd_info,&production_fd));
    /* -1 from the official export means an already-signaled payload, not a manufactured omission. */
    pthread_mutex_lock(&mutex);s->pending=1;s->submitted++;pthread_mutex_unlock(&mutex);
    ASurfaceTransaction *t=ASurfaceTransaction_create();REQUIRE(t);set_buffer(t,control,s->ahb,production_fd,s,release_callback);ASurfaceTransaction_apply(t);ASurfaceTransaction_delete(t);
    s->used=1;source_used=1;frames++;
    if(selected!=last){fprintf(stderr,"SCAHB_COLOR value=%d frame=%u production_fd=%d\n",selected,frames,production_fd);last=selected;}
    if(frames==12||frames%240==0)fprintf(stderr,"SCAHB_PROGRESS frames=%u slots=3\n",frames);
  }
  /* Detach the final buffer so its callback can arrive before destroying imported memory. */
  ASurfaceTransaction *hide=ASurfaceTransaction_create();REQUIRE(hide);ASurfaceTransaction_setVisibility(hide,control,ASURFACE_TRANSACTION_VISIBILITY_HIDE);set_buffer(hide,control,NULL,-1,NULL,release_callback);ASurfaceTransaction_apply(hide);ASurfaceTransaction_delete(hide);
  pthread_mutex_lock(&mutex);int outstanding=1;
  while(outstanding){outstanding=0;for(unsigned n=0;n<N;n++)outstanding+=r->slots[n].pending;if(!outstanding)break;struct timespec deadline;REQUIRE(!clock_gettime(CLOCK_REALTIME,&deadline));deadline.tv_sec+=5;int status=pthread_cond_timedwait(&changed,&mutex,&deadline);if(status){fprintf(stderr,"SCAHB_FAILURE lifecycle_callback_drain status=%d outstanding=%d; retaining_resources_until_process_exit=1\n",status,outstanding);pthread_mutex_unlock(&mutex);goto retained;}}
  pthread_mutex_unlock(&mutex);REQUIRE(!r->callback_error);
  VkSemaphore waits[N];VkPipelineStageFlags stages[N];uint32_t wc=0;
  for(unsigned n=0;n<N;n++){slot *s=&r->slots[n];if(s->release_fd>=0){import_release(device,s,s->release_fd);s->release_fd=-1;waits[wc]=s->release_wait;stages[wc++]=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;}}
  if(wc){VkSubmitInfo drain={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.waitSemaphoreCount=wc,.pWaitSemaphores=waits,.pWaitDstStageMask=stages};CHECK(vkQueueSubmit(queue,1,&drain,VK_NULL_HANDLE));}
  CHECK(vkDeviceWaitIdle(device));
  for(unsigned n=0;n<N;n++){slot *s=&r->slots[n];REQUIRE(s->submitted==s->callbacks);fprintf(stderr,"SCAHB_SLOT slot=%u submits=%u release_callbacks=%u imported_release_waits=%u\n",n,s->submitted,s->callbacks,s->import_waits);vkDestroySemaphore(device,s->production,NULL);vkDestroySemaphore(device,s->release_wait,NULL);vkDestroyFence(device,s->completed,NULL);vkDestroyCommandPool(device,s->pool,NULL);vkDestroyImage(device,s->image,NULL);vkFreeMemory(device,s->memory,NULL);AHardwareBuffer_release(s->ahb);}
  fprintf(stderr,"SCAHB_TERMINAL frames=%u callbacks=%u valid_release_fds=%u lifecycle_drained=1 game_modules_executed=0\n",frames,r->callbacks,r->valid_release_fds);free(r);vkDestroyImage(device,source,NULL);vkFreeMemory(device,sm,NULL);ASurfaceControl_release(control);ANativeWindow_release(window);vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);
retained:
  ; /* Driver/tool namespaces stay loaded until this isolated app exits. */
done:
  (*env)->ReleaseStringUTFChars(env,directory,dir);(*env)->ReleaseStringUTFChars(env,hooks,hook);
}
