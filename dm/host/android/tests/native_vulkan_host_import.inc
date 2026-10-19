// Standard Vulkan byte oracle for driver-owned anonymous pages only. This mode
// returns before NVN/device/dirty-pool initialization and does not load a game.
#include "../../../gfx/vulkan/nvn_vk_loader.h"
#include <sys/mman.h>

typedef struct {
  VkInstance instance;
  VkPhysicalDevice physical;
  VkDevice device;
  VkQueue queue;
  VkCommandPool pool;
  VkCommandBuffer command[2];
  VkFence fence[2];
  VkSemaphore gate;
  uint64_t release_value;
  VkDebugUtilsMessengerEXT messenger;
  unsigned validation_errors;
} host_import_context;

static VkBool32 VKAPI_PTR host_import_validation(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT *data, void *user) {
  (void)type;
  host_import_context *context=user;
  if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    __atomic_fetch_add(&context->validation_errors,1,__ATOMIC_RELAXED);
  LOG("[host import validation] %s: %s",data->pMessageIdName?data->pMessageIdName:"",data->pMessage?data->pMessage:"");
  return VK_FALSE;
}
static int host_import_extension(VkPhysicalDevice physical,const char *name) {
  uint32_t count=0;
  CHECK(vkEnumerateDeviceExtensionProperties(physical,NULL,&count,NULL)==VK_SUCCESS);
  VkExtensionProperties *items=calloc(count?count:1,sizeof *items);CHECK(items!=NULL);
  CHECK(vkEnumerateDeviceExtensionProperties(physical,NULL,&count,items)==VK_SUCCESS);
  int found=0;
  for(uint32_t i=0;i<count;i++)if(!strcmp(items[i].extensionName,name))found=1;
  free(items);return found;
}
static uint32_t host_import_memory_type(VkPhysicalDevice physical,uint32_t bits,
                                        VkMemoryPropertyFlags required) {
  VkPhysicalDeviceMemoryProperties properties;
  vkGetPhysicalDeviceMemoryProperties(physical,&properties);
  for(uint32_t i=0;i<properties.memoryTypeCount;i++)
    if((bits&(1u<<i)) && (properties.memoryTypes[i].propertyFlags&required)==required)
      return i;
  die("Host import fixture has no compatible memory type");
}
static void host_import_context_destroy(host_import_context *context) {
  if(context->device) {
    for(unsigned i=0;i<2;i++)vkDestroyFence(context->device,context->fence[i],NULL);
    vkDestroySemaphore(context->device,context->gate,NULL);
    vkDestroyCommandPool(context->device,context->pool,NULL);
    vkDestroyDevice(context->device,NULL);
  }
  if(context->messenger)
    vkDestroyDebugUtilsMessengerEXT(context->instance,context->messenger,NULL);
  vkDestroyInstance(context->instance,NULL);
  CHECK(__atomic_load_n(&context->validation_errors,__ATOMIC_RELAXED)==0);
}
static uint32_t host_import_pattern(unsigned pass,unsigned slot,size_t word) {
  return UINT32_C(0x85ac3107)^(uint32_t)word*UINT32_C(0x9e3779b9)^pass*65537u^slot*257u;
}
static void host_import_begin(host_import_context *context,unsigned slot) {
  CHECK(vkResetFences(context->device,1,&context->fence[slot])==VK_SUCCESS);
  CHECK(vkResetCommandBuffer(context->command[slot],0)==VK_SUCCESS);
  VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  CHECK(vkBeginCommandBuffer(context->command[slot],&begin)==VK_SUCCESS);
}
static void host_import_submit(host_import_context *context,unsigned slot) {
  CHECK(vkEndCommandBuffer(context->command[slot])==VK_SUCCESS);
  if(!slot)context->release_value++;
  VkTimelineSemaphoreSubmitInfo timeline={.sType=VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
    .waitSemaphoreValueCount=1,.pWaitSemaphoreValues=&context->release_value};
  VkPipelineStageFlags stage=VK_PIPELINE_STAGE_TRANSFER_BIT;
  VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.pNext=&timeline,
    .waitSemaphoreCount=1,.pWaitSemaphores=&context->gate,.pWaitDstStageMask=&stage,
    .commandBufferCount=1,.pCommandBuffers=&context->command[slot]};
  CHECK(vkQueueSubmit(context->queue,1,&submit,context->fence[slot])==VK_SUCCESS);
}
static void host_import_wait(host_import_context *context) {
  // Both submissions are pending behind this host-signaled timeline value.
  // No GPU work can consume their imported data before both submits return.
  VkSemaphoreSignalInfo signal={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
    .semaphore=context->gate,.value=context->release_value};
  CHECK(vkSignalSemaphoreKHR(context->device,&signal)==VK_SUCCESS);
  // A bounded standard fence wait is functional synchronization, not timing.
  CHECK(vkWaitForFences(context->device,2,context->fence,VK_TRUE,UINT64_C(10000000000))==VK_SUCCESS);
}
static void host_import_barrier(VkCommandBuffer command,VkPipelineStageFlags source,
                                 VkPipelineStageFlags destination,VkAccessFlags writes,
                                 VkAccessFlags reads) {
  VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .srcAccessMask=writes,.dstAccessMask=reads};
  vkCmdPipelineBarrier(command,source,destination,0,1,&barrier,0,NULL,0,NULL);
}
static void host_import_round(host_import_context *context,VkDeviceSize alignment,unsigned round,
                               PFN_vkGetMemoryHostPointerPropertiesEXT properties) {
  const size_t extent=(size_t)alignment*2,mapping_size=(size_t)alignment*3;
  void *mapping=mmap(NULL,mapping_size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
  CHECK(mapping!=MAP_FAILED);
  uint32_t *host=(void *)(((uintptr_t)mapping+(uintptr_t)alignment-1)&~((uintptr_t)alignment-1));
  memset(host,0,extent);
  VkMemoryHostPointerPropertiesEXT host_properties={
    .sType=VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT};
  CHECK(properties(context->device,VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
    host,&host_properties)==VK_SUCCESS);
  CHECK(host_properties.memoryTypeBits!=0);
  VkExternalMemoryBufferCreateInfo external={.sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
    .handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
  VkBufferCreateInfo create={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.pNext=&external,
    .size=alignment,.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT};
  VkBuffer imported[2];VkMemoryRequirements requirements[2];
  uint32_t memory_bits=host_properties.memoryTypeBits;
  for(unsigned i=0;i<2;i++) {
    CHECK(vkCreateBuffer(context->device,&create,NULL,&imported[i])==VK_SUCCESS);
    vkGetBufferMemoryRequirements(context->device,imported[i],&requirements[i]);
    CHECK(requirements[i].size<=alignment && alignment%requirements[i].alignment==0);
    memory_bits&=requirements[i].memoryTypeBits;
  }
  VkImportMemoryHostPointerInfoEXT import={.sType=VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
    .handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,.pHostPointer=host};
  VkMemoryAllocateInfo allocation={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.pNext=&import,
    .allocationSize=extent,.memoryTypeIndex=host_import_memory_type(context->physical,memory_bits,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
  VkDeviceMemory memory;
  CHECK(vkAllocateMemory(context->device,&allocation,NULL,&memory)==VK_SUCCESS);
  for(unsigned i=0;i<2;i++)
    CHECK(vkBindBufferMemory(context->device,imported[i],memory,alignment*i)==VK_SUCCESS);
  void *alias=NULL;
  CHECK(vkMapMemory(context->device,memory,0,extent,0,&alias)==VK_SUCCESS);
  CHECK(alias!=NULL && !memcmp(alias,host,extent));
  vkUnmapMemory(context->device,memory);
  // Original host memory must survive unmap and remains the sole test source.
  host[0]=UINT32_C(0x42b718e3);CHECK(host[0]==UINT32_C(0x42b718e3));

  VkBuffer readback;create.pNext=NULL;create.size=extent;
  CHECK(vkCreateBuffer(context->device,&create,NULL,&readback)==VK_SUCCESS);
  VkMemoryRequirements read_requirements;
  vkGetBufferMemoryRequirements(context->device,readback,&read_requirements);
  allocation.pNext=NULL;allocation.allocationSize=read_requirements.size;
  allocation.memoryTypeIndex=host_import_memory_type(context->physical,read_requirements.memoryTypeBits,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VkDeviceMemory read_memory;
  CHECK(vkAllocateMemory(context->device,&allocation,NULL,&read_memory)==VK_SUCCESS);
  CHECK(vkBindBufferMemory(context->device,readback,read_memory,0)==VK_SUCCESS);
  uint32_t *received=NULL;
  CHECK(vkMapMemory(context->device,read_memory,0,extent,0,(void **)&received)==VK_SUCCESS);
  const size_t words=(size_t)alignment/sizeof(uint32_t);
  for(unsigned pass=0;pass<3;pass++) {
    // Every CPU edit occurs after import; an import-time flush cannot satisfy
    // this repeated coherent publication check. Two submits precede any wait.
    for(unsigned slot=0;slot<2;slot++) {
      for(size_t i=0;i<words;i++)host[slot*words+i]=host_import_pattern(round*3+pass,slot,i);
      host_import_begin(context,slot);
      host_import_barrier(context->command[slot],VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT);
      host_import_barrier(context->command[slot],VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_HOST_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
      VkBufferCopy copy={.dstOffset=alignment*slot,.size=alignment};
      vkCmdCopyBuffer(context->command[slot],imported[slot],readback,1,&copy);
      host_import_barrier(context->command[slot],VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT);
      host_import_submit(context,slot);
    }
    host_import_wait(context);
    for(unsigned slot=0;slot<2;slot++)for(size_t i=0;i<words;i++)
      CHECK(received[slot*words+i]==host_import_pattern(round*3+pass,slot,i));
    for(unsigned slot=0;slot<2;slot++) {
      host_import_begin(context,slot);
      host_import_barrier(context->command[slot],VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
      vkCmdFillBuffer(context->command[slot],imported[slot],0,alignment,
        UINT32_C(0x75b00000)^round*256u^pass*4u^slot);
      host_import_barrier(context->command[slot],VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT);
      host_import_submit(context,slot);
    }
    host_import_wait(context);
    for(unsigned slot=0;slot<2;slot++)for(size_t i=0;i<words;i++)
      CHECK(host[slot*words+i]==(UINT32_C(0x75b00000)^round*256u^pass*4u^slot));
  }
  vkUnmapMemory(context->device,read_memory);
  vkDestroyBuffer(context->device,readback,NULL);vkFreeMemory(context->device,read_memory,NULL);
  for(unsigned slot=0;slot<2;slot++)vkDestroyBuffer(context->device,imported[slot],NULL);
  vkFreeMemory(context->device,memory,NULL);
  for(size_t i=0;i<extent/4;i++)host[i]=host_import_pattern(round,7,i);
  for(size_t i=0;i<extent/4;i++)CHECK(host[i]==host_import_pattern(round,7,i));
  CHECK(munmap(mapping,mapping_size)==0);
}
static int host_import_oracle(void) {
  CHECK(!V.device && !V.instance);
  host_import_context context={0};
  const int validate=getenv("DM_VULKAN_VALIDATE")!=NULL;
  vk_loader_initialize();
  VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pApplicationName="d2r-native-host-import-oracle",.apiVersion=VK_API_VERSION_1_1};
  const char *layer="VK_LAYER_KHRONOS_validation";
  const char *instance_extensions[]={VK_EXT_DEBUG_UTILS_EXTENSION_NAME,VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME};
  VkValidationFeatureEnableEXT sync=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
  VkValidationFeaturesEXT features={.sType=VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
    .enabledValidationFeatureCount=1,.pEnabledValidationFeatures=&sync};
  VkDebugUtilsMessengerCreateInfoEXT debug={.sType=VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
    .pNext=getenv("DM_VULKAN_SYNC_VALIDATE")?&features:NULL,
    .messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
    .messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|
      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,.pfnUserCallback=host_import_validation,.pUserData=&context};
  VkInstanceCreateInfo instance={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
  if(validate) {
    instance.pNext=&debug;instance.enabledLayerCount=1;instance.ppEnabledLayerNames=&layer;
    instance.enabledExtensionCount=debug.pNext?2:1;instance.ppEnabledExtensionNames=instance_extensions;
  }
  CHECK(vkCreateInstance(&instance,NULL,&context.instance)==VK_SUCCESS);
  volkLoadInstance(context.instance);
  if(validate) {
    CHECK(vkCreateDebugUtilsMessengerEXT!=NULL);
    debug.pNext=NULL;
    CHECK(vkCreateDebugUtilsMessengerEXT(context.instance,&debug,NULL,&context.messenger)==VK_SUCCESS);
  }
  uint32_t physical_count=0;CHECK(vkEnumeratePhysicalDevices(context.instance,&physical_count,NULL)==VK_SUCCESS);
  CHECK(physical_count>0);VkPhysicalDevice *physical=calloc(physical_count,sizeof *physical);CHECK(physical!=NULL);
  CHECK(vkEnumeratePhysicalDevices(context.instance,&physical_count,physical)==VK_SUCCESS);
  context.physical=physical[0];free(physical);
  VkPhysicalDeviceProperties device_properties;
  vkGetPhysicalDeviceProperties(context.physical,&device_properties);
  LOG("[host import oracle] uid=%lu device=%s opt_in=%s validation=%d",
    (unsigned long)getuid(),device_properties.deviceName,getenv("TU_DM_HOST_IMPORT")?getenv("TU_DM_HOST_IMPORT"):"unset",validate);
  if(!host_import_extension(context.physical,VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME)) {
    host_import_context_destroy(&context);
    printf("{\"test\":\"host-import-standard-api\",\"result\":\"unsupported\",\"reason\":\"extension_absent\",\"uid\":%lu}\n",(unsigned long)getuid());
    return 0;
  }
  VkPhysicalDeviceExternalMemoryHostPropertiesEXT host_properties={
    .sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT};
  VkPhysicalDeviceProperties2 properties={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&host_properties};
  vkGetPhysicalDeviceProperties2(context.physical,&properties);
  VkDeviceSize alignment=host_properties.minImportedHostPointerAlignment;
  CHECK(alignment>=4 && !(alignment&(alignment-1)));
  if(alignment>65536) {
    host_import_context_destroy(&context);
    printf("{\"test\":\"host-import-standard-api\",\"result\":\"unsupported\",\"reason\":\"fixture_alignment_limit\"}\n");return 0;
  }
  VkPhysicalDeviceExternalBufferInfo query={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO,
    .usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    .handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
  VkExternalBufferProperties external={.sType=VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES};
  vkGetPhysicalDeviceExternalBufferProperties(context.physical,&query,&external);
  CHECK(external.externalMemoryProperties.externalMemoryFeatures&VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT);
  CHECK(external.externalMemoryProperties.compatibleHandleTypes&query.handleType);
  if(strstr(device_properties.deviceName,"Turnip")) {
    CHECK(!(external.externalMemoryProperties.externalMemoryFeatures&VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT));
    query.flags=VK_BUFFER_CREATE_SPARSE_BINDING_BIT;
    vkGetPhysicalDeviceExternalBufferProperties(context.physical,&query,&external);
    CHECK(external.externalMemoryProperties.externalMemoryFeatures==0);
    query.flags=0;query.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_MAPPED_FOREIGN_MEMORY_BIT_EXT;
    vkGetPhysicalDeviceExternalBufferProperties(context.physical,&query,&external);
    CHECK(external.externalMemoryProperties.externalMemoryFeatures==0);
    VkPhysicalDeviceExternalImageFormatInfo image_external={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
      .handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
    VkPhysicalDeviceImageFormatInfo2 image={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
      .pNext=&image_external,.format=VK_FORMAT_R8G8B8A8_UNORM,.type=VK_IMAGE_TYPE_2D,
      .tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    VkExternalImageFormatProperties image_external_properties={.sType=VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
    VkImageFormatProperties2 image_properties={.sType=VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,.pNext=&image_external_properties};
    CHECK(vkGetPhysicalDeviceImageFormatProperties2(context.physical,&image,&image_properties)==VK_ERROR_FORMAT_NOT_SUPPORTED);
  }
  VkPhysicalDeviceTimelineSemaphoreFeatures timeline_features={
    .sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
  VkPhysicalDeviceFeatures2 physical_features={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
    .pNext=&timeline_features};
  vkGetPhysicalDeviceFeatures2(context.physical,&physical_features);
  if(!timeline_features.timelineSemaphore ||
     !host_import_extension(context.physical,VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME)) {
    host_import_context_destroy(&context);
    printf("{\"test\":\"host-import-standard-api\",\"result\":\"unsupported\",\"reason\":\"timeline_fixture_prerequisite\"}\n");return 0;
  }
  uint32_t queue_count=0;vkGetPhysicalDeviceQueueFamilyProperties(context.physical,&queue_count,NULL);
  VkQueueFamilyProperties *queues=calloc(queue_count?queue_count:1,sizeof *queues);CHECK(queues!=NULL);
  vkGetPhysicalDeviceQueueFamilyProperties(context.physical,&queue_count,queues);uint32_t family=UINT32_MAX;
  for(uint32_t i=0;i<queue_count;i++)if(queues[i].queueCount &&
      (queues[i].queueFlags&(VK_QUEUE_TRANSFER_BIT|VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))) {family=i;break;}
  free(queues);CHECK(family!=UINT32_MAX);float priority=1;
  VkDeviceQueueCreateInfo queue={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,
    .queueCount=1,.pQueuePriorities=&priority};
  const char *device_extensions[]={VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME,
    VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME};
  VkDeviceCreateInfo device={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&timeline_features,
    .queueCreateInfoCount=1,.pQueueCreateInfos=&queue,
    .enabledExtensionCount=2,.ppEnabledExtensionNames=device_extensions};
  CHECK(vkCreateDevice(context.physical,&device,NULL,&context.device)==VK_SUCCESS);
  volkLoadDevice(context.device);vkGetDeviceQueue(context.device,family,0,&context.queue);
  PFN_vkGetMemoryHostPointerPropertiesEXT get_properties=(void *)vkGetDeviceProcAddr(context.device,"vkGetMemoryHostPointerPropertiesEXT");
  CHECK(get_properties!=NULL && vkSignalSemaphoreKHR!=NULL);
  VkSemaphoreTypeCreateInfo timeline_type={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
    .semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE,.initialValue=0};
  VkSemaphoreCreateInfo semaphore={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,.pNext=&timeline_type};
  CHECK(vkCreateSemaphore(context.device,&semaphore,NULL,&context.gate)==VK_SUCCESS);
  VkCommandPoolCreateInfo pool={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    .queueFamilyIndex=family};CHECK(vkCreateCommandPool(context.device,&pool,NULL,&context.pool)==VK_SUCCESS);
  VkCommandBufferAllocateInfo commands={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=context.pool,
    .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=2};
  CHECK(vkAllocateCommandBuffers(context.device,&commands,context.command)==VK_SUCCESS);
  VkFenceCreateInfo fence={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  for(unsigned i=0;i<2;i++)CHECK(vkCreateFence(context.device,&fence,NULL,&context.fence[i])==VK_SUCCESS);
  for(unsigned round=0;round<4;round++)host_import_round(&context,alignment,round,get_properties);
  host_import_context_destroy(&context);CHECK(!V.device && !V.instance);
  printf("{\"test\":\"host-import-standard-api\",\"result\":\"passed\",\"checks\":%u,\"uid\":%lu,\"rounds\":4,\"submits_before_each_wait\":2,\"timeline_gate\":true,\"alignment\":%llu,\"import_bytes\":%llu,\"validation_requested\":%s}\n",
    checks,(unsigned long)getuid(),(unsigned long long)alignment,(unsigned long long)alignment*2,validate?"true":"false");
  return 0;
}
