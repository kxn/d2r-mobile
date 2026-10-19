#include "opus_shadow.h"
#include <opus.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <stdatomic.h>
#if defined(__ANDROID__) && !defined(DM_OPUS_SHADOW_OWNED_HOST)
#include "host_stack.h"
#endif

enum { MAX_CHANNELS=2,MAX_SAMPLES=5760,CONTEXT_BUDGET=256,PROCESS_BUDGET=1024,
       MAX_SHADOW_CONTEXTS=32,MAX_NATIVE_CONTEXTS=64 };
typedef struct shadow {
  struct shadow *next;
  pthread_mutex_t serial;
  unsigned refs;int detached;
  void *self;void **original;
  uintptr_t table[12]; // original offset-to-top + RTTI, ten unchanged ABI slots
  OpusDecoder *decoder;int16_t *pcm;
  int rate,channels,active,reset_pending,mode;
  void *sdk_state;int selected_native; // write under serial+map; outside reads under map
  _Atomic int core_faulted;
  uint64_t native_packets,native_samples;
  unsigned compared,differences,max_difference,difference_packets;
  void (*report)(const char *);
} shadow;
typedef struct core_scope {
  struct core_scope *previous;shadow *s;unsigned phase;
  int rate,channels,captures,capture_invalid;
  void *captured;
  int called,result;
} core_scope;
static _Thread_local core_scope *current_core;
static shadow *contexts;
// The map lock never covers guest SDK/native codec execution or a wait on
// serial. Different decoders remain independent; only one decoder is ordered.
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static dm_opus_shadow_stats stats;
static unsigned live_contexts[3]; // existing map lock; native/shadow budgets are independent
static unsigned messages,attempts;
static void lock(void){pthread_mutex_lock(&mutex);}
static void unlock(void){pthread_mutex_unlock(&mutex);}
static shadow *borrow(void *self) {
  lock();shadow *s=contexts;
  while(s && s->self!=self)s=s->next;
  if(s)s->refs++;
  unlock();
  if(s)pthread_mutex_lock(&s->serial);
  return s;
}
static void release(shadow *s) {
  pthread_mutex_unlock(&s->serial);
  lock();s->refs--;unsigned dispose=s->detached && s->refs==0;unlock();
  if(dispose){pthread_mutex_destroy(&s->serial);free(s);}
}
#define ADD_STAT(field,value) do{lock();stats.field+=(value);unlock();}while(0)
static void report(shadow *s,const char *event,int result) {
  if(!s->report)return;
  lock();int allowed=messages<64;if(allowed)messages++;unlock();
  if(!allowed)return;
  char line[768];
  snprintf(line,sizeof line,"[opus shadow] %s object=%p result=%d rate=%d channels=%d compared=%u successful_packets=%u bit_exact_packets=%u non_bit_exact_packets=%u differences=%u max_abs=%u active=%d mode=%d native_packets=%llu native_samples=%llu; formal=%s native=%s",
      event,s->self,result,s->rate,s->channels,s->compared,s->compared,s->compared-s->difference_packets,s->difference_packets,s->differences,s->max_difference,s->active,s->mode,(unsigned long long)s->native_packets,(unsigned long long)s->native_samples,s->selected_native?"native-core/SDK-wrapper":"SDK",opus_get_version_string());
  s->report(line);
}
static void clear_native(shadow *s) {
  if(s->decoder)opus_decoder_destroy(s->decoder);
  free(s->pcm);s->decoder=NULL;s->pcm=NULL;s->active=s->reset_pending=0;
}
// Only these callbacks execute on the native stack. They never enter the
// original SDK, guest callbacks, ExitThread or a nonlocal exception boundary.
static int native_call(void (*fn)(void *),void *argument) {
#if defined(__ANDROID__) && !defined(DM_OPUS_SHADOW_OWNED_HOST)
  return dm_thread_host_call(fn,argument);
#else
  fn(argument);return 1; // standalone host correctness fixture, no guest stack
#endif
}
static void native_initialize(void *argument) {
  shadow *s=argument;clear_native(s);int error=OPUS_OK;
  s->decoder=opus_decoder_create(s->rate,s->channels,&error);
  if(s->decoder && !error && s->mode==DM_OPUS_SHADOW)s->pcm=calloc(MAX_SAMPLES*MAX_CHANNELS+16,sizeof *s->pcm);
  if(!s->decoder || error || (s->mode==DM_OPUS_SHADOW && !s->pcm)){clear_native(s);return;}
  s->active=1;
}
typedef struct {shadow *s;const void *input;int bytes,count,decoded;unsigned guards;} decode_job;
static void native_decode(void *argument) {
  decode_job *job=argument;shadow *s=job->s;
  if(s->reset_pending) {
    job->decoded=opus_decoder_ctl(s->decoder,OPUS_RESET_STATE);
    s->reset_pending=0;if(job->decoded)return;
  }
  for(unsigned n=0;n<8;n++)s->pcm[n]=s->pcm[MAX_SAMPLES*MAX_CHANNELS+8+n]=(int16_t)0x5a3c;
  job->decoded=opus_decode(s->decoder,job->input,job->bytes,s->pcm+8,job->count,0);
  for(unsigned n=0;n<8;n++)job->guards|=s->pcm[n]!=(int16_t)0x5a3c ||
      s->pcm[MAX_SAMPLES*MAX_CHANNELS+8+n]!=(int16_t)0x5a3c;
}
static int disjoint(const void *a,uint64_t bytes,const void *b,uint64_t other) {
  uintptr_t first=(uintptr_t)a,second=(uintptr_t)b;
  if(bytes>UINTPTR_MAX-first || other>UINTPTR_MAX-second)return 0;
  return first+bytes<=second || second+other<=first;
}
static int publish_binding(shadow *s,void *state,int selected) {
  lock();
  if(selected)for(shadow *at=contexts;at;at=at->next)
    if(at!=s && at->selected_native && at->sdk_state==state){unlock();return 0;}
  s->sdk_state=state;s->selected_native=selected;unlock();return 1;
}
static int core_reject(void){return OPUS_UNIMPLEMENTED;}
static void reject_core(shadow *s,unsigned kind,uintptr_t request) {
  atomic_store_explicit(&s->core_faulted,1,memory_order_release);
  ADD_STAT(native_api_rejects,1);
  report(s,"native core unsupported scope/state/API; fail-closed until Reset/Init",(int)(kind==DM_OPUS_CORE_CTL?request:kind));
}
typedef struct {shadow *s;const unsigned char *input;int bytes;int16_t *pcm;int capacity;int result;} native_core_job;
static void run_native_core(void *argument) {
  native_core_job *job=argument;
  job->result=opus_decode(job->s->decoder,job->input,job->bytes,job->pcm,job->capacity,0);
}
static void run_native_reset(void *argument) {
  native_core_job *job=argument;
  job->result=opus_decoder_ctl(job->s->decoder,OPUS_RESET_STATE);
}
static int native_core_decode(void *state,const unsigned char *input,int bytes,int16_t *pcm,int capacity,int fec) {
  core_scope *scope=current_core;
  if(!scope || scope->phase!=DM_OPUS_CORE_DECODE || !scope->s->selected_native || state!=scope->s->sdk_state || fec!=0)return OPUS_UNIMPLEMENTED;
  shadow *s=scope->s;
  if(scope->called++){reject_core(s,DM_OPUS_CORE_DECODE,0);return scope->result=OPUS_UNIMPLEMENTED;}
  if(atomic_load_explicit(&s->core_faulted,memory_order_acquire))return scope->result=OPUS_UNIMPLEMENTED;
  native_core_job job={s,input,bytes,pcm,capacity,OPUS_INTERNAL_ERROR};
  if(!native_call(run_native_core,&job)) {
    ADD_STAT(stack_rejects,1);atomic_store_explicit(&s->core_faulted,1,memory_order_release);
    report(s,"native core has no safe host stack; fail-closed",0);
  }
  if(job.result<0)ADD_STAT(native_core_errors,1);
  scope->result=job.result;return job.result;
}
static int native_core_reset(void *state,int request) {
  core_scope *scope=current_core;
  if(!scope || scope->phase!=DM_OPUS_CORE_DECODE || state!=scope->s->sdk_state || request!=OPUS_RESET_STATE)return OPUS_UNIMPLEMENTED;
  shadow *s=scope->s;native_core_job job={.s=s,.result=OPUS_INTERNAL_ERROR};
  if(!native_call(run_native_reset,&job)) {
    ADD_STAT(stack_rejects,1);atomic_store_explicit(&s->core_faulted,1,memory_order_release);
  } else if(!job.result) {
    atomic_store_explicit(&s->core_faulted,0,memory_order_release);ADD_STAT(native_resets,1);
  }
  if(job.result<0)ADD_STAT(native_core_errors,1);
  return job.result;
}
void *dm_opus_core_target(unsigned kind,const uintptr_t args[8],void *original) {
  core_scope *scope=current_core;
  if(scope && scope->s->mode==DM_OPUS_NATIVE) {
    shadow *s=scope->s;
    if(scope->phase==DM_OPUS_CORE_INIT && kind==DM_OPUS_CORE_INIT) {
      scope->captures++;
      if(scope->captures!=1 || !args[0] || (int)args[1]!=scope->rate || (int)args[2]!=scope->channels)scope->capture_invalid=1;
      scope->captured=(void *)args[0];return original;
    }
    if(s->selected_native) {
      if(scope->phase==DM_OPUS_CORE_DECODE && args[0]==(uintptr_t)s->sdk_state) {
        if(kind==DM_OPUS_CORE_CTL && (int)args[1]==OPUS_RESET_STATE)return (void *)native_core_reset;
        if(kind==DM_OPUS_CORE_DECODE && (int)args[5]==0)return (void *)native_core_decode;
      }
      reject_core(s,kind,args[1]);return (void *)core_reject;
    }
  }
  // An associated native state is never sent back to its unadvanced SDK state,
  // even when an unexpected raw API arrives outside the controlled wrapper.
  // Do not wait for serial while an unknown SDK caller may hold its global lock.
  lock();shadow *owned=contexts;
  while(owned && (!owned->selected_native || owned->sdk_state!=(void *)args[0]))owned=owned->next;
  if(owned) {
    atomic_store_explicit(&owned->core_faulted,1,memory_order_release);stats.native_api_rejects++;
    void (*logger)(const char *)=owned->report;
    int allowed=logger && messages<64;if(allowed)messages++;
    unlock();
    if(allowed){char line[256];snprintf(line,sizeof line,"[opus core] unsupported external owned API kind=%u state=%p; fail-closed=-5 until Reset/Init (no stale SDK fallback)",kind,(void *)args[0]);logger(line);}
    return (void *)core_reject;
  }
  unlock();return original;
}
static void destroy(void *self,int deleting) {
  shadow *s=borrow(self);if(!s)return;
  if(s->detached){release(s);return;}
  void (*original)(void *)=(void *)s->original[deleting?1:0];
  lock();shadow **at=&contexts;while(*at!=s)at=&(*at)->next;
  *at=s->next;s->detached=1;stats.live--;live_contexts[s->mode]--;unlock();
  // Restore BEFORE guest destruction, including destructors that make another
  // virtual call. Borrowers already queued on serial see detached and never
  // access self. Guest D0 alone frees self; release touches host ctx only.
  *(void ***)self=s->original;report(s,deleting?"D0":"D1",0);clear_native(s);
  original(self);release(s);
}
static void shadow_d1(void *self){destroy(self,0);}
static void shadow_d0(void *self){destroy(self,1);}
static uint64_t shadow_work_size(const void *self,int rate,int channels) {
  shadow *s=borrow((void *)self);if(!s)return 0;
  uint64_t result=0;
  if(!s->detached){uint64_t (*original)(const void *,int,int)=(void *)s->original[2];result=original(self,rate,channels);}
  release(s);return result;
}
static int query(const void *self,unsigned slot) {
  shadow *s=borrow((void *)self);if(!s)return 0;
  int result=0;
  if(!s->detached){int (*original)(const void *)=(void *)s->original[slot];result=original(self);}
  release(s);return result;
}
static int shadow_initialized(const void *self){return query(self,3);}
static int shadow_rate(const void *self){return query(self,6);}
static int shadow_channels(const void *self){return query(self,7);}
static uint32_t shadow_initialize(void *self,int rate,int channels,void *work,uint64_t size) {
  shadow *s=borrow(self);if(!s)return UINT32_MAX;
  if(s->detached){release(s);return UINT32_MAX;}
  uint32_t (*original)(void *,int,int,void *,uint64_t)=(void *)s->original[4];
  core_scope scope={.previous=current_core,.s=s,.phase=DM_OPUS_CORE_INIT,.rate=rate,.channels=channels};
  current_core=&scope;uint32_t result=original(self,rate,channels,work,size);current_core=scope.previous;
  if(result && s->mode==DM_OPUS_NATIVE && s->selected_native) {
    // A rejected re-Init does not discard an already advanced native history.
    // The audited SDK returns8 for invalid rate/channels before its first core
    // write and preserves its wrapper state/getters. Preserve the native state
    // too. Other failures may have touched SDK work: retain ownership but stop
    // math until a real Reset/successful Init; NEVER route to stale SDK history.
    int (*get_rate)(const void *)=(void *)s->original[6],(*get_channels)(const void *)=(void *)s->original[7];
    int (*is_initialized)(const void *)=(void *)s->original[3];
    int invalid_rate=rate!=8000 && rate!=12000 && rate!=16000 && rate!=24000 && rate!=48000;
    int unchanged=result==8 && (invalid_rate || channels<1 || channels>2) &&
        scope.captures==1 && !scope.capture_invalid && is_initialized(self) &&
        get_rate(self)==s->rate && get_channels(self)==s->channels;
    if(!unchanged)atomic_store_explicit(&s->core_faulted,1,memory_order_release);
    report(s,unchanged?"failed re-Init rejected before core write; native history retained":
        "failed re-Init; native ownership retained, fail-closed until Reset/Init",result);
    release(s);return result;
  }
  publish_binding(s,NULL,0);clear_native(s);
  atomic_store_explicit(&s->core_faulted,0,memory_order_release);
  // An unsupported/failed initialization never alters the SDK object's state
  // or guest work buffer and cannot select a native formal-output backend.
  int (*get_rate)(const void *)=(void *)s->original[6],(*get_channels)(const void *)=(void *)s->original[7];
  int (*is_initialized)(const void *)=(void *)s->original[3];
  if(!result && is_initialized(self) && get_rate(self)==rate && get_channels(self)==channels &&
      (rate==8000||rate==12000||rate==16000||rate==24000||rate==48000) && channels>=1 && channels<=MAX_CHANNELS) {
    s->rate=rate;s->channels=channels;
    if(s->mode==DM_OPUS_NATIVE && (scope.captures!=1 || scope.capture_invalid || !scope.captured)) {
      ADD_STAT(init_fallbacks,1);report(s,"native selection rejected before first packet: no exact SDK init capture",0);
      release(s);return result;
    }
    if(!native_call(native_initialize,s)){ADD_STAT(stack_rejects,1);report(s,"Initialize: no safe native stack",0);}
    else if(s->active){
      ADD_STAT(initialized,1);
      if(s->mode==DM_OPUS_NATIVE) {
        if(publish_binding(s,scope.captured,1)) {
          s->active=0;ADD_STAT(native_selected,1);report(s,"native core selected; original SDK wrapper retained",0);
        } else {
          clear_native(s);report(s,"native selection rejected: SDKstate already owned",0);
        }
      }
    }
    else{ADD_STAT(native_errors,1);report(s,"native Initialize failed",0);}

  }
  if(s->mode==DM_OPUS_NATIVE && !s->selected_native && !result)ADD_STAT(init_fallbacks,1);
  report(s,"Initialize",result);release(s);return result;
}
static void shadow_finalize(void *self) {
  shadow *s=borrow(self);if(!s)return;
  if(s->detached){release(s);return;}
  void (*original)(void *)=(void *)s->original[5];
  // Verified SDK Finalize clears its state and makes no further virtual call.
  original(self);report(s,"Finalize",0);publish_binding(s,NULL,0);clear_native(s);release(s);
}
static void shadow_reset(void *self) {
  shadow *s=borrow(self);if(!s)return;
  if(s->detached){release(s);return;}
  void (*original)(void *)=(void *)s->original[8];original(self);
  // SDK schedules reset until the next decoded packet. Mirror only after the
  // original reports success; do not guess whether an error advanced state.
  if(s->mode==DM_OPUS_SHADOW){s->reset_pending=1;s->active=s->decoder!=NULL;}
  release(s);
}
static uint32_t shadow_decode(void *self,uint64_t *consumed,int *count,int16_t *pcm,
                              uint64_t pcm_bytes,const void *input,uint64_t input_bytes) {
  shadow *s=borrow(self);if(!s)return UINT32_MAX;
  if(s->detached){release(s);return UINT32_MAX;}
  uint32_t (*original)(void *,uint64_t *,int *,int16_t *,uint64_t,const void *,uint64_t)=(void *)s->original[9];
  core_scope scope={.previous=current_core,.s=s,.phase=DM_OPUS_CORE_DECODE};
  current_core=&scope;
  uint32_t result=original(self,consumed,count,pcm,pcm_bytes,input,input_bytes);
  current_core=scope.previous;
  if(s->mode==DM_OPUS_NATIVE) {
    if(!result && s->selected_native && scope.called==1 && scope.result>=0 && count && *count==scope.result) {
      s->native_packets++;s->native_samples+=(unsigned)scope.result;
      lock();stats.native_packets++;stats.native_samples+=(unsigned)scope.result;
      stats.native_scalar_samples+=(uint64_t)(unsigned)scope.result*s->channels;unlock();
      if(s->native_packets==1 || s->native_packets==256)report(s,"native core packet success; SDK parsing/errors/mutex retained",scope.result);
    } else if(!result && s->selected_native) {
      // A successful wrapper that bypassed the audited core call cannot be
      // accepted as native validation, and can never select stale SDK history.
      reject_core(s,DM_OPUS_CORE_DECODE,0);result=5; // audited SDK maps OPUS_UNIMPLEMENTED to5
    } else if(!result)ADD_STAT(tracked_sdk_packets,1);
    if(result)ADD_STAT(sdk_errors,1);
    release(s);return result;
  }
  if(!result)ADD_STAT(tracked_sdk_packets,1);
  if(result){ADD_STAT(sdk_errors,1);s->active=0;s->reset_pending=0;release(s);return result;}
  if(!s->active){release(s);return result;}
  lock();int budget=s->compared<CONTEXT_BUDGET && attempts<PROCESS_BUDGET;
  if(budget)attempts++;
  unlock();
  if(!budget) {
    ADD_STAT(stopped_budget,1);s->active=0;
    if(s->compared>=CONTEXT_BUDGET)
      report(s,s->max_difference<=1?
          "bounded complete; tolerance_success=1 (not full compatibility)":
          "bounded complete; lifetime tolerance_success=0 (earlier failure)",0);
    else report(s,"process budget exhausted; incomplete object window",0);
    release(s);return result;
  }
  // The original wrapper validated the real header and supplied consumed.
  // Reuse that span; do not infer packet header layout/endianness from audio.
  if(!consumed || !count || !input || !pcm || *consumed<8 || *consumed>input_bytes ||
      *consumed-8>INT_MAX || *count<=0 || *count>MAX_SAMPLES ||
      (uint64_t)*count*s->channels*2>pcm_bytes) {
    ADD_STAT(bounds_rejects,1);s->active=0;report(s,"successful SDK bounds rejected",0);release(s);return result;
  }
  // Reject aliasing before reading the post-SDK packet. The original already
  // succeeded, but overlapping outputs may have overwritten its input bytes.
  if(!disjoint(input,input_bytes,pcm,pcm_bytes) ||
      !disjoint(input,input_bytes,consumed,sizeof *consumed) ||
      !disjoint(input,input_bytes,count,sizeof *count)) {
    ADD_STAT(bounds_rejects,1);s->active=0;release(s);return result;
  }
  decode_job job={s,(const unsigned char *)input+8,(int)(*consumed-8),*count,OPUS_INTERNAL_ERROR,0};
  if(!native_call(native_decode,&job)) {
    ADD_STAT(stack_rejects,1);s->active=0;report(s,"no safe native stack",0);release(s);return result;
  }
  int decoded=job.decoded;
  if(decoded!=*count || job.guards){ADD_STAT(native_errors,1);s->active=0;report(s,"native count/guard mismatch",decoded);release(s);return result;}
  unsigned differences=0,max_difference=0;
  for(unsigned n=0;n<(unsigned)decoded*s->channels;n++) {
    int difference=(int)pcm[n]-(int)s->pcm[8+n];if(difference<0)difference=-difference;
    if(difference){differences++;if((unsigned)difference>max_difference)max_difference=difference;}
  }
  s->compared++;lock();stats.compared++;stats.pcm_samples+=(uint64_t)decoded*s->channels;
  s->differences+=differences;stats.pcm_differences+=differences;
  if(max_difference>s->max_difference)s->max_difference=max_difference;
  if(max_difference>stats.max_abs_difference)stats.max_abs_difference=max_difference;
  unlock();
  if(differences)s->difference_packets++;
  if(max_difference>1) {
    s->active=0; // out-of-tolerance stops regardless of the bounded log quota
    report(s,"PCM difference >1LSB; shadow stopped",decoded);
  } else {
    // This is a correctness-only observation allowance, never a native-output
    // switch or a claim of bit-exact/full SDK compatibility.
    if(differences && s->difference_packets<=4)
      report(s,"non-bit-exact <=1LSB; shadow continues",decoded);
    if(s->compared>=CONTEXT_BUDGET) {
      s->active=0;ADD_STAT(stopped_budget,1);
      report(s,s->max_difference<=1?
          "bounded complete; tolerance_success=1 (not full compatibility)":
          "bounded complete; lifetime tolerance_success=0 (earlier failure)",0);
    }
  }
  release(s);return result; // Only SDK has written caller output/count/consumed.
}
int dm_opus_attach_mode(void *self,const void *expected_table,void *const expected[10],void (*log)(const char *),int mode) {
  if(mode!=DM_OPUS_SHADOW && mode!=DM_OPUS_NATIVE)return 0;
  if(!self || !expected_table)return 0;
  void **vt=*(void ***)self;
  if(vt!=(void **)expected_table+2 || (uintptr_t)vt[-2])return 0;
  for(unsigned n=0;n<10;n++)if(!expected[n] || vt[n]!=expected[n])return 0;
  unsigned limit=mode==DM_OPUS_NATIVE?MAX_NATIVE_CONTEXTS:MAX_SHADOW_CONTEXTS;
  lock();if(live_contexts[mode]>=limit){stats.capacity_rejects++;unlock();return 0;}
  for(shadow *at=contexts;at;at=at->next)if(at->self==self){unlock();return 0;}
  shadow *s=calloc(1,sizeof *s);if(!s){unlock();return 0;}
  pthread_mutexattr_t attr;pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE);
  int error=pthread_mutex_init(&s->serial,&attr);pthread_mutexattr_destroy(&attr);
  if(error){free(s);unlock();return 0;}
  pthread_mutex_lock(&s->serial);s->refs=1;
  s->self=self;s->original=vt;s->report=log;s->mode=mode;memcpy(s->table,vt-2,sizeof s->table);
  s->table[2]=(uintptr_t)shadow_d1;s->table[3]=(uintptr_t)shadow_d0;
  s->table[4]=(uintptr_t)shadow_work_size;s->table[5]=(uintptr_t)shadow_initialized;
  s->table[8]=(uintptr_t)shadow_rate;s->table[9]=(uintptr_t)shadow_channels;
  s->table[6]=(uintptr_t)shadow_initialize;s->table[7]=(uintptr_t)shadow_finalize;
  s->table[10]=(uintptr_t)shadow_reset;s->table[11]=(uintptr_t)shadow_decode;
  s->next=contexts;contexts=s;stats.attached++;stats.live++;live_contexts[mode]++;*(void ***)self=(void **)(s->table+2);
  unlock();report(s,mode==DM_OPUS_SHADOW?"attached (shadow only)":"attached (native requested, pending Init)",0);release(s);return 1;
}
int dm_opus_shadow_attach(void *self,const void *table,void *const slots[10],void (*log)(const char *)) {
  return dm_opus_attach_mode(self,table,slots,log,DM_OPUS_SHADOW);
}
void dm_opus_shadow_snapshot(dm_opus_shadow_stats *out){lock();*out=stats;unlock();}
