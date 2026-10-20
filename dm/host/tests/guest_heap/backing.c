// Owned VM semantics only: no guest code, driver mapping or GPU emulator.
#define _GNU_SOURCE
#include "dm.h"
#include <stdatomic.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
static int reject_shared_map, reject_protect;
static void *test_mmap(void *p,size_t n,int prot,int flags,int fd,off_t off) {
    if (reject_shared_map && (flags & MAP_SHARED)) { errno=ENOMEM; return MAP_FAILED; }
    return mmap(p,n,prot,flags,fd,off);
}
static int test_mprotect(void *p,size_t n,int prot) {
    if (reject_protect) {errno=ENOMEM;return -1;}
    return mprotect(p,n,prot);
}
#define mmap test_mmap
#define mprotect test_mprotect
#include "../../hle_svc.c"
#include "../../guest_heap_startup.h"
#undef mmap
#undef mprotect
// Only unrelated platform services are stubbed; memory SVC bodies are real.
module_t g_mod[MAX_MOD];int g_nmod;struct dm_flags g_flag;
int svc_thread(int num,uint64_t *r) {(void)num;(void)r;return 0;}
uint64_t svc_host_call(int num,uint64_t *r) {(void)num;(void)r;return 0;}
uint64_t guest_ns(void) {return 0;}
uint64_t dm_entropy64(void) {return 0;}
void hlecount_svc(int num) {(void)num;}
void dm_backtrace(uint64_t fp) {(void)fp;}
module_t *module_of(const void *p) {(void)p;return NULL;}
#define CHECK(x) do {if(!(x)){fprintf(stderr,"CHECK line %d %s\n",__LINE__,#x);abort();}checks++;} while(0)
static unsigned checks;
static void wait_ok(pid_t p) {int s;CHECK(waitpid(p,&s,0)==p);CHECK(WIFEXITED(s)&&WEXITSTATUS(s)==0);}
static void expect_signal(void (*fn)(void),int sig) {
    pid_t p=fork();CHECK(p>=0);if(!p){fn();_exit(0);}int s;CHECK(waitpid(p,&s,0)==p);CHECK(WIFSIGNALED(s)&&WTERMSIG(s)==sig);
}
static void unread_committed_tail(void) {volatile uint8_t v=g_heap[8192];(void)v;}
static void unread_shrunk(void) {volatile uint8_t v=g_heap[4096];(void)v;}
static void write_readonly_guest(void) {g_heap[0]=0x73;}
static void failed_reserve(void) {CHECK(dm_guest_heap_backing_select(1));reject_shared_map=1;mem_init();}
static void failed_shrink(void) {reject_protect=1;guest_heap_resize(4096);}
static void *initialize(void *unused) {
    (void)unused;uint64_t r[32]={0};r[1]=I_HeapAddr;svc_dispatch(0x29,r,0);
    if(r[0] || r[1]!=(uintptr_t)g_heap)abort();
    return NULL;
}
static void set_heap(size_t size,uint64_t result) {
    uint64_t r[32]={0};r[1]=size;svc_dispatch(0x01,r,0);CHECK(r[0]==result);
    if(!result)CHECK(r[1]==(uintptr_t)g_heap);
}
static void run_mode(int shared) {
    CHECK(!dm_guest_shared_heap_contains(0,1));
    CHECK(!dm_guest_shared_heap_import_pointer(0,1));
    CHECK(!dm_guest_heap_readable(0,1));
    uint8_t *as;size_t as_size;svc_guest_range(&as,&as_size);CHECK(!as && as_size==RESERVE);
    CHECK(!dm_guest_heap_backing_select(2));
    CHECK(dm_guest_heap_backing_select(shared));
    CHECK(dm_guest_heap_backing_select(shared));
    CHECK(!dm_guest_heap_backing_select(!shared));
    // Concurrent first SVC initialization publishes exactly one whole AS.
    pthread_t threads[8];
    for(int k=0;k<8;k++)CHECK(!pthread_create(&threads[k],NULL,initialize,NULL));
    for(int k=0;k<8;k++)CHECK(!pthread_join(threads[k],NULL));
    CHECK(g_heap==g_as+GUEST_HEAP_OFFSET);CHECK(g_heap_cap==(4ull<<30));
    svc_guest_range(&as,&as_size);CHECK(as==g_as && as_size==RESERVE);
    CHECK(!dm_guest_heap_backing_select(shared));
    CHECK(!dm_guest_shared_heap_contains((uintptr_t)g_heap,1));
    set_heap(8192,0);
    g_heap[0]=0x21;g_heap[4096]=0x32;
    uint64_t info[8]={0},r[32]={0};r[0]=(uintptr_t)info;r[2]=(uintptr_t)g_heap;
    svc_dispatch(0x06,r,0);CHECK(r[0]==0);CHECK(info[0]==(uintptr_t)g_heap);CHECK(info[1]==8192);
    CHECK(((uint32_t *)info)[4]==5 && ((uint32_t *)info)[6]==3);
    CHECK(dm_guest_heap_readable((uintptr_t)g_heap,8192));
    CHECK(dm_guest_shared_heap_contains((uintptr_t)g_heap,8192)==shared);
    CHECK(dm_guest_shared_heap_contains((uintptr_t)g_heap+8191,1)==shared);
    CHECK(!dm_guest_shared_heap_contains((uintptr_t)g_heap,0));
    CHECK(!dm_guest_shared_heap_contains((uintptr_t)g_heap-1,1));
    CHECK(!dm_guest_shared_heap_contains((uintptr_t)g_heap+8191,2));
    CHECK(!dm_guest_shared_heap_contains(UINTPTR_MAX-3,16));
    CHECK(!dm_guest_shared_heap_contains((uintptr_t)g_alias,1));
    CHECK(!dm_guest_shared_heap_contains((uintptr_t)g_stack,1));
    uint8_t *import=dm_guest_shared_heap_import_pointer((uintptr_t)g_heap,8192);
    CHECK((import!=NULL)==shared);
    if(shared) {
        CHECK(import!=g_heap);CHECK(!(((uintptr_t)import^(uintptr_t)g_heap)&((1u<<20)-1)));
        CHECK(import[0]==0x21 && import[4096]==0x32);
        CHECK(!mprotect(g_heap,8192,PROT_READ));
        import[0]=0x56;CHECK(g_heap[0]==0x56); // same backing while guest remains RO
        expect_signal(write_readonly_guest,SIGSEGV);
        CHECK(!mprotect(g_heap,8192,PROT_READ|PROT_WRITE));
        g_heap[0]=0x21;CHECK(import[0]==0x21);
        CHECK(!dm_guest_shared_heap_contains((uintptr_t)import,1));
        CHECK(!dm_guest_shared_heap_import_pointer((uintptr_t)g_heap+8191,2));
    }
    CHECK(!mprotect(g_alias,4096,PROT_READ|PROT_WRITE));
    CHECK(!mprotect(g_stack,4096,PROT_READ|PROT_WRITE));
    g_alias[0]=0x43;g_stack[0]=0x54;
    pid_t child=fork();CHECK(child>=0);
    if(!child){g_heap[0]=0x65;g_alias[0]=0x76;g_stack[0]=0x87;_exit(0);}wait_ok(child);
    CHECK(g_heap[0]==(shared?0x65:0x21));CHECK(g_alias[0]==0x43);CHECK(g_stack[0]==0x54);
    // No page outside the committed heap is made usable by shared provenance.
    expect_signal(unread_committed_tail,SIGSEGV);
    reject_protect=1;set_heap(12288,0xD401);reject_protect=0;
    CHECK(g_heap_cur==8192);CHECK(!dm_guest_shared_heap_contains((uintptr_t)g_heap+8192,1));
    CHECK(g_heap[4096]==0x32);set_heap(g_heap_cap+4096,0xD401);CHECK(g_heap_cur==8192);
    set_heap((size_t)app_pool()+4096,0x10801);CHECK(g_heap_cur==8192);
    if(shared)expect_signal(failed_shrink,SIGABRT);
    set_heap(4096,0);CHECK(g_heap_cur==4096);expect_signal(unread_shrunk,SIGSEGV);
    CHECK(!dm_guest_shared_heap_contains((uintptr_t)g_heap+4096,1));
    set_heap(8192,0);CHECK(g_heap[4096]==0x32);
    set_heap(0,0);CHECK(!dm_guest_shared_heap_contains((uintptr_t)g_heap,1));
    CHECK(!munmap(g_as,RESERVE));
    printf("{\"test\":\"owned-guest-heap-backing\",\"shared\":%d,\"checks\":%u,\"gpu_or_guest_executed\":false}\n",shared,checks);fflush(stdout);
}
static int probe_calls;
static int unavailable(void) {probe_calls++;return 0;}
static int available(void) {probe_calls++;return 1;}
static void startup_case(int requested,int supported,int expected) {
    int (*probe)(void)=supported?available:unavailable;
    CHECK(dm_guest_heap_startup_freeze(2,probe)==-1);CHECK(probe_calls==0);
    CHECK(dm_guest_heap_startup_freeze(requested,probe)==expected);
    CHECK(probe_calls==requested);mem_init();set_heap(4096,0);
    CHECK(dm_guest_shared_heap_contains((uintptr_t)g_heap,4096)==expected);
    CHECK(!dm_guest_heap_backing_select(!expected));CHECK(!munmap(g_as,RESERVE));
    printf("{\"test\":\"cold-heap-mode-selection\",\"requested\":%d,\"supported\":%d,\"actual_shared\":%d,\"checks\":%u}\n",requested,supported,expected,checks);fflush(stdout);
}
static void startup_probe_views(void) {
    CHECK(dm_guest_heap_shared_mode()==-1);CHECK(!g_heap_import_alias);
    dm_guest_heap_probe_views views;
    CHECK(!dm_guest_heap_probe_views_create(0,4096,&views));
    CHECK(!views.guest && !views.import_alias);
    reject_shared_map=1;CHECK(!dm_guest_heap_probe_views_create(8192,4096,&views));reject_shared_map=0;
    CHECK(!views.guest && !views.import_alias);CHECK(dm_guest_heap_shared_mode()==-1);
    CHECK(dm_guest_heap_probe_views_create(8192,4096,&views));
    CHECK(views.guest!=views.import_alias);CHECK(views.bytes==8192);
    CHECK(!(((uintptr_t)views.guest^(uintptr_t)views.import_alias)&((1u<<20)-1)));
    CHECK(!mprotect(views.guest,views.bytes,PROT_READ|PROT_WRITE));
    memset(views.guest,0x21,views.bytes);CHECK(!memcmp(views.guest,views.import_alias,views.bytes));
    CHECK(!mprotect(views.guest,views.bytes,PROT_READ));
    ((uint8_t *)views.import_alias)[4096]=0x74;CHECK(((uint8_t *)views.guest)[4096]==0x74);
    CHECK(dm_guest_heap_shared_mode()==-1);CHECK(!g_heap_import_alias);
    dm_guest_heap_probe_views_destroy(&views);CHECK(!views.guest && !views.import_alias && !views.bytes);
    CHECK(dm_guest_heap_shared_mode()==-1);CHECK(!g_heap_import_alias);
}
int main(void) {
    startup_probe_views();
    // Every mode is a cold process: production has no reset/reselect API.
    for(int shared=0;shared<2;shared++){pid_t p=fork();CHECK(p>=0);if(!p){run_mode(shared);_exit(0);}wait_ok(p);}
    for(int requested=0;requested<2;requested++)for(int supported=0;supported<2;supported++) {
        pid_t p=fork();CHECK(p>=0);if(!p){startup_case(requested,supported,requested&&supported);_exit(0);}wait_ok(p);
    }
    expect_signal(failed_reserve,SIGABRT);
    printf("{\"test\":\"owned-guest-heap-parent\",\"checks\":%u,\"result\":\"passed\"}\n",checks);
}
