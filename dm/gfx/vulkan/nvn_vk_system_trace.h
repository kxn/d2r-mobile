// Android system-trace annotations, dormant unless a profiler enables app
// tracing. These are CPU/queue/fence spans, never GPU execution timestamps.
#pragma once
#include <stdint.h>
#ifdef __ANDROID__
#include <android/trace.h>
static inline int vk_system_trace_begin(const char *name) {
  if(!ATrace_isEnabled()) return 0;
  ATrace_beginSection(name);return 1;
}
static inline void vk_system_trace_end(int active) { if(active) ATrace_endSection(); }
static inline void vk_system_trace_counter(const char *name,int64_t value) {
  if(ATrace_isEnabled()) ATrace_setCounter(name,value);
}
static inline int vk_system_trace_async_begin(const char *name,uint64_t serial) {
  if(!ATrace_isEnabled()) return 0;
  ATrace_beginAsyncSection(name,(int32_t)serial);return 1;
}
static inline void vk_system_trace_async_end(const char *name,uint64_t serial,int active) {
  if(active) ATrace_endAsyncSection(name,(int32_t)serial);
}
#else
static inline int vk_system_trace_begin(const char *name) { (void)name;return 0; }
static inline void vk_system_trace_end(int active) { (void)active; }
static inline void vk_system_trace_counter(const char *name,int64_t value) {
  (void)name;(void)value;
}
static inline int vk_system_trace_async_begin(const char *name,uint64_t serial) {
  (void)name;(void)serial;return 0;
}
static inline void vk_system_trace_async_end(const char *name,uint64_t serial,int active) {
  (void)name;(void)serial;(void)active;
}
#endif
