// Driver PSO data persists independently of game/shader assets. GPU work is
// never waited here; the worker serializes CPU cache access and writes off-thread.
#include "nvn_vk_internal.h"
#include "../../host/crypto.h"
#include "nvn_vk_system_trace.h"
#include "nvn_vk_prewarm.h"
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include <unistd.h>

enum { CACHE_HEADER=88, CACHE_LIMIT=64<<20 };
static char cache_path[4096];
static pthread_mutex_t cache_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t worker_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t worker_cv=PTHREAD_COND_INITIALIZER;
static uint64_t generation,handled,flush_requested;
static _Atomic int worker_enabled;
static uint32_t u32(const uint8_t *p) {
  return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put32(uint8_t *p,uint32_t n) { for(unsigned i=0;i<4;i++) p[i]=n>>(8*i); }
static void put64(uint8_t *p,uint64_t n) { put32(p,n);put32(p+4,n>>32); }
static int read_all(int fd,void *data,size_t size) {
  uint8_t *p=data;
  while(size) {
    ssize_t n=read(fd,p,size);
    if(n<0&&errno==EINTR) continue;
    if(n<=0) return 0;
    p+=n;size-=n;
  }
  return 1;
}
static int write_all(int fd,const void *data,size_t size) {
  const uint8_t *p=data;
  while(size) {
    ssize_t n=write(fd,p,size);
    if(n<0&&errno==EINTR) continue;
    if(n<=0) return 0;
    p+=n;size-=n;
  }
  return 1;
}
static int driver_header(const uint8_t *p,size_t size) {
  return size>=32 && u32(p)==32 && u32(p+4)==VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
      u32(p+8)==V.props.vendorID && u32(p+12)==V.props.deviceID &&
      !memcmp(p+16,V.props.pipelineCacheUUID,VK_UUID_SIZE);
}
static void metadata(uint8_t h[CACHE_HEADER],const void *data,size_t size) {
  memset(h,0,CACHE_HEADER);memcpy(h,"DMVKPC1",7);
  put32(h+8,1);put32(h+12,sizeof(void *));
  put32(h+16,V.props.vendorID);put32(h+20,V.props.deviceID);
  put32(h+24,V.props.driverVersion);put32(h+28,V.props.apiVersion);
  memcpy(h+32,V.props.pipelineCacheUUID,VK_UUID_SIZE);put64(h+48,size);
  CC_SHA256(data,size,h+56);
}
VkPipelineCache vk_pipeline_cache_load(const char *path,size_t *loaded) {
  *loaded=0;
  void *data=NULL;
  if(path&&*path) {
    int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(fd>=0) {
      struct stat st;uint8_t h[CACHE_HEADER],expected[CACHE_HEADER];
      if(!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>=CACHE_HEADER+32&&
         st.st_size<=CACHE_HEADER+CACHE_LIMIT&&read_all(fd,h,sizeof h)) {
        size_t size=st.st_size-CACHE_HEADER;
        data=malloc(size);
        if(data&&read_all(fd,data,size)) {
          metadata(expected,data,size);
          if(!memcmp(h,expected,sizeof h)&&driver_header(data,size)) *loaded=size;
        }
      }
      close(fd);
      if(!*loaded) LOG("[vulkan PSO cache] ignored incompatible or damaged file");
    }
  }
  VkPipelineCacheCreateInfo ci={.sType=VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
      .initialDataSize=*loaded,.pInitialData=*loaded?data:NULL};
  VkPipelineCache cache=VK_NULL_HANDLE;
  VkResult result=vkCreatePipelineCache(V.device,&ci,NULL,&cache);
  if(result!=VK_SUCCESS&&*loaded) {
    LOG("[vulkan PSO cache] driver rejected persisted data (%d); using empty cache",result);
    *loaded=0;ci.initialDataSize=0;ci.pInitialData=NULL;
    result=vkCreatePipelineCache(V.device,&ci,NULL,&cache);
  }
  free(data);VK_CHECK(result);return cache;
}
static int store(const void *data,size_t size) {
  if(!driver_header(data,size)) return 0;
  uint8_t h[CACHE_HEADER];metadata(h,data,size);
  char temporary[sizeof cache_path+24];
  snprintf(temporary,sizeof temporary,"%s.tmp-XXXXXX",cache_path);
  int fd=mkstemp(temporary);if(fd<0) return 0;
  fcntl(fd,F_SETFD,FD_CLOEXEC);
  int ok=write_all(fd,h,sizeof h)&&write_all(fd,data,size)&&!fsync(fd);
  if(close(fd)) ok=0;
  if(ok&&rename(temporary,cache_path)) ok=0;
  if(!ok) unlink(temporary);
  return ok;
}
#include "nvn_vk_prewarm_store.inc"
static void *writer(void *unused) {
  (void)unused;
#ifdef __ANDROID__
  pthread_setname_np(pthread_self(),"dm-pso-cache");
#endif
  pthread_mutex_lock(&worker_mutex);
  for(;;) {
    while(generation==handled) pthread_cond_wait(&worker_cv,&worker_mutex);
    // Coalesce compilation bursts. This is worker scheduling, not profiling;
    // lifecycle checkpoints request immediate persistence instead.
    while(flush_requested<=handled) {
      struct timespec deadline;clock_gettime(CLOCK_REALTIME,&deadline);deadline.tv_sec++;
      int result=pthread_cond_timedwait(&worker_cv,&worker_mutex,&deadline);
      if(result==ETIMEDOUT) break;
      if(flush_requested>handled) break;
    }
    uint64_t target=generation;
    pthread_mutex_unlock(&worker_mutex);
    size_t size=0;void *data=NULL;
    int trace=vk_system_trace_begin("Vulkan PSO cache snapshot");
    pthread_mutex_lock(&cache_mutex);
    VkResult result=vkGetPipelineCacheData(V.device,V.pipeline_cache,&size,NULL);
    if(result==VK_SUCCESS&&size>=32&&size<=CACHE_LIMIT) {
      data=malloc(size);
      if(data) result=vkGetPipelineCacheData(V.device,V.pipeline_cache,&size,data);
    }
    pthread_mutex_unlock(&cache_mutex);
    vk_system_trace_end(trace);
    int ok=data&&result==VK_SUCCESS&&store(data,size);
    free(data);
    vk_prewarm_store();
    if(!ok) LOG("[vulkan PSO cache] optional snapshot/write skipped or failed");
    else LOG("[vulkan PSO cache] persisted %zu driver bytes",size);
    pthread_mutex_lock(&worker_mutex);
    handled=target;pthread_cond_broadcast(&worker_cv);
  }
  return NULL;
}
void vk_pipeline_cache_init(void) {
  const char *root=V.options.pipeline_cache_directory;
  if(root&&*root) {
    char directory[sizeof cache_path];
    int n=snprintf(directory,sizeof directory,"%s/vulkan-pso",root);
    if(n>0&&n<(int)sizeof directory&&(!mkdir(directory,0700)||errno==EEXIST)) {
      char uuid[VK_UUID_SIZE*2+1];
      for(unsigned i=0;i<VK_UUID_SIZE;i++) snprintf(uuid+i*2,3,"%02x",V.props.pipelineCacheUUID[i]);
      n=snprintf(cache_path,sizeof cache_path,"%s/v1-%08x-%08x-%08x-%s.bin",
          directory,V.props.vendorID,V.props.deviceID,V.props.driverVersion,uuid);
      if(n<0||n>=(int)sizeof cache_path) cache_path[0]=0;
    }
  }
  size_t loaded;
  V.pipeline_cache=vk_pipeline_cache_load(cache_path,&loaded);
  LOG("[vulkan PSO cache] loaded=%zu persistent=%d",loaded,!!cache_path[0]);
  if(cache_path[0]) {
    pthread_t thread;
    if(!pthread_create(&thread,NULL,writer,NULL)) { pthread_detach(thread);atomic_store(&worker_enabled,1); }
    else LOG("[vulkan PSO cache] writer unavailable; retaining memory cache");
  }
}
void vk_pipeline_cache_begin(void) { pthread_mutex_lock(&cache_mutex); }
void vk_pipeline_cache_end(int created) {
  pthread_mutex_unlock(&cache_mutex);
  if(created)vk_pipeline_cache_changed();
}
void vk_pipeline_cache_changed(void) {
  if(atomic_load(&worker_enabled)) {
    pthread_mutex_lock(&worker_mutex);generation++;pthread_cond_signal(&worker_cv);
    pthread_mutex_unlock(&worker_mutex);
  }
}
void vk_pipeline_cache_checkpoint(void) {
  if(!atomic_load(&worker_enabled)) return;
  pthread_mutex_lock(&worker_mutex);
  uint64_t target=generation;
  if(flush_requested<target) flush_requested=target;
  pthread_cond_broadcast(&worker_cv);
  while(handled<target) pthread_cond_wait(&worker_cv,&worker_mutex);
  pthread_mutex_unlock(&worker_mutex);
}
const char *vk_pipeline_cache_path(void) { return cache_path; }
