// 原宿主的平台边界：Apple 保持原实现，ELF 使用原生 TLS 和链接器注册段。
// Native host platform boundary: preserve Apple behavior; use native TLS and
// linker sections on ELF.
#pragma once
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <sys/stat.h>
#ifdef __APPLE__
#define DM_HLE_SECTION(name) __attribute__((used, section("__DATA," name)))
#define dm_tsd_set pthread_setspecific
#define dm_tsd_get pthread_getspecific
#else
#define TARGET_OS_IPHONE 0
#define TARGET_OS_OSX 0
#define DM_HLE_SECTION(name)                                                   \
  __attribute__((used, retain, section(name), aligned(8)))
int dm_tsd_set(pthread_key_t key, const void *value);
void *dm_tsd_get(pthread_key_t key);
int64_t dm_tsd_offset(pthread_key_t key);
#ifdef DM_TLS_DESC
uintptr_t dm_tsd_descriptor(void);
#endif
void dm_tsd_check(void);
long dm_self_read(void *out, const void *address, size_t size);
extern const unsigned char __start_swhle[], __stop_swhle[];
extern const unsigned char __start_swforce[], __stop_swforce[];
#endif

static inline uint64_t dm_platform_clock_ns(clockid_t clock) {
#ifdef __APPLE__
  return clock_gettime_nsec_np(clock);
#else
  struct timespec ts;
  clock_gettime(clock, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
#endif
}
static inline uint64_t dm_platform_uptime_ns(void) {
#ifdef __APPLE__
  return dm_platform_clock_ns(CLOCK_UPTIME_RAW);
#else
  return dm_platform_clock_ns(CLOCK_MONOTONIC_RAW);
#endif
}
static inline struct timespec dm_platform_stat_mtime(const struct stat *st) {
#ifdef __APPLE__
  return st->st_mtimespec;
#else
  return st->st_mtim;
#endif
}
