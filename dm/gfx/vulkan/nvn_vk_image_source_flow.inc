// One operation owns these logical source values across CPU publication and
// native Pool admission. Do not cache a physical object, dirty/readiness proof
// or GPU producer. Nested ghost/draw scopes never overwrite a parent's values.
#ifdef DM_VULKAN_NATIVE_RESOURCES
enum { IMAGE_SOURCE_DEPTH=8,IMAGE_SOURCE_MAX=5*128 };
typedef struct {
  vk_texture_source_value source;
  unsigned short reg;
} image_source_entry;
_Static_assert(sizeof(vk_texture_source_value)==160,"Operation image source value");
_Static_assert(sizeof(image_source_entry)==168,"Operation image source entry");
typedef struct {const vk_shader *shader;unsigned begin,count;} image_source_stage;
typedef struct {
  image_source_entry *entries;unsigned capacity,count;int valid,compute,allocation_failed;
  image_source_stage stages[6];
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  uint64_t test_generation;
#endif
} image_source_plan;
static image_source_plan image_source_plans[IMAGE_SOURCE_DEPTH];
static _Thread_local unsigned image_source_depth;
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
  if(!image_sources_enabled() && !image_source_depth)return 0;
  unsigned depth=image_source_depth++;
  if(depth<IMAGE_SOURCE_DEPTH) {
    image_source_plans[depth].valid=0;
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
    image_source_plans[depth].test_generation=++image_source_test_generation;
#endif
  }
  return (int)image_source_depth;
}
void vk_shader_image_sources_end(int token) {
  if(!token)return;
  if(token!=(int)image_source_depth)die("Vulkan image source operation scope");
  if(!image_source_depth)die("Vulkan image source operation underflow");
  unsigned depth=--image_source_depth;
  if(depth<IMAGE_SOURCE_DEPTH)image_source_plans[depth].valid=0;
}
static image_source_plan *image_sources_current(void) {
  if(!image_source_depth || image_source_depth>IMAGE_SOURCE_DEPTH)return NULL;
  image_source_plan *plan=&image_source_plans[image_source_depth-1];
  return plan->valid?plan:NULL;
}
static image_source_plan *image_sources_build(vk_shader *const list[6],const vk_state *state,int compute) {
  if(!image_sources_enabled() || state!=&S || !image_source_depth || image_source_depth>IMAGE_SOURCE_DEPTH)return NULL;
  image_source_plan *plan=&image_source_plans[image_source_depth-1];plan->valid=0;
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
    plan->entries=memory;plan->capacity=count;
  }
  plan->count=0;plan->compute=compute;
  for(unsigned stage=0;stage<6;stage++) {
    const vk_shader *s=stage>=(compute?5:0) && stage<(compute?6:5)?list[stage]:NULL;
    image_source_stage *part=&plan->stages[stage];
    *part=(image_source_stage){.shader=s,.begin=plan->count};
    if(!s)continue;
    for(vk_image_binding_iterator it=vk_image_binding_begin(s);;) {
      unsigned k;if(!vk_image_binding_next(s,&it,&k))break;
      const vk_binding *b=&s->bindings[k];if(b->kind!=3 && b->kind!=6)continue;
      if(plan->count==count)return NULL;
      image_source_entry *entry=&plan->entries[plan->count++];
      entry->reg=b->reg;entry->source.id=-1;
      part->count++;
    }
    if(part->count!=s->sampled_source_schema-1)return NULL;
  }
  plan->valid=plan->count==count;return plan->valid?plan:NULL;
}
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
// Functional observer for owned call-argument and lifetime oracles only.
int vk_test_image_source_origin(const tex_t *texture,uint64_t *generation) {
  image_source_plan *plan=image_sources_current();if(!plan)return 0;
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
#else
int vk_shader_image_sources_begin(void) {return 0;}
void vk_shader_image_sources_end(int token) {(void)token;}
#endif
