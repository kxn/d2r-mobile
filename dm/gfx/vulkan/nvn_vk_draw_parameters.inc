// Private per-original-graphics-context UAB slots. No guest/resource lookup:
// products have completed the original consumer and hold its recording pins.
#include "nvn_vk_uab.h"
#include "nvn_vk_record_owner.h"
typedef struct draw_parameter_block {
  struct draw_parameter_block *next;
  vk_uab_packet *packet;
  int version;
  unsigned used;
} draw_parameter_block;
typedef struct draw_parameter_schema {
  struct draw_parameter_schema *next;
  vk_shader *shader; // original device-lifetime immutable schema identity
  vk_uab_schema schema;
  draw_parameter_block *blocks,*current;
} draw_parameter_schema;
struct draw_parameters {
  draw_parameter_schema *schemas,*last[6];
  vk_uab_batch *batch;
  unsigned block_count,active,discard;
  uint64_t last_serial,object_epoch;
};
enum { DRAW_PARAMETER_BLOCK_SLOTS=32,DRAW_PARAMETER_MAX_BLOCKS=256 };
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
static uint64_t draw_parameter_actual,draw_parameter_fallback,draw_parameter_pso_retry;
static uint64_t draw_parameter_reuses,draw_parameter_pending_rejected;
#endif
static void draw_parameters_flush(draw_parameters *p) {if(p)vk_uab_batch_flush(p->batch);}
static void draw_parameters_begin(draw_parameters *p) {
  if(!p)return;
  if(p->active || p->last_serial>vk_completed_serial())die("Vulkan draw parameters reused before completion");
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  if(p->last_serial)draw_parameter_reuses++;
#endif
  if(!vk_uab_batch_begin(p->batch))die("Vulkan draw parameter batch reuse");
  p->active=1;
  for(draw_parameter_schema *s=p->schemas;s;s=s->next) {
    s->current=s->blocks;
    for(draw_parameter_block *b=s->blocks;b;b=b->next) {b->version=-1;b->used=0;}
  }
}
// One decision for the complete draw, not another per-resource identity walk.
// An equal raw handle cannot revive a descriptor whose old object was retired.
static int draw_parameters_valid(draw_parameters *p) {
  if(!p || p->object_epoch==vk_record_object_epoch())return 1;
  p->discard=1;return 0;
}
static VkDescriptorSet draw_parameters_write(draw_parameters **owner,const vk_shader_prepared_values *values) {
  if(!values->count)return VK_NULL_HANDLE;
  draw_parameters *p=*owner;
  if(!p) {
    p=calloc(1,sizeof *p);if(!p)return VK_NULL_HANDLE;
    p->batch=vk_uab_batch_create(1024);
    if(!p->batch){free(p);return VK_NULL_HANDLE;}
    if(!vk_uab_batch_begin(p->batch))die("Vulkan draw parameter initial batch");
    p->object_epoch=vk_record_object_epoch();
    p->active=1;*owner=p;
  }
  if(!p->active)die("Vulkan draw parameters outside recording");
  vk_shader *shader=values->shader;
  if(shader->set>=6)return VK_NULL_HANDLE;
  draw_parameter_schema *s=p->last[shader->set];
  if(!s || s->shader!=shader) {
    for(s=p->schemas;s && s->shader!=shader;s=s->next) {}
    if(!s) {
      s=calloc(1,sizeof *s);if(!s)return VK_NULL_HANDLE;
      if(!vk_uab_schema_from_shader(shader,0,&s->schema)) {free(s);return VK_NULL_HANDLE;}
      s->shader=shader;s->next=p->schemas;p->schemas=s;
    }
    p->last[shader->set]=s;
  }
  draw_parameter_block *b=s->current;
  if(b && b->used==DRAW_PARAMETER_BLOCK_SLOTS)b=b->next;
  if(!b) {
    if(p->block_count==DRAW_PARAMETER_MAX_BLOCKS)return VK_NULL_HANDLE;
    b=calloc(1,sizeof *b);if(!b)return VK_NULL_HANDLE;
    b->packet=vk_uab_packet_create(&s->schema,DRAW_PARAMETER_BLOCK_SLOTS,1);
    if(!b->packet){free(b);return VK_NULL_HANDLE;}
    b->version=-1;
    if(s->current)s->current->next=b;else s->blocks=b;
    p->block_count++;
  }
  s->current=b;
  if(b->version<0) {
    b->version=vk_uab_packet_begin(b->packet);
    if(b->version<0)return VK_NULL_HANDLE;
  }
  unsigned slot=b->used++;
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  static int rejected;
  if(!rejected && shader->stage==1 && getenv("DM_DRAW_UAB_WRITE_FAIL_ONCE")) {
    rejected=1;p->discard=1;return VK_NULL_HANDLE;
  }
#endif
  if(!vk_uab_packet_write_batch(b->packet,b->version,slot,values->values,p->batch)) {
    // Immutable image/texel identity changed at this ordinal. Do not mutate a
    // recorded non-UAB binding, retire its pool, or re-consume owner resources.
    // Root may discard only after forgetting every retained CB using this pool.
    p->discard=1;return VK_NULL_HANDLE;
  }
  return vk_uab_packet_set(b->packet,b->version,slot);
}
static void draw_parameters_end(draw_parameters *p) {
  if(!p)return;
  if(!p->active)die("Vulkan draw parameters end outside recording");
  draw_parameters_flush(p);
  uint64_t serial=vk_recording_serial();
  for(draw_parameter_schema *s=p->schemas;s;s=s->next)
    for(draw_parameter_block *b=s->blocks;b;b=b->next)if(b->version>=0) {
      // A rejected first immutable write may have reserved an empty slot.
      if(vk_uab_packet_seal(b->packet,b->version)) {
        if(!vk_uab_packet_pending(b->packet,b->version,serial))die("Vulkan draw parameter pending ownership");
      } else if(!vk_uab_packet_abandon(b->packet,b->version))die("Vulkan empty draw parameter abandonment");
    }
  if(!vk_uab_batch_end(p->batch))die("Vulkan draw parameter batch end");
  p->last_serial=serial;p->active=0;
}
int vk_graphics_parameters_discard_requested(const vk_graphics_submission *state) {
  return state && state->parameters && (state->parameters->discard ||
      state->parameters->object_epoch!=vk_record_object_epoch());
}
// Caller must first vk_record_owner_forget/reset every CB borrowing these sets.
// This is not an automatic recycle operation: executable CB references persist.
int vk_graphics_parameters_discard(vk_graphics_submission *state) {
  if(!state || !state->parameters)return 1;
  draw_parameters *p=state->parameters;
  if(p->active || p->last_serial>vk_completed_serial())return 0;
  for(draw_parameter_schema *s=p->schemas;s;) {
    draw_parameter_schema *next=s->next;
    for(draw_parameter_block *b=s->blocks;b;) {
      draw_parameter_block *next_block=b->next;
      if(!vk_uab_packet_destroy(b->packet))die("Vulkan completed draw parameter pool still pending");
      free(b);b=next_block;
    }
    free(s);s=next;
  }
  if(!vk_uab_batch_destroy(p->batch))die("Vulkan draw parameter batch lifetime");
  free(p);state->parameters=NULL;return 1;
}
static void draw_parameters_materialize(vk_shader *const list[6],const vk_shader_prepared_values *products,
                                        VkDescriptorSet sets[6],vk_uniform_offsets offsets[6]) {
  for(unsigned i=0;i<5;i++)if(list[i]) {
    sets[i]=products[i].count?vk_shader_materialize_prepared(&products[i]):VK_NULL_HANDLE;
    offsets[list[i]->set].count=0;
  }
}
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
void vk_test_draw_parameter_stats(uint64_t out[5]) {
  out[0]=draw_parameter_actual;out[1]=draw_parameter_fallback;out[2]=draw_parameter_pso_retry;
  out[3]=draw_parameter_reuses;out[4]=draw_parameter_pending_rejected;
}
#endif
