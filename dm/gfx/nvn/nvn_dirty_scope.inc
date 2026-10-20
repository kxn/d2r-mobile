// Image-only scopes use immutable 1MiB address buckets, not whole NVN Pools.
// Only referenced pages are tracked/protected. Immutable envelopes and arrays
// are never freed/reallocated/reused: a signal reader can outlive release.
// Inactive bucket metadata costs 13 bytes per page at its address high-water
// mark; MAX_RANGE bounds the total. Legacy slots retain their original reuse.
_Static_assert(__atomic_always_lock_free(sizeof(uint32_t),0),"signal page ownership must be lock-free");
enum { DIRTY_SCOPE_BUCKET = 1 << 20 };
typedef struct {uint32_t slot;uintptr_t first,end;} dirty_scope_piece;
struct nvn_dirty_scope {size_t count;dirty_scope_piece pieces[];};
static uint64_t scope_clock(range_t *r) {
    advance(&r->changes);return advance(&g_changes);
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
void nvn_dirty_scope_gpu_write_counted(const void *mem,size_t bytes,
                                     nvn_dirty_scope_publish *token) {
    uintptr_t a=address(mem);if(!bytes || bytes>UINTPTR_MAX-a) {
        if(token)token->valid=0;
        return;
    }
    uintptr_t end=a+bytes;
    if(token && (a<token->address || bytes>token->bytes ||
        a-token->address>token->bytes-bytes))token->valid=0;
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
            uint64_t completion=scope_clock(r);
            if(token && token->valid) {
                // Count only this exact marker's successful publications, never
                // infer ownership from a before/after global clock difference.
                if(!completion || completion==UINT64_MAX ||
                    generation==UINT64_MAX || generation<=token->old_stamp ||
                    token->own_count==UINT64_MAX ||
                    atomic_load(&r->changes)==UINT64_MAX)token->valid=0;
                else {
                    token->own_count++;
                    if(generation>token->own_max)token->own_max=generation;
                }
            }
        }
    }
}

void nvn_dirty_scope_gpu_write(const void *mem,size_t bytes) {
    nvn_dirty_scope_gpu_write_counted(mem,bytes,NULL);
}
// Caller retains the scope token and storage; g_slot_mu excludes additions,
// release and slot lifetime changes while inspecting exact page ownership.
// Protected state matters independently of completion clocks: a CPU writer may
// reserve generation N and pause before publishing, while our GPU mark is N+1.
static int scope_publish_protected(const nvn_dirty_scope_publish *t) {
    uintptr_t at=t->address,end=at+t->bytes;
    if(!t->scope || !t->bytes || end<at)return 0;
    for(size_t k=0;k<t->scope->count && at<end;k++) {
        dirty_scope_piece piece=t->scope->pieces[k];
        range_t *r=&g_r[piece.slot];
        uintptr_t lo=r->base+(piece.first<<g_shift),
                  hi=r->base+(piece.end<<g_shift);
        if(at<lo)return 0;
        if(at>=hi)continue;
        if(!atomic_load_explicit(&r->live,memory_order_acquire) ||
            atomic_load(&r->lifetime)==UINT64_MAX ||
            atomic_load(&r->changes)==UINT64_MAX)return 0;
        uintptr_t stop=end<hi?end:hi;
        for(uintptr_t p=(at-r->base)>>g_shift,
            last=(stop-1-r->base)>>g_shift;p<=last;p++)
            if(!atomic_load(&r->scope_refs[p]) ||
                atomic_load(&r->state[p])!=PG_PROT)return 0;
        at=stop;
    }
    return at==end;
}
int nvn_dirty_scope_publish_begin(nvn_dirty_scope *scope,const void *mem,
                                 size_t bytes,uint64_t stamp,
                                 nvn_dirty_scope_publish *token) {
    *token=(nvn_dirty_scope_publish){0};
    uintptr_t a=address(mem);
    if(!scope || !bytes || bytes>UINTPTR_MAX-a || !stamp ||
        stamp==UINT64_MAX || pthread_mutex_trylock(&g_slot_mu))return 0;
    *token=(nvn_dirty_scope_publish){.scope=scope,.address=a,.bytes=bytes,
        .before=atomic_load_explicit(&g_changes,memory_order_acquire),
        .old_stamp=stamp};
    token->valid=token->before && token->before!=UINT64_MAX &&
        scope_publish_protected(token) && !nvn_dirty_since(mem,bytes,stamp) &&
        atomic_load_explicit(&g_changes,memory_order_acquire)==token->before;
    pthread_mutex_unlock(&g_slot_mu);return token->valid;
}
int nvn_dirty_scope_publish_finish(nvn_dirty_scope_publish *token,
                                  uint64_t *own_stamp) {
    if(!token->valid || !token->own_count || !token->own_max ||
        token->own_count>=UINT64_MAX-token->before ||
        pthread_mutex_trylock(&g_slot_mu)) {token->valid=0;return 0;}
    int ok=scope_publish_protected(token) &&
        atomic_load_explicit(&g_changes,memory_order_acquire)==
            token->before+token->own_count;
    pthread_mutex_unlock(&g_slot_mu);
    token->valid=0;
    if(ok)*own_stamp=token->own_max;
    return ok;
}
