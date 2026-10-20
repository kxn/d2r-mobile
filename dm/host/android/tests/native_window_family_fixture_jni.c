// Thin cold ordinary-UID entry for the EXISTING original NVN GPU fixture.
// No game module loader, renderer, CPU shader interpreter or measurement code.
#include "dm.h"
#include "guest_heap_startup.h"
#include "../fixture_output.h"
#include "../../../gfx/vulkan/nvn_vk_internal.h"
#include "../../../gfx/vulkan/nvn_vk_loader.h"
#include <jni.h>
#include <stdatomic.h>
#include <unistd.h>
extern int vk_pool_shared_mode(void);
extern int dm_vulkan_test_main(void);

// The CLI's weak declaration now resolves to the SAME real early selector as
// production, before the fixture executes original svcSetHeapSize(4 MiB).
int dm_guest_heap_startup_for_fixture(int requested) {
  return dm_guest_heap_startup_freeze(requested,vk_pool_shared_mode);
}
static void path_join(char out[4096],const char *root,const char *leaf) {
  int n=snprintf(out,4096,"%s/%s",root,leaf);
  if(n<0 || n>=4096)die("Shared window fixture path overflow");
}
static void fixture_env(const char *name,const char *value) {
  if(setenv(name,value,1))die("Shared window fixture environment");
}
static void uuid_print(const uint8_t uuid[VK_UUID_SIZE]) {
  for(unsigned n=0;n<VK_UUID_SIZE;n++)printf("%02x",uuid[n]);
}
JNIEXPORT jint JNICALL Java_com_d2rmobile_nativewindowfamilyfixture_MainActivity_nativeRun(
    JNIEnv *env,jclass klass,jstring directory,jstring library_directory,jstring pack_digest,jstring producer_digest) {
  (void)klass;static atomic_int selected;
  if(getuid()<10000 || vk_loader_is_initialized() || atomic_exchange(&selected,1))return 78;
  const char *root=(*env)->GetStringUTFChars(env,directory,NULL);
  if(!root)return 79;
  const char *hooks=(*env)->GetStringUTFChars(env,library_directory,NULL);
  if(!hooks){(*env)->ReleaseStringUTFChars(env,directory,root);return 79;}
  if(root[0]!='/' || hooks[0]!='/')die("Shared window fixture requires app-private absolute paths");
  const char *pack=(*env)->GetStringUTFChars(env,pack_digest,NULL);
  const char *producer=(*env)->GetStringUTFChars(env,producer_digest,NULL);
  if(!pack || !producer || strlen(pack)!=64 || strlen(producer)!=64)die("Native family digest length");
  for(unsigned n=0;n<64;n++)if(!((pack[n]>='0'&&pack[n]<='9')||(pack[n]>='a'&&pack[n]<='f')) ||
      !((producer[n]>='0'&&producer[n]<='9')||(producer[n]>='a'&&producer[n]<='f')))die("Native family digest schema");
  char resource[4096],table[4096];path_join(resource,root,"resource4");path_join(table,resource,"native-layouts.bin");
  char output[4096],driver[4096],shaders[4096],cache[4096];
  path_join(output,root,"native-family-test.log");path_join(driver,root,"driver");
  path_join(shaders,root,"vulkan-fixture");path_join(cache,root,"shader-cache");
  int error=dm_fixture_output_open(output);if(error)_exit(error);
  printf("{\"native_window_family_app_configuration\":true,\"uid\":%u,\"pid\":%u,"
         "\"provider\":\"adreno\",\"window_bytes\":65536,\"async\":true,"
         "\"validation\":\"disabled_explicitly\",\"game_modules_executed\":false}\n",
         (unsigned)getuid(),(unsigned)getpid());
  if(g_nmod)die("Shared window oracle cannot execute game modules");
  vk_loader_set_driver("adreno",driver,hooks);
  vk_pool_window_bytes(65536);vk_pool_mirror_reference(0);mtl_pool_defer_publication(0);
  fixture_env("TU_DM_HOST_IMPORT","true");fixture_env("DM_GUEST_HEAP_SHARED","1");
  fixture_env("DM_VULKAN_SHARED_POOLS","1");fixture_env("DM_VULKAN_POOL_WINDOW_BYTES","65536");
  fixture_env("DM_SHARED_GUEST_HEAP_FIXTURE","1");
  fixture_env("DM_NATIVE_RESOURCE_NVN_FIXTURE",resource);
  fixture_env("DM_VULKAN_NATIVE_RESOURCE_TABLE",table);
  fixture_env("DM_VULKAN_NATIVE_PACK_SHA256",pack);fixture_env("DM_VULKAN_NATIVE_PRODUCER_SHA256",producer);
  fixture_env("DM_VULKAN_ASYNC","1");fixture_env("DM_VULKAN_TEXEL_STORAGE","1");
  fixture_env("DM_VULKAN_SHADERS",shaders);fixture_env("DM_CAPTURE_TEST_DIR",root);
  fixture_env("MESA_SHADER_CACHE_DIR",cache);
  // Disable existing internal timestamps; this is a byte oracle, not profiling.
  fixture_env("DM_VULKAN_EXTERNAL_PROFILE","1");
  if(unsetenv("PANVK_DM_HOST_IMPORT") || unsetenv("DM_VULKAN_VALIDATE") || unsetenv("DM_VULKAN_SYNC_VALIDATE") ||
      unsetenv("DM_VULKAN_POOL_SYNC_REFERENCE") || unsetenv("DM_SHARED_WINDOW_DENY_CPU_OBSERVE") ||
      unsetenv("DM_SHARED_WINDOW_STARTUP_FALLBACK_FIXTURE") || unsetenv("DM_LEGACY_ASTC_COPY_FIXTURE") ||
      unsetenv("DM_SHARED_IMPORT_WINDOW_FIXTURE") || unsetenv("DM_NATIVE_LANE_WINDOW_FIXTURE"))
    die("Shared window oracle requires uncombined cold selection");
  (*env)->ReleaseStringUTFChars(env,pack_digest,pack);
  (*env)->ReleaseStringUTFChars(env,producer_digest,producer);
  (*env)->ReleaseStringUTFChars(env,library_directory,hooks);
  (*env)->ReleaseStringUTFChars(env,directory,root);
  int rc=dm_vulkan_test_main();
  int actual_astc=V.features.textureCompressionASTC_LDR && V.props.deviceType!=VK_PHYSICAL_DEVICE_TYPE_CPU;
  if(g_nmod || !vk_pool_shared_active() || !dm_guest_heap_shared_mode() || V.options.pool_window_bytes!=65536)
    die("Shared window oracle did not execute the requested immutable mode");
  VkPhysicalDeviceIDProperties id={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
  VkPhysicalDeviceProperties2 properties={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&id};
  vkGetPhysicalDeviceProperties2(V.physical,&properties);
  printf("{\"native_window_family_app_identity\":true,\"vendor_id\":%u,\"device_id\":%u,\"api_version\":%u,\"device_UUID\":\"",
         properties.properties.vendorID,properties.properties.deviceID,properties.properties.apiVersion);
  uuid_print(id.deviceUUID);printf("\",\"driver_UUID\":\"");uuid_print(id.driverUUID);
  printf("\",\"pipeline_cache_UUID\":\"");uuid_print(properties.properties.pipelineCacheUUID);printf("\"}\n");
  // The original body prints unsupported rather than faking ASTC capability.
  // This app requires that real physical branch for a complete acceptance.
  if(!rc && !actual_astc)rc=77;
  printf("{\"native_window_family_app_terminal\":true,\"uid\":%u,\"pid\":%u,\"rc\":%d,"
         "\"actual_ASTC_GPU\":%s,\"actual_shared_heap\":true,\"window_bytes\":65536,"
         "\"game_modules_executed\":false}\n",(unsigned)getuid(),(unsigned)getpid(),rc,actual_astc?"true":"false");
  return rc;
}
