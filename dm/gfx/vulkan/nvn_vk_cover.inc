// 原 attr_read/first_draw_covers；几何证明直接复用原 nvn_cover.h。
// Upstream attr_read/first_draw_covers; reuse original nvn_cover.h geometry proof.
#include "../nvn/nvn_cover.h"
static int cover_positions(vk_shader *vs,vk_shader *fs,const p_draw *draw,const p_draw_idx *idx,float p[64][4]) {
  if(V.options.render_record_mode)return 0;
  if(!vs || !fs || vs->pos_attr<0 || vs->pos_attr>=16 || fs->may_discard || fs->writes_smask ||
     !fs->color_mask || S.ms.a2c || S.poly.mode!=NVN_POLYGON_MODE_FILL || S.color.logic_op!=NVN_LOGIC_OP_COPY) return 0;
  if(!vk_tbdr_enabled()) return 0;
  int n=draw?draw->count:idx->count,a=vs->pos_attr;
  if(n<3||n>64||(draw&&draw->first<0)||(idx&&(idx->instances<1||idx->itype<1||idx->itype>2))) return 0;
  va_st attr=S.attr[a];if(attr.stream>=16) return 0;
  vs_st stream=S.stream[attr.stream];NVNbufferRange vb=S.vertex[attr.stream];
  VkFormat format=vk_nvn_format(attr.fmt);
  unsigned components=format==VK_FORMAT_R32G32_SFLOAT?2:format==VK_FORMAT_R32G32B32_SFLOAT?3:format==VK_FORMAT_R32G32B32A32_SFLOAT?4:0;
  if(!components||!stream.stride||stream.divisor||!vb.address) return 0;
  uint8_t indices[256];unsigned isize=idx?(idx->itype==1?2:4):0;
  // Optional proof: if GPU data is pending, retain LOAD instead of submitting
  // and waiting merely to prove that the load might be redundant.
  if(idx&&!vk_cover_read(idx->ibuf,(uint64_t)n*isize,indices)) return 0;
  for(int k=0;k<n;k++) {
    int64_t vertex=draw?(int64_t)draw->first+k:0;
    if(idx) {
      uint32_t value=0;memcpy(&value,indices+k*isize,isize);vertex=(int64_t)value+idx->base_vertex;
    }
    if(vertex<0 || (uint64_t)vertex>(UINT64_MAX-attr.offset)/stream.stride) return 0;
    uint64_t offset=(uint64_t)vertex*stream.stride+attr.offset;
    if(offset>vb.size || components*4>vb.size-offset || vb.address>UINT64_MAX-offset) return 0;
    p[k][0]=p[k][1]=p[k][2]=0;p[k][3]=1;
    if(!vk_cover_read(vb.address+offset,components*4,p[k])) return 0;
    if(vs->pos_w1) p[k][3]=1;
  }
  return 1;
}
// Serial + the original physical description identify image_get's one image.
// Only the existing single-layer 2D mip path has independent layout cells.
// Keep unspecified/all-mip views, reinterpretation and other aliases opaque.
int vk_cover_mip_ranges_disjoint(const tex_t *target,const view_t *tv,
                                const tex_t *sampled,const view_t *sv) {
  if(!target || !sampled || !sv || !target->serial || target->serial!=sampled->serial ||
      target->mem!=sampled->mem || memcmp(&target->d,&sampled->d,sizeof target->d) ||
      memcmp(&target->L,&sampled->L,sizeof target->L) ||
      target->d.target!=NVN_TEXTURE_TARGET_2D || target->L.layers!=1 ||
      target->d.samples>1 || target->d.levels<2 || target->d.levels>16 ||
      !sv->set_levels || sv->nlevels<=0) return 0;
  const view_t *views[]={tv,sv};
  for(unsigned k=0;k<2;k++) if(views[k]) {
    const view_t *v=views[k];
    if((v->set_format && v->format!=target->d.format) ||
        (v->set_target && v->target!=NVN_TEXTURE_TARGET_2D) ||
        (v->set_layers && (v->min_layer!=0 || v->nlayers!=1))) return 0;
  }
  int attached=tv&&tv->set_levels?tv->base_level:0;
  if(tv && tv->set_levels && tv->nlevels!=1) return 0;
  int first=sv->base_level,count=sv->nlevels;
  if(attached<0 || attached>=target->d.levels || first<0 ||
      first>=target->d.levels || count>target->d.levels-first) return 0;
  return attached<first || attached-first>=count;
}
static int cover_reads_target(vk_shader *list[6],const tex_t *target,const view_t *target_view,
                              uint64_t bound_epoch) {
  int exempted=0;
  for(unsigned st=0;st<5;st++) if(list[st]) for(unsigned i=0;i<list[st]->nb;i++) {
    const vk_binding *b=&list[st]->bindings[i];int image=b->kind==4||b->kind==9;
    if(!image&&b->kind!=3&&b->kind!=6&&b->kind!=7) continue;
    int limit=image?32:128;if(b->reg<0||b->reg>=limit) return 1;
    uint64_t h=image?S.img[st][b->reg]:S.tex[st][b->reg];if(!h) continue;
    tex_t t;view_t v;int has_view;
    if(!vk_texture_descriptor_view(h&0xfffff,&t,&v,&has_view)) return 1;
    if((b->kind==3 || b->kind==6) && bound_epoch==vk_texture_descriptor_epoch() &&
        vk_cover_mip_ranges_disjoint(target,target_view,&t,has_view?&v:NULL)) {
      exempted=1;continue;
    }
    uintptr_t a=(uintptr_t)target->mem,c=(uintptr_t)t.mem;
    if(target->serial==t.serial || (a&&c&&(a==c||(a<c?c-a<target->L.size:a-c<t.L.size)))) return 1;
  }
  // Bindings were prepared before this proof. Registry mutation must not let
  // a newer view justify discarding data read by the earlier bound view.
  return exempted && bound_epoch!=vk_texture_descriptor_epoch();
}
static void cover_first_draw(render_job *j,vk_shader *list[6],const VkPipelineDepthStencilStateCreateInfo *ds,
                             const p_draw *draw,const p_draw_idx *idx,int positions,const float p[64][4],uint64_t bound_epoch) {
  if(j->first_draw) return;
  j->first_draw=1;
  if(V.options.render_record_mode)return;
  if(!positions || (j->has_depth&&((ds->depthTestEnable&&ds->depthCompareOp!=VK_COMPARE_OP_ALWAYS)||ds->stencilTestEnable))) return;
  int n=draw?draw->count:idx->count,prim=draw?draw->prim:idx->prim;
  int sc[]={S.scissor.x,S.scissor.y,S.scissor.w,S.scissor.h};
  for(unsigned k=0;k<j->color_count;k++) {
    unsigned a=j->colors[k].attachment;const tex_t *target=S.targets.color[k];
    const view_t *v=S.targets.has_cview[k]?&S.targets.cview[k]:NULL;
    if(a==VK_ATTACHMENT_UNUSED||!target||j->attachments[a].loadOp!=VK_ATTACHMENT_LOAD_OP_LOAD ||
       j->attachments[a].samples!=VK_SAMPLE_COUNT_1_BIT || target->d.target==NVN_TEXTURE_TARGET_3D ||
       !(list[1]->color_mask&(1u<<k)) || ((S.color.blend_en>>k)&1) || ((S.chmask.mask>>(k*4))&15)!=15 || cover_reads_target(list,target,v,bound_epoch)) continue;
    int level=v&&v->set_levels?v->base_level:0;if(level<0||level>31) continue;
    unsigned w=target->d.width>>level,h=target->d.height>>level;if(!w) w=1;if(!h) h=1;
    if(!nvn_cover(p,n,prim,S.poly.cull,S.poly.front==NVN_FRONT_FACE_CCW,S.has_viewport,S.viewport[0],S.has_scissor,sc,w,h)) continue;
    j->attachments[a].loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;j->load_elisions++;
    // 原 LOADPROBE 为常见浮点附件加载哨兵；正确的全覆盖必须覆盖它的每个像素。
    // Original LOADPROBE loads a sentinel for common float attachments. A
    // correct coverage proof must overwrite every one of its pixels.
    VkFormat f=j->attachments[a].format;
    int probe=vk_load_probe_mode();
    if(probe>=1&&probe<=3 && (f==VK_FORMAT_R8G8B8A8_UNORM||f==VK_FORMAT_R8G8B8A8_SRGB||f==VK_FORMAT_R16G16B16A16_SFLOAT||f==VK_FORMAT_B10G11R11_UFLOAT_PACK32)) {
      j->attachments[a].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
      float sentinel[4]={.123f,.456f,.789f,.321f};
      if(probe==2) memcpy(sentinel,(float[]){.777f,.222f,.555f,.888f},16);
      if(probe==3) for(unsigned c=0;c<4;c++) sentinel[c]=NAN;
      memcpy(j->clears[a].color.float32,sentinel,16);
    }
  }
}
