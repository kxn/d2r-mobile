// Actual ARM ABI oracle for the production tail stubs. No SDK/game execution.
#include <stdatomic.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <assert.h>
_Atomic uintptr_t dm_opus_original_math[3] __attribute__((visibility("hidden")));
_Atomic unsigned dm_opus_core_dispatch_enabled __attribute__((visibility("hidden")));
uintptr_t dm_opus_tail_expected_sp __attribute__((visibility("hidden")));
uintptr_t dm_opus_tail_expected_x18 __attribute__((visibility("hidden")));
extern int dm_opus_tail_register_test(void),dm_opus_tail_oracle(void);
extern int dm_opus_ctl_entry(void *,int,...);
static unsigned selections;
void *dm_opus_math_select(unsigned kind,const uintptr_t args[8]) {
    assert(kind<3 && args);selections++;
    __asm__ volatile("movi v0.16b,#0\nmovi v1.16b,#0\nmovi v2.16b,#0\nmovi v3.16b,#0\nmovi v4.16b,#0\nmovi v5.16b,#0\nmovi v6.16b,#0\nmovi v7.16b,#0"
                     :::"v0","v1","v2","v3","v4","v5","v6","v7");
    uintptr_t target=atomic_load(&dm_opus_original_math[kind]);
    return target?(void *)target:(void *)dm_opus_tail_oracle;
}
static int original_varargs(void *state,int request,...) {
    assert(state==(void *)0x1234 && request==0x99);
    va_list list;va_start(list,request);
    for(unsigned i=0;i<10;i++)assert(va_arg(list,uint64_t)==UINT64_C(0x77110000)+i);
    assert(va_arg(list,double)==1.25 && va_arg(list,double)==-7.5);
    assert(va_arg(list,void *)==(void *)0xfeedbeef);va_end(list);return 123;
}
int main(void) {
    assert(dm_opus_tail_register_test());assert(selections==1); // lazily unresolved default
    atomic_store(&dm_opus_original_math[1],(uintptr_t)dm_opus_tail_oracle);
    atomic_store(&dm_opus_core_dispatch_enabled,0);
    assert(dm_opus_tail_register_test());assert(selections==1); // fast default
    atomic_store(&dm_opus_core_dispatch_enabled,1);
    assert(dm_opus_tail_register_test());assert(selections==2); // slow unowned
    atomic_store(&dm_opus_original_math[1],(uintptr_t)original_varargs);
    for(unsigned slow=0;slow<2;slow++) {
      atomic_store(&dm_opus_core_dispatch_enabled,slow);
      assert(dm_opus_ctl_entry((void *)0x1234,0x99,
        UINT64_C(0x77110000),UINT64_C(0x77110001),UINT64_C(0x77110002),UINT64_C(0x77110003),
        UINT64_C(0x77110004),UINT64_C(0x77110005),UINT64_C(0x77110006),UINT64_C(0x77110007),
        UINT64_C(0x77110008),UINT64_C(0x77110009),1.25,-7.5,(void *)0xfeedbeef)==123);
    }
    puts("ARM Opus tail ABI PASS: lazy/default/unowned, x0-x8, q0-q7 full128bits, originalSP>=9integerargs, x18, real Cvarargs10ints+2FP+pointer (not SDK conformance)");
}
