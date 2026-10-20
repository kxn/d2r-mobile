// Included by hle_svc.c. All mappings here are newly reserved, owned guest AS;
// no driver mapping or already published guest memory may be replaced.
#include "dm_guest_heap.h"
#define RESERVE (5ull << 30)
#define GUEST_HEAP_OFFSET (1ull << 30)
static uint8_t *g_as;
static uint8_t *g_heap;
static _Atomic size_t g_heap_cur;
static size_t g_heap_cap;
static int g_heap_rejects;
static uint8_t *g_alias; static size_t g_alias_cap;
static uint8_t *g_stack; static size_t g_stack_cap;
static pthread_mutex_t g_heap_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t g_mem_once = PTHREAD_ONCE_INIT;
static _Atomic int g_mem_ready;
static int g_heap_selection; // 0 unspecified legacy, 1 selected private, 2 shared

int dm_guest_heap_backing_select(int shared) {
    if (shared != 0 && shared != 1) return 0;
    pthread_mutex_lock(&g_heap_mu);
    int next = shared ? 2 : 1;
    int ok = !atomic_load_explicit(&g_mem_ready, memory_order_relaxed) &&
        (!g_heap_selection || g_heap_selection == next);
    if (ok) g_heap_selection = next;
    pthread_mutex_unlock(&g_heap_mu);
    return ok;
}
int dm_guest_heap_readable(uint64_t a, size_t n) {
    if (!atomic_load_explicit(&g_mem_ready, memory_order_acquire)) return 0;
    uint64_t b = (uintptr_t)g_heap;
    size_t cur = atomic_load_explicit(&g_heap_cur, memory_order_acquire);
    return a >= b && n <= cur && a - b <= cur - n;
}
int dm_guest_shared_heap_contains(uintptr_t a, size_t n) {
    if (!n || !atomic_load_explicit(&g_mem_ready, memory_order_acquire) ||
        g_heap_selection != 2) return 0;
    uintptr_t b = (uintptr_t)g_heap;
    size_t cur = atomic_load_explicit(&g_heap_cur, memory_order_acquire);
    return a >= b && n <= cur && a - b <= cur - n;
}
static void mem_init_once(void) {
    pthread_mutex_lock(&g_heap_mu);
    uint8_t *as = mmap(NULL, RESERVE, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (as == MAP_FAILED) die("reserve guest AS errno=%d", errno);
    if (g_heap_selection == 2) {
        // Atomic composition within OUR unused reservation, before publishing
        // any base address. Alias/stack retain the original private mapping.
        // Deliberately no MAP_NORESERVE: shmem's full 4 GiB commitment may fail
        // here. Success is not a guarantee against later OOM/SIGBUS.
        void *heap = mmap(as + GUEST_HEAP_OFFSET, RESERVE - GUEST_HEAP_OFFSET,
            PROT_NONE, MAP_FIXED | MAP_SHARED | MAP_ANON, -1, 0);
        if (heap == MAP_FAILED) {
            int saved_errno = errno;
            munmap(as, RESERVE);
            die("shared guest heap reservation failed before publication errno=%d", saved_errno);
        }
        if (heap != as + GUEST_HEAP_OFFSET) die("shared guest heap reservation address");
    }
    g_alias = as + 0x10000000; g_alias_cap = 0x20000000;
    g_stack = as + 0x30000000; g_stack_cap = 0x10000000;
    g_heap = as + GUEST_HEAP_OFFSET; g_heap_cap = RESERVE - GUEST_HEAP_OFFSET;
    g_as = as;
    atomic_store_explicit(&g_mem_ready, 1, memory_order_release);
    LOG("guest AS %p +%#llx heap %p alias %p stack %p backing=%s noreserve=0",
        g_as, (unsigned long long)RESERVE, g_heap, g_alias, g_stack,
        g_heap_selection == 2 ? "shared-anonymous" : "private-anonymous");
    pthread_mutex_unlock(&g_heap_mu);
}
static void mem_init(void) {
    if (!atomic_load_explicit(&g_mem_ready, memory_order_acquire))
        pthread_once(&g_mem_once, mem_init_once);
}
// The SVC's application-pool cap check remains outside this helper. Logical
// extent is published only after successful protection, never on syscall error.
static int guest_heap_resize(size_t size) {
    pthread_mutex_lock(&g_heap_mu);
    size_t cur = atomic_load_explicit(&g_heap_cur, memory_order_relaxed);
    int rc = 0;
    if (size > g_heap_cap) { errno = ENOMEM; rc = -1; }
    else if (size > cur) rc = mprotect(g_heap + cur, size - cur, PROT_READ | PROT_WRITE);
    else if (size < cur) rc = mprotect(g_heap + size, cur - size, PROT_NONE);
    if (!rc) atomic_store_explicit(&g_heap_cur, size, memory_order_release);
    // A failed multi-VMA shrink can have partially changed permissions. Do not
    // continue a shared guest with a falsely usable old extent after that error.
    if (rc && size < cur && g_heap_selection == 2)
        die("shared guest heap shrink failed errno=%d", errno);
    pthread_mutex_unlock(&g_heap_mu);
    return rc;
}
