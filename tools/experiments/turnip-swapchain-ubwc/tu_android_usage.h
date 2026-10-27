/* SPDX-License-Identifier: MIT */
#pragma once
#include "vulkan/vulkan_core.h"
void tu_android_apply_ubwc_usage(VkPhysicalDevice physical,
                                  const VkPhysicalDeviceImageFormatInfo2 *info,
                                  VkImageFormatProperties2 *properties);
