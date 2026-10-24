// Linux 图形验证夹具：调用原 NVN C 实现与同一套 GPU 测试，不装载或执行任何游戏 CPU 代码。
// Linux graphics validation fixture: call original NVN C implementations and the same GPU tests, without loading or executing game CPU code.
#include "../../nvn/hle_nvn.c"
#include <signal.h>
void *nvn_thunk[NVN_NFUNC];
const char *nvn_names[NVN_NFUNC]={
#include "nvn_test_names.inc"
};
void nvn_unimpl_entry(void) { die("unimplemented NVN function in graphics fixture"); }
void nvn_prof_entry(void) { die("profiling thunk unavailable in graphics fixture"); }
__attribute__((constructor)) static void direct_calls(void) {
  nvn_init_slots();for(unsigned i=0;i<NVN_NFUNC;i++)nvn_thunk[i]=nvn_slot[i].fn;
}
uint64_t g_t0_ns;
uint64_t now_ns(void) { return dm_platform_clock_ns(CLOCK_MONOTONIC); }
uint64_t guest_ns(void) { return now_ns(); }
int dm_detclock_enabled(void) { return 0; }
void dm_guest_frame_tick(void) {}
#ifndef DM_GUEST_HEAP_GPU_TESTS
int dm_guest_heap_stat(unsigned long long *total,unsigned long long *free_bytes,unsigned long long *largest) { *total=*free_bytes=*largest=0;return 0; }
#endif
double front_display_refresh(char *name,size_t size) { if(size)name[0]=0;return 0; }
static int (*fault_handlers[8])(void *,int,void *);
static unsigned fault_count;
static void (*write_handlers[8])(void *,size_t);
static unsigned write_count;
void dm_fault_hook_register(int (*fn)(void *,int,void *)) { if(fault_count==8)die("fixture fault hooks");fault_handlers[fault_count++]=fn; }
void dm_guest_write_prep_register(void (*fn)(void *,size_t)) { if(write_count==8)die("fixture write hooks");write_handlers[write_count++]=fn; }
void dm_guest_write_prepare(void *ptr,size_t size) { for(unsigned i=0;i<write_count;i++)write_handlers[i](ptr,size); }
#include "../../../host/guest_write_lease.inc"
static void fault_handler(int signal,siginfo_t *info,void *context) {
  for(unsigned i=0;i<fault_count;i++)if(fault_handlers[i](info->si_addr,info->si_code==SEGV_ACCERR,context))return;
  _exit(128+signal);
}
void crash_thread_init(void) {
  struct sigaction action={.sa_sigaction=fault_handler,.sa_flags=SA_SIGINFO};sigemptyset(&action.sa_mask);sigaction(SIGSEGV,&action,NULL);
}
#include "../../../host/android/tests/native_vulkan.c"
#ifdef DM_VULKAN_TEST_EMBED
#include "uab_parameters.inc"
#include "pixel_local_loader.inc"
#include "quad_transfer_contract.inc"
#include "quad_readonly_contract.inc"
#ifdef DM_VULKAN_NATIVE_RESOURCES
#include "native_layout.inc"
#endif
#ifdef DM_VULKAN_RETAINED_REASONS
extern void vk_retained_diagnostic_publish(uint64_t);
static void diagnostic_final(void) { vk_retained_diagnostic_publish(0); }
#endif
int main(void) {
#ifdef DM_VULKAN_RETAINED_REASONS
  atexit(diagnostic_final); // guest=0 is host fixture final snapshot only
#endif
  if(getenv("DM_DESCRIPTOR_COMPLETED_MUTATION_FIXTURE")){
    extern unsigned vk_test_descriptor_completed_mutation(void);
    crash_thread_init();mtl_init();unsigned n=vk_test_descriptor_completed_mutation();
    printf("{\"test\":\"descriptor-completed-mutation\",\"checks\":%u,\"game_modules_executed\":false,\"result\":\"passed\"}\n",n);return 0;
  }
  if(getenv("DM_UAB_PARAMETERS_FIXTURE")){crash_thread_init();mtl_init();return uab_parameters_test();}
#ifdef DM_VULKAN_NATIVE_RESOURCES
  const char *table=getenv("DM_NATIVE_LAYOUT_METADATA_FIXTURE");
  if(table)return native_layout_test(table);
#endif
  if(getenv("DM_QUAD_READONLY_CONTRACT_FIXTURE"))return quad_readonly_contract_test();
  if(getenv("DM_QUAD_TRANSFER_CONTRACT_FIXTURE"))return quad_transfer_contract_test();
  if(getenv("DM_PIXEL_LOCAL_LOADER_FIXTURE")) {crash_thread_init();mtl_init();return pixel_local_loader_test();}
  return dm_vulkan_test_main();
}
#endif
