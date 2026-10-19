// NVN 格式到 Vulkan 格式；无法精确表达的格式返回 UNDEFINED。
// NVN-to-Vulkan formats; formats without an exact representation return
// UNDEFINED.
#include "nvn_vk_internal.h"
// Physical-device format properties are immutable. Populate the small core
// format table while init_device is still owned by its pthread_once caller;
// normal recording then needs neither a driver query nor a cache mutex.
// Extension formats retain the original driver query until explicitly cached.
static VkFormatProperties format_properties[VK_FORMAT_ASTC_12x12_SRGB_BLOCK+1];
static VkPhysicalDevice format_properties_device;
void vk_format_properties_init(void) {
  for(unsigned f=0;f<sizeof format_properties/sizeof *format_properties;f++)
    vkGetPhysicalDeviceFormatProperties(V.physical,(VkFormat)f,format_properties+f);
  format_properties_device=V.physical;
}
void vk_format_properties(VkFormat format,VkFormatProperties *properties) {
  if(format_properties_device && format_properties_device==V.physical &&
      (unsigned)format<sizeof format_properties/sizeof *format_properties)
    *properties=format_properties[(unsigned)format];
  else vkGetPhysicalDeviceFormatProperties(V.physical,format,properties);
}
VkFormat vk_nvn_format(int f) {
  static const VkFormat table[] = {VK_FORMAT_UNDEFINED,
                                   VK_FORMAT_R8_UNORM,
                                   VK_FORMAT_R8_SNORM,
                                   VK_FORMAT_R8_UINT,
                                   VK_FORMAT_R8_SINT,
                                   VK_FORMAT_R16_SFLOAT,
                                   VK_FORMAT_R16_UNORM,
                                   VK_FORMAT_R16_SNORM,
                                   VK_FORMAT_R16_UINT,
                                   VK_FORMAT_R16_SINT,
                                   VK_FORMAT_R32_SFLOAT,
                                   VK_FORMAT_R32_UINT,
                                   VK_FORMAT_R32_SINT,
                                   VK_FORMAT_R8G8_UNORM,
                                   VK_FORMAT_R8G8_SNORM,
                                   VK_FORMAT_R8G8_UINT,
                                   VK_FORMAT_R8G8_SINT,
                                   VK_FORMAT_R16G16_SFLOAT,
                                   VK_FORMAT_R16G16_UNORM,
                                   VK_FORMAT_R16G16_SNORM,
                                   VK_FORMAT_R16G16_UINT,
                                   VK_FORMAT_R16G16_SINT,
                                   VK_FORMAT_R32G32_SFLOAT,
                                   VK_FORMAT_R32G32_UINT,
                                   VK_FORMAT_R32G32_SINT,
                                   VK_FORMAT_R8G8B8_UNORM,
                                   VK_FORMAT_R8G8B8_SNORM,
                                   VK_FORMAT_R8G8B8_UINT,
                                   VK_FORMAT_R8G8B8_SINT,
                                   VK_FORMAT_R16G16B16_SFLOAT,
                                   VK_FORMAT_R16G16B16_UNORM,
                                   VK_FORMAT_R16G16B16_SNORM,
                                   VK_FORMAT_R16G16B16_UINT,
                                   VK_FORMAT_R16G16B16_SINT,
                                   VK_FORMAT_R32G32B32_SFLOAT,
                                   VK_FORMAT_R32G32B32_UINT,
                                   VK_FORMAT_R32G32B32_SINT,
                                   VK_FORMAT_R8G8B8A8_UNORM,
                                   VK_FORMAT_R8G8B8A8_SNORM,
                                   VK_FORMAT_R8G8B8A8_UINT,
                                   VK_FORMAT_R8G8B8A8_SINT,
                                   VK_FORMAT_R16G16B16A16_SFLOAT,
                                   VK_FORMAT_R16G16B16A16_UNORM,
                                   VK_FORMAT_R16G16B16A16_SNORM,
                                   VK_FORMAT_R16G16B16A16_UINT,
                                   VK_FORMAT_R16G16B16A16_SINT,
                                   VK_FORMAT_R32G32B32A32_SFLOAT,
                                   VK_FORMAT_R32G32B32A32_UINT,
                                   VK_FORMAT_R32G32B32A32_SINT,
                                   VK_FORMAT_S8_UINT,
                                   VK_FORMAT_D16_UNORM,
                                   VK_FORMAT_X8_D24_UNORM_PACK32,
                                   VK_FORMAT_D32_SFLOAT,
                                   VK_FORMAT_D24_UNORM_S8_UINT,
                                   VK_FORMAT_D32_SFLOAT_S8_UINT,
                                   VK_FORMAT_R8G8B8A8_SRGB,
                                   VK_FORMAT_R8G8B8A8_SRGB,
                                   VK_FORMAT_R4G4B4A4_UNORM_PACK16,
                                   VK_FORMAT_UNDEFINED,
                                   VK_FORMAT_R5G5B5A1_UNORM_PACK16,
                                   VK_FORMAT_R5G6B5_UNORM_PACK16,
                                   VK_FORMAT_A2B10G10R10_UNORM_PACK32,
                                   VK_FORMAT_A2B10G10R10_UINT_PACK32,
                                   VK_FORMAT_B10G11R11_UFLOAT_PACK32,
                                   VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,
                                   VK_FORMAT_BC1_RGB_UNORM_BLOCK,
                                   VK_FORMAT_BC1_RGBA_UNORM_BLOCK,
                                   VK_FORMAT_BC2_UNORM_BLOCK,
                                   VK_FORMAT_BC3_UNORM_BLOCK,
                                   VK_FORMAT_BC1_RGB_SRGB_BLOCK,
                                   VK_FORMAT_BC1_RGBA_SRGB_BLOCK,
                                   VK_FORMAT_BC2_SRGB_BLOCK,
                                   VK_FORMAT_BC3_SRGB_BLOCK,
                                   VK_FORMAT_BC4_UNORM_BLOCK,
                                   VK_FORMAT_BC4_SNORM_BLOCK,
                                   VK_FORMAT_BC5_UNORM_BLOCK,
                                   VK_FORMAT_BC5_SNORM_BLOCK,
                                   VK_FORMAT_BC7_UNORM_BLOCK,
                                   VK_FORMAT_BC7_SRGB_BLOCK,
                                   VK_FORMAT_BC6H_SFLOAT_BLOCK,
                                   VK_FORMAT_BC6H_UFLOAT_BLOCK,
                                   VK_FORMAT_R8_USCALED,
                                   VK_FORMAT_R8_SSCALED,
                                   VK_FORMAT_R16_USCALED,
                                   VK_FORMAT_R16_SSCALED,
                                   VK_FORMAT_R32_UINT,
                                   VK_FORMAT_R32_SINT,
                                   VK_FORMAT_R8G8_USCALED,
                                   VK_FORMAT_R8G8_SSCALED,
                                   VK_FORMAT_R16G16_USCALED,
                                   VK_FORMAT_R16G16_SSCALED,
                                   VK_FORMAT_R32G32_UINT,
                                   VK_FORMAT_R32G32_SINT,
                                   VK_FORMAT_R8G8B8_USCALED,
                                   VK_FORMAT_R8G8B8_SSCALED,
                                   VK_FORMAT_R16G16B16_USCALED,
                                   VK_FORMAT_R16G16B16_SSCALED,
                                   VK_FORMAT_R32G32B32_UINT,
                                   VK_FORMAT_R32G32B32_SINT,
                                   VK_FORMAT_R8G8B8A8_USCALED,
                                   VK_FORMAT_R8G8B8A8_SSCALED,
                                   VK_FORMAT_R16G16B16A16_USCALED,
                                   VK_FORMAT_R16G16B16A16_SSCALED,
                                   VK_FORMAT_R32G32B32A32_UINT,
                                   VK_FORMAT_R32G32B32A32_SINT,
                                   VK_FORMAT_A2B10G10R10_SNORM_PACK32,
                                   VK_FORMAT_A2B10G10R10_SINT_PACK32,
                                   VK_FORMAT_A2B10G10R10_USCALED_PACK32,
                                   VK_FORMAT_A2B10G10R10_SSCALED_PACK32,
                                   VK_FORMAT_R8G8B8A8_UNORM,
                                   VK_FORMAT_R8G8B8A8_SNORM,
                                   VK_FORMAT_R8G8B8A8_UINT,
                                   VK_FORMAT_R8G8B8A8_SINT,
                                   VK_FORMAT_R16G16B16A16_SFLOAT,
                                   VK_FORMAT_R16G16B16A16_UNORM,
                                   VK_FORMAT_R16G16B16A16_SNORM,
                                   VK_FORMAT_R16G16B16A16_UINT,
                                   VK_FORMAT_R16G16B16A16_SINT,
                                   VK_FORMAT_R32G32B32A32_SFLOAT,
                                   VK_FORMAT_R32G32B32A32_UINT,
                                   VK_FORMAT_R32G32B32A32_SINT};
  if (f >= 0x79 && f <= 0x94)
    return VK_FORMAT_ASTC_4x4_UNORM_BLOCK + (f - 0x79) % 14 * 2 + (f >= 0x87);
  if (f == 0x95)
    return VK_FORMAT_B5G6R5_UNORM_PACK16;
  if (f == 0x97)
    return VK_FORMAT_B5G5R5A1_UNORM_PACK16;
  if (f == 0x98)
    return VK_FORMAT_A1R5G5B5_UNORM_PACK16;
  if (f == 0x99 || f == 0x9a)
    return VK_FORMAT_B8G8R8A8_UNORM;
  if (f == 0x9b || f == 0x9c)
    return VK_FORMAT_B8G8R8A8_SRGB;
  return (unsigned)f < sizeof table / sizeof *table ? table[f]
                                                    : VK_FORMAT_UNDEFINED;
}
