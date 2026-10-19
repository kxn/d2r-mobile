// 音频通知只适配宿主信号量，队列与游戏 SystemEvent 保持原实现。
// Adapt only host notification semaphores; retain original queues and guest
// SystemEvent handling.
#pragma once
#ifdef __APPLE__
#include <dispatch/dispatch.h>
typedef dispatch_semaphore_t dm_audio_sem;
static inline dm_audio_sem dm_audio_sem_create(void) {
  return dispatch_semaphore_create(0);
}
static inline void dm_audio_sem_signal(dm_audio_sem s) {
  dispatch_semaphore_signal(s);
}
static inline void dm_audio_sem_wait(dm_audio_sem s) {
  dispatch_semaphore_wait(s, DISPATCH_TIME_FOREVER);
}
static inline void dm_audio_sem_free(dm_audio_sem s) { dispatch_release(s); }
#else
#include <errno.h>
#include <semaphore.h>
#include <stdlib.h>
typedef sem_t *dm_audio_sem;
static inline dm_audio_sem dm_audio_sem_create(void) {
  sem_t *s = malloc(sizeof *s);
  if (!s || sem_init(s, 0, 0))
    abort();
  return s;
}
static inline void dm_audio_sem_signal(dm_audio_sem s) {
  if (sem_post(s))
    abort();
}
static inline void dm_audio_sem_wait(dm_audio_sem s) {
  while (sem_wait(s))
    if (errno != EINTR)
      abort();
}
static inline void dm_audio_sem_free(dm_audio_sem s) {
  sem_destroy(s);
  free(s);
}
#endif
