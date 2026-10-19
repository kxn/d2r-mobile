#include "guest_address_space.h"
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_FIXED_NOREPLACE
// Linux ABI; older kernels may ignore it, so exact return verification below
// is mandatory. Never substitute MAP_FIXED (which would destroy other VMAs).
#define MAP_FIXED_NOREPLACE 0x100000
#endif

static int reserve_exact(uint8_t *address, size_t size,
                         uint8_t **owned, size_t *owned_size) {
  if(!size)return 0;
  void *p=mmap(address,size,PROT_NONE,
      MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
  if(p==MAP_FAILED)return errno;
  // Record the actual returned allocation even when an old kernel ignores the
  // flag. Common rollback releases this result, never the colliding request;
  // a failed cleanup remains explicitly owned in the output for retry.
  *owned=p;*owned_size=size;
  return p==address?0:EEXIST;
}

int dm_guest_as_release(dm_guest_as *s) {
  if(!s)return EINVAL;
  s->ready=0;
  if(s->raw_protection_changed) {
    if(mprotect(s->raw_map,s->raw_size,PROT_READ|PROT_WRITE))return errno;
    s->raw_protection_changed=0;
  }
  if(s->prefix_size) {
    if(munmap(s->prefix,s->prefix_size))return errno;
    s->prefix_size=0;
  }
  if(s->tail_size) {
    if(munmap(s->tail,s->tail_size))return errno;
    s->tail_size=0;
  }
  *s=(dm_guest_as){0};return 0;
}

int dm_guest_as_reserve(dm_guest_as *out, void *raw_map, size_t raw_size,
                        void *slice, size_t slice_size,
                        size_t slice_heap_offset, size_t initial_opened) {
  if(!out || out->base || out->prefix_size || out->tail_size)return EINVAL;
  long page=sysconf(_SC_PAGESIZE);
  if(page<=0 || SIZE_MAX<DM_GUEST_AS_BYTES)return ENOTSUP;
  uintptr_t raw=(uintptr_t)raw_map,usable=(uintptr_t)slice;
  size_t mask=(size_t)page-1;
  if((page&(page-1)) || !raw || !raw_size || !usable || !slice_size ||
     ((raw|usable|raw_size|slice_size|slice_heap_offset|initial_opened)&mask) ||
     raw_size>UINTPTR_MAX-raw || usable<raw || usable>raw+raw_size ||
     slice_size>raw+raw_size-usable || slice_heap_offset>DM_GUEST_HEAP_BYTES ||
     slice_size>DM_GUEST_HEAP_BYTES-slice_heap_offset ||
     initial_opened>DM_GUEST_HEAP_BYTES || usable<slice_heap_offset)
    return EINVAL;
  uintptr_t heap=usable-slice_heap_offset;
  if(heap<DM_GUEST_HEAP_OFFSET || heap-DM_GUEST_HEAP_OFFSET==0 ||
     heap-DM_GUEST_HEAP_OFFSET>UINTPTR_MAX-DM_GUEST_AS_BYTES ||
     raw<heap || raw+raw_size>heap+DM_GUEST_HEAP_BYTES)return EINVAL;
  uintptr_t base=heap-DM_GUEST_HEAP_OFFSET,end=base+DM_GUEST_AS_BYTES;
  dm_guest_as s={.base=(void *)base,.heap=(void *)heap,.raw_map=raw_map,
    .raw_size=raw_size,.slice_heap_offset=slice_heap_offset,
    .slice_size=slice_size,.opened=initial_opened,.prefix=(void *)base,
    .tail=(void *)(raw+raw_size)};
  size_t prefix_size=raw-base,tail_size=end-(raw+raw_size);
  int error=reserve_exact((void *)base,prefix_size,&s.prefix,&s.prefix_size);
  if(error)goto rollback;
  error=reserve_exact((void *)(raw+raw_size),tail_size,&s.tail,&s.tail_size);
  if(error)goto rollback;
  // No pointers are published: protect before opening/initializing this prefix.
  // A failed mprotect may have changed a subrange, so rollback restores all RW.
  s.raw_protection_changed=1;
  if(mprotect(raw_map,raw_size,PROT_NONE)){error=errno;goto rollback;}
  if(initial_opened && mprotect(s.heap,initial_opened,PROT_READ|PROT_WRITE)) {
    error=errno;goto rollback;
  }
  uintptr_t opened_end=heap+initial_opened;
  if(opened_end>raw) {
    uintptr_t zero_end=opened_end<raw+raw_size?opened_end:raw+raw_size;
    memset(raw_map,0,zero_end-raw);
  }
  s.ready=1;*out=s;return 0;
rollback:
  if(dm_guest_as_release(&s))*out=s; // cleanup-only; never a publishable AS
  return error;
}
