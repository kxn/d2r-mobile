// 对应原 nvn_mtl.m 帧率策略；Android 在原 NVN 取图节拍器等待。
// Original nvn_mtl.m frame-rate policy; Android waits in the NVN acquire pacer.
#include "../../host/dm.h"
#include "../nvn/nvn_pace.h"
#include "nvn_vk_window.h"
#include <stdatomic.h>
__attribute__((weak)) int dm_fps_setting(void) { return 60; }
static _Atomic int thermal_limit;
void vk_set_thermal_limit(int limit) {
  int next=limit==20 || limit==30?limit:0;
  if(atomic_exchange(&thermal_limit,next)!=next)
    LOG("[vulkan] thermal render limit %d FPS (0=original setting)",next);
}
int mtl_fps_target(void) {
  static _Atomic int override=-2;
  int fps=atomic_load(&override);
  if (fps==-2) {
    const char *value=getenv("DM_FPS"); int requested=value?atoi(value):-1;
    if (requested!=30 && requested!=60) requested=-1;
    int unset=-2;
    if (atomic_compare_exchange_strong(&override,&unset,requested)) {
      fps=requested;
      if (fps>0) LOG("DM_FPS=%d: original frame-rate test override",fps);
    } else fps=unset;
  }
  int target=fps>0?fps:dm_fps_setting()==30?30:60;
  int limit=atomic_load_explicit(&thermal_limit,memory_order_relaxed);
  return limit && target>limit?limit:target;
}
double mtl_pace_hz(void) {
  return nvn_pace_hz(vk_window_refresh(),mtl_fps_target());
}
