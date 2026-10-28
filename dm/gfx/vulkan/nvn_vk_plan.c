// 一次按真实调用/跳转顺序展开，再逆扫深度模板需求；不确定时保留附件。
// Flatten real call/jump order once, then scan depth/stencil needs backwards.
// Uncertainty retains attachments, as in upstream Metal ds_scan.
#include "nvn_vk_state.h"
#include "nvn_vk_plan.h"
#include "nvn_vk_shader.h"
#include "nvn_vk_freeze.h"
#include "nvn_vk_pixel_local_runtime.h"
#include "nvn_vk_system_trace.h"
#include <stdatomic.h>
typedef struct {
  const uint8_t *op;
  // The first four bytes are written by walk; reverse-only outputs follow.
  uint8_t draw,drawing_writes,opcode,sample_counter;
  uint8_t need,sample_live,writes,state_run;
} planned_op;
_Static_assert(OP_COUNT_<=256,"Vulkan planned opcode width");
_Static_assert(sizeof(planned_op)==16,"Vulkan planned opcode uses existing padding");
_Static_assert(offsetof(planned_op,drawing_writes)==offsetof(planned_op,draw)+1 &&
    offsetof(planned_op,opcode)==offsetof(planned_op,draw)+2 &&
    offsetof(planned_op,sample_counter)==offsetof(planned_op,draw)+3,
    "Vulkan forward plan bytes must remain contiguous");
static planned_op *plan;
// count/replay are always RAW ordinals; only node_count indexes storage.
static unsigned count, capacity, replay,node_count;
static int valid,done,store_valid;
static uint64_t generation;
static int sparse_requested=-1,sparse_mode;
static vk_pass_cursor current_cursor,next_cursor;
static planned_op sparse_current;
static int sparse_plan_requested(void) {
  if(sparse_requested>=0)return sparse_requested;
  const char *v=getenv("DM_VULKAN_SPARSE_PLAN");
  if(v && strcmp(v,"0") && strcmp(v,"1"))die("DM_VULKAN_SPARSE_PLAN expects 0 or 1");
  sparse_requested=v && !strcmp(v,"1");
  LOG("[vulkan] sparse submission plan: %s (fixed for this process)",sparse_requested?"enabled":"disabled");
  return sparse_requested;
}
// The high bit of sample_counter is unused by an anchor (its value is 0/1).
// A span reuses those four forward bytes as a tagged 31-bit raw-op count.
_Static_assert(offsetof(planned_op,sample_counter)==11,"Vulkan sparse tag position");
_Static_assert(__BYTE_ORDER__==__ORDER_LITTLE_ENDIAN__,"Vulkan sparse tagged count byte order");
static int plan_is_span(const planned_op *o) { return !!(o->sample_counter&128); }
static unsigned plan_raw_count(const planned_op *o) {
  if(!sparse_mode || !plan_is_span(o))return 1;
  uint32_t v;memcpy(&v,&o->draw,4);return v&UINT32_C(0x7fffffff);
}
static void plan_span_count(planned_op *o,unsigned n) {
  uint32_t v=UINT32_C(0x80000000)|n;memcpy(&o->draw,&v,4);
}
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
static int plan_fail_allocation;
#define PLAN_REALLOC(p,n) (plan_fail_allocation?NULL:realloc((p),(n)))
#else
#define PLAN_REALLOC(p,n) realloc((p),(n))
#endif
static int plan_reserve(void) {
  if(node_count<capacity)return 1;
  unsigned next=capacity?capacity*2:4096;
  planned_op *grown=PLAN_REALLOC(plan,next*sizeof *plan);
  if(!grown)return 0;
  plan=grown;capacity=next;return 1;
}
enum { PP_PURE=1,PP_STORE=2,PP_DS=4,PP_STENCIL=8,PP_DRAW=16,PP_QUERY=32 };
// PURE means a zero reverse-transfer function, not permission to skip its
// forward effects. Program/texture/image setters still update the Store state.
static const uint8_t plan_properties[OP_COUNT_]={
  [OP_BIND_PROGRAM]=PP_PURE|PP_STORE,[OP_BIND_STATE]=PP_PURE|PP_DS,
  [OP_BIND_VATTR]=PP_PURE,[OP_BIND_VSTREAM]=PP_PURE,[OP_BIND_VBUF]=PP_PURE,
  [OP_BIND_UBO]=PP_PURE,[OP_BIND_SSBO]=PP_PURE,[OP_BIND_SAMP]=PP_PURE,
  [OP_BIND_TEX]=PP_PURE|PP_STORE,[OP_BIND_IMG]=PP_PURE|PP_STORE,
  [OP_STENCIL]=PP_PURE|PP_STENCIL,[OP_POLY_OFFSET]=PP_PURE,
  [OP_VIEWPORTS]=PP_PURE,[OP_DEPTH_RANGE]=PP_PURE,[OP_BLEND_COLOR]=PP_PURE,
  [OP_DRAW]=PP_DRAW|PP_STORE,[OP_DRAW_IDX]=PP_DRAW|PP_STORE,
  [OP_REPORT_COUNTER]=PP_QUERY,[OP_RESET_COUNTER]=PP_QUERY,
  [OP_SET_RT]=PP_STORE,[OP_SCISSOR]=PP_STORE,[OP_DISPATCH]=PP_STORE,
  [OP_CLEAR_COLOR]=PP_STORE,[OP_CLEAR_DS]=PP_STORE,
  [OP_COPY_T2T]=PP_STORE,[OP_COPY_T2B]=PP_STORE,[OP_COPY_B2T]=PP_STORE,
  [OP_CLEAR_TEX]=PP_STORE,[OP_SET_TEX_POOL]=PP_STORE,[OP_SET_SAMP_POOL]=PP_STORE
};
static int emit_pure_span(const uint8_t *first,unsigned n) {
  if(!plan_reserve())return 0;
  planned_op *out=&plan[node_count++];out->op=first;plan_span_count(out,n);return 1;
}

