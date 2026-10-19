// Android 前端分辨率边界；沿用原 front_game_res 的两档参数位。
// Android frontend resolution boundary, retaining original front_game_res profiles.
#pragma once
#include <stdint.h>
int dm_android_display_configure(unsigned width, unsigned height, const char *override);
uint64_t front_game_res(int docked);
