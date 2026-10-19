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

#ifdef DM_DIRTY_RANGE_TEST_HOOKS
static int transition_armed,transition_entered,transition_continue,transition_kind;
static void transition_pause(const void *addr,int release,void *context) {
    (void)addr;(void)context;
    pthread_mutex_lock(&gate);
    if(transition_armed && release==transition_kind) {
        transition_armed=0;transition_entered=1;pthread_cond_broadcast(&changed);
        while(!transition_continue)pthread_cond_wait(&changed,&gate);
    }
    pthread_mutex_unlock(&gate);
}
typedef struct {void *addr;size_t len;nvn_dirty_owned_exclusion *token;int release,result;} transition_op;
static void *transition_thread(void *context) {
    transition_op *op=context;
    op->result=op->release?nvn_dirty_owned_release(op->token):nvn_dirty_owned_exclude(op->addr,op->len,op->token);return NULL;
}
static void transition_arm(int release) {
    pthread_mutex_lock(&gate);transition_armed=1;transition_kind=release;
    transition_entered=transition_continue=0;pthread_mutex_unlock(&gate);
    nvn_dirty_test_owned_transition(transition_pause,NULL);
}
static void transition_wait(void) {
    pthread_mutex_lock(&gate);while(!transition_entered)pthread_cond_wait(&changed,&gate);pthread_mutex_unlock(&gate);
}
static void transition_resume(pthread_t thread) {
    pthread_mutex_lock(&gate);transition_continue=1;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&gate);
    CHECK(!pthread_join(thread,NULL));nvn_dirty_test_owned_transition(NULL,NULL);
}
#endif
typedef struct {uint8_t *address;uint64_t generation;int entered,ready;} scoped_publication;
static void scoped_publication_pause(const void *addr,uint64_t generation,void *context) {
    scoped_publication *p=context;if(addr!=p->address)return;
    pthread_mutex_lock(&gate);p->generation=generation;p->entered=1;pthread_cond_broadcast(&changed);
    while(!p->ready)pthread_cond_wait(&changed,&gate);pthread_mutex_unlock(&gate);
}
static void *scoped_publication_writer(void *context) {
    scoped_publication *p=context;dm_guest_write_prepare(p->address,g_pg);p->address[0]=0xa6;return NULL;
}
typedef struct {
    void *address;size_t size;nvn_dirty_scope_proof proof;
    _Atomic int ready,go;unsigned attempts,unexpected;
} scoped_reuse;
static void *scoped_reuse_reader(void *context) {
    scoped_reuse *r=context;atomic_store(&r->ready,1);
    while(!atomic_load(&r->go))sched_yield();
    for(unsigned i=0;i<10000;i++) {
        nvn_dirty_scope_proof out;uint64_t epoch;
        r->attempts++;r->unexpected+=nvn_dirty_scope_refresh(r->address,r->size,&r->proof,&out,&epoch)!=0;
    }
    return NULL;
}
static void scoped_test(void) {
    size_t pg=(size_t)getpagesize();uint8_t *p=mmap(NULL,6*pg,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(p!=MAP_FAILED);memset(p,0,6*pg);nvn_dirty_add_range(p,4*pg);nvn_dirty_add_range(p+4*pg,2*pg);
    nvn_dirty_scope_proof local,overlap,other,current;uint64_t epoch,epoch2;
    CHECK(sizeof(nvn_dirty_scope_proof)==48);CHECK(sizeof(range_t)==104);
    CHECK(nvn_dirty_scope_observe(p+2*pg,pg,&local,&epoch));CHECK(epoch);
    CHECK(nvn_dirty_scope_observe(p,pg,&overlap,&epoch2));
    CHECK(nvn_dirty_scope_observe(p+4*pg,pg,&other,&epoch2));
    CHECK(!nvn_dirty_scope_observe(p+4*pg-1,2,&current,&epoch2));
    CHECK(!nvn_dirty_scope_observe(p+6*pg,1,&current,&epoch2));
    CHECK(!nvn_dirty_scope_observe((void *)(UINTPTR_MAX-4),8,&current,&epoch2));
    CHECK(!nvn_dirty_scope_observe(p,0,&current,&epoch2));
    CHECK(!nvn_dirty_scope_refresh(p+2*pg,pg-1,&local,&current,&epoch2));
    CHECK(!nvn_dirty_scope_refresh(p+pg,pg,&local,&current,&epoch2));
    CHECK(!nvn_dirty_scope_refresh(p+2*pg,pg,NULL,&current,&epoch2));
    CHECK(!nvn_dirty_scope_refresh(p+2*pg,pg,&local,NULL,&epoch2));
    CHECK(!nvn_dirty_scope_refresh(p+2*pg,pg,&local,&current,NULL));
    nvn_dirty_scope_proof bad=local;bad.slot=MAX_RANGE;
    CHECK(!nvn_dirty_scope_refresh(p+2*pg,pg,&bad,&current,&epoch2));CHECK(epoch2==0);
    bad=local;bad.reserved=1;CHECK(!nvn_dirty_scope_refresh(p+2*pg,pg,&bad,&current,&epoch2));
    bad=local;bad.lifetime=0;CHECK(!nvn_dirty_scope_refresh(p+2*pg,pg,&bad,&current,&epoch2));
    bad=local;bad.exclusion_version=1;CHECK(!nvn_dirty_scope_refresh(p+2*pg,pg,&bad,&current,&epoch2));
    bad=local;bad.exclusion_version=UINT64_MAX-1;CHECK(!nvn_dirty_scope_refresh(p+2*pg,pg,&bad,&current,&epoch2));
    nvn_dirty_test_lock_slots(1);
    CHECK(nvn_dirty_scope_refresh(p+2*pg,pg,&local,&current,&epoch2));
    CHECK(!nvn_dirty_scope_observe(p+2*pg,pg,&current,&epoch2));nvn_dirty_test_lock_slots(0);
    nvn_dirty_owned_exclusion token={0};
#ifdef DM_DIRTY_RANGE_TEST_HOOKS
    transition_op begin={p,pg,&token,0,0};pthread_t worker;transition_arm(0);
    CHECK(!pthread_create(&worker,NULL,transition_thread,&begin));transition_wait();
    // The real begin is paused with version odd and slot mutex held. Cached
    // disjoint refresh fails immediately; it never takes the mutex or waits.
    CHECK(!nvn_dirty_scope_refresh(p+2*pg,pg,&local,&current,&epoch2));
    CHECK(!nvn_dirty_scope_refresh(p,pg,&overlap,&current,&epoch2));
    CHECK(nvn_dirty_scope_refresh(p+4*pg,pg,&other,&current,&epoch2));
    CHECK(!nvn_dirty_scope_observe(p+2*pg,pg,&current,&epoch2));
    transition_resume(worker);CHECK(begin.result);
#else
    CHECK(nvn_dirty_owned_exclude(p,pg,&token));
#endif
    CHECK(!nvn_dirty_change_epoch());
    CHECK(nvn_dirty_scope_refresh(p+2*pg,pg,&local,&current,&epoch2));
    CHECK(epoch2!=epoch && current.changes!=local.changes && current.exclusion_version!=local.exclusion_version);
    local=current;epoch=epoch2;
    CHECK(!nvn_dirty_scope_refresh(p,pg,&overlap,&current,&epoch2));
    CHECK(!nvn_dirty_scope_observe(p+pg-1,2,&current,&epoch2)); // page intersection
    CHECK(nvn_dirty_scope_refresh(p+4*pg,pg,&other,&current,&epoch2));CHECK(current.changes==other.changes);
    CHECK(!nvn_dirty_since(p+2*pg,pg,UINT64_MAX));CHECK(nvn_dirty_since(p,pg,UINT64_MAX));
    size_t bytes=0;nvn_dirty_visit(p,3*pg,UINT64_MAX,visit_count,&bytes);CHECK(bytes==pg);
    p[0]=0x91;dm_guest_write_prepare(p,pg);p[1]=0xa6;
    CHECK(nvn_dirty_scope_refresh(p+2*pg,pg,&local,&current,&epoch2));
    CHECK(epoch2==epoch && current.changes==local.changes); // no false loss outside
    uint64_t stamp=nvn_dirty_now();nvn_dirty_protect(p+2*pg,pg);p[2*pg]=0x73; // actual fault
    CHECK(nvn_dirty_since(p+2*pg,pg,stamp));
    CHECK(nvn_dirty_scope_refresh(p+2*pg,pg,&local,&current,&epoch2));
    CHECK(current.changes!=local.changes && epoch2!=epoch);local=current;
    CHECK(nvn_dirty_scope_refresh(p+4*pg,pg,&other,&current,&epoch2));CHECK(current.changes==other.changes);
    bytes=0;nvn_dirty_visit(p+pg,2*pg,stamp,visit_count,&bytes);CHECK(bytes==pg);
#ifdef DM_DIRTY_RANGE_TEST_HOOKS
    transition_op release={p,pg,&token,1,0};transition_arm(1);
    CHECK(!pthread_create(&worker,NULL,transition_thread,&release));transition_wait();
    CHECK(!nvn_dirty_scope_refresh(p+2*pg,pg,&local,&current,&epoch2));
    CHECK(nvn_dirty_scope_refresh(p+4*pg,pg,&other,&current,&epoch2));
    transition_resume(worker);CHECK(release.result);
#else
    CHECK(nvn_dirty_owned_release(&token));
#endif
    CHECK(nvn_dirty_scope_refresh(p+2*pg,pg,&local,&current,&epoch2));
    CHECK(current.changes!=local.changes && current.exclusion_version!=local.exclusion_version);
    CHECK(nvn_dirty_scope_refresh(p,pg,&overlap,&current,&epoch2));CHECK(current.changes!=overlap.changes);
    CHECK(nvn_dirty_since(p,pg,stamp));
    CHECK(nvn_dirty_scope_observe(p+2*pg,pg,&local,&epoch));
    // PG_BUSY second page forces actual prefix rollback after begin changes.
    pthread_mutex_lock(&gate);blocked_page=p+pg;arm=1;entered=resume_protector=0;pthread_mutex_unlock(&gate);
    pthread_t protector;CHECK(!pthread_create(&protector,NULL,protect_thread,p+pg));
    pthread_mutex_lock(&gate);while(!entered)pthread_cond_wait(&changed,&gate);pthread_mutex_unlock(&gate);
    CHECK(!nvn_dirty_owned_exclude(p,2*pg,&token));CHECK(!token.identity);
    CHECK(nvn_dirty_scope_refresh(p+2*pg,pg,&local,&current,&epoch2));
    CHECK(current.changes!=local.changes && current.exclusion_version!=local.exclusion_version && epoch2!=epoch);
    CHECK(nvn_dirty_since(p,pg,stamp));CHECK(atomic_load(&find((uintptr_t)p)->state[0])==PG_NONE);
    pthread_mutex_lock(&gate);resume_protector=1;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&gate);
    CHECK(!pthread_join(protector,NULL));dm_guest_write_prepare(p+pg,pg);p[pg]=0x37;
    // Original out-of-order publication window: reserve N for page2, let
    // page3 publish N+1, then publish page2's N. Never use a proof/global epoch
    // as an upload generation; the original pre-reservation stamp must remain.
    nvn_dirty_protect(p+2*pg,2*pg);stamp=nvn_dirty_now();
    scoped_publication publication={.address=p+2*pg};
    nvn_dirty_test_publication_hook(scoped_publication_pause,&publication);
    pthread_t publisher;CHECK(!pthread_create(&publisher,NULL,scoped_publication_writer,&publication));
    pthread_mutex_lock(&gate);while(!publication.entered)pthread_cond_wait(&changed,&gate);pthread_mutex_unlock(&gate);
    dm_guest_write_prepare(p+3*pg,pg);p[3*pg]=0x62;CHECK(nvn_dirty_now()>publication.generation);
    CHECK(nvn_dirty_scope_observe(p+2*pg,pg,&local,&epoch));
    CHECK(!nvn_dirty_since(p+2*pg,pg,stamp)); // N is reserved, not published
    pthread_mutex_lock(&gate);publication.ready=1;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&gate);
    CHECK(!pthread_join(publisher,NULL));nvn_dirty_test_publication_hook(NULL,NULL);
    CHECK(nvn_dirty_scope_refresh(p+2*pg,pg,&local,&current,&epoch2));
    CHECK(current.changes!=local.changes && epoch2!=epoch);
    CHECK(nvn_dirty_since(p+2*pg,pg,stamp) && p[2*pg]==0xa6);
    // Slot reuse never lets a cached lifetime prove the replacement clean.
    CHECK(nvn_dirty_scope_observe(p+2*pg,pg,&local,&epoch));
    nvn_dirty_remove_range(p);CHECK(!nvn_dirty_scope_refresh(p+2*pg,pg,&local,&current,&epoch2));
    nvn_dirty_add_range(p,4*pg);CHECK(!nvn_dirty_scope_refresh(p+2*pg,pg,&local,&current,&epoch2));
    CHECK(nvn_dirty_scope_observe(p+2*pg,pg,&current,&epoch2));
    CHECK(current.slot==local.slot && current.lifetime!=local.lifetime);
    // A real cached reader races repeated slot destruction/rebuild. Its
    // original lifetime has already been removed before the start gate; no
    // successful refresh is permitted even when the same slot/address recur.
    scoped_reuse reuse={.address=p+2*pg,.size=pg,.proof=local};
    pthread_t reuse_reader;CHECK(!pthread_create(&reuse_reader,NULL,scoped_reuse_reader,&reuse));
    while(!atomic_load(&reuse.ready))sched_yield();atomic_store(&reuse.go,1);
    for(unsigned i=0;i<100;i++){nvn_dirty_remove_range(p);nvn_dirty_add_range(p,4*pg);}
    CHECK(!pthread_join(reuse_reader,NULL));CHECK(reuse.attempts==10000 && !reuse.unexpected);
    CHECK(nvn_dirty_scope_observe(p+2*pg,pg,&current,&epoch2));
    local=current;
    // Saturation/transition sentinels and ordinary publication saturation are
    // injected only into owned test allocations at a quiescent boundary.
    range_t *r=find((uintptr_t)p);
    atomic_store(&r->owned_version,UINT64_MAX-5);
    CHECK(nvn_dirty_owned_exclude(p,pg,&token));CHECK(nvn_dirty_owned_release(&token));
    CHECK(atomic_load(&r->owned_version)==UINT64_MAX-1);
    CHECK(!nvn_dirty_owned_exclude(p,pg,&token)); // reserve both begin AND release versions
    atomic_store(&r->owned_version,UINT64_MAX);
    CHECK(!nvn_dirty_scope_refresh(p+2*pg,pg,&local,&current,&epoch2));
    CHECK(nvn_dirty_since(p+2*pg,pg,UINT64_MAX));
    CHECK(!nvn_dirty_owned_exclude(p,pg,&token));
    atomic_store(&r->owned_version,UINT64_MAX-1);CHECK(!nvn_dirty_owned_exclude(p,pg,&token));
    atomic_store(&r->owned_version,0);nvn_dirty_test_poison_range(p,0);
    CHECK(!nvn_dirty_scope_refresh(p+2*pg,pg,&local,&current,&epoch2));
    uint64_t saved=nvn_dirty_test_global_epoch(UINT64_MAX);
    CHECK(!nvn_dirty_scope_refresh(p+4*pg,pg,&other,&current,&epoch2));nvn_dirty_test_global_epoch(saved+1);
    nvn_dirty_remove_range(p);nvn_dirty_remove_range(p+4*pg);CHECK(!munmap(p,6*pg));
}

