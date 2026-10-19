// Only a complete bounded clear-only detour with exact target restoration.
// No cross-submission, nested-call, query/event or resource-consumer inference.
int vk_pass_mip_clear_plan(const p_set_rt *original,const p_set_rt *current,
                           const p_scissor *sc,vk_mip_clear_plan *out) {
  if(!valid || !store_valid || !replay || !vk_replay_preserves_ops() ||
      plan[replay-1].opcode!=OP_CLEAR_COLOR || !original || !current ||
      original->n!=2 || current->n!=2) return 0;
  memset(out,0,sizeof *out);out->generation=generation;
  p_set_rt rt=*current;p_scissor rectangle=sc?*sc:(p_scissor){0};
  int has_sc=sc!=NULL;unsigned groups=1,mask=0;
  unsigned begin=replay-1,limit=count-begin<256?count:begin+256;
  for(unsigned k=begin;k<limit;k++) {
    const planned_op *o=&plan[k];op_hdr h;memcpy(&h,o->op,sizeof h);
    const void *a=o->op+8;
    out->ops[out->op_count++]=o->op;
    switch(o->opcode) {
    case OP_SET_RT:
      if(h.size<8+sizeof rt) return 0;
      rt=*(const p_set_rt *)a;
      if(!memcmp(&rt,original,sizeof rt))return groups==2 && mask==3 && out->record_count==4;
      if(mask!=3 || ++groups>2 || rt.n!=2)return 0;
      mask=0;
      break;
    case OP_SCISSOR:
      if(h.size<8+sizeof rectangle)return 0;
      rectangle=*(const p_scissor *)a;has_sc=1;break;
    case OP_CLEAR_COLOR: {
      if(h.size<8+sizeof(p_clear_color) || out->record_count==4)return 0;
      const p_clear_color *p=a;
      if(p->index<0 || p->index>=2 || p->mask!=15 || (mask&(1u<<p->index)))return 0;
      mask|=1u<<p->index;
      out->records[out->record_count++]=(vk_mip_clear_record){
        .op=o->op,.targets=rt,.scissor=rectangle,.clear=*p,.has_scissor=has_sc};
      break;
    }
    // These setters do not access GPU memory. Preserve their normal replay;
    // only the color writes may be postponed, never state changes.
    case OP_VIEWPORTS:if(h.size<8+sizeof(p_viewports))return 0;break;
    case OP_DEPTH_RANGE:if(h.size<8+sizeof(p_depth_range))return 0;break;
    case OP_STENCIL:if(h.size<8+sizeof(p_stencil))return 0;break;
    case OP_BIND_STATE:if(h.size<8+sizeof(p_bind_state))return 0;break;
    case OP_BIND_PROGRAM:if(h.size<8+sizeof(p_bind_program))return 0;break;
    case OP_BIND_VATTR:if(h.size<8+sizeof(p_bind_vattr))return 0;break;
    case OP_BIND_VSTREAM:if(h.size<8+sizeof(p_bind_vstream))return 0;break;
    case OP_BIND_VBUF:if(h.size<8+sizeof(p_bind_vbuf))return 0;break;
    case OP_BIND_UBO:case OP_BIND_SSBO:if(h.size<8+sizeof(p_bind_buf))return 0;break;
    case OP_BIND_TEX:case OP_BIND_SAMP:case OP_BIND_IMG:
      if(h.size<8+sizeof(p_bind_handles))return 0;break;
    default:return 0;
    }
  }
  return 0;
}
