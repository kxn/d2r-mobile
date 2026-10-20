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
int vk_loader_is_initialized(void);
void vk_loader_initialize(void);
void vk_loader_check_instance(VkInstance instance,int android_surface);
void vk_loader_check_device(VkPhysicalDevice physical,int driver_properties);
#ifdef __cplusplus
}
#endif

// Debug-only fixture result; never initializes a device or changes selection.
int vk_loader_debug_identity_verified(void);

// Cached provider decision, used only for official validation startup output.
int vk_loader_debug_layers_active(void);
