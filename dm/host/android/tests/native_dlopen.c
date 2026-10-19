// 普通 dlopen 后在既有线程与新线程上运行原改写器夹具；不靠 LD_PRELOAD。
// Run the original rewriter fixture after ordinary dlopen, on an existing
// thread and new threads, without LD_PRELOAD.
#include <dlfcn.h>
#include <stdio.h>
#include <pthread.h>
#include <stdint.h>
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ready = PTHREAD_COND_INITIALIZER;
static int loaded;
static unsigned arrived;
static int (*probe)(void);
static void *existing_worker(void *unused) {
  (void)unused;
  pthread_mutex_lock(&mutex);
  ++arrived;
  pthread_cond_broadcast(&ready);
  while (!loaded) pthread_cond_wait(&ready, &mutex);
  pthread_mutex_unlock(&mutex);
  return (void *)(uintptr_t)probe();
}
int main(int argc, char **argv) {
  if (argc != 3) return 2;
  pthread_t workers[64];
  if (pthread_create(&workers[0], NULL, existing_worker, NULL)) return 1;
  pthread_mutex_lock(&mutex);
  while (!arrived) pthread_cond_wait(&ready, &mutex);
  pthread_mutex_unlock(&mutex);
  void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!library) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
  int (*test)(int, char **) = (int (*)(int, char **))dlsym(library, "dm_native_fixture_main");
  if (!test) { fprintf(stderr, "dlsym: %s\n", dlerror()); return 1; }
  char *args[] = {"native_tlsdesc_test", argv[2], NULL};
  if (test(2, args)) return 1;
  probe = (int (*)(void))dlsym(library, "dm_native_thread_probe");
  if (!probe) { fprintf(stderr, "dlsym probe: %s\n", dlerror()); return 1; }
  pthread_mutex_lock(&mutex); loaded = 1; pthread_cond_broadcast(&ready);
  pthread_mutex_unlock(&mutex);
  for (unsigned i = 1; i < 64; ++i)
    if (pthread_create(&workers[i], NULL, existing_worker, NULL)) return 1;
  for (unsigned i = 0; i < 64; ++i) {
    void *result;
    if (pthread_join(workers[i], &result) || result) return 1;
  }
  puts("{\"native_tlsdesc\":true,\"dlopen_without_preload\":true,\"preexisting_thread\":true,\"concurrent_thread_probes\":64,\"result\":\"passed\"}");
  return 0;
}
