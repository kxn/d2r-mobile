// Owned ABI/lifecycle oracle, NOT the SDK conformance or real-game acceptance.
// Original-method mocks use a separate upstream decoder; the mirror must not
// alter their output, ownership, forwarding, errors, reset or concurrency.
#include "opus_shadow.h"
// Include the real implementation solely to inspect its borrow predicate in
// this deterministic lifecycle test. No production test hook/counter is added.
#include "../opus_shadow.c"
#include <opus.h>
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sched.h>

typedef struct {void **vptr;OpusDecoder *decoder;int rate,channels,reset,error,block,adjustment,skip_capture,double_capture,init_error;int16_t expected_pcm0;} object;
static uintptr_t table[12];
static unsigned finalized,deleted,logs,tolerated_logs,summary_logs;
static unsigned sdk_core_decodes,sdk_core_resets;
static int original_core_init(void *state,int rate,int channels){return opus_decoder_init(state,rate,channels);}
static int original_core_decode(void *state,const unsigned char *input,int size,int16_t *pcm,int capacity,int fec) {
  sdk_core_decodes++;return opus_decode(state,input,size,pcm,capacity,fec);
}
static int original_core_reset(void *state,int request){assert(request==OPUS_RESET_STATE);sdk_core_resets++;return opus_decoder_ctl(state,request);}
static int selected_init(object *s,int rate,int channels) {
  uintptr_t args[8]={(uintptr_t)s->decoder,(unsigned)rate,(unsigned)channels};
  int (*fn)(void *,int,int)=s->skip_capture?original_core_init:
      (int (*)(void *,int,int))dm_opus_core_target(DM_OPUS_CORE_INIT,args,(void *)original_core_init);
  int result=fn(s->decoder,rate,channels);
  if(s->double_capture){fn=dm_opus_core_target(DM_OPUS_CORE_INIT,args,(void *)original_core_init);result=fn(s->decoder,rate,channels);}
  return result;
}
static pthread_mutex_t control=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static int a_entered,a_release,b_finished,d_started,d_finished;
static void (*saved_finalize)(void *);
static uint64_t work_size(const void *self,int rate,int channels){(void)self;return (uint64_t)rate*channels+77;}
static int initialized(const void *self){return ((const object *)self)->decoder!=NULL;}
static void finalize(void *self){object *s=self;if(s->decoder)opus_decoder_destroy(s->decoder);s->decoder=NULL;finalized++;}
static void d1(void *self){object *s=self;assert(s->vptr==(void **)(table+2));((void (*)(void *))s->vptr[5])(self);}
static void d0(void *self){d1(self);pthread_mutex_lock(&control);deleted++;pthread_mutex_unlock(&control);free(self);}
static uint32_t initialize(void *self,int rate,int channels,void *work,uint64_t size){
  object *s=self;assert(work==(void *)0x1234 && size==98765);
  if(s->init_error)return 33; // original wrapper failure preserves existing object
  if(s->decoder && ((rate!=8000 && rate!=12000 && rate!=16000 && rate!=24000 && rate!=48000) || channels<1 || channels>2)) {
    assert(selected_init(s,rate,channels)==OPUS_BAD_ARG);return 8;
  }
  finalize(s);s->rate=rate;s->channels=channels;s->reset=0;
  int error;s->decoder=opus_decoder_create(rate,channels,&error);
  if(!error)error=selected_init(s,rate,channels);
  return error?77:0;
}
static int rate(const void *self){return ((const object *)self)->rate;}
static int channels(const void *self){return ((const object *)self)->channels;}
static void reset(void *self){((object *)self)->reset=1;}
static uint32_t decode(void *self,uint64_t *consumed,int *count,int16_t *pcm,uint64_t pcm_bytes,const void *input,uint64_t input_bytes){
  object *s=self;
  if(s->block){pthread_mutex_lock(&control);a_entered=1;pthread_cond_broadcast(&changed);while(!a_release)pthread_cond_wait(&changed,&control);pthread_mutex_unlock(&control);}
  if(s->error){s->error=0;return 99;}
  if(input_bytes<8)return 6;
  uint32_t n;memcpy(&n,input,4);if(n>input_bytes-8)return 6;
  if(!s->decoder || !count || !consumed || !pcm || !input)return 4;
  int samples=opus_packet_get_nb_samples((const unsigned char *)input+8,n,s->rate);
  if(samples<0)return 6;
  if(pcm_bytes<(uint64_t)samples*s->channels*2)return 7;
  if(s->reset){
    uintptr_t reset_args[8]={(uintptr_t)s->decoder,OPUS_RESET_STATE};
    int (*fn)(void *,int)=dm_opus_core_target(DM_OPUS_CORE_CTL,reset_args,(void *)original_core_reset);
    s->reset=0;if(fn(s->decoder,OPUS_RESET_STATE)<0)return 4;
  }
  uintptr_t args[8]={(uintptr_t)s->decoder,(uintptr_t)input+8,n,(uintptr_t)pcm,(uintptr_t)(pcm_bytes/2/s->channels),0};
  int (*fn)(void *,const unsigned char *,int,int16_t *,int,int)=dm_opus_core_target(DM_OPUS_CORE_DECODE,args,(void *)original_core_decode);
  int decoded=fn(s->decoder,(const unsigned char *)input+8,n,pcm,(int)(pcm_bytes/2/s->channels),0);
  if(decoded<0)return decoded==-3?9:decoded==-5?5:4;
  assert(pcm[0]>=INT16_MIN+2 && pcm[0]<=INT16_MAX-2);
  pcm[0]=(int16_t)(pcm[0]+s->adjustment);s->expected_pcm0=pcm[0];
  *count=decoded;*consumed=n+8;return 0;
}
static void log_line(const char *s){
  assert(strstr(s,"formal=SDK") || strstr(s,"formal=native-core/SDK-wrapper") || strstr(s,"[opus core]"));logs++;
  if(strstr(s,"non-bit-exact <=1LSB; shadow continues"))tolerated_logs++;
  if(strstr(s,"bounded complete; tolerance_success=1")) {
    assert(strstr(s,"successful_packets=256"));
    assert(strstr(s,"not full compatibility"));summary_logs++;
  }
}
static object *new_mode(int mode){
  object *s=calloc(1,sizeof *s);assert(s);s->vptr=(void **)(table+2);
  assert(dm_opus_attach_mode(s,table,(void **)(table+2),log_line,mode));
  assert(((uint64_t (*)(const void *,int,int))s->vptr[2])(s,48000,2)==96077);
  return s;
}
static object *new_object(void){return new_mode(DM_OPUS_SHADOW);}
static void init(object *s,int rate_value,int channel_value){
  assert(!((uint32_t (*)(void *,int,int,void *,uint64_t))s->vptr[4])(s,rate_value,channel_value,(void *)0x1234,98765));
  assert(((int (*)(const void *))s->vptr[3])(s));
  assert(((int (*)(const void *))s->vptr[6])(s)==rate_value);
  assert(((int (*)(const void *))s->vptr[7])(s)==channel_value);
}
static unsigned char packet[4096];static uint64_t packet_bytes;
static unsigned call(object *s){
  int16_t pcm[5760*2+2];pcm[0]=pcm[5760*2+1]=12345;
  uint64_t consumed=0xfeed;int count=-300;
  uint32_t result=((uint32_t (*)(void *,uint64_t *,int *,int16_t *,uint64_t,const void *,uint64_t))s->vptr[9])
       (s,&consumed,&count,pcm+1,5760*2*2,packet,packet_bytes);
  assert(pcm[0]==12345 && pcm[5760*2+1]==12345);
  if(!result){assert(consumed==packet_bytes && count==s->rate/50 && pcm[1]==s->expected_pcm0);}
  else{assert(consumed==0xfeed && count==-300);}
  return result;
}
static dm_opus_shadow_stats snapshot(void){dm_opus_shadow_stats s;dm_opus_shadow_snapshot(&s);return s;}
static void *thread_a(void *arg){assert(!call(arg));return NULL;}
static void *thread_b(void *arg){assert(!call(arg));pthread_mutex_lock(&control);b_finished=1;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&control);return NULL;}
static void *thread_d(void *arg){
  object *s=arg;void (*dtor)(void *)=(void *)s->vptr[1];
  pthread_mutex_lock(&control);d_started=1;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&control);
  dtor(s);
  pthread_mutex_lock(&control);d_finished=1;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&control);return NULL;
}
static void *thread_finalize(void *arg){saved_finalize(arg);return NULL;}
static void compare_pair(object *reference,object *candidate,uint64_t bytes,uint64_t capacity) {
  int16_t a[5760*2+2],b[5760*2+2];memset(a,0x35,sizeof a);memcpy(b,a,sizeof a);
  uint64_t used_a=0x7788,used_b=used_a;int count_a=-88,count_b=count_a;
  uint32_t (*ref)(void *,uint64_t *,int *,int16_t *,uint64_t,const void *,uint64_t)=(void *)reference->vptr[9];
  uint32_t (*test)(void *,uint64_t *,int *,int16_t *,uint64_t,const void *,uint64_t)=(void *)candidate->vptr[9];
  uint32_t ra=ref(reference,&used_a,&count_a,a+1,capacity,packet,bytes);
  uint32_t rb=test(candidate,&used_b,&count_b,b+1,capacity,packet,bytes);
  assert(ra==rb && used_a==used_b && count_a==count_b && !memcmp(a,b,sizeof a));
}
static void native_mode_test(void) {
  object *reference=calloc(1,sizeof *reference);reference->vptr=(void **)(table+2);
  object *candidate=new_mode(DM_OPUS_NATIVE);init(reference,48000,2);init(candidate,48000,2);
  shadow *state=borrow(candidate);assert(state && state->selected_native && state->sdk_state==candidate->decoder && !state->pcm);release(state);
  unsigned initial_decodes=sdk_core_decodes;
  for(unsigned i=0;i<300;i++)compare_pair(reference,candidate,packet_bytes,5760*2*2);
  assert(sdk_core_decodes==initial_decodes+300); // onlyreference reached original math
  state=borrow(candidate);assert(state && state->native_packets==300 && state->selected_native);release(state);
  // Actual SDK rejects these before touching core bytes and retains getters.
  // Preserve the advanced native history, not an unadvanced SDK fallback.
  for(unsigned bad=0;bad<2;bad++) {
    OpusDecoder *old=candidate->decoder;
    assert(((uint32_t (*)(void *,int,int,void *,uint64_t))reference->vptr[4])(reference,bad?48000:44100,bad?3:2,(void *)0x1234,98765)==8);
    assert(((uint32_t (*)(void *,int,int,void *,uint64_t))candidate->vptr[4])(candidate,bad?48000:44100,bad?3:2,(void *)0x1234,98765)==8);
    assert(candidate->decoder==old && initialized(candidate) && rate(candidate)==48000 && channels(candidate)==2);
    state=borrow(candidate);assert(state && state->selected_native && state->sdk_state==old && state->decoder);release(state);
    compare_pair(reference,candidate,packet_bytes,5760*2*2);
    assert(sdk_core_decodes==initial_decodes+301+bad);
  }
  candidate->init_error=1;
  assert(((uint32_t (*)(void *,int,int,void *,uint64_t))candidate->vptr[4])(candidate,48000,2,(void *)0x1234,98765)==33);
  candidate->init_error=0;
  unsigned failed_decodes=sdk_core_decodes;assert(call(candidate)==5 && sdk_core_decodes==failed_decodes);
  ((void (*)(void *))reference->vptr[8])(reference);((void (*)(void *))candidate->vptr[8])(candidate);
  compare_pair(reference,candidate,packet_bytes,5760*2*2);
  // Original wrapper rejects truncated packet/tinycapacity before either math;
  // history remains advanced only by valid packets on both backends.
  compare_pair(reference,candidate,7,5760*2*2);compare_pair(reference,candidate,packet_bytes,2);
  assert(sdk_core_decodes==initial_decodes+303);
  ((void (*)(void *))reference->vptr[8])(reference);((void (*)(void *))candidate->vptr[8])(candidate);
  compare_pair(reference,candidate,packet_bytes,5760*2*2);
  unsigned untouched=0x12345678;
  uintptr_t args[8]={(uintptr_t)candidate->decoder,OPUS_GET_FINAL_RANGE_REQUEST,(uintptr_t)&untouched};
  int (*unsupported)(void *,int,...)=dm_opus_core_target(DM_OPUS_CORE_CTL,args,(void *)original_core_reset);
  assert(unsupported(candidate->decoder,OPUS_GET_FINAL_RANGE_REQUEST,&untouched)==OPUS_UNIMPLEMENTED && untouched==0x12345678);
  unsigned decodes=sdk_core_decodes;assert(call(candidate)==5 && sdk_core_decodes==decodes); // no stale SDKfallback
  ((void (*)(void *))reference->vptr[8])(reference);((void (*)(void *))candidate->vptr[8])(candidate);
  compare_pair(reference,candidate,packet_bytes,5760*2*2); // knownReset clears fault
  // Caller mutating the SDK wrapper's state pointer cannot cause fallback to
  // some other unadvanced SDK decoder after native selection.
  OpusDecoder *owned_state=candidate->decoder;candidate->decoder=reference->decoder;
  decodes=sdk_core_decodes;assert(call(candidate)==5 && sdk_core_decodes==decodes);
  candidate->decoder=owned_state;
  ((void (*)(void *))reference->vptr[8])(reference);((void (*)(void *))candidate->vptr[8])(candidate);
  compare_pair(reference,candidate,packet_bytes,5760*2*2);
  // Unowned state remains original. No reinterpretation of caller SDK work.
  args[0]=(uintptr_t)reference->decoder;assert(dm_opus_core_target(DM_OPUS_CORE_CTL,args,(void *)original_core_reset)==(void *)original_core_reset);
  ((void (*)(void *))candidate->vptr[5])(candidate);
  init(candidate,16000,1);init(reference,16000,1);compare_pair(reference,candidate,packet_bytes,5760*2*2);
  // Missing or ambiguous actual init capture chooses SDK beforefirstpacket;
  // later healthy packets cannot spontaneously activate native.
  for(unsigned mode=0;mode<2;mode++) {
    candidate->skip_capture=mode==0;candidate->double_capture=mode==1;init(candidate,48000,2);init(reference,48000,2);
    state=borrow(candidate);assert(state && !state->selected_native && !state->decoder);release(state);
    decodes=sdk_core_decodes;compare_pair(reference,candidate,packet_bytes,5760*2*2);assert(sdk_core_decodes==decodes+2);
  }
  candidate->skip_capture=candidate->double_capture=0;init(candidate,48000,2);init(reference,48000,2);
  compare_pair(reference,candidate,packet_bytes,5760*2*2);
  ((void (*)(void *))candidate->vptr[1])(candidate);d0(reference);
  dm_opus_shadow_stats s=snapshot();assert(s.native_selected>=3 && s.native_packets==308 && s.native_samples>0 && s.native_scalar_samples>s.native_samples && s.native_resets==4 && s.native_api_rejects==2 && s.init_fallbacks==2 && !s.live);
}
static void capacity_test(void) {
  // Actual decoder allocations and real map/ownership, not a mirrored limit
  // function: mixed modes prove independent budgets even though production
  // selects one startup mode. Every native object owns initialized codec state.
  object *native[64],*shadows[32];dm_opus_shadow_stats before=snapshot();
  for(unsigned i=0;i<64;i++){native[i]=new_mode(DM_OPUS_NATIVE);init(native[i],48000,2);}
  for(unsigned i=0;i<32;i++){shadows[i]=new_mode(DM_OPUS_SHADOW);init(shadows[i],48000,2);}
  assert(snapshot().live==96 && snapshot().native_selected==before.native_selected+64);
  object *rejected[2];
  for(unsigned i=0;i<2;i++) {
    rejected[i]=calloc(1,sizeof *rejected[i]);assert(rejected[i]);rejected[i]->vptr=(void **)(table+2);
    assert(!dm_opus_attach_mode(rejected[i],table,(void **)(table+2),log_line,i?DM_OPUS_SHADOW:DM_OPUS_NATIVE));
    assert(rejected[i]->vptr==(void **)(table+2));init(rejected[i],48000,2);
  }
  assert(snapshot().capacity_rejects==before.capacity_rejects+2);
  object *reference=calloc(1,sizeof *reference);assert(reference);reference->vptr=(void **)(table+2);init(reference,48000,2);
  unsigned decodes=sdk_core_decodes;
  for(unsigned i=0;i<8;i++)compare_pair(reference,native[0],packet_bytes,5760*2*2);
  assert(sdk_core_decodes==decodes+8); // original reference only
  ((void (*)(void *))native[0]->vptr[1])(native[0]);
  native[0]=new_mode(DM_OPUS_NATIVE);init(native[0],48000,2); // released capacity really reused
  assert(snapshot().live==96);
  // A previously rejected, already initialized SDK object never changes backend
  // merely because another object freed a slot. No late history substitution.
  decodes=sdk_core_decodes;assert(!call(rejected[0]));assert(sdk_core_decodes==decodes+1);
  assert(rejected[0]->vptr==(void **)(table+2));
  for(unsigned i=0;i<64;i++)((void (*)(void *))native[i]->vptr[1])(native[i]);
  for(unsigned i=0;i<32;i++)((void (*)(void *))shadows[i]->vptr[1])(shadows[i]);
  for(unsigned i=0;i<2;i++)d0(rejected[i]);
  d0(reference);
  assert(snapshot().live==0 && live_contexts[DM_OPUS_NATIVE]==0 && live_contexts[DM_OPUS_SHADOW]==0);
  assert(snapshot().capacity_rejects==before.capacity_rejects+2);
}
int main(void){
  alarm(25); // Deadlock safeguard only; no latency/performance measurement.
  table[0]=0;table[1]=0x11223344;
  void *slots[10]={(void *)d1,(void *)d0,(void *)work_size,(void *)initialized,(void *)initialize,(void *)finalize,(void *)rate,(void *)channels,(void *)reset,(void *)decode};
  memcpy(table+2,slots,sizeof slots);
  object bare={.vptr=(void **)(table+2)};void *wrong[10];memcpy(wrong,slots,sizeof wrong);wrong[4]=NULL;
  assert(!dm_opus_shadow_attach(&bare,table,wrong,NULL));assert(bare.vptr==(void **)(table+2));
  int error;OpusEncoder *encoder=opus_encoder_create(48000,2,OPUS_APPLICATION_AUDIO,&error);assert(encoder && !error);
  int16_t source[1920];for(unsigned n=0;n<1920;n++)source[n]=(int16_t)((n*173)%10000-5000);
  int bytes=opus_encode(encoder,source,960,packet+8,sizeof packet-8);assert(bytes>0);uint32_t n=bytes;memcpy(packet,&n,4);packet_bytes=bytes+8;opus_encoder_destroy(encoder);
  object *a=new_object(),*b=new_object();init(a,48000,2);init(b,48000,2);
  for(unsigned i=0;i<12;i++){assert(!call(a));assert(!call(b));}
  dm_opus_shadow_stats before=snapshot();assert(before.compared==24 && before.pcm_differences==0);
  const int rates[]={8000,12000,16000,24000,48000};
  for(unsigned r=0;r<5;r++){init(b,rates[r],1);assert(!call(b));}
  init(b,48000,2);before=snapshot();
  a->error=1;assert(call(a)==99);assert(!call(a));assert(snapshot().compared==before.compared);
  ((void (*)(void *))a->vptr[8])(a);assert(!call(a));assert(snapshot().compared==before.compared+1);
  ((void (*)(void *))a->vptr[5])(a);assert(!((int (*)(const void *))a->vptr[3])(a));init(a,48000,2);assert(!call(a));
  // One blocked SDK call must not prevent a different decoder from finishing.
  a->block=1;pthread_t ta,tb,td,tf;assert(!pthread_create(&ta,NULL,thread_a,a));
  pthread_mutex_lock(&control);while(!a_entered)pthread_cond_wait(&changed,&control);pthread_mutex_unlock(&control);
  assert(!pthread_create(&tb,NULL,thread_b,b));
  pthread_mutex_lock(&control);while(!b_finished)pthread_cond_wait(&changed,&control);assert(!a_release);pthread_mutex_unlock(&control);
  // D0 and Finalize both borrow while Decode owns this object's serial lock.
  // Either waiter may win afterwards; Finalize must never touch freed self.
  saved_finalize=(void *)a->vptr[5];assert(!pthread_create(&tf,NULL,thread_finalize,a));
  assert(!pthread_create(&td,NULL,thread_d,a));
  pthread_mutex_lock(&control);while(!d_started)pthread_cond_wait(&changed,&control);assert(!d_finished);pthread_mutex_unlock(&control);
  for(;;){lock();shadow *at=contexts;while(at && at->self!=a)at=at->next;int borrowed=at && at->refs>=3;unlock();if(borrowed)break;sched_yield();}
  pthread_mutex_lock(&control);a_release=1;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&control);
  assert(!pthread_join(ta,NULL));assert(!pthread_join(tb,NULL));assert(!pthread_join(td,NULL));assert(!pthread_join(tf,NULL));assert(deleted==1);
  // A failed original init cannot activate native decoding; a later explicit
  // successful init is the only way to create another independent state.
  assert(((uint32_t (*)(void *,int,int,void *,uint64_t))b->vptr[4])(b,12345,2,(void *)0x1234,98765)==8);
  assert(((int (*)(const void *))b->vptr[3])(b));assert(rate(b)==48000 && channels(b)==2);init(b,48000,2);assert(!call(b));
  for(unsigned i=0;i<300;i++)assert(!call(b));
  dm_opus_shadow_stats bounded=snapshot();assert(bounded.stopped_budget==1 && bounded.compared<=CONTEXT_BUDGET*2);
  ((void (*)(void *))b->vptr[0])(b);assert(b->vptr==(void **)(table+2));free(b);
  dm_opus_shadow_stats after=snapshot();assert(after.live==0 && after.native_errors==0 && after.pcm_differences==0 && after.sdk_errors==1);
  unsigned initial_summary=summary_logs,initial_tolerated=tolerated_logs;
  // Alter only the mock oracle output, not the native decoder or its state.
  // Formal PCM must retain these exact +/-1 values throughout all256 packets.
  object *one=new_object();init(one,48000,2);dm_opus_shadow_stats exact=snapshot();
  for(unsigned i=0;i<CONTEXT_BUDGET;i++) {
    one->adjustment=(i&1)?-1:1;assert(!call(one));
    shadow *state=borrow(one);assert(state && state->compared==i+1 && state->difference_packets==i+1);
    assert(state->active==(i+1<CONTEXT_BUDGET));release(state);
  }
  dm_opus_shadow_stats tolerant=snapshot();
  assert(tolerant.compared==exact.compared+256 && tolerant.pcm_differences==exact.pcm_differences+256);
  assert(tolerant.max_abs_difference==1 && summary_logs==initial_summary+1 && tolerated_logs==initial_tolerated+4);
  assert(!call(one));assert(snapshot().compared==tolerant.compared); // no257th comparison
  ((void (*)(void *))one->vptr[1])(one);
  for(int delta=-2;delta<=2;delta+=4) {
    object *two=new_object();init(two,48000,2);
    // Exceed the per-object difference log quota before the hard error; it
    // must still stop, not be hidden by the first-four logging limit.
    two->adjustment=1;for(unsigned i=0;i<5;i++)assert(!call(two));
    two->adjustment=delta;before=snapshot();
    assert(!call(two));shadow *state=borrow(two);assert(state && !state->active && state->compared==6 && state->max_difference==2);release(state);
    assert(snapshot().compared==before.compared+1);
    two->adjustment=0;assert(!call(two));assert(snapshot().compared==before.compared+1);
    ((void (*)(void *))two->vptr[8])(two);assert(!call(two));
    state=borrow(two);assert(state && state->active && state->compared==7 && !state->reset_pending);release(state);
    assert(snapshot().compared==before.compared+2); // explicitReset resynchronizes bothstates
    ((void (*)(void *))two->vptr[1])(two);
  }
  after=snapshot();assert(after.live==0 && after.native_errors==0 && after.max_abs_difference==2 && after.sdk_errors==1);
  assert(finalized>=6 && logs>0 && logs<=64);
  native_mode_test();capacity_test();after=snapshot();
  printf("opus shadow: original PCM/count/errors preserved, independent decoders, reset/init/finalize/D0/D1/borrow lifetime PASS; +/-1LSB continued256, +/-2 stopped then Reset restored; compared=%llu (owned mocks, NOT SDK conformance)\n",(unsigned long long)after.compared);
}
