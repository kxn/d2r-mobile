// Native hardware-only functional test, using the original HLE thread lifecycle.
#include "host_stack.h"
#include <sys/mman.h>
#include <sched.h>
extern int native_host_stack_abi(void (*fn)(void *),void *argument);
typedef struct {
    uintptr_t low,high,host18,guest_low,guest_high;
    _Atomic unsigned *ready;
    unsigned calls,nested,signals;
    int direct;
} host_call_state;
static _Thread_local host_call_state *host_signal_state;
static void host_call_signal(int sig) {
    (void)sig;host_call_state *s=host_signal_state;
    uintptr_t sp;__asm__ volatile("mov %0,sp":"=r"(sp));
    stack_t alt;if(!s || sigaltstack(NULL,&alt))_Exit(70);
    if(sp<(uintptr_t)alt.ss_sp || sp>=(uintptr_t)alt.ss_sp+alt.ss_size)_Exit(71);
    s->signals++;
}
__attribute__((noinline)) static void host_call_nested(void *argument) {
    host_call_state *s=argument;volatile uint8_t scratch[4096];
    uintptr_t sp;__asm__ volatile("mov %0,sp":"=r"(sp));
    if(sp<s->low || sp>=s->high || (sp&15))die("host call nested stack");
    for(unsigned i=0;i<sizeof scratch;i+=128)scratch[i]=(uint8_t)(i/128);
    for(unsigned i=0;i<sizeof scratch;i+=128)if(scratch[i]!=(uint8_t)(i/128))die("host call nested canary");
    s->nested++;
}
__attribute__((noinline)) static void host_call_large(void *argument) {
    host_call_state *s=argument;volatile uint8_t scratch[96*1024];
    uintptr_t sp,host18;__asm__ volatile("mov %0,sp\nmov %1,x18":"=r"(sp),"=r"(host18));
    if(sp<s->low || sp>=s->high || (sp&15) || host18!=s->host18 ||
       (sp>=s->guest_low && sp<s->guest_high))die("host call native stack/x18");
    for(unsigned i=0;i<sizeof scratch;i+=256)scratch[i]=(uint8_t)(i/256);
    if(!dm_thread_host_call(host_call_nested,s))die("host call nested rejected");
    if(!s->calls){host_signal_state=s;if(raise(SIGUSR2))die("host call raise");host_signal_state=NULL;}
    for(unsigned i=0;i<sizeof scratch;i+=256)if(scratch[i]!=(uint8_t)(i/256))die("host call parked frame overwritten");
    s->calls++;
}
static void host_call_get_bounds(host_call_state *s) {
    pthread_attr_t attr;void *base;size_t size;
    if(pthread_getattr_np(pthread_self(),&attr) || pthread_attr_getstack(&attr,&base,&size) || pthread_attr_destroy(&attr))die("host call pthread bounds");
    s->low=(uintptr_t)base;s->high=s->low+size;
    __asm__ volatile("mov %0,x18":"=r"(s->host18));
}
static _Noreturn void host_call_guest(uint64_t argument) {
    host_call_state *s=(void *)(uintptr_t)argument;volatile uint8_t scratch[1024];
    for(unsigned i=0;i<sizeof scratch;i++)scratch[i]=(uint8_t)i;
    host_call_get_bounds(s);atomic_fetch_add(s->ready,1);
    while(atomic_load(s->ready)<2)sched_yield();
    for(unsigned i=0;i<32;i++) {
        uintptr_t before,after;__asm__ volatile("mov %0,sp":"=r"(before));
        if(!native_host_stack_abi(host_call_large,s))die("host call callee saved registers");
        __asm__ volatile("mov %0,sp":"=r"(after));
        if(before!=after || before<s->guest_low || before>=s->guest_high)die("host call guest SP");
    }
    for(unsigned i=0;i<sizeof scratch;i++)if(scratch[i]!=(uint8_t)i)die("host call guest frame canary");
    uint64_t regs[32]={0};svc_thread(0x0a,regs);die("host call ExitThread returned");
}
static void *host_call_unregistered(void *argument) {
    host_call_state *s=argument;
    if(dm_thread_host_call(host_call_large,s))die("host call unregistered should fail closed");
    return NULL;
}
static void native_host_stack_test(void) {
    struct sigaction old,action={.sa_handler=host_call_signal,.sa_flags=SA_ONSTACK};sigemptyset(&action.sa_mask);
    if(sigaction(SIGUSR2,&action,&old))die("host call signal install");
    host_call_state direct={.direct=1};host_call_get_bounds(&direct);
    if(!native_host_stack_abi(host_call_large,&direct) || direct.calls!=1 || direct.nested!=1 || direct.signals!=1)die("host call main direct");
    pthread_t unknown;if(pthread_create(&unknown,NULL,host_call_unregistered,&direct) || pthread_join(unknown,NULL))die("host call unregistered fixture");
    const size_t page=(size_t)sysconf(_SC_PAGESIZE),bytes=16384;
    if(!page || bytes%page)die("host call page size");
    uint8_t *maps[2];uint32_t handles[2];host_call_state states[2]={0};_Atomic unsigned ready=0;
    for(unsigned n=0;n<2;n++) {
        maps[n]=mmap(NULL,bytes+2*page,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(maps[n]==MAP_FAILED || mprotect(maps[n]+page,bytes,PROT_READ|PROT_WRITE))die("host call guarded guest stack");
        memset(maps[n]+page,0xa5,512);
        states[n].guest_low=(uintptr_t)maps[n]+page;states[n].guest_high=states[n].guest_low+bytes;states[n].ready=&ready;
        uint64_t regs[32]={0};regs[1]=(uintptr_t)host_call_guest;regs[2]=(uintptr_t)&states[n];regs[3]=states[n].guest_high;regs[4]=44;regs[5]=UINT64_MAX;
        if(!svc_thread(8,regs)||regs[0])die("host call CreateThread");
        handles[n]=(uint32_t)regs[1];
        regs[0]=handles[n];if(!svc_thread(9,regs)||regs[0])die("host call StartThread");
    }
    for(unsigned n=0;n<2;n++) {
        uint64_t regs[32]={0};regs[1]=(uintptr_t)&handles[n];regs[2]=1;regs[3]=UINT64_C(5000000000);
        if(!svc_thread(0x18,regs)||regs[0]||regs[1])die("host call WaitSynchronization");
        if(states[n].calls!=32 || states[n].nested!=32 || states[n].signals!=1)die("host call counts/isolation");
        for(unsigned j=0;j<512;j++)if(maps[n][page+j]!=0xa5)die("host call guest guard canary");
        regs[0]=handles[n];if(!svc_thread(0x16,regs)||regs[0])die("host call CloseHandle");
        if(munmap(maps[n],bytes+2*page))die("host call munmap");
    }
    if(sigaction(SIGUSR2,&old,NULL))die("host call signal restore");
    LOG("[native host stack fixture] main-direct + two original HLE16KiB guarded guest threads, 64 nested96KiB callbacks, x18/x19-x29/d8-d15/SP, canaries, altstack signals passed");
}
