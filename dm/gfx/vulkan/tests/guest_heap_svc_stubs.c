// Only used by the optional owned whole-AS GPU fixture. Memory SVC comes from
// real hle_svc.c; unrelated guest services are not executed in this host test.
#include "dm.h"
module_t g_mod[MAX_MOD];int g_nmod;struct dm_flags g_flag;
int svc_thread(int num,uint64_t *r) {(void)num;(void)r;return 0;}
uint64_t svc_host_call(int num,uint64_t *r) {(void)num;(void)r;return 0;}
uint64_t dm_entropy64(void) {return 0;}
void hlecount_svc(int num) {(void)num;}
void dm_backtrace(uint64_t fp) {(void)fp;}
module_t *module_of(const void *p) {(void)p;return NULL;}

#include "guest_heap_startup.h"
extern int vk_pool_shared_mode(void);
int dm_guest_heap_startup_for_fixture(int requested) {
    return dm_guest_heap_startup_freeze(requested,vk_pool_shared_mode);
}
