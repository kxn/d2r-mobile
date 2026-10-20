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
  VkBool32 imported_host_coherent;
  VkBool32 implicit_host_dependency,owned_dirty_cycle,shared_anonymous;
  VkDeviceSize noncoherent_atom;
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
                                        VkMemoryPropertyFlags required,
                                        VkBool32 prefer_coherent) {
  VkPhysicalDeviceMemoryProperties properties;
  vkGetPhysicalDeviceMemoryProperties(physical,&properties);
  for(unsigned pass=0;pass<(prefer_coherent?2u:1u);pass++)
    for(uint32_t i=0;i<properties.memoryTypeCount;i++)
      if((bits&(1u<<i)) && (properties.memoryTypes[i].propertyFlags&required)==required &&
          (!prefer_coherent || pass ||
           (properties.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)))
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
static void host_import_cache_range(host_import_context *context,VkDeviceMemory memory,
                                     VkDeviceSize offset,VkDeviceSize size,VkBool32 invalidate) {
  if(context->imported_host_coherent)return;
  CHECK(offset%context->noncoherent_atom==0 && size%context->noncoherent_atom==0);
  VkMappedMemoryRange range={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
    .memory=memory,.offset=offset,.size=size};
  CHECK((invalidate?vkInvalidateMappedMemoryRanges(context->device,1,&range):
    vkFlushMappedMemoryRanges(context->device,1,&range))==VK_SUCCESS);
}
static void host_import_round(host_import_context *context,VkDeviceSize alignment,unsigned round,
                               PFN_vkGetMemoryHostPointerPropertiesEXT properties) {
  const size_t extent=(size_t)alignment*2,mapping_size=(size_t)alignment*3;
  // Only this fixture-owned mapping changes type; never protect/rebind a driver
  // VkMap VA or any guest heap. Keep private as the original control.
  int mapping_flags=MAP_ANONYMOUS|(context->shared_anonymous?MAP_SHARED:MAP_PRIVATE);
  void *mapping=mmap(NULL,mapping_size,PROT_READ|PROT_WRITE,mapping_flags,-1,0);
  CHECK(mapping!=MAP_FAILED);
  LOG("[host import mapping] round=%u anonymous=%s import_bytes=%zu mapping_bytes=%zu",round,
    context->shared_anonymous?"shared":"private",extent,mapping_size);
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
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT,VK_TRUE)};
  VkPhysicalDeviceMemoryProperties memory_properties;
  vkGetPhysicalDeviceMemoryProperties(context->physical,&memory_properties);
  context->imported_host_coherent=(memory_properties.memoryTypes[allocation.memoryTypeIndex].propertyFlags&
    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)!=0;
  CHECK(context->imported_host_coherent || alignment%context->noncoherent_atom==0);
  LOG("[host import oracle] round=%u import_type=%u flags=%#x host_coherent=%u atom=%llu",round,
    allocation.memoryTypeIndex,memory_properties.memoryTypes[allocation.memoryTypeIndex].propertyFlags,
    context->imported_host_coherent,(unsigned long long)context->noncoherent_atom);
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
  // Keep the imported Vulkan memory mapped while using standard mapped-range APIs.
  CHECK(vkMapMemory(context->device,memory,0,extent,0,&alias)==VK_SUCCESS);

  VkBuffer readback;create.pNext=NULL;create.size=extent;
  CHECK(vkCreateBuffer(context->device,&create,NULL,&readback)==VK_SUCCESS);
  VkMemoryRequirements read_requirements;
  vkGetBufferMemoryRequirements(context->device,readback,&read_requirements);
  allocation.pNext=NULL;allocation.allocationSize=read_requirements.size;
  allocation.memoryTypeIndex=host_import_memory_type(context->physical,read_requirements.memoryTypeBits,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,VK_FALSE);
  VkDeviceMemory read_memory;
  CHECK(vkAllocateMemory(context->device,&allocation,NULL,&read_memory)==VK_SUCCESS);
  CHECK(vkBindBufferMemory(context->device,readback,read_memory,0)==VK_SUCCESS);
  uint32_t *received=NULL;
  CHECK(vkMapMemory(context->device,read_memory,0,extent,0,(void **)&received)==VK_SUCCESS);
  const size_t words=(size_t)alignment/sizeof(uint32_t);
  if(context->owned_dirty_cycle)nvn_dirty_add_range(host,extent);
  for(unsigned pass=0;pass<3;pass++) {
    uint64_t dirty_stamp=nvn_dirty_now(),faults_before=0,preps_before=0;
    if(context->owned_dirty_cycle) {
      nvn_dirty_stats(&faults_before,&preps_before);
      nvn_dirty_protect(host,extent);
    }
    // Every CPU edit occurs after import; an import-time flush cannot satisfy
    // repeated publication. Noncoherent memory uses standard range operations.
    // Two submits precede any wait.
    for(unsigned slot=0;slot<2;slot++) {
      if(context->owned_dirty_cycle && pass!=1)dm_guest_write_prepare(host+slot*words,alignment);
      for(size_t i=0;i<words;i++)host[slot*words+i]=host_import_pattern(round*3+pass,slot,i);
      host_import_cache_range(context,memory,alignment*slot,alignment,VK_FALSE);
      host_import_begin(context,slot);
      host_import_barrier(context->command[slot],VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT);
      if(!context->implicit_host_dependency)
        host_import_barrier(context->command[slot],VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_ACCESS_HOST_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
      VkBufferCopy copy={.dstOffset=alignment*slot,.size=alignment};
      vkCmdCopyBuffer(context->command[slot],imported[slot],readback,1,&copy);
      host_import_barrier(context->command[slot],VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT);
      host_import_submit(context,slot);
    }
    host_import_wait(context);
    if(context->owned_dirty_cycle) {
      uint64_t faults_after,preps_after;nvn_dirty_stats(&faults_after,&preps_after);
      CHECK(nvn_dirty_since(host,extent,dirty_stamp));
      CHECK(pass==1?faults_after>faults_before:preps_after>preps_before);
      printf("{\"test_details\":\"standard-owned-dirty-cycle\",\"round\":%u,\"pass\":%u,\"cpu_write_path\":\"%s\","
        "\"cpu_first\":%u,\"GPU_copy_readback_first\":%u,\"faults\":%llu,\"prepares\":%llu}\n",
        round,pass,pass==1?"original-fault":"original-prepare",((uint8_t *)host)[0],((uint8_t *)received)[0],
        (unsigned long long)(faults_after-faults_before),(unsigned long long)(preps_after-preps_before));
    }
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
    host_import_cache_range(context,memory,0,extent,VK_TRUE);
    for(unsigned slot=0;slot<2;slot++)for(size_t i=0;i<words;i++)
      CHECK(host[slot*words+i]==(UINT32_C(0x75b00000)^round*256u^pass*4u^slot));
  }
  vkUnmapMemory(context->device,read_memory);
  vkDestroyBuffer(context->device,readback,NULL);vkFreeMemory(context->device,read_memory,NULL);
  for(unsigned slot=0;slot<2;slot++)vkDestroyBuffer(context->device,imported[slot],NULL);
  if(context->owned_dirty_cycle)nvn_dirty_remove_range(host);
  vkUnmapMemory(context->device,memory);
  vkFreeMemory(context->device,memory,NULL);
  CHECK(vkDeviceWaitIdle(context->device)==VK_SUCCESS);
  for(size_t i=0;i<extent/4;i++)host[i]=host_import_pattern(round,7,i);
  for(size_t i=0;i<extent/4;i++)CHECK(host[i]==host_import_pattern(round,7,i));
  CHECK(munmap(mapping,mapping_size)==0);
}
void dm_vulkan_test_implicit_host(int enabled) {
  if(V.device || V.instance)die("Host publication fixture requires a cold process");
  if(enabled)setenv("DM_VULKAN_HOST_IMPORT_IMPLICIT_HOST","1",1);
  else unsetenv("DM_VULKAN_HOST_IMPORT_IMPLICIT_HOST");
}
void dm_vulkan_test_owned_dirty(int enabled) {
  if(V.device || V.instance)die("Owned dirty fixture requires a cold process");
  if(enabled)setenv("DM_VULKAN_HOST_IMPORT_OWNED_DIRTY","1",1);
  else unsetenv("DM_VULKAN_HOST_IMPORT_OWNED_DIRTY");
}
static int host_import_oracle(void) {
  CHECK(!V.device && !V.instance);
  host_import_context context={0};
  const char *implicit=getenv("DM_VULKAN_HOST_IMPORT_IMPLICIT_HOST");
  CHECK(!implicit || !strcmp(implicit,"0") || !strcmp(implicit,"1"));
  context.implicit_host_dependency=implicit && !strcmp(implicit,"1");
  const char *dirty=getenv("DM_VULKAN_HOST_IMPORT_OWNED_DIRTY");
  CHECK(!dirty || !strcmp(dirty,"0") || !strcmp(dirty,"1"));
  context.owned_dirty_cycle=dirty && !strcmp(dirty,"1");
  const char *shared=getenv("DM_VULKAN_HOST_IMPORT_SHARED_ANON");
  CHECK(!shared || !strcmp(shared,"0") || !strcmp(shared,"1"));
  context.shared_anonymous=shared && !strcmp(shared,"1");
  printf("{\"test_configuration\":\"host-import-anonymous-mapping\",\"kind\":\"%s\","
    "\"scope\":\"fixture-owned mapping only; no guest heap or driver mapping mutation\"}\n",
    context.shared_anonymous?"shared":"private");
  printf("{\"test_configuration\":\"standard-owned-dirty\",\"enabled\":%s}\n",
    context.owned_dirty_cycle?"true":"false");
  printf("{\"test_configuration\":\"host-import-publication\",\"implicit_host_dependency\":%s}\n",
    context.implicit_host_dependency?"true":"false");
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
  if(validate) {
    uint32_t layer_count=0;
    VkResult enumeration=vkEnumerateInstanceLayerProperties(&layer_count,NULL);
    LOG("[host import oracle] layer_enumeration=%d count=%u",enumeration,layer_count);
    if(enumeration==VK_SUCCESS && layer_count<=256) {
      VkLayerProperties *layers=calloc(layer_count?layer_count:1,sizeof *layers);
      CHECK(layers!=NULL);
      enumeration=vkEnumerateInstanceLayerProperties(&layer_count,layers);
      if(enumeration==VK_SUCCESS)
        for(uint32_t i=0;i<layer_count;i++)LOG("[host import oracle] available_layer=%s",layers[i].layerName);
      free(layers);
    }
  }
  VkResult instance_result=vkCreateInstance(&instance,NULL,&context.instance);
  LOG("[host import oracle] create_instance_result=%d validation=%d",instance_result,validate);
  CHECK(instance_result==VK_SUCCESS);
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
  context.noncoherent_atom=device_properties.limits.nonCoherentAtomSize;
  CHECK(context.noncoherent_atom && !(context.noncoherent_atom&(context.noncoherent_atom-1)));
  LOG("[host import oracle] uid=%lu device=%s turnip_opt_in=%s panvk_opt_in=%s validation=%d",
    (unsigned long)getuid(),device_properties.deviceName,getenv("TU_DM_HOST_IMPORT")?getenv("TU_DM_HOST_IMPORT"):"unset",
    getenv("PANVK_DM_HOST_IMPORT")?getenv("PANVK_DM_HOST_IMPORT"):"unset",validate);
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
  if(strstr(device_properties.deviceName,"Turnip") || strstr(device_properties.deviceName,"Mali")) {
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
  printf("{\"test_details\":\"host-import-memory\",\"host_coherent\":%s,\"noncoherent_atom\":%llu}\n",
    context.imported_host_coherent?"true":"false",(unsigned long long)context.noncoherent_atom);
  host_import_context_destroy(&context);CHECK(!V.device && !V.instance);
  printf("{\"test\":\"host-import-standard-api\",\"result\":\"passed\",\"checks\":%u,\"uid\":%lu,\"rounds\":4,\"submits_before_each_wait\":2,\"timeline_gate\":true,\"alignment\":%llu,\"import_bytes\":%llu,\"validation_requested\":%s}\n",
    checks,(unsigned long)getuid(),(unsigned long long)alignment,(unsigned long long)alignment*2,validate?"true":"false");
  return 0;
}
