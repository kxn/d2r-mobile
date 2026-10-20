/* Calls the actual debug bridge CreateDevice function with capability-query
 * stubs. Host unit contract only, not a Vulkan/device acceptance test. */
#include "replay_bridge.c"
#include <assert.h>
#include <sys/wait.h>
#include <signal.h>
static unsigned ext_queries,feature_queries,device_calls;
static int supports_bda=1,supports_extension=1,forbid_device,forbid_feature;
static const VkDeviceCreateInfo *original;
static VkResult VKAPI_CALL mock_extensions(VkPhysicalDevice p,const char *layer,uint32_t *count,VkExtensionProperties *out) {
  (void)p;(void)layer;ext_queries++;
  if(!out){*count=supports_extension?1:0;return VK_SUCCESS;}
  assert(*count>=1);*count=1;strcpy(out[0].extensionName,VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);return VK_SUCCESS;
}
static void VKAPI_CALL mock_features(VkPhysicalDevice p,VkPhysicalDeviceFeatures2 *out) {
  (void)p;if(forbid_feature)_exit(7);feature_queries++;
  assert(out->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
  VkPhysicalDeviceBufferDeviceAddressFeatures *b=out->pNext;
  assert(b&&b->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES);
  b->bufferDeviceAddress=supports_bda;/* Deliberately support more than requested. */
  b->bufferDeviceAddressCaptureReplay=VK_TRUE;b->bufferDeviceAddressMultiDevice=VK_TRUE;
}
static VkResult VKAPI_CALL mock_device(VkPhysicalDevice p,const VkDeviceCreateInfo *in,const VkAllocationCallbacks *a,VkDevice *out) {
  (void)p;(void)a;(void)out;if(forbid_device)_exit(9);device_calls++;
  if(!tool_rebind_bda){assert(in==original);assert(!ext_queries&&!feature_queries);}
  else {
    assert(ext_queries==2&&feature_queries==1);
    assert(in->queueCreateInfoCount==original->queueCreateInfoCount&&in->pQueueCreateInfos==original->pQueueCreateInfos&&in->pEnabledFeatures==original->pEnabledFeatures);
    assert(in->enabledExtensionCount==1&&!strcmp(in->ppEnabledExtensionNames[0],VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME));
    const VkPhysicalDeviceBufferDeviceAddressFeatures *b=in->pNext;
    assert(b->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES&&b->bufferDeviceAddress);
    assert(!b->bufferDeviceAddressCaptureReplay&&!b->bufferDeviceAddressMultiDevice&&b->pNext==original->pNext);
  }
  return VK_SUCCESS;
}
static void must_abort(VkDeviceCreateInfo *ci,int ext,int feature,int forbidden_query) {
  pid_t child=fork();assert(child>=0);
  if(!child){supports_extension=ext;supports_bda=feature;forbid_feature=forbidden_query;forbid_device=1;ext_queries=feature_queries=0;VkDevice dev;create_device(VK_NULL_HANDLE,ci,NULL,&dev);_exit(8);}
  int status;assert(waitpid(child,&status,0)==child);assert(WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
}
int main(void) {
  atomic_store(&real_create_device,mock_device);atomic_store(&real_extensions,mock_extensions);atomic_store(&real_features2,mock_features);
  VkPhysicalDeviceFeatures enabled={.robustBufferAccess=VK_TRUE};
  VkPhysicalDeviceFeatures2 old_chain={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  VkDeviceCreateInfo ci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&old_chain,.pEnabledFeatures=&enabled};
  original=&ci;VkDeviceCreateInfo before=ci;VkPhysicalDeviceFeatures2 before_chain=old_chain;VkDevice dev;
  tool_rebind_bda=0;assert(create_device(VK_NULL_HANDLE,&ci,NULL,&dev)==VK_SUCCESS&&device_calls==1);assert(!memcmp(&ci,&before,sizeof ci));
  device_calls=0;tool_rebind_bda=1;assert(create_device(VK_NULL_HANDLE,&ci,NULL,&dev)==VK_SUCCESS&&device_calls==1);assert(!memcmp(&ci,&before,sizeof ci)&&!memcmp(&old_chain,&before_chain,sizeof old_chain));
  must_abort(&ci,0,1,1);must_abort(&ci,1,0,0);
  VkPhysicalDeviceBufferDeviceAddressFeatures disabled={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};ci.pNext=&disabled;must_abort(&ci,1,1,0);
  puts("PASS actual bridge entry: OFF same pointer and zero support queries; ON actual support queries before unchanged-input CreateDevice, no extra captureReplay/multiDevice; real fail() SIGABRT negatives for unsupported extension/feature and existing disabled feature. Host stubs only, no GPU/ADB.");
  return 0;
}
