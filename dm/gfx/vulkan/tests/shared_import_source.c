// Compile the production address policy with the real Android macro, even on
// a host. In particular, embedding fixtures must not permit rejected pointers.
#include "../nvn_vk_shared_import.h"
#include <assert.h>
#include <stdio.h>
#ifndef DM_SHARED_IMPORT_NO_PROVIDER
void *dm_guest_shared_heap_import_pointer(uintptr_t address,size_t bytes) {
  if(!bytes || address<0x4000 || bytes>8192 || address-0x4000>8192-bytes)return NULL;
  return (void *)(address+0x10000000);
}
#endif
int main(void) {
#ifndef DM_SHARED_IMPORT_NO_PROVIDER
  assert(shared_import_host_pointer(0x4000,8192)==(void *)0x10004000);
  assert(shared_import_host_pointer(0x5fff,1)==(void *)0x10005fff);
#endif
  const struct {uintptr_t address;size_t bytes;} rejected[]={
    {0,1},{0x4000,0},{0x3fff,1},{0x5fff,2},{UINTPTR_MAX-3,16}};
  for(unsigned n=0;n<sizeof rejected/sizeof *rejected;n++) {
    void *actual=shared_import_host_pointer(rejected[n].address,rejected[n].bytes);
#if defined(DM_VULKAN_TEST_EMBED) && !defined(__ANDROID__)
    assert(actual==(void *)rejected[n].address);
#else
    assert(actual==NULL);
#endif
  }
#ifdef DM_SHARED_IMPORT_NO_PROVIDER
  assert(shared_import_host_pointer(0x4000,4096)==NULL);
#endif
  puts("shared-import-address-policy passed");
}
