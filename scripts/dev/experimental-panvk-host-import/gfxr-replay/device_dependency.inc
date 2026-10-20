/* Correct the existing GFXR depth-resolve dependency, not captured game state. */
static int replay_device_dependency(const VkDeviceCreateInfo *in,VkDeviceCreateInfo *out,const char ***owned) {
  int depth=0,renderpass=0;*owned=NULL;
  if(in->enabledExtensionCount>4096 || (in->enabledExtensionCount&&!in->ppEnabledExtensionNames))return -1;
  for(uint32_t n=0;n<in->enabledExtensionCount;n++) {
    const char *name=in->ppEnabledExtensionNames[n];if(!name)return -1;
    depth|=!strcmp(name,VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME);
    renderpass|=!strcmp(name,VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME);
  }
  if(!depth||renderpass)return 0;
  const char **names=calloc((size_t)in->enabledExtensionCount+1,sizeof *names);if(!names)return -1;
  memcpy(names,in->ppEnabledExtensionNames,in->enabledExtensionCount*sizeof *names);
  names[in->enabledExtensionCount]=VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME;
  *out=*in;out->enabledExtensionCount++;out->ppEnabledExtensionNames=names;*owned=names;return 1;
}