#ifdef DM_VULKAN_BORROWED_NVN_FIXTURE
static void borrowed_lease_test(void) {
    size_t pg=(size_t)getpagesize();uint8_t *p=mmap(NULL,3*pg,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(p!=MAP_FAILED);memset(p,0,3*pg);
    nvn_dirty_pool_lease lease={0},bad;nvn_dirty_owned_exclusion token={0};
    CHECK(!nvn_dirty_pool_lease_observe(p,2*pg,&lease));
    nvn_dirty_add_range(p,2*pg);nvn_dirty_add_range(p+2*pg,pg);
    CHECK(nvn_dirty_pool_lease_observe(p,2*pg,&lease));CHECK(lease.lifetime);
    CHECK(!nvn_dirty_pool_lease_observe(p,pg,&bad)); // not the full registered lease
    CHECK(!nvn_dirty_pool_lease_observe(p+1,pg,&bad));
    CHECK(!nvn_dirty_pool_lease_observe((void *)(UINTPTR_MAX-pg+1),2*pg,&bad));
    bad=lease;bad.lifetime++;CHECK(!nvn_dirty_pool_lease_exclude(&bad,&token));
    bad=lease;bad.slot++;CHECK(!nvn_dirty_pool_lease_exclude(&bad,&token));
    bad=lease;bad.base+=pg;bad.size-=pg;CHECK(!nvn_dirty_pool_lease_exclude(&bad,&token));
    bad=lease;bad.reserved=1;CHECK(!nvn_dirty_pool_lease_exclude(&bad,&token));CHECK(!token.identity);
    // Same real PG_BUSY protector interleaving through the BORROWED wrapper.
    pthread_mutex_lock(&gate);blocked_page=p+pg;arm=1;entered=resume_protector=0;pthread_mutex_unlock(&gate);
    pthread_t worker;CHECK(!pthread_create(&worker,NULL,protect_thread,p+pg));
    pthread_mutex_lock(&gate);while(!entered)pthread_cond_wait(&changed,&gate);pthread_mutex_unlock(&gate);
    CHECK(!nvn_dirty_pool_lease_exclude(&lease,&token));CHECK(!token.identity);
    range_t *r=find((uintptr_t)p);CHECK(atomic_load(&r->state[0])==PG_NONE && atomic_load(&r->state[1])==PG_BUSY);
    CHECK(!atomic_load(&r->owned_identity) && nvn_dirty_change_epoch());
    pthread_mutex_lock(&gate);resume_protector=1;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&gate);
    CHECK(!pthread_join(worker,NULL));CHECK(atomic_load(&r->state[1])==PG_PROT);
    CHECK(!nvn_dirty_pool_lease_exclude(&lease,&token)); // no unprotect for import
    dm_guest_write_prepare(p+pg,pg);p[pg]=17; // original legal write, not a retry path in NVN
    CHECK(nvn_dirty_pool_lease_exclude(&lease,&token));
    nvn_dirty_scope_proof proof,after;uint64_t epoch;
    CHECK(nvn_dirty_scope_observe(p+2*pg,pg,&proof,&epoch));
    nvn_dirty_protect(p,2*pg);p[0]=29;CHECK(atomic_load(&r->state[0])==PG_OWNED);
    CHECK(nvn_dirty_scope_refresh(p+2*pg,pg,&proof,&after,&epoch));
    CHECK(!nvn_dirty_scope_observe(p,pg,&proof,&epoch));
    CHECK(nvn_dirty_owned_release(&token));
    nvn_dirty_pool_lease stale=lease;nvn_dirty_remove_range(p);nvn_dirty_add_range(p,2*pg);
    CHECK(!nvn_dirty_pool_lease_exclude(&stale,&token));CHECK(!token.identity);
    CHECK(nvn_dirty_pool_lease_observe(p,2*pg,&lease));CHECK(lease.lifetime!=stale.lifetime);
    CHECK(nvn_dirty_pool_lease_exclude(&lease,&token));CHECK(nvn_dirty_owned_release(&token));
    nvn_dirty_remove_range(p);nvn_dirty_remove_range(p+2*pg);CHECK(!munmap(p,3*pg));
}
#endif

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
    CHECK(!nvn_dirty_since(p+3*pg,pg,UINT64_MAX));
    size_t bytes=0;nvn_dirty_visit(p,2*pg,UINT64_MAX,visit_count,&bytes);CHECK(bytes==pg);
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
#ifdef DM_VULKAN_BORROWED_NVN_FIXTURE
    borrowed_lease_test(); // before scoped_test intentionally exhausts reusable-slot clocks
#endif
    scoped_test();
    printf("owned dirty exclusion: %u checks passed; no GPU/guest path enabled\n",checks);return 0;
#endif
}