static const planned_op *current_entry(void) { return sparse_mode?&sparse_current:&plan[replay-1]; }
static vk_pass_cursor cursor_at_current(void) {
  if(sparse_mode)return current_cursor;
  return (vk_pass_cursor){.op=plan[replay-1].op,.node=replay-1,.ordinal=replay-1,.generation=generation};
}
static vk_pass_cursor cursor_at_next(void) {
  if(sparse_mode)return next_cursor;
  return (vk_pass_cursor){.op=replay<count?plan[replay].op:NULL,.node=replay,.ordinal=replay,.generation=generation};
}
static unsigned cursor_opcode(const vk_pass_cursor *cursor) {
  const planned_op *o=&plan[cursor->node];
  if(!sparse_mode || !plan_is_span(o))return o->opcode;
  op_hdr h;memcpy(&h,cursor->op,8);return h.op;
}
static planned_op cursor_entry(const vk_pass_cursor *cursor,unsigned opcode) {
  const planned_op *o=&plan[cursor->node];
  if(!sparse_mode || !plan_is_span(o))return *o;
  unsigned run=V.options.state_segments?plan_raw_count(o)-1-cursor->offset+o->state_run:0;
  return (planned_op){.op=cursor->op,.opcode=opcode,.need=o->need,.writes=o->writes,
    .sample_live=o->sample_live,.state_run=run>255?255:run};
}
static void cursor_advance(vk_pass_cursor *cursor,unsigned size) {
  const planned_op *o=&plan[cursor->node];cursor->ordinal++;
  if(sparse_mode && plan_is_span(o) && cursor->offset+1<plan_raw_count(o)) {
    cursor->offset++;cursor->op+=size;
  } else {
    cursor->offset=0;cursor->node++;
    cursor->op=cursor->ordinal<count?plan[cursor->node].op:NULL;
  }
}
static void cursor_advance_raw(vk_pass_cursor *cursor) {
  unsigned size=0;
  if(sparse_mode && plan_is_span(&plan[cursor->node])) {op_hdr h;memcpy(&h,cursor->op,8);size=h.size;}
  cursor_advance(cursor,size);
}
uint64_t vk_pass_generation(void) { return generation; }
int vk_pass_future_op(unsigned offset,const uint8_t **op,int *samples_live) {
  if(!valid || !store_valid || !replay || !vk_replay_preserves_ops() || offset>=count-(replay-1)) return 0;
  if(!sparse_mode) {
    const planned_op *o=&plan[replay-1+offset];
    *op=o->op;if(samples_live)*samples_live=o->sample_live;return 1;
  }
  vk_pass_cursor cursor;
  if(!vk_pass_future_begin(&cursor,op,samples_live))return 0;
  while(offset--)if(!vk_pass_future_next(&cursor,op,samples_live))return 0;
  return 1;
}
int vk_pass_future_begin(vk_pass_cursor *cursor,const uint8_t **op,int *samples_live) {
  if(!valid || !store_valid || !replay || !vk_replay_preserves_ops())return 0;
  *cursor=cursor_at_current();*op=cursor->op;
  if(samples_live)*samples_live=plan[cursor->node].sample_live;
  return 1;
}
int vk_pass_future_next(vk_pass_cursor *cursor,const uint8_t **op,int *samples_live) {
  if(!valid || !store_valid || cursor->generation!=generation || !vk_replay_preserves_ops() || cursor->ordinal+1>=count)return 0;
  cursor_advance_raw(cursor);*op=cursor->op;
  if(samples_live)*samples_live=plan[cursor->node].sample_live;
  return 1;
}
static _Atomic uint64_t sample_live_draws,sample_dead_draws;
static _Atomic uint64_t sample_elided_draws;
static uint64_t recorded_sample_elided,recorded_reference_scans,recorded_reference_ops;
static _Atomic int sample_liveness_requested=-1,sample_liveness_active;
static int sample_elision_used;
void vk_sample_liveness_reference(int reference) { atomic_store(&sample_liveness_requested,!!reference); }
void vk_sample_query_elision_stats(uint64_t *elided,int *reference) {
  *elided=atomic_load(&sample_elided_draws);*reference=atomic_load(&sample_liveness_active);
}
void vk_sample_liveness_stats(uint64_t *live,uint64_t *dead) {
  *live=atomic_load(&sample_live_draws);*dead=atomic_load(&sample_dead_draws);
}

