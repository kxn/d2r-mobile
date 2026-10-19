// Owned anonymous pages only. Deterministic protection interleavings, not a
// guest-address probe, GPU timer, or replacement for the Vulkan byte oracle.
#define mprotect owned_test_mprotect
#include "../nvn/nvn_dirty.c"
#undef mprotect
int mprotect(void *,size_t,int);
#include <sys/wait.h>
#include <sys/resource.h>
#include <signal.h>

module_t g_mod[MAX_MOD]; int g_nmod;
module_t *module_of(const void *p) {(void)p;return NULL;}
#ifndef __APPLE__
int crash_fault_dispatch(void *,int,void *);
// Same Linux owned-Vulkan fixture adapter; invoke the original hook registry.
// Production Android's ART/ESR signal-chain handling is not tested here.
static void owned_signal(int sig,siginfo_t *info,void *context) {
    if(crash_fault_dispatch(info->si_addr,info->si_code==SEGV_ACCERR,context))return;
    _exit(128+sig);
}
static void owned_install(void) {
    struct sigaction action={.sa_sigaction=owned_signal,.sa_flags=SA_SIGINFO};
    sigemptyset(&action.sa_mask);sigaction(SIGSEGV,&action,NULL);
}
#endif
static unsigned checks;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"owned exclusion check failed %s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static void *blocked_page;
static int arm,entered,resume_protector;
int owned_test_mprotect(void *p,size_t n,int prot) {
    if(prot==PROT_READ && p==blocked_page) {
        pthread_mutex_lock(&gate);
        if(arm) {
            arm=0;entered=1;pthread_cond_broadcast(&changed);
            while(!resume_protector)pthread_cond_wait(&changed,&gate);
        }
        pthread_mutex_unlock(&gate);
    }
    return mprotect(p,n,prot);
}
static void *protect_thread(void *p) {nvn_dirty_protect(p,g_pg);return NULL;}
static void visit_count(const void *p,size_t n,void *v) {(void)p;*(size_t *)v+=n;}
static void expect_abort(int remove_range,const void *p) {
    fflush(NULL);pid_t child=fork();CHECK(child>=0);
    if(!child) {
        struct rlimit none={0,0};setrlimit(RLIMIT_CORE,&none);
        if(remove_range)nvn_dirty_remove_range(p);else nvn_dirty_add_range(p,g_pg);
        _exit(99);
    }
    int status;CHECK(waitpid(child,&status,0)==child);
    CHECK(WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
}
int main(void) {
#ifdef __APPLE__
    puts("owned exclusion unsupported on Metal; no production behavior changed");return 0;
#else
    owned_install();size_t pg=(size_t)getpagesize();
    uint8_t *p=mmap(NULL,4*pg,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(p!=MAP_FAILED);memset(p,0,4*pg);nvn_dirty_add_range(p,4*pg);
    nvn_dirty_owned_exclusion token={0},other={0};
    CHECK(!nvn_dirty_owned_exclude(p+1,pg,&token));CHECK(!token.identity);
    CHECK(!nvn_dirty_owned_exclude(p,pg-1,&token));
    CHECK(!nvn_dirty_owned_exclude(p,0,&token));
    CHECK(!nvn_dirty_owned_exclude((void *)(UINTPTR_MAX-pg+1),2*pg,&token));
    CHECK(!nvn_dirty_owned_exclude(p,32u<<20,&token));
    CHECK(!nvn_dirty_owned_exclude(p+4*pg,pg,&token));
    CHECK(!nvn_dirty_owned_exclude(p,pg,NULL));
    nvn_dirty_range_proof before,after;CHECK(nvn_dirty_range_observe(p,pg,&before));
    uint64_t stamp=nvn_dirty_now(),epoch=nvn_dirty_change_epoch();CHECK(epoch);
    CHECK(nvn_dirty_owned_exclude(p,pg,&token));CHECK(token.identity);
    CHECK(nvn_dirty_change_epoch()==0);
    CHECK(!nvn_dirty_range_refresh(&before,&after));
    CHECK(!nvn_dirty_range_observe(p,pg,&after));
    CHECK(!nvn_dirty_range_observe(p+3*pg,pg,&after)); // conservative range-wide
    CHECK(nvn_dirty_since(p,pg,UINT64_MAX));
    CHECK(nvn_dirty_since(p+3*pg,pg,UINT64_MAX));
    size_t bytes=0;nvn_dirty_visit(p,2*pg,UINT64_MAX,visit_count,&bytes);CHECK(bytes==2*pg);
    CHECK(!nvn_dirty_owned_exclude(p,pg,&other));CHECK(!other.identity);
    CHECK(!nvn_dirty_owned_exclude(p+2*pg,pg,&other)); // one token/range
    uint64_t faults,preps,faults2,preps2;nvn_dirty_stats(&faults,&preps);
    nvn_dirty_protect(p,2*pg);CHECK(atomic_load(&find((uintptr_t)p)->state[0])==PG_OWNED);
    ((volatile uint8_t *)p)[0]=0xa6; // must not SIGSEGV/fault-unprotect owned page
    dm_guest_write_prepare(p,pg);p[1]=0x91;
    nvn_dirty_stats(&faults2,&preps2);CHECK(faults2==faults&&preps2==preps);
    p[pg]=0x73;nvn_dirty_stats(&faults2,&preps2);CHECK(faults2==faults+1);
    CHECK(p[0]==0xa6&&p[1]==0x91&&p[pg]==0x73);
    expect_abort(0,p);expect_abort(1,p); // token never silently lost on lifecycle edit
    nvn_dirty_owned_exclusion stale=token,mutated=token;mutated.size=2*pg;
    CHECK(!nvn_dirty_owned_release(&mutated));CHECK(nvn_dirty_change_epoch()==0);
    CHECK(nvn_dirty_owned_release(&token));CHECK(!token.identity);
    CHECK(!nvn_dirty_owned_release(&token));CHECK(!nvn_dirty_owned_release(&stale));
    CHECK(nvn_dirty_change_epoch()!=0&&nvn_dirty_change_epoch()!=epoch);
    CHECK(nvn_dirty_since(p,pg,stamp));CHECK(nvn_dirty_range_observe(p,pg,&after));
    CHECK(after.changes!=before.changes);
    nvn_dirty_protect(p,pg);stamp=nvn_dirty_now();p[0]=0x55;CHECK(nvn_dirty_since(p,pg,stamp));
    // A protected page is refused, never made writable for a later import.
    nvn_dirty_protect(p+2*pg,pg);CHECK(!nvn_dirty_owned_exclude(p+2*pg,pg,&other));
    CHECK(!other.identity);CHECK(atomic_load(&find((uintptr_t)p)->state[2])==PG_PROT);
    dm_guest_write_prepare(p+2*pg,pg);p[2*pg]=3;
    // Ambiguous already-registered alias refuses reservation without mutation.
    fflush(NULL);pid_t alias_child=fork();CHECK(alias_child>=0);
    if(!alias_child) {
        nvn_dirty_add_range(p+3*pg,pg);
        _exit(nvn_dirty_owned_exclude(p+3*pg,pg,&other) || other.identity || atomic_load(&g_owned_active) ? 1 : 0);
    }
    int alias_status;CHECK(waitpid(alias_child,&alias_status,0)==alias_child);
    CHECK(WIFEXITED(alias_status)&&WEXITSTATUS(alias_status)==0);
    // Real PG_BUSY overlap: protect owns page1 and is paused before its
    // syscall. Begin claims page0 then must roll that prefix back on page1.
    pthread_mutex_lock(&gate);blocked_page=p+pg;arm=1;entered=resume_protector=0;pthread_mutex_unlock(&gate);
    pthread_t worker;CHECK(!pthread_create(&worker,NULL,protect_thread,p+pg));
    pthread_mutex_lock(&gate);while(!entered)pthread_cond_wait(&changed,&gate);pthread_mutex_unlock(&gate);
    CHECK(!nvn_dirty_owned_exclude(p,2*pg,&other));CHECK(!other.identity);
    range_t *r=find((uintptr_t)p);CHECK(atomic_load(&r->state[0])==PG_NONE);
    CHECK(atomic_load(&r->state[1])==PG_BUSY);CHECK(!atomic_load(&r->owned_identity));
    CHECK(!atomic_load(&g_owned_active));p[0]=0x62;
    pthread_mutex_lock(&gate);resume_protector=1;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&gate);
    CHECK(!pthread_join(worker,NULL));CHECK(atomic_load(&r->state[1])==PG_PROT);
    dm_guest_write_prepare(p+pg,pg);p[pg]=0x63;
    CHECK(nvn_dirty_owned_exclude(p,2*pg,&token));CHECK(token.identity!=stale.identity);
    CHECK(nvn_dirty_owned_release(&token));CHECK(!nvn_dirty_owned_release(&stale));
    for(size_t n=0;n<4;n++)CHECK(atomic_load(&r->state[n])==PG_NONE);
    nvn_dirty_remove_range(p);CHECK(!munmap(p,4*pg));
    printf("owned dirty exclusion: %u checks passed; no GPU/guest path enabled\n",checks);return 0;
#endif
}
