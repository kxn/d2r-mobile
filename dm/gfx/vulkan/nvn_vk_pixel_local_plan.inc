// Bounded real-command lookahead corresponding to upstream fuse_try. No
// instruction is removed or replayed by this planner. All identities are
// rechecked where the second subpass is actually entered.
enum { PL_MAX_OPS=1024 };
typedef struct {
  uint64_t generation,descriptor_epoch;
  const uint8_t *set_op,*draw_op,*ops[PL_MAX_OPS];
  unsigned count;
  vk_state first,second;
  const vk_pixel_local_proof *fragment,*vertex;
  tex_t inputs[8];view_t input_views[8];int has_input_view[8];
  uint64_t handles[8];
  uint8_t clears[8];VkClearValue clear_values[8];
  unsigned width,height;
} pl_candidate;
static pl_candidate *pl_scratch;
static int pl_payload(const op_hdr *h,size_t bytes) {return h->size>=8 && bytes<=h->size-8;}
static int pl_shadow(vk_state *state,const uint8_t *op) {
  op_hdr h;memcpy(&h,op,sizeof h);const void *data=op+8;
  switch(h.op) {
  case OP_BEGIN:case OP_END:case OP_CALL:case OP_JUMP:return 1;
  case OP_BARRIER:
    if(!pl_payload(&h,8))return 0;
    // Ordering requests with buffer/indirect effects are not a local fetch
    // dependency. Keep the original pass boundary for all unknown flag bits.
    return !(*(const uint64_t *)data & ~(uint64_t)NVN_BARRIER_INVALIDATE_TEXTURE_BIT);
  case OP_SET_RT:if(!pl_payload(&h,sizeof(p_set_rt)))return 0;state->targets=*(const p_set_rt *)data;return 1;
  case OP_SCISSOR:if(!pl_payload(&h,sizeof(p_scissor)))return 0;state->scissor=*(const p_scissor *)data;state->has_scissor=1;return 1;
  case OP_BIND_PROGRAM:if(!pl_payload(&h,sizeof(p_bind_program)))return 0;state->program=((const p_bind_program *)data)->prog;return 1;
  case OP_BIND_STATE: {
    if(!pl_payload(&h,sizeof(p_bind_state)))return 0;
    const p_bind_state *p=data;
    switch(p->kind) {
    case 0:{blend_st b;memcpy(&b,p->bytes,sizeof b);if(b.target>=8)return 0;state->blend[b.target]=b;return 1;}
    case 1:memcpy(&state->color,p->bytes,sizeof state->color);return 1;
    case 2:memcpy(&state->chmask,p->bytes,sizeof state->chmask);return 1;
    case 3:memcpy(&state->ms,p->bytes,sizeof state->ms);return 1;
    case 4:memcpy(&state->poly,p->bytes,sizeof state->poly);return 1;
    case 5:memcpy(&state->ds,p->bytes,sizeof state->ds);return 1;
    default:return 0;
    }
  }
  case OP_BIND_VATTR: {
    if(!pl_payload(&h,8))return 0;const p_bind_vattr *p=data;
    if(p->first<0||p->n<0||p->first>16||p->n>16-p->first||!pl_payload(&h,8+(size_t)p->n*sizeof *p->st))return 0;
    memcpy(state->attr+p->first,p->st,p->n*sizeof *p->st);return 1;
  }
  case OP_BIND_VSTREAM: {
    if(!pl_payload(&h,8))return 0;const p_bind_vstream *p=data;
    if(p->first<0||p->n<0||p->first>16||p->n>16-p->first||!pl_payload(&h,8+(size_t)p->n*sizeof *p->st))return 0;
    memcpy(state->stream+p->first,p->st,p->n*sizeof *p->st);return 1;
  }
  case OP_BIND_VBUF: {
    if(!pl_payload(&h,8))return 0;const p_bind_vbuf *p=data;
    if(p->first<0||p->n<0||p->first>16||p->n>16-p->first||!pl_payload(&h,8+(size_t)p->n*sizeof *p->r))return 0;
    memcpy(state->vertex+p->first,p->r,p->n*sizeof *p->r);return 1;
  }
  case OP_BIND_UBO:case OP_BIND_SSBO: {
    if(!pl_payload(&h,sizeof(p_bind_buf)))return 0;const p_bind_buf *p=data;
    if(p->stage<0||p->stage>=6||p->index<0||p->index>=16)return 0;
    (h.op==OP_BIND_UBO?state->ubo:state->ssbo)[p->stage][p->index]=*p;return 1;
  }
  case OP_BIND_TEX:case OP_BIND_SAMP:case OP_BIND_IMG: {
    if(!pl_payload(&h,16))return 0;const p_bind_handles *p=data;int max=h.op==OP_BIND_IMG?32:128;
    if(p->stage<0||p->stage>=6||p->first<0||p->n<0||p->first>max||p->n>max-p->first||!pl_payload(&h,16+(size_t)p->n*8))return 0;
    uint64_t *dest=h.op==OP_BIND_IMG?state->img[p->stage]:h.op==OP_BIND_TEX?state->tex[p->stage]:state->samp[p->stage];
    memcpy(dest+p->first,p->h,(size_t)p->n*8);return 1;
  }
  case OP_VIEWPORTS: {
    if(!pl_payload(&h,8))return 0;const p_viewports *p=data;
    if(p->first<0||p->n<0||p->first>16||p->n>16-p->first||!pl_payload(&h,8+(size_t)p->n*sizeof *p->v))return 0;
    memcpy(state->viewport+p->first,p->v,p->n*sizeof *p->v);state->has_viewport=1;return 1;
  }
  case OP_DEPTH_RANGE:if(!pl_payload(&h,sizeof(p_depth_range)))return 0;state->depth_range[0]=((const p_depth_range *)data)->n;state->depth_range[1]=((const p_depth_range *)data)->f;return 1;
  case OP_BLEND_COLOR:if(!pl_payload(&h,sizeof(p_blend_color)))return 0;memcpy(state->blend_color,((const p_blend_color *)data)->rgba,16);return 1;
  case OP_POLY_OFFSET:if(!pl_payload(&h,sizeof(p_poly_offset)))return 0;state->offset=*(const p_poly_offset *)data;return 1;
  case OP_STENCIL: {
    if(!pl_payload(&h,sizeof(p_stencil)))return 0;const p_stencil *p=data;if(p->which<0 || p->which>2 || (p->face&~3))return 0;
    for(unsigned f=0;f<2;f++)if(p->face&(1u<<f)) {
      if(p->which==0)state->stencil_value[f]=p->value_mask;else if(p->which==1)state->stencil_mask[f]=p->mask;else state->stencil_ref[f]=p->ref;
    }return 1;
  }
  default:return 0;
  }
}
static int pl_tex_same(const tex_t *a,const tex_t *b) {
  return a && b && a->serial && a->serial==b->serial && a->mem==b->mem &&
      !memcmp(&a->d,&b->d,sizeof a->d) && !memcmp(&a->L,&b->L,sizeof a->L) &&
      a->has_layout==b->has_layout && !memcmp(a->layout,b->layout,sizeof a->layout);
}
static int pl_tex_alias(const tex_t *a,const tex_t *b) {
  if(!a || !b)return 0;if(a->serial==b->serial)return 1;
  uintptr_t x=(uintptr_t)a->mem,y=(uintptr_t)b->mem;
  return x && y && a->L.size && b->L.size &&
      (x<=y?y-x<a->L.size:x-y<b->L.size);
}
static int pl_texture_shape(const tex_t *t,const view_t *view,unsigned width,unsigned height) {
  if(t && (t->d.format==NVN_FORMAT_RGBX8 || t->d.format==NVN_FORMAT_RGBX8_SRGB ||
      t->d.format==NVN_FORMAT_BGRX8 || t->d.format==NVN_FORMAT_BGRX8_SRGB))return 0;
  if(!t || !t->serial || t->d.target!=NVN_TEXTURE_TARGET_2D || t->d.samples>1 || t->L.layers!=1 ||
      !t->d.levels || t->d.levels>16)return 0;
  if(view && ((view->set_target && view->target!=NVN_TEXTURE_TARGET_2D) ||
      (view->set_format && view->format!=t->d.format) ||
      (view->set_layers && (view->min_layer || view->nlayers>1)) ||
      (view->set_levels && (view->base_level || view->nlevels<0 || view->nlevels>t->d.levels))))return 0;
  return t->d.width==width && t->d.height==height;
}
// Refine only the producer read-vs-hoisted-image check. A descriptor's
// single physical texture can expose a sibling mip without reading the
// attachment. Unspecified ranges remain opaque (especially external history
// inputs); no whole-image/guest-memory alias proof is weakened.
static int pl_sampled_mips_disjoint(const tex_t *target,const view_t *tv,
                                  const tex_t *sampled,const view_t *sv) {
  if(!pl_tex_same(target,sampled) || !tv || !sv ||
      target->d.target!=NVN_TEXTURE_TARGET_2D || target->L.layers!=1 ||
      target->d.depth!=1 || target->d.width<1 || target->d.height<1 ||
      target->d.samples>1 || target->d.levels<2 || target->d.levels>16)return 0;
  const view_t *views[]={tv,sv};
  for(unsigned k=0;k<2;k++) {
    const view_t *v=views[k];
    if(!v->set_levels || v->base_level<0 || v->nlevels<=0 ||
        v->base_level>=target->d.levels || v->nlevels>target->d.levels-v->base_level ||
        (v->set_format && v->format!=target->d.format) ||
        (v->set_target && v->target!=NVN_TEXTURE_TARGET_2D) ||
        (v->set_layers && (v->min_layer || v->nlayers!=1)))return 0;
  }
  // Subtraction follows checked nonnegative bounds; no end-index overflow.
  return tv->base_level<sv->base_level?
      sv->base_level-tv->base_level>=tv->nlevels:
      tv->base_level-sv->base_level>=sv->nlevels;
}
static int pl_shader_read_alias(const vk_state *s,const tex_t *target,const view_t *target_view) {
  vk_shader *list[6]={0};if(!s->program)return 1;vk_program_shaders(s->program,list);
  if(list[2]||list[3]||list[4]||!list[0]||!list[1])return 1;
  for(unsigned stage=0;stage<2;stage++) {
    if(list[stage]->has_resource_writes)return 1;
    for(unsigned k=0;k<list[stage]->nb;k++) {
      const vk_binding *b=&list[stage]->bindings[k];int image=b->kind==4 || b->kind==9;
      if(b->kind<3 || b->kind==5 || b->kind==8)continue;
      if(b->count!=1 || (!image && b->kind!=3 && b->kind!=6 && b->kind!=7))return 1;
      if(b->reg<0 || b->reg>=(image?32:128))return 1;
      uint64_t h=image?s->img[stage][b->reg]:s->tex[stage][b->reg];tex_t t;view_t view;int has_view;
      if(!vk_texture_descriptor_view(h&0xfffff,&t,&view,&has_view))return 1;
      if((b->kind==3 || b->kind==6) && has_view &&
          pl_sampled_mips_disjoint(target,target_view,&t,&view))continue;
      if(pl_tex_alias(&t,target))return 1;
    }
  }return 0;
}
static int pl_inputs(pl_candidate *plan,const vk_state *state,const vk_pixel_local_proof *proof) {
  if(!proof || !proof->count || proof->count>8 || proof->set!=1 || !state->program)return 0;
  vk_shader *list[6]={0};vk_program_shaders(state->program,list);if(!list[1])return 0;
  unsigned seen=0;
  for(unsigned k=0;k<proof->count;k++) {
    const vk_pixel_local_binding *input=&proof->bindings[k];const vk_binding *binding=NULL;
    if(input->input_index>=proof->count || (seen&(1u<<input->input_index)) || !input->lod_mask || (input->lod_mask&~3))return 0;
    seen|=1u<<input->input_index;
    for(unsigned b=0;b<list[1]->nb;b++)if(list[1]->bindings[b].binding==input->binding) {
      if(binding)return 0;binding=&list[1]->bindings[b];
    }
    if(!binding || binding->count!=1 || binding->kind!=3 || binding->reg<0 || binding->reg>=128)return 0;
    uint64_t h=state->tex[1][binding->reg];int sid=binding->sampler>=0?
        (binding->sampler<128?(int)(state->samp[1][binding->sampler]>>20):-1):(int)(h>>20);
    samp_par sampler;unsigned index=input->input_index;
    if(!h || sid<0 || !vk_sampler_descriptor(sid,&sampler) || !isfinite(sampler.lod_min) ||
        !isfinite(sampler.lod_max) || !isfinite(sampler.lod_bias) || !isfinite(sampler.aniso) ||
        sampler.cmp_mode || sampler.aniso>1 || sampler.lod_bias!=0 || sampler.lod_min>0 ||
        sampler.lod_max<0 || sampler.minf>5 || sampler.magf>1)return 0;
    if(!vk_texture_descriptor_view(h&0xfffff,&plan->inputs[index],&plan->input_views[index],&plan->has_input_view[index]) ||
        !pl_texture_shape(&plan->inputs[index],plan->has_input_view[index]?&plan->input_views[index]:NULL,plan->width,plan->height))return 0;
    plan->handles[index]=h;
  }
  return seen==((1u<<proof->count)-1);
}
// Canonical group bit positions are ascending (set,binding), never raw binding
// numbers. Only producer MRT intersections become inputs; other samples remain
// in the original descriptor schema and execute normal dynamic dependencies.
static const vk_pixel_local_proof *pl_local_subset(pl_candidate *p,const vk_state *state,
    vk_shader *shader,VkShaderModule actual,const p_set_rt *producer) {
  const vk_pixel_local_proof *full=vk_shader_pixel_local(shader,actual);
  if(!full || !pl_inputs(p,state,full))return NULL;
  unsigned mask=0;
  for(unsigned k=0;k<full->count;k++)for(int a=0;a<producer->n;a++)
    if(pl_tex_same(&p->inputs[k],producer->color[a]))mask|=1u<<k;
  if(!mask)return NULL;
  const vk_pixel_local_proof *selected=vk_shader_pixel_local_subset(shader,actual,mask);
  if(!selected || selected->selection_mask!=mask || selected->group_count!=full->count ||
      selected->uv_location!=full->uv_location || selected->source_shader!=full->source_shader)return NULL;
  // Copy already validated descriptor facts into compact selected order. No
  // resource operation/callback occurs between the full proof and this copy.
  unsigned at=0;
  for(unsigned bit=0;bit<full->count;bit++)if(mask&(1u<<bit)) {
    if(at>=selected->count || selected->bindings[at].binding!=full->bindings[bit].binding ||
        selected->bindings[at].lod_mask!=full->bindings[bit].lod_mask)return NULL;
    if(at!=bit) {
      p->inputs[at]=p->inputs[bit];p->input_views[at]=p->input_views[bit];
      p->has_input_view[at]=p->has_input_view[bit];p->handles[at]=p->handles[bit];
    }
    at++;
  }
  return at==selected->count?selected:NULL;
}
static int pl_attachment_shape(const tex_t *t,const view_t *v,unsigned w,unsigned h) {
  return pl_texture_shape(t,v,w,h) && (!v || !v->set_levels || v->nlevels==1);
}
static int pl_no_writers(const vk_state *s) {
  vk_shader *list[6]={0};if(!s->program)return 0;vk_program_shaders(s->program,list);
  if(!list[0]||!list[1]||list[2]||list[3]||list[4]||list[5])return 0;
  return !list[0]->has_resource_writes && !list[1]->has_resource_writes;
}
static int pl_no_ds_writes(const vk_state *s) {
  if(s->ds.dt && s->ds.dw)return 0;
  return !s->ds.st ||
      ((!s->stencil_mask[0] || (s->ds.fail_f==NVN_STENCIL_OP_KEEP && s->ds.zfail_f==NVN_STENCIL_OP_KEEP && s->ds.zpass_f==NVN_STENCIL_OP_KEEP)) &&
       (!s->stencil_mask[1] || (s->ds.fail_b==NVN_STENCIL_OP_KEEP && s->ds.zfail_b==NVN_STENCIL_OP_KEEP && s->ds.zpass_b==NVN_STENCIL_OP_KEEP)));
}
static int pl_draw_header(const uint8_t *op,const p_draw **draw,const p_draw_idx **idx) {
  op_hdr h;memcpy(&h,op,8);*draw=NULL;*idx=NULL;
  if(h.op==OP_DRAW && pl_payload(&h,sizeof **draw))*draw=(const void *)(op+8);
  else if(h.op==OP_DRAW_IDX && pl_payload(&h,sizeof **idx))*idx=(const void *)(op+8);
  else return 0;
  int primitive=*draw?(*draw)->prim:(*idx)->prim;
  return primitive==NVN_DRAW_PRIMITIVE_TRIANGLES || primitive==NVN_DRAW_PRIMITIVE_TRIANGLE_STRIP || primitive==NVN_DRAW_PRIMITIVE_TRIANGLE_FAN;
}
static int pl_clear(pl_candidate *p,const vk_state *s,const op_hdr *h,const uint8_t *op) {
  if(h->op!=OP_CLEAR_COLOR || !pl_payload(h,sizeof(p_clear_color)))return 0;
  const p_clear_color *c=(const void *)(op+8);
  if(c->index<0 || c->index>=s->targets.n || c->mask!=15 || p->clears[c->index] ||
      !s->targets.color[c->index] ||
      (s->has_scissor && (s->scissor.x>0 || s->scissor.y>0 ||
        (int64_t)s->scissor.x+s->scissor.w<p->width || (int64_t)s->scissor.y+s->scissor.h<p->height)))return 0;
  for(unsigned k=0;k<4;k++)if(!isfinite(c->rgba[k]))return 0;
  p->clears[c->index]=1;memcpy(p->clear_values[c->index].color.float32,c->rgba,16);return 1;
}
static pl_candidate *pl_plan(const p_draw *draw,const p_draw_idx *idx,unsigned width,unsigned height) {
  if(!vk_pixel_local_enabled() || !width || !height || !pl_no_writers(&S) || !vk_ghost_idle())return NULL;
  if(!pl_scratch)pl_scratch=calloc(1,sizeof *pl_scratch);
  if(!pl_scratch)return NULL;
  pl_candidate *p=pl_scratch;memset(p,0,sizeof *p);
  p->generation=vk_pass_generation();p->descriptor_epoch=vk_texture_descriptor_epoch();p->width=width;p->height=height;p->first=S;
  vk_state scan=S;const uint8_t *current;int live=1;
  if(!vk_pass_future_op(0,&current,&live) || (live && vk_counter_counting()) || current+8!=(const void *)(draw? (const void *)draw:(const void *)idx))return NULL;
  int changed=0;
  for(unsigned n=1;n<PL_MAX_OPS;n++) {
    const uint8_t *op;if(!vk_pass_future_op(n,&op,&live))return NULL;
    op_hdr h;memcpy(&h,op,8);p->ops[p->count++]=op;
    if(h.op==OP_SET_RT) {
      if(changed || !pl_payload(&h,sizeof(p_set_rt)))return NULL;
      if(target_fields_equal(&scan.targets,(const void *)(op+8)))continue;
      changed=1;p->set_op=op;
    }
    if(h.op==OP_DRAW || h.op==OP_DRAW_IDX) {
      if((live && vk_counter_counting()) || !pl_no_writers(&scan))return NULL;
      if(!changed)continue;
      const p_draw *d;const p_draw_idx *i;if(!pl_draw_header(op,&d,&i))return NULL;
      vk_shader *list[6]={0};vk_program_shaders(scan.program,list);
      p->fragment=pl_local_subset(p,&scan,list[1],list[1]->module,&S.targets);
      p->vertex=vk_shader_pixel_local(list[0],list[0]->module);
      if(!p->fragment || !p->vertex ||
          !pl_geometry(&scan,p->vertex,p->fragment->uv_location,d,i,width,height) || !pl_no_ds_writes(&scan))return NULL;
      p->draw_op=op;p->second=scan;break;
    }
    if(h.op==OP_CLEAR_COLOR && changed) {if(!pl_clear(p,&scan,&h,op))return NULL;continue;}
    if(h.op==OP_BARRIER && changed) {
      // Only this closed producer->consumer boundary may represent primitive
      // and fragment ordering by the real subpass dependency. All producers
      // and the eventual consumer must have no programmable resource writes;
      // query/transfer/unknown commands still reject the plan, and consumer
      // resources/geometry are checked again at their actual draw. Their only
      // observable producer results are the color/depth-stencil writes covered
      // by that dependency. Keep pre-target ordering and indirect/other flags
      // on the ordinary path. Replay still executes vk_nvn_barrier unchanged:
      // no NVN flags or dynamic buffer/VS dependencies are consumed here.
      if(!pl_payload(&h,8))return NULL;
      uint64_t flags;memcpy(&flags,op+8,sizeof flags);
      const uint64_t local_order=NVN_BARRIER_ORDER_PRIMITIVES_BIT|
          NVN_BARRIER_ORDER_FRAGMENTS_BIT|NVN_BARRIER_INVALIDATE_TEXTURE_BIT;
      if(flags&~local_order)return NULL;
      continue;
    }
    if(!pl_shadow(&scan,op))return NULL;
  }
  if(!p->draw_op || p->second.targets.n<1 || p->second.targets.n>8)return NULL;
  unsigned local=0;
  for(unsigned k=0;k<p->fragment->count;k++) {
    const tex_t *input=&p->inputs[k];int found=0;
    for(int a=0;a<S.targets.n;a++)if(S.targets.color[a] && pl_tex_same(input,S.targets.color[a])) {
      if(!pl_attachment_shape(input,S.targets.has_cview[a]?&S.targets.cview[a]:NULL,width,height))return NULL;
      found=1;break;
    }
    local+=found;
    for(unsigned j=0;j<k;j++)if(pl_tex_alias(input,&p->inputs[j]))return NULL;
    for(int a=0;a<p->second.targets.n;a++)if(pl_tex_alias(input,p->second.targets.color[a]))return NULL;
    if(pl_tex_alias(input,p->second.targets.depth))return NULL;
  }
  if(!local)return NULL;
  // The producer must not observe an attachment whose LOAD/CLEAR is hoisted
  // into the union framebuffer. Rewalk actual state transitions, not guesses
  // based on shader names or captured descriptor values.
  scan=S;
  for(unsigned n=0;n<=p->count;n++) {
    const uint8_t *op=n?p->ops[n-1]:current;op_hdr h;memcpy(&h,op,8);
    if(op==p->set_op)break;
    if(h.op==OP_DRAW || h.op==OP_DRAW_IDX) {
      if(!pl_no_writers(&scan))return NULL;
      for(int a=0;a<S.targets.n;a++)if(S.targets.color[a] && pl_shader_read_alias(&scan,S.targets.color[a],S.targets.has_cview[a]?&S.targets.cview[a]:NULL))return NULL;
      for(int a=0;a<p->second.targets.n;a++) {
        const tex_t *t=p->second.targets.color[a];
        if(!t || !pl_attachment_shape(t,p->second.targets.has_cview[a]?&p->second.targets.cview[a]:NULL,width,height) || pl_shader_read_alias(&scan,t,p->second.targets.has_cview[a]?&p->second.targets.cview[a]:NULL))return NULL;
        for(int b=0;b<S.targets.n;b++)if(pl_tex_alias(t,S.targets.color[b]))return NULL;
      }
      if(p->second.targets.depth && p->second.targets.depth!=S.targets.depth && pl_shader_read_alias(&scan,p->second.targets.depth,p->second.targets.has_dview?&p->second.targets.dview:NULL))return NULL;
      for(unsigned k=0;k<p->fragment->count;k++) {
        int source=0;for(int a=0;a<S.targets.n;a++)source|=pl_tex_same(&p->inputs[k],S.targets.color[a]);
        if(!source && pl_shader_read_alias(&scan,&p->inputs[k],p->has_input_view[k]?&p->input_views[k]:NULL))return NULL;
      }
    } else if(!pl_shadow(&scan,op))return NULL;
  }
  if(p->generation!=vk_pass_generation() || p->descriptor_epoch!=vk_texture_descriptor_epoch())return NULL;
  return p;
}
// A local COLOR->INPUT dependency covers neither vertex-stage fetches nor
// neighborhood/other ordinary shader reads of the same attachment. Inspect
// all remaining resources, including unconverted bindings in the same FS.
static int pl_union_alias(const pl_candidate *p,const tex_t *t) {
  if(!t || !t->serial)return 1;
  for(int k=0;k<p->first.targets.n;k++)if(pl_tex_alias(t,p->first.targets.color[k]))return 1;
  if(pl_tex_alias(t,p->first.targets.depth))return 1;
  for(int k=0;k<p->second.targets.n;k++)if(pl_tex_alias(t,p->second.targets.color[k]))return 1;
  if(pl_tex_alias(t,p->second.targets.depth))return 1;
  for(unsigned k=0;k<p->fragment->count;k++)if(pl_tex_alias(t,&p->inputs[k]))return 1;
  return 0;
}
static int pl_consumer_resources(const vk_state *s,const pl_candidate *p,const vk_pixel_local_proof *proof) {
  vk_shader *list[6]={0};vk_program_shaders(s->program,list);list[1]=proof->source_shader;
  for(unsigned stage=0;stage<5;stage++)if(list[stage])for(unsigned k=0;k<list[stage]->nb;k++) {
    const vk_binding *b=&list[stage]->bindings[k];int converted=0;
    if(stage==1 && b->kind==3)for(unsigned q=0;q<proof->count;q++)converted|=b->binding==(int)proof->bindings[q].binding;
    if(converted || b->kind==0 || b->kind==5 || (b->kind==1 && b->reg<3))continue;
    if(b->count!=1 || vk_binding_writable(b))return 0;
    if(b->kind==1 || b->kind==2 || b->kind==8) {
      const p_bind_buf *buffer=NULL;
      if(b->kind==1 && b->reg>=3 && b->reg<19)buffer=&s->ubo[stage][b->reg-3];
      if((b->kind==2 || b->kind==8) && b->reg>=0 && b->reg<16)
        buffer=b->kind==8?&s->ubo[stage][b->reg]:&s->ssbo[stage][b->reg];
      if(!buffer)return 0;
      if(buffer->addr && buffer->size) {
        if(buffer->addr>UINTPTR_MAX || buffer->size>UINT64_MAX-buffer->addr)return 0;
        tex_t range={.serial=UINT64_MAX,.mem=(void *)(uintptr_t)buffer->addr};range.L.size=buffer->size;
        if(pl_union_alias(p,&range))return 0;
      }
      continue;
    }
    int image=b->kind==4 || b->kind==9;
    if((!image && b->kind!=3 && b->kind!=6 && b->kind!=7 && b->kind!=VK_BINDING_TEXEL_STORAGE) ||
        b->reg<0 || b->reg>=(image?32:128))return 0;
    uint64_t handle=image?s->img[stage][b->reg]:s->tex[stage][b->reg];tex_t texture;
    if(!vk_texture_descriptor(handle&0xfffff,&texture) || pl_union_alias(p,&texture))return 0;
  }
  return 1;
}
