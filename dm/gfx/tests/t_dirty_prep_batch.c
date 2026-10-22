// Functional syscall-boundary and ownership oracle; no timer or sampler.
// Compile the unchanged base with DIRTY_PREP_REFERENCE to check its byte/state
// semantics too; its intentionally larger syscall count is reported separately.
#define mprotect checked_mprotect
#ifndef DM_DIRTY_SOURCE
#define DM_DIRTY_SOURCE "../nvn/nvn_dirty.c"
#endif
#include DM_DIRTY_SOURCE
#undef mprotect
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>

int mprotect(void *, size_t, int);
static int (*fault_hook)(void *,int,void *);
static void (*prepare_hook)(void *,size_t);
void dm_fault_hook_register(int (*fn)(void *,int,void *)) { fault_hook=fn; }
void dm_guest_write_prep_register(void (*fn)(void *,size_t)) { prepare_hook=fn; }
void dm_guest_write_prepare(void *p,size_t n) { prepare_hook(p,n); }
#define CHECK(v) do { if(!(v)) { fprintf(stderr,"failed line %d: %s\n",__LINE__,#v);abort(); } } while(0)
static _Atomic unsigned rw_calls,rw_pages,max_pages;
static int fail_write_permission;
static uint8_t *data;
static pthread_mutex_t gate_mu=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_cv=PTHREAD_COND_INITIALIZER;
static int gate_armed,gate_entered,first_prefix;

