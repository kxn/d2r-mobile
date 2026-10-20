// Ordinary-UID thin entry for the existing original022 NVN functional fixture.
#include "dm.h"
#include "../fixture_output.h"
#include "../../../gfx/vulkan/nvn_vk_internal.h"
#include "../../../gfx/nvn/nvn_mtl.h"
#include "../../../gfx/vulkan/nvn_vk_loader.h"
#include <jni.h>
#include <stdatomic.h>
#include <unistd.h>
extern int dm_vulkan_test_main(void);
static void bc3_fixture_path(char out[4096],const char *root,const char *leaf) {
  int n=snprintf(out,4096,"%s/%s",root,leaf);if(n<0 || n>=4096)die("BC3 fixture path overflow");
}
static void bc3_fixture_env(const char *name,const char *value) {if(setenv(name,value,1))die("BC3 fixture environment");}
JNIEXPORT jint JNICALL Java_com_d2rmobile_panvkbc3fixture_MainActivity_nativeRun(
    JNIEnv *env,jclass klass,jstring directory,jstring libraries) {
  (void)klass;static atomic_int started;
  if(getuid()<10000 || vk_loader_is_initialized() || atomic_exchange(&started,1))return 78;
  const char *root=(*env)->GetStringUTFChars(env,directory,NULL);if(!root)return 79;
  const char *hooks=(*env)->GetStringUTFChars(env,libraries,NULL);if(!hooks){(*env)->ReleaseStringUTFChars(env,directory,root);return 79;}
  if(root[0]!='/' || hooks[0]!='/')die("BC3 fixture requires app-private absolute paths");
  char output[4096],dataset[4096],driver[4096],shaders[4096],cache[4096];
  bc3_fixture_path(output,root,"bc3-test.log");bc3_fixture_path(dataset,root,"bc3-capture.bin");
  bc3_fixture_path(driver,root,"driver");bc3_fixture_path(shaders,root,"vulkan-fixture");bc3_fixture_path(cache,root,"shader-cache");
  int error=dm_fixture_output_open(output);if(error)_exit(error);
  printf("{\"BC3_app_configuration\":true,\"uid\":%u,\"pid\":%u,\"provider\":\"panvk\",\"shared\":false,\"TRACE\":false,\"game_modules_executed\":false}\n",(unsigned)getuid(),(unsigned)getpid());
  if(g_nmod)die("BC3 fixture cannot execute game modules");
  vk_loader_set_driver("panvk",driver,hooks);
  bc3_fixture_env("PANVK_DEBUG","startup");bc3_fixture_env("DM_VULKAN_SHARED_POOLS","0");bc3_fixture_env("DM_GUEST_HEAP_SHARED","0");
  bc3_fixture_env("DM_VULKAN_ASYNC","1");bc3_fixture_env("DM_VULKAN_EXTERNAL_PROFILE","1");
  bc3_fixture_env("DM_VULKAN_SHADERS",shaders);bc3_fixture_env("DM_CAPTURE_TEST_DIR",root);bc3_fixture_env("MESA_SHADER_CACHE_DIR",cache);
  bc3_fixture_env("DM_PANVK_BC3_CAPTURE_FIXTURE",dataset);
  if(unsetenv("DM_PANVK_BC3_CAPTURE_NEGATIVE") || unsetenv("DM_SHARED_GUEST_HEAP_FIXTURE") || unsetenv("DM_VULKAN_VALIDATE") || unsetenv("DM_VULKAN_SYNC_VALIDATE"))die("BC3 fixture requires uncombined cold case");
  (*env)->ReleaseStringUTFChars(env,libraries,hooks);(*env)->ReleaseStringUTFChars(env,directory,root);
  int rc=dm_vulkan_test_main();
  if(g_nmod || mtl_pool_shared_mode() || V.props.deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU)die("BC3 fixture did not execute requested actual device mode");
  printf("{\"BC3_app_terminal\":true,\"uid\":%u,\"pid\":%u,\"rc\":%d,\"vendor_id\":%u,\"device_id\":%u,\"shared\":false,\"game_modules_executed\":false}\n",(unsigned)getuid(),(unsigned)getpid(),rc,V.props.vendorID,V.props.deviceID);
  return rc;
}