static int index_valid,reference_mode,check_mode;
static struct { atomic_uint_fast64_t builds,indexed,reference,reference_ops,query_events,comparisons,mismatches,fallbacks; } index_stats;
// Publication only: called by the same recording owner at physical commit.
// No command planning, query control or synchronization state is changed.
void vk_pass_stats_publish(void) {
  atomic_store_explicit(&sample_elided_draws,recorded_sample_elided,memory_order_relaxed);
  atomic_store_explicit(&index_stats.reference,recorded_reference_scans,memory_order_relaxed);
  atomic_store_explicit(&index_stats.reference_ops,recorded_reference_ops,memory_order_relaxed);
}
// A single submission-local forward interpreter. It follows the exact same
// CALL/JUMP order as walk; no command handle is cached across submissions.
typedef struct {
  p_set_rt rt;
  p_scissor sc;
  int has_sc,target_buckets[9];
  const prog_t *program;
  uint64_t slots[6][160];
  uint8_t known_zero[6];
} store_build_context;
_Static_assert(sizeof(store_build_context)<=8192,"Submission store context remains bounded");
static void store_index_begin(store_build_context *ctx);
static void store_index_op(store_build_context *ctx,unsigned seq,unsigned opcode,const void *a,unsigned size);
static void store_index_finish(void);
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
static void store_walk_test_verify(void);
#endif
static ds_st scan_ds;
static uint32_t scan_stencil_mask[2];
static int planned_stencil_writer(void) {
  return scan_ds.st &&
      ((scan_stencil_mask[0] && (scan_ds.fail_f!=NVN_STENCIL_OP_KEEP ||
        scan_ds.zfail_f!=NVN_STENCIL_OP_KEEP || scan_ds.zpass_f!=NVN_STENCIL_OP_KEEP)) ||
       (scan_stencil_mask[1] && (scan_ds.fail_b!=NVN_STENCIL_OP_KEEP ||
        scan_ds.zfail_b!=NVN_STENCIL_OP_KEEP || scan_ds.zpass_b!=NVN_STENCIL_OP_KEEP)));
}
// Exactly the original vk_state_op family. Boundaries/observers/effects are
// deliberately absent, including SCISSOR, CALL, JUMP and END. The plan's
// existing forward validation owns the immutable command storage until replay.
static int pure_state_opcode(unsigned op) {return !!(plan_properties[op]&PP_PURE);}
static int walk(const uint8_t *p,unsigned depth,store_build_context *store) {
  if(depth>32 || !p)return 0;
  const uint8_t *pending=NULL;unsigned pending_count=0;
  for(unsigned guard=0;guard<1000000;guard++) {
    op_hdr h;memcpy(&h,p,sizeof h);
    if(h.size<8 || (h.size&7) || h.op>=OP_COUNT_ || count==1000000)return 0;
    const uint8_t *a=p+8;unsigned properties=plan_properties[h.op];
    int pure=sparse_mode && (properties&PP_PURE);planned_op *o=NULL;
    if(pure) {
      // Accumulate in this walk frame, not in the shared plan cache line.
      // Every control op is non-pure, so it flushes before CALL/JUMP/END;
      // a pending span can only contain sequential bytes of this one segment.
      if(!pending_count)pending=p;
      pending_count++;count++;
    } else {
      if(pending_count) {
        if(!emit_pure_span(pending,pending_count))return 0;
        pending_count=0;
      }
      if(!plan_reserve())return 0;
      o=&plan[node_count++];count++;o->op=p;
      const uint8_t forward[4]={0,0,(uint8_t)h.op,0};
      memcpy((uint8_t *)o+offsetof(planned_op,draw),forward,sizeof forward);
    }
    if((properties&PP_QUERY) && h.size>=16) {
      uint64_t type;memcpy(&type,a,8);o->sample_counter=type==NVN_COUNTER_TYPE_SAMPLES_PASSED;
    }
    if(properties&PP_DS) {
      if(h.size<8+sizeof(p_bind_state))return 0;
      const p_bind_state *b=(const void *)a;
      if(b->kind==5)memcpy(&scan_ds,b->bytes,sizeof scan_ds);
    }
    if(properties&PP_STENCIL) {
      if(h.size<8+sizeof(p_stencil))return 0;
      const p_stencil *st=(const void *)a;
      if(st->which==1)for(unsigned face=0;face<2;face++)
        if(st->face&(1u<<face))scan_stencil_mask[face]=st->mask;
    }
    if(properties&PP_DRAW) {
      o->draw=(scan_ds.dt||scan_ds.dw?1:0)|(scan_ds.st?2:0);
      o->drawing_writes=(scan_ds.dw?1:0)|(planned_stencil_writer()?2:0);
    }
    // Metadata grouping does not skip forward resource interpretation. In
    // particular every Program/TEX/IMG update reaches the original Store code
    // at its unchanged raw ordinal, even while a pure span is pending.
    if(store && index_valid && (properties&PP_STORE))
      store_index_op(store,count-1,h.op,a,h.size);
    if(h.op==OP_END)return 1;
    if(h.op==OP_JUMP || h.op==OP_CALL) {
      if(h.size<16)return 0;
      const uint8_t *target;memcpy(&target,a,8);
      if(h.op==OP_JUMP) {if(!target)return 0;p=target;continue;}
      if(!walk(target,depth+1,store))return 0;
    }
    p+=h.size;
  }
  return 0;
}
static void submission_index_build(const uint64_t *handles,int n) {
  int query_reference=atomic_load(&sample_liveness_requested);
  if(query_reference<0) {
    const char *v=getenv("DM_VULKAN_SAMPLE_LIVENESS_REFERENCE");
    if(v&&strcmp(v,"0")&&strcmp(v,"1")) die("DM_VULKAN_SAMPLE_LIVENESS_REFERENCE expects 0 or 1");
    query_reference=v&&!strcmp(v,"1");atomic_store(&sample_liveness_requested,query_reference);
  }
  atomic_store(&sample_liveness_active,query_reference);sample_elision_used=0;
  if(!++generation) die("Vulkan submission generation overflow");
  sparse_mode=sparse_plan_requested();
  count=replay=node_count=0;scan_ds=S.ds;valid=1;
  memcpy(scan_stencil_mask,S.stencil_mask,sizeof scan_stencil_mask);
  done=0;store_valid=vk_replay_preserves_ops();
  index_valid=0;
  reference_mode=V.options.store_reference;
  check_mode=V.options.store_check;
  // Cold comparison of the complete planning cost. The existing invalid-plan
  // paths retain depth/stencil, STORE and live queries; no dependency, GPU
  // fence or guest command is omitted. State spans and optional lookahead
  // consumers fall back to their ordinary replay paths as well.
  static int planning_reference=-1;
  if(planning_reference<0) {
    const char *v=getenv("DM_VULKAN_SUBMISSION_PLAN_REFERENCE");
    if(v && strcmp(v,"0") && strcmp(v,"1"))
      die("DM_VULKAN_SUBMISSION_PLAN_REFERENCE expects 0 or 1");
    planning_reference=v && !strcmp(v,"1");
    LOG("[vulkan] submission planning: %s (fixed for this process)",
        planning_reference?"conservative replay without prepass":"enabled");
  }
  if(planning_reference) {valid=store_valid=0;return;}
  if(!vk_tbdr_enabled() || n<0 || (n&&!handles)) { valid=0;return; }
  store_build_context context;
  // Isolate the store-elision budget without disabling query/depth/state
  // consumers of the flattened plan or falling back to per-target raw scans.
  int build_store=store_valid && !V.options.store_keep_all && !V.options.render_record_mode && (!reference_mode||check_mode);
  if(build_store) store_index_begin(&context);
  for(int i=0;i<n;i++) if(!walk((const void *)(uintptr_t)handles[i],0,build_store?&context:NULL)) {
    valid=index_valid=0;return;
  }
  unsigned need=0,writes=0,samples_needed=1,state_run=0;
  uint64_t live_draws=0,dead_draws=0;
  for(unsigned i=node_count;i-->0;) {
    // Cursor reconstruction must not read a prior submission's run byte when
    // state-segment replay is disabled in this cold configuration.
    plan[i].state_run=0;
    if(sparse_mode && plan_is_span(&plan[i])) {
      plan[i].sample_live=samples_needed;plan[i].need=need;plan[i].writes=writes;
      if(V.options.state_segments) {
        plan[i].state_run=state_run<255?state_run+1:255;
        unsigned n=plan_raw_count(&plan[i]);state_run=n>=255-state_run?255:state_run+n;
      }
      continue;
    }
    unsigned opcode=plan[i].opcode;
    if(plan[i].sample_counter) samples_needed=opcode==OP_REPORT_COUNTER;
    plan[i].sample_live=samples_needed;
    // Reuse one byte of existing padding and this mandatory reverse pass.
    // Consecutive pure entries are physically adjacent: any CALL/JUMP/END
    // emitted by walk has state_run=0. Cap runs without growing the plan.
    if(V.options.state_segments) {
      state_run=pure_state_opcode(opcode)?(state_run<255?state_run+1:255):0;
      plan[i].state_run=state_run;
    }
    if(opcode==OP_DRAW||opcode==OP_DRAW_IDX) {
      if(samples_needed) live_draws++;else dead_draws++;
    }
    switch(opcode) {
    case OP_SET_RT:case OP_COPY_B2B:case OP_UPDATE_UB:case OP_COPY_B2T:
    case OP_COPY_T2B:case OP_COPY_T2T:case OP_CLEAR_TEX:case OP_DISPATCH:
      need=writes=0;break;
    case OP_CLEAR_COLOR:case OP_CLEAR_DS:
      need=3;if(opcode==OP_CLEAR_DS) writes=3;break;
    default:need|=plan[i].draw;writes|=plan[i].drawing_writes;break;
    }
    plan[i].need=need;
    plan[i].writes=writes;
  }
  next_cursor=(vk_pass_cursor){.op=count?plan[0].op:NULL,.generation=generation};
  static int first_sparse_reduction;
  if(sparse_mode && node_count<count && !first_sparse_reduction) {
    first_sparse_reduction=1;
    LOG("[vulkan sparse plan] first actual reduction raw_ops=%u nodes=%u active_bytes=%zu",
        count,node_count,(size_t)node_count*sizeof *plan);
  }
  atomic_fetch_add(&sample_live_draws,live_draws);
  atomic_fetch_add(&sample_dead_draws,dead_draws);
  if(build_store) {
    // Event collection is already complete. Stable scatter and alias closure
    // still run after the complete reverse proof, exactly as before.
    int trace=vk_system_trace_begin("NVN attachment store index finalization");
    store_index_finish();
    vk_system_trace_end(trace);
  }
}
void mtl_sub_index(const uint64_t *handles,int n) {
  vk_stream_begin();
  int trace=vk_system_trace_begin("NVN submission planning");
  submission_index_build(handles,n);
  vk_system_trace_end(trace);
}
void mtl_exec_rest(const uint64_t *handles,int n) {
  // 整提交索引已经包含余下句柄，不再逐通道扫描返回栈。
  // The submission index already includes remaining handles and call returns.
  // 最终回放结束后撤销上下文；不能在快照退休后访问旧操作指针。
  // Retire the replay context before snapshots can retire their op storage.
  if(!handles && !n) { valid=0;vk_stream_end(); }
}
void vk_pass_plan_op(const uint8_t *op) {
  if(!sparse_mode) {vk_pass_plan_op_header(op,0,0);return;}
  op_hdr h;memcpy(&h,op,8);vk_pass_plan_op_header(op,h.op,h.size);
}
void vk_pass_plan_op_header(const uint8_t *op,unsigned opcode,unsigned size) {
  if(!valid) return;
  if(replay>=count || (sparse_mode?next_cursor.op:plan[replay].op)!=op) {
    if(sample_elision_used) die("Vulkan query liveness proof invalidated by replay mismatch");
    LOG("[vulkan pass] lookahead/replay mismatch; retain depth/stencil");
    valid=0;return;
  }
  if(sparse_mode) {
    current_cursor=next_cursor;sparse_current=cursor_entry(&current_cursor,opcode);
    cursor_advance(&next_cursor,size);
  }
  replay++;
  done=0;
}
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
static uint64_t state_segments_consumed,state_operations_consumed;
void vk_test_state_segment_stats(uint64_t *segments,uint64_t *operations) {
  *segments=state_segments_consumed;*operations=state_operations_consumed;
}
#endif
unsigned vk_pass_state_segment(const uint8_t **next,unsigned guard_left) {
  // The first original operation/checkpoint already ran. Do not recover a
  // mismatch here: the ordinary next iteration retains query-elision's fatal
  // mismatch rule and all original diagnostics. No new per-member checks.
  if(!valid || !done || replay>=count || (sparse_mode?next_cursor.op:plan[replay].op)!=*next)return 0;
  unsigned n,raw=0;
  const planned_op *span=NULL;
  if(sparse_mode) {
    // Every pure opcode is grouped by walk. A nonempty state tail is one
    // sequential span: CALL/JUMP/END and every other non-pure opcode flush it.
    // The anchor after it has state_run=0, so no cross-node run is possible.
    if(!V.options.state_segments || !plan_is_span(&plan[next_cursor.node]))return 0;
    span=&plan[next_cursor.node];raw=plan_raw_count(span);
    n=raw-next_cursor.offset;if(n>255)n=255;
  } else n=plan[replay].state_run;
  if(n>guard_left)n=guard_left;
  if(!n)return 0;
  // Admission is once per nonempty span, never once per state member.
  // Host freeze/toggle changes are applied at owner/frame boundaries. Active
  // runtime filtering, pass observers and suspended consumers retain the old
  // per-op order. No producer of these scopes exists in a pure setter tail.
  if(vk_pool_operation_active() || vk_shader_operation_active() ||
      vk_texture_binding_scope_active() || vk_texture_shader_collecting() ||
      vk_ghost_replaying() || vk_mip_clear_pending() ||
      vk_pixel_local_replay_active() || !vk_replay_preserves_ops())return 0;
  // A pure run always has a following boundary (ultimately END). vk_state_op
  // only changes CPU state/version bookkeeping under the caller's no-operation
  // proof; it cannot inspect the plan or enter a GPU callback in this scope.
  if(sparse_mode) {
    vk_pass_cursor cursor=next_cursor,last=cursor;
    const uint8_t *p=cursor.op,*last_op=p;unsigned last_opcode=0;
    for(unsigned i=0;i<n;i++) {
      op_hdr h;memcpy(&h,p,8);last_op=p;last_opcode=h.op;
      vk_state_op(h.op,p+sizeof(op_hdr));p+=h.size;
    }
    // Publish once after all state applications, as in dense replay. Current
    // still denotes the last consumed RAW op; future/done consumers must not
    // observe the next anchor as though it had already executed.
    last.op=last_op;last.ordinal+=n-1;last.offset+=n-1;
    unsigned run=raw-last.offset;
    sparse_current=(planned_op){.op=last_op,.opcode=last_opcode,
      .need=span->need,.writes=span->writes,.sample_live=span->sample_live,
      .state_run=run>255?255:run};
    cursor.ordinal+=n;cursor.offset+=n;
    if(cursor.offset==raw) {
      cursor.offset=0;cursor.node++;cursor.op=cursor.ordinal<count?plan[cursor.node].op:NULL;
    } else cursor.op=p;
    current_cursor=last;next_cursor=cursor;
  } else {
    for(unsigned i=0;i<n;i++) {
      const planned_op *o=&plan[replay+i];
      vk_state_op(o->opcode,o->op+sizeof(op_hdr));
    }
  }
  replay+=n;done=1;*next=sparse_mode?next_cursor.op:plan[replay].op;
  // One owner-thread admission marker, after real consumption, so official
  // A/B orchestration can reject an enabled experiment which never executed.
  static int first_consumed;
  if(!first_consumed) {
    first_consumed=1;
    LOG("[vulkan state segments] first actual state segment operations=%u",n);
  }
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  state_segments_consumed++;state_operations_consumed+=n;
#endif
  return n;
}
void vk_pass_plan_done(const void *payload) {
  if(valid && replay && current_entry()->op+8==payload) done=1;
}
int vk_pass_samples_live(const void *payload) {
  if(atomic_load(&sample_liveness_active)||!valid||!store_valid||!replay||!vk_replay_preserves_ops()) return 1;
  const planned_op *o=current_entry();
  // Reuse the existing reverse RESET/REPORT proof. Tail results remain live
  // across submissions; internal draws and transformed replay retain queries.
  if(o->op+8!=payload||(o->opcode!=OP_DRAW&&o->opcode!=OP_DRAW_IDX)||o->sample_live) return 1;
  sample_elision_used=1;++recorded_sample_elided;return 0;
}
#include "nvn_vk_clear_group.inc"
static int color_clear_group_cursor(vk_pass_cursor cursor,unsigned remaining) {
  if(!sparse_mode)return color_clear_group_without_depth(&plan[replay].opcode,sizeof *plan,remaining);
  unsigned limit=remaining<256?remaining:256;
  for(unsigned k=0;k<limit;k++,cursor_advance_raw(&cursor)) {
    int step=color_clear_group_step(cursor_opcode(&cursor),k+1==remaining);
    if(step)return step>0;
  }
  return 0;
}
unsigned vk_pass_depth_need(void) {
  if(!valid || !replay) return 3;
  const planned_op *o=current_entry();
  // 起始颜色清除本身不用深度；后面的清除仍按原保守规则计入。
  // An initial color clear does not use depth; subsequent clears remain conservative.
  if(o->opcode==OP_CLEAR_COLOR) {
    // Multiple color clears may share one physical pass without ever using
    // depth/stencil. The original reverse scan treats later clears as DS
    // consumers; narrow that only after a complete bounded no-draw proof.
    if(store_valid && vk_replay_preserves_ops() && replay<count &&
        color_clear_group_cursor(cursor_at_next(),count-replay))
      return 0;
    return replay<count?plan[sparse_mode?next_cursor.node:replay].need:0;
  }
  return o->opcode==OP_DRAW || o->opcode==OP_DRAW_IDX?o->need:3;
}
unsigned vk_pass_depth_writes(void) {
  if(!valid || !store_valid || !replay || !vk_replay_preserves_ops()) return 3;
  const planned_op *o=current_entry();
  if(o->opcode==OP_CLEAR_COLOR) return replay<count?plan[sparse_mode?next_cursor.node:replay].writes:0;
  return o->opcode==OP_DRAW || o->opcode==OP_DRAW_IDX?o->writes:3;
}
// Only isolate a sole clear. Preserve grouping with subsequent clears/draws;
// an inconclusive bounded scan keeps the ordinary target set. The isolated
// clear is independently correct even if a later operation starts a new pass.
int vk_pass_clear_without_draws(void) {
  if(!valid || !store_valid || !replay || !vk_replay_preserves_ops()) return 0;
  unsigned current=current_entry()->opcode;
  if(current!=OP_CLEAR_COLOR && current!=OP_CLEAR_DS) return 0;
  unsigned limit=count-replay>256?replay+256:count;
  vk_pass_cursor cursor=cursor_at_next();
  for(unsigned k=replay;k<limit;k++,cursor_advance_raw(&cursor)) {
    switch(cursor_opcode(&cursor)) {
    case OP_DRAW:case OP_DRAW_IDX:case OP_CLEAR_COLOR:case OP_CLEAR_DS:return 0;
    case OP_SET_RT:case OP_COPY_B2B:case OP_UPDATE_UB:case OP_COPY_B2T:
    case OP_COPY_T2B:case OP_COPY_T2T:case OP_CLEAR_TEX:case OP_DISPATCH:return 1;
    default:break;
    }
  }
  return limit==count;
}

