#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
static uint64_t draw_emission_complete,draw_emission_interleaved;
void vk_test_draw_emission_stats(uint64_t *complete,uint64_t *interleaved) {
  *complete=draw_emission_complete;*interleaved=draw_emission_interleaved;
}
static int draw_emission_test_reference(void) {
  static int enabled=-1;
  if(enabled<0) {const char *v=getenv("DM_DRAW_EMISSION_REFERENCE");enabled=v&&!strcmp(v,"1");}
  return enabled;
}
#endif
// Synchronous resolved delivery: no resource lookup, owner preparation or
// fallback replay. Only the original per-secondary emitted-state suppression.
static void draw_emission_immediate(render_job *j,const vk_draw_emission *p,vk_sample_query occlusion) {
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  if(p->geometry_complete)draw_emission_complete++;else draw_emission_interleaved++;
#endif
  if(p->command!=V.cmd)die("Vulkan resolved draw command context changed");
  if(p->geometry_complete) {
    for(unsigned i=0;i<16;i++)if(p->vertex_mask&(1u<<i))
      render_bind_vertex(j,i,p->vertices[i].buffer,p->vertices[i].offset,p->state_reference);
    if(p->index_bound)render_bind_index(j,p->index_buffer,p->index_offset,p->index_type,p->state_reference);
  }
  vk_counter_draw_begin(occlusion);
  if(p->state_reference || j->bound_pipeline!=p->pipeline) {
    vkCmdBindPipeline(V.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p->pipeline);
    j->bound_pipeline=p->pipeline;
  }
  vk_shader_push_bias_values(p->layout,p->bias_enabled,p->bias_words,p->state_reference?NULL:&j->bias_state);
  const VkViewport viewport=p->viewport;const VkRect2D sc=p->scissor;
  const float *depth_bias=p->depth_bias;
  const vk_uniform_offsets *uniform_offsets=p->uniform_offsets;
  if(p->state_reference || !j->dynamic_state_valid || memcmp(&j->viewport,&viewport,sizeof viewport)) {
    vkCmdSetViewport(V.cmd,0,1,&viewport);j->viewport=viewport;
  }
  if(p->state_reference || !j->dynamic_state_valid || memcmp(&j->scissor,&sc,sizeof sc)) {
    vkCmdSetScissor(V.cmd,0,1,&sc);j->scissor=sc;
  }
  if(p->state_reference || !j->dynamic_state_valid || memcmp(j->depth_bias,depth_bias,sizeof p->depth_bias)) {
    vkCmdSetDepthBias(V.cmd,depth_bias[0],depth_bias[1],depth_bias[2]);
    memcpy(j->depth_bias,depth_bias,sizeof p->depth_bias);
  }
  j->dynamic_state_valid=1;
  if(p->state_reference) {
    for(int i=0;i<5;i++) if(p->stage_present_mask&(1u<<i))
      vkCmdBindDescriptorSets(V.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p->layout,
                              p->stage_set[i], 1, &p->stage_sets[i], uniform_offsets[p->stage_set[i]].count,
                              uniform_offsets[p->stage_set[i]].offsets);
  } else {
    // Sets are immutable per draw. Bind contiguous set numbers together;
    // holes retain their original unbound state, including absent stages.
    VkDescriptorSet ordered[6]={0};
    for(int i=0;i<5;i++) if(p->stage_present_mask&(1u<<i)) ordered[p->stage_set[i]]=p->stage_sets[i];
    // Different pipeline layouts may disturb other set numbers. Restrict
    // retention to the exact layout and this secondary's own emitted state.
    if(j->bound_layout!=p->layout) {
      memset(j->bound_sets,0,sizeof j->bound_sets);j->bound_layout=p->layout;
    }
    int changed[6]={0};
    for(unsigned i=0;i<6;i++)changed[i]=ordered[i] && (ordered[i]!=j->bound_sets[i] ||
      uniform_offsets[i].count!=j->bound_uniform_offsets[i].count ||
      memcmp(uniform_offsets[i].offsets,j->bound_uniform_offsets[i].offsets,
             uniform_offsets[i].count*sizeof *uniform_offsets[i].offsets));
    for(unsigned first=0;first<6;) {
      if(!changed[first]) { first++;continue; }
      unsigned end=first+1;
      while(end<6 && changed[end])end++;
      uint32_t offsets[6*VK_UNIFORM_OFFSET_CAPACITY],count=0;
      for(unsigned i=first;i<end;i++) {
        memcpy(offsets+count,uniform_offsets[i].offsets,uniform_offsets[i].count*sizeof *offsets);
        count+=uniform_offsets[i].count;
      }
      vkCmdBindDescriptorSets(V.cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,p->layout,
                              first,end-first,ordered+first,count,offsets);
      memcpy(j->bound_sets+first,ordered+first,(end-first)*sizeof *ordered);
      for(unsigned i=first;i<end;i++) {
        j->bound_uniform_offsets[i].count=uniform_offsets[i].count;
        memcpy(j->bound_uniform_offsets[i].offsets,uniform_offsets[i].offsets,
               uniform_offsets[i].count*sizeof *uniform_offsets[i].offsets);
      }
      first=end;
    }
  }
  if(p->indexed)
    vkCmdDrawIndexed(V.cmd,p->count,p->instances,p->first,p->vertex_offset,p->first_instance);
  else
    vkCmdDraw(V.cmd,p->count,p->instances,p->first,p->first_instance);
}

