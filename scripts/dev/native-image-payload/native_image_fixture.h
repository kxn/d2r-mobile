// Cold owned fixture only; no running game may share the Vulkan context.
#pragma once
#include "volk.h"
#ifdef __cplusplus
extern "C" {
#endif
int dm_native_image_fixture_run(const char *mode, const char *directory,
                                PFN_vkGetInstanceProcAddr proc, int validation);
#ifdef __cplusplus
}
#endif
