// Standard Vulkan dispatch; Android may select a process-local driver at startup.
#pragma once
#ifdef __ANDROID__
#ifndef VK_USE_PLATFORM_ANDROID_KHR
#define VK_USE_PLATFORM_ANDROID_KHR 1
#endif
#endif
#include "../../third_party/volk/volk.h"
#ifdef __cplusplus
extern "C" {
#endif
void vk_loader_set_driver(const char *provider,const char *directory,const char *hooks);
void vk_loader_initialize(void);
int vk_loader_is_initialized(void);
void vk_loader_check_instance(VkInstance instance,int android_surface);
void vk_loader_check_device(VkPhysicalDevice physical,int driver_properties);
#ifdef __cplusplus
}
#endif
