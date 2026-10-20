/* Official replay -m rebind injects BDA buffer usage even for traces without
 * BDA. This debug-only opt-in supplies that tool requirement, never driver
 * capability emulation. Original input lists, chain nodes and feature values
 * stay untouched. Stack-owned added node lives through synchronous CreateDevice.
 */
typedef struct replay_rebind_bda_state {
  VkDeviceCreateInfo create;
  VkPhysicalDeviceBufferDeviceAddressFeatures bda;
  const char **owned_names;
  unsigned added_extension,added_feature,existing_enabled;
} replay_rebind_bda_state;
enum {
  REPLAY_BDA_UNSUPPORTED_EXTENSION=-1,REPLAY_BDA_UNSUPPORTED_FEATURE=-2,
  REPLAY_BDA_BAD_EXTENSIONS=-3,REPLAY_BDA_CONFLICTING_EXT=-4,
  REPLAY_BDA_CHAIN=-5,REPLAY_BDA_DUPLICATE_FEATURE=-6,
  REPLAY_BDA_EXISTING_DISABLED=-7,REPLAY_BDA_ALLOC=-8
};
static const char *replay_rebind_bda_error(int code) {
  switch(code) {
  case REPLAY_BDA_UNSUPPORTED_EXTENSION:return "private ICD lacks KHR buffer-device-address extension";
  case REPLAY_BDA_UNSUPPORTED_FEATURE:return "private ICD lacks normal bufferDeviceAddress feature";
  case REPLAY_BDA_BAD_EXTENSIONS:return "invalid/duplicate BDA extension list";
  case REPLAY_BDA_CONFLICTING_EXT:return "EXT BDA conflicts with required KHR BDA; original unchanged";
  case REPLAY_BDA_CHAIN:return "cyclic/overlong pNext chain";
  case REPLAY_BDA_DUPLICATE_FEATURE:return "duplicate/incompatible BDA or Vulkan12 structures";
  case REPLAY_BDA_EXISTING_DISABLED:return "existing BDA feature is disabled; cannot preserve original value";
  case REPLAY_BDA_ALLOC:return "required extension list allocation failed";
  default:return "unknown BDA tool configuration";
  }
}
static int replay_rebind_bda_prepare(const VkDeviceCreateInfo *in,
    int enabled,int supported_extension,int supported_feature,
    replay_rebind_bda_state *state) {
  memset(state,0,sizeof *state);state->create=*in;
  /* Default OFF does not inspect the chain, allocate, or require capabilities. */
  if(!enabled)return 0;
  if(!supported_extension)return REPLAY_BDA_UNSUPPORTED_EXTENSION;
  if(!supported_feature)return REPLAY_BDA_UNSUPPORTED_FEATURE;
  if(in->enabledExtensionCount>4096 ||
     (in->enabledExtensionCount&&!in->ppEnabledExtensionNames))return REPLAY_BDA_BAD_EXTENSIONS;
  unsigned khr=0;
  for(uint32_t n=0;n<in->enabledExtensionCount;n++) {
    const char *name=in->ppEnabledExtensionNames[n];if(!name)return REPLAY_BDA_BAD_EXTENSIONS;
    if(!strcmp(name,VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME))return REPLAY_BDA_CONFLICTING_EXT;
    khr+=!strcmp(name,VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
  }
  if(khr>1)return REPLAY_BDA_BAD_EXTENSIONS;
  const VkBaseInStructure *seen[64],*node=in->pNext;unsigned count=0,bda_nodes=0,v12_nodes=0;
  while(node) {
    if(count==64)return REPLAY_BDA_CHAIN;
    for(unsigned n=0;n<count;n++)if(seen[n]==node)return REPLAY_BDA_CHAIN;
    seen[count++]=node;
    if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT)
      return REPLAY_BDA_CONFLICTING_EXT;
    if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES) {
      if(++bda_nodes>1 || v12_nodes)return REPLAY_BDA_DUPLICATE_FEATURE;
      const VkPhysicalDeviceBufferDeviceAddressFeatures *b=(const void*)node;
      if(!b->bufferDeviceAddress)return REPLAY_BDA_EXISTING_DISABLED;
      state->existing_enabled=1;
    } else if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES) {
      if(++v12_nodes>1 || bda_nodes)return REPLAY_BDA_DUPLICATE_FEATURE;
      const VkPhysicalDeviceVulkan12Features *v=(const void*)node;
      if(!v->bufferDeviceAddress)return REPLAY_BDA_EXISTING_DISABLED;
      state->existing_enabled=1;
    }
    node=node->pNext;
  }
  if(!khr) {
    state->owned_names=calloc((size_t)in->enabledExtensionCount+1,sizeof *state->owned_names);
    if(!state->owned_names)return REPLAY_BDA_ALLOC;
    if(in->enabledExtensionCount)memcpy(state->owned_names,in->ppEnabledExtensionNames,
      in->enabledExtensionCount*sizeof *state->owned_names);
    state->owned_names[in->enabledExtensionCount]=VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME;
    state->create.ppEnabledExtensionNames=state->owned_names;state->create.enabledExtensionCount++;
    state->added_extension=1;
  }
  if(!state->existing_enabled) {
    state->bda=(VkPhysicalDeviceBufferDeviceAddressFeatures){
      .sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,
      .pNext=(void*)in->pNext,.bufferDeviceAddress=VK_TRUE};
    state->create.pNext=&state->bda;state->added_feature=1;
  }
  return state->added_extension||state->added_feature;
}
static void replay_rebind_bda_finish(replay_rebind_bda_state *state) {
  free(state->owned_names);state->owned_names=NULL;
}
