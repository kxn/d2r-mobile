// 客体内存写保护脏页跟踪：预排布贴图（CPU 写、GPU 读）的池页上传后设只读，游戏一写即缺页 → 标脏、解保护；
// 使用前查覆盖页的写代号，比上传时新就重传。NVN 语义：贴图描述符指向池内存，GPU 每次读的是当前内容
// （实测游戏会把流式素材写进仍被采样的贴图存储，不重新登记）。
// 信号处理约束：缺页路径只用原子操作与 mprotect，不分配、不加锁。
// Write-protect dirty-page tracking for guest memory: pool pages of packaged textures (CPU writes, GPU reads) are set read-only after upload, and as soon as the game writes it faults -> mark dirty, unprotect;
// before use, check the write generation of the covered pages and re-upload if newer than at upload time. NVN semantics: a texture descriptor points to pool memory, and the GPU reads the current content every time
// (measured: the game writes streamed assets into texture storage that is still being sampled, without re-registering).
// Signal handler constraint: the page-fault path uses only atomic operations and mprotect, no allocation, no locks.
#include "nvn_dirty.h"
#include "../../host/dm.h"
#include <stdatomic.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#include "nvn_platform.h"
#include <sched.h>

// 页状态与 mprotect 的次序（改保护的一方先把状态改成 BUSY，mprotect 落地后才改成终态；别的线程见 BUSY 就等）：
//   设只读：NONE → BUSY → mprotect(只读) → PROT；解保护：PROT → BUSY → mprotect(可写) → NONE。
// 不变式：状态是 NONE 的页一定可写、是 PROT 的页一定只读。原先设只读是「先记 PROT、攒一段再 mprotect」，这段时间里另一线程的写前解保护
// 会把状态改回 NONE，随后攒批的 mprotect 落地 ⇒ 页只读而状态 NONE：游戏线程写它时缺页处理只回「重执行」永远空转；
// 宿主用系统调用往里读文件时 prep_write 看状态 NONE 跳过，系统调用不走缺页处理，直接 EFAULT、数据读不全。
// BUSY 最长持续 = 一次 mprotect 的时间（设只读时再加上给这一段逐页改状态的几次原子写）。等待有上限 BUSY_WAIT_MS，超限报红：
// 缺页处理里超限按「不是我们的页」返回，交给崩溃处理如实报崩，不静默空转。等待只读原子量、不拿任何锁。
// Order of page state and mprotect (the side changing protection first sets the state to BUSY and sets the final state only after mprotect has landed; other threads wait when they see BUSY):
//   set read-only: NONE -> BUSY -> mprotect(read-only) -> PROT; unprotect: PROT -> BUSY -> mprotect(writable) -> NONE.
// Invariant: a page whose state is NONE is always writable, and one whose state is PROT is always read-only. Setting read-only used to be "record PROT first, batch up a run, then mprotect"; during that time another thread's unprotect-before-write
// could set the state back to NONE, and then the batched mprotect landed => the page read-only with state NONE: when the game thread wrote to it the page-fault handler only returned "re-execute" and spun forever;
// when the host read a file into it with a system call, prep_write saw state NONE and skipped, the system call did not go through the page-fault handler and failed with EFAULT, leaving the data incomplete.
// BUSY lasts at most one mprotect (plus, when setting read-only, the few atomic writes that change the state of the pages in the run). The wait has an upper limit BUSY_WAIT_MS and reports red beyond it:
// in the page-fault handler, exceeding it returns "not our page" and lets the crash handler report the crash truthfully instead of spinning silently. The wait only reads atomics and takes no lock.
enum { PG_NONE = 0, PG_PROT = 1, PG_BUSY = 2, PG_OWNED = 3 };
enum { BUSY_WAIT_MS = 2000 };
// 跟踪区间 = 各内存池（MemoryPoolInitialize 登记）。池销毁后复用 live=0 的槽，信号处理里无锁扫描。
// Tracked ranges = the memory pools (registered by MemoryPoolInitialize). After a pool is destroyed its live=0 slot is reused; the signal handler scans without locks.
#define MAX_RANGE 4096   // 原为 256：内存池多于它时超出的池不跟踪，里面的贴图每次取用都当成脏的重传（暗黑实测峰值见内存读数行的「内存池」；塞尔达约 300 个，踩过）。扫描只到已用的槽数，容量大不增加开销 | was 256: with more memory pools than that the excess pools are untracked and their textures are treated as dirty and re-uploaded on every use (for Diablo's measured peak see "memory pools" in the memory readout line; Zelda has about 300 and hit this). The scan only covers slots in use, so a larger capacity costs nothing
typedef struct { uintptr_t base, end; _Atomic uint8_t *state; _Atomic uint64_t *gen; size_t cap; _Atomic int live; _Atomic uint64_t latest; _Atomic uint64_t lifetime, changes;
    _Atomic uint64_t owned_identity, owned_version; _Atomic uintptr_t owned_base, owned_end;
} range_t;
static range_t g_r[MAX_RANGE];
static _Atomic int g_nr;
static uintptr_t g_pg, g_shift;
static _Atomic uint64_t g_now = 1;
// Completion sequence, distinct from generation reservation. Increment for
// every published edit, even when concurrent writers finish out of order.
// Cache consumers retain their original per-page upload stamp.
static _Atomic uint64_t g_changes = 1;
// Explicit owned-fixture opt-in only. Zero on the ordinary game path.
// Old unscoped proofs remain conservative while a token is active. New
// fixed-envelope scopes preserve disjoint queries without a registry/page scan.
static _Atomic unsigned g_owned_active;
static uint64_t g_owned_identity; // slot mutex; never reused
static _Atomic uint64_t g_faults, g_preps;
static _Atomic uint64_t g_adds_total;
static _Atomic int g_live, g_live_peak;   // 在册的池数与峰值（内存读数行用） | number of registered pools and peak (for the memory readout line)
void nvn_dirty_stat(int *live, int *peak, int *cap) { *live = atomic_load(&g_live); *peak = atomic_load(&g_live_peak); *cap = MAX_RANGE; }
static _Atomic int g_overflow_logged;
static pthread_mutex_t g_slot_mu = PTHREAD_MUTEX_INITIALIZER;   // 建池 / 销池（客体线程，可能并发）互斥选槽；缺页处理路径不碰它 | Pool creation / destruction (guest threads, possibly concurrent) pick slots under a mutex; the page-fault handling path does not touch it
static _Atomic uint64_t g_mp_calls, g_mp_pages, g_mp_ticks;   // 本模块的 mprotect：调用次数、覆盖页数、耗时（mach 时基，缺页路径里也安全） | This module's mprotect: call count, pages covered, time spent (mach timebase, safe in the page-fault path too)
static void mp(uintptr_t a, size_t len, int prot) {
    uint64_t t = nvn_clock_ticks(); mprotect((void *)a, len, prot);
    atomic_fetch_add(&g_mp_ticks, nvn_clock_ticks() - t); atomic_fetch_add(&g_mp_calls, 1); atomic_fetch_add(&g_mp_pages, len >> g_shift);
}

