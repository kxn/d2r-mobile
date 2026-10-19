// Real two-subpass recording. Never reuse a single-subpass secondary inside
// a newly invented topology, and never replay the first draw on fallback.
struct pl_job {
  pl_candidate plan;
  VkCommandBuffer first;
  vk_attachment second_attachments[8],depth_attachment;
  VkAttachmentReference second_colors[8],second_depth,inputs[8];
  VkImageView input_views[8];VkImageLayout input_layouts[8];
  vk_pixel_local_image tokens[8];
  tex_t textures[9];view_t views[9];uint8_t has_view[9];
  uint32_t next,subpass,second_count,second_has_depth,clear_seen;
  const uint8_t *current;
  int finished;
};
static int pl_add_attachment(pl_job *p,const tex_t *t,const view_t *v,
    VkAttachmentDescription descriptions[9],VkImageView handles[9],unsigned *count,
    const vk_attachment *a) {
  for(unsigned n=0;n<*count;n++)if(pl_tex_same(t,&p->textures[n])) {
    const view_t *old=p->has_view[n]?&p->views[n]:NULL;
    int level=v&&v->set_levels?v->base_level:0,layer=v&&v->set_layers?v->min_layer:0;
    int old_level=old&&old->set_levels?old->base_level:0,old_layer=old&&old->set_layers?old->min_layer:0;
    int format=v&&v->set_format?v->format:t->d.format,old_format=old&&old->set_format?old->format:t->d.format;
    return level==old_level && layer==old_layer && format==old_format?(int)n:-1;
  }
  if(*count==9 || !t || !a->view)return -1;
  for(unsigned n=0;n<*count;n++)if(pl_tex_alias(t,&p->textures[n]))return -1;
  unsigned n=(*count)++;p->textures[n]=*t;p->has_view[n]=!!v;if(v)p->views[n]=*v;
  handles[n]=a->view;
  descriptions[n]=(VkAttachmentDescription){.format=a->format,.samples=a->samples,
    .loadOp=VK_ATTACHMENT_LOAD_OP_LOAD,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
    .stencilLoadOp=VK_ATTACHMENT_LOAD_OP_LOAD,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_STORE,
    .initialLayout=a->layout,.finalLayout=a->layout};
  return n;
}
static pl_job *pl_prepare(const p_draw *draw,const p_draw_idx *idx,unsigned width,unsigned height) {
  if(active_job)return NULL;
  uint64_t recording=vk_recording_serial(),passes=V.passes;
  pl_candidate *candidate=pl_plan(draw,idx,width,height);if(!candidate)return NULL;
  pl_job *p=calloc(1,sizeof *p);if(!p)return NULL;p->plan=*candidate;
  p->second_count=p->plan.second.targets.n;
  for(unsigned k=0;k<p->second_count;k++) {
    const p_set_rt *rt=&p->plan.second.targets;
    vk_texture_prepare_attachment(rt->color[k],rt->has_cview[k]?&rt->cview[k]:NULL);
    p->second_attachments[k]=vk_texture_attachment(rt->color[k],rt->has_cview[k]?&rt->cview[k]:NULL);
    vk_attachment *a=&p->second_attachments[k];
    if(a->samples!=VK_SAMPLE_COUNT_1_BIT || a->extent.width!=width || a->extent.height!=height)goto fail;
  }
  if(p->plan.second.targets.depth) {
    const p_set_rt *rt=&p->plan.second.targets;
    p->depth_attachment=vk_texture_readonly_attachment(rt->depth,rt->has_dview?&rt->dview:NULL);
    vk_attachment *a=&p->depth_attachment;
    if(a->samples!=VK_SAMPLE_COUNT_1_BIT || a->extent.width!=width || a->extent.height!=height)goto fail;
    p->second_has_depth=1;
  }
  for(unsigned round=0;round<2;round++)for(unsigned k=0;k<p->plan.fragment->count;k++) {
    int local=0;for(int a=0;a<S.targets.n;a++)local|=pl_tex_same(&p->plan.inputs[k],S.targets.color[a]);
    if(!vk_texture_pixel_local_prepare(&p->plan.inputs[k],local,&p->tokens[k]))goto fail;
  }
  for(unsigned k=0;k<p->plan.fragment->count;k++)if(!vk_texture_pixel_local_valid(&p->tokens[k]))goto fail;
  // A DS attachment absent from subpass0 still LOADs at union-pass entry.
  // Establish old writers/readers -> real LOAD/STORE before beginning it,
  // rather than trying to hoist an attachment hazard from inside subpass1.
  if(!p->plan.first.targets.depth && p->second_has_depth) {
    const p_set_rt *rt=&p->plan.second.targets;
    int store=!V.load_store_op_none; // Same condition as pl_job_create below.
    if(!vk_texture_pixel_local_depth_preflight(rt->depth,rt->has_dview?&rt->dview:NULL,
        &p->depth_attachment,store))goto fail;
  }
  for(unsigned k=0;k<p->plan.fragment->count;k++)if(!vk_texture_pixel_local_valid(&p->tokens[k]))goto fail;
  if(recording!=vk_recording_serial() || passes!=V.passes)goto fail;
  if(memcmp(&p->plan.first,&S,sizeof S) || p->plan.generation!=vk_pass_generation() ||
      p->plan.descriptor_epoch!=vk_texture_descriptor_epoch())goto fail;
  return p;
fail:free(p);return NULL;
}
static int pl_job_create(render_job *j,pl_job *p,VkAttachmentDescription descriptions[9],
    VkImageView handles[9],VkAttachmentReference colors[8],const VkAttachmentReference *depth,
    unsigned *count,unsigned width,unsigned height) {
  if(!p)return 0;
  if(p->plan.width!=width || p->plan.height!=height) {free(p);return 0;}
  for(unsigned k=0;k<p->plan.fragment->count;k++)if(!vk_texture_pixel_local_valid(&p->tokens[k])) {free(p);return 0;}
  unsigned original=*count;
  VkAttachmentDescription original_descriptions[9];VkImageView original_handles[9];
  memcpy(original_descriptions,descriptions,original*sizeof *descriptions);
  memcpy(original_handles,handles,original*sizeof *handles);
  for(unsigned n=0;n<original;n++) {
    int found=0;
    for(int k=0;k<S.targets.n;k++)if(colors[k].attachment==n && S.targets.color[k]) {
      p->textures[n]=*S.targets.color[k];p->has_view[n]=S.targets.has_cview[k];
      if(p->has_view[n])p->views[n]=S.targets.cview[k];found=1;break;
    }
    if(!found && depth && depth->attachment==n && S.targets.depth) {
      p->textures[n]=*S.targets.depth;p->has_view[n]=S.targets.has_dview;
      if(p->has_view[n])p->views[n]=S.targets.dview;found=1;
    }
    if(!found)goto fallback;
  }
  for(unsigned a=0;a<original;a++)for(unsigned b=0;b<a;b++)
    if(pl_tex_alias(&p->textures[a],&p->textures[b]))goto fallback;
  for(unsigned k=0;k<p->plan.fragment->count;k++) {
    vk_pixel_local_image *token=&p->tokens[k];if(!vk_texture_pixel_local_valid(token))goto fallback;
    int a=pl_add_attachment(p,&p->plan.inputs[k],NULL,descriptions,handles,count,&token->attachment);
    if(a<0)goto fallback;
    // The same physical single-mip view is used by framebuffer and descriptor.
    if(token->local && descriptions[a].initialLayout!=token->attachment.layout)goto fallback;
    handles[a]=token->attachment.view;descriptions[a].initialLayout=token->attachment.layout;
    descriptions[a].finalLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if(!token->local && V.load_store_op_none)descriptions[a].storeOp=VK_ATTACHMENT_STORE_OP_NONE_EXT;
    p->inputs[k]=(VkAttachmentReference){a,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    p->input_views[k]=token->attachment.view;p->input_layouts[k]=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  }
  p->second_count=p->plan.second.targets.n;
  for(unsigned k=0;k<p->second_count;k++) {
    const p_set_rt *rt=&p->plan.second.targets;
    vk_attachment a=p->second_attachments[k];
    if(a.samples!=VK_SAMPLE_COUNT_1_BIT || a.extent.width!=width || a.extent.height!=height)goto fallback;
    int n=pl_add_attachment(p,rt->color[k],rt->has_cview[k]?&rt->cview[k]:NULL,descriptions,handles,count,&a);
    if(n<0)goto fallback;p->second_colors[k]=(VkAttachmentReference){n,a.layout};
    // Clear commands remain at their exact command position in subpass1.
    // LOAD keeps an early-aborted plan from executing a future clear.
  }
  if(p->plan.second.targets.depth) {
    const p_set_rt *rt=&p->plan.second.targets;
    vk_attachment a=p->depth_attachment;
    if(a.samples!=VK_SAMPLE_COUNT_1_BIT || a.extent.width!=width || a.extent.height!=height)goto fallback;
    int n=pl_add_attachment(p,rt->depth,rt->has_dview?&rt->dview:NULL,descriptions,handles,count,&a);
    if(n<0)goto fallback;p->second_depth=(VkAttachmentReference){n,a.layout};p->second_has_depth=1;
    // Preserve the backend's existing read-only DS STORE_NONE optimization.
    // A shared first DS must also stay physically read-only throughout.
    if(V.load_store_op_none && a.layout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL &&
        (!depth || depth->attachment!=(unsigned)n || depth->layout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL))
      descriptions[n].storeOp=descriptions[n].stencilStoreOp=VK_ATTACHMENT_STORE_OP_NONE_EXT;
  }
  // All callbacks/promotions have completed. Consumer admission will recheck
  // every descriptor, physical token and current geometry at its own position.
  for(unsigned k=0;k<p->plan.fragment->count;k++)if(!vk_texture_pixel_local_valid(&p->tokens[k]))goto fallback;
  p->plan.descriptor_epoch=vk_texture_descriptor_epoch();
  uint32_t preserve[2][9],np[2]={0};
  for(unsigned a=0;a<*count;a++) {
    int first=depth && depth->attachment==a,second=p->second_has_depth && p->second_depth.attachment==a;
    for(int k=0;k<S.targets.n;k++)first|=colors[k].attachment==a;
    for(unsigned k=0;k<p->second_count;k++)second|=p->second_colors[k].attachment==a;
    for(unsigned k=0;k<p->plan.fragment->count;k++)second|=p->inputs[k].attachment==a;
    if(!first)preserve[0][np[0]++]=a;if(!second)preserve[1][np[1]++]=a;
  }
  VkSubpassDescription sub[2]={
    {.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=S.targets.n,.pColorAttachments=colors,
     .pDepthStencilAttachment=depth,.preserveAttachmentCount=np[0],.pPreserveAttachments=preserve[0]},
    {.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.inputAttachmentCount=p->plan.fragment->count,.pInputAttachments=p->inputs,
     .colorAttachmentCount=p->second_count,.pColorAttachments=p->second_colors,
     .pDepthStencilAttachment=p->second_has_depth?&p->second_depth:NULL,
     .preserveAttachmentCount=np[1],.pPreserveAttachments=preserve[1]}};
  VkSubpassDependency dependency={.srcSubpass=0,.dstSubpass=1,
    .srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
    .dstStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
    .srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_INPUT_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
    .dependencyFlags=VK_DEPENDENCY_BY_REGION_BIT};
  VkRenderPassCreateInfo rp={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=*count,
    .pAttachments=descriptions,.subpassCount=2,.pSubpasses=sub,.dependencyCount=1,.pDependencies=&dependency};
  j->pass_object=render_pass_acquire(&rp);j->pass=j->pass_object->handle;
  draw_recorded.pixel_local_plans++;
  j->local=p;j->preserve_stores=1;j->attachment_count=*count;j->color_count=S.targets.n;
  memcpy(j->attachments,descriptions,*count*sizeof *descriptions);memcpy(j->colors,colors,S.targets.n*sizeof *colors);
  if(depth)j->depth=*depth;
  return 1;
fallback:
  // Added resources may have been promoted, but no secondary or guest command
  // was consumed. Original draw resolution must be restarted by the caller.
  memcpy(descriptions,original_descriptions,original*sizeof *descriptions);
  memcpy(handles,original_handles,original*sizeof *handles);free(p);*count=original;return 0;
}
static int pl_render_end(render_job *j) {
  pl_job *p=j->local;if(!p)return 0;
  if(!p->finished)draw_recorded.pixel_local_aborts++;
  vk_counter_pass_end();VK_CHECK(vkEndCommandBuffer(j->secondary));V.cmd=j->primary;
  VkRenderPassBeginInfo begin={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=j->pass,
    .framebuffer=j->framebuffer,.renderArea=j->area,.clearValueCount=j->attachment_count,.pClearValues=j->clears};
  vkCmdBeginRenderPass(V.cmd,&begin,VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
  VkCommandBuffer first=p->subpass?p->first:j->secondary;
  vkCmdExecuteCommands(V.cmd,1,&first);
  vkCmdNextSubpass(V.cmd,VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
  if(p->subpass)vkCmdExecuteCommands(V.cmd,1,&j->secondary);
  vkCmdEndRenderPass(V.cmd);
  if(!p->subpass)for(unsigned k=0;k<p->plan.fragment->count;k++)
    if(!vk_texture_pixel_local_enter(&p->tokens[k]))die("Vulkan pixel-local fallback physical lifetime");
  // The empty-second fallback still performs each original producer STORE.
  // Future consumers therefore see defined contents, with no replayed draws.
  active_job=NULL;active_writes=0;
  for(unsigned n=0;n<j->attachment_count;n++)if(p->textures[n].serial &&
      j->attachments[n].storeOp==VK_ATTACHMENT_STORE_OP_STORE)
    vk_texture_attachment_store(&p->textures[n],p->has_view[n]?&p->views[n]:NULL,1);
  vk_gpu_region_end(j->gpu_region);return 1;
}
int vk_pixel_local_target_boundary(const void *op) {
  return active_job && active_job->local && !active_job->local->subpass &&
      active_job->local->plan.set_op==op && active_job->local->plan.generation==vk_pass_generation();
}
int vk_pixel_local_replay(const uint8_t *op) {
  if(!active_job || !active_job->local)return 0;
  pl_job *p=active_job->local;
  if(p->finished) {vk_render_end();return 0;}
  if(p->plan.generation!=vk_pass_generation() || p->next>=p->plan.count || p->plan.ops[p->next]!=op) {
    vk_render_end();return 0;
  }
  p->current=op;p->next++;return 0;
}
static int pl_enter(render_job *j);
static int pl_target_clear(const p_set_rt *rt,const p_scissor *sc,int index,
    VkImageAspectFlags aspect,const VkClearValue *value) {
  if(!active_job || !active_job->local)return 0;
  pl_job *p=active_job->local;
  if(p->finished || !p->current || ((const op_hdr *)p->current)->op!=OP_CLEAR_COLOR ||
      index<0 || index>=(int)p->second_count || !p->plan.clears[index] ||
      (p->clear_seen&(1u<<index)) || aspect!=VK_IMAGE_ASPECT_COLOR_BIT ||
      !target_fields_equal(rt,&p->plan.second.targets) ||
      memcmp(value,&p->plan.clear_values[index],sizeof *value) ||
      (sc && (sc->x>0 || sc->y>0 || (int64_t)sc->x+sc->w<p->plan.width || (int64_t)sc->y+sc->h<p->plan.height)))return 0;
  if(!pl_enter(active_job))return 0;
  VkClearAttachment clear={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.colorAttachment=index,.clearValue=*value};
  VkClearRect rect={.rect={{0,0},{p->plan.width,p->plan.height}},.layerCount=1};
  vkCmdClearAttachments(V.cmd,1,&clear,1,&rect);
  // Keep original metadata/zero tracking at this exact logical command.
  p->clear_seen|=1u<<index;active_writes|=1u<<index;
  V.nvn_barrier|=NVN_BARRIER_INVALIDATE_TEXTURE_BIT;
  vk_texture_attachment_record(rt->color[index],rt->has_cview[index]?&rt->cview[index]:NULL,0);
  return 1;
}
static const vk_pixel_local_proof *pl_consumer(vk_shader *vs,vk_shader *fs,VkShaderModule actual,
    const p_draw *draw,const p_draw_idx *idx) {
  render_job *j=active_job;if(!j || !j->local)return NULL;pl_job *p=j->local;
  uint64_t recording=vk_recording_serial(),pass=V.passes;
  const void *payload=draw?(const void *)draw:(const void *)idx;
  if(!p->current || p->current+8!=payload || p->current!=p->plan.draw_op)return NULL;
  pl_candidate *test=pl_scratch;test->width=p->plan.width;test->height=p->plan.height;
  const vk_pixel_local_proof *proof=pl_local_subset(test,&S,fs,actual,&p->plan.first.targets),*vertex=vk_shader_pixel_local(vs,vs->module);
  if(!proof || !vertex || proof->selection_mask!=p->plan.fragment->selection_mask ||
      proof->group_count!=p->plan.fragment->group_count || proof->set!=p->plan.fragment->set || proof->count!=p->plan.fragment->count ||
      proof->uv_location!=p->plan.fragment->uv_location ||
      memcmp(proof->bindings,p->plan.fragment->bindings,proof->count*sizeof *proof->bindings) ||
      !target_fields_equal(&S.targets,&p->plan.second.targets) || !pl_consumer_resources(&S,&p->plan,proof))goto fail;
  int geometry=pl_geometry(&S,vertex,proof->uv_location,draw,idx,p->plan.width,p->plan.height);
  // A reference CPU read can submit/wait/recycle the context. Do not touch p
  // after that callback until both identity and recording lifetime still match.
  if(active_job!=j || recording!=vk_recording_serial() || pass!=V.passes)return NULL;
  if(!geometry)goto fail;
  uint64_t epoch=vk_texture_descriptor_epoch();
  test=pl_scratch;test->width=p->plan.width;test->height=p->plan.height;
  if(!pl_inputs(test,&S,proof))goto fail;
  for(unsigned k=0;k<proof->count;k++)if(test->handles[k]!=p->plan.handles[k] ||
      !pl_tex_same(&test->inputs[k],&p->plan.inputs[k]) || test->has_input_view[k]!=p->plan.has_input_view[k] ||
      (test->has_input_view[k] && memcmp(&test->input_views[k],&p->plan.input_views[k],sizeof(view_t))) ||
      !vk_texture_pixel_local_valid(&p->tokens[k]))goto fail;
  if(epoch!=vk_texture_descriptor_epoch())goto fail;
  for(unsigned k=0;k<p->second_count;k++)if(p->plan.clears[k] && !(p->clear_seen&(1u<<k)))goto fail;
  return proof;
fail:vk_render_end();return NULL;
}
static int pl_enter(render_job *j) {
  pl_job *p=j->local;if(!p || active_job!=j || V.cmd!=j->secondary)return 0;
  if(p->subpass)return 1;
  if(!vk_ghost_idle()) {vk_render_end();return 0;}
  for(unsigned k=0;k<p->plan.fragment->count;k++)if(!vk_texture_pixel_local_valid(&p->tokens[k]))return 0;
  vk_counter_pass_end();VK_CHECK(vkEndCommandBuffer(j->secondary));p->first=j->secondary;
  j->secondary=vk_command_buffer(VK_COMMAND_BUFFER_LEVEL_SECONDARY);
  VkCommandBufferInheritanceInfo inherit={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
    .renderPass=j->pass,.subpass=1,.framebuffer=j->framebuffer};
  VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT|VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,.pInheritanceInfo=&inherit};
  VK_CHECK(vkBeginCommandBuffer(j->secondary,&begin));V.cmd=j->secondary;p->subpass=1;
  memset(&j->bound_pipeline,0,sizeof j->bound_pipeline);j->bound_layout=VK_NULL_HANDLE;
  memset(j->bound_sets,0,sizeof j->bound_sets);memset(j->bound_uniform_offsets,0,sizeof j->bound_uniform_offsets);
  memset(&j->bias_state,0,sizeof j->bias_state);j->dynamic_state_valid=0;render_binding_reset(j);
  for(unsigned k=0;k<p->plan.fragment->count;k++)if(!vk_texture_pixel_local_enter(&p->tokens[k]))die("Vulkan pixel-local token changed without a callback");
  active_targets=S.targets;active_writes=0;
  j->has_depth=p->second_has_depth;j->depth=p->second_depth;j->color_count=p->second_count;
  memcpy(j->colors,p->second_colors,p->second_count*sizeof *j->colors);return 1;
}
