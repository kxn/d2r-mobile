// Recording-owner experiment. Cache a completed read consumption, not merely a
// VkBuffer lookup. NVN setters invalidate individual schema ordinals; producers,
// alias membership, physical pass and recording boundaries invalidate the proof.
// Only direct shared-lazy UBOs qualify. SSBO bias, CPU mirrors, image preparation,
// host constants, writable stages and optional/recursive draws remain original.
static int ubo_plan_enabled,ubo_plan_started;
int vk_ubo_plan_active(void) {return ubo_plan_enabled;}
static unsigned ubo_draw_depth;
static uint64_t ubo_plan_revision=1,ubo_vertex_revision;
typedef struct {
  uint64_t serial,pass,buffer_epoch,registry,alias;
} ubo_plan_phase;
typedef struct {
  const vk_shader *shader;
  int dynamic,admitted;
  ubo_plan_phase phase;
  uint64_t ready[2],pending[2],slots[16][2],versions[16];
  uint32_t offsets[VK_UNIFORM_OFFSET_CAPACITY];
} uniform_consumption_plan;
static uniform_consumption_plan ubo_plans[2];
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
// Host fixture oracles only. vertex_skipped counts initial attempts; a later
// callback may retry them. These are not production saved-work measurements.
static uint64_t ubo_test_consumed,ubo_test_vertex_skipped,ubo_test_captured;
static void (*ubo_test_before_require)(void *);
static void *ubo_test_before_require_context;
void vk_test_ubo_before_require(void (*callback)(void *),void *context) {
  ubo_test_before_require=callback;ubo_test_before_require_context=context;
}
void vk_test_ubo_consumption(uint64_t out[3],int *enabled) {
  out[0]=ubo_test_consumed;out[1]=ubo_test_vertex_skipped;out[2]=ubo_test_captured;
  *enabled=ubo_plan_enabled;
}
#endif
static void ubo_plan_startup(void) {
  if(ubo_plan_started)return;
  const char *setting=getenv("DM_VULKAN_BUFFER_CONSUMPTION_PLAN");
  if(setting && strcmp(setting,"0") && strcmp(setting,"1"))
    die("DM_VULKAN_BUFFER_CONSUMPTION_PLAN expects 0 or 1");
  ubo_plan_enabled=setting && !strcmp(setting,"1");ubo_plan_started=1;
  LOG("[vulkan buffer plan] enabled=%d",ubo_plan_enabled);
}
static void ubo_plan_clear(uniform_consumption_plan *p) {
  p->ready[0]=p->ready[1]=p->pending[0]=p->pending[1]=0;p->admitted=0;
}
void vk_ubo_plan_break(void) {
  if(!ubo_plan_enabled)return;
  ubo_plan_clear(&ubo_plans[0]);ubo_plan_clear(&ubo_plans[1]);
  if(!++ubo_plan_revision)die("Uniform consumption revision overflow");
}
void vk_ubo_plan_changed(unsigned stage,unsigned slot,uint64_t address,uint64_t size) {
  if(!ubo_plan_enabled || stage>=2 || slot>=16)return;
  const p_bind_buf *old=&S.ubo[stage][slot];
  if(old->addr==address && old->size==size)return;
  uniform_consumption_plan *p=&ubo_plans[stage];
  p->ready[0]&=~p->slots[slot][0];p->ready[1]&=~p->slots[slot][1];
  p->pending[0]&=~p->slots[slot][0];p->pending[1]&=~p->slots[slot][1];
  if(!++p->versions[slot])die("Uniform slot version overflow");
}
static ubo_plan_phase ubo_phase(void) {
  return (ubo_plan_phase){vk_recording_serial(),V.passes,V.buffer_epoch,
      vk_pool_uniform_epoch(),vk_texture_uniform_alias_epoch()};
}
static int ubo_phase_equal(ubo_plan_phase a,ubo_plan_phase b) {
  return a.serial==b.serial && a.pass==b.pass && a.buffer_epoch==b.buffer_epoch &&
      a.registry==b.registry && a.alias==b.alias && a.registry && a.alias;
}
static int ubo_plan_current(uniform_consumption_plan *p) {
  if(!p->admitted)return 0;
  if(ubo_draw_depth!=1 || vk_ghost_replaying() || vk_mip_clear_pending() ||
      !ubo_phase_equal(p->phase,ubo_phase())) {vk_ubo_plan_break();return 0;}
  return 1;
}
void vk_ubo_plan_draw_enter(void) {
  if(!ubo_plan_enabled)return;
  if(++ubo_draw_depth!=1)vk_ubo_plan_break();
}
void vk_ubo_plan_draw_leave(void) {
  if(!ubo_plan_enabled)return;
  if(!ubo_draw_depth)die("Uniform draw scope underflow");
  --ubo_draw_depth;
}
void vk_ubo_plan_admit(vk_shader *const list[6],int dynamic,int eligible) {
  if(!ubo_plan_enabled)return;
  ubo_plan_phase phase=ubo_phase();
  if(!eligible || ubo_draw_depth!=1 || !phase.registry || !phase.alias) {
    vk_ubo_plan_break();return;
  }
  for(unsigned st=0;st<2;st++) {
    uniform_consumption_plan *p=&ubo_plans[st];
    if(p->shader!=list[st] || p->dynamic!=dynamic) {
      ubo_plan_clear(p);memset(p->slots,0,sizeof p->slots);
      p->shader=list[st];p->dynamic=dynamic;
    }
    if(!ubo_phase_equal(p->phase,phase))ubo_plan_clear(p);
    p->phase=phase;p->admitted=list[st]!=NULL;
  }
}
void vk_ubo_plan_vertex_mask(vk_shader *shader,uint64_t skip[2]) {
  if(!ubo_plan_enabled)return;
  uniform_consumption_plan *p=&ubo_plans[0];
  if(p->shader!=shader || !ubo_plan_current(p))return;
  if((p->ready[0]|p->ready[1]) && !vk_pool_uniform_group_borrow(0)) {vk_ubo_plan_break();return;}
  skip[0]=p->ready[0];skip[1]=p->ready[1];
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  ubo_test_vertex_skipped+=__builtin_popcountll(skip[0])+__builtin_popcountll(skip[1]);
  if((skip[0]|skip[1]) && ubo_test_before_require) {
    void (*callback)(void *)=ubo_test_before_require;void *context=ubo_test_before_require_context;
    ubo_test_before_require=NULL;ubo_test_before_require_context=NULL;callback(context);
  }
#endif
}
int vk_ubo_plan_vertex_retry(uint64_t skip[2]) {
  if(!ubo_plan_enabled || !(skip[0]|skip[1]))return 0;
  if(ubo_plan_current(&ubo_plans[0])) {
    skip[0]&=~ubo_plans[0].ready[0];skip[1]&=~ubo_plans[0].ready[1];
  }
  return !!(skip[0]|skip[1]);
}
void vk_ubo_plan_vertex_done(void) {if(ubo_plan_enabled)ubo_vertex_revision=ubo_plan_revision;}
static uniform_consumption_plan *ubo_plan_values(vk_shader *s,int dynamic,int local,
    descriptor_value *values,vk_uniform_offsets *offsets,uint64_t skip[2]) {
  if(!ubo_plan_enabled || s->stage>=2)return NULL;
  uniform_consumption_plan *p=&ubo_plans[s->stage];
  if(local || p->shader!=s || p->dynamic!=dynamic || values!=buffer_plan_payloads[s->stage] ||
      !ubo_plan_current(p)) {ubo_plan_clear(p);return NULL;}
  if((p->ready[0]|p->ready[1]) && !vk_pool_uniform_group_borrow(s->stage)) {vk_ubo_plan_break();return NULL;}
  skip[0]=p->ready[0];skip[1]=p->ready[1];
  // Vulkan still requires the complete live dynamic-offset prefix. Descriptor
  // values are not copied. Every changed/nonqualifying consumer overwrites its
  // ordinal below, and every schema ordinal was filled on the original cold run.
  if((skip[0]|skip[1]) && dynamic && offsets && offsets->count)
    memcpy(offsets->offsets,p->offsets,offsets->count*sizeof *offsets->offsets);
  return p;
}
static void ubo_plan_capture(uniform_consumption_plan *p,unsigned n,unsigned slot,
    uint64_t version,uint64_t revision,uint64_t address,uint64_t size,void *owner) {
  if(!p || !owner || slot>=16 || n>=128 || p->versions[slot]!=version ||
      ubo_plan_revision!=revision || !ubo_plan_current(p) ||
      (p==&ubo_plans[0] && ubo_vertex_revision!=revision) ||
      !vk_texture_uniform_alias_proved(address,size))return;
  unsigned stage=(unsigned)(p-ubo_plans);
  if(!vk_pool_uniform_group_add(stage,owner))return;
  uint64_t bit=UINT64_C(1)<<(n%64);
  p->slots[slot][n/64]|=bit;p->pending[n/64]|=bit;
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  ubo_test_captured++;
#endif
}
static int ubo_plan_retry(uniform_consumption_plan *p,uint64_t skipped[2],uint64_t consume[2]) {
  if(!p || !(skipped[0]|skipped[1]))return 0;
  uint64_t retry[2]={skipped[0],skipped[1]};
  if(ubo_plan_current(p)) {retry[0]&=~p->ready[0];retry[1]&=~p->ready[1];}
  skipped[0]&=~retry[0];skipped[1]&=~retry[1];
  consume[0]|=retry[0];consume[1]|=retry[1];return !!(retry[0]|retry[1]);
}
void vk_ubo_plan_recorded(void) {
  if(!ubo_plan_enabled || ubo_draw_depth!=1)return;
  for(unsigned st=0;st<2;st++) {
    uniform_consumption_plan *p=&ubo_plans[st];
    if(!ubo_plan_current(p))continue;
    p->ready[0]|=p->pending[0];p->ready[1]|=p->pending[1];
    p->pending[0]=p->pending[1]=0;
  }
}
