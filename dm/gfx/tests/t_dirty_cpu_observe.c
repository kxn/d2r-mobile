// Owned CPU-only oracle for the exact short observation protocol used by the
// shared Vulkan uploader. Real pread + real SIGSEGV/futex; no GPU or timing.
#define _GNU_SOURCE 1
#define DM_DIRTY_RANGE_TEST_HOOKS 1
#include "../nvn/nvn_dirty.c"
#include "../../host/guest_write_lease.inc"
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
static int (*fault_hook)(void *,int,void *);
static void (*prep_hook)(void *,size_t);
void dm_fault_hook_register(int (*h)(void *,int,void *)){fault_hook=h;}
void dm_guest_write_prep_register(void (*h)(void *,size_t)){prep_hook=h;}
void dm_guest_write_prepare(void *p,size_t n){if(prep_hook)prep_hook(p,n);}
static _Atomic unsigned checks;
#define CHECK(x) do{checks++;if(!(x))die("CPU image observation check line%d: %s",__LINE__,#x);}while(0)
static void fault(int sig,siginfo_t *info,void *ctx){if(fault_hook && fault_hook(info->si_addr,1,ctx))return;_exit(128+sig);}
static void wait_flag(_Atomic int *p){while(!atomic_load_explicit(p,memory_order_acquire))sched_yield();}
static const dm_guest_write_lease_hooks hooks={nvn_dirty_kernel_write_begin,nvn_dirty_kernel_write_end};
static uint8_t *memory;
static _Atomic int kernel_ready,kernel_go,kernel_done,observation_ready,observation_end,writer_done,writer_wait;
static uint64_t captured_stamp;static uint32_t snapshot;
static void *kernel_write(void *unused) {
  (void)unused;
  // This buffer is outside the image's four bytes, but on the SAME host page.
  dm_guest_write_lease lease=dm_guest_write_begin(memory+128,4);CHECK(lease.cookie);
  atomic_store(&kernel_ready,1);wait_flag(&kernel_go);
  int fd=open("/dev/zero",O_RDONLY);CHECK(fd>=0);
  CHECK(pread(fd,memory+128,4,0)==4);CHECK(!close(fd));
  dm_guest_write_end(&lease);atomic_store(&kernel_done,1);return NULL;
}
static void *observe(void *unused) {
  (void)unused;nvn_dirty_gpu_range range={memory,4};
  nvn_dirty_gpu_gate *phase=nvn_dirty_gpu_gate_arm(&range,1);CHECK(phase);
  // All old kernel leases/PG_BUSY preparations finished; the whole host page
  // is RO. The original uploader performs CPU reads only until cancel.
  captured_stamp=nvn_dirty_now();memcpy(&snapshot,memory,4);
  atomic_store_explicit(&observation_ready,1,memory_order_release);wait_flag(&observation_end);
  CHECK(*(volatile uint32_t *)memory==snapshot);
  nvn_dirty_gpu_gate_cancel(phase);return NULL;
}
static void wait_notice(const void *p,uint64_t gate,void *context) {
  (void)p;(void)gate;(void)context;atomic_store_explicit(&writer_wait,1,memory_order_release);
}
static void *user_write(void *unused) {
  (void)unused;*(volatile uint32_t *)memory=0xaabbccdd;
  atomic_store_explicit(&writer_done,1,memory_order_release);return NULL;
}
int main(void) {
  alarm(30);struct rlimit no_core={0,0};CHECK(!setrlimit(RLIMIT_CORE,&no_core));
  struct sigaction sa={.sa_sigaction=fault,.sa_flags=SA_SIGINFO};CHECK(!sigaction(SIGSEGV,&sa,NULL));
  CHECK(nvn_dirty_gpu_gates_enable());dm_guest_write_lease_register(&hooks);
  size_t page=(size_t)getpagesize();memory=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
  CHECK(memory!=MAP_FAILED);*(uint32_t *)memory=0x12345678;
  // Retain the original assertion as a real negative: the old uncoordinated
  // image protect makes a previously prepared kernel destination fail EFAULT.
  int outcome[2];CHECK(!pipe(outcome));pid_t child=fork();CHECK(child>=0);
  if(!child) {
    CHECK(!close(outcome[0]));
    dm_guest_write_lease lease=dm_guest_write_begin(memory+128,4);CHECK(lease.cookie);
    nvn_dirty_scope *scope=nvn_dirty_scope_acquire(memory,4);CHECK(scope);
    nvn_dirty_protect(memory,4);int fd=open("/dev/zero",O_RDONLY);CHECK(fd>=0);
    ssize_t n=pread(fd,memory+128,4,0);int result[2]={(int)n,errno};
    CHECK(write(outcome[1],result,sizeof result)==sizeof result);
    CHECK(n==4); // original assertion must fail, not a weakened EFAULT check
    _exit(5);
  }
  CHECK(!close(outcome[1]));int result[2];CHECK(read(outcome[0],result,sizeof result)==sizeof result);CHECK(!close(outcome[0]));
  int status;CHECK(waitpid(child,&status,0)==child);CHECK(WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
  CHECK(result[0]==-1 && result[1]==EFAULT);
  LOG("[CPU observe original negative] pread=%d errno=%d (EFAULT), original n==4 assertion aborted",result[0],result[1]);
  pthread_t kernel,observer,writer;
  CHECK(!pthread_create(&kernel,NULL,kernel_write,NULL));wait_flag(&kernel_ready);
  CHECK(!nvn_dirty_tracked(memory,4)); // kernel lease predates the first image scope
  nvn_dirty_scope *scope=nvn_dirty_scope_acquire(memory,4);CHECK(scope);
  CHECK(!pthread_create(&observer,NULL,observe,NULL));
  range_t *r=find((uintptr_t)memory);uintptr_t pi=((uintptr_t)memory-r->base)>>g_shift;
  while(!atomic_load_explicit(&r->gpu_gate[pi],memory_order_acquire))sched_yield();
  CHECK(!atomic_load(&observation_ready)); // real arm is waiting for the old lease
  atomic_store(&kernel_go,1);CHECK(!pthread_join(kernel,NULL));CHECK(atomic_load(&kernel_done));
  wait_flag(&observation_ready);CHECK(snapshot==0x12345678);
  CHECK(!nvn_dirty_since(memory,4,captured_stamp));
  // Disjoint pages receive no observation gate or fake dirty metadata.
  memory[page]=0x81;CHECK(!nvn_dirty_tracked(memory+page,1));
  nvn_dirty_test_gpu_wait_hook(wait_notice,NULL);
  CHECK(!pthread_create(&writer,NULL,user_write,NULL));wait_flag(&writer_wait);
  CHECK(!atomic_load(&writer_done) && *(uint32_t *)memory==snapshot);
  atomic_store(&observation_end,1);CHECK(!pthread_join(observer,NULL));CHECK(!pthread_join(writer,NULL));
  nvn_dirty_test_gpu_wait_hook(NULL,NULL);
  CHECK(*(uint32_t *)memory==0xaabbccdd);CHECK(snapshot==0x12345678);
  CHECK(nvn_dirty_since(memory,4,captured_stamp)); // end-after-read store cannot be acknowledged
  nvn_dirty_scope_release(scope);CHECK(!munmap(memory,page*2));
  printf("{\"test\":\"dirty-image-CPU-observation-kernel-lease\",\"checks\":%u,\"original_pread_assertion_failed\":true,\"lease_before_scope_same_page_neighbor\":true,\"CPU_read_snapshot_stable\":true,\"post_end_store_remains_dirty\":true,\"GPU_executed\":false,\"result\":\"passed\"}\n",checks);
}
