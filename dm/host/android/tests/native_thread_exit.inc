// Owned native ARM64 fixture: the original HLE creates/starts/exits/waits for
// pthread-backed guest threads. A tiny guarded guest stack must never service
// libc's TLS-destructor/allocator work after ExitThread.
#include <sys/mman.h>
#include <time.h>
#include <sched.h>
extern int svc_thread(int number,uint64_t regs[32]);
extern void thr_counts(int *handles,int *live,int *objects);
typedef struct {
  uint8_t *guest_low;size_t guest_size;
  uintptr_t host_low,host_size,host18;
  _Atomic unsigned release;
  int finished,main_fallback;
  pthread_mutex_t mutex;pthread_cond_t condition;
} native_thread_exit_state;
static pthread_key_t native_thread_exit_key;
static void native_thread_exit_host_bounds(native_thread_exit_state *s) {
  pthread_attr_t attr;void *base=NULL;size_t size=0;
  if(pthread_getattr_np(pthread_self(),&attr) || pthread_attr_getstack(&attr,&base,&size) ||
      pthread_attr_destroy(&attr) || !base || size<256*1024)
    die("thread exit fixture: native stack bounds");
  s->host_low=(uintptr_t)base;s->host_size=size;
  __asm__ volatile("mov %0,x18" : "=r"(s->host18));
}
__attribute__((noinline)) static void native_thread_exit_destructor(void *argument) {
  native_thread_exit_state *s=argument;
  volatile uint8_t large[96*1024];
  uintptr_t sp,host18;
  __asm__ volatile("mov %0,sp\nmov %1,x18" : "=r"(sp),"=r"(host18));
  if(sp<s->host_low || sp>=s->host_low+s->host_size || host18!=s->host18 ||
      (s->guest_low && sp>=(uintptr_t)s->guest_low && sp<(uintptr_t)s->guest_low+s->guest_size))
    die("thread exit fixture: destructor is not on original pthread stack/ABI");
  for(size_t i=0;i<sizeof large;i+=4096)large[i]=(uint8_t)(i/4096+1);
  // Touch and free real allocations; this also exercises allocator TLS cleanup
  // rather than replacing the libc path with a test-only destructor runner.
  for(unsigned i=0;i<16;i++) {
    size_t size=65536+i*8192;volatile uint8_t *p=malloc(size);
    if(!p)die("thread exit fixture: allocator pressure");
    p[0]=(uint8_t)i;p[size-1]=(uint8_t)(i+1);
    if(p[0]!=(uint8_t)i || p[size-1]!=(uint8_t)(i+1))die("thread exit fixture: allocation bytes");
    free((void *)p);
  }
  for(size_t i=0;i<sizeof large;i+=4096)
    if(large[i]!=(uint8_t)(i/4096+1))die("thread exit fixture: large destructor stack bytes");
  if(s->main_fallback) {
    puts("{\"backend\":\"native-arm64\",\"thread_exit_main_fallback\":true,\"tls_destructor_stack_bytes\":98304,\"result\":\"passed\"}");
    fflush(stdout);
  } else {
    pthread_mutex_lock(&s->mutex);s->finished=1;
    pthread_cond_signal(&s->condition);pthread_mutex_unlock(&s->mutex);
  }
}
static _Noreturn void native_thread_exit_entry(uint64_t argument) {
  native_thread_exit_state *s=(void *)(uintptr_t)argument;
  volatile uint8_t guest_frame[2048];
  uintptr_t sp;__asm__ volatile("mov %0,sp" : "=r"(sp));
  if(!s->main_fallback && (sp<(uintptr_t)s->guest_low || sp>=(uintptr_t)s->guest_low+s->guest_size))
    die("thread exit fixture: entry did not switch to guest stack");
  for(unsigned i=0;i<sizeof guest_frame;i+=128)guest_frame[i]=(uint8_t)i;
  native_thread_exit_host_bounds(s);
  if(pthread_setspecific(native_thread_exit_key,s))die("thread exit fixture: set TLS destructor");
  while(!atomic_load(&s->release))sched_yield();
  for(unsigned i=0;i<sizeof guest_frame;i+=128)
    if(guest_frame[i]!=(uint8_t)i)die("thread exit fixture: guest stack bytes");
  uint64_t regs[32]={0};svc_thread(0x0a,regs);
  die("thread exit fixture: ExitThread returned");
}
static void native_thread_exit_test(void) {
  if(pthread_key_create(&native_thread_exit_key,native_thread_exit_destructor))die("thread exit fixture: TLS key");
  int handles0,live0,objects0;thr_counts(&handles0,&live0,&objects0);
  const size_t page=(size_t)sysconf(_SC_PAGESIZE),guest_size=16384;
  if(!page || guest_size%page)die("thread exit fixture: unsupported page size");
  for(unsigned round=0;round<8;round++) {
    uint8_t *mapping=mmap(NULL,guest_size+2*page,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(mapping==MAP_FAILED || mprotect(mapping+page,guest_size,PROT_READ|PROT_WRITE))die("thread exit fixture: guarded stack");
    native_thread_exit_state state={.guest_low=mapping+page,.guest_size=guest_size,
      .mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};
    uint64_t regs[32]={0};regs[1]=(uintptr_t)native_thread_exit_entry;
    regs[2]=(uintptr_t)&state;regs[3]=(uintptr_t)mapping+page+guest_size;regs[4]=44;regs[5]=UINT64_MAX;
    if(!svc_thread(0x08,regs)||regs[0])die("thread exit fixture: CreateThread");
    uint32_t handle=(uint32_t)regs[1];regs[0]=handle;
    if(!svc_thread(0x09,regs)||regs[0])die("thread exit fixture: StartThread");
    if(round&1) { regs[0]=handle;if(!svc_thread(0x16,regs)||regs[0])die("thread exit fixture: early CloseHandle"); }
    atomic_store(&state.release,1);
    if(!(round&1)) {
      memset(regs,0,sizeof regs);regs[1]=(uintptr_t)&handle;regs[2]=1;regs[3]=UINT64_C(5000000000);
      if(!svc_thread(0x18,regs)||regs[0]||regs[1])die("thread exit fixture: actual WaitSynchronization");
      regs[0]=handle;if(!svc_thread(0x16,regs)||regs[0])die("thread exit fixture: CloseHandle");
    }
    // HLE completion precedes libc destructors. Wait for the actual destructor
    // separately so the fixture never destroys its state while it is in use.
    struct timespec deadline;clock_gettime(CLOCK_REALTIME,&deadline);deadline.tv_sec+=5;
    pthread_mutex_lock(&state.mutex);
    while(!state.finished)if(pthread_cond_timedwait(&state.condition,&state.mutex,&deadline))die("thread exit fixture: TLS destructor timeout");
    pthread_mutex_unlock(&state.mutex);
    if(pthread_cond_destroy(&state.condition)||pthread_mutex_destroy(&state.mutex)||munmap(mapping,guest_size+2*page))
      die("thread exit fixture: release guarded stack");
  }
  int handles,live,objects;thr_counts(&handles,&live,&objects);
  if(handles!=handles0||live!=live0||objects!=objects0)die("thread exit fixture: HLE object lifetime");
  if(pthread_key_delete(native_thread_exit_key))die("thread exit fixture: delete TLS key");
  LOG("[native thread exit fixture] 8 HLE threads,16KiB guarded guest stacks,96KiB real TLS destructor stacks, x18/native-stack bounds, allocator pressure, actual WaitSynchronization and close-before-exit passed");
}
static _Noreturn void native_thread_exit_main_test(void) {
  // A separate native_rule_test invocation exercises the registered main
  // thread, whose stack was never switched and therefore has no continuation.
  static native_thread_exit_state state={.main_fallback=1,.release=1};
  if(pthread_key_create(&native_thread_exit_key,native_thread_exit_destructor))die("thread exit fixture: main TLS key");
  native_thread_exit_entry((uintptr_t)&state);
}
