// 链接生产帧率策略，宿主设置与刷新率作为可控边界，不装载游戏。
// Link production pacing policy with controlled host settings and refresh;
// no game modules execute in this test.
#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
static int setting=60;
static double refresh;
int dm_fps_setting(void) { return setting; }
double vk_window_refresh(void) { return refresh; }
int mtl_fps_target(void);
void vk_set_thermal_limit(int limit);
double mtl_pace_hz(void);
int main(int argc, char **argv) {
  assert(argc==2); int forced=atoi(argv[1]); unsigned checks=0;
  const double displays[]={0,59.94,60,90,120,144,165,240};
  const double rates[2][8]={{60,59.94,60,90,60,72,82.5,60},
                            {30,29.97,30,30,30,36,33,30}};
  for (unsigned mode=0;mode<2;mode++) {
    setting=mode?30:60; int target=forced?forced:setting;
    assert(mtl_fps_target()==target); checks++;
    for (unsigned i=0;i<8;i++) {
      refresh=displays[i];
      assert(fabs(mtl_pace_hz()-rates[target==30][i])<1e-6); checks++;
    }
  }
  setting=25;
  assert(mtl_fps_target()==(forced?forced:60)); checks++;
  // 每进程锁存环境覆盖；实时省电设置仍可来回切换。
  // Environment override is process-latched; live eco settings can toggle.
  setenv("DM_FPS",forced==30?"60":"30",1);
  setting=60; assert(mtl_fps_target()==(forced?forced:60)); checks++;
  setting=30; assert(mtl_fps_target()==(forced?forced:30)); checks++;
  // Thermal pressure caps rendering even when the diagnostic asks for60.
  // Releasing pressure preserves the original user setting/latched override.
  setting=60; vk_set_thermal_limit(30);
  assert(mtl_fps_target()==30); checks++;
  refresh=120; assert(fabs(mtl_pace_hz()-30)<1e-6); checks++;
  vk_set_thermal_limit(20);
  assert(mtl_fps_target()==20); checks++;
  assert(fabs(mtl_pace_hz()-20)<1e-6); checks++;
  vk_set_thermal_limit(0);
  assert(mtl_fps_target()==(forced?forced:60)); checks++;
  setting=30; assert(mtl_fps_target()==(forced?forced:30)); checks++;
  printf("{\"pacing_checks\":%u,\"forced_fps\":%d,\"result\":\"passed\"}\n",checks,forced);
}
