// Same closed mathematical condition as upstream uv_proof. Every current
// vertex/index and affine coefficient is read through the existing no-wait
// CPU proof; GPU producers or unreadable bytes keep the original path.
static int pl_read_attribute(const vk_state *state,unsigned attribute,
    const p_draw *draw,const p_draw_idx *indexed,float values[64][4]) {
  if(attribute>=16 || (!!draw+!!indexed)!=1)return 0;
  int count=draw?draw->count:indexed->count;
  if(count<3 || count>64 || (draw && draw->first<0) ||
      (indexed && (indexed->instances!=1 || indexed->base_instance ||
       (indexed->itype!=NVN_INDEX_TYPE_UNSIGNED_SHORT && indexed->itype!=NVN_INDEX_TYPE_UNSIGNED_INT))))return 0;
  const va_st *a=&state->attr[attribute];if(a->stream>=16)return 0;
  const vs_st *stream=&state->stream[a->stream];const NVNbufferRange *v=&state->vertex[a->stream];
  VkFormat f=vk_nvn_format(a->fmt);
  unsigned components=f==VK_FORMAT_R32G32_SFLOAT?2:f==VK_FORMAT_R32G32B32_SFLOAT?3:
      f==VK_FORMAT_R32G32B32A32_SFLOAT?4:0;
  if(!components || !stream->stride || stream->divisor || !v->address)return 0;
  uint8_t indices[256];unsigned size=indexed?(indexed->itype==NVN_INDEX_TYPE_UNSIGNED_SHORT?2:4):0;
  if(indexed && (!indexed->ibuf || !vk_cover_read(indexed->ibuf,(uint64_t)count*size,indices)))return 0;
  for(int k=0;k<count;k++) {
    int64_t vertex=draw?(int64_t)draw->first+k:0;
    if(indexed) {uint32_t index=0;memcpy(&index,indices+k*size,size);vertex=(int64_t)index+indexed->base_vertex;}
    if(vertex<0 || (uint64_t)vertex>(UINT64_MAX-a->offset)/stream->stride)return 0;
    uint64_t offset=(uint64_t)vertex*stream->stride+a->offset;
    if(offset>v->size || components*4>v->size-offset || v->address>UINT64_MAX-offset)return 0;
    values[k][0]=values[k][1]=values[k][2]=0;values[k][3]=1;
    if(!vk_cover_read(v->address+offset,components*4,values[k]))return 0;
    for(unsigned c=0;c<components;c++)if(!isfinite(values[k][c]))return 0;
  }
  return 1;
}
static int pl_uniform_float(const vk_state *state,const vk_pixel_local_uniform *ref,float *out) {
  // VS descriptor set0, NVN UBO slots are recovered from the shader schema,
  // never guessed from binding numbers or from captured material values.
  if(ref->set!=0 || !state->program || (ref->offset&3) || ref->offset>65532)return 0;
  vk_shader *stages[6]={0};vk_program_shaders(state->program,stages);
  if(!stages[0])return 0;
  const vk_binding *match=NULL;
  for(unsigned k=0;k<stages[0]->nb;k++)if(stages[0]->bindings[k].binding==ref->binding) {
    if(match)return 0;match=&stages[0]->bindings[k];
  }
  if(!match || match->kind!=1 || match->count!=1 || match->reg<3 || match->reg>=19)return 0;
  const p_bind_buf *b=&state->ubo[0][match->reg-3];
  if(!b->addr || ref->offset>b->size || sizeof *out>b->size-ref->offset || b->addr>UINT64_MAX-ref->offset)return 0;
  return vk_cover_read(b->addr+ref->offset,sizeof *out,out) && isfinite(*out);
}
static int pl_geometry(const vk_state *state,const vk_pixel_local_proof *vertex,
    unsigned location,const p_draw *draw,const p_draw_idx *indexed,unsigned width,unsigned height) {
  if(!vertex || !width || !height || vertex->position_attribute>=16 ||
      state->poly.mode!=NVN_POLYGON_MODE_FILL || state->ms.a2c || state->ms.sl_en)return 0;
  if(state->has_viewport && (state->viewport[0][0]!=0 || state->viewport[0][1]!=0 ||
      state->viewport[0][2]!=(float)width || state->viewport[0][3]!=(float)height))return 0;
  if(state->has_scissor && (state->scissor.x>0 || state->scissor.y>0 ||
      (int64_t)state->scissor.x+state->scissor.w<(int64_t)width ||
      (int64_t)state->scissor.y+state->scissor.h<(int64_t)height))return 0;
  const vk_pixel_local_uv *uv=NULL;
  for(unsigned k=0;k<vertex->uv_count;k++)if(vertex->uv[k].location==location) {
    if(uv)return 0;uv=&vertex->uv[k];
  }
  if(!uv || uv->attribute>=16 || uv->mode>1)return 0;
  float scale[2]={1,1},bias[2]={0,0},p[64][4],q[64][4];
  if(uv->mode)for(unsigned c=0;c<2;c++)
    if(!pl_uniform_float(state,&uv->scale[c],&scale[c]) || !pl_uniform_float(state,&uv->bias[c],&bias[c]))return 0;
  if(!pl_read_attribute(state,vertex->position_attribute,draw,indexed,p) ||
      !pl_read_attribute(state,uv->attribute,draw,indexed,q))return 0;
  unsigned count=draw?draw->count:indexed->count;
  for(unsigned k=0;k<count;k++) {
    float u=uv->mode?fmaf(q[k][0],scale[0],bias[0]):q[k][0];
    float v=uv->mode?fmaf(q[k][1],scale[1],bias[1]):q[k][1];
    // The Vulkan backend emits the original NVN top-left viewport by using
    // negative VkViewport.height, just like the original Metal proof.
    if(!isfinite(u) || !isfinite(v) || u!=(p[k][0]+1.0f)*.5f || v!=(1.0f-p[k][1])*.5f)return 0;
  }
  return 1;
}
