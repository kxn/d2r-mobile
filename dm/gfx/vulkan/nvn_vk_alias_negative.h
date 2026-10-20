// Owner-thread geometry proof only. No image contents, eligible flags, node
// pointers or GPU visibility are cached. Membership changes invalidate it.
#ifndef DM_NVN_VK_ALIAS_NEGATIVE_H
#define DM_NVN_VK_ALIAS_NEGATIVE_H
#include "nvn_vk_alias_interval.h"
enum { ALIAS_NEGATIVE_SLOTS=32 };
typedef struct { uint64_t epoch,start,end; } alias_negative_entry;
typedef struct {
  uint64_t epoch; // zero permanently disables caching after wraparound
  alias_negative_entry entries[ALIAS_NEGATIVE_SLOTS];
} alias_negative_cache;
static void alias_negative_changed(alias_negative_cache *c) {
  if(c->epoch)c->epoch++;
}
static unsigned alias_negative_slot(uint64_t start,uint64_t end) {
  return (unsigned)((start>>4)^(start>>20)^end)&(ALIAS_NEGATIVE_SLOTS-1);
}
static int alias_negative_matches(const alias_negative_cache *c,uint64_t start,uint64_t end) {
  const alias_negative_entry *e=&c->entries[alias_negative_slot(start,end)];
  return c->epoch && e->epoch==c->epoch && e->start==start && e->end==end;
}
static int alias_geometry_eligible(const alias_range_node *n,int ignored) {
  (void)n;(void)ignored;return 1;
}
// Only a range with NO geometric node overlap may enter the cache. A node
// that is currently uninitialized/ineligible must retain the original query:
// those live flags can change without changing tree membership.
static int alias_negative_overlap(alias_negative_cache *c,const alias_range_node *root,
    uint64_t start,uint64_t end,int (*eligible)(const alias_range_node *,int),int shared) {
  if(alias_negative_matches(c,start,end))return 0;
  if(alias_range_overlap(root,start,end,alias_geometry_eligible,0))
    return alias_range_overlap(root,start,end,eligible,shared);
  if(c->epoch)c->entries[alias_negative_slot(start,end)]=(alias_negative_entry){c->epoch,start,end};
  return 0;
}
#endif
