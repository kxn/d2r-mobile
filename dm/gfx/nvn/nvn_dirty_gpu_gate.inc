// Linux-only raw futex sleep in the existing dirty fault hook. No mutex,
// allocation, command submission, Vulkan calls or spinning while GPU pending.
// Scope bucket arrays are immutable and never freed, including their gate
// words, so a waiter cannot outlive its futex address. The batch only retains
// word references; original submission/image/pool references retain storage.
#ifdef __linux__
#include <errno.h>
#include <limits.h>
#include <linux/futex.h>
#include <sys/syscall.h>
_Static_assert(ATOMIC_POINTER_LOCK_FREE==2 && ATOMIC_INT_LOCK_FREE==2,
    "signal gate state requires lock-free pointer/int atomics");
typedef struct gpu_host_lease {
    struct gpu_host_lease *global_next;
    _Atomic(struct gpu_host_lease *) tls_next;
    uintptr_t begin,end;
} gpu_host_lease;
typedef struct {range_t *range;uintptr_t page;_Atomic uint32_t *word;} gpu_gate_page;
struct nvn_dirty_gpu_gate {uint32_t id;size_t count;gpu_gate_page *pages;int submitted;};
static gpu_host_lease *gpu_host_leases;
static _Thread_local _Atomic(gpu_host_lease *) gpu_host_tls;
static _Thread_local _Atomic uint32_t gpu_arm_owner;
static pthread_cond_t gpu_host_cv=PTHREAD_COND_INITIALIZER;
#if defined(DM_DIRTY_RANGE_TEST_HOOKS) || defined(DM_VULKAN_TEST_EMBED)
static void (*gpu_wait_test_hook)(const void *,uint64_t,void *);
static void *gpu_wait_test_context;
void nvn_dirty_test_gpu_wait_hook(void (*hook)(const void *,uint64_t,void *),void *context) {
    gpu_wait_test_context=context;gpu_wait_test_hook=hook; // before thread creation / after join
}
#endif
static uint32_t gpu_next_gate; // g_slot_mu, never reuse/wrap a live generation
static int gpu_futex_wait(_Atomic uint32_t *word,uint32_t expected) {
    int saved=errno;
    long rc=syscall(SYS_futex,(uint32_t *)word,FUTEX_WAIT_PRIVATE,expected,NULL,NULL,0);
    int error=errno;errno=saved;
    return !rc || error==EAGAIN || error==EINTR;
}
static void gpu_futex_wake(_Atomic uint32_t *word) {
    int saved=errno;
    syscall(SYS_futex,(uint32_t *)word,FUTEX_WAKE_PRIVATE,INT_MAX,NULL,NULL,0);
    errno=saved;
}
static int gpu_host_owns_page(uintptr_t page) {
    for(gpu_host_lease *h=atomic_load_explicit(&gpu_host_tls,memory_order_acquire);h;
        h=atomic_load_explicit(&h->tls_next,memory_order_acquire))
        if(page<h->end && page+g_pg>h->begin)return 1;
    return 0;
}
static int gpu_gate_claim_allowed(range_t *r,uintptr_t page) {
    uint32_t gate=atomic_load_explicit(&r->gpu_gate[page],memory_order_acquire);
    if(!gate)return 1;
    // An already granted kernel-write lease may finish during ARMING. The
    // submit owner waits for its end before protecting/flushing this page.
    return (gate&1) && gate!=gpu_arm_owner &&
        gpu_host_owns_page(r->base+(page<<g_shift));
}
static int gpu_gate_before_unprotect(range_t *r,uintptr_t page,int signal_context) {
    (void)signal_context;
    for(;;) {
        uint32_t gate=atomic_load_explicit(&r->gpu_gate[page],memory_order_acquire);
        if(!gate)return 1;
        if(gate==gpu_arm_owner)return 0; // never wait for own unsubmitted work
        if((gate&1) && gpu_host_owns_page(r->base+(page<<g_shift)))return 1;
#if defined(DM_DIRTY_RANGE_TEST_HOOKS) || defined(DM_VULKAN_TEST_EMBED)
        if(gpu_wait_test_hook)gpu_wait_test_hook((void *)(r->base+(page<<g_shift)),gate,gpu_wait_test_context);
#endif
        if(!gpu_futex_wait(&r->gpu_gate[page],gate))return 0;
    }
}
int nvn_dirty_gpu_gates_enable(void) {
    pthread_mutex_lock(&g_slot_mu);
    if(!gpu_gates_enabled) {
        // Registration is cold. No in-progress FS call may have received the
        // no-hook path before this is enabled; backend does this pre-guest.
        if(atomic_load(&g_nr))die("GPU gate enable after dirty trackers exist");
        gpu_gates_enabled=1;
    }
    pthread_mutex_unlock(&g_slot_mu);return 1;
}
// Only inspect already allocated immutable buckets. A host lease itself also
// records its full range, even when no scope exists yet: a later gate cannot
// miss a kernel writer which began before the first image observation.
static _Atomic uint32_t *gpu_gate_blocker(uintptr_t a,uintptr_t end) {
    int nr=atomic_load_explicit(&g_nr,memory_order_acquire);
    for(int k=0;k<nr;k++) {
        range_t *r=&g_r[k];
        if(!r->gpu_gate || a>=r->end || end<=r->base)continue;
        uintptr_t lo=a>r->base?a:r->base,hi=end<r->end?end:r->end;
        for(uintptr_t p=(lo-r->base)>>g_shift,last=(hi-1-r->base)>>g_shift;p<=last;p++)
            if(atomic_load_explicit(&r->gpu_gate[p],memory_order_acquire))return &r->gpu_gate[p];
    }
    return NULL;
}
void *nvn_dirty_kernel_write_begin(void *mem,size_t bytes) {
    if(!gpu_gates_enabled || !bytes)return NULL;
    uintptr_t a=address(mem);if(bytes>UINTPTR_MAX-a)die("Kernel write lease overflow");
    gpu_host_lease *h=malloc(sizeof *h);if(!h)die("Kernel write lease allocation");
    *h=(gpu_host_lease){.begin=a,.end=a+bytes};
    for(;;) {
        pthread_mutex_lock(&g_slot_mu);
        _Atomic uint32_t *blocked=gpu_gate_blocker(a,a+bytes);
        if(!blocked) {
            h->global_next=gpu_host_leases;gpu_host_leases=h;
            // Initialize the immutable lease before publishing it to an interrupted
            // same-thread signal handler. Unlink before freeing below.
            atomic_store_explicit(&h->tls_next,atomic_load_explicit(&gpu_host_tls,memory_order_acquire),memory_order_relaxed);
            atomic_store_explicit(&gpu_host_tls,h,memory_order_release);
            pthread_mutex_unlock(&g_slot_mu);return h;
        }
        uint32_t gate=atomic_load_explicit(blocked,memory_order_acquire);
        pthread_mutex_unlock(&g_slot_mu);
        if(gate==gpu_arm_owner)die("Kernel write on own unsubmitted GPU gate");
        if(gate && !gpu_futex_wait(blocked,gate))die("Kernel write gate futex");
    }
}
void nvn_dirty_kernel_write_end(void *cookie) {
    if(!cookie)return;
    gpu_host_lease *h=cookie;
    pthread_mutex_lock(&g_slot_mu);
    _Atomic(gpu_host_lease *) *link=&gpu_host_tls;
    gpu_host_lease *at=atomic_load_explicit(link,memory_order_acquire);
    while(at && at!=h) {link=&at->tls_next;at=atomic_load_explicit(link,memory_order_acquire);}
    if(!at)die("Kernel write lease wrong owner or duplicate end");
    atomic_store_explicit(link,atomic_load_explicit(&h->tls_next,memory_order_acquire),memory_order_release);
    gpu_host_lease **p=&gpu_host_leases;
    while(*p && *p!=h)p=&(*p)->global_next;
    if(!*p)die("Kernel write lease global lifetime");
    *p=h->global_next;pthread_cond_broadcast(&gpu_host_cv);
    pthread_mutex_unlock(&g_slot_mu);free(h);
}
static int gpu_page_order(const void *a,const void *b) {
    uintptr_t x=(uintptr_t)((const gpu_gate_page *)a)->word,
              y=(uintptr_t)((const gpu_gate_page *)b)->word;
    return (x>y)-(x<y);
}
static int gpu_host_conflicts(const nvn_dirty_gpu_gate *g,int own) {
    gpu_host_lease *h=own?atomic_load_explicit(&gpu_host_tls,memory_order_acquire):gpu_host_leases;
    for(;h;h=own?atomic_load_explicit(&h->tls_next,memory_order_acquire):h->global_next)
        for(size_t k=0;k<g->count;k++) {
            const gpu_gate_page *p=&g->pages[k];
            uintptr_t a=p->range->base+(p->page<<g_shift);
            if(a<h->end && a+g_pg>h->begin)return 1;
        }
    return 0;
}
nvn_dirty_gpu_gate *nvn_dirty_gpu_gate_arm(const nvn_dirty_gpu_range *ranges,size_t count) {
    if(!gpu_gates_enabled || !count)return NULL;
    if(gpu_arm_owner)die("Nested unsubmitted GPU gate owner");
    nvn_dirty_gpu_gate *g=calloc(1,sizeof *g);if(!g)die("GPU gate batch allocation");
    pthread_mutex_lock(&g_slot_mu);
    size_t capacity=0;
    for(size_t n=0;n<count;n++) {
        uintptr_t a=address(ranges[n].address);size_t bytes=ranges[n].bytes;
        if(!bytes)continue;
        if(bytes>UINTPTR_MAX-a)die("GPU gate range overflow");
        uintptr_t end=a+bytes;
        int nr=atomic_load_explicit(&g_nr,memory_order_acquire);
        for(int k=0;k<nr;k++) {
            range_t *r=&g_r[k];
            if(!r->gpu_gate || !atomic_load_explicit(&r->live,memory_order_acquire) ||
                a>=r->end || end<=r->base)continue;
            uintptr_t lo=a>r->base?a:r->base,hi=end<r->end?end:r->end;
            for(uintptr_t p=(lo-r->base)>>g_shift,last=(hi-1-r->base)>>g_shift;p<=last;p++) {
                if(!atomic_load(&r->scope_refs[p]))continue;
                // Bounded functional metadata, never pin/scan unreferenced
                // guest pages or allocate metadata proportional to Pool size.
                if(g->count==capacity) {
                    size_t cap=capacity?capacity*2:64;
                    if(cap>(1u<<20))die("GPU gate page-list capacity");
                    void *fresh=realloc(g->pages,cap*sizeof *g->pages);
                    if(!fresh)die("GPU gate page-list allocation");
                    g->pages=fresh;capacity=cap;
                }
                g->pages[g->count++]=(gpu_gate_page){r,p,&r->gpu_gate[p]};
            }
        }
    }
    if(!g->count) {pthread_mutex_unlock(&g_slot_mu);free(g);return NULL;}
    qsort(g->pages,g->count,sizeof *g->pages,gpu_page_order);
    size_t unique=0;for(size_t k=0;k<g->count;k++)
        if(!unique || g->pages[k].word!=g->pages[unique-1].word)g->pages[unique++]=g->pages[k];
    g->count=unique;
    // The caller already waited real prior GPU+CPU effects on these pages.
    // Do not sleep while holding a prefix or depend on a later commit phase.
    for(size_t k=0;k<g->count;k++)
        if(atomic_load_explicit(g->pages[k].word,memory_order_acquire))die("GPU gate prior dependency not retired");
    if(gpu_host_conflicts(g,1))die("GPU gate owner retains its own kernel-write lease");
    if(gpu_next_gate>=UINT32_MAX-2)die("GPU gate generation exhausted");
    g->id=(gpu_next_gate+=2)|1;gpu_arm_owner=g->id;
    for(size_t k=0;k<g->count;k++)atomic_store_explicit(g->pages[k].word,g->id,memory_order_release);
    // Existing leased kernel writes can still unprotect during ARMING; they
    // finish first. New begin sees the gate and sleeps before taking a lease.
    while(gpu_host_conflicts(g,0))pthread_cond_wait(&gpu_host_cv,&g_slot_mu);
    for(size_t k=0;k<g->count;k++) {
        gpu_gate_page *p=&g->pages[k];
        // Unlike opportunistic texture protect, arm cannot skip BUSY. A
        // preparer which already reserved N may finish RW after that skip.
        // Drain it, then establish the final RO before any cache Flush.
        for(;;) {
            uint8_t old=PG_NONE;
            if(atomic_compare_exchange_strong(&p->range->state[p->page],&old,PG_BUSY)) {
                if(mp(p->range->base+(p->page<<g_shift),g_pg,PROT_READ))
                    die("GPU gate could not protect image page");
                atomic_store(&p->range->state[p->page],PG_PROT);break;
            }
            if(old==PG_PROT)break;
            if(old!=PG_BUSY || !busy_wait(p->range,p->page))die("GPU gate page protection owner stuck");
        }
    }
    pthread_mutex_unlock(&g_slot_mu);return g;
}
void nvn_dirty_gpu_gate_submitted(nvn_dirty_gpu_gate *g) {
    if(!g)return;
    if(g->submitted || gpu_arm_owner!=g->id)die("GPU gate submit owner/lifetime");
    for(size_t k=0;k<g->count;k++) {
        uint32_t expected=g->id;
        if(!atomic_compare_exchange_strong_explicit(g->pages[k].word,&expected,g->id&~1u,
            memory_order_release,memory_order_relaxed))die("GPU gate submit mismatch");
    }
    g->submitted=1;gpu_arm_owner=0;
}
static void gpu_gate_drop(nvn_dirty_gpu_gate *g,int completed) {
    if(!g)return;
    if(g->submitted!=completed || (!completed&&gpu_arm_owner!=g->id))die("GPU gate completion/cancel phase");
    for(size_t k=0;k<g->count;k++) {
        uint32_t expected=completed?g->id&~1u:g->id;
        if(!atomic_compare_exchange_strong_explicit(g->pages[k].word,&expected,0,
            memory_order_release,memory_order_relaxed))die("GPU gate completion mismatch");
        gpu_futex_wake(g->pages[k].word);
    }
    if(!completed)gpu_arm_owner=0;
    free(g->pages);free(g);
}
void nvn_dirty_gpu_gate_complete(nvn_dirty_gpu_gate *g) {gpu_gate_drop(g,1);}
void nvn_dirty_gpu_gate_cancel(nvn_dirty_gpu_gate *g) {gpu_gate_drop(g,0);}
#else
// This experimental cache phase is unavailable on non-Linux hosts; the
// startup policy must retain legacy rather than silently spinning in signal.
struct nvn_dirty_gpu_gate {int unused;};
static int gpu_gate_claim_allowed(range_t *r,uintptr_t p){(void)r;(void)p;return 0;}
static int gpu_gate_before_unprotect(range_t *r,uintptr_t p,int sig){(void)r;(void)p;(void)sig;return 0;}
int nvn_dirty_gpu_gates_enable(void){return 0;}
void *nvn_dirty_kernel_write_begin(void *p,size_t n){(void)p;(void)n;return NULL;}
void nvn_dirty_kernel_write_end(void *p){(void)p;}
nvn_dirty_gpu_gate *nvn_dirty_gpu_gate_arm(const nvn_dirty_gpu_range *r,size_t n){(void)r;(void)n;die("GPU gates unavailable");}
void nvn_dirty_gpu_gate_submitted(nvn_dirty_gpu_gate *g){(void)g;}
void nvn_dirty_gpu_gate_complete(nvn_dirty_gpu_gate *g){(void)g;}
void nvn_dirty_gpu_gate_cancel(nvn_dirty_gpu_gate *g){(void)g;}
#endif
