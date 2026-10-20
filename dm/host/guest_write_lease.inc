// Shared implementation for production crash.c and the existing headless
// host fixture. Default no-hook path performs no allocation/environment read.
#include <stdatomic.h>
#include <errno.h>
static _Atomic(const dm_guest_write_lease_hooks *) g_guest_write_lease_hooks;
void dm_guest_write_lease_register(const dm_guest_write_lease_hooks *hooks) {
    if (!hooks || !hooks->begin || !hooks->end) die("Invalid guest kernel-write lease hooks");
    const dm_guest_write_lease_hooks *expected = NULL;
    if (!atomic_compare_exchange_strong_explicit(&g_guest_write_lease_hooks,
            &expected, hooks, memory_order_acq_rel, memory_order_acquire) && expected != hooks)
        die("Guest kernel-write lease hooks already registered");
}
dm_guest_write_lease dm_guest_write_begin(void *addr, size_t len) {
    dm_guest_write_lease lease = {0};
    const dm_guest_write_lease_hooks *hooks =
        atomic_load_explicit(&g_guest_write_lease_hooks, memory_order_acquire);
    if (hooks) {
        lease.cookie = hooks->begin(addr, len);
        if (lease.cookie) lease.end = hooks->end;
    }
    // The lease precedes unprotection: a GPU admission cannot pass between
    // preparing the pages and the kernel actually writing those pages.
    dm_guest_write_prepare(addr, len);
    return lease;
}
void dm_guest_write_end(dm_guest_write_lease *lease) {
    if (!lease || !lease->cookie) return;
    void *cookie = lease->cookie;
    void (*end)(void *) = lease->end;
    lease->cookie = NULL; lease->end = NULL;
    // A release hook may perform a futex wake/cleanup. Preserve the actual
    // pread errno so the original FS error mapping/log remains unchanged.
    int saved = errno; end(cookie); errno = saved;
}
