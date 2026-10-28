// Optional work-count diagnostic for the existing DPR1 recipe. It observes
// ready inputs while executing the original .spec path; it is not a timer,
// sampler, new shader selection, or permission to wait for a GPU producer.
enum { UP_OBS_READY,UP_OBS_BASE_FALLBACK,UP_OBS_PENDING,UP_OBS_TUPLE_OVERFLOW,
  UP_OBS_MASKS,UP_OBS_TUPLES=UP_OBS_MASKS+16,UP_OBS_WORDS=UP_OBS_TUPLES+32 };
typedef struct uniform_predicate_observer {
  vk_uniform_predicate predicate; // first member: only observe-mode allocations
  struct uniform_predicate_observer *next;
  char key[17],base_sha[65],spec_sha[65];
  uint64_t recorded[UP_OBS_WORDS];
  _Atomic uint64_t published[UP_OBS_WORDS],sequence;
  int dirty;
} uniform_predicate_observer;
static _Atomic(uniform_predicate_observer *) uniform_predicate_observers;
static int uniform_predicate_observe_initialized;
static atomic_int uniform_predicate_observe_active;

static int uniform_predicate_observe_policy(void) {
  if(!uniform_predicate_observe_initialized) {
    const char *value=getenv("DM_VULKAN_UNIFORM_PREDICATE_OBSERVE");
    if(value && strcmp(value,"0") && strcmp(value,"1"))
      die("DM_VULKAN_UNIFORM_PREDICATE_OBSERVE expects 0 or 1");
    uniform_predicate_observe_active=value && !strcmp(value,"1");
    uniform_predicate_observe_initialized=1;
    if(uniform_predicate_observe_active) {
      if(atomic_load(&uniform_predicate_enabled))die("Uniform predicate observation requires hybrid disabled");
      LOG("[vulkan uniform predicate observation] ready-input histogram; original modules/spec/PSO preserved");
    }
  }
  return uniform_predicate_observe_active;
}
static void uniform_predicate_observe_hash(char output[65],const void *bytes,size_t size) {
  uint8_t digest[32];CC_SHA256(bytes,size,digest);
  static const char hex[]="0123456789abcdef";
  for(unsigned n=0;n<32;n++) { output[n*2]=hex[digest[n]>>4];output[n*2+1]=hex[digest[n]&15]; }
  output[64]=0;
}
static vk_uniform_predicate *uniform_predicate_observe_allocate(void) {
  uniform_predicate_observer *observer=calloc(1,sizeof *observer);
  if(!observer)die("Vulkan Uniform observation allocation");
  atomic_init(&observer->sequence,0);
  for(unsigned n=0;n<UP_OBS_WORDS;n++)atomic_init(&observer->published[n],0);
  return &observer->predicate;
}
static void uniform_predicate_observe_register(vk_uniform_predicate *predicate,vk_shader *shader,
    const void *base,size_t base_size,const void *recipe,size_t recipe_size) {
  uniform_predicate_observer *observer=(uniform_predicate_observer *)predicate;
  memcpy(observer->key,shader->key,sizeof observer->key);
  uniform_predicate_observe_hash(observer->base_sha,base,base_size);
  uniform_predicate_observe_hash(observer->spec_sha,recipe,recipe_size);
  observer->next=atomic_load_explicit(&uniform_predicate_observers,memory_order_relaxed);
  atomic_store_explicit(&uniform_predicate_observers,observer,memory_order_release);
}
static VkShaderModule uniform_predicate_observe_pick(vk_shader *shader,uint32_t values[2]) {
  // Execute the unchanged baseline first. Unselected/pending/over-limit inputs
  // must not cause any extra CPU read, GPU flush, demand or wait for statistics.
  VkShaderModule module=vk_uniform_specialization_pick(shader,values);
  uniform_predicate_observer *observer=(uniform_predicate_observer *)shader->predicate;
  observer->dirty=1;
  if(!shader->specialization_module || module!=shader->specialization_module && module!=shader->range_module) {
    observer->recorded[UP_OBS_BASE_FALLBACK]++;return module;
  }
  vk_uniform_predicate *p=&observer->predicate;
  p_bind_buf uniform=S.ubo[1][p->slot];uint32_t bits;
  int available=uniform.size>=p->offset+4 && uniform.addr<=UINT64_MAX-p->offset;
  if(available) {
    p_bind_buf original=S.ubo[1][shader->specialization_slot];
    if(uniform.addr+p->offset==original.addr+shader->specialization_offset)bits=values[0];
    else available=vk_pool_try_read(uniform.addr+p->offset,4,&bits)==1;
  }
  if(!available) { observer->recorded[UP_OBS_PENDING]++;return module; }
  uint32_t mask=0;
  for(unsigned n=0;n<p->count;n++) {
    uint32_t equal=(bits&p->predicates[n].and_mask)==p->predicates[n].compare;
    mask|=(equal^(p->predicates[n].op==2))<<p->predicates[n].bit;
  }
  observer->recorded[UP_OBS_READY]++;observer->recorded[UP_OBS_MASKS+mask]++;
  uint64_t tuple=((uint64_t)values[0]<<32)|mask;
  for(unsigned n=0;n<16;n++) {
    uint64_t *entry=&observer->recorded[UP_OBS_TUPLES+n*2];
    if(!entry[1] || entry[0]==tuple) { entry[0]=tuple;entry[1]++;return module; }
  }
  observer->recorded[UP_OBS_TUPLE_OVERFLOW]++;return module;
}
static void uniform_predicate_observe_publish(void) {
  if(!uniform_predicate_observe_active)return;
  for(uniform_predicate_observer *o=atomic_load_explicit(&uniform_predicate_observers,memory_order_acquire);o;o=o->next) {
    if(!o->dirty)continue;
    atomic_fetch_add_explicit(&o->sequence,1,memory_order_acq_rel);
    for(unsigned n=0;n<UP_OBS_WORDS;n++)atomic_store_explicit(&o->published[n],o->recorded[n],memory_order_relaxed);
    atomic_fetch_add_explicit(&o->sequence,1,memory_order_release);o->dirty=0;
  }
}
int vk_uniform_predicate_observation_enabled(void) {
  return atomic_load_explicit(&uniform_predicate_observe_active,memory_order_relaxed);
}
// Caller owns the returned string. Rows are process-lived immutable identities;
// only snapshots published at the original shader_end boundary are exported.
char *vk_uniform_predicate_observation_json(void) {
  uniform_predicate_observer *head=atomic_load_explicit(&uniform_predicate_observers,memory_order_acquire);
  size_t rows=0;for(uniform_predicate_observer *o=head;o;o=o->next)rows++;
  if(rows>(SIZE_MAX-256)/4096)die("Vulkan Uniform observation JSON overflow");
  size_t capacity=256+rows*4096,used=0;char *json=malloc(capacity);
  if(!json)die("Vulkan Uniform observation JSON allocation");
#define UP_OBS_APPEND(...) do { int length=snprintf(json+used,capacity-used,__VA_ARGS__); \
  if(length<0 || (size_t)length>=capacity-used) { die("Vulkan Uniform observation JSON capacity"); } \
  used+=(size_t)length; } while(0)
  UP_OBS_APPEND("{\"version\":1,\"enabled\":%s,\"scope\":\"ready-input draw counts; not GPU invocations or timing\",\"rows\":[",uniform_predicate_observe_active?"true":"false");
  unsigned index=0;
  for(uniform_predicate_observer *o=head;o;o=o->next) {
    uint64_t words[UP_OBS_WORDS]={0},sequence=0;int coherent=0;
    for(unsigned retry=0;retry<4;retry++) {
      sequence=atomic_load_explicit(&o->sequence,memory_order_acquire);if(sequence&1)continue;
      for(unsigned n=0;n<UP_OBS_WORDS;n++)words[n]=atomic_load_explicit(&o->published[n],memory_order_relaxed);
      atomic_thread_fence(memory_order_acquire);
      if(sequence==atomic_load_explicit(&o->sequence,memory_order_relaxed)) {coherent=1;break;}
    }
    UP_OBS_APPEND("%s{\"key\":\"%s\",\"base_sha256\":\"%s\",\"spec_sha256\":\"%s\",\"coherent\":%s,\"publish_sequence\":%llu",index++?",":"",o->key,o->base_sha,o->spec_sha,coherent?"true":"false",(unsigned long long)sequence);
    if(coherent) {
      UP_OBS_APPEND(",\"ready\":%llu,\"base_fallback\":%llu,\"second_pending\":%llu,\"tuple_overflow\":%llu,\"masks\":[",(unsigned long long)words[UP_OBS_READY],(unsigned long long)words[UP_OBS_BASE_FALLBACK],(unsigned long long)words[UP_OBS_PENDING],(unsigned long long)words[UP_OBS_TUPLE_OVERFLOW]);
      for(unsigned n=0;n<16;n++)UP_OBS_APPEND("%s%llu",n?",":"",(unsigned long long)words[UP_OBS_MASKS+n]);
      UP_OBS_APPEND("],\"tuples\":[");unsigned count=0;
      for(unsigned n=0;n<16;n++)if(words[UP_OBS_TUPLES+n*2+1]) {
        uint64_t tuple=words[UP_OBS_TUPLES+n*2];
        UP_OBS_APPEND("%s{\"spec0_raw\":%u,\"mask\":%u,\"draws\":%llu}",count++?",":"",(uint32_t)(tuple>>32),(uint32_t)tuple,(unsigned long long)words[UP_OBS_TUPLES+n*2+1]);
      }
      UP_OBS_APPEND("]");
    }
    UP_OBS_APPEND("}");
  }
  UP_OBS_APPEND("]}");
#undef UP_OBS_APPEND
  return json;
}
