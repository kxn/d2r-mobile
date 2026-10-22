// Host-only oracle for the production representation and unchanged query.
// Original uncompressed representation supplies every start/view/plane answer;
// the existing real GPU store fixture additionally runs the independent scan.
#define FRONTIER_CHECK(x) do { ++frontier_checks; if(!(x)) die("store frontier oracle: %s at %u",#x,__LINE__); } while(0)
unsigned vk_pass_test_store_frontier(void) {
  unsigned frontier_checks=0;
  vk_state saved=S;
  int old_policy=atomic_load(&frontier_reference_requested);
  int old_reference=V.options.store_reference,old_check=V.options.store_check;
  V.options.store_reference=0;V.options.store_check=1;
  uint8_t memory[16384]={0};tex_t t[3]={0};
  for(unsigned i=0;i<3;i++) {
    t[i].serial=UINT64_C(0x700000001)+i;
    t[i].mem=memory+(i==1?1024:i==2?8192:0);t[i].L.size=4096;
    t[i].d.width=t[i].d.height=16;
  }
  unsigned original_events=0,compact_events=0;
  for(unsigned variant=0;variant<13;variant++) {
    memset(&S,0,sizeof S);
    p_set_rt rt={.n=1,.color={&t[0]},.depth=&t[0]};
    S.targets=rt;S.scissor=(p_scissor){0,0,16,16};S.has_scissor=1;
    uint64_t words[1024]={0};unsigned used=0;
    p_draw d={NVN_DRAW_PRIMITIVE_TRIANGLES,0,3};
    p_copy_b2t read={.tex=&t[1]};
    p_clear_color clear={.index=0,.mask=variant&1?7:15};
    p_clear_ds ds={.depth_mask=variant&2?0:1,.stencil_mask=variant&4?127:255};
    // Alias B reads before and after A's clear must not migrate across it.
    for(unsigned i=0;i<5;i++)forward_test_append((void *)words,&used,OP_COPY_T2B,&read,sizeof read);
    forward_test_append((void *)words,&used,OP_CLEAR_COLOR,&clear,sizeof clear);
    for(unsigned i=0;i<5;i++)forward_test_append((void *)words,&used,OP_COPY_T2B,&read,sizeof read);
    for(unsigned i=0;i<5;i++)forward_test_append((void *)words,&used,OP_DRAW,&d,sizeof d);
    // Same serial, distinct mip/layer ATT membership, and a global need.
    rt.has_cview[0]=1;rt.cview[0]=(view_t){.set_levels=1,.base_level=1,.set_layers=1,.min_layer=variant%3};
    forward_test_append((void *)words,&used,OP_SET_RT,&rt,sizeof rt);
    for(unsigned i=0;i<3;i++)forward_test_append((void *)words,&used,OP_DRAW,&d,sizeof d);
    forward_test_append((void *)words,&used,OP_SET_TEX_POOL,NULL,0);
    forward_test_append((void *)words,&used,OP_CLEAR_DS,&ds,sizeof ds);
    read.tex=&t[0];
    for(unsigned i=0;i<4;i++)forward_test_append((void *)words,&used,OP_COPY_B2T,&read,sizeof read);
    // Nonaliased clears still delimit runs; partial scissor cannot kill.
    rt.color[0]=&t[2];rt.depth=&t[2];
    p_scissor sc={0,0,variant&8?4:16,16};
    forward_test_append((void *)words,&used,OP_SCISSOR,&sc,sizeof sc);
    forward_test_append((void *)words,&used,OP_SET_RT,&rt,sizeof rt);
    forward_test_append((void *)words,&used,OP_CLEAR_COLOR,&clear,sizeof clear);
    for(unsigned i=0;i<4;i++)forward_test_append((void *)words,&used,OP_COPY_T2B,&read,sizeof read);
    forward_test_append((void *)words,&used,OP_END,NULL,0);
    uint64_t handle=(uintptr_t)words;
    unsigned char answers[256*3*3*3*3];unsigned total=0,original_count=0;
    for(unsigned mode=0;mode<2;mode++) {
      // First exact old representation, then actual compact production mode.
      atomic_store(&frontier_reference_requested,mode?0:1);
      if(variant==12) {
        // Actual production event builders, interleaved READ/ATT at one seq.
        // A global clear of the alias cuts every bucket's active use tail.
        store_build_context ctx;store_index_begin(&ctx);count=8;
        valid=store_valid=1;replay=done=0;
        int cached=-1;view_t view={.set_levels=1,.base_level=1};
        event_add(0,&t[0],NULL,IX_READ,0,0);
        attachment_event_add(0,&cached,&t[0],NULL,IX_ATT,0,0);
        event_add(0,&t[0],NULL,IX_READ,0,0);
        event_add(1,&t[0],NULL,IX_READ,0,0);
        ctx.rt=(p_set_rt){.n=1,.color={&t[1]}};
        p_clear_color all={.index=0,.mask=15};
        store_index_op(&ctx,2,OP_CLEAR_COLOR,&all,8+sizeof all);
        event_add(3,&t[0],NULL,IX_READ,0,0);
        attachment_event_add(3,&cached,&t[0],&view,IX_ATT,0,0);
        attachment_event_add(4,&cached,&t[0],&view,IX_ATT,0,0);
        global_need(5);event_add(6,&t[0],NULL,IX_READ,0,0);
        event_add(7,&t[0],NULL,IX_READ,0,0);
        store_index_finish();
      } else submission_index_build(&handle,1);
      FRONTIER_CHECK(valid&&index_valid&&count<256);
      if(!mode){original_events+=event_count;original_count=count;}
      else {compact_events+=event_count;FRONTIER_CHECK(count==original_count);}
      unsigned k=0;
      for(unsigned seq=0;seq<=count;seq++)for(unsigned target=0;target<3;target++)
        for(unsigned view=0;view<3;view++)for(unsigned layer=0;layer<3;layer++)for(unsigned plane=0;plane<3;plane++) {
          replay=seq+1;done=0;
          view_t v={.set_levels=1,.base_level=view,.set_layers=1,.min_layer=layer};
          VkImageAspectFlagBits bits=plane==0?VK_IMAGE_ASPECT_COLOR_BIT:plane==1?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_STENCIL_BIT;
          int result=store_index_query(&t[target],&v,bits);
          FRONTIER_CHECK(k<sizeof answers);
          if(!mode)answers[k]=result;else FRONTIER_CHECK(answers[k]==result);
          k++;
        }
      if(!mode)total=k;else FRONTIER_CHECK(total==k);
    }
  }
  FRONTIER_CHECK(compact_events<original_events);
  // Original growth failure still disables only the optional store proof.
  unsigned capacity_before=event_capacity;event_capacity=0;store_walk_fail_allocation=1;
  uint64_t words[64]={0};unsigned used=0;p_draw d={NVN_DRAW_PRIMITIVE_TRIANGLES,0,3};
  S.targets=(p_set_rt){.n=1,.color={&t[0]}};
  forward_test_append((void *)words,&used,OP_DRAW,&d,sizeof d);
  forward_test_append((void *)words,&used,OP_END,NULL,0);uint64_t handle=(uintptr_t)words;
  submission_index_build(&handle,1);FRONTIER_CHECK(valid&&!index_valid);
  store_walk_fail_allocation=0;event_capacity=capacity_before;
  submission_index_build(&handle,1);FRONTIER_CHECK(valid&&index_valid);
  LOG("[store frontier fixture] original_events=%u compact_events=%u; bucket=%zu event=%zu",original_events,compact_events,sizeof(store_bucket),sizeof(store_event));
  valid=store_valid=index_valid=0;count=replay=done=0;S=saved;
  // Keep compact and independent original scan checking for the GPU oracle.
  V.options.store_reference=0;V.options.store_check=1;
  (void)old_policy;(void)old_reference;(void)old_check;
  return frontier_checks;
}
#undef FRONTIER_CHECK
