// Optional official-tool integration; never measures GPU time itself.
#pragma once
#include "nvn_vk_loader.h"
int vk_renderdoc_request(const char *directory);
void vk_renderdoc_present(VkInstance instance, int discarded);
