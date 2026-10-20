// Owned functional Linux fault/futex + paired kernel-write oracle. No GPU,
// timings or sampling: a worker represents only the completion release phase.
#define _GNU_SOURCE 1
#define DM_DIRTY_RANGE_TEST_HOOKS 1
#define syscall gate_test_syscall
#include "../nvn/nvn_dirty.c"
#undef syscall
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/wait.h>
long syscall(long,...);
static int (*fault_hook)(void *,int,void *);
static void (*prep_hook)(void *,size_t);
void dm_fault_hook_register(int (*h)(void *,int,void *)){fault_hook=h;}
void dm_guest_write_prep_register(void (*h)(void *,size_t)){prep_hook=h;}
static _Atomic unsigned checks;
#define CHECK(v) do {checks++;if(!(v))die("GPU dirty gate check %u line%d: %s",checks,__LINE__,#v);}while(0)
static _Atomic unsigned waits,wakes,again_count,interrupt_count;
static _Atomic int wait_entered,wait_go,hold_wait,inject_interrupt;
long gate_test_syscall(long number,...) {
    va_list ap;va_start(ap,number);
    uint32_t *word=va_arg(ap,uint32_t *);int op=va_arg(ap,int);
    uint32_t value=va_arg(ap,uint32_t);void *timeout=va_arg(ap,void *);
    void *other=va_arg(ap,void *);int last=va_arg(ap,int);va_end(ap);
    if(op==FUTEX_WAIT_PRIVATE) {
        atomic_fetch_add(&waits,1);atomic_store(&wait_entered,1);
        while(atomic_load(&hold_wait) && !atomic_load(&wait_go))sched_yield();
        if(atomic_exchange(&inject_interrupt,0)){errno=EINTR;atomic_fetch_add(&interrupt_count,1);return -1;}
    }
    long result=syscall(number,word,op,value,timeout,other,last);
    if(op==FUTEX_WAIT_PRIVATE && result<0 && errno==EAGAIN)atomic_fetch_add(&again_count,1);
    if(op==FUTEX_WAKE_PRIVATE && result>0)atomic_fetch_add(&wakes,result);
    return result;
}
static void fault(int sig,siginfo_t *info,void *ctx) {
    if(fault_hook && fault_hook(info->si_addr,1,ctx))return;
    _exit(128+sig);
}
static _Atomic int writer_started,writer_done;
static void *write_user(void *p) {
    atomic_store(&writer_started,1);*(volatile uint32_t *)p=0x12985643;
    atomic_store(&writer_done,1);return NULL;
}
static void *write_prepare(void *p) {
    atomic_store(&writer_started,1);prep_hook(p,4);*(volatile uint32_t *)p=0x23896754;
    atomic_store(&writer_done,1);return NULL;
}
static void wait_flag(_Atomic int *p){while(!atomic_load(p))sched_yield();}
static nvn_dirty_gpu_gate *gate;
static void *complete(void *unused){(void)unused;nvn_dirty_gpu_gate_complete(gate);return NULL;}
static _Atomic int kernel_ready,kernel_go,kernel_done,arm_started,arm_done;
static uint8_t *kernel_bytes;
static int kernel_prepare_early;
static void *kernel_write(void *unused) {
    (void)unused;void *lease=nvn_dirty_kernel_write_begin(kernel_bytes,4096);CHECK(lease);
    if(kernel_prepare_early)prep_hook(kernel_bytes,4096);
    atomic_store(&kernel_ready,1);wait_flag(&kernel_go);
    // Arm has already published its gate. The previously granted lease must
    // still allow prepare+read to finish, rather than self-wait/EFAULT.
    if(!kernel_prepare_early)prep_hook(kernel_bytes,4096);
    int fd=open("/dev/zero",O_RDONLY);CHECK(fd>=0);
    CHECK(read(fd,kernel_bytes,4096)==4096);CHECK(!close(fd));
    nvn_dirty_kernel_write_end(lease);atomic_store(&kernel_done,1);return NULL;
}
static nvn_dirty_gpu_range arm_range;
static void *arm_worker(void *unused) {
    (void)unused;atomic_store(&arm_started,1);
    gate=nvn_dirty_gpu_gate_arm(&arm_range,1);CHECK(gate);
    atomic_store(&arm_done,1);nvn_dirty_gpu_gate_submitted(gate);return NULL;
}
static _Atomic int partial_entered,partial_go;
static void partial_publish(const void *p,uint64_t generation,void *context) {
    (void)p;(void)generation;(void)context;atomic_store(&partial_entered,1);wait_flag(&partial_go);
}
static void *old_prepare(void *p){prep_hook(p,4);*(volatile uint32_t *)p=0x67;return NULL;}
int main(void) {
    alarm(30);
    struct sigaction sa={.sa_sigaction=fault,.sa_flags=SA_SIGINFO};CHECK(!sigaction(SIGSEGV,&sa,NULL));
    CHECK(nvn_dirty_gpu_gates_enable());
    uint8_t *m=mmap(NULL,4*4096,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);CHECK(m!=MAP_FAILED);
    nvn_dirty_scope *a=nvn_dirty_scope_acquire(m,8192);CHECK(a);
    nvn_dirty_scope *alias=nvn_dirty_scope_acquire(m+4,4);CHECK(alias);
    nvn_dirty_gpu_range range={(void *)(m+16),4};
    gate=nvn_dirty_gpu_gate_arm(&range,1);CHECK(gate && gate->count==1);
    CHECK(atomic_load(&find((uintptr_t)m)->state[((uintptr_t)m-find((uintptr_t)m)->base)>>g_shift])==PG_PROT);
    nvn_dirty_gpu_gate_submitted(gate);
    // A disjoint page remains writable and acquires no futex wait.
    unsigned old_waits=atomic_load(&waits);m[4096]=0x19;CHECK(atomic_load(&waits)==old_waits);
    atomic_store(&hold_wait,1);atomic_store(&wait_go,0);atomic_store(&wait_entered,0);
    pthread_t writer,worker;CHECK(!pthread_create(&writer,NULL,write_user,m+32));wait_flag(&wait_entered);
    CHECK(!atomic_load(&writer_done));
    // Clear+wake BEFORE the blocked syscall enters the kernel: exact expected
    // value wait must return EAGAIN, not lose the completion wake.
    CHECK(!pthread_create(&worker,NULL,complete,NULL));CHECK(!pthread_join(worker,NULL));
    atomic_store(&wait_go,1);CHECK(!pthread_join(writer,NULL));
    CHECK(atomic_load(&writer_done) && atomic_load(&again_count)>0);
    CHECK(*(uint32_t *)(m+32)==0x12985643);
    // Existing prepare has the same gate. Inject EINTR once; then use the
    // actual Linux futex syscall (also captured by optional official strace).
    nvn_dirty_protect(m,8192);gate=nvn_dirty_gpu_gate_arm(&range,1);CHECK(gate);
    nvn_dirty_gpu_gate_submitted(gate);atomic_store(&hold_wait,0);
    atomic_store(&wait_entered,0);atomic_store(&inject_interrupt,1);atomic_store(&writer_done,0);
    CHECK(!pthread_create(&writer,NULL,write_prepare,m+48));wait_flag(&wait_entered);
    CHECK(!atomic_load(&writer_done));
    CHECK(!pthread_create(&worker,NULL,complete,NULL));CHECK(!pthread_join(worker,NULL));
    CHECK(!pthread_join(writer,NULL));CHECK(atomic_load(&interrupt_count)==1);
    CHECK(*(uint32_t *)(m+48)==0x23896754);
    // Cancel/failed-submit leaves consistent RO state and wakes a real fault.
    gate=nvn_dirty_gpu_gate_arm(&range,1);CHECK(gate);atomic_store(&hold_wait,1);
    atomic_store(&wait_go,0);atomic_store(&wait_entered,0);atomic_store(&writer_done,0);
    CHECK(!pthread_create(&writer,NULL,write_user,m+36));wait_flag(&wait_entered);
    nvn_dirty_gpu_gate_cancel(gate);atomic_store(&wait_go,1);
    CHECK(!pthread_join(writer,NULL));CHECK(atomic_load(&writer_done));
    // The unsubmitted owner must reject, never sleep waiting for itself.
    pid_t child=fork();CHECK(child>=0);
    if(!child) {nvn_dirty_gpu_gate *own=nvn_dirty_gpu_gate_arm(&range,1);if(!own)_exit(9);m[17]=0x44;_exit(8);}
    int status;CHECK(waitpid(child,&status,0)==child);CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==128+SIGSEGV);
    // Lease covers BOTH orderings: prepare after ARMING, and prepare already
    // returned while the kernel write has not even started yet.
    for(int early=0;early<2;early++) {
      nvn_dirty_protect(m,8192);kernel_bytes=m;arm_range=range;kernel_prepare_early=early;
      atomic_store(&kernel_ready,0);atomic_store(&kernel_go,0);atomic_store(&kernel_done,0);
      atomic_store(&arm_started,0);atomic_store(&arm_done,0);
      CHECK(!pthread_create(&writer,NULL,kernel_write,NULL));wait_flag(&kernel_ready);
      CHECK(!pthread_create(&worker,NULL,arm_worker,NULL));wait_flag(&arm_started);
      range_t *r=find((uintptr_t)m);uintptr_t page=((uintptr_t)m-r->base)>>g_shift;
      while(!atomic_load(&r->gpu_gate[page]))sched_yield();
      CHECK(!atomic_load(&arm_done));atomic_store(&kernel_go,1);
      CHECK(!pthread_join(writer,NULL));CHECK(!pthread_join(worker,NULL));CHECK(atomic_load(&kernel_done));
      for(unsigned k=0;k<4096;k++)CHECK(!m[k]);
      nvn_dirty_gpu_gate_complete(gate);
    }
    // A kernel lease predates the first image scope: its full range must be
    // retained independently of dirty metadata that does not exist yet.
    kernel_bytes=m+8192;kernel_prepare_early=0;
    atomic_store(&kernel_ready,0);atomic_store(&kernel_go,0);atomic_store(&kernel_done,0);
    CHECK(!pthread_create(&writer,NULL,kernel_write,NULL));wait_flag(&kernel_ready);
    nvn_dirty_scope *late_scope=nvn_dirty_scope_acquire(kernel_bytes,4096);CHECK(late_scope);
    arm_range=(nvn_dirty_gpu_range){kernel_bytes+16,4};
    atomic_store(&arm_started,0);atomic_store(&arm_done,0);
    CHECK(!pthread_create(&worker,NULL,arm_worker,NULL));wait_flag(&arm_started);
    range_t *late=find((uintptr_t)kernel_bytes);uintptr_t lp=((uintptr_t)kernel_bytes-late->base)>>g_shift;
    while(!atomic_load(&late->gpu_gate[lp]))sched_yield();
    CHECK(!atomic_load(&arm_done));atomic_store(&kernel_go,1);
    CHECK(!pthread_join(writer,NULL));CHECK(!pthread_join(worker,NULL));CHECK(atomic_load(&kernel_done));
    nvn_dirty_gpu_gate_complete(gate);nvn_dirty_scope_release(late_scope);
    // A new FS begin after submission is not an existing admitted lease: it
    // sleeps before prepare/kernel access and obtains no partial admission.
    arm_range=range;gate=nvn_dirty_gpu_gate_arm(&range,1);CHECK(gate);
    nvn_dirty_gpu_gate_submitted(gate);kernel_bytes=m;kernel_prepare_early=0;
    atomic_store(&kernel_ready,0);atomic_store(&kernel_go,0);atomic_store(&kernel_done,0);
    atomic_store(&hold_wait,1);atomic_store(&wait_go,0);atomic_store(&wait_entered,0);
    CHECK(!pthread_create(&writer,NULL,kernel_write,NULL));wait_flag(&wait_entered);
    CHECK(!atomic_load(&kernel_ready));nvn_dirty_gpu_gate_complete(gate);
    atomic_store(&wait_go,1);wait_flag(&kernel_ready);atomic_store(&kernel_go,1);
    CHECK(!pthread_join(writer,NULL));CHECK(atomic_load(&kernel_done));
    atomic_store(&hold_wait,0);
    // PG_BUSY/generation N is already reserved before arm. The owner waits
    // protection, and the old user writer is either before arm's final RO or
    // blocked until completion; it can never reopen RW after final protection.
    nvn_dirty_protect(m,8192);arm_range=range;
    atomic_store(&partial_entered,0);atomic_store(&partial_go,0);
    nvn_dirty_test_publication_hook(partial_publish,NULL);
    CHECK(!pthread_create(&writer,NULL,old_prepare,m+12));wait_flag(&partial_entered);
    atomic_store(&arm_started,0);atomic_store(&arm_done,0);
    CHECK(!pthread_create(&worker,NULL,arm_worker,NULL));wait_flag(&arm_started);
    range_t *pending=find((uintptr_t)m);uintptr_t pp=((uintptr_t)m-pending->base)>>g_shift;
    while(!atomic_load(&pending->gpu_gate[pp]))sched_yield();
    CHECK(!atomic_load(&arm_done));atomic_store(&partial_go,1);
    CHECK(!pthread_join(worker,NULL));nvn_dirty_gpu_gate_complete(gate);
    CHECK(!pthread_join(writer,NULL));nvn_dirty_test_publication_hook(NULL,NULL);
    CHECK(*(uint32_t *)(m+12)==0x67);
    nvn_dirty_scope_release(alias);nvn_dirty_scope_release(a);
    CHECK(!munmap(m,4*4096));
    printf("{\"test\":\"dirty-image-GPU-prewrite-gate\",\"checks\":%u,\"futex_waits\":%u,\"actual_wakes\":%u,\"EAGAIN\":%u,\"injected_EINTR\":%u,\"GPU_executed\":false,\"result\":\"passed\"}\n",checks,atomic_load(&waits),atomic_load(&wakes),atomic_load(&again_count),atomic_load(&interrupt_count));
}
