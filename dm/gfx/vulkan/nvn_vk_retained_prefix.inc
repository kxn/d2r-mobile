// Reuse only the first fully resolved, UAB-owned prefix of a render job.
// Later commands keep their original transient secondary, after this prefix.
// Resource preparation/pins/history are never replayed or skipped here.
enum { RETAINED_MIN_DRAWS=64, RETAINED_KEY_BUDGET=16u*1024*1024 };
typedef struct {
  VkRenderPass pass;
  VkFramebuffer framebuffer;
  VkRect2D area;
  vk_draw_emission *keys;
  unsigned count,capacity;
} retained_prefix;
typedef struct retained_recordings {
  vk_record_owner *owner;
  retained_prefix slots[VK_RECORD_OWNER_MAX_SLOTS];
  size_t key_bytes;
  uint64_t pending_serial;
  int used;
} retained_recordings;

// Compare only consumed values. Unused slots, padding and the transient
// command-buffer handle are not part of the fixed GPU encoding.
static int retained_draw_equal(const vk_draw_emission *a,const vk_draw_emission *b) {
  if(a->pipeline!=b->pipeline || a->layout!=b->layout ||
     a->vertex_mask!=b->vertex_mask || a->index_bound!=b->index_bound ||
     a->stage_present_mask!=b->stage_present_mask ||
     a->bias_enabled!=b->bias_enabled || a->indexed!=b->indexed ||
     a->state_reference!=b->state_reference || a->count!=b->count ||
     a->instances!=b->instances || a->first!=b->first ||
     a->first_instance!=b->first_instance || a->vertex_offset!=b->vertex_offset ||
     memcmp(&a->viewport,&b->viewport,sizeof a->viewport) ||
     memcmp(&a->scissor,&b->scissor,sizeof a->scissor) ||
     memcmp(a->depth_bias,b->depth_bias,sizeof a->depth_bias))return 0;
  for(unsigned i=0;i<16;i++)if(a->vertex_mask&(1u<<i))
    if(a->vertices[i].buffer!=b->vertices[i].buffer || a->vertices[i].offset!=b->vertices[i].offset)return 0;
  if(a->index_bound && (a->index_buffer!=b->index_buffer ||
      a->index_offset!=b->index_offset || a->index_type!=b->index_type))return 0;
  for(unsigned i=0;i<5;i++)if(a->stage_present_mask&(1u<<i)) {
    if(a->stage_set[i]!=b->stage_set[i] || a->stage_sets[i]!=b->stage_sets[i])return 0;
    if(a->uniform_offsets[a->stage_set[i]].count || b->uniform_offsets[b->stage_set[i]].count)return 0;
  }
  if(a->bias_enabled && memcmp(a->bias_words,b->bias_words,sizeof a->bias_words))return 0;
  return 1;
}
static void retained_job_begin(render_job *j) {
  if(!V.retained_draws || !graphics_state)return;
  unsigned n=graphics_state->retained_next_job++;
  if(n<VK_RECORD_OWNER_MAX_SLOTS) {j->retained_slot=n;j->retained_slot_valid=1;}
}
static int retained_prefix_try(render_job *j,const vk_draw_emission *draws,unsigned count) {
  if(!V.retained_draws || !graphics_state || !j->retained_slot_valid ||
      j->retained_secondary || j->retained_preceded || j->local ||
      j->pending_uncacheable || count<RETAINED_MIN_DRAWS || count>2048 ||
      V.options.gpu_profile || vk_trace_frame_active())return 0;
  for(unsigned i=0;i<count;i++) {
    if(!draws[i].geometry_complete || !draws[i].retained_parameters)return 0;
    for(unsigned s=0;s<6;s++)if(draws[i].uniform_offsets[s].count)return 0;
  }
  // Deferred descriptor writes, including first immutable image values, must
  // be published before recording or replaying the first set that uses them.
  draw_parameters_flush(graphics_state->parameters);
  retained_recordings *r=graphics_state->retained;
  if(!r) {
    r=calloc(1,sizeof *r);if(!r)return 0;
    r->owner=vk_record_owner_create(VK_RECORD_OWNER_MAX_SLOTS);
    graphics_state->retained=r;
  }
  unsigned index=j->retained_slot;retained_prefix *entry=&r->slots[index];
  VkCommandBuffer cached=vk_record_owner_cached(r->owner,index);
  int same=cached && entry->count==count && entry->pass==j->pass &&
      entry->framebuffer==j->framebuffer && !memcmp(&entry->area,&j->area,sizeof j->area);
  if(same)for(unsigned i=0;i<count;i++)if(!retained_draw_equal(&entry->keys[i],&draws[i])) {same=0;break;}
  if(same) {
    j->retained_secondary=cached;r->used=1;
    static int first_replay;
    if(!first_replay) {first_replay=1;LOG("[vulkan retained draws] first replay draws=%u slot=%u recording=%llu",count,index,(unsigned long long)vk_recording_serial());}
    return 1;
  }
  if(entry->capacity<count) {
    size_t extra=(size_t)(count-entry->capacity)*sizeof *entry->keys;
    if(extra>RETAINED_KEY_BUDGET-r->key_bytes)return 0;
    vk_draw_emission *grown=realloc(entry->keys,(size_t)count*sizeof *grown);
    if(!grown)return 0;
    entry->keys=grown;entry->capacity=count;r->key_bytes+=extra;
  }
  VkCommandBufferInheritanceInfo inherit={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
    .renderPass=j->pass,.framebuffer=j->framebuffer};
  VkCommandBuffer command=vk_record_owner_begin(r->owner,index,&inherit);
  if(!command)return 0;
  render_job capture=*j;
  capture.secondary=command;capture.pending_count=0;
  capture.bound_pipeline=VK_NULL_HANDLE;capture.bound_layout=VK_NULL_HANDLE;
  memset(capture.bound_sets,0,sizeof capture.bound_sets);
  memset(capture.bound_uniform_offsets,0,sizeof capture.bound_uniform_offsets);
  memset(&capture.bias_state,0,sizeof capture.bias_state);capture.dynamic_state_valid=0;
  render_binding_reset(&capture);
  VkCommandBuffer original_command=V.cmd;render_job *original_job=active_job;
  V.cmd=command;active_job=&capture;
  for(unsigned i=0;i<count;i++) {
    vk_draw_emission current=draws[i];current.command=command;
    draw_emission_immediate(&capture,&current,(vk_sample_query){0});
  }
  active_job=original_job;V.cmd=original_command;
  // Current original pins are still held. A retirement observed during
  // capture conservatively rejects reuse and emits the original prefix.
  if(!vk_record_owner_end(r->owner,index)) {entry->count=0;return 0;}
  memcpy(entry->keys,draws,(size_t)count*sizeof *draws);
  entry->count=count;entry->pass=j->pass;entry->framebuffer=j->framebuffer;entry->area=j->area;
  j->retained_secondary=command;r->used=1;
  static int first_record;
  if(!first_record) {first_record=1;LOG("[vulkan retained draws] first recording draws=%u slot=%u",count,index);}
  return 1;
}
static void retained_submission_end(vk_graphics_submission *state) {
  retained_recordings *r=state->retained;
  if(r && r->used) {
    r->pending_serial=vk_recording_serial();
    vk_record_owner_pending(r->owner,r->pending_serial);
  }
}
static void retained_submission_recycle(vk_graphics_submission *state) {
  retained_recordings *r=state->retained;
  if(r && r->pending_serial) {
    vk_record_owner_reclaim(r->owner,r->pending_serial);r->pending_serial=0;
  }
}
static void retained_submission_forget(vk_graphics_submission *state) {
  retained_recordings *r=state->retained;if(!r)return;
  if(!vk_record_owner_forget(r->owner))die("Vulkan retained context discarded before completion");
  for(unsigned i=0;i<VK_RECORD_OWNER_MAX_SLOTS;i++) {
    free(r->slots[i].keys);r->slots[i].keys=NULL;r->slots[i].count=r->slots[i].capacity=0;
  }
  r->key_bytes=0;r->used=0;
}
