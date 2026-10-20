// Image-only scopes use immutable 1MiB address buckets, not whole NVN Pools.
// Only referenced pages are tracked/protected. Immutable envelopes and arrays
// are never freed/reallocated/reused: a signal reader can outlive release.
// Inactive bucket metadata costs 13 bytes per page at its address high-water
// mark; MAX_RANGE bounds the total. Legacy slots retain their original reuse.
_Static_assert(__atomic_always_lock_free(sizeof(uint32_t),0),"signal page ownership must be lock-free");
enum { DIRTY_SCOPE_BUCKET = 1 << 20 };
typedef struct {uint32_t slot;uintptr_t first,end;} dirty_scope_piece;
struct nvn_dirty_scope {size_t count;dirty_scope_piece pieces[];};
static void scope_clock(range_t *r) {
    advance(&r->changes);advance(&g_changes);
}
static range_t *scope_bucket(uintptr_t base) {
    int n=atomic_load(&g_nr);
    for(int k=0;k<n;k++)if(g_r[k].scope_refs && g_r[k].base==base)return &g_r[k];
    if(n==MAX_RANGE)return NULL;
    range_t *r=&g_r[n];size_t pages=DIRTY_SCOPE_BUCKET/g_pg;
    r->state=calloc(pages,sizeof *r->state);r->gen=calloc(pages,sizeof *r->gen);
    r->scope_refs=calloc(pages,sizeof *r->scope_refs);
    if(!r->state || !r->gen || !r->scope_refs)die("image dirty scope metadata allocation");
    r->base=base;r->end=base+DIRTY_SCOPE_BUCKET;r->cap=pages;
    atomic_store(&g_nr,n+1);return r;
}
static void scope_piece_release(dirty_scope_piece piece) {
    range_t *r=&g_r[piece.slot];
    for(uintptr_t p=piece.first;p<piece.end;p++) {
        uint32_t refs=atomic_load(&r->scope_refs[p]);if(!refs)die("image dirty scope ref underflow");
        if(refs==1) {
            // Restore RW before hiding the page from signal lookup. Do not
            // retain a claimed prefix while waiting for another fault owner.
            uintptr_t a=r->base+(p<<g_shift);prep_write((void *)a,g_pg);
        }
        atomic_fetch_sub_explicit(&r->scope_refs[p],1,memory_order_release);
    }
    if(!r->scope_users)die("image dirty scope users underflow");
    if(!--r->scope_users) {
        advance(&r->lifetime);atomic_store_explicit(&r->live,0,memory_order_release);
        atomic_fetch_sub(&g_live,1);
    }
    scope_clock(r);
}
nvn_dirty_scope *nvn_dirty_scope_acquire(const void *mem,size_t size) {
    pthread_once(&dirty_once,dirty_init);
    uintptr_t a=address(mem);
    if(!size || size>UINTPTR_MAX-a || a+size>UINTPTR_MAX-(g_pg-1) || g_pg>DIRTY_SCOPE_BUCKET)return NULL;
    uintptr_t first=a&~(g_pg-1),end=(a+size+g_pg-1)&~(g_pg-1);
    size_t count=((end-1)/DIRTY_SCOPE_BUCKET)-(first/DIRTY_SCOPE_BUCKET)+1;
    if(count>MAX_RANGE)return NULL;
    nvn_dirty_scope *scope=calloc(1,sizeof *scope+count*sizeof *scope->pieces);
    if(!scope)die("image dirty scope token allocation");
    pthread_mutex_lock(&g_slot_mu);
    // Never layer scopes over a legacy tracker. Its removal contract has no
    // page reference ownership and could otherwise expose a still-live image.
    int n=atomic_load(&g_nr),ok=1;
    for(int k=0;k<n;k++)if(!g_r[k].scope_refs && atomic_load(&g_r[k].live) &&
        first<g_r[k].end && end>g_r[k].base){ok=0;break;}
    for(uintptr_t at=first;ok && at<end;) {
        uintptr_t base=at&~(uintptr_t)(DIRTY_SCOPE_BUCKET-1);
        if(base>UINTPTR_MAX-DIRTY_SCOPE_BUCKET){ok=0;break;}
        range_t *r=scope_bucket(base);if(!r){ok=0;break;}
        uintptr_t stop=end<r->end?end:r->end,p=(at-base)>>g_shift,q=(stop-base)>>g_shift;
        for(uintptr_t k=p;k<q;k++)if(atomic_load(&r->scope_refs[k])==UINT32_MAX)die("image dirty scope refs exhausted");
        if(!r->scope_users) {
            advance(&r->lifetime);
            int live=atomic_fetch_add(&g_live,1)+1,peak=atomic_load(&g_live_peak);
            while(live>peak && !atomic_compare_exchange_weak(&g_live_peak,&peak,live)){}
        }
        r->scope_users++;
        for(uintptr_t k=p;k<q;k++)atomic_fetch_add_explicit(&r->scope_refs[k],1,memory_order_release);
        atomic_store_explicit(&r->live,1,memory_order_release);scope_clock(r);
        scope->pieces[scope->count++]=(dirty_scope_piece){(uint32_t)(r-g_r),p,q};at=stop;
    }
    if(!ok) {while(scope->count)scope_piece_release(scope->pieces[--scope->count]);free(scope);scope=NULL;}
    pthread_mutex_unlock(&g_slot_mu);return scope;
}
void nvn_dirty_scope_release(nvn_dirty_scope *scope) {
    if(!scope)return;
    pthread_mutex_lock(&g_slot_mu);
    while(scope->count)scope_piece_release(scope->pieces[--scope->count]);
    pthread_mutex_unlock(&g_slot_mu);free(scope);
}
void nvn_dirty_scope_gpu_write(const void *mem,size_t bytes) {
    uintptr_t a=address(mem);if(!bytes || bytes>UINTPTR_MAX-a)return;
    uintptr_t end=a+bytes;
    // Inspect only image buckets, never scan all pages of an unrelated shared
    // Buffer or read its bytes. The owner retains storage during this hook.
    int n=atomic_load_explicit(&g_nr,memory_order_acquire);
    for(int k=0;k<n;k++) {
        range_t *r=&g_r[k];
        if(!r->scope_refs || !atomic_load_explicit(&r->live,memory_order_acquire) ||
            a>=r->end || end<=r->base)continue;
        uintptr_t lo=a>r->base?a:r->base,hi=end<r->end?end:r->end;
        for(uintptr_t page=(lo-r->base)>>g_shift,last=(hi-1-r->base)>>g_shift;page<=last;page++) {
            if(!atomic_load_explicit(&r->scope_refs[page],memory_order_acquire))continue;
            uint64_t generation=atomic_fetch_add(&g_now,1)+1;
            atomic_store(&r->gen[page],generation);
            uint64_t latest=atomic_load(&r->latest);
            while(latest<generation && !atomic_compare_exchange_weak(&r->latest,&latest,generation)){}
            scope_clock(r);
        }
    }
}
