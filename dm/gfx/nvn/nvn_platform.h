// NVN 平台时钟与内存诊断；与游戏地址无关。
// NVN platform clock and memory diagnostics; independent of game addresses.
#pragma once
#include "../../host/platform.h"
uint64_t nvn_clock_ticks(void);
uint64_t nvn_clock_to_ns(uint64_t ticks);
void nvn_sleep_until(uint64_t ns);
double nvn_resident_mb(void);
#ifdef __APPLE__
#define NVN_RESIDENT_LABEL "footprint"
#else
#define NVN_RESIDENT_LABEL "RSS"
#endif
