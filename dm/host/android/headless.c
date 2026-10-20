// 无图形引导的诊断与前端边界；系统服务和线程使用原 hle_*.c，不在这里伪造。
// Diagnostic/frontend boundaries for headless bring-up; original hle_*.c
// implements services and threads.
#include "dm.h"
#include "platform.h"
#include <signal.h>
#include <sys/ucontext.h>
#include <unistd.h>
struct dm_flags g_flag;
uint64_t g_t0_ns;
static pthread_key_t crash_key;
static pthread_once_t crash_once = PTHREAD_ONCE_INIT;
static void free_crash_stack(void *memory) {
  stack_t disabled = {.ss_flags = SS_DISABLE};
  sigaltstack(&disabled, NULL);
  free(memory);
}
static void crash_key_init(void) {
  if (pthread_key_create(&crash_key, free_crash_stack))
    die("crash TLS key");
}
#ifndef DM_NATIVE_FEATURES
static int (*fault_hooks[8])(void *, int, void *);
static unsigned fault_hook_count;
void dm_fault_hook_register(int (*fn)(void *, int, void *)) {
  if (fault_hook_count == 8)
    die("fault hook capacity");
  fault_hooks[fault_hook_count++] = fn;
}
#endif
static struct sigaction previous_actions[NSIG];
static pthread_once_t handlers_once = PTHREAD_ONCE_INIT;
static __thread int native_thread;
static void fault(int sig, siginfo_t *info, void *context) {
  ucontext_t *u = context;
  if (sig == SIGSEGV || sig == SIGBUS) {
    // ARM64 ESR 在保留区的 esr_context；WnR 位区分写保护缺页。
    // ARM64 ESR lives in the reserved esr_context; WnR distinguishes
    // write-protection faults.
    struct record {
      uint32_t magic, size;
    } *record = (void *)u->uc_mcontext.__reserved;
    uint64_t esr = 0;
    size_t left = sizeof u->uc_mcontext.__reserved;
    while (left >= sizeof *record && record->magic &&
           record->size >= sizeof *record && record->size <= left) {
      if (record->magic == 0x45535201 && record->size >= 16) {
        memcpy(&esr, (char *)record + 8, 8);
        break;
      }
      left -= record->size;
      record = (void *)((char *)record + record->size);
    }
#ifdef DM_NATIVE_FEATURES
    extern int crash_fault_dispatch(void *, int, void *);
    if (crash_fault_dispatch(info->si_addr, (esr >> 6) & 1, context)) return;
#else
    for (unsigned i = 0; i < fault_hook_count; i++)
      if (fault_hooks[i](info->si_addr, (esr >> 6) & 1, context))
        return;
#endif
  }
#ifdef DM_ANDROID_APP
  // ART 用缺页实现 Java 异常；非客体线程的此类信号必须交回原信号链。
  // ART uses page faults for Java exceptions; forward such signals on non-guest
  // threads to its original signal chain.
  if (!native_thread && (sig == SIGSEGV || sig == SIGBUS) &&
      (previous_actions[sig].sa_flags & SA_SIGINFO) &&
      previous_actions[sig].sa_sigaction &&
      previous_actions[sig].sa_sigaction != fault) {
    previous_actions[sig].sa_sigaction(sig, info, context);
    return;
  }
#endif
  char line[240];
  int n = snprintf(line, sizeof(line),
                   "native fault signal=%d address=%p pc=%#llx sp=%#llx\n", sig,
                   info->si_addr, (unsigned long long)u->uc_mcontext.pc,
                   (unsigned long long)u->uc_mcontext.sp);
  write(2, line,
        n > 0 ? ((size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1) : 0);
  module_t *m = module_of((void *)(uintptr_t)u->uc_mcontext.pc);
  n = snprintf(
      line, sizeof(line),
      "native location %s+%#llx LR=%#llx x0=%#llx x1=%#llx x2=%#llx\n",
      m ? m->name : "host",
      (unsigned long long)(u->uc_mcontext.pc - (m ? (uintptr_t)m->base : 0)),
      (unsigned long long)u->uc_mcontext.regs[30],
      (unsigned long long)u->uc_mcontext.regs[0],
      (unsigned long long)u->uc_mcontext.regs[1],
      (unsigned long long)u->uc_mcontext.regs[2]);
  write(2, line,
        n > 0 ? ((size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1) : 0);
  uint64_t fp = u->uc_mcontext.regs[29];
  for (int i = 0; i < 12 && fp && !(fp & 15); i++) {
    uint64_t saved[2];
    if (dm_self_read(saved, (void *)(uintptr_t)fp, 16) != 16)
      break;
    m = module_of((void *)(uintptr_t)saved[1]);
    {
      n = snprintf(
          line, sizeof(line), "native frame %s+%#llx\n", m ? m->name : "host",
          (unsigned long long)(saved[1] - (m ? (uintptr_t)m->base : 0)));
      write(2, line,
            n > 0 ? ((size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1)
                  : 0);
    }
    if (saved[0] <= fp)
      break;
    fp = saved[0];
  }
#ifdef DM_ANDROID_APP
  // 保留 debuggerd 的崩溃报告与 abort message，不能提前 _exit 丢失宿主调用栈。
  // Preserve debuggerd's crash report and abort message instead of losing
  // native stacks through an early _exit.
  if ((previous_actions[sig].sa_flags & SA_SIGINFO) &&
      previous_actions[sig].sa_sigaction &&
      previous_actions[sig].sa_sigaction != fault)
    previous_actions[sig].sa_sigaction(sig, info, context);
#endif
  _exit(128 + sig);
}
static void install_handlers(void) {
  struct sigaction a = {.sa_sigaction = fault,
                        .sa_flags = SA_SIGINFO | SA_ONSTACK};
  sigemptyset(&a.sa_mask);
  const int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGABRT, SIGALRM};
  for (unsigned i = 0; i < sizeof signals / sizeof *signals; i++)
    if (sigaction(signals[i], &a, &previous_actions[signals[i]]))
      die("install native crash handler");
#ifdef DM_NATIVE_FEATURES
  void crash_watch_start(void); crash_watch_start();
#endif
}
void crash_thread_init(void) {
  pthread_once(&crash_once, crash_key_init);
  if (pthread_getspecific(crash_key))
    return;
  stack_t s = {.ss_sp = malloc(65536), .ss_size = 65536};
  if (!s.ss_sp || sigaltstack(&s, NULL))
    die("sigaltstack");
  pthread_once(&handlers_once, install_handlers);
  native_thread = 1;
  if (pthread_setspecific(crash_key, s.ss_sp))
    die("crash TLS set");
}
#ifndef DM_NATIVE_FEATURES
void dm_backtrace(uint64_t frame) {
  for (int i = 0; i < 16 && frame && !(frame & 15); i++) {
    uint64_t saved[2];
    if (dm_self_read(saved, (void *)(uintptr_t)frame, 16) != 16)
      break;
    module_t *m = module_of((void *)(uintptr_t)saved[1]);
    if (m)
      LOG("native frame %s+%#llx", m->name,
          (unsigned long long)(saved[1] - (uint64_t)m->base));
    if (saved[0] <= frame)
      break;
    frame = saved[0];
  }
}
#endif
uint64_t front_game_res(int docked);
#ifndef DM_NATIVE_WORLD_CORE
uint64_t dm_game_res(int docked) { return front_game_res(docked); }
#endif
#ifndef DM_NATIVE_FEATURES
void crash_watch_frame(void) {}
#endif
__attribute__((weak)) void
uimsg_subscribe(void (*fn)(const char *, const char *, const char *)) {
  (void)fn;
  die("headless build has no UI message subscriptions");
}
#ifndef DM_NATIVE_FEATURES
void dm_prof_add(int id, uint64_t ns, uint64_t bytes) {
  (void)id;
  (void)ns;
  (void)bytes;
}

// 无图形外壳没有自定义页语言偏好；保留游戏已有设置。
// The headless shell has no custom-page language preference; retain existing
// game settings.
int dm_custom_lang_explicit(void) { return -1; }
#endif
#ifndef DM_NATIVE_FEATURES
static void (*write_hooks[8])(void *, size_t);
static unsigned write_hook_count;
void dm_guest_write_prep_register(void (*fn)(void *, size_t)) {
  if (write_hook_count == 8)
    die("guest write hook capacity");
  write_hooks[write_hook_count++] = fn;
}
void dm_guest_write_prepare(void *address, size_t size) {
  for (unsigned i = 0; i < write_hook_count; i++)
    write_hooks[i](address, size);
}
#include "../guest_write_lease.inc"
#endif

double front_display_refresh(char *name, size_t n) {
  if (n)
    name[0] = 0;
  return 0;
}

__attribute__((weak)) uint64_t dm_prof_ribbon_ns(void) { return 0; }

// 命令行没有输入窗口；APK 用强实现接 Android 输入框。
// The CLI has no input window; the APK overrides this with an Android dialog.
__attribute__((weak)) int front_text_input(const char *title, const char *sub,
                                           const char *guide,
                                           const char *initial, int max_chars,
                                           char *out, int out_cap) {
  (void)title;
  (void)sub;
  (void)guide;
  (void)initial;
  (void)max_chars;
  (void)out;
  (void)out_cap;
  return -1;
}
