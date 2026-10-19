// ELF 宿主线程槽：键页按 ABI 存 TP 偏移或原生 TLS 描述符；槽由编译器管理。
// ELF host thread slots: key pages hold TP offsets or native TLS descriptors
// according to the ABI; the compiler owns the slot storage.
#include "dm.h"
#include "platform.h"
#include <errno.h>
#include <sys/uio.h>
#include <unistd.h>

#ifdef DM_TLS_DESC
__attribute__((visibility("hidden"))) __thread uintptr_t dm_tls_slots[8];
#define slots dm_tls_slots
uintptr_t dm_tsd_descriptor(void) {
  uintptr_t descriptor;
  __asm__("adrp %0, :tlsdesc:dm_tls_slots\n"
          "add %0, %0, :tlsdesc_lo12:dm_tls_slots" : "=r"(descriptor));
  return descriptor;
}
#else
static __thread uintptr_t slots[8] __attribute__((tls_model("initial-exec")));
#endif
static int slot_index(pthread_key_t key) {
  const pthread_key_t keys[8] = {g_key18,     g_keytls,     g_keytlr,
                                 g_keygate,   g_keyres[0],  g_keyres[1],
                                 g_keyaspect, g_keyaspectui};
  for (int i = 0; i < 8; i++)
    if (keys[i] == key)
      return i;
  return -1;
}
int dm_tsd_set(pthread_key_t key, const void *value) {
  int i = slot_index(key);
  if (i < 0)
    return pthread_setspecific(key, value);
  slots[i] = (uintptr_t)value;
  return 0;
}
void *dm_tsd_get(pthread_key_t key) {
  int i = slot_index(key);
  return i < 0 ? pthread_getspecific(key) : (void *)slots[i];
}
int64_t dm_tsd_offset(pthread_key_t key) {
  int i = slot_index(key);
  if (i < 0)
    die("unregistered guest TLS key %u", (unsigned)key);
  return (int64_t)((uintptr_t)&slots[i] -
                   (uintptr_t)__builtin_thread_pointer());
}
void dm_tsd_check(void) {
  for (int i = 0; i < g_nmod; i++) {
    const uint64_t *page =
        (const uint64_t *)(g_mod[i].base + g_mod[i].key_page_off);
#ifndef DM_TLS_DESC
    const pthread_key_t keys[8] = {g_key18,     g_keytls,     g_keytlr,
                                   g_keygate,   g_keyres[0],  g_keyres[1],
                                   g_keyaspect, g_keyaspectui};
#endif
    for (int k = 0; k < 8; k++)
      if (page[g_mod[i].key_idx[k]] !=
#ifdef DM_TLS_DESC
          dm_tsd_descriptor())
#else
          (uint64_t)dm_tsd_offset(keys[k]))
#endif
        die("ELF TLS offset differs on this thread");
#ifdef DM_TLS_DESC
    if (g_mod[i].host_abi == 3 && page[g_mod[i].host_gate_idx] != (uintptr_t)dm_gate)
      die("ELF process gate address differs");
#endif
  }
#ifdef DM_TLS_DESC
  // 在宿主初始化阶段分配本线程动态 TLS；离线改写块只走已准备好的解析器。
  // Allocate this thread's dynamic TLS during host initialization, before
  // offline-rewritten guest blocks reach the native resolver.
  (void)*(volatile uintptr_t *)&slots[0];
#endif
}

long dm_self_read(void *out, const void *address, size_t size) {
  struct iovec dst = {out, size}, src = {(void *)address, size};
  return process_vm_readv(getpid(), &dst, 1, &src, 1, 0);
}
