#include <jni.h>
#include <android/fdsan.h>
#include <unistd.h>
#include <cstdio>
#include <string>
#include <vector>
#include "dm/gfx/vulkan/nvn_vk_loader.h"
#include "dm/host/android/stderr_redirect.h"
int owned_sfloat_probe_main(int,char **);
extern "C" JNIEXPORT jint JNICALL Java_com_d2rmobile_texelformatprobe_ProbeActivity_runProbe(
    JNIEnv *env,jclass,jstring root_arg,jstring hooks_arg,jint power,jlong chunks,jlong start,jboolean quiet,jboolean inject,jboolean full) {
  const char *r=env->GetStringUTFChars(root_arg,nullptr),*h=env->GetStringUTFChars(hooks_arg,nullptr);
  if(!r||!h)return 90;
  std::string root(r),hooks(h);env->ReleaseStringUTFChars(root_arg,r);env->ReleaseStringUTFChars(hooks_arg,h);
  std::string output=root+"/probe-output.log";
  int error=dm_android_redirect_stderr(output.c_str());if(error)return error;
  if(fileno(stdout)!=STDOUT_FILENO || fcntl(STDOUT_FILENO,F_GETFD)<0 || android_fdsan_get_owner_tag(STDOUT_FILENO))return 91;
  fflush(stdout);if(dup2(STDERR_FILENO,STDOUT_FILENO)<0)return 91;clearerr(stdout);setvbuf(stdout,nullptr,_IONBF,0);
  printf("{\"event\":\"app_entry\",\"uid\":%u,\"pid\":%u,\"driver_provider\":\"adreno\",\"game_modules_executed\":false}\n",getuid(),getpid());
  vk_loader_set_driver("adreno",(root+"/driver").c_str(),hooks.c_str());
  setenv("MESA_SHADER_CACHE_DIR",(root+"/shader-cache").c_str(),1);
  std::vector<std::string> options={"owned-sfloat-current-turnip","--chunk-pow",std::to_string(power),"--chunks",std::to_string(chunks),"--start-chunk",std::to_string(start),"--conversion",quiet?"quiet":"raw"};
  if(inject)options.push_back("--inject-error");if(full)options.push_back("--full");
  std::vector<char*> argv;for(auto &v:options)argv.push_back(v.data());
  int rc=owned_sfloat_probe_main((int)argv.size(),argv.data());
  printf("{\"event\":\"app_terminal\",\"rc\":%d,\"game_modules_executed\":false}\n",rc);return rc;
}
