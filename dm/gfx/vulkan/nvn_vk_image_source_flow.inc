// One operation owns these logical source values across CPU publication and
// native Pool admission. Do not cache a physical object, dirty/readiness proof
// or GPU producer. Nested ghost/draw scopes never overwrite a parent's values.
#ifdef DM_VULKAN_NATIVE_RESOURCES
#include "../nvn/nvn_native_layout.h"
enum { IMAGE_SOURCE_DEPTH=8,IMAGE_SOURCE_MAX=5*128 };
typedef struct {
  vk_texture_source_value source;
  unsigned short reg;
} image_source_entry;
_Static_assert(sizeof(vk_texture_source_value)==160,"Operation image source value");
_Static_assert(sizeof(image_source_entry)==168,"Operation image source entry");
typedef struct {const vk_shader *shader;unsigned begin,count;} image_source_stage;
typedef struct {
  uint64_t epoch,version,dirty[6][2],cpu[6][2],native[6][2];
  unsigned short slots[6][128]; // immutable reg -> existing entry ordinal, not another value directory
  int valid;
} image_source_partition;
static int source_partition_enabled=-1;
static uint64_t source_partition_version=1;
typedef struct {
  image_source_partition *partition;
  image_source_entry *entries;unsigned capacity,count,expected;int valid,compute,allocation_failed;
  image_source_stage stages[6];
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  uint64_t test_generation;int test_collecting;
#endif
} image_source_plan;
static image_source_plan image_source_plans[IMAGE_SOURCE_DEPTH];
static _Thread_local unsigned image_source_depth;
void vk_shader_image_sources_changed(unsigned stage,unsigned first,unsigned count,const uint64_t *values) {
  if(source_partition_enabled!=1 || stage>=6 || first>128 || count>128-first)return;
  image_source_partition *p=image_source_plans[0].partition;
  for(unsigned n=0;n<count;n++)if((S.tex[stage][first+n]&0xfffff)!=(values[n]&0xfffff)) {
    unsigned slot=first+n;if(p)p->dirty[stage][slot/64]|=UINT64_C(1)<<(slot%64);
    if(!++source_partition_version)die("Image source partition version overflow");
  }
}
void vk_shader_image_sources_invalidate(void) {
  if(source_partition_enabled!=1)return;
  if(image_source_plans[0].partition)image_source_plans[0].partition->valid=0;
  if(!++source_partition_version)die("Image source partition version overflow");
}

#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
static int image_source_owned_test;
static uint64_t image_source_test_generation;
void vk_test_image_source_flow(int active) {image_source_owned_test=!!active;}
#endif
static int image_sources_enabled(void) {
  return V.options.pool_window_bytes && (vk_native_resources_active()
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
      || image_source_owned_test
#endif
      );
}
int vk_shader_image_sources_begin(void) {
  if(source_partition_enabled<0) {
    const char *v=getenv("DM_VULKAN_SOURCE_PARTITION");
    if(v && strcmp(v,"0") && strcmp(v,"1"))die("DM_VULKAN_SOURCE_PARTITION expects 0 or 1");
    source_partition_enabled=v && !strcmp(v,"1");
    LOG("[vulkan] image source partition enabled=%d",source_partition_enabled);
  }
  if(image_source_depth)vk_shader_image_sources_invalidate();
  if(!image_sources_enabled() && !image_source_depth)return 0;
  unsigned depth=image_source_depth++;
  if(depth<IMAGE_SOURCE_DEPTH) {
    image_source_plans[depth].valid=0;
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
    image_source_plans[depth].test_generation=++image_source_test_generation;
    image_source_plans[depth].test_collecting=0;
#endif
  }
  return (int)image_source_depth;
}
void vk_shader_image_sources_end(int token) {
  if(!token)return;
  if(token!=(int)image_source_depth)die("Vulkan image source operation scope");
  if(!image_source_depth)die("Vulkan image source operation underflow");
  unsigned depth=--image_source_depth;
  if(depth<IMAGE_SOURCE_DEPTH) {
    image_source_plans[depth].valid=0;
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
    image_source_plans[depth].test_collecting=0;
#endif
  }
}
static image_source_plan *image_sources_current(void) {
  if(!image_source_depth || image_source_depth>IMAGE_SOURCE_DEPTH)return NULL;
  image_source_plan *plan=&image_source_plans[image_source_depth-1];
  return plan->valid?plan:NULL;
}
static image_source_plan *image_sources_start(vk_shader *const list[6],const vk_state *state,int compute) {
  if(!image_sources_enabled() || state!=&S || !image_source_depth || image_source_depth>IMAGE_SOURCE_DEPTH)return NULL;
  image_source_plan *plan=&image_source_plans[image_source_depth-1];plan->valid=0;
  if(plan->partition)plan->partition->valid=0;
  unsigned count=0;
  for(unsigned stage=compute?5:0;stage<(compute?6:5);stage++)if(list[stage]) {
    const vk_shader *s=list[stage];
    if(s->stage!=stage || !s->sampled_source_schema || s->sampled_source_schema>129)return NULL;
    count+=s->sampled_source_schema-1;
  }
  if(!count || count>IMAGE_SOURCE_MAX)return NULL;
  if(count>plan->capacity) {
    if(plan->allocation_failed)return NULL;
    void *memory=realloc(plan->entries,(size_t)count*sizeof *plan->entries);
    if(!memory) {plan->allocation_failed=1;return NULL;} // retain the original path, never retry OOM each draw
    plan->entries=memory;
    // Logical values may outlive a scope, but only id+descriptor epoch may
    // admit them again. New storage has no such proof; initialize its ids.
    for(unsigned n=plan->capacity;n<count;n++)plan->entries[n].source.id=-1;
    plan->capacity=count;
  }
  plan->count=0;plan->expected=count;plan->compute=compute;
  for(unsigned stage=0;stage<6;stage++) {
    const vk_shader *s=stage>=(compute?5:0) && stage<(compute?6:5)?list[stage]:NULL;
    plan->stages[stage]=(image_source_stage){.shader=s};
  }
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  plan->test_collecting=1;
#endif
  return plan;
}
static void image_sources_finish(image_source_plan *plan,int complete) {
  if(!plan)return;
  for(unsigned stage=0;stage<6;stage++) {
    const image_source_stage *part=&plan->stages[stage];
    if(part->shader && part->count!=part->shader->sampled_source_schema-1)complete=0;
  }
  // Never expose a partially populated plan to callbacks during preflight.
  plan->valid=complete && plan->count==plan->expected;
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  plan->test_collecting=0;
#endif
}

