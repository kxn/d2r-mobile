// Exercise the real HLE wait-to-CV transition, handoff, timeout and cancellation
// result under controlled interleavings. No performance measurements.
#include "../dm.h"
#include "../platform.h"
#include <stdatomic.h>
#include <unistd.h>
static int observed_signal(pthread_cond_t *);
#define pthread_cond_signal observed_signal
#define DM_TEST_HOOKS 1
#include "../hle_thread.c"
#undef pthread_cond_signal

struct dm_flags g_flag;
uint64_t g_t0_ns;
void (*dm_test_cv_after_release)(void);
static unsigned signals,phases;
static thr_t *target;
static _Atomic uint32_t word;
static int signal_phase=-1,cancel_case;
static pthread_t producer;
static int producer_started;
#define CHECK(v) do { if (!(v)) { fprintf(stderr,"line%d: %s\n",__LINE__,#v);abort(); } } while (0)
static int observed_signal(pthread_cond_t *cv) {
    if (target && cv==&target->cv) signals++;
    return pthread_cond_signal(cv);
}
uint64_t now_ns(void) { return dm_platform_clock_ns(CLOCK_MONOTONIC); }
uint64_t guest_ns(void) { return now_ns(); }
int dm_detclock_enabled(void) { return 0; }
void dm_prof_add(int id,uint64_t ns,uint64_t bytes) { abort(); }
void crash_thread_init(void) { abort(); }
void guest_thread_init(void) { abort(); }
void dm_thread_enter(uint64_t e,uint64_t a,uint64_t sp) { abort(); }
#ifdef __ANDROID__
_Noreturn void dm_thread_enter_host(uint64_t e,uint64_t a,uint64_t sp,uint64_t save[3]) { abort(); }
_Noreturn void dm_thread_exit_host(const uint64_t save[3],void *a,void (*cleanup)(void *)) { abort(); }
void dm_call_host_stack(uintptr_t sp,void (*fn)(void *),void *argument) { abort(); }
#endif
void *guest_sym(const char *name) { return NULL; }
module_t *module_of(const void *address) { return NULL; }
long dm_self_read(void *out,const void *address,size_t bytes) { abort(); }

static void deliver(void) {
    if (cancel_case) wake(target,R_CANCELLED);
    else lock_handoff((uintptr_t)&word);
}
static void *after_poll(void *unused) {
    (void)unused;
    // G and polling protect the transition. Wake only after the waiter has
    // closed its polling phase and atomically released G into cond_wait.
    for (;;) {
        pthread_mutex_lock(&G);
        if (target->wkind && !target->polling) {
            deliver();pthread_mutex_unlock(&G);return NULL;
        }
        pthread_mutex_unlock(&G);sched_yield();
    }
}
static void phase(int number) {
    phases++;
    if (number!=signal_phase) return;
    if (producer_started==1) {
        CHECK(!pthread_create(&producer,NULL,after_poll,NULL));
        producer_started=2;
    } else {
        pthread_mutex_lock(&G);deliver();pthread_mutex_unlock(&G);
    }
}
static void one(unsigned handle,int64_t timeout,int which,int fallback,int cancel) {
    thr_t t={.handle=handle,.wtag=handle,.waddr=(uintptr_t)&word};
    CHECK(!pthread_cond_init(&t.cv,NULL));
    target=&t;signals=phases=0;signal_phase=which;cancel_case=cancel;
    producer_started=fallback?1:0;atomic_store(&word,0x1002|WAITERS_BIT);
    dm_test_short_wait_phase=phase;
    if (fallback==2) {
        CHECK(!pthread_create(&producer,NULL,after_poll,NULL));producer_started=2;
    }
    pthread_mutex_lock(&G);
    int64_t result=block_inner(&t,cancel?4:1,timeout);
    CHECK(!t.polling && !t.wkind && !g_wait);
    pthread_mutex_unlock(&G);
    if (producer_started==2) CHECK(!pthread_join(producer,NULL));
    if (which<0 && !fallback) CHECK(result==R_TIMEOUT && !signals);
    else {
        CHECK(result==(cancel?R_CANCELLED:0));
        CHECK(signals==(fallback?1u:0u));
        CHECK(cancel || atomic_load(&word)==handle);
    }
    if (handle!=0x1001 || timeout==0 || (timeout>0 && (uint64_t)timeout<=g_wait_budget_ns)) CHECK(!phases);
    CHECK(!pthread_cond_destroy(&t.cv));target=NULL;
}
int main(void) {
    alarm(20); // A lost wake is a test failure, never an infinite test.
    CHECK(dm_thread_wait_budget_startup(101)==-1);
    CHECK(!dm_thread_wait_budget_startup(30));
    for (unsigned i=0;i<300;i++) {
        one(0x1001,-1,0,0,0); // Handoff as polling opens.
        one(0x1001,-1,1,0,0); // Handoff after the budget expires, before G reentry.
        one(0x1001,-1,0,0,1); // Preserve cancellation result without sleeping.
    }
    for (unsigned i=0;i<100;i++) one(0x1001,-1,1,1,0); // Real CV fallback / producer race.
    one(0x1001,0,-1,0,0);
    one(0x1001,1,-1,0,0);
    one(0x1001,1000000,-1,0,0);
    one(0x1002,-1,-1,2,0); // Other threads retain sleeping behavior.
    CHECK(!dm_thread_wait_budget_startup(0));
    one(0x1001,-1,-1,2,0); // Zero budget retains the original CV path.
    CHECK(!dm_thread_wait_budget_startup(30));
    thr_t registered={0};g_all=&registered;
    CHECK(dm_thread_wait_budget_startup(0)==-1 && g_wait_budget_ns==30000);
    g_all=NULL;
    puts("HLE short waits: handoff/poll-to-CV/cancellation/timeout/secondary-thread/cold-policy passed");
    return 0;
}