static _Atomic int g_busy_red;
// 等这一页离开 BUSY。返回 1 = 已离开；0 = 等过了上限（报红一次）。信号处理里可用：只有原子读、mach_absolute_time、sched_yield
// Wait for this page to leave BUSY. Returns 1 = it has left; 0 = waited past the limit (reported red once). Usable in a signal handler: only atomic reads, mach_absolute_time and sched_yield
static int busy_wait(range_t *r, uintptr_t p) {
    uint64_t t0 = 0;
    for (unsigned n = 1; atomic_load(&r->state[p]) == PG_BUSY; n++) {
        if (n & 0x3ff) continue;
        uint64_t now = nvn_clock_ticks(); if (!t0) t0 = now;
        if (nvn_clock_to_ns(now - t0) > (uint64_t)BUSY_WAIT_MS * 1000000ull) {
            if (!atomic_exchange(&g_busy_red, 1)) LOG("【红】脏页跟踪：页 %p 停在「忙」超过 %d 毫秒（改保护的线程没走完），不再等", (void *)(r->base + (p << g_shift)), BUSY_WAIT_MS);
            return 0; }
        sched_yield();
    }
    return 1;
}
// Android 的 malloc 指针可能带顶字节标签，信号地址与 mprotect 使用去标签地址。
// Android malloc pointers may carry top-byte tags; signal addresses and mprotect use untagged addresses.
static uintptr_t address(const void *p) {
    uintptr_t a = (uintptr_t)p;
#if defined(__ANDROID__) && defined(__aarch64__)
    a &= UINT64_C(0x00ffffffffffffff);
#endif
    return a;
}
static range_t *find(uintptr_t a) {
    int n = atomic_load(&g_nr);
    for (int i = 0; i < n; i++) if (atomic_load_explicit(&g_r[i].live, memory_order_acquire) && a >= g_r[i].base && a < g_r[i].end) return &g_r[i];
    return NULL;
}
// MAX is a permanent invalid key; saturation must never expose a wrap window.
// All changes after initialization are RMWs: an acquire of a later completion
// also observes earlier lifetime invalidation through the release sequence.
_Static_assert(__atomic_always_lock_free(sizeof(uintptr_t),0), "owned scope requires lock-free pointer atomics");
_Static_assert(__atomic_always_lock_free(sizeof(uint64_t), 0), "dirty publication requires lock-free 64-bit atomics");
static void advance(_Atomic uint64_t *clock) {
    uint64_t old = atomic_load_explicit(clock, memory_order_relaxed);
    while (old != UINT64_MAX && !atomic_compare_exchange_weak_explicit(
        clock, &old, old + 1, memory_order_release, memory_order_relaxed)) {}
}
#ifdef DM_DIRTY_RANGE_TEST_HOOKS
static nvn_dirty_test_hook test_hook;
static void *test_hook_context;
void nvn_dirty_test_publication_hook(nvn_dirty_test_hook hook, void *context) {
    test_hook_context = context; test_hook = hook; // set/reset only while writer joined
}
#endif
static void unprotect_page(range_t *r, uintptr_t p, int host_write) {   // 调用方已把状态从 PROT 改成 BUSY | The caller has already changed the state from PROT to BUSY
    mp(r->base + (p << g_shift), g_pg, PROT_READ | PROT_WRITE);
    uint64_t generation = atomic_fetch_add(&g_now, 1) + 1;
#ifdef DM_DIRTY_RANGE_TEST_HOOKS
    // Never pause a signal handler. Only the owned host-preparation test uses it.
    if (host_write && test_hook) test_hook((void *)(r->base + (p << g_shift)), generation, test_hook_context);
#else
    (void)host_write;
#endif
    atomic_store(&r->gen[p], generation);
    // 并发解保护不能让区间代号倒退；先发布页代号再发布区间最大值。
    // Concurrent unprotects must not move the range clock backwards. Publish
    // the page generation before the range maximum.
    uint64_t latest = atomic_load(&r->latest);
    while (latest < generation && !atomic_compare_exchange_weak(&r->latest, &latest, generation)) {}
    advance(&r->changes);
    advance(&g_changes);
    atomic_store(&r->state[p], PG_NONE);
}
static int on_fault(void *addr, int is_write, void *uc) {
    (void)uc;
    range_t *r = is_write ? find(address(addr)) : NULL;
    if (!r) return 0;
    uintptr_t p = (address(addr) - r->base) >> g_shift;
    uint8_t st = PG_PROT;
    if (atomic_compare_exchange_strong(&r->state[p], &st, PG_BUSY)) { unprotect_page(r, p, 0); atomic_fetch_add(&g_faults, 1); return 1; }
    if (st == PG_BUSY) return busy_wait(r, p);   // 别的线程正在改这页的保护：等它改完重执行（改成了只读就再缺页一次、走上面那一支） | Another thread is changing this page's protection: wait for it to finish and re-execute (if it became read-only, it faults once more and takes the branch above)
    return atomic_load(&r->gen[p]) != 0;   // 刚被别的线程解掉（本次缺页发生在解之前）：重执行；从未保护过的页不是我们的 | Just unprotected by another thread (this fault happened before the unprotect): re-execute; pages that were never protected are not ours
}
static void prep_write(void *addr, size_t len) {
    uintptr_t a = address(addr), e = a + len;
    while (len && a < e) {
        range_t *r = find(a);
        if (!r) { a = (a + g_pg) & ~(g_pg - 1); continue; }
        uintptr_t stop = e < r->end ? e : r->end;
        for (uintptr_t p = (a - r->base) >> g_shift, q = (stop - 1 - r->base) >> g_shift; p <= q; p++) {
            for (;;) { uint8_t st = PG_PROT;   // 见 BUSY 等完要重看：对方可能是在设只读，等完这页是 PROT，照样得解 | After waiting out BUSY, look again: the other side may have been setting read-only, in which case the page is PROT after the wait and still has to be unprotected
                if (atomic_compare_exchange_strong(&r->state[p], &st, PG_BUSY)) { unprotect_page(r, p, 1); atomic_fetch_add(&g_preps, 1); break; }
                if (st != PG_BUSY || !busy_wait(r, p)) break; }
        }
        a = stop;
    }
}

