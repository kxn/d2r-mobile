// 独立检查屏幕参数与原两档分辨率参数位；不装载游戏。
// Independently check screen inputs and original resolution profiles; no game loading.
#include "../display.h"
#include <assert.h>
#include <stdio.h>
static void expected(unsigned dw, unsigned dh, unsigned hw) {
  assert(front_game_res(1) == ((uint64_t)dh << 32 | dw));
  assert(front_game_res(0) == (UINT64_C(720) << 32 | hw));
}
int main(void) {
  expected(1920,1080,1280);
  assert(dm_android_display_configure(2772,1240,NULL)); expected(2772,1240,1608);
  assert(dm_android_display_configure(1920,1080,NULL)); expected(1920,1080,1280);
  assert(dm_android_display_configure(2048,1536,NULL)); expected(2048,1152,1280);
  assert(dm_android_display_configure(2772,1240,"1280x720")); expected(1280,720,1280);
  assert(dm_android_display_configure(2772,1240,"640x480")); expected(640,480,960);
  assert(dm_android_display_configure(2772,1240,"1600x900")); expected(1600,900,1280);
  const char *invalid[] = {"0x720","1280x0","4097x720","1280x4097","1280x720junk",
    "-1280x720","1280x-720"," 1280x720","1280x 720","1280","1280x","x720",
    "184467440737095516161280x720","1280x18446744073709551616720"};
  for (unsigned i=0;i<sizeof invalid/sizeof *invalid;i++) {
    assert(!dm_android_display_configure(2772,1240,invalid[i])); expected(1600,900,1280);
  }
  assert(!dm_android_display_configure(0,1240,NULL)); expected(1600,900,1280);
  assert(!dm_android_display_configure(2772,0,NULL)); expected(1600,900,1280);
  assert(!dm_android_display_configure(4096,16,NULL)); expected(1600,900,1280);
  puts("{\"frontend\":\"android\",\"resolution_policy\":\"passed\",\"game_executed\":false}");
}
