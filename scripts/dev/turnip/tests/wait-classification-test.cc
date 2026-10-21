#include <cassert>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <initializer_list>
#include <sys/ioctl.h>
#include <vulkan/vulkan_core.h>
#define __user
#include "msm_kgsl.h"
struct Step { int ret,error; uint64_t next_time_ns; uint32_t expected_timeout_ms; };
static Step steps[8]; static unsigned available,calls,logs;static uint64_t current_time_ns;
static int logged_error;static unsigned logged_context,logged_timestamp;
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"CHECK failed %s:%d: %s\n",__FILE__,__LINE__,#x);std::abort();}}while(0)
static uint64_t os_time_get_nano(){return current_time_ns;}
static int scripted_ioctl(int fd,unsigned long request,void *arg){
 CHECK(fd==17);CHECK(request==IOCTL_KGSL_DEVICE_WAITTIMESTAMP_CTXTID);CHECK(calls<available);
 auto*w=static_cast<kgsl_device_waittimestamp_ctxtid*>(arg);CHECK(w->context_id==15&&w->timestamp==6814);
 Step s=steps[calls++];CHECK(w->timeout==s.expected_timeout_ms);current_time_ns=s.next_time_ns;errno=s.error;return s.ret;
}
static void mesa_loge(const char*,unsigned context,unsigned timestamp,int error,const char*){
 logs++;logged_error=error;logged_context=context;logged_timestamp=timestamp;
 errno=EBADF; // Logging may change errno; classification must use captured error.
}
#define ioctl scripted_ioctl
#include WAIT_SOURCE_PATH
#undef ioctl
static void reset(){std::memset(steps,0,sizeof steps);available=calls=logs=0;current_time_ns=0;logged_error=0;logged_context=logged_timestamp=0;}
static void add(int ret,int error,uint64_t time,uint32_t timeout){CHECK(available<8);steps[available++]={ret,error,time,timeout};}
int main(){
#ifdef TEST_ORIGINAL_REFERENCE
 reset();add(-1,EPROTO,0,30);CHECK(wait_timestamp_safe(17,15,6814,30000000)==VK_TIMEOUT);CHECK(calls==1&&logs==0);
 std::puts("{\"test\":\"original-release-negative-reference\",\"EPROTO_wrongly_returns_TIMEOUT\":true,\"checks\":\"passed\"}");
#else
 unsigned cases=0;
 reset();add(0,0,0,30);CHECK(wait_timestamp_safe(17,15,6814,30000000)==VK_SUCCESS);CHECK(calls==1&&logs==0);cases++;
 reset();add(-1,ETIMEDOUT,30000000,30);CHECK(wait_timestamp_safe(17,15,6814,30000000)==VK_TIMEOUT);CHECK(calls==1&&logs==0);cases++;
 for(int error:{EPROTO,EDEADLK,EIO,EINVAL,EBADF,ENOMEM}){
  reset();add(-1,error,0,30);CHECK(wait_timestamp_safe(17,15,6814,30000000)==VK_ERROR_DEVICE_LOST);CHECK(calls==1&&logs==1&&logged_error==error&&logged_context==15&&logged_timestamp==6814);cases++;
 }
 reset();add(-1,EINTR,4000000,30);add(-1,EAGAIN,9000000,26);add(0,0,0,21);CHECK(wait_timestamp_safe(17,15,6814,30000000)==VK_SUCCESS);CHECK(calls==3&&logs==0);cases++;
 reset();add(-1,EAGAIN,30000000,30);CHECK(wait_timestamp_safe(17,15,6814,30000000)==VK_TIMEOUT);CHECK(calls==1&&logs==0);cases++;
 reset();add(-1,EINTR,1000000,30);add(-1,EPROTO,2000000,29);CHECK(wait_timestamp_safe(17,15,6814,30000000)==VK_ERROR_DEVICE_LOST);CHECK(calls==2&&logs==1&&logged_error==EPROTO);cases++;
 reset();add(-1,EPROTO,0,0);CHECK(wait_timestamp_safe(17,15,6814,0)==VK_ERROR_DEVICE_LOST);CHECK(calls==1&&logs==1);cases++;
 reset();add(-1,EINTR,4000000,UINT32_MAX);add(0,0,0,UINT32_MAX);CHECK(wait_timestamp_safe(17,15,6814,UINT64_MAX)==VK_SUCCESS);CHECK(calls==2&&logs==0);cases++;
 std::printf("{\"test\":\"exact-wait-timestamp-safe-scripted-ioctl\",\"cases\":%u,\"result\":\"passed\",\"sanitizers\":\"ASan+UBSan\"}\n",cases);
#endif
}
