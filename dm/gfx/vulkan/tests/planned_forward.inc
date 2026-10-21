// Owned host test of the real planner; never compiled into the app.
static unsigned forward_test_checks;
#define FORWARD_CHECK(c) do { ++forward_test_checks; if(!(c)) die("planned forward reuse check: %s at %u",#c,__LINE__); } while(0)
static void forward_test_append(uint8_t *bytes,unsigned *used,unsigned opcode,const void *data,unsigned size) {
  op_hdr h={opcode,(8+size+7)&~7u};
  memcpy(bytes+*used,&h,8);
  if(size) memcpy(bytes+*used+8,data,size);
  *used+=h.size;
}
unsigned vk_pass_test_forward_reuse(void) {
  FORWARD_CHECK(vk_tbdr_enabled());
  vk_state saved=S;
  int saved_reference=V.options.store_reference,saved_check=V.options.store_check;
  V.options.store_reference=1;V.options.store_check=0;
  // Repeated submissions change both initial DS and stream-bound DS. Poison
  // the entire reusable allocation, including reverse outputs and padding.
  for(unsigned round=0;round<4;round++) {
    memset(&S,0,sizeof S);
    ds_st keep={.fail_f=NVN_STENCIL_OP_KEEP,.zfail_f=NVN_STENCIL_OP_KEEP,.zpass_f=NVN_STENCIL_OP_KEEP,
      .fail_b=NVN_STENCIL_OP_KEEP,.zfail_b=NVN_STENCIL_OP_KEEP,.zpass_b=NVN_STENCIL_OP_KEEP};
    ds_st before=keep,after=keep;
    before.dt=before.dw=before.st=1;before.zpass_f=NVN_STENCIL_OP_REPLACE;
    if(round==1) before=keep;
    if(round==2) {before=keep;before.st=1;before.zpass_f=NVN_STENCIL_OP_REPLACE;after.dt=1;}
    if(round==3) {before=keep;before.dt=before.st=1;after.dw=after.st=1;after.zpass_f=NVN_STENCIL_OP_REPLACE;}
    S.ds=before;S.stencil_mask[0]=S.stencil_mask[1]=round==2?0:1;
    uint64_t root_a_words[64]={0},root_b_words[64]={0},child_words[8]={0},tail_words[8]={0};
    uint8_t *a=(void *)root_a_words,*b=(void *)root_b_words,*child=(void *)child_words,*tail=(void *)tail_words;
    unsigned an=0,bn=0,cn=0,tn=0;
    p_draw draw={NVN_DRAW_PRIMITIVE_TRIANGLES,0,3};
    forward_test_append(child,&cn,OP_DRAW,&draw,sizeof draw);forward_test_append(child,&cn,OP_END,NULL,0);
    forward_test_append(tail,&tn,OP_DRAW,&draw,sizeof draw);forward_test_append(tail,&tn,OP_END,NULL,0);
    uint64_t call=(uintptr_t)child,jump=(uintptr_t)tail,counter=NVN_COUNTER_TYPE_SAMPLES_PASSED;
    forward_test_append(a,&an,OP_DRAW,&draw,sizeof draw);
    forward_test_append(a,&an,OP_CALL,&call,8);
    forward_test_append(a,&an,OP_RESET_COUNTER,&counter,8);
    forward_test_append(a,&an,OP_END,NULL,0);
    p_bind_state bind={.kind=5};memcpy(bind.bytes,&after,sizeof after);
    p_stencil stencil={.face=3,.mask=round==3?1:0,.which=1};
    forward_test_append(b,&bn,OP_BIND_STATE,&bind,sizeof bind);
    forward_test_append(b,&bn,OP_STENCIL,&stencil,sizeof stencil);
    forward_test_append(b,&bn,OP_CALL,&call,8);
    forward_test_append(b,&bn,OP_REPORT_COUNTER,&counter,8);
    forward_test_append(b,&bn,OP_JUMP,&jump,8);
    uint64_t roots[2]={(uintptr_t)a,(uintptr_t)b};
    if(capacity) memset(plan,0xa5,capacity*sizeof *plan);
    submission_index_build(roots,2);
    FORWARD_CHECK(valid&&count==15&&replay==0&&!index_valid);
    unsigned before_read=(before.dt||before.dw?1:0)|(before.st?2:0);
    unsigned before_write=(before.dw?1:0)|((round==0)?2:0);
    unsigned after_read=(after.dt||after.dw?1:0)|(after.st?2:0);
    unsigned after_write=(after.dw?1:0)|(round==3?2:0);
    unsigned reads[15]={0},writes_expected[15]={0};
    reads[0]=reads[2]=before_read;reads[9]=reads[13]=after_read;
    writes_expected[0]=writes_expected[2]=before_write;writes_expected[9]=writes_expected[13]=after_write;
    unsigned need=0,writes=0;
    for(unsigned i=count;i-->0;) {
      need|=reads[i];writes|=writes_expected[i];
      FORWARD_CHECK(plan[i].draw==reads[i]);
      FORWARD_CHECK(plan[i].drawing_writes==writes_expected[i]);
      FORWARD_CHECK(plan[i].sample_counter==(i==4||i==11));
      FORWARD_CHECK(plan[i].need==need&&plan[i].writes==writes);
      FORWARD_CHECK(plan[i].sample_live==(i>4));
    }
    // Same child's raw pointer appears twice with different query liveness.
    FORWARD_CHECK(plan[2].op==plan[9].op&&plan[2].sample_live==0&&plan[9].sample_live==1);
    for(unsigned i=0;i<count;i++) {
      vk_pass_plan_op(plan[i].op);FORWARD_CHECK(valid&&replay==i+1);
      if(i==0||i==2||i==9||i==13) {
        FORWARD_CHECK(vk_pass_depth_need()==plan[i].need);
        FORWARD_CHECK(vk_pass_depth_writes()==plan[i].writes);
      }
      vk_pass_plan_done(plan[i].op+8);FORWARD_CHECK(done);
    }
    // Failure after a valid prefix must never expose that prefix or stale
    // reverse results to consumers; the following round reuses the storage.
    op_hdr bad={OP_END,7};uint64_t invalid[2]={(uintptr_t)a,(uintptr_t)&bad};
    submission_index_build(invalid,2);
    FORWARD_CHECK(!valid&&!index_valid&&vk_pass_depth_need()==3&&vk_pass_depth_writes()==3);
    FORWARD_CHECK(vk_pass_samples_live(child+8)==1);
    op_hdr short_bind={OP_BIND_STATE,8};invalid[1]=(uintptr_t)&short_bind;
    submission_index_build(invalid,2);FORWARD_CHECK(!valid&&!index_valid);
    uint64_t cycle[2];op_hdr h={OP_CALL,16};memcpy(cycle,&h,8);cycle[1]=(uintptr_t)cycle;
    invalid[1]=(uintptr_t)cycle;submission_index_build(invalid,2);FORWARD_CHECK(!valid&&!index_valid);
  }
  submission_index_build(NULL,0);FORWARD_CHECK(valid&&!count&&!replay);
  submission_index_build(NULL,1);FORWARD_CHECK(!valid&&!index_valid);
  // Do not let stack-backed test ops escape. Keep generation monotonic.
  valid=store_valid=index_valid=0;count=replay=done=0;
  S=saved;V.options.store_reference=saved_reference;V.options.store_check=saved_check;
  return forward_test_checks;
}
#undef FORWARD_CHECK
