// Reuse only the first fully resolved, UAB-owned prefix of a render job.
// Later commands keep their original transient secondary, after this prefix.
// Resource preparation/pins/history are never replayed or skipped here.
enum { RETAINED_KEY_BUDGET=16u*1024*1024 };
_Static_assert(RETAINED_MAX_JOBS*RETAINED_MAX_CHUNKS==VK_RECORD_OWNER_MAX_SLOTS,"retained owner slot product");
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

#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
static uint64_t retained_test_captures,retained_test_replays,retained_test_rollbacks;
uint64_t vk_test_retained_chunk_rollbacks(void) {return retained_test_rollbacks;}
void vk_test_retained_prefix_stats(uint64_t *captures,uint64_t *replays) {
  *captures=retained_test_captures;*replays=retained_test_replays;
}
#endif

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
  if(n<RETAINED_MAX_JOBS) {j->retained_slot=n;j->retained_slot_valid=1;}
}
// Resolve one fixed-size chunk privately. Failure has no GPU execution side
// effect: the caller can still deliver the entire original prepared prefix.
static VkCommandBuffer retained_chunk_prepare(render_job *j,retained_recordings *r,
    unsigned index,const vk_draw_emission *draws,unsigned *captured,unsigned *replayed) {
  const unsigned count=RETAINED_CHUNK_DRAWS;
  retained_prefix *entry=&r->slots[index];
  VkCommandBuffer cached=vk_record_owner_cached(r->owner,index);
  int same=cached && entry->count==count && entry->pass==j->pass &&
      entry->framebuffer==j->framebuffer && !memcmp(&entry->area,&j->area,sizeof j->area);
  if(same)for(unsigned i=0;i<count;i++)if(!retained_draw_equal(&entry->keys[i],&draws[i])) {same=0;break;}
  if(same) {
    (*replayed)++;return cached;
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
  (*captured)++;return command;
}
static unsigned retained_prefix_try(render_job *j,const vk_draw_emission *draws,unsigned count) {
  if(!V.retained_draws || !graphics_state || !j->retained_slot_valid ||
      j->retained_count || j->retained_preceded || j->local ||
      j->pending_uncacheable || count<RETAINED_CHUNK_DRAWS ||
      count>RETAINED_MAX_CHUNKS*RETAINED_CHUNK_DRAWS ||
      j->gpu_region || vk_trace_frame_active())return 0;
  unsigned chunks=count/RETAINED_CHUNK_DRAWS;
  unsigned retained=chunks*RETAINED_CHUNK_DRAWS;
  for(unsigned i=0;i<retained;i++) {
    if(!draws[i].geometry_complete || !draws[i].retained_parameters)return 0;
    for(unsigned s=0;s<5;s++)if(draws[i].stage_present_mask&(1u<<s))
      if(draws[i].uniform_offsets[draws[i].stage_set[s]].count)return 0;
  }
  draw_parameters_flush(graphics_state->parameters);
  retained_recordings *r=graphics_state->retained;
  if(!r) {
    r=calloc(1,sizeof *r);if(!r)return 0;
    r->owner=vk_record_owner_create(VK_RECORD_OWNER_MAX_SLOTS);
    graphics_state->retained=r;
  }
  VkCommandBuffer commands[RETAINED_MAX_CHUNKS];
  unsigned captured=0,replayed=0;
  uint64_t epoch=vk_record_object_epoch();
  for(unsigned c=0;c<chunks;c++) {
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
    if(c==1 && !retained_test_rollbacks && getenv("DM_RETAINED_CHUNK_FAIL_ONCE")) {
      // The first chunk is already privately recorded/reused, but no command
      // array has been published. The original whole prefix must execute once.
      if(j->retained_count)die("Vulkan partial retained chunk publication");
      retained_test_rollbacks++;
      LOG("[retained chunk rollback fixture] second chunk failed; whole prefix remains original");
      return 0;
    }
#endif
    commands[c]=retained_chunk_prepare(j,r,j->retained_slot*RETAINED_MAX_CHUNKS+c,
        draws+c*RETAINED_CHUNK_DRAWS,&captured,&replayed);
    if(!commands[c])return 0;
  }
  // Even cached chunks selected before a later chunk's recording must remain
  // in the same physical generation. Do not publish a partial chunk array.
  if(epoch!=vk_record_object_epoch())return 0;
  memcpy(j->retained_secondaries,commands,chunks*sizeof *commands);
  j->retained_count=chunks;r->used=1;
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  retained_test_captures+=captured;retained_test_replays+=replayed;
#endif
  static int first_record,first_replay;
  if(captured && !first_record) {first_record=1;LOG("[vulkan retained draws] first recording chunks=%u draws=%u slot=%u",captured,retained,j->retained_slot);}
  if(replayed && !first_replay) {first_replay=1;LOG("[vulkan retained draws] first replay chunks=%u draws=%u slot=%u recording=%llu",replayed,retained,j->retained_slot,(unsigned long long)vk_recording_serial());}
  return retained;
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
