#pragma once
#include "../../ios/Sources/dm_touch_controls.h"
// Android 线程上的原 front_ios.m 界面桥；游戏操作仍由原队列交游戏线程执行。
// Android-thread adaptation of front_ios.m. Original queues keep all game
// operations on the guest thread.
int dm_android_ui_finger(dm_tc_state *s, int frontend, int phase, int slot,
                         float x, float y, double now);
void dm_android_ui_tick(dm_tc_state *s, double now);
void dm_android_ui_cancel(void);
int dm_android_ui_ghost_rect(float rect[4]);
