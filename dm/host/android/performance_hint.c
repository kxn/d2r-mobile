// Public Android performance hints. No affinity/clock/thermal overrides.
// Optional NDK symbols preserve API29 compatibility; unsupported devices keep
// the original policy. Session calls stay on their owning native thread.
#include "dm_performance_hint.h"
#include "dm.h"
#include <android/performance_hint.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
static struct {
  APerformanceHintManager *manager;
  APerformanceHintManager *(*get)(void);
  int64_t (*preferred)(APerformanceHintManager *);
  APerformanceHintSession *(*create)(APerformanceHintManager *,const int32_t *,size_t,int64_t);
  int (*target)(APerformanceHintSession *,int64_t);
  int (*report)(APerformanceHintSession *,int64_t);
  void (*close)(APerformanceHintSession *);
  AWorkDuration *(*work_create)(void);
  void (*work_release)(AWorkDuration *);
  void (*work_start)(AWorkDuration *,int64_t);
  void (*work_total)(AWorkDuration *,int64_t);
  void (*work_cpu)(AWorkDuration *,int64_t);
  void (*work_gpu)(AWorkDuration *,int64_t);
  int (*report2)(APerformanceHintSession *,AWorkDuration *);
} api;
static pthread_once_t once=PTHREAD_ONCE_INIT;
static _Atomic int reference_mode=1,available=-1,active,v2,last_error,last_tid;
static _Atomic uint64_t sessions,reports,failures,last_target,last_cpu,last_wall;
static _Atomic int64_t preferred_ns;
static _Thread_local struct {
  APerformanceHintSession *session;
  AWorkDuration *work;
  uint64_t budget,target;
  int failed;
} owner;

static void initialize(void) {
  void *lib=dlopen("libandroid.so",RTLD_NOW|RTLD_LOCAL);
  if(!lib) { atomic_store(&available,0);return; }
#define LOAD(member,name) *(void **)(&api.member)=dlsym(lib,name)
  LOAD(get,"APerformanceHint_getManager");
  LOAD(preferred,"APerformanceHint_getPreferredUpdateRateNanos");
  LOAD(create,"APerformanceHint_createSession");
  LOAD(target,"APerformanceHint_updateTargetWorkDuration");
  LOAD(report,"APerformanceHint_reportActualWorkDuration");
  LOAD(close,"APerformanceHint_closeSession");
  LOAD(work_create,"AWorkDuration_create");LOAD(work_release,"AWorkDuration_release");
  LOAD(work_start,"AWorkDuration_setWorkPeriodStartTimestampNanos");
  LOAD(work_total,"AWorkDuration_setActualTotalDurationNanos");
  LOAD(work_cpu,"AWorkDuration_setActualCpuDurationNanos");
  LOAD(work_gpu,"AWorkDuration_setActualGpuDurationNanos");
  LOAD(report2,"APerformanceHint_reportActualWorkDuration2");
#undef LOAD
  if(!api.get||!api.preferred||!api.create||!api.target||!api.report||!api.close||!(api.manager=api.get())) {
    atomic_store(&available,0);return;
  }
  int64_t rate=api.preferred(api.manager);atomic_store(&preferred_ns,rate);
  atomic_store(&available,rate>0);
  atomic_store(&v2,!!(api.work_create&&api.work_release&&api.work_start&&api.work_total&&api.work_cpu&&api.work_gpu&&api.report2));
  LOG("[android performance hints] available=%d preferred_ns=%lld duration_v2=%d",
      atomic_load(&available),(long long)rate,atomic_load(&v2));
}
void dm_android_hint_reference(int reference) { atomic_store(&reference_mode,!!reference); }
void dm_android_hint_budget(uint64_t period_ns) { owner.budget=period_ns; }
static void close_owner(void) {
  if(owner.session) { api.close(owner.session);owner.session=NULL;atomic_fetch_sub(&active,1); }
  if(owner.work) { api.work_release(owner.work);owner.work=NULL; }
  owner.target=0;
}
static void failed(int error) {
  atomic_store(&last_error,error);atomic_fetch_add(&failures,1);
  close_owner();owner.failed=1;
  LOG("[android performance hints] tid=%d disabled after error=%d",(int)gettid(),error);
}
void dm_android_hint_work(uint64_t start_ns,uint64_t wall_ns,uint64_t cpu_ns) {
  if(atomic_load(&reference_mode)) { close_owner();owner.failed=0;return; }
  if(owner.failed||!owner.budget||owner.budget>UINT64_C(1000000000)||!start_ns||!wall_ns||!cpu_ns) return;
  pthread_once(&once,initialize);
  if(!atomic_load(&available)) return;
  if(!owner.session) {
    int32_t tid=gettid();owner.session=api.create(api.manager,&tid,1,owner.budget);
    if(!owner.session) { failed(ENOTSUP);return; }
    owner.target=owner.budget;
    if(atomic_load(&v2)) owner.work=api.work_create();
    atomic_fetch_add(&active,1);atomic_fetch_add(&sessions,1);atomic_store(&last_tid,tid);
    LOG("[android performance hints] session tid=%d target_ns=%llu",tid,(unsigned long long)owner.target);
  }
  int result=0;
  if(owner.target!=owner.budget) {
    result=api.target(owner.session,owner.budget);
    if(result) { failed(result);return; }
    owner.target=owner.budget;
  }
  if(owner.work) {
    api.work_start(owner.work,start_ns);api.work_total(owner.work,wall_ns);
    api.work_cpu(owner.work,cpu_ns);
    // Zero explicitly means "GPU not measured" in the NDK work-duration API.
    // Do not mislabel CPU blocking time as GPU execution time.
    api.work_gpu(owner.work,0);result=api.report2(owner.session,owner.work);
  } else result=api.report(owner.session,wall_ns);
  if(result) { failed(result);return; }
  atomic_fetch_add(&reports,1);atomic_store(&last_target,owner.target);
  atomic_store(&last_cpu,cpu_ns);atomic_store(&last_wall,wall_ns);
}
void dm_android_hint_snapshot(dm_android_hint_stats *out) {
  *out=(dm_android_hint_stats){.reference=atomic_load(&reference_mode),.available=atomic_load(&available),
    .active=atomic_load(&active),.v2=atomic_load(&v2),.last_error=atomic_load(&last_error),.tid=atomic_load(&last_tid),
    .sessions=atomic_load(&sessions),.reports=atomic_load(&reports),.failures=atomic_load(&failures),
    .target_ns=atomic_load(&last_target),.cpu_ns=atomic_load(&last_cpu),.wall_ns=atomic_load(&last_wall),
    .preferred_ns=atomic_load(&preferred_ns)};
}