void nvn_dirty_add_range(const void *mem, size_t size) {
    static int inited;
    if (!inited) { inited = 1; g_pg = (uintptr_t)getpagesize(); g_shift = __builtin_ctzl(g_pg); dm_fault_hook_register(on_fault); dm_guest_write_prep_register(prep_write); }
    atomic_fetch_add(&g_adds_total, 1);
    pthread_mutex_lock(&g_slot_mu);
    int n = atomic_load(&g_nr);
    uintptr_t added = address(mem), added_end = added + size;
    for(int i=0;i<n;i++) if(atomic_load(&g_r[i].live) && atomic_load(&g_r[i].owned_identity) &&
        (added_end<added || (added<g_r[i].end && g_r[i].base<added_end)))
        die("owned dirty exclusion forbids overlapping range registration");
    int slot = -1;
    for (int i = 0; i < n; i++) if (!atomic_load_explicit(&g_r[i].live, memory_order_acquire)) { slot = i; break; }
    if (slot < 0) {
        if (n >= MAX_RANGE) {
            int expected = 0;
            if (atomic_compare_exchange_strong(&g_overflow_logged, &expected, 1)) LOG("【红】脏页跟踪区间已满（%d），池 %p 不跟踪", MAX_RANGE, mem);
            pthread_mutex_unlock(&g_slot_mu); return;
        }
        slot = n;
    }
    { int lv = atomic_fetch_add(&g_live, 1) + 1, pk = atomic_load(&g_live_peak); while (lv > pk && !atomic_compare_exchange_weak(&g_live_peak, &pk, lv)) {} }
    range_t *r = &g_r[slot];
    // Neither clock is reset on reuse. Invalidate the old token BEFORE publishing
    // any sequence which could otherwise be paired with that old lifetime.
    advance(&r->lifetime);
    advance(&r->changes);
    uintptr_t base = address(mem) & ~(g_pg - 1), end = (address(mem) + size + g_pg - 1) & ~(g_pg - 1);
    size_t np = (end - base) >> g_shift;
    _Atomic uint8_t *state = r->state;
    _Atomic uint64_t *gen = r->gen;
    if (r->cap < np) {
        state = calloc(np, 1); gen = calloc(np, sizeof *gen);
        if (!state || !gen) die("脏页表分配失败（%zu 页）", np);
        // 旧数组不 free：信号处理里可能仍有极短暂的陈旧读者，泄漏量 = 每页 9 字节。
        // The old array is not freed: the signal handler may still have a very brief stale reader; leak = 9 bytes per page.
        r->cap = np;
    } else {
        memset((void *)state, 0, np); memset((void *)gen, 0, np * sizeof *gen);
    }
    r->base = base; r->end = end; r->state = state; r->gen = gen;
    atomic_store(&r->latest, 0);
    if (slot == n) atomic_store(&g_nr, n + 1);
    atomic_store_explicit(&r->live, 1, memory_order_release);
    advance(&g_changes);
    pthread_mutex_unlock(&g_slot_mu);
}
// 池销毁：先把仍受保护的页还原成可写（池内存归还游戏后可能另作他用），再标失效
// Pool destruction: first restore still-protected pages to writable (the pool memory may be reused for something else once returned to the game), then mark invalid
void nvn_dirty_remove_range(const void *mem) {
    pthread_mutex_lock(&g_slot_mu);
    range_t *r = find(address(mem));
    if(r && atomic_load(&r->owned_identity)) die("owned dirty exclusion requires quiescent release before range removal");
    if (r) { advance(&r->lifetime); advance(&r->changes); prep_write((void *)r->base, r->end - r->base); atomic_store_explicit(&r->live, 0, memory_order_release); atomic_fetch_sub(&g_live, 1); advance(&g_changes); }
    pthread_mutex_unlock(&g_slot_mu);
}
int nvn_dirty_tracked(const void *addr, size_t len) {
    uintptr_t a=address(addr);if(len>UINTPTR_MAX-a)return 0;
    range_t *r = find(a); return r && len<=r->end-a;
}
// 设只读：先把页标成 BUSY，mprotect 落地后才标 PROT（次序与不变式见文件头的状态说明）
// Set read-only: mark the page BUSY first, and mark it PROT only after mprotect has landed (for the order and the invariant see the state notes at the top of the file)
void nvn_dirty_protect(const void *addr, size_t len) {
    if (!len || !nvn_dirty_tracked(addr, len)) return;
    range_t *r = find(address(addr));
    uintptr_t a = address(addr), p = (a - r->base) >> g_shift, q = (a + len - 1 - r->base) >> g_shift;
    // 连续置成 PROT 的页合成一次 mprotect（逐页调用在首传大贴图时一帧上千次；选角页启动帧 5：4888 次）。
    // 已是 PROT / BUSY 的页断开连续段：不重复保护，也不碰别的线程正在改的页
    // Consecutive pages set to PROT are merged into one mprotect (per-page calls ran to over a thousand per frame when large textures were first uploaded; character select, startup frame 5: 4888 calls).
    // Pages already PROT / BUSY break a run: they are not protected again, and pages another thread is changing are left alone
    uintptr_t run = 0; int open = 0;
    for (uintptr_t i = p; i <= q + 1; i++) {
        uint8_t st = PG_NONE;
        int took = i <= q && atomic_compare_exchange_strong(&r->state[i], &st, PG_BUSY);
        if (took && !open) { run = i; open = 1; }
        if (!took && open) { mp(r->base + (run << g_shift), (i - run) << g_shift, PROT_READ); for (uintptr_t k = run; k < i; k++) atomic_store(&r->state[k], PG_PROT); open = 0; }
    }
}
uint64_t nvn_dirty_now(void) { return atomic_load(&g_now); }
uint64_t nvn_dirty_change_epoch(void) {
    if(atomic_load_explicit(&g_owned_active,memory_order_acquire))return 0;
    uint64_t value = atomic_load_explicit(&g_changes, memory_order_acquire);
    return value == UINT64_MAX || atomic_load_explicit(&g_owned_active,memory_order_acquire) ? 0 : value;
}
int nvn_dirty_range_refresh(const nvn_dirty_range_proof *old, nvn_dirty_range_proof *proof) {
    if (!old->lifetime || old->lifetime == UINT64_MAX || old->slot >= MAX_RANGE) return 0;
    range_t *r = &g_r[old->slot];
    if(atomic_load_explicit(&r->owned_identity,memory_order_acquire))return 0;
    uint64_t life = atomic_load_explicit(&r->lifetime, memory_order_acquire);
    uint64_t seq = atomic_load_explicit(&r->changes, memory_order_acquire);
    // No plain envelope/state pointer reads: slot reuse may be concurrent.
    if (life != old->lifetime || !seq || seq == UINT64_MAX ||
        !atomic_load_explicit(&r->live, memory_order_acquire) ||
        atomic_load_explicit(&r->lifetime, memory_order_acquire) != life ||
        atomic_load_explicit(&r->owned_identity,memory_order_acquire)) return 0;
    *proof = (nvn_dirty_range_proof){old->slot, 0, life, seq};
    return 1;
}
int nvn_dirty_range_observe(const void *addr, size_t len, nvn_dirty_range_proof *proof) {
    uintptr_t a = address(addr);
    if (!len || len > UINTPTR_MAX - a || pthread_mutex_trylock(&g_slot_mu)) return 0;
    range_t *r = find(a);
    int valid = r && len <= r->end - a;
    if (valid) {
        nvn_dirty_range_proof key = {(uint32_t)(r - g_r), 0,
            atomic_load_explicit(&r->lifetime, memory_order_acquire), 0};
        valid = nvn_dirty_range_refresh(&key, proof);
    }
    pthread_mutex_unlock(&g_slot_mu);
    return valid;
}
// Stable metadata snapshot, never a wait. Atomic fields avoid C data races
// across transitions; the monotonic version closes begin/release/rollback ABA.
// Sequentially consistent metadata operations keep the two version reads and
// interval reads in one total order; no page/registry walk on cached refresh.
typedef struct { uint64_t version;uintptr_t begin,end;int active; } owned_scope;
static int owned_scope_read(range_t *r,owned_scope *out) {
    uint64_t version=atomic_load(&r->owned_version);
    if((version&1) || version==UINT64_MAX)return 0;
    uint64_t identity=atomic_load(&r->owned_identity);
    uintptr_t begin=0,end=0;
    if(identity) {begin=atomic_load(&r->owned_base);end=atomic_load(&r->owned_end);}
    if(atomic_load(&r->owned_version)!=version)return 0;
    *out=(owned_scope){version,begin,end,identity!=0};return 1;
}
static int owned_scope_overlap(const owned_scope *scope,uintptr_t begin,uintptr_t end) {
    return scope->active && begin<scope->end && scope->begin<end;
}
// A proof object must be obtained by observe and remain unmodified. Actual
// query arguments bind refresh to that exact envelope, not an arbitrary slot.
int nvn_dirty_scope_refresh(const void *addr,size_t len,const nvn_dirty_scope_proof *old,
                           nvn_dirty_scope_proof *proof,uint64_t *epoch) {
    uintptr_t begin=address(addr);
    if(epoch)*epoch=0;
    if(!old || !proof || !epoch || !len || len>UINTPTR_MAX-begin || old->reserved ||
       old->begin!=begin || old->end!=begin+len || !old->lifetime ||
       old->lifetime==UINT64_MAX || old->slot>=MAX_RANGE ||
       (old->exclusion_version&1) || old->exclusion_version==UINT64_MAX)return 0;
    // Capture BEFORE the negative page test performed by the caller, just as
    // the original global shortcut. Never advance an upload stamp from this.
    uint64_t global=atomic_load_explicit(&g_changes,memory_order_acquire);
    if(!global || global==UINT64_MAX)return 0;
    range_t *r=&g_r[old->slot];
    uint64_t life=atomic_load_explicit(&r->lifetime,memory_order_acquire);
    owned_scope scope;
    if(life!=old->lifetime || !owned_scope_read(r,&scope) ||
       owned_scope_overlap(&scope,begin,begin+len) || old->exclusion_version>scope.version)return 0;
    uint64_t seq=atomic_load_explicit(&r->changes,memory_order_acquire);
    if(!seq || seq==UINT64_MAX || !atomic_load_explicit(&r->live,memory_order_acquire) ||
       atomic_load(&r->owned_version)!=scope.version ||
       atomic_load_explicit(&r->lifetime,memory_order_acquire)!=life)return 0;
    *proof=(nvn_dirty_scope_proof){.slot=old->slot,.lifetime=life,.changes=seq,
        .exclusion_version=scope.version,.begin=begin,.end=begin+len};
    *epoch=global;return 1;
}
int nvn_dirty_scope_observe(const void *addr,size_t len,nvn_dirty_scope_proof *proof,uint64_t *epoch) {
    uintptr_t begin=address(addr);
    if(epoch)*epoch=0;
    if(!proof || !epoch || !len || len>UINTPTR_MAX-begin || pthread_mutex_trylock(&g_slot_mu))return 0;
    range_t *r=find(begin);int good=r && len<=r->end-begin;
    if(good) {
        nvn_dirty_scope_proof key={.slot=(uint32_t)(r-g_r),
            .lifetime=atomic_load_explicit(&r->lifetime,memory_order_acquire),.begin=begin,.end=begin+len};
        good=nvn_dirty_scope_refresh(addr,len,&key,proof,epoch);
    }
    pthread_mutex_unlock(&g_slot_mu);return good;
}
static void owned_transition_begin(range_t *r) {
    uint64_t version=atomic_load(&r->owned_version);
    if((version&1) || version>UINT64_MAX-2)die("owned exclusion version exhausted");
    atomic_fetch_add(&r->owned_version,1);
}
static void owned_transition_end(range_t *r) {atomic_fetch_add(&r->owned_version,1);}
#ifdef DM_DIRTY_RANGE_TEST_HOOKS
static nvn_dirty_owned_transition_hook owned_test_hook;
static void *owned_test_context;
void nvn_dirty_test_owned_transition(nvn_dirty_owned_transition_hook hook,void *context) {
    owned_test_hook=hook;owned_test_context=context; // set/reset with writer joined
}
static void owned_transition_test(const void *addr,int release) {
    if(owned_test_hook)owned_test_hook(addr,release,owned_test_context);
}
#else
static void owned_transition_test(const void *addr,int release) {(void)addr;(void)release;}
#endif
// Slot mutex held. Publish conservative dirty state before returning any page
// to the ordinary protection flow, including rollback of a partial claim.
static void owned_publish(range_t *r,uintptr_t first,uintptr_t count) {
    if(!count)return;
    uint64_t generation=atomic_fetch_add(&g_now,1)+1;
    for(uintptr_t p=first;p<first+count;p++)atomic_store(&r->gen[p],generation);
    uint64_t latest=atomic_load(&r->latest);
    while(latest<generation && !atomic_compare_exchange_weak(&r->latest,&latest,generation)) {}
    advance(&r->changes);advance(&g_changes);
}
int nvn_dirty_owned_exclude(const void *addr,size_t len,nvn_dirty_owned_exclusion *token) {
#ifdef __APPLE__
    (void)addr;(void)len;(void)token;return 0;
#else
    uintptr_t a=address(addr);
    if(!token || token->identity || token->lifetime || token->slot || token->reserved || token->base || token->size ||
        !g_pg || !len || len>(16u<<20) || len>UINTPTR_MAX-a ||
        (a&(g_pg-1)) || (len&(g_pg-1)))return 0;
    pthread_mutex_lock(&g_slot_mu);
    range_t *r=find(a);
    if(!r || len>r->end-a || atomic_load(&r->owned_identity) ||
        !atomic_load(&r->lifetime) || atomic_load(&r->lifetime)==UINT64_MAX ||
        atomic_load(&r->changes)==UINT64_MAX || g_owned_identity==UINT64_MAX ||
        (atomic_load(&r->owned_version)&1) || atomic_load(&r->owned_version)>UINT64_MAX-4) {
        pthread_mutex_unlock(&g_slot_mu);return 0;
    }
    // Do not let another tracker protect an alias of any claimed page.
    for(int i=0,n=atomic_load(&g_nr);i<n;i++)
        if(&g_r[i]!=r && atomic_load(&g_r[i].live) && a<g_r[i].end && g_r[i].base<a+len) {
            pthread_mutex_unlock(&g_slot_mu);return 0;
        }
    uint64_t identity=++g_owned_identity;
    owned_transition_begin(r);
    atomic_store(&r->owned_base,a);atomic_store(&r->owned_end,a+len);
    // Defeat negative proofs BEFORE any CPU writes can escape protection.
    atomic_fetch_add(&g_owned_active,1);
    atomic_store(&r->owned_identity,identity);
    advance(&r->changes);advance(&g_changes);
    owned_transition_test(addr,0);
    uintptr_t first=(a-r->base)>>g_shift,count=len>>g_shift,claimed=0;
    for(;claimed<count;claimed++) {
        uint8_t expected=PG_NONE;
        if(!atomic_compare_exchange_strong(&r->state[first+claimed],&expected,PG_OWNED))break;
    }
    if(claimed!=count) {
        // Never touch the page owned by PROT/BUSY/another operation. No
        // mprotect, GUP, or retry; only release our own successful prefix.
        owned_publish(r,first,claimed);
        for(uintptr_t p=first;p<first+claimed;p++)atomic_store(&r->state[p],PG_NONE);
        atomic_store(&r->owned_identity,0);
        atomic_fetch_sub(&g_owned_active,1);
        advance(&r->changes);advance(&g_changes);owned_transition_end(r);
        pthread_mutex_unlock(&g_slot_mu);return 0;
    }
    owned_transition_end(r);
    *token=(nvn_dirty_owned_exclusion){.slot=(uint32_t)(r-g_r),
        .lifetime=atomic_load(&r->lifetime),.identity=identity,.base=a,.size=len};
    pthread_mutex_unlock(&g_slot_mu);return 1;
#endif
}
int nvn_dirty_owned_release(nvn_dirty_owned_exclusion *token) {
    if(!token || !token->identity || !token->lifetime || token->slot>=MAX_RANGE || token->reserved)return 0;
    pthread_mutex_lock(&g_slot_mu);range_t *r=&g_r[token->slot];
    if(!atomic_load(&r->live) || atomic_load(&r->lifetime)!=token->lifetime ||
        atomic_load(&r->owned_identity)!=token->identity || r->owned_base!=token->base ||
        token->size!=r->owned_end-r->owned_base) {
        pthread_mutex_unlock(&g_slot_mu);return 0;
    }
    uintptr_t first=(r->owned_base-r->base)>>g_shift,count=token->size>>g_shift;
    for(uintptr_t p=first;p<first+count;p++)if(atomic_load(&r->state[p])!=PG_OWNED)
        die("owned dirty exclusion page state changed before quiescent release");
    owned_transition_begin(r);owned_transition_test((const void *)token->base,1);
    owned_publish(r,first,count);
    for(uintptr_t p=first;p<first+count;p++)atomic_store(&r->state[p],PG_NONE);
    atomic_store(&r->owned_identity,0);
    atomic_fetch_sub(&g_owned_active,1);advance(&r->changes);advance(&g_changes);owned_transition_end(r);
    *token=(nvn_dirty_owned_exclusion){0};pthread_mutex_unlock(&g_slot_mu);return 1;
}
#ifdef DM_DIRTY_RANGE_TEST_HOOKS
void nvn_dirty_test_poison_range(const void *addr, int lifetime) {
    pthread_mutex_lock(&g_slot_mu);
    range_t *r = find(address(addr));
    if (r) atomic_store_explicit(lifetime ? &r->lifetime : &r->changes, UINT64_MAX, memory_order_release);
    pthread_mutex_unlock(&g_slot_mu);
}
uint64_t nvn_dirty_test_global_epoch(uint64_t value) { return atomic_exchange(&g_changes, value); }
void nvn_dirty_test_lock_slots(int lock) { if (lock) pthread_mutex_lock(&g_slot_mu); else pthread_mutex_unlock(&g_slot_mu); }
#endif
void nvn_dirty_visit(const void *addr, size_t len, uint64_t stamp,
                     void (*visit)(const void *, size_t, void *), void *context) {
    if (!len) return;
    uintptr_t a = address(addr);
    if(len>UINTPTR_MAX-a) {visit(addr,len,context);return;}
    range_t *r = find(a);
    if (!r || len > r->end - a) { visit(addr, len, context); return; }
    owned_scope scope={0};
    if(!owned_scope_read(r,&scope))stamp=0;
    else if(!scope.active)scope=(owned_scope){0};
    for (size_t offset = 0; offset < len;) {
        uintptr_t current = a + offset, page = (current - r->base) >> g_shift;
        size_t size = g_pg - (current & (g_pg - 1));
        if (size > len - offset) size = len - offset;
        if (!stamp || owned_scope_overlap(&scope,current,current+size) || atomic_load(&r->gen[page]) > stamp)
            visit((const uint8_t *)addr + offset, size, context);
        offset += size;
    }
}
int nvn_dirty_since(const void *addr, size_t len, uint64_t stamp) {
    if (!len) return 0;
    if (!nvn_dirty_tracked(addr, len)) return 1;   // 未跟踪时保守视为脏，宁可多传也不显示旧内容 | When untracked, conservatively treat as dirty: better to upload more than to show stale content
    range_t *r = find(address(addr));
    owned_scope scope={0};
    if(!owned_scope_read(r,&scope) || owned_scope_overlap(&scope,address(addr),address(addr)+len))return 1;
    if (atomic_load(&r->latest) <= stamp) return 0;
    if (address(addr) == r->base && len == r->end - r->base) return 1;
    uintptr_t a = address(addr), p = (a - r->base) >> g_shift, q = (a + len - 1 - r->base) >> g_shift;
    for (uintptr_t i = p; i <= q; i++) if (atomic_load(&r->gen[i]) > stamp) return 1;   // 上传前最后一次写的代号 ≤ 戳；之后的写必 > 戳 | The generation of the last write before upload is <= the stamp; any later write must be > the stamp
    return 0;
}
void nvn_dirty_slot_stats(int *live, int *used_slots, uint64_t *adds_total) {
    int n = atomic_load(&g_nr), active = 0;
    for (int i = 0; i < n; i++) active += atomic_load_explicit(&g_r[i].live, memory_order_acquire) != 0;
    *live = active; *used_slots = n; *adds_total = atomic_load(&g_adds_total);
}
void nvn_dirty_stats(uint64_t *faults, uint64_t *preps) { *faults = atomic_load(&g_faults); *preps = atomic_load(&g_preps); }
void nvn_dirty_mp_stats(uint64_t *calls, uint64_t *pages, uint64_t *ns) {
    *calls = atomic_load(&g_mp_calls); *pages = atomic_load(&g_mp_pages); *ns = nvn_clock_to_ns(atomic_load(&g_mp_ticks));
}
