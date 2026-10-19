// 对应 front_ios.m 的原生像素主机档、720 高掌机档和 DM_GAME_RES。
// Match front_ios.m: native-pixel docked profile, 720-high handheld profile, DM_GAME_RES.
#include "display.h"
#include <stdatomic.h>
#include <errno.h>
#include <stdlib.h>

static _Atomic uint64_t profiles[2] = {
    (UINT64_C(720) << 32) | 1280, (UINT64_C(1080) << 32) | 1920};

int dm_android_display_configure(unsigned width, unsigned height, const char *override) {
  int overridden = override && *override;
  if (overridden) {
    char *end;
    if (*override < '0' || *override > '9') return 0;
    errno = 0;
    unsigned long w = strtoul(override, &end, 10);
    if (errno || w > 4096 || *end != 'x') return 0;
    end++;
    if (*end < '0' || *end > '9') return 0;
    unsigned long h = strtoul(end, &end, 10);
    if (errno || h > 4096 || *end) return 0;
    width = (unsigned)w; height = (unsigned)h;
  }
  // 与原参数位的 4096 宽限制一致；拒绝零、溢出或异常尺寸。
  // Respect the original 4096-wide parameter limit and reject invalid dimensions.
  if (width < 16 || height < 16 || width > 4096 || height > 4096) return 0;
  // 窄屏先沿用原 16:9 留边路径；宽屏由原 PARAMS/UI 宽高比槽接管。
  // Narrow screens use original 16:9 letterboxing; original PARAMS/UI slots handle wide screens.
  if (!overridden && (uint64_t)width * 9 < (uint64_t)height * 16)
    height = (unsigned)(((uint64_t)width * 9 + 16) / 32) * 2;
  if (height < 16) return 0;
  unsigned handheld_width = (unsigned)(((uint64_t)width * 720 + (uint64_t)height * 4) /
                                        ((uint64_t)height * 8)) * 8;
  if (handheld_width > 4096) return 0;
  atomic_store(&profiles[0], (UINT64_C(720) << 32) | handheld_width);
  atomic_store(&profiles[1], ((uint64_t)height << 32) | width);
  return 1;
}

uint64_t front_game_res(int docked) { return atomic_load(&profiles[docked != 0]); }
