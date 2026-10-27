// Shared verdict: 1 proves a boundary, -1 rejects, 0 continues.
static int color_clear_group_step(unsigned opcode,int final_raw_end) {
    switch(opcode) {
    case OP_SET_RT:case OP_COPY_B2B:case OP_UPDATE_UB:case OP_COPY_B2T:
    case OP_COPY_T2B:case OP_COPY_T2T:case OP_CLEAR_TEX:case OP_DISPATCH:
      return 1;
    case OP_END:
      // The flattened plan also contains returns from nested command calls.
      // Only the final end of this submission is a complete no-draw tail.
      return final_raw_end?1:-1;
    case OP_CLEAR_COLOR:
    case OP_BEGIN:case OP_VIEWPORTS:case OP_SCISSOR:case OP_DEPTH_RANGE:
    case OP_BIND_PROGRAM:case OP_BIND_STATE:case OP_BIND_VATTR:case OP_BIND_VSTREAM:
    case OP_BIND_VBUF:case OP_BIND_UBO:case OP_BIND_SSBO:case OP_BIND_TEX:
    case OP_BIND_SAMP:case OP_STENCIL:case OP_POLY_OFFSET:case OP_SET_TEX_POOL:
    case OP_SET_SAMP_POOL:case OP_COPY_STRIDE:case OP_BLEND_COLOR:case OP_BIND_IMG:
      return 0;
    default:
      // Includes real draws, DS clears, calls/jumps, counters/events, barriers
      // and future opcodes: retain the original conservative attachment set.
      return -1;
    }
}
// A bounded, immutable opcode proof only. Do not reorder clears or speculate
// across calls, externally visible events, unknown commands or a missing tail.
// The stride lets the existing planned-op array be scanned without copying it.
static int color_clear_group_without_depth(const uint8_t *opcodes,size_t stride,
                                          unsigned remaining) {
  if(!opcodes || !stride) return 0;
  unsigned limit=remaining<256?remaining:256;
  if(limit && stride>SIZE_MAX/limit) return 0;
  for(unsigned k=0;k<limit;k++) {
    int step=color_clear_group_step(opcodes[k*stride],k+1==remaining);
    if(step)return step>0;
  }
  return 0;
}
