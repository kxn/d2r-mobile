#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
static int fail_allocation;
static void *test_calloc(size_t n,size_t s) {return fail_allocation?NULL:calloc(n,s);}
#define calloc test_calloc
#include "device_rebind_bda.inc"
#undef calloc
static unsigned checks;
#define T(x) do {assert(x);checks++;}while(0)
static int prep(const VkDeviceCreateInfo *ci,replay_rebind_bda_state *st) {
  return replay_rebind_bda_prepare(ci,1,1,1,st);
}
int main(void) {
  replay_rebind_bda_state s;
  VkPhysicalDeviceFeatures features={.robustBufferAccess=VK_TRUE};
  VkDeviceQueueCreateInfo q={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueCount=1};
  VkDeviceCreateInfo ci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&q,.pEnabledFeatures=&features};
  VkDeviceCreateInfo saved=ci;
  /* Default OFF must not inspect a nonexistent chain or allocate/query caps. */
  ci.pNext=(void*)0x1234;fail_allocation=1;
  T(replay_rebind_bda_prepare(&ci,0,0,0,&s)==0);T(!memcmp(&s.create,&ci,sizeof ci));T(!s.owned_names);
  fail_allocation=0;ci=saved;
  T(replay_rebind_bda_prepare(&ci,1,0,1,&s)==REPLAY_BDA_UNSUPPORTED_EXTENSION);
  T(replay_rebind_bda_prepare(&ci,1,1,0,&s)==REPLAY_BDA_UNSUPPORTED_FEATURE);
  T(prep(&ci,&s)==1);T(!memcmp(&ci,&saved,sizeof ci));T(s.added_extension&&s.added_feature&&!s.existing_enabled);
  T(s.create.queueCreateInfoCount==ci.queueCreateInfoCount&&s.create.pQueueCreateInfos==ci.pQueueCreateInfos&&s.create.pEnabledFeatures==ci.pEnabledFeatures);
  T(s.create.pNext==&s.bda&&s.bda.pNext==ci.pNext&&s.bda.bufferDeviceAddress==VK_TRUE);
  T(!s.bda.bufferDeviceAddressCaptureReplay&&!s.bda.bufferDeviceAddressMultiDevice);
  T(s.create.enabledExtensionCount==1&&!strcmp(s.create.ppEnabledExtensionNames[0],VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME));replay_rebind_bda_finish(&s);T(!s.owned_names);
  const char *names[]={VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME};ci.ppEnabledExtensionNames=names;ci.enabledExtensionCount=1;
  VkPhysicalDeviceShaderFloat16Int8Features tail={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES,.shaderFloat16=VK_TRUE};
  VkPhysicalDeviceFeatures2 first={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&tail};ci.pNext=&first;
  VkPhysicalDeviceFeatures2 first_saved=first;VkPhysicalDeviceShaderFloat16Int8Features tail_saved=tail;saved=ci;
  T(prep(&ci,&s)==1);T(s.bda.pNext==&first);T(!memcmp(&first,&first_saved,sizeof first)&&!memcmp(&tail,&tail_saved,sizeof tail));T(!memcmp(&ci,&saved,sizeof ci));
  T(s.create.ppEnabledExtensionNames[0]==names[0]&&s.create.enabledExtensionCount==2);replay_rebind_bda_finish(&s);
  VkPhysicalDeviceBufferDeviceAddressFeatures bda={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,.pNext=&tail,.bufferDeviceAddress=VK_TRUE,.bufferDeviceAddressCaptureReplay=VK_TRUE};
  VkPhysicalDeviceBufferDeviceAddressFeatures bda_saved=bda;ci.pNext=&bda;
  T(prep(&ci,&s)==1);T(s.added_extension&&!s.added_feature&&s.existing_enabled&&s.create.pNext==&bda);T(!memcmp(&bda,&bda_saved,sizeof bda));replay_rebind_bda_finish(&s);
  const char *khr[]={VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME};ci.ppEnabledExtensionNames=khr;ci.enabledExtensionCount=1;
  T(prep(&ci,&s)==0);T(!s.owned_names&&!s.added_feature&&s.create.pNext==&bda);T(!memcmp(&s.create,&ci,sizeof ci));
  bda.bufferDeviceAddress=VK_FALSE;T(prep(&ci,&s)==REPLAY_BDA_EXISTING_DISABLED);T(!bda.bufferDeviceAddress);bda.bufferDeviceAddress=VK_TRUE;
  VkPhysicalDeviceVulkan12Features v12={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,.bufferDeviceAddress=VK_TRUE,.timelineSemaphore=VK_TRUE,.shaderFloat16=VK_TRUE};ci.pNext=&v12;
  VkPhysicalDeviceVulkan12Features v12_saved=v12;
  T(prep(&ci,&s)==0);T(!s.added_feature&&s.create.pNext==&v12);T(!memcmp(&v12,&v12_saved,sizeof v12));
  v12.bufferDeviceAddress=VK_FALSE;T(prep(&ci,&s)==REPLAY_BDA_EXISTING_DISABLED);v12.bufferDeviceAddress=VK_TRUE;
  v12.pNext=&bda;T(prep(&ci,&s)==REPLAY_BDA_DUPLICATE_FEATURE);v12.pNext=NULL;
  VkPhysicalDeviceVulkan12Features v12b=v12;v12.pNext=&v12b;T(prep(&ci,&s)==REPLAY_BDA_DUPLICATE_FEATURE);v12.pNext=NULL;
  VkPhysicalDeviceBufferDeviceAddressFeatures bda2=bda;bda.pNext=&bda2;ci.pNext=&bda;T(prep(&ci,&s)==REPLAY_BDA_DUPLICATE_FEATURE);bda.pNext=&bda;T(prep(&ci,&s)==REPLAY_BDA_CHAIN);bda.pNext=NULL;
  VkPhysicalDeviceBufferDeviceAddressFeaturesEXT ext={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT,.bufferDeviceAddress=VK_TRUE};ci.pNext=&ext;T(prep(&ci,&s)==REPLAY_BDA_CONFLICTING_EXT);ci.pNext=NULL;
  const char *extname[]={VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME};ci.ppEnabledExtensionNames=extname;T(prep(&ci,&s)==REPLAY_BDA_CONFLICTING_EXT);
  const char *dups[]={khr[0],khr[0]};ci.ppEnabledExtensionNames=dups;ci.enabledExtensionCount=2;T(prep(&ci,&s)==REPLAY_BDA_BAD_EXTENSIONS);
  ci.enabledExtensionCount=4097;T(prep(&ci,&s)==REPLAY_BDA_BAD_EXTENSIONS);ci.enabledExtensionCount=1;ci.ppEnabledExtensionNames=NULL;T(prep(&ci,&s)==REPLAY_BDA_BAD_EXTENSIONS);
  const char *bad[]={NULL};ci.ppEnabledExtensionNames=bad;T(prep(&ci,&s)==REPLAY_BDA_BAD_EXTENSIONS);
  ci.enabledExtensionCount=0;ci.ppEnabledExtensionNames=NULL;fail_allocation=1;T(prep(&ci,&s)==REPLAY_BDA_ALLOC);T(!s.owned_names);replay_rebind_bda_finish(&s);fail_allocation=0;
  VkBaseInStructure chain[65]={0};for(unsigned n=0;n<65;n++){chain[n].sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;chain[n].pNext=n+1<65?&chain[n+1]:NULL;}
  ci.pNext=chain;T(prep(&ci,&s)==REPLAY_BDA_CHAIN);chain[63].pNext=NULL;T(prep(&ci,&s)==1);T(s.bda.pNext==chain);replay_rebind_bda_finish(&s);
  for(int error=-1;error>=-8;error--)T(replay_rebind_bda_error(error)!=NULL);
  printf("PASS%u actual BDA bridge helper checks: OFF passthrough, queried-support rejection, unchanged original fields/nodes, no captureReplay/multiDevice additions, existing TRUE/FALSE, duplicate/conflict/cycle/limit and OOM negatives; no GPU/ADB\n",checks);
  return 0;
}
