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
void vk_loader_set_driver(const char *directory,const char *hooks);
void vk_loader_initialize(void);
#ifdef __cplusplus
}
#endif
