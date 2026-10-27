/* Experimental Turnip Android WSI allocation policy. SPDX-License-Identifier: MIT */
#include "tu_device.h"
#include "tu_image.h"
#include "vk_common_entrypoints.h"
#include "util/u_debug.h"

/* Qualcomm's documented vendor gralloc usage bit. The allocator, mapper and
 * compositor still own/describe the actual layout; never invent a modifier. */
static constexpr uint64_t qcom_alloc_ubwc = UINT64_C(1) << 28;
DEBUG_GET_ONCE_BOOL_OPTION(tu_ubwc_swapchain, "TU_UBWC_SWAPCHAIN", false)

static bool
request_swapchain_ubwc(VkDevice handle, VkFormat format,
                       VkImageUsageFlags usage)
{
   if (!debug_get_option_tu_ubwc_swapchain() ||
       !(usage & (VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)))
      return false;
   switch (format) {
   case VK_FORMAT_R8G8B8A8_UNORM:
   case VK_FORMAT_R8G8B8A8_SRGB:
   case VK_FORMAT_B8G8R8A8_UNORM:
   case VK_FORMAT_B8G8R8A8_SRGB:
      break;
   default:
      return false;
   }
   VK_FROM_HANDLE(tu_device, device, handle);
   return ubwc_possible(device, format, VK_IMAGE_TYPE_2D, 0, usage, 0,
                        &device->physical_device->dev_info,
                        VK_SAMPLE_COUNT_1_BIT, 1, device->use_z24uint_s8uint);
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_GetSwapchainGrallocUsageANDROID(VkDevice device, VkFormat format,
                                   VkImageUsageFlags usage, int *gralloc_usage)
{
   VkResult result = vk_common_GetSwapchainGrallocUsageANDROID(
      device, format, usage, gralloc_usage);
   if (result == VK_SUCCESS && request_swapchain_ubwc(device, format, usage)) {
      *gralloc_usage |= qcom_alloc_ubwc;
      fprintf(stderr, "[Turnip WSI] UBWC request gralloc0 format=%u usage=%#x\n",
              format, usage);
   }
   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_GetSwapchainGrallocUsage2ANDROID(
   VkDevice device, VkFormat format, VkImageUsageFlags usage,
   VkSwapchainImageUsageFlagsANDROID swapchain_usage,
   uint64_t *consumer_usage, uint64_t *producer_usage)
{
   VkResult result = vk_common_GetSwapchainGrallocUsage2ANDROID(
      device, format, usage, swapchain_usage, consumer_usage, producer_usage);
   /* Shared/front-buffer allocation has a distinct compression contract. */
   if (result == VK_SUCCESS && !swapchain_usage &&
       request_swapchain_ubwc(device, format, usage)) {
      *producer_usage |= qcom_alloc_ubwc;
      fprintf(stderr, "[Turnip WSI] UBWC request gralloc1 format=%u usage=%#x\n",
              format, usage);
   }
   return result;
}
