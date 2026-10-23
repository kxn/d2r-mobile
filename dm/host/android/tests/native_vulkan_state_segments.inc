// Host-only contract oracle. No timings or production work counters. The full
// suite separately renders through the original NVN recorder with this mode.
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
static void state_segment_test(void) {
  mtl_init();
  typedef struct { op_hdr h;p_bind_buf b; } packet;
  packet a[305]={0},b[305]={0},child[4]={0};
  vk_state saved=S;int selected=V.options.state_segments;
  int tbdr=vk_tbdr_enabled();vk_tbdr_reference(0);
  for(unsigned k=0;k<304;k++) {
    a[k].h=(op_hdr){OP_BIND_UBO,sizeof(packet)};
    a[k].b=(p_bind_buf){.stage=1,.index=3,.addr=0x10000+256*k,.size=16};
  }
  a[0].h.op=OP_BEGIN;a[304].h=(op_hdr){OP_END,sizeof(packet)};
  for(unsigned k=0;k<3;k++) {
    child[k]=a[k+1];child[k].b.index=4;child[k].b.addr=0x90000+256*k;
  }
  child[3].h=(op_hdr){OP_END,sizeof(packet)};
  mtl_host_apply(1); // owner-applied unfrozen state, matching normal submission
  for(unsigned mode=0;mode<2;mode++)for(unsigned shape=0;shape<6;shape++) {
    V.options.state_segments=mode;
    memcpy(b,a,sizeof b);
    if(shape==1) {
      // Child CALL is replayed at nesting 2; the following parent segment and
      // real JUMP resume the shared flattened plan at their exact old indices.
      b[6].h.op=OP_CALL;uint64_t target=(uintptr_t)child;memcpy(&b[6].b,&target,8);
      b[9].h.op=OP_JUMP;target=(uintptr_t)&b[10];memcpy(&b[9].b,&target,8);
    }
    uint64_t handle=(uintptr_t)b,segments0,ops0,segments1,ops1;
    vk_test_state_segment_stats(&segments0,&ops0);
    mtl_sub_index(&handle,1);
    if(shape==2)vk_texture_shader_begin(); // no-suspended-consumer gate
    if(shape==3)mtl_host_apply(2); // real owner freeze request gate
    if(shape==4) {extern int g_dm_rt_ab;g_dm_rt_ab=1;}
    // A different raw handle must fall back through original mismatch handling.
    // There has been no query elision, so the ordinary mismatch remains safe.
    mtl_exec(shape==5?(const uint8_t *)a:(const uint8_t *)b);
    if(shape==2)vk_texture_shader_abandon();
    if(shape==3)mtl_host_apply(1);
    if(shape==4) {extern int g_dm_rt_ab;g_dm_rt_ab=0;}
    mtl_exec_rest(NULL,0);
    vk_test_state_segment_stats(&segments1,&ops1);
    CHECK(S.ubo[1][3].addr==a[303].b.addr && S.ubo[1][3].size==16);
    if(shape==1)CHECK(S.ubo[1][4].addr==child[2].b.addr);
    if(mode && shape<2)CHECK(segments1>segments0 && ops1>ops0+255);
    else CHECK(segments1==segments0 && ops1==ops0);
  }
  V.options.state_segments=selected;vk_state_restore(&saved);vk_tbdr_reference(!tbdr);
  LOG("[vulkan state segment fixture] startup_mode=%d original setters, capped long spans, nested CALL/JUMP, observer/consumer gates and mismatch fallback passed",selected);
}
#endif
