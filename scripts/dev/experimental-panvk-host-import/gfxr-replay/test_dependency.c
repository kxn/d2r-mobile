#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include "device_dependency.inc"
int main(void) {
  VkDeviceCreateInfo ci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.flags=0,.pNext=(void*)0x1234},out={0};const char **owned;
  assert(replay_device_dependency(&ci,&out,&owned)==0&&!owned);
  const char *names[]={VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME};ci.enabledExtensionCount=1;ci.ppEnabledExtensionNames=names;
  assert(replay_device_dependency(&ci,&out,&owned)==1&&owned);assert(out.enabledExtensionCount==2&&out.pNext==ci.pNext&&out.ppEnabledExtensionNames[0]==names[0]);assert(!strcmp(out.ppEnabledExtensionNames[1],VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME));assert(ci.enabledExtensionCount==1&&ci.ppEnabledExtensionNames==names);free(owned);
  const char *both[]={VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME,VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME};ci.enabledExtensionCount=2;ci.ppEnabledExtensionNames=both;assert(replay_device_dependency(&ci,&out,&owned)==0&&!owned);
  ci.enabledExtensionCount=4097;assert(replay_device_dependency(&ci,&out,&owned)==-1);ci.enabledExtensionCount=1;ci.ppEnabledExtensionNames=NULL;assert(replay_device_dependency(&ci,&out,&owned)==-1);
  puts("existing GFXR dependency correction: passthrough/pNext/original-list/required-extension/invalid-input checks passed; no GPU/ADB");return 0;
}
