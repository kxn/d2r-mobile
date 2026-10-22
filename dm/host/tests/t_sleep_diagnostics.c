// Real production SleepThread dispatch, with call-count oracles, not a timer.
#include "../dm.h"
#include <sched.h>
static int checked_sched_yield(void);
#define sched_yield checked_sched_yield
#include "../hle_thread.c"
#undef sched_yield

struct dm_flags g_flag;
static unsigned clock_calls,yield_calls,prof_calls;
// Deterministic diagnostic-clock stub; no wall-time/performance measurement.
uint64_t now_ns(void) { return ++clock_calls; }
uint64_t guest_ns(void) { abort(); }
int dm_detclock_enabled(void) { return 0; }
static int checked_sched_yield(void) { yield_calls++;return sched_yield(); }
void dm_prof_add(int id,uint64_t ns,uint64_t bytes) {
    if((id!=PROF_SLEEP && id!=PROF_MAIN_WAIT) || !ns || bytes)abort();
    prof_calls++;
}
void crash_thread_init(void) { abort(); }
void guest_thread_init(void) { abort(); }
void dm_thread_enter(uint64_t entry,uint64_t arg,uint64_t sp) { abort(); }
void *guest_sym(const char *name) { return NULL; }
long dm_self_read(void *out,const void *address,size_t size) { abort(); }
#define CHECK(v) do {if(!(v)){fprintf(stderr,"line%d: %s\n",__LINE__,#v);abort();}}while(0)
static void yield_three(void) {
    for(int64_t value=0;value>=-2;value--) {
        uint64_t regs[32]={(uint64_t)value};
        CHECK(svc_thread(0x0B,regs)==1 && regs[0]==(uint64_t)value);
    }
}
static void *worker(void *unused) {
    CHECK(svc_thread_main_init()==0x1002);
    yield_three();return NULL;
}
int main(void) {
    g_flag.bootprof=getenv("DM_BOOTPROF")!=NULL;
    CHECK(svc_thread_main_init()==0x1001);
    yield_three();
    uint64_t n,ns;dm_main_yield_stats(&n,&ns);
    CHECK(yield_calls==3);
    CHECK(clock_calls==(g_flag.bootprof?12:0));
    CHECK(prof_calls==(g_flag.bootprof?6:0));
    CHECK(n==(g_flag.bootprof?3:0) && ns==(g_flag.bootprof?3:0));
    uint64_t regs[32]={1};CHECK(svc_thread(0x0B,regs)==1); // positive sleep never yields
    CHECK(yield_calls==3 && clock_calls==(g_flag.bootprof?14:0));
    CHECK(prof_calls==(g_flag.bootprof?8:0));
    pthread_t thread;CHECK(!pthread_create(&thread,NULL,worker,NULL));CHECK(!pthread_join(thread,NULL));
    dm_main_yield_stats(&n,&ns);
    CHECK(yield_calls==6 && clock_calls==(g_flag.bootprof?20:0));
    CHECK(prof_calls==(g_flag.bootprof?11:0));
    CHECK(n==(g_flag.bootprof?3:0) && ns==(g_flag.bootprof?3:0));
    printf("{\"test\":\"SleepThread-diagnostic-policy\",\"bootprof\":%d,\"yield_calls\":%u,\"clock_calls\":%u,\"result\":\"passed\"}\n",g_flag.bootprof,yield_calls,clock_calls);
    return 0;
}
