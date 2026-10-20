// Thin owned entry, original adrenotools loader contract and original producer
// shader/API oracle. No game/NSO, alternate renderer, timer or measurement.
#include "volk.h"
#include "mbedtls/sha256.h"
#include "adrenotools/driver.h"
#include "../../../dm/host/android/fixture_output.h"
#include <jni.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdatomic.h>
#include <sys/stat.h>
#define DECL(n) extern int dm_native_linear_shape_##n(const char*,const char*,PFN_vkGetInstanceProcAddr,int)
DECL(0);DECL(1);DECL(2);DECL(3);DECL(4);DECL(5);DECL(6);DECL(7);
static void fail(const char *reason){fprintf(stderr,"MATRIX_FATAL %s\n",reason);_exit(80);}
static void path(char out[4096],const char *a,const char *b){if(snprintf(out,4096,"%s/%s",a,b)>=4096)fail("path length");}
static void verify_driver(const char *name) {
  FILE *f=fopen(name,"rb");if(!f)fail("driver file missing");
  mbedtls_sha256_context c;mbedtls_sha256_init(&c);mbedtls_sha256_starts(&c,0);
  uint8_t buf[65536],digest[32];size_t size;while((size=fread(buf,1,sizeof buf,f)))mbedtls_sha256_update(&c,buf,size);
  if(ferror(f)||fclose(f))fail("driver read");mbedtls_sha256_finish(&c,digest);mbedtls_sha256_free(&c);
  static const uint8_t wanted[32]={0xf8,0x30,0x28,0x5f,0xe6,0x8f,0x0a,0x74,0x47,0x92,0x26,0x9d,0xa5,0xf1,0x60,0xed,0x86,0x36,0xc1,0x72,0xb1,0xf6,0x51,0x1d,0xb9,0xd9,0x94,0x78,0xe1,0xa2,0xac,0xce};
  if(memcmp(wanted,digest,32))fail("driver SHA256 differs");
}
JNIEXPORT jint JNICALL Java_com_d2rmobile_nativeastclinearfixture_MainActivity_nativeRun(JNIEnv *env,jclass klass,jstring root_string,jstring hooks_string,jint selected_shape,jboolean real_resource) {
  (void)klass;static atomic_int used;if(getuid()<10000||atomic_exchange(&used,1))return 78;
  if(selected_shape < -1 || selected_shape>7 || (real_resource && selected_shape!=7))return 79;
  const char *root=(*env)->GetStringUTFChars(env,root_string,NULL),*hooks=(*env)->GetStringUTFChars(env,hooks_string,NULL);
  if(!root||!hooks||root[0]!='/'||hooks[0]!='/')fail("absolute app paths required");
  char output[4096],driver[4096],driver_library[4096],tools[4096],hook_directory[4096];
  path(output,root,"native-linear-matrix.log");int rc=dm_fixture_output_open(output);if(rc)_exit(rc);
  printf("{\"native_linear_matrix_start\":true,\"uid\":%u,\"pid\":%u,\"shape_selector\":%d,\"real_resource_bake\":%s,\"game_modules_executed\":false,\"validation\":\"disabled_explicitly\"}\n",getuid(),getpid(),selected_shape,real_resource?"true":"false");
  path(driver,root,"driver");path(driver_library,driver,"libvulkan_freedreno.so");verify_driver(driver_library);
  path(tools,hooks,"libadrenotools.so");if(snprintf(hook_directory,sizeof hook_directory,"%s/",hooks)>=4096)fail("hook path");
  char driver_directory[4096];if(snprintf(driver_directory,sizeof driver_directory,"%s/",driver)>=4096)fail("driver path");
  if(setenv("TU_DM_HOST_IMPORT","true",1))fail("environment");
  char cache[4096];path(cache,root,"mesa-cache");if(mkdir(cache,0700)&&access(cache,F_OK))fail("cache directory");if(setenv("MESA_SHADER_CACHE_DIR",cache,1))fail("cache env");
  void *loader_tools=dlopen(tools,RTLD_NOW|RTLD_LOCAL);if(!loader_tools)fail(dlerror());
  typedef void *(*open_fn)(int,int,const char*,const char*,const char*,const char*,const char*,void**);
  open_fn open_driver=(open_fn)dlsym(loader_tools,"adrenotools_open_libvulkan");if(!open_driver)fail("adrenotools entry");
  void *loader=open_driver(RTLD_NOW|RTLD_LOCAL,ADRENOTOOLS_DRIVER_CUSTOM,NULL,hook_directory,driver_directory,"libvulkan_freedreno.so",NULL,NULL);if(!loader)fail("custom loader");
  PFN_vkGetInstanceProcAddr proc=(PFN_vkGetInstanceProcAddr)dlsym(loader,"vkGetInstanceProcAddr");if(!proc)fail("custom Vulkan entry");
  int(*runs[])(const char*,const char*,PFN_vkGetInstanceProcAddr,int)={dm_native_linear_shape_0,dm_native_linear_shape_1,dm_native_linear_shape_2,dm_native_linear_shape_3,dm_native_linear_shape_4,dm_native_linear_shape_5,dm_native_linear_shape_6,dm_native_linear_shape_7};
  const unsigned widths[]={1,5,63,64,95,128,128,256},heights[]={1,7,65,64,97,128,256,256},levels[]={1,3,7,7,7,8,9,9};
  unsigned completed=0;
  if(real_resource) {
    char directory[4096];path(directory,root,"resource4");
    rc=runs[7]("--resource-bake",directory,proc,0);
  }
  for(unsigned n=0;!real_resource && n<8;n++)if(selected_shape<0 || n==(unsigned)selected_shape){
    char leaf[64],directory[4096];snprintf(leaf,sizeof leaf,"shape-%u-%ux%u",n,widths[n],heights[n]);path(directory,root,leaf);if(mkdir(directory,0700))fail("fresh shape output required");
    printf("{\"native_linear_shape_begin\":true,\"index\":%u,\"width\":%u,\"height\":%u,\"levels\":%u}\n",n,widths[n],heights[n],levels[n]);
    rc=runs[n]("--native-linear-matrix",directory,proc,0);if(rc)break;completed++;
    printf("{\"native_linear_shape_complete\":true,\"index\":%u,\"width\":%u,\"height\":%u,\"levels\":%u,\"formats\":4}\n",n,widths[n],heights[n],levels[n]);
  }
  printf("{\"native_linear_matrix_terminal\":true,\"uid\":%u,\"pid\":%u,\"rc\":%d,\"completed_shapes\":%u,\"GPU_sampling_requested\":%s,\"real_resource_bake\":%s,\"game_modules_executed\":false}\n",getuid(),getpid(),rc,completed,real_resource?"false":"true",real_resource?"true":"false");
  (*env)->ReleaseStringUTFChars(env,root_string,root);(*env)->ReleaseStringUTFChars(env,hooks_string,hooks);
  // Keep loader and tools alive until process cleanup, matching original loader.
  return rc;
}
