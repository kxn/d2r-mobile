#ifndef DM_NATIVE_FEATURES
// 原 diag.c 的 UI 观测门（main+0xb1dc60）与订阅接口，使用 ELF 安全读。
// Original diag.c UI observation gate (main+0xb1dc60) and subscription
// interface, with safe ELF self-reads.
#include "dm.h"
#include "platform.h"
#include "uimsg_hash.h"
#include <stdatomic.h>
static pthread_once_t hash_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t ui_lock = PTHREAD_MUTEX_INITIALIZER;
static void (*subscribers[8])(const char *, const char *, const char *);
static _Atomic unsigned subscriber_count;
static _Atomic uint64_t ribbon_ns;
static int logging;
static FILE *output;
static unsigned sequence;
uint64_t dm_prof_ribbon_ns(void) { return atomic_load(&ribbon_ns); }
void uimsg_emit(const char *category, const char *action,
                const char *argument) {
  for (unsigned i = 0, n = atomic_load(&subscriber_count); i < n; i++)
    subscribers[i](category, action, argument);
}
static void json_string(FILE *f, const char *s) {
  fputc('"', f);
  for (; *s; s++) {
    if (*s == '"' || *s == '\\')
      fputc('\\', f);
    fputc(*s, f);
  }
  fputc('"', f);
}
static void observe(const uint64_t *registers) {
  uint64_t fields[3];
  if (dm_self_read(fields, (const void *)(uintptr_t)registers[1],
                   sizeof fields) != sizeof fields)
    return;
  pthread_once(&hash_once, uih_build);
  char category_buffer[24], action_buffer[24], argument[65] = {0};
  int have_argument = 0;
  if (fields[2]) {
    long n = dm_self_read(argument, (const void *)(uintptr_t)fields[2], 64);
    if (n > 0) {
      argument[n] = 0;
      size_t i = 0;
      for (; i < (size_t)n && argument[i]; i++)
        if ((uint8_t)argument[i] < 0x20 || (uint8_t)argument[i] >= 0x7f)
          break;
      have_argument = i < (size_t)n && !argument[i];
    }
  }
  const char *category = uih_name(fields[0], category_buffer),
             *action = uih_name(fields[1], action_buffer);
  if (!strcmp(category, "PanelManager") && !strcmp(action, "OpenPanel") &&
      have_argument && !strcmp(argument, "MainMenuButtonRibbon")) {
    uint64_t zero = 0;
    atomic_compare_exchange_strong(&ribbon_ns, &zero, now_ns());
  }
  uimsg_emit(category, action, have_argument ? argument : "");
  if (!logging)
    return;
  pthread_mutex_lock(&ui_lock);
  if (output) {
    fprintf(
        output,
        "{\"seq\":%u,\"cat_h\":\"0x%016llx\",\"act_h\":\"0x%016llx\",\"arg\":",
        sequence++, (unsigned long long)fields[0],
        (unsigned long long)fields[1]);
    if (have_argument)
      json_string(output, argument);
    else
      fputs("null", output);
    fputs(",\"cat\":", output);
    json_string(output, category);
    fputs(",\"act\":", output);
    json_string(output, action);
    fputs("}\n", output);
    fflush(output);
  }
  static uint64_t previous[2];
  static char previous_argument[65];
  if (previous[0] != fields[0] || previous[1] != fields[1] ||
      strcmp(previous_argument, have_argument ? argument : "")) {
    LOG("UIMSG [%.2fs] %s %s %s", (now_ns() - g_t0_ns) / 1e9, category, action,
        have_argument ? argument : "");
    previous[0] = fields[0];
    previous[1] = fields[1];
    strlcpy(previous_argument, have_argument ? argument : "",
            sizeof previous_argument);
  }
  pthread_mutex_unlock(&ui_lock);
}
void uimsg_subscribe(void (*function)(const char *, const char *,
                                      const char *)) {
  if (!gate_obs_present(OBS_UIMSG))
    die("Original UI observation gate missing");
  pthread_mutex_lock(&ui_lock);
  unsigned count = atomic_load(&subscriber_count);
  if (count == 8)
    die("UI subscriber capacity");
  subscribers[count] = function;
  atomic_store(&subscriber_count, count + 1);
  gate_obs_set(OBS_UIMSG, observe);
  pthread_mutex_unlock(&ui_lock);
}
void dm_android_ui_init(void) {
  extern void dm_android_game_state_init(void);
  dm_android_game_state_init();
  extern void dm_android_touch_ui_init(void) __attribute__((weak));
  if (dm_android_touch_ui_init)
    dm_android_touch_ui_init();
  const char *path = getenv("DM_UIMSG");
  logging = path != NULL;
  if (!logging)
    return;
  if (strcmp(path, "1")) {
    output = fopen(path, "w");
    if (!output)
      die("UI message output open");
  }
  if (!gate_obs_present(OBS_UIMSG))
    die("Original UI observation gate missing");
  gate_obs_set(OBS_UIMSG, observe);
}

#else
#include "dm.h"
void dm_android_ui_init(void) {
    void probe_install(void), ticks_install(void), hid_keys_init(void);
    void dm_android_game_state_init(void), dm_android_touch_ui_init(void);
    void target_rank_install(void);
    probe_install();
    ticks_install();
    target_rank_install();
    hid_keys_init();
    dm_android_game_state_init();
    dm_android_touch_ui_init();
}
#endif
