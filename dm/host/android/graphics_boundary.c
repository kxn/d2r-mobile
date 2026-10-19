// 无图形引导在原 NVN 接管点明确停止，不让游戏进入 Switch 显卡驱动。
// Headless bring-up stops explicitly at the original NVN takeover point, before
// Switch GPU driver execution.
#include "hle.h"
#include <unistd.h>
HLE_FORCE(
    "nvnBootstrapLoader",
    "无图形验收边界：下一阶段接原 NVN 接口的 Vulkan 后端 | Headless acceptance "
    "boundary: next connect Vulkan behind original NVN interfaces",
    void *, headless_nvn, const char *name) {
  LOG("native graphics boundary: %s requires the Vulkan backend", name);
  puts("{\"backend\":\"native-arm64\",\"stage\":\"graphics-boundary\","
       "\"graphics_available\":false}");
  fflush(NULL);
  _exit(78);
}
