// Original dirty tracker + real mprotect/fault oracle, no timing or sampling.
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
