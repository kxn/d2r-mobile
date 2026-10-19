// 真 ARM64 上运行原改写器产物：原子窗口并发、TLS、栈及 HLE 强制接管。
// Run original rewriter output on ARM64 hardware: concurrent exclusive windows,
// TLS, stack and forced HLE.
#include "hle.h"
#include "crypto.h"
#include <signal.h>
#include <stdatomic.h>
#include <unistd.h>
extern void guest_thread_init(void), crash_thread_init(void);
extern uint32_t svc_thread_main_init(void);
static _Atomic unsigned override_calls, signals_seen;
HLE_FORCE("fixture_override",
          "原创测试：覆盖客体的错误实现 | Original test: override an "
          "intentionally wrong guest implementation",
          uint64_t, override, void) {
  atomic_fetch_add(&override_calls, 1);
  return 42;
}
static void signal_handler(int sig) {
  (void)sig;
  volatile unsigned char scratch[512];
  for (unsigned i = 0; i < sizeof(scratch); i++)
    scratch[i] = (unsigned char)i;
  atomic_fetch_add(&signals_seen, scratch[1]);
}
static void *worker(void *arg) {
  (void)arg;
  crash_thread_init();
  guest_thread_init();
  void (*increment)(void) = guest_sym("native_increment");
  for (unsigned i = 0; i < 100000; i++)
    increment();
  return NULL;
}
// 主 HUD 回调被替换时，附加日志观察者仍按顺序执行且不得改寄存器。
// Replacing the product HUD callback must retain diagnostic observers in
// registration order without changing the original gate registers.
void gate_register(uint64_t, const char *, int, uint64_t);
static unsigned observed_order;
static void observe_one(const uint64_t *r) { if (r[0] != 42) die("observation registers"); observed_order = observed_order * 10 + 1; }
static void observe_two(const uint64_t *r) { (void)r; observed_order = observed_order * 10 + 2; }
static void observe_three(const uint64_t *r) { (void)r; observed_order = observed_order * 10 + 3; }
static void observation_test(void) {
    const uint64_t ret = 0x1000000000000040ull;
    uint64_t regs[32] = {42}, before[32]; memcpy(before, regs, sizeof regs);
    gate_register(ret, "obs", OBS_TICK, 0);
    gate_obs_set(OBS_TICK, observe_one);
    gate_obs_add(OBS_TICK, observe_two);
    gate_obs_add(OBS_TICK, observe_two);
    gate_dispatch(ret, regs);
    if (observed_order != 12 || memcmp(regs, before, sizeof regs)) die("observation primary/subscriber");
    observed_order = 0;
    gate_obs_set(OBS_TICK, observe_three);
    gate_dispatch(ret, regs);
    if (observed_order != 32 || memcmp(regs, before, sizeof regs)) die("observation replacement lost subscriber");
    observed_order = 0;
    gate_obs_set(OBS_TICK, NULL);
    gate_dispatch(ret, regs);
    if (observed_order != 2) die("observation clearing lost subscriber");
}
static void digest_test(void) {
    const char *inputs[] = {"", "abc", "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"};
    const char *expected[] = {"da39a3ee5e6b4b0d3255bfef95601890afd80709", "a9993e364706816aba3e25717850c26c9cd0d89d", "84983e441c3bd26ebaae4aa1f95129e5e54670f1"};
    for (unsigned i = 0; i < 3; i++) {
        unsigned char digest[CC_SHA1_DIGEST_LENGTH]; char hex[41];
        if (CC_SHA1(inputs[i], strlen(inputs[i]), digest) != digest) die("SHA1 output pointer");
        for (unsigned j = 0; j < sizeof digest; j++) snprintf(hex + 2*j, 3, "%02x", digest[j]);
        if (strcmp(hex, expected[i])) die("original UI SHA1 guard known vector %u", i);
    }
}
#ifdef DM_TLS_DESC
// 由 dlopen 前就存在的线程调用，也用于多线程并发隔离验证。
// Called on a thread that predates dlopen, and concurrently for isolation.
__attribute__((visibility("default"))) int dm_native_thread_probe(void) {
  crash_thread_init();
  guest_thread_init();
  void *tls = dm_tsd_get(g_keytls), *tlr = dm_tsd_get(g_keytlr);
  uintptr_t before, after;
  __asm__ volatile("mov %0,x18" : "=r"(before));
  uint64_t result = ((uint64_t (*)(void))guest_sym("nnMain"))();
  __asm__ volatile("mov %0,x18" : "=r"(after));
  return result == 42 && tls == dm_tsd_get(g_keytls) &&
         tlr == dm_tsd_get(g_keytlr) && before == after ? 0 : 1;
}
#endif
#include "native_thread_exit.inc"
#include "native_host_stack.inc"
int main(int argc, char **argv) {
  int exit_main=argc==3&&!strcmp(argv[2],"--thread-main-exit");
  if (argc != 2&&!exit_main)
    return 2;
  const char *modules[] = {"fixture"};
  observation_test();
  digest_test();
  crash_thread_init();
  load_modules(argv[1], modules, 1);
  guest_thread_init();
  svc_thread_main_init();
  if(exit_main)native_thread_exit_main_test();
  native_thread_exit_test();
  native_host_stack_test();
  uintptr_t host18_before, host18_after;
  __asm__ volatile("mov %0,x18" : "=r"(host18_before));
  void *tls = dm_tsd_get(g_keytls), *tlr = dm_tsd_get(g_keytlr);
  uint64_t value = ((uint64_t (*)(void))guest_sym("nnMain"))();
  __asm__ volatile("mov %0,x18" : "=r"(host18_after));
  if (value != 42 || override_calls != 1 || tls != dm_tsd_get(g_keytls) ||
      tlr != dm_tsd_get(g_keytlr) || host18_before != host18_after)
    die("native ABI fixture failed: value=%#llx", value);
  struct sigaction a = {.sa_handler = signal_handler};
  sigemptyset(&a.sa_mask);
  if (sigaction(SIGUSR1, &a, NULL))
    die("sigaction test");
  pthread_t threads[2];
  for (unsigned i = 0; i < 2; i++)
    if (pthread_create(&threads[i], NULL, worker, NULL))
      die("pthread_create test");
  for (unsigned i = 0; i < 1000; i++) {
    pthread_kill(threads[0], SIGUSR1);
    pthread_kill(threads[1], SIGUSR1);
  }
  for (unsigned i = 0; i < 2; i++)
    pthread_join(threads[i], NULL);
  uint64_t count = *(uint64_t *)guest_sym("native_count");
  if (count != 200000 || !signals_seen)
    die("native atomic/signal test failed: count=%llu signals=%u", count,
        signals_seen);
  printf(
      "{\"backend\":\"native-arm64\",\"result\":\"passed\",\"atomic_"
      "increments\":%llu,\"signal_deliveries\":%u,\"hle_override_calls\":%u,\"observation_composition_checks\":3,\"sha1_known_vectors\":3}\n",
      (unsigned long long)count, signals_seen, override_calls);
  return 0;
}
// 夹具不含游戏私有门；意外进入即失败。 | The fixture has no private game gates;
// unexpected entry fails.
uint64_t svc_host_call(int num, uint64_t *regs) {
  (void)regs;
  die("unexpected fixture host gate %#x", num);
}