#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
// Functional observer for owned call-argument and lifetime oracles only.
int vk_test_image_source_origin(const tex_t *texture,uint64_t *generation) {
  image_source_plan *plan=image_sources_current();
  if(!plan && image_source_depth && image_source_depth<=IMAGE_SOURCE_DEPTH) {
    image_source_plan *building=&image_source_plans[image_source_depth-1];
    if(building->test_collecting)plan=building;
  }
  if(!plan)return 0;
  for(unsigned n=0;n<plan->count;n++)if(&plan->entries[n].source.texture==texture) {
    *generation=plan->test_generation;return plan->entries[n].source.id+1;
  }
  return 0;
}
unsigned vk_test_image_source_live(void) {image_source_plan *plan=image_sources_current();return plan?plan->count:0;}
uint64_t vk_test_image_source_bytes(void) {
  uint64_t bytes=sizeof image_source_plans;
  for(unsigned n=0;n<IMAGE_SOURCE_DEPTH;n++)bytes+=(uint64_t)image_source_plans[n].capacity*sizeof(image_source_entry);
  return bytes;
}
#endif
static image_source_stage *image_sources_stage(image_source_plan *plan,const vk_shader *s,
    const vk_state *state,unsigned stage,int compute) {
  if(!plan || state!=&S || plan->compute!=compute || plan->stages[stage].shader!=s)return NULL;
  return &plan->stages[stage];
}
static const tex_t *image_source_resolve(image_source_entry *entry,const vk_state *state,unsigned stage) {
  // Read the actual handle at EACH consumer. A CPU-publication callback may
  // have re-entered NVN state or registration; a saved handle is not authority.
  uint64_t handle=state->tex[stage][entry->reg];
  return vk_texture_source_resolve(&entry->source,handle&0xfffff);
}
static int image_partition_supported(image_source_plan *plan,const vk_state *state,int compute) {
  return source_partition_enabled==1 && plan==&image_source_plans[0] && image_source_depth==1 &&
      state==&S && !compute && !V.options.pool_defer_publication;
}
static int image_partition_classify(image_source_plan *plan,unsigned stage,unsigned local) {
  image_source_partition *p=plan->partition;image_source_stage *part=&plan->stages[stage];
  image_source_entry *entry=&plan->entries[part->begin+local];
  const tex_t *t=image_source_resolve(entry,&S,stage);if(!t)return 0;
  uint64_t bit=UINT64_C(1)<<(local%64);unsigned word=local/64;
  p->cpu[stage][word]&=~bit;p->native[stage][word]&=~bit;
  if(t->has_layout && nvn_native_layout_is_marker(t->layout))p->native[stage][word]|=bit;
  else if(t->has_layout && t->mem && t->L.size)p->cpu[stage][word]|=bit;
  return 1;
}
static void image_partition_build(image_source_plan *plan,uint64_t epoch,uint64_t version) {
  if(!plan || !plan->valid || !image_partition_supported(plan,&S,plan->compute))return;
  if(!plan->partition) {plan->partition=calloc(1,sizeof *plan->partition);if(!plan->partition)return;}
  image_source_partition *p=plan->partition;p->valid=0;
  if(epoch!=vk_texture_descriptor_epoch() || version!=source_partition_version)return;
  memset(p->slots,0xff,sizeof p->slots);memset(p->cpu,0,sizeof p->cpu);memset(p->native,0,sizeof p->native);
  for(unsigned stage=0;stage<5;stage++) {
    image_source_stage *part=&plan->stages[stage];
    for(unsigned n=0;n<part->count;n++) {
      image_source_entry *entry=&plan->entries[part->begin+n];
      if(entry->reg>=128 || p->slots[stage][entry->reg]!=UINT16_MAX ||
          !entry->source.valid || entry->source.epoch!=epoch)return;
      p->slots[stage][entry->reg]=n;
      if(!image_partition_classify(plan,stage,n))return;
    }
  }
  if(epoch!=vk_texture_descriptor_epoch() || version!=source_partition_version)return;
  memset(p->dirty,0,sizeof p->dirty);p->epoch=epoch;p->version=version;p->valid=1;
}
static int image_partition_refresh(image_source_plan *plan,vk_shader *const list[6],const vk_state *state,int compute) {
  if(!plan || !image_partition_supported(plan,state,compute) || !plan->partition)return 0;
  image_source_partition *p=plan->partition;
  if(!p->valid || p->epoch!=vk_texture_descriptor_epoch())return 0;
  for(unsigned stage=0;stage<6;stage++) {
    const vk_shader *shader=stage<5?list[stage]:NULL;
    if(plan->stages[stage].shader!=shader || (shader && plan->stages[stage].count!=shader->sampled_source_schema-1))return 0;
  }
  uint64_t version=source_partition_version;
  for(unsigned stage=0;stage<5;stage++)for(unsigned word=0;word<2;word++) {
    uint64_t bits=p->dirty[stage][word];p->dirty[stage][word]=0;
    while(bits) {unsigned slot=word*64+__builtin_ctzll(bits);bits&=bits-1;
      unsigned local=p->slots[stage][slot];
      if(local!=UINT16_MAX && !image_partition_classify(plan,stage,local)) {p->valid=0;return 0;}
    }
  }
  if(p->epoch!=vk_texture_descriptor_epoch() || version!=source_partition_version) {p->valid=0;return 0;}
  p->version=version;return 1;
}
static int image_partition_cpu(vk_shader *const list[6],const vk_state *state,int compute) {
  image_source_plan *plan=&image_source_plans[0];
  if(!image_partition_refresh(plan,list,state,compute))return 0;
  image_source_partition *p=plan->partition;uint64_t version=source_partition_version;
  plan->valid=0; // callback may see only its own nested source scope
  for(unsigned stage=0;stage<5;stage++)for(unsigned word=0;word<2;word++) {
    uint64_t bits=p->cpu[stage][word];
    while(bits) {unsigned n=word*64+__builtin_ctzll(bits);bits&=bits-1;
      vk_texture_cpu_preflight(&plan->entries[plan->stages[stage].begin+n].source.texture);
      if(!p->valid || p->epoch!=vk_texture_descriptor_epoch() || version!=source_partition_version)return 0;
    }
  }
  for(int k=0;k<state->targets.n;k++)vk_texture_cpu_preflight(state->targets.color[k]);
  vk_texture_cpu_preflight(state->targets.depth);
  if(!p->valid || p->epoch!=vk_texture_descriptor_epoch() || version!=source_partition_version)return 0;
  plan->valid=1;
  static int reported;if(!reported) {reported=1;LOG("[vulkan] image source partition actual sparse CPU operation");}
  return 1;
}

#else
void vk_shader_image_sources_changed(unsigned stage,unsigned first,unsigned count,const uint64_t *values) {(void)stage;(void)first;(void)count;(void)values;}
void vk_shader_image_sources_invalidate(void) {}
int vk_shader_image_sources_begin(void) {return 0;}
void vk_shader_image_sources_end(int token) {(void)token;}
#endif