// 原 sd_finish 的同提交覆盖证明。采样按整张算，级/层的附件覆盖分开算。
// Same-submit overwrite proof from upstream sd_finish. Sampling reads the whole
// texture conservatively; attachment overwrite proofs distinguish mip/layer.
static int overlaps(const tex_t *a,const tex_t *b) {
  if(!a||!b) return 0;
  if(a->serial==b->serial) return 1;
  uintptr_t x=(uintptr_t)a->mem,y=(uintptr_t)b->mem;
  return x&&y&&(x==y||(x<y?y-x<a->L.size:x-y<b->L.size));
}
static int level(const view_t *v) { return v&&v->set_levels?v->base_level:0; }
static int layer(const view_t *v) { return v&&v->set_layers?v->min_layer:0; }
static int same(const tex_t *a,const view_t *av,const tex_t *b,const view_t *bv) {
  return a&&b&&a->serial==b->serial&&level(av)==level(bv)&&layer(av)==layer(bv);
}
static int full_clear(const tex_t *t,const view_t *v,const p_scissor *sc,int has_sc) {
  int l=level(v);if(l<0||l>31) return 0;
  uint32_t w=t->d.width>>l,h=t->d.height>>l;if(!w) w=1;if(!h) h=1;
  return !has_sc || (sc->x<=0&&sc->y<=0&&(int64_t)sc->x+sc->w>=w&&(int64_t)sc->y+sc->h>=h);
}
static int store_scan(const tex_t *target,const view_t *view,VkImageAspectFlagBits plane) {
  if(!valid || !store_valid || !replay || !vk_replay_preserves_ops()) return 1;
  ++recorded_reference_scans;
  p_set_rt rt=S.targets;p_scissor sc=S.scissor;int has_sc=S.has_scissor;
  const prog_t *program=S.program;uint64_t slots[6][160];
  for(unsigned st=0;st<6;st++) {
    memcpy(slots[st],S.tex[st],sizeof S.tex[st]);
    memcpy(slots[st]+128,S.img[st],sizeof S.img[st]);
  }
  vk_pass_cursor cursor=cursor_at_current();
  if(done)cursor_advance_raw(&cursor);
  for(;cursor.ordinal<count;cursor_advance_raw(&cursor)) {
    ++recorded_reference_ops;
    op_hdr h;memcpy(&h,cursor.op,sizeof h);const void *a=cursor.op+8;
    switch(h.op) {
    case OP_SET_RT:rt=*(const p_set_rt *)a;break;
    case OP_SCISSOR:sc=*(const p_scissor *)a;has_sc=1;break;
    case OP_BIND_PROGRAM:program=((const p_bind_program *)a)->prog;break;
    case OP_BIND_TEX:case OP_BIND_IMG: {
      const p_bind_handles *b=a;int base=h.op==OP_BIND_IMG?128:0,limit=h.op==OP_BIND_IMG?32:128;
      if(b->stage<0||b->stage>=6||b->first<0||b->n<0||b->n>limit-b->first) return 1;
      memcpy(slots[b->stage]+base+b->first,b->h,b->n*8);break;
    }
    case OP_DRAW:case OP_DRAW_IDX:case OP_DISPATCH: {
      vk_shader *shaders[6]={0};if(program) vk_program_shaders(program,shaders);
      for(unsigned st=0;st<6;st++) {
        if((h.op==OP_DISPATCH)!=(st==5)) continue;
        vk_shader *s=shaders[st];
        unsigned count=s?s->nb:160;
        for(unsigned i=0;i<count;i++) {
          int slot=i;
          if(s) {
            const vk_binding *b=&s->bindings[i];
            if(b->kind==4||b->kind==9) slot=128+b->reg;
            else if(b->kind==3||b->kind==6||b->kind==7) slot=b->reg;
            else continue;
          }
          if(slot<0||slot>=160) return 1;
          uint64_t handle=slots[st][slot];if(!handle) continue;
          tex_t t;if(!vk_texture_descriptor(handle&0xfffff,&t)||overlaps(target,&t)) return 1;
        }
      }
      if(h.op!=OP_DISPATCH) {
        if(rt.n<0||rt.n>8) return 1;
        for(int k=0;k<rt.n;k++)
          if(overlaps(target,rt.color[k]) && (target->serial!=rt.color[k]->serial||same(target,view,rt.color[k],rt.has_cview[k]?&rt.cview[k]:NULL))) return 1;
        if(overlaps(target,rt.depth) && (target->serial!=rt.depth->serial||same(target,view,rt.depth,rt.has_dview?&rt.dview:NULL))) return 1;
      }
      break;
    }
    case OP_CLEAR_COLOR: {
      const p_clear_color *c=a;if(rt.n<0||rt.n>8) return 1;
      if(c->index<0||c->index>=rt.n) return 1;
      const tex_t *t=rt.color[c->index];const view_t *v=rt.has_cview[c->index]?&rt.cview[c->index]:NULL;
      if(overlaps(target,t)) {
        if(!same(target,view,t,v)) { if(target->serial!=t->serial) return 1;break; }
        return !(plane==VK_IMAGE_ASPECT_COLOR_BIT && (c->mask&15)==15 && full_clear(t,v,&sc,has_sc));
      }
      break;
    }
    case OP_CLEAR_DS: {
      const p_clear_ds *c=a;const view_t *v=rt.has_dview?&rt.dview:NULL;
      if(overlaps(target,rt.depth)) {
        if(!same(target,view,rt.depth,v)) { if(target->serial!=rt.depth->serial) return 1;break; }
        int clears=plane==VK_IMAGE_ASPECT_DEPTH_BIT?c->depth_mask:(c->stencil_mask&255)==255;
        if(clears) return !full_clear(rt.depth,v,&sc,has_sc);
      }
      break;
    }
    case OP_COPY_T2T: {
      const p_copy_t2t *c=a;if(overlaps(target,c->src)||overlaps(target,c->dst)) return 1;break;
    }
    case OP_COPY_T2B:case OP_COPY_B2T:
      if(overlaps(target,((const p_copy_b2t *)a)->tex)) return 1;
      break;
    case OP_CLEAR_TEX:
      if(overlaps(target,((const p_clear_tex *)a)->tex)) return 1;
      break;
    case OP_SET_TEX_POOL:case OP_SET_SAMP_POOL:return 1;
    default:break;
    }
  }
  return 1;
}
#include "nvn_vk_store_index.inc"

#include "nvn_vk_mip_clear_plan.inc"

#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
#include "tests/planned_forward.inc"
#include "tests/store_walk_fusion.inc"
#endif
