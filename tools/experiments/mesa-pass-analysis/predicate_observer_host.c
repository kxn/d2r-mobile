// Functional contract fixture for the real observer include, not a profiler.
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <openssl/sha.h>
typedef uint64_t VkShaderModule;
typedef struct { uint64_t addr,size; } p_bind_buf;
typedef struct { unsigned op,and_mask,compare,bit; } predicate_word;
typedef struct { unsigned slot,offset,id,count;predicate_word predicates[4];VkShaderModule module; } vk_uniform_predicate;
typedef struct { char key[17];vk_uniform_predicate *predicate;VkShaderModule specialization_module;
  unsigned specialization_slot,specialization_offset; } vk_shader;
static struct { p_bind_buf ubo[6][16]; } S;
static atomic_int uniform_predicate_enabled;
static unsigned reads,picks;static int readable=1;static VkShaderModule original_module=123;
static uint32_t original_raw=1;
static void die(const char *format,...) { (void)format;exit(86); }
#define LOG(...) ((void)0)
#define CC_SHA256(data,size,out) SHA256(data,size,out)
static VkShaderModule vk_uniform_specialization_pick(vk_shader *s,uint32_t values[2]) {
  (void)s;picks++;values[0]=original_raw;values[1]=0;return original_module;
}
static int vk_pool_try_read(uint64_t address,size_t size,void *out) {
  assert(size==4);reads++;if(!readable)return 0;memcpy(out,(void *)(uintptr_t)address,size);return 1;
}
#include "../../../dm/gfx/vulkan/nvn_vk_uniform_predicate.inc"
static vk_shader create(const char *key,unsigned slot,unsigned offset) {
  vk_shader s={.specialization_module=123,.specialization_slot=0};memcpy(s.key,key,17);
  s.predicate=uniform_predicate_observe_allocate();s.predicate->slot=slot;s.predicate->offset=offset;
  s.predicate->count=2;s.predicate->predicates[0]=(predicate_word){2,16,0,0};
  s.predicate->predicates[1]=(predicate_word){2,2,0,1};
  uniform_predicate_observe_register(s.predicate,&s,"base",4,"recipe",6);
  assert(s.predicate->module==0);return s;
}
int main(int argc,char **argv) {
  assert(argc==2);
  assert(!vk_uniform_predicate_observation_enabled());
  if(!strcmp(argv[1],"off")) {
    assert(!uniform_predicate_observe_policy());setenv("DM_VULKAN_UNIFORM_PREDICATE_OBSERVE","1",1);
    assert(!uniform_predicate_observe_policy());char *json=vk_uniform_predicate_observation_json();puts(json);free(json);return 0;
  }
  if(!strcmp(argv[1],"conflict"))atomic_store(&uniform_predicate_enabled,1);
  assert(uniform_predicate_observe_policy());
  assert(vk_uniform_predicate_observation_enabled());
  uint32_t first=1,second=0;S.ubo[1][0]=(p_bind_buf){(uintptr_t)&first,4};
  S.ubo[1][1]=(p_bind_buf){(uintptr_t)&second,4};
  vk_shader s=create("0123456789abcdef",1,0);uint32_t values[2]={99,99};
  for(unsigned mask=0;mask<4;mask++) {
    second=(mask&1?16:0)|(mask&2?2:0);
    assert(uniform_predicate_observe_pick(&s,values)==123 && values[0]==1 && values[1]==0);
  }
  assert(reads==4 && picks==4);
  // Pending second word preserves the baseline and never invents a value.
  readable=0;assert(uniform_predicate_observe_pick(&s,values)==123);readable=1;
  assert(values[0]==1 && values[1]==0 && reads==5);
  // A baseline fallback is returned without even attempting the second read.
  original_module=77;assert(uniform_predicate_observe_pick(&s,values)==77 && reads==5);original_module=123;
  // Ready original/second aliases reuse the baseline word without a new read.
  vk_shader alias=create("1111111111111111",0,0);
  assert(uniform_predicate_observe_pick(&alias,values)==123 && reads==5);
  // Malformed/overflowing live ranges are not dereferenced for diagnostics.
  S.ubo[1][1]=(p_bind_buf){UINT64_MAX-1,4};s.predicate->offset=4;
  assert(uniform_predicate_observe_pick(&s,values)==123 && reads==5);
  S.ubo[1][1]=(p_bind_buf){(uintptr_t)&second,4};s.predicate->offset=0;
  // Tuple recording is bounded; aggregate mask counts retain all draws.
  vk_shader overflow=create("2222222222222222",1,0);
  for(unsigned n=0;n<17;n++) {original_raw=n;assert(uniform_predicate_observe_pick(&overflow,values)==123);}
  uniform_predicate_observer *o=(uniform_predicate_observer *)overflow.predicate;
  assert(o->recorded[UP_OBS_TUPLE_OVERFLOW]==1 && o->recorded[UP_OBS_READY]==17);
  char *before=vk_uniform_predicate_observation_json();assert(strstr(before,"\"ready\":0"));free(before);
  uniform_predicate_observe_publish();char *json=vk_uniform_predicate_observation_json();puts(json);free(json);
  // Process-lived production rows are explicitly reclaimed by this host test.
  uniform_predicate_observer *row=atomic_load(&uniform_predicate_observers);
  while(row){uniform_predicate_observer *next=row->next;free(row);row=next;}
  return 0;
}
