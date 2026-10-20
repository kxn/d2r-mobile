// Included by hle_svc.c. All mappings here are newly reserved, owned guest AS;
// no driver mapping or already published guest memory may be replaced.
#include "dm_guest_heap.h"
#if defined(__linux__)
#include <sys/syscall.h>
#include <linux/memfd.h>
#include <fcntl.h>
#endif
#define RESERVE (5ull << 30)
#define GUEST_HEAP_OFFSET (1ull << 30)
static uint8_t *g_as;
static uint8_t *g_heap;
static uint8_t *g_heap_import_alias;
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
int dm_guest_heap_shared_mode(void) {
    if (!atomic_load_explicit(&g_mem_ready, memory_order_acquire)) return -1;
    return g_heap_selection == 2;
}
int dm_guest_shared_heap_contains(uintptr_t a, size_t n) {
    if (!n || !atomic_load_explicit(&g_mem_ready, memory_order_acquire) ||
        g_heap_selection != 2) return 0;
    uintptr_t b = (uintptr_t)g_heap;
    size_t cur = atomic_load_explicit(&g_heap_cur, memory_order_acquire);
    return a >= b && n <= cur && a - b <= cur - n;
}
void *dm_guest_shared_heap_import_pointer(uintptr_t a, size_t n) {
    if (!dm_guest_shared_heap_contains(a,n) || !g_heap_import_alias) return NULL;
    return g_heap_import_alias + (a-(uintptr_t)g_heap);
}
// Both VMAs are composed before publishing the AS. A separate RW VMA lets
// KGSL pin the same file pages without relaxing the guest's dirty-page RO
// protection. Reserving it allocates VA, not another image/pool copy.
static void *guest_heap_map_shared(uint8_t *guest, size_t capacity, uint8_t **import_alias) {
    *import_alias=NULL;
#if defined(__linux__) && defined(SYS_memfd_create)
    int fd=(int)syscall(SYS_memfd_create,"d2r-guest-heap",MFD_CLOEXEC|MFD_ALLOW_SEALING);
    if(fd<0)return MAP_FAILED;
    if(ftruncate(fd,(off_t)capacity) || fcntl(fd,F_ADD_SEALS,F_SEAL_SHRINK|F_SEAL_GROW|F_SEAL_SEAL)) {
        int saved_errno=errno;close(fd);errno=saved_errno;return MAP_FAILED;
    }
    void *heap=mmap(guest,capacity,PROT_NONE,MAP_FIXED|MAP_SHARED,fd,0);
    if(heap==MAP_FAILED) {int saved_errno=errno;close(fd);errno=saved_errno;return MAP_FAILED;}
    // Preserve pointer alignment modulo every supported host-import alignment
    // (the Vulkan startup gate caps that alignment at 1 MiB).
    const size_t alignment=1u<<20;
    size_t reserve=capacity+alignment;
    uint8_t *raw=mmap(NULL,reserve,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(raw==MAP_FAILED) {int saved_errno=errno;close(fd);errno=saved_errno;return MAP_FAILED;}
    size_t prefix=((uintptr_t)guest-(uintptr_t)raw)&(alignment-1);
    uint8_t *alias=raw+prefix;
    void *mapped=mmap(alias,capacity,PROT_READ|PROT_WRITE,MAP_FIXED|MAP_SHARED,fd,0);
    int saved_errno=errno;close(fd);
    if(mapped==MAP_FAILED) {munmap(raw,reserve);errno=saved_errno;return MAP_FAILED;}
    if(mapped!=alias)die("shared guest import alias reservation address");
    if(prefix && munmap(raw,prefix))die("shared guest import alias prefix");
    if(reserve-prefix-capacity && munmap(alias+capacity,reserve-prefix-capacity))
        die("shared guest import alias suffix");
    *import_alias=alias;return heap;
#else
    (void)guest;(void)capacity;errno=ENOTSUP;return MAP_FAILED;
#endif
}
int dm_guest_heap_probe_views_create(size_t bytes, size_t alignment, dm_guest_heap_probe_views *out) {
    *out=(dm_guest_heap_probe_views){0};
    if(!alignment || (alignment&(alignment-1)) || alignment<(size_t)getpagesize() ||
        alignment>(1u<<20) || !bytes || bytes>(2u<<20) || bytes%alignment) {
        errno=EINVAL;return 0;
    }
    size_t reserve=bytes+alignment;
    uint8_t *raw=mmap(NULL,reserve,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(raw==MAP_FAILED)return 0;
    uint8_t *guest=(void *)(((uintptr_t)raw+alignment-1)&~(uintptr_t)(alignment-1));
    size_t prefix=guest-raw,suffix=reserve-prefix-bytes;
    if(prefix && munmap(raw,prefix))die("shared probe guest prefix");
    if(suffix && munmap(guest+bytes,suffix))die("shared probe guest suffix");
    uint8_t *alias=NULL;
    if(guest_heap_map_shared(guest,bytes,&alias)==MAP_FAILED) {
        int saved_errno=errno;munmap(guest,bytes);errno=saved_errno;return 0;
    }
    *out=(dm_guest_heap_probe_views){.guest=guest,.import_alias=alias,.bytes=bytes};return 1;
}
void dm_guest_heap_probe_views_destroy(dm_guest_heap_probe_views *views) {
    if(views->guest && munmap(views->guest,views->bytes))die("shared probe guest release");
    if(views->import_alias && munmap(views->import_alias,views->bytes))die("shared probe alias release");
    *views=(dm_guest_heap_probe_views){0};
}
static void mem_init_once(void) {
    pthread_mutex_lock(&g_heap_mu);
    uint8_t *as = mmap(NULL, RESERVE, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (as == MAP_FAILED) die("reserve guest AS errno=%d", errno);
    if (g_heap_selection == 2) {
        // Atomic composition within OUR unused reservation, before publishing
        // any base address. Alias/stack retain the original private mapping.
        // Fixed-size sealed memfd, mapped twice, remains one shared backing.
        // Success does not guarantee resident capacity against OOM/SIGBUS.
        void *heap = guest_heap_map_shared(as + GUEST_HEAP_OFFSET, RESERVE - GUEST_HEAP_OFFSET,&g_heap_import_alias);
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
    LOG("guest AS %p +%#llx heap %p alias %p stack %p backing=%s noreserve=0 import_alias=%p",
        g_as, (unsigned long long)RESERVE, g_heap, g_alias, g_stack,
        g_heap_selection == 2 ? "shared-memfd" : "private-anonymous",g_heap_import_alias);
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
