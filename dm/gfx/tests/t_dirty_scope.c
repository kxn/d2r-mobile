// Original dirty tracker + real mprotect/fault oracle, no timing or sampling.
#define DM_DIRTY_RANGE_TEST_HOOKS 1
#define mprotect scope_checked_mprotect
#include "../nvn/nvn_dirty.c"
#undef mprotect
int mprotect(void *,size_t,int);
#include <signal.h>
static int (*fault_hook)(void *,int,void *);
static void (*prep_hook)(void *,size_t);
void dm_fault_hook_register(int (*fn)(void *,int,void *)){fault_hook=fn;}
void dm_guest_write_prep_register(void (*fn)(void *,size_t)){prep_hook=fn;}
void dm_guest_write_prepare(void *p,size_t n){prep_hook(p,n);}
static unsigned checks;
#define CHECK(c) do {checks++;if(!(c))die("dirty scope check %u line%d: %s",checks,__LINE__,#c);}while(0)
static void fault(int sig,siginfo_t *info,void *ctx){if(fault_hook && fault_hook(info->si_addr,1,ctx))return;_exit(128+sig);}
static _Atomic int pause_fault,fault_entered,continue_fault,release_entered;
static uint8_t *fault_page;
int scope_checked_mprotect(void *mem,size_t size,int prot) {
    if(prot==(PROT_READ|PROT_WRITE) && atomic_exchange(&pause_fault,0)) {
        atomic_store(&fault_entered,1);while(!atomic_load(&continue_fault))sched_yield();
    }
    return mprotect(mem,size,prot);
}
static void *fault_writer(void *unused){(void)unused;fault_page[0]=0x65;return NULL;}
static void *scope_releaser(void *token){atomic_store(&release_entered,1);nvn_dirty_scope_release(token);return NULL;}
static _Atomic int publication_entered,publication_continue;
static void publication_pause(const void *p,uint64_t generation,void *context) {
    (void)p;(void)generation;(void)context;
    atomic_store(&publication_entered,1);
    while(!atomic_load(&publication_continue))sched_yield();
}
static void *prep_writer(void *p) {
    dm_guest_write_prepare(p,1);*(volatile uint8_t *)p=0x43;return NULL;
}
static void publish_ack_tests(uint8_t *m) {
    nvn_dirty_scope *scope=nvn_dirty_scope_acquire(m,8192);CHECK(scope);
    nvn_dirty_scope_publish t;uint64_t stamp,out,expected;
#define BEGIN() do {nvn_dirty_protect(m,8192);stamp=nvn_dirty_now();CHECK(nvn_dirty_scope_publish_begin(scope,m,8192,stamp,&t));out=0x1234;}while(0)
    BEGIN();
    nvn_dirty_scope_gpu_write_counted(m+17,16,&t);
    nvn_dirty_scope_gpu_write_counted(m+31,16,&t);
    nvn_dirty_scope_gpu_write_counted(m+4096,16,&t);
    CHECK(t.own_count==3);expected=t.own_max;
    CHECK(nvn_dirty_scope_publish_finish(&t,&out) && out==expected);
    CHECK(!nvn_dirty_since(m,8192,out));
    CHECK(!nvn_dirty_scope_publish_finish(&t,&out));
    // Same-value CPU store is still a real edit, before OR after own marker.
    for(int after=0;after<2;after++) {
        BEGIN();if(after)nvn_dirty_scope_gpu_write_counted(m,16,&t);
        *(volatile uint8_t *)m=*(volatile uint8_t *)m;
        if(!after)nvn_dirty_scope_gpu_write_counted(m,16,&t);
        CHECK(!nvn_dirty_scope_publish_finish(&t,&out) && out==0x1234);
        CHECK(nvn_dirty_since(m,8192,stamp));
    }
    BEGIN();nvn_dirty_scope_gpu_write_counted(m,16,&t);
    m[4096]=0x57; // another mip/page of same texture must not be swallowed
    CHECK(!nvn_dirty_scope_publish_finish(&t,&out) && out==0x1234);
    CHECK(nvn_dirty_since(m+4096,4096,stamp));
    // PG_NONE is not proof, even if no generation changed since old stamp.
    CHECK(!nvn_dirty_scope_publish_begin(scope,m,8192,nvn_dirty_now(),&t));
    BEGIN();nvn_dirty_scope_gpu_write(m+4096,4);
    nvn_dirty_scope_gpu_write_counted(m,16,&t);
    CHECK(!nvn_dirty_scope_publish_finish(&t,&out));
    // New overlapping ownership changes invalidate a previously clean token.
    BEGIN();nvn_dirty_scope *other=nvn_dirty_scope_acquire(m+1,1);CHECK(other);
    nvn_dirty_scope_gpu_write_counted(m,16,&t);
    CHECK(!nvn_dirty_scope_publish_finish(&t,&out));nvn_dirty_scope_release(other);
    BEGIN();other=nvn_dirty_scope_acquire(m+1,1);CHECK(other);nvn_dirty_scope_release(other);
    nvn_dirty_scope_gpu_write_counted(m,16,&t);CHECK(!nvn_dirty_scope_publish_finish(&t,&out));
    // Unrelated writes are conservative global noise, not absorbed as own.
    nvn_dirty_scope *noise=nvn_dirty_scope_acquire(m+8192,4096);CHECK(noise);
    nvn_dirty_protect(m+8192,4096);
    BEGIN();m[8192]=0x2a;nvn_dirty_scope_gpu_write_counted(m,16,&t);
    CHECK(!nvn_dirty_scope_publish_finish(&t,&out));nvn_dirty_scope_release(noise);
    // A source already dirty before begin cannot become clean by GPU marking.
    nvn_dirty_protect(m,8192);stamp=nvn_dirty_now();*(volatile uint8_t *)(m+3)=*(volatile uint8_t *)(m+3);
    CHECK(!nvn_dirty_scope_publish_begin(scope,m,8192,stamp,&t));
    // Cold ownership lock contention is a bounded failure, never an owner wait.
    nvn_dirty_protect(m,8192);stamp=nvn_dirty_now();
    pthread_mutex_lock(&g_slot_mu);
    CHECK(!nvn_dirty_scope_publish_begin(scope,m,8192,stamp,&t));
    pthread_mutex_unlock(&g_slot_mu);
    BEGIN();nvn_dirty_scope_gpu_write_counted(m,16,&t);
    pthread_mutex_lock(&g_slot_mu);CHECK(!nvn_dirty_scope_publish_finish(&t,&out));
    pthread_mutex_unlock(&g_slot_mu);
    // Explicit no-coverage/overflow/no-own-event failures never alter output.
    BEGIN();nvn_dirty_scope_gpu_write_counted(m+8192,1,&t);
    CHECK(!nvn_dirty_scope_publish_finish(&t,&out));
    BEGIN();CHECK(!nvn_dirty_scope_publish_finish(&t,&out));
    CHECK(!nvn_dirty_scope_publish_begin(scope,m-1,8193,nvn_dirty_now(),&t));
    CHECK(!nvn_dirty_scope_publish_begin(scope,(void *)(UINTPTR_MAX-7),16,nvn_dirty_now(),&t));
    CHECK(!nvn_dirty_scope_publish_begin(scope,m,8192,0,&t));
    // A CPU writer has reserved N but not published it. Our GPU marks N+1.
    // Only checking completion delta would incorrectly accept here.
    BEGIN();atomic_store(&publication_entered,0);atomic_store(&publication_continue,0);
    nvn_dirty_test_publication_hook(publication_pause,NULL);
    pthread_t writer;CHECK(!pthread_create(&writer,NULL,prep_writer,m));
    while(!atomic_load(&publication_entered))sched_yield();
    nvn_dirty_scope_gpu_write_counted(m,16,&t);
    CHECK(nvn_dirty_change_epoch()==t.before+t.own_count);
    CHECK(!nvn_dirty_scope_publish_finish(&t,&out) && out==0x1234);
    atomic_store(&publication_continue,1);CHECK(!pthread_join(writer,NULL));
    nvn_dirty_test_publication_hook(NULL,NULL);
    CHECK(m[0]==0x43 && nvn_dirty_since(m,8192,stamp));
    // A newer global generation reservation is never returned as own stamp.
    BEGIN();nvn_dirty_scope_gpu_write_counted(m,16,&t);expected=t.own_max;
    atomic_fetch_add(&g_now,1);
    CHECK(nvn_dirty_scope_publish_finish(&t,&out) && out==expected && out<nvn_dirty_now());
    BEGIN();nvn_dirty_scope_gpu_write_counted(m,16,&t);t.own_count=UINT64_MAX;
    CHECK(!nvn_dirty_scope_publish_finish(&t,&out));
    // Saturated publication clock is permanently unknown; restore only test clock.
    BEGIN();uint64_t epoch=atomic_exchange(&g_changes,UINT64_MAX);
    nvn_dirty_scope_gpu_write_counted(m,16,&t);CHECK(!nvn_dirty_scope_publish_finish(&t,&out));
    atomic_store(&g_changes,epoch+1);
    nvn_dirty_scope_release(scope);
#undef BEGIN
}
int main(void) {
    alarm(30);
    struct sigaction sa={.sa_sigaction=fault,.sa_flags=SA_SIGINFO};sigaction(SIGSEGV,&sa,NULL);
    size_t bytes=4*DIRTY_SCOPE_BUCKET;
    uint8_t *raw=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);CHECK(raw!=MAP_FAILED);
    uintptr_t base=((uintptr_t)raw+DIRTY_SCOPE_BUCKET-1)&~(uintptr_t)(DIRTY_SCOPE_BUCKET-1);
    uint8_t *m=(void *)(base+DIRTY_SCOPE_BUCKET-4096);
    nvn_dirty_scope *a=nvn_dirty_scope_acquire(m+7,8192-14);CHECK(a && a->count==2);
    CHECK(!nvn_dirty_tracked(m-4096,4096));CHECK(nvn_dirty_tracked(m,8192));CHECK(!nvn_dirty_tracked(m+8192,4096));
    nvn_dirty_range_proof proof={0};CHECK(!nvn_dirty_range_observe(m,8192,&proof)); // no cross-bucket single proof
    nvn_dirty_scope *b=nvn_dirty_scope_acquire(m+4096+17,4096);CHECK(b && b->count==1);
    nvn_dirty_protect(m,8192);uint64_t stamp=nvn_dirty_now();CHECK(!nvn_dirty_since(m,8192,stamp));
    m[31]=9;CHECK(nvn_dirty_since(m,8192,stamp));CHECK(!nvn_dirty_since(m+4096,4096,stamp));
    nvn_dirty_protect(m,8192);stamp=nvn_dirty_now();
    nvn_dirty_scope_gpu_write(m+4096+13,4);CHECK(nvn_dirty_since(m+4096,4096,stamp));
    CHECK(atomic_load(&find((uintptr_t)(m+4096))->state[0])==PG_PROT); // GPU marker never unprotects
    nvn_dirty_scope_release(a);CHECK(!nvn_dirty_tracked(m,4096));CHECK(nvn_dirty_tracked(m+4096,4096));
    m[3]=12; // last owner released: original mapping writable, not our fault
    nvn_dirty_protect(m+4096,4096);stamp=nvn_dirty_now();m[4096+27]=8;CHECK(nvn_dirty_since(m+4096,4096,stamp));
    nvn_dirty_protect(m+4096,4096);nvn_dirty_scope_release(b);CHECK(!nvn_dirty_tracked(m+4096,8192));
    m[4096+3]=3;m[8192+3]=4; // b's byte range spans two pages
    a=nvn_dirty_scope_acquire(m,4096);CHECK(a);CHECK(nvn_dirty_range_observe(m,4096,&proof));
    nvn_dirty_scope_release(a);CHECK(!nvn_dirty_range_refresh(&proof,&proof));
    a=nvn_dirty_scope_acquire(m,4096);CHECK(a);CHECK(!nvn_dirty_range_refresh(&proof,&proof));
    nvn_dirty_protect(m,4096);dm_guest_write_prepare(m,4096);memset(m,0x71,4096);nvn_dirty_scope_release(a);
    // A real write fault owns BUSY while another thread releases the last
    // scope. Release must not free/reassign arrays, nor hide a still-RO page.
    a=nvn_dirty_scope_acquire(m,4096);CHECK(a);nvn_dirty_protect(m,4096);fault_page=m;
    range_t *stable=find((uintptr_t)m);_Atomic uint8_t *states=stable->state;
    pthread_t writer,releaser;atomic_store(&pause_fault,1);
    CHECK(!pthread_create(&writer,NULL,fault_writer,NULL));
    while(!atomic_load(&fault_entered))sched_yield();
    CHECK(!pthread_create(&releaser,NULL,scope_releaser,a));
    while(!atomic_load(&release_entered))sched_yield();
    atomic_store(&continue_fault,1);CHECK(!pthread_join(writer,NULL));CHECK(!pthread_join(releaser,NULL));
    CHECK(stable->state==states && atomic_load(&states[((uintptr_t)m-stable->base)>>g_shift])==PG_NONE);
    CHECK(m[0]==0x65 && !nvn_dirty_tracked(m,4096));m[1]=0x66;
    publish_ack_tests(m);
    // Original legacy storage must not be silently adopted as scoped storage.
    nvn_dirty_add_range(m,4096);CHECK(!nvn_dirty_scope_acquire(m,4096));nvn_dirty_remove_range(m);
    // Exhaustion after acquiring the first existing bucket must roll back its
    // references; a partial scope cannot masquerade as a complete certificate.
    int slots=atomic_load(&g_nr);atomic_store(&g_nr,MAX_RANGE);
    CHECK(!nvn_dirty_scope_acquire((void *)(base+2*DIRTY_SCOPE_BUCKET-4096),8192));
    atomic_store(&g_nr,slots);CHECK(!nvn_dirty_tracked((void *)(base+2*DIRTY_SCOPE_BUCKET-4096),4096));
    CHECK(!nvn_dirty_scope_acquire((void *)(UINTPTR_MAX-31),64));CHECK(!nvn_dirty_scope_acquire(m,0));
    CHECK(!munmap(raw,bytes));
    printf("{\"test\":\"image-only-dirty-scope\",\"checks\":%u,\"result\":\"passed\"}\n",checks);
    return 0;
}