int checked_mprotect(void *addr,size_t n,int prot) {
    if(prot==(PROT_READ|PROT_WRITE)) {
        range_t *r=find((uintptr_t)addr);
        CHECK(r && n && !((uintptr_t)addr&(g_pg-1)) && !(n&(g_pg-1)));
        CHECK(n<=r->end-(uintptr_t)addr); // never cross a range or guard
        for(uintptr_t p=((uintptr_t)addr-r->base)>>g_shift,
            end=p+(n>>g_shift);p<end;p++) CHECK(atomic_load(&r->state[p])==PG_BUSY);
        unsigned pages=n>>g_shift,old=atomic_load(&max_pages);
        while(old<pages && !atomic_compare_exchange_weak(&max_pages,&old,pages)) {}
        atomic_fetch_add(&rw_calls,1);atomic_fetch_add(&rw_pages,pages);
        pthread_mutex_lock(&gate_mu);
        if(gate_armed && addr==data+64*g_pg) {
            gate_armed=0;gate_entered=1;pthread_cond_broadcast(&gate_cv);
            while(!first_prefix)pthread_cond_wait(&gate_cv,&gate_mu);
        }
        if(gate_entered && addr==data) {first_prefix=1;pthread_cond_broadcast(&gate_cv);}
        pthread_mutex_unlock(&gate_mu);
        if(fail_write_permission)return -1;
    }
    return mprotect(addr,n,prot);
}
static void fault(int sig,siginfo_t *info,void *ctx) {
    if(fault_hook && fault_hook(info->si_addr,1,ctx))return;
    _exit(128+sig);
}
static void reset_counts(void) {
    atomic_store(&rw_calls,0);atomic_store(&rw_pages,0);atomic_store(&max_pages,0);
}
static void kernel_write_all(unsigned pages) {
    int fd=open("/dev/zero",O_RDONLY);CHECK(fd>=0);
    CHECK(read(fd,data,pages*g_pg)==(ssize_t)(pages*g_pg));CHECK(!close(fd));
    for(unsigned p=0;p<pages;p++)CHECK(data[p*g_pg]==0);
}
static void *prepare_later(void *unused) {
    (void)unused;dm_guest_write_prepare(data+64*g_pg,64*g_pg);return NULL;
}
#ifndef DIRTY_PREP_REFERENCE
static void failed_permission(int sig) {
    (void)sig;
    range_t *r=find((uintptr_t)data);
    // A failed batch must not publish NONE/generations and pretend success.
    for(unsigned p=0;p<64;p++)if(atomic_load(&r->state[p])!=PG_BUSY)_exit(90);
    _exit(88);
}
#endif
int main(void) {
    alarm(30); // functional deadlock watchdog, not a measurement
    struct sigaction sa={.sa_sigaction=fault,.sa_flags=SA_SIGINFO};
    sigemptyset(&sa.sa_mask);CHECK(!sigaction(SIGSEGV,&sa,NULL));
    const unsigned pages=257;
    size_t page=getpagesize(),bytes=pages*page;
    uint8_t *mapping=mmap(NULL,bytes+2*page,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
    CHECK(mapping!=MAP_FAILED);data=mapping+page;
    CHECK(!mprotect(data,bytes,PROT_READ|PROT_WRITE));memset(data,0x6d,bytes);
    nvn_dirty_add_range(data,bytes);nvn_dirty_protect(data,bytes);
    uint64_t stamp=nvn_dirty_now(),faults0,preps0,faults1,preps1;
    nvn_dirty_stats(&faults0,&preps0);reset_counts();
    dm_guest_write_prepare(data+17,bytes-34);
    unsigned contiguous_calls=atomic_load(&rw_calls);
    CHECK(atomic_load(&rw_pages)==pages);
#ifdef DIRTY_PREP_REFERENCE
    CHECK(contiguous_calls==pages);
#else
    CHECK(contiguous_calls==5 && atomic_load(&max_pages)==64);
#endif
    CHECK(nvn_dirty_now()==stamp+pages);kernel_write_all(pages);
    nvn_dirty_stats(&faults1,&preps1);CHECK(faults1==faults0 && preps1-preps0==pages);
    for(unsigned p=0;p<pages;p++)CHECK(nvn_dirty_since(data+p*page,page,stamp));

    // Existing writable holes retain their exact generation and are never
    // included in a claimed run. Real fault and host-preparation paths mix.
    nvn_dirty_protect(data,bytes);
    for(unsigned p=0;p<pages;p+=3)((volatile uint8_t *)data)[p*page]=0x37;
    uint64_t generations[pages];range_t *r=find((uintptr_t)data);
    for(unsigned p=0;p<pages;p++)generations[p]=atomic_load(&r->gen[p]);
    nvn_dirty_stats(&faults0,&preps0);reset_counts();dm_guest_write_prepare(data,bytes);
    CHECK(atomic_load(&rw_pages)==pages-(pages+2)/3);
    for(unsigned p=0;p<pages;p++)CHECK(p%3?atomic_load(&r->gen[p])>generations[p]:atomic_load(&r->gen[p])==generations[p]);
    kernel_write_all(pages);nvn_dirty_stats(&faults1,&preps1);CHECK(faults1==faults0);
    CHECK(preps1-preps0==pages-(pages+2)/3);

    // Two adjacent tracked ranges are separate syscall ownership domains.
    nvn_dirty_remove_range(data);nvn_dirty_add_range(data,128*page);
    nvn_dirty_add_range(data+128*page,(pages-128)*page);
    nvn_dirty_protect(data,128*page);nvn_dirty_protect(data+128*page,(pages-128)*page);
    reset_counts();dm_guest_write_prepare(data,bytes);CHECK(atomic_load(&rw_pages)==pages);
#ifndef DIRTY_PREP_REFERENCE
    CHECK(atomic_load(&rw_calls)==5);
#endif
    kernel_write_all(pages);nvn_dirty_remove_range(data);nvn_dirty_remove_range(data+128*page);

    // B holds the next run BUSY until A has flushed its earlier prefix. If A
    // waits on B while retaining its prefix, the watchdog fails this test.
    nvn_dirty_add_range(data,bytes);nvn_dirty_protect(data,bytes);
    pthread_mutex_lock(&gate_mu);gate_armed=1;pthread_mutex_unlock(&gate_mu);
    pthread_t b;CHECK(!pthread_create(&b,NULL,prepare_later,NULL));
    pthread_mutex_lock(&gate_mu);while(!gate_entered)pthread_cond_wait(&gate_cv,&gate_mu);pthread_mutex_unlock(&gate_mu);
    dm_guest_write_prepare(data,bytes);CHECK(!pthread_join(b,NULL));kernel_write_all(pages);
    CHECK(!atomic_load(&g_busy_red));
#ifndef DIRTY_PREP_REFERENCE
    nvn_dirty_protect(data,bytes);
    pid_t child=fork();CHECK(child>=0);
    if(!child) {
        signal(SIGABRT,failed_permission);fail_write_permission=1;
        dm_guest_write_prepare(data,bytes);_exit(91);
    }
    int status;CHECK(waitpid(child,&status,0)==child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status)==88);
#endif
    nvn_dirty_remove_range(data);CHECK(!munmap(mapping,bytes+2*page));
    uint64_t mp_calls,mp_pages,mp_ns;
    nvn_dirty_mp_stats(&mp_calls,&mp_pages,&mp_ns);
    if(getenv("DM_BOOTPROF")) CHECK(mp_calls>0 && mp_pages>=mp_calls);
    else CHECK(mp_calls==0 && mp_pages==0 && mp_ns==0);
    printf("{\"test\":\"dirty-prep-batch\",\"contiguous_pages\":%u,\"RW_syscalls\":%u,\"result\":\"passed\"}\n",pages,contiguous_calls);
    return 0;
}
