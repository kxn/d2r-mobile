// 保留 Apple 时基；ELF 使用单调纳秒，不伪造 Mach 服务。
// Preserve Apple's timebase; ELF uses monotonic nanoseconds without Mach stubs.
#include "nvn_platform.h"
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_time.h>
#endif
uint64_t nvn_clock_ticks(void) {
#ifdef __APPLE__
  return mach_absolute_time();
#else
  return dm_platform_clock_ns(CLOCK_MONOTONIC);
#endif
}
uint64_t nvn_clock_to_ns(uint64_t ticks) {
#ifdef __APPLE__
  mach_timebase_info_data_t tb;
  mach_timebase_info(&tb);
  return ticks * tb.numer / tb.denom;
#else
  return ticks;
#endif
}
void nvn_sleep_until(uint64_t ns) {
#ifdef __APPLE__
  mach_timebase_info_data_t tb;
  mach_timebase_info(&tb);
  mach_wait_until(ns * tb.denom / tb.numer);
#else
  struct timespec t = {.tv_sec = ns / 1000000000ull,
                       .tv_nsec = ns % 1000000000ull};
  while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, NULL) == EINTR) {
  }
#endif
}
double nvn_resident_mb(void) {
#ifdef __APPLE__
  task_vm_info_data_t vi;
  mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vi, &count) !=
      KERN_SUCCESS)
    return -1;
  return vi.phys_footprint / 1048576.0;
#else
  unsigned long total, resident;
  FILE *f = fopen("/proc/self/statm", "r");
  if (!f)
    return -1;
  int n = fscanf(f, "%lu %lu", &total, &resident);
  fclose(f);
  return n == 2 ? (double)resident * sysconf(_SC_PAGESIZE) / 1048576.0 : -1;
#endif
}
