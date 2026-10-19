// Android 命令行宿主：直接复用原装载器、HLE 和 ARM64 入口，启动顺序来自
// host/main.c。 Android command-line host: reuse the original loader, HLE and
// ARM64 entry; startup order follows host/main.c.
#include "dm.h"
#include "platform.h"
#include <unistd.h>

extern void guest_thread_init(void), crash_thread_init(void);
extern uint32_t svc_thread_main_init(void);
static void *required(const char *name) {
  void *p = guest_sym(name);
  if (!p)
    die("missing symbol %s", name);
  return p;
}
static void run0(const char *name) {
  LOG("native >>> %s", name);
  ((void (*)(void))required(name))();
  LOG("native <<< %s", name);
}
int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: dmhost_native DIR --entry SYMBOL [modules...] | "
                    "DIR --sdk-init|--constructors|--run [modules...]\n");
    return 2;
  }
  const char *dir = argv[1], *entry = NULL;
  int stage = !strcmp(argv[2], "--sdk-init")       ? 1
              : !strcmp(argv[2], "--constructors") ? 2
              : !strcmp(argv[2], "--run")          ? 3
                                                   : 0;
  int init = stage != 0, first = 3;
  if (!init) {
    if (argc < 4 || strcmp(argv[2], "--entry"))
      return 2;
    entry = argv[3];
    first = 4;
  } else
    first = 3;
  const char *names[MAX_MOD] = {"main", "sdk", "subsdk0"};
  int n = 3;
  if (argc > first) {
    n = argc - first;
    if (n > MAX_MOD)
      return 2;
    for (int i = 0; i < n; i++)
      names[i] = argv[first + i];
  }
  g_t0_ns = now_ns();
  g_flag.imports = getenv("DM_IMPORTS") != NULL;
  g_flag.svc = getenv("DM_SVC") != NULL;
  g_flag.thr = getenv("DM_THR") != NULL;
  g_flag.dbg = 1;
  g_flag.fs = getenv("DM_FS") != NULL;
  g_flag.movie = getenv("DM_MOVIE") != NULL;
  g_flag.bootprof = getenv("DM_BOOTPROF") != NULL;
  crash_thread_init();
  load_modules(dir, names, n);
  extern void dm_android_ui_init(void) __attribute__((weak));
  if (stage >= 3 && dm_android_ui_init) dm_android_ui_init();
#ifdef DM_NATIVE_FEATURES
  if (stage >= 3) {
    void tact_chk_init(void), tact_prefetch_start(const char *);
    void crash_set_dump_hook(void (*)(void)), svc_thread_dump(void);
    crash_set_dump_hook(svc_thread_dump);
    tact_chk_init(); tact_prefetch_start(dir);
    void loader_hash_log(void); loader_hash_log();
  }
#endif
  guest_thread_init();
#ifdef DM_NATIVE_FEATURES
  if (stage >= 3) {
    void inflate_bench_maybe(void), crash_sample_self(void);
    inflate_bench_maybe(); crash_sample_self();
  }
#endif
  uint32_t hmain = svc_thread_main_init();
  if (entry) {
    LOG("native entry %s", entry);
    uint64_t value = ((uint64_t (*)(void))required(entry))();
    printf("{\"backend\":\"native-arm64\",\"entry\":\"%s\",\"value\":%llu}\n",
           entry, (unsigned long long)value);
  } else {
    run0("_init_libc0");
    LOG("native >>> nnosInitialize");
    ((void (*)(uint64_t, uint64_t))required("nnosInitialize"))(hmain, 0);
    LOG("native <<< nnosInitialize");
    run0("_init_libc1");
    run0("nninitInitializeSdkModule");
    run0("nninitStartup");
    run0("_init_libc2");
    if (stage >= 2)
      run_module_inits();
    if (stage >= 3) {
      static char *game_argv[] = {"D2R", NULL};
      ((void (*)(int))required("_ZN2nn2os11SetHostArgcEi"))(1);
      ((void (*)(char **))required("_ZN2nn2os11SetHostArgvEPPc"))(game_argv);
#ifdef DM_NATIVE_FEATURES
      // 与原 main.c guest_main 相同；必须在建面板前选定原触屏布局。
      // Same order as original guest_main: choose touch layouts before the
      // game constructs any panels.
      void dm_guest_boot_configure(void), dm_guest_spawn_selftest(void), dm_padmode_boot(void);
      dm_guest_boot_configure();
      dm_guest_spawn_selftest();
      dm_padmode_boot();
#endif
      run0("nnMain");
    }
    printf("{\"backend\":\"native-arm64\",\"stage\":\"%s\"}\n",
           stage == 1   ? "sdk-initialized"
           : stage == 2 ? "constructors-completed"
                        : "nnMain-returned");
  }
  fflush(NULL);
  _exit(0);
}
