// Android counterpart of the original host's render-thread deadline declaration.
#pragma once
#include <stdint.h>
typedef struct {
  int reference,available,active,v2,last_error,tid;
  uint64_t sessions,reports,failures,target_ns,cpu_ns,wall_ns;
  int64_t preferred_ns;
} dm_android_hint_stats;
void dm_android_hint_reference(int reference);
void dm_android_hint_budget(uint64_t period_ns);
void dm_android_hint_work(uint64_t start_ns,uint64_t wall_ns,uint64_t cpu_ns);
void dm_android_hint_snapshot(dm_android_hint_stats *out);
