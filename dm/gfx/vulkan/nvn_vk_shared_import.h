#ifndef DM_NVN_VK_SHARED_IMPORT_H
#define DM_NVN_VK_SHARED_IMPORT_H
#include <stdint.h>
#include <stddef.h>
extern void *dm_guest_shared_heap_import_pointer(uintptr_t,size_t) __attribute__((weak));
static void *shared_import_host_pointer(uintptr_t address,size_t bytes) {
  void *alias=dm_guest_shared_heap_import_pointer?
      dm_guest_shared_heap_import_pointer(address,bytes):NULL;
#if defined(DM_VULKAN_TEST_EMBED) && !defined(__ANDROID__)
  // Only host-owned fixtures can lack the real SVC heap. Android builds also
  // embed fixtures, so that macro alone must never authorize a guest retry.
  if(!alias)alias=(void *)address;
#endif
  return alias;
}
#endif