// Pending packets borrow precisely the existing recording's resource pins and
// immutable descriptor sets. Owner preparation/history remains at the original
// draw position. No packet can escape render_end into another command buffer.
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
static uint64_t draw_pending_queued,draw_pending_flushes,draw_pending_max,draw_pending_abandoned;
unsigned vk_test_draw_pending_count(void) {return active_job?active_job->pending_count:0;}
void vk_test_draw_pending_stats(uint64_t *queued,uint64_t *flushes,uint64_t *maximum,uint64_t *abandoned) {
  *queued=draw_pending_queued;*flushes=draw_pending_flushes;
  *maximum=draw_pending_max;*abandoned=draw_pending_abandoned;
}
#endif
void vk_draw_flush_pending(void) {
  render_job *j=active_job;
  if(!j || !j->pending_count)return;
  unsigned count=j->pending_count;
  // Clear before emission: query hooks may call this entry, but queued draws
  // have no query and emission never invokes owner/resource preparation.
  j->pending_count=0;
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  draw_pending_flushes++;
#endif
  for(unsigned i=0;i<count;i++)draw_emission_immediate(j,&j->pending_draws[i],(vk_sample_query){0});
}
static void draw_emission_deliver(render_job *j,const vk_draw_emission *p,vk_sample_query query) {
  if(!V.retained_draws) {draw_emission_immediate(j,p,query);return;}
  if(!p->geometry_complete || query.pool || j->pending_uncacheable) {
    vk_draw_flush_pending();draw_emission_immediate(j,p,query);return;
  }
  if(j!=active_job || j->local || V.cmd!=j->secondary || !graphics_state)
    die("Vulkan pending draw outside its ordinary recording");
  if(j->pending_count==j->pending_capacity) {
    unsigned capacity=j->pending_capacity?j->pending_capacity*2:32;
    size_t extra=(capacity-j->pending_capacity)*sizeof *j->pending_draws;
    vk_draw_emission *grown=NULL;
    if(capacity<=2048 && extra<=16u*1024*1024-graphics_state->pending_bytes)
      grown=realloc(j->pending_draws,capacity*sizeof *grown);
    if(!grown) {
      // A full prefix is not a persistent run: keep correctness via immediate
      // delivery and remember that this entire job cannot later be retained.
      vk_draw_flush_pending();j->pending_uncacheable=1;
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
      draw_pending_abandoned++;
#endif
      draw_emission_immediate(j,p,query);return;
    }
    j->pending_draws=grown;j->pending_capacity=capacity;
    graphics_state->pending_bytes+=extra;
  }
  j->pending_draws[j->pending_count++]=*p;
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  draw_pending_queued++;
  if(j->pending_count>draw_pending_max)draw_pending_max=j->pending_count;
#endif
}
