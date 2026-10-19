/* SPDX-License-Identifier: GPL-2.0 */
/* Diagnostic only. Read-only GED QUERY_INFO ABI from MediaTek's GPL headers:
 * OnePlusOSS/android_kernel_5.10_oneplus_mt6983, branch
 * oneplus/mt6983_t_13.0_oneplus_ace_2v, drivers/gpu/mediatek/ged/include/.
 * Never send boost, event, frequency or target-PID setters. Hardware max below
 * is not the effective ceiling counter observed in Perfetto.
 */
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
struct GED_BRIDGE_PACKAGE {
  uint32_t ui32FunctionID;int32_t i32Size;void *pvParamIn;
  int32_t i32InBufferSize;void *pvParamOut;int32_t i32OutBufferSize;
};
int main(void) {
  int fd=open("/proc/ged",O_RDONLY|O_CLOEXEC);
  if(fd<0) { perror("open /proc/ged");return 1; }
  const unsigned selectors[]={0,5,6,8,10,13,14};
  const char *names[]={"gpu_loading_percent","current_frequency_khz","current_opp_index",
    "hardware_max_frequency_khz","hardware_min_frequency_khz","event_status_raw","debug_status_raw"};
  puts("{");
  for(unsigned i=0;i<sizeof selectors/sizeof *selectors;i++) {
    uint32_t input=selectors[i];uint64_t output=UINT64_MAX;
    struct GED_BRIDGE_PACKAGE p={_IOWR('g',5,struct GED_BRIDGE_PACKAGE),sizeof p,&input,sizeof input,&output,sizeof output};
    if(ioctl(fd,p.ui32FunctionID,&p)) { perror("GED QUERY_INFO");close(fd);return 2; }
    printf("  \"%s\":%llu,\n",names[i],(unsigned long long)output);
  }
  // The documented query's three control inputs remain zero: it must not set
  // a target PID or GIFT hint. Oversized zeroed payloads retain space for ABI
  // additions; only the eight documented output integers are interpreted.
  int32_t input[1024]={0},output[1024]={0};
  struct GED_BRIDGE_PACKAGE p={_IOWR('g',16,struct GED_BRIDGE_PACKAGE),sizeof p,input,sizeof input,output,sizeof output};
  if(ioctl(fd,p.ui32FunctionID,&p)) { perror("GED query DVFS info");close(fd);return 3; }
  printf("  \"dvfs_query\":{\"error\":%d,\"current_frequency_khz\":%d,\"effective_ceiling_khz\":%d,\"prediction_khz\":%d,\"target_fps\":%d,\"gpu_time_us\":%d}\n",output[0],output[1],output[2],output[3],output[4],output[7]);
  puts("}");close(fd);
}
