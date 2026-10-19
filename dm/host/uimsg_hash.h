// UI 消息哈希反查，提取自原 diag.c 的 main 只读段扫描，不包含游戏字符串或内容。
// UI message hash lookup extracted from original diag.c's main read-only scan;
// contains no game strings or content.
#pragma once
#include "dm.h"
#define UIH_BITS 18
static struct {
  uint64_t h;
  const char *s;
} *g_uih;
static uint64_t fnv1a(const char *p, size_t n) {
  uint64_t h = 0xcbf29ce484222325ull;
  for (size_t i = 0; i < n; i++)
    h = (h ^ (uint8_t)p[i]) * 0x100000001b3ull;
  return h;
}
static void uih_build(void) {
  module_t *m = &g_mod[0];
  g_uih = calloc(1u << UIH_BITS, sizeof *g_uih);
  const char *p = (const char *)m->base + m->ro_off,
             *e = (const char *)m->base + m->data_off;
  while (p < e) {
    const char *q = p;
    while (q < e && *q >= 0x20 && *q < 0x7f)
      q++;
    if (q < e && !*q && q - p >= 3) {
      uint64_t h = fnv1a(p, q - p);
      unsigned i = (unsigned)(h >> (64 - UIH_BITS));
      while (g_uih[i].s && g_uih[i].h != h)
        i = (i + 1) & ((1u << UIH_BITS) - 1);
      g_uih[i].h = h;
      g_uih[i].s = p;
    }
    p = q + 1;
  }
}
static const char *uih_name(uint64_t h, char *buf) {
  if (g_uih)
    for (unsigned i = (unsigned)(h >> (64 - UIH_BITS)); g_uih[i].s;
         i = (i + 1) & ((1u << UIH_BITS) - 1))
      if (g_uih[i].h == h)
        return g_uih[i].s;
  snprintf(buf, 24, "%#llx", (unsigned long long)h);
  return buf;
}
