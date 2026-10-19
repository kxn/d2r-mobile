// SDKdefault + optional correctness shadow (SDK PCM) or explicitly selected
// nativecore PCM inside the original SDK packet/capacity/error/mutex wrapper.
#pragma once
#include <stdint.h>
typedef struct {
  uint64_t attached,live,initialized,compared,pcm_samples,pcm_differences;
  uint64_t sdk_errors,native_errors,bounds_rejects,stopped_budget,stack_rejects;
  uint32_t max_abs_difference;
  uint64_t native_selected,native_packets,native_samples,native_scalar_samples;
  uint64_t native_resets,native_core_errors,native_api_rejects,init_fallbacks;
  uint64_t tracked_sdk_packets; // attached objects only; SDKdefault is uninstrumented
  uint64_t capacity_rejects; // constructor attempts rejected before Init; SDK vptr retained
} dm_opus_shadow_stats;
// expected_table points to the original Itanium vtable prefix (two words).
// Every actual virtual slot is checked against original resolved SDK symbols.
int dm_opus_shadow_attach(void *self,const void *expected_table,void *const expected[10],
                          void (*report)(const char *));
void dm_opus_shadow_snapshot(dm_opus_shadow_stats *out);

enum { DM_OPUS_SHADOW=1,DM_OPUS_NATIVE=2 };
enum { DM_OPUS_CORE_INIT=0,DM_OPUS_CORE_CTL=1,DM_OPUS_CORE_DECODE=2 };
int dm_opus_attach_mode(void *self,const void *expected_table,void *const expected[10],
                       void (*report)(const char *),int mode);
// Used by the transparent core entry stubs after original symbols are resolved.
// The returned fixed native handler or original entry receives untouched args.
void *dm_opus_core_target(unsigned kind,const uintptr_t args[8],void *original);
