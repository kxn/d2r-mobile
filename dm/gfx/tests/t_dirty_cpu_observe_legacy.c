// Real plain dirty tracker + paired pread + signal/futex lifecycle oracle.
// No guest mappings, GPU, performance timer or sampler.
#define _GNU_SOURCE 1
#define DM_DIRTY_RANGE_TEST_HOOKS 1
#include "../nvn/nvn_dirty.c"
#include "../../host/guest_write_lease.inc"
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
static int (*fault_hook)(void *,int,void *);
static void (*prep_hook)(void *,size_t);
void dm_fault_hook_register(int (*h)(void *,int,void *)){fault_hook=h;}
void dm_guest_write_prep_register(void (*h)(void *,size_t)){prep_hook=h;}
void dm_guest_write_prepare(void *p,size_t n){if(prep_hook)prep_hook(p,n);}
static _Atomic unsigned checks;
#define CHECK(x) do{checks++;if(!(x))die("legacy CPU observation check line%d: %s",__LINE__,#x);}while(0)
static void wait_flag(_Atomic int *p){while(!atomic_load_explicit(p,memory_order_acquire))sched_yield();}
static void fault(int sig,siginfo_t *info,void *ctx){if(fault_hook && fault_hook(info->si_addr,1,ctx))return;_exit(128+sig);}
static uint8_t *memory;static size_t page;
static _Atomic int kernel_ready,kernel_go,kernel_done,observe_ready,observe_go,close_done;
static _Atomic int fault_wait,fault_relookup,fault_resume,fault_done,new_kernel_done;
static uint64_t stamp;static uint32_t snapshot;
static void *kernel_writer(void *unused) {
    (void)unused;dm_guest_write_lease lease=dm_guest_write_begin(memory+128,4);CHECK(lease.cookie);
    atomic_store(&kernel_ready,1);wait_flag(&kernel_go);
    int fd=open("/dev/zero",O_RDONLY);CHECK(fd>=0);CHECK(pread(fd,memory+128,4,0)==4);CHECK(!close(fd));
    dm_guest_write_end(&lease);atomic_store(&kernel_done,1);return NULL;
}
static void *observer(void *unused) {
    (void)unused;nvn_dirty_gpu_gate *g=nvn_dirty_cpu_observation_begin(memory,4,&stamp);CHECK(g);
    memcpy(&snapshot,memory,4);atomic_store(&observe_ready,1);wait_flag(&observe_go);
    CHECK(snapshot==*(uint32_t *)memory);nvn_dirty_cpu_observation_end(g);return NULL;
}
static void *closer(void *unused) {(void)unused;nvn_dirty_remove_range(memory);atomic_store(&close_done,1);return NULL;}
static void close_notice(unsigned phase) {
    if(phase==1)atomic_store(&fault_wait,1);
    if(phase==2){atomic_store(&fault_relookup,1);wait_flag(&fault_resume);}
}
static void *fault_writer(void *unused) {(void)unused;*(volatile uint32_t *)memory=0x55667788;atomic_store(&fault_done,1);return NULL;}
static void *new_kernel_writer(void *unused) {
    (void)unused;dm_guest_write_lease lease=dm_guest_write_begin(memory+256,4);CHECK(lease.cookie);
    int fd=open("/dev/zero",O_RDONLY);CHECK(fd>=0);CHECK(pread(fd,memory+256,4,0)==4);CHECK(!close(fd));
    dm_guest_write_end(&lease);atomic_store(&new_kernel_done,1);return NULL;
}
static _Atomic int publication_ready,publication_go;
static void delay_publication(const void *p,uint64_t gen,void *context) {
    (void)p;(void)gen;(void)context;atomic_store(&publication_ready,1);wait_flag(&publication_go);
}
static void *busy_writer(void *unused) {
    (void)unused;dm_guest_write_lease lease=dm_guest_write_begin(memory,4);CHECK(lease.cookie);
    *(uint32_t *)memory=0x10203040;dm_guest_write_end(&lease);return NULL;
}
int main(void) {
    alarm(30);struct rlimit no_core={0,0};CHECK(!setrlimit(RLIMIT_CORE,&no_core));
    struct sigaction sa={.sa_sigaction=fault,.sa_flags=SA_SIGINFO};CHECK(!sigaction(SIGSEGV,&sa,NULL));
    CHECK(nvn_dirty_cpu_observation_enable());CHECK(nvn_dirty_cpu_observation_enable());
    page=getpagesize();memory=mmap(NULL,page*3,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);CHECK(memory!=MAP_FAILED);
    *(uint32_t *)memory=0x11223344;
    // Lease may precede the first legacy registration, just as a real FS read.
    pthread_t kernel,observe,close_thread,user,new_kernel;
    CHECK(!pthread_create(&kernel,NULL,kernel_writer,NULL));wait_flag(&kernel_ready);
    nvn_dirty_add_range(memory,page);range_t *old=find((uintptr_t)memory);CHECK(old && !old->scope_refs && old->gpu_gate);
    _Atomic uint8_t *old_state=old->state;_Atomic uint64_t *old_gen=old->gen;
    uint64_t rejected=0x1234;
    CHECK(!nvn_dirty_cpu_observation_begin(memory+page-1,2,&rejected)&&rejected==0x1234);
    CHECK(!nvn_dirty_cpu_observation_begin((void *)(UINTPTR_MAX-3),8,&rejected)&&rejected==0x1234);
    CHECK(!nvn_dirty_cpu_observation_begin(memory,0,&rejected)&&rejected==0x1234);
    CHECK(!old->observer_users && !atomic_load(old->gpu_gate));
    CHECK(!pthread_create(&observe,NULL,observer,NULL));
    while(!atomic_load(old->gpu_gate))sched_yield();
    CHECK(!atomic_load(&observe_ready));
    CHECK(!pthread_create(&close_thread,NULL,closer,NULL));
    while(atomic_load(&old->closing)!=1)sched_yield();
    CHECK(!atomic_load(&close_done));
    // Closing must release the slot mutex while both arm and close await FS end.
    atomic_store(&kernel_go,1);CHECK(!pthread_join(kernel,NULL));wait_flag(&observe_ready);
    CHECK(atomic_load(&kernel_done) && snapshot==0x11223344 && !atomic_load(&close_done));
    nvn_dirty_test_plain_close_hook(close_notice);
    CHECK(!pthread_create(&user,NULL,fault_writer,NULL));wait_flag(&fault_wait);
    CHECK(!pthread_create(&new_kernel,NULL,new_kernel_writer,NULL));
    CHECK(!atomic_load(&fault_done) && !atomic_load(&new_kernel_done));
    atomic_store(&observe_go,1);CHECK(!pthread_join(observe,NULL));CHECK(!pthread_join(close_thread,NULL));
    wait_flag(&fault_relookup);CHECK(atomic_load(&old->closing)==2 && !atomic_load(&old->live));
    CHECK(old->state==old_state && old->gen==old_gen && !atomic_load(old->gpu_gate));
    CHECK(!pthread_join(new_kernel,NULL));CHECK(atomic_load(&new_kernel_done));
    // Deliberately create a new Pool at the same VA before the old waiting
    // signal resumes. It must redo lookup, not publish to the old generation.
    int highwater=atomic_load(&g_nr);nvn_dirty_add_range(memory,page);range_t *fresh=find((uintptr_t)memory);
    CHECK(fresh && fresh!=old && atomic_load(&g_nr)==highwater+1);
    nvn_dirty_protect(memory,4);uint64_t before=nvn_dirty_now();
    atomic_store(&fault_resume,1);CHECK(!pthread_join(user,NULL));nvn_dirty_test_plain_close_hook(NULL);
    CHECK(atomic_load(&fault_done) && *(uint32_t *)memory==0x55667788);
    CHECK(nvn_dirty_since(memory,4,before));CHECK(!atomic_load(old_gen));
    // A genuine PG_BUSY preparation must finish before final RO; the old lease
    // is permitted through ARMING and closing until its actual write ends.
    nvn_dirty_protect(memory,4);nvn_dirty_test_publication_hook(delay_publication,NULL);
    atomic_store(&observe_ready,0);atomic_store(&observe_go,0);atomic_store(&close_done,0);
    CHECK(!pthread_create(&kernel,NULL,busy_writer,NULL));wait_flag(&publication_ready);
    CHECK(atomic_load(fresh->state)==PG_BUSY);
    CHECK(!pthread_create(&observe,NULL,observer,NULL));while(!atomic_load(fresh->gpu_gate))sched_yield();
    CHECK(!pthread_create(&close_thread,NULL,closer,NULL));while(atomic_load(&fresh->closing)!=1)sched_yield();
    CHECK(!atomic_load(&observe_ready) && !atomic_load(&close_done));
    atomic_store(&publication_go,1);CHECK(!pthread_join(kernel,NULL));nvn_dirty_test_publication_hook(NULL,NULL);
    wait_flag(&observe_ready);CHECK(snapshot==0x10203040);atomic_store(&observe_go,1);
    CHECK(!pthread_join(observe,NULL));CHECK(!pthread_join(close_thread,NULL));CHECK(!atomic_load(&fresh->accesses));
    // Disjoint pages and ordinary proof still use the original state/gen.
    nvn_dirty_add_range(memory+page,page);nvn_dirty_range_proof proof;
    CHECK(nvn_dirty_range_observe(memory+page,4,&proof));
    uint64_t v=0;nvn_dirty_gpu_gate *g=nvn_dirty_cpu_observation_begin(memory+page,4,&v);CHECK(g);
    nvn_dirty_cpu_observation_end(g);nvn_dirty_protect(memory+page,4);before=nvn_dirty_now();
    dm_guest_write_prepare(memory+page,4);memory[page]=0x81;
    CHECK(nvn_dirty_since(memory+page,4,before));nvn_dirty_range_proof now;
    CHECK(nvn_dirty_range_refresh(&proof,&now)&&now.changes!=proof.changes);
    nvn_dirty_remove_range(memory+page);CHECK(!munmap(memory,page*3));
    // Two live legacy trackers for one page cannot grant a unique certificate.
    pid_t child=fork();CHECK(child>=0);
    if(!child) {
        uint8_t *p=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);CHECK(p!=MAP_FAILED);
        nvn_dirty_add_range(p,page*2);nvn_dirty_add_range(p+page,page);uint64_t keep=9;
        CHECK(!nvn_dirty_cpu_observation_begin(p+page,1,&keep)&&keep==9);_exit(0);
    }
    int status;CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    child=fork();CHECK(child>=0);
    if(!child) {
        size_t bytes=(MAX_RANGE+1)*page;
        uint8_t *p=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);CHECK(p!=MAP_FAILED);
        int available=MAX_RANGE-atomic_load(&g_nr);
        for(int k=0;k<available;k++)nvn_dirty_add_range(p+(size_t)k*page,page);
        CHECK(atomic_load(&g_nr)==MAX_RANGE);
        uint8_t *overflow=p+(size_t)available*page;nvn_dirty_add_range(overflow,page);
        uint64_t keep=7;CHECK(!nvn_dirty_tracked(overflow,1));
        CHECK(!nvn_dirty_cpu_observation_begin(overflow,1,&keep)&&keep==7);
        CHECK(nvn_dirty_since(overflow,1,nvn_dirty_now()));_exit(0);
    }
    CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    child=fork();CHECK(child>=0);
    if(!child) {
        uint8_t *p=mmap(NULL,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);CHECK(p!=MAP_FAILED);
        plain_observer_metadata_used=PLAIN_OBSERVER_METADATA_LIMIT;
        int old_slots=atomic_load(&g_nr);nvn_dirty_add_range(p,page);
        CHECK(atomic_load(&g_nr)==old_slots && !nvn_dirty_tracked(p,1));
        uint64_t keep=3;CHECK(!nvn_dirty_cpu_observation_begin(p,1,&keep)&&keep==3);
        CHECK(nvn_dirty_since(p,1,nvn_dirty_now()));_exit(0);
    }
    CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    printf("{\"test\":\"plain-dirty-CPU-observer-lifetime\",\"checks\":%u,\"old_FS_before_range\":true,\"Finalize_ARMING_no_cycle\":true,\"signal_relooks_same_VA_new_Pool\":true,\"PG_BUSY_kernel_write_completed\":true,\"original_state_gen_proof\":true,\"overlap_hole_overflow_rejected\":true,\"GPU_executed\":false,\"result\":\"passed\"}\n",checks);
}
