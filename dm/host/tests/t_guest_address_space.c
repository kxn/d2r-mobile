#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "../guest_address_space.h"
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
static unsigned checks;
#define CHECK(x) do {if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);_exit(1);}checks++;}while(0)
void *__real_mmap(void *,size_t,int,int,int,off_t);
int __real_munmap(void *,size_t);
int __real_mprotect(void *,size_t,int);
static int replace_hint,protect_fail_at,protect_calls,unmap_fail_at,unmap_calls;
static void *fallback_map,*raw_guard;
static size_t fallback_size,raw_guard_size;
void *__wrap_mmap(void *a,size_t n,int p,int f,int d,off_t o) {
  if(replace_hint && (f&MAP_FIXED_NOREPLACE)) {
    replace_hint=0;fallback_map=__real_mmap(NULL,n,p,f&~MAP_FIXED_NOREPLACE,d,o);
    fallback_size=n;return fallback_map;
  }
  return __real_mmap(a,n,p,f,d,o);
}
int __wrap_munmap(void *a,size_t n) {
  CHECK(!raw_guard || (uintptr_t)a+n<=(uintptr_t)raw_guard || (uintptr_t)a>=(uintptr_t)raw_guard+raw_guard_size);
  if(++unmap_calls==unmap_fail_at){errno=EAGAIN;return -1;}
  return __real_munmap(a,n);
}
int __wrap_mprotect(void *a,size_t n,int p) {
  if(++protect_calls==protect_fail_at){errno=ENOMEM;return -1;}
  return __real_mprotect(a,n,p);
}
typedef struct {uint8_t *whole,*base,*raw,*slice;size_t page,raw_size,offset;} fixture;
static fixture create(void) {
  fixture q={.page=(size_t)sysconf(_SC_PAGESIZE),.offset=128u<<20};
  q.whole=__real_mmap(NULL,DM_GUEST_AS_BYTES+2*q.page,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);CHECK(q.whole!=MAP_FAILED);
  q.base=q.whole+q.page;q.raw=q.base+DM_GUEST_HEAP_OFFSET+q.offset;q.raw_size=6*q.page;q.slice=q.raw+q.page;
  CHECK(!__real_munmap(q.base,q.raw-q.base));CHECK(!__real_munmap(q.raw+q.raw_size,q.base+DM_GUEST_AS_BYTES-q.raw-q.raw_size));
  CHECK(!__real_mprotect(q.raw,q.raw_size,PROT_READ|PROT_WRITE));memset(q.raw,0xa5,q.raw_size);
  CHECK(!__real_mprotect(q.whole,q.page,PROT_READ|PROT_WRITE));CHECK(!__real_mprotect(q.base+DM_GUEST_AS_BYTES,q.page,PROT_READ|PROT_WRITE));
  q.whole[0]=0x39;q.base[DM_GUEST_AS_BYTES]=0x71;
  raw_guard=q.raw;raw_guard_size=q.raw_size;protect_calls=unmap_calls=0;protect_fail_at=unmap_fail_at=replace_hint=0;return q;
}
static void destroy(fixture *q) {
  CHECK(q->whole[0]==0x39 && q->base[DM_GUEST_AS_BYTES]==0x71);
  CHECK(!__real_munmap(q->raw,q->raw_size));raw_guard=NULL;
  CHECK(!__real_munmap(q->whole,q->page));CHECK(!__real_munmap(q->base+DM_GUEST_AS_BYTES,q->page));
}
static void unmapped(void *p,size_t n) {
  void *v=__real_mmap(p,n,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);CHECK(v==p);CHECK(!__real_munmap(v,n));
}
static int reserve(dm_guest_as *s,fixture q,size_t opened) {
  return dm_guest_as_reserve(s,q.raw,q.raw_size,q.slice,4*q.page,q.offset+q.page,opened);
}
static void closed_page(uint8_t *p) {
  pid_t child=fork();CHECK(child>=0);
  if(!child){signal(SIGSEGV,SIG_DFL);volatile uint8_t v=*p;(void)v;_exit(0);}
  int status;CHECK(waitpid(child,&status,0)==child);CHECK(WIFSIGNALED(status)&&WTERMSIG(status)==SIGSEGV);
}
int main(void) {
  struct rlimit no_core={0};CHECK(!setrlimit(RLIMIT_CORE,&no_core));
  fixture q=create();dm_guest_as s={0};size_t opened=q.offset+3*q.page;
  CHECK(!reserve(&s,q,opened));CHECK(s.ready && s.base==q.base && s.heap==q.base+DM_GUEST_HEAP_OFFSET && s.opened==opened);
  CHECK(s.raw_map==q.raw && s.raw_size==q.raw_size && s.prefix+s.prefix_size==q.raw && s.tail==q.raw+q.raw_size);
  for(size_t k=0;k<3*q.page;k++)CHECK(q.raw[k]==0);
  CHECK(s.heap[0]==0 && q.raw[-1]==0);closed_page(q.raw+3*q.page);closed_page(s.heap+DM_GUEST_HEAP_BYTES-q.page);
  q.slice[9]=0x4b;CHECK(!dm_guest_as_release(&s)&&!s.base);
  CHECK(q.slice[9]==0x4b);for(size_t k=3*q.page;k<q.raw_size;k++)CHECK(q.raw[k]==0xa5);
  unmapped(q.base,q.raw-q.base);unmapped(q.raw+q.raw_size,q.base+DM_GUEST_AS_BYTES-q.raw-q.raw_size);CHECK(!dm_guest_as_release(&s));destroy(&q);
  // Tail collision: prefix succeeds but must be rolled back; existing byte and
  // driver raw bytes remain exact. Never cover the occupied page with MAP_FIXED.
  q=create();uint8_t *collision=q.base+DM_GUEST_AS_BYTES-q.page;
  CHECK(__real_mmap(collision,q.page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==collision);collision[0]=0xcd;
  CHECK(reserve(&s,q,opened)==EEXIST && !s.base);CHECK(collision[0]==0xcd && q.raw[0]==0xa5 && protect_calls==0);unmapped(q.base,q.raw-q.base);CHECK(!__real_munmap(collision,q.page));destroy(&q);
  // Simulate an old kernel ignoring NOREPLACE and returning a different hint.
  q=create();replace_hint=1;CHECK(reserve(&s,q,opened)==EEXIST&&!s.base);CHECK(fallback_map!=MAP_FAILED && fallback_map!=q.base);unmapped(fallback_map,fallback_size);CHECK(protect_calls==0 && q.raw[0]==0xa5);destroy(&q);
  // Old-kernel hint fallback plus cleanup failure retains the ACTUAL other VA.
  q=create();replace_hint=1;unmap_fail_at=1;CHECK(reserve(&s,q,opened)==EEXIST && s.base && !s.ready);CHECK(s.prefix==fallback_map && s.prefix!=q.base && !s.raw_protection_changed);CHECK(!dm_guest_as_release(&s));unmapped(fallback_map,fallback_size);destroy(&q);
  // Opening failure restores original driver RW/content and drops reservations.
  q=create();protect_fail_at=2;CHECK(reserve(&s,q,opened)==ENOMEM&&!s.base);CHECK(q.raw[0]==0xa5 && q.raw[q.raw_size-1]==0xa5);unmapped(q.base,q.raw-q.base);unmapped(q.raw+q.raw_size,q.base+DM_GUEST_AS_BYTES-q.raw-q.raw_size);destroy(&q);
  // Exceptional rollback-unmap failure retains explicit cleanup-only ownership.
  q=create();protect_fail_at=2;unmap_fail_at=1;CHECK(reserve(&s,q,opened)==ENOMEM && s.base && !s.ready);CHECK(!s.raw_protection_changed);CHECK(!dm_guest_as_release(&s)&&!s.base);CHECK(q.raw[0]==0xa5);destroy(&q);
  // A release protection failure cannot leave a publishable ready AS.
  q=create();CHECK(!reserve(&s,q,opened));protect_fail_at=protect_calls+1;CHECK(dm_guest_as_release(&s)==ENOMEM && s.base && !s.ready && s.raw_protection_changed);CHECK(!dm_guest_as_release(&s)&&!s.base);destroy(&q);
  // Unopened raw remains unspecified until caller explicitly opens/initializes.
  q=create();CHECK(!reserve(&s,q,0));closed_page(q.raw);CHECK(!dm_guest_as_release(&s));CHECK(q.raw[0]==0xa5);destroy(&q);
  // Pure invalid geometry has no mapping/protection side effects.
  q=create();CHECK(dm_guest_as_reserve(&s,q.raw,q.raw_size,q.slice,4*q.page,q.offset+1,opened)==EINVAL);
  CHECK(dm_guest_as_reserve(&s,q.raw,q.raw_size,q.slice,4*q.page,DM_GUEST_HEAP_BYTES,opened)==EINVAL);
  CHECK(dm_guest_as_reserve(&s,(void *)(UINTPTR_MAX-q.page+1),q.page*2,q.slice,q.page,0,0)==EINVAL);
  CHECK(dm_guest_as_reserve(&s,q.raw,q.raw_size,q.slice,q.page,0,0)==EINVAL);
  CHECK(!s.base&&protect_calls==0&&unmap_calls==0&&q.raw[0]==0xa5);destroy(&q);
  printf("{\"test\":\"guest-as-shared-slice-os\",\"checks\":%u,\"result\":\"passed\",\"actual_vulkan_mapping\":false}\n",checks);
}
