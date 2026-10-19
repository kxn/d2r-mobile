// Owned GPU bytes only; intentional direct guest inspection below diagnoses
// skipped publication, never treats native raw CPU access as automatically safe.
#ifdef DM_VULKAN_TEST_EMBED
extern int vk_test_pool_publication(uint64_t,uint64_t,int *,int *,int *);
static void publication_write(uint8_t *address,unsigned bytes,uint32_t value) {
  mtl_sub_begin();vk_pool_requirement need=pool_window_need(address,bytes,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0);
  int op=vk_pool_operation_begin_ranges(&need,1);pool_window_fill(address,bytes,value);
  vk_buffer_writeback_stage((uintptr_t)address,bytes,VK_PIPELINE_STAGE_TRANSFER_BIT);
  vk_pool_operation_end(op);vk_internal_wait(vk_internal_commit());
}
static void publication_gpu_check(uint8_t *address,unsigned bytes,const uint8_t *expected) {
  uint8_t *actual=malloc(bytes);CHECK(actual!=NULL);mtl_sub_begin();
  vk_pool_requirement need=pool_window_need(address,bytes,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0);
  int op=vk_pool_operation_begin_ranges(&need,1);pool_window_read(address,bytes,actual);
  vk_pool_operation_end(op);vk_internal_wait(vk_internal_commit());
  for(unsigned k=0;k<bytes;k++)CHECK(actual[k]==expected[k]);free(actual);
}
static void publication_meta(uint8_t *address,unsigned bytes,int eligible,int unknown,int exposed) {
  int a=-1,b=-1,c=-1;CHECK(vk_test_pool_publication((uintptr_t)address,bytes,&a,&b,&c));
  CHECK(a==eligible&&b==unknown&&c==exposed);
}
static void publication_ghost_unknown_test(unsigned test) {
  uint8_t controls[2][0x880];memset(controls[0],12,sizeof controls[0]);memset(controls[1],4,sizeof controls[1]);
  const void *vertex=controls[0],*fragment=controls[1];
  void mtl_ui_ghost(int,float,float,const float[4],float,float);
  int mtl_ui_ghost_rect(float[4]);vk_state saved=S;
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,1<<20));memset(storage,0,1<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;CALL(nvnMemoryPoolBuilderSetDefaults,&mb);
  CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,(test==2?NVN_MEMORY_POOL_FLAGS_CPU_CACHED:NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS)|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtexture textures[2];for(unsigned i=0;i<2;i++) {
    NVNtextureBuilder tb;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
    CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&tb,i?4:16);
    CALL(nvnTextureBuilderSetHeight,&tb,i?4:16);CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);
    if(i) {
      NVNpackagedTextureLayout packaged={{0}};CALL(nvnTextureBuilderSetPackagedTextureLayout,&tb,&packaged);
      for(unsigned byte=65536;byte<69632;byte+=4)memcpy(storage+byte,(uint32_t[]){0xff80ff00u},4);
    }
    CALL(nvnTextureBuilderSetStorage,&tb,&pool,i*65536);CHECK(CALL(nvnTextureInitialize,&textures[i],&tb));
  }
  const float quad[]={-.75f,-.75f,0,0,0,-.25f,-.75f,0,1,0,-.75f,-.25f,0,0,1,-.25f,-.25f,0,1,1};
  memcpy(storage+0x30000,quad,sizeof quad);memcpy(storage+0x40000,quad,sizeof quad);
  float *other=(float *)(storage+0x40000);for(unsigned n=0;n<4;n++)other[n*5]+=1;
  float *matrix=(float *)(storage+0x50000);matrix[0]=matrix[5]=matrix[10]=matrix[15]=1;
  NVNprogram program;NVNshaderData shaders[]={{.control=vertex},{.control=fragment}};
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  NVNvertexAttribState attr[2];for(unsigned n=0;n<2;n++) {
    CALL(nvnVertexAttribStateSetDefaults,&attr[n]);CALL(nvnVertexAttribStateSetStreamIndex,&attr[n],0);
    CALL(nvnVertexAttribStateSetFormat,&attr[n],n?NVN_FORMAT_RG32F:NVN_FORMAT_RGB32F,n?12:0);
  }
  NVNvertexStreamState stream;CALL(nvnVertexStreamStateSetDefaults,&stream);CALL(nvnVertexStreamStateSetStride,&stream,20);
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);
  CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);
  enum { texture_id=77 };mtl_set_tex_pool_n(256);mtl_reg_tex(texture_id,(const tex_t *)&textures[1],NULL);
  mtl_sub_begin();S=(vk_state){.program=(const prog_t *)&program,.depth_range={0,1},.chmask={~0u}};
  S.targets=(p_set_rt){.n=1,.color={(const tex_t *)&textures[0]}};
  S.poly=*(const poly_st *)&polygon;S.ds=*(const ds_st *)&ds;S.color=*(const color_st *)&color;
  S.attr[0]=*(const va_st *)&attr[0];S.attr[1]=*(const va_st *)&attr[1];S.stream[0]=*(const vs_st *)&stream;
  S.vertex[0]=(NVNbufferRange){(uintptr_t)storage+0x30000,80};
  if(test) {
    S.attr[1].stream=1;S.stream[1]=S.stream[0];S.stream[1].divisor=2;
    S.vertex[1]=(NVNbufferRange){(uintptr_t)storage+0x40000,80};vk_vertex_divisor_fallback(1);
  }
  S.ubo[0][0]=(p_bind_buf){.stage=0,.index=0,.addr=(uintptr_t)matrix,.size=64};S.tex[1][0]=texture_id;
  S.has_viewport=S.has_scissor=1;S.viewport[0][2]=S.viewport[0][3]=16;S.scissor=(p_scissor){0,0,16,16};
  vk_pipeline_state_changed();
  p_clear_tex clear={.tex=(const tex_t *)&textures[0],.r={0,0,0,16,16,1},.mask=15,.c.f={0,0,0,1}};
  vk_texture_clear(&clear);clear.tex=(const tex_t *)&textures[1];clear.r.width=clear.r.height=4;
  memcpy(clear.c.f,(float[]){0,1,.5f,1},16);vk_texture_clear(&clear);
  vk_buffer *raw;uint64_t raw_offset;
  uint8_t *raw_source=storage+(test?0x40000:65536);uint32_t written_word=test?0x3f000000u:0xff0000ffu;
  CHECK(vk_pool_address_stage((uintptr_t)raw_source,4096,(void **)&raw,&raw_offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
  const float grid[]={.125f,.125f,.25f,.25f};mtl_ui_ghost(1,.25f,.25f,grid,.75f,.75f);
  p_draw draw={.prim=NVN_DRAW_PRIMITIVE_TRIANGLE_STRIP,.count=4};vk_draw(&draw,NULL);
  CHECK(vk_render_active());uint64_t pass=V.passes,draws=V.draws;
  pool_range_fixture pending={.capacity=32};CHECK(vk_ghost_pool_ranges(pool_range_collect,&pending));CHECK(pending.count>0);
  uint64_t recording=vk_recording_serial(),fence=mtl_last_fence();VkCommandBuffer command=V.cmd;
  // The raw pool buffer is independent of the already-uploaded sampled image.
  // Keep the real secondary pending, then issue a legal primary GPU writer to
  // its CPU source. This deliberately tests the otherwise rare nested edge;
  // it does not claim a normal NVN transfer bypasses render_end.
  VkBufferMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
    .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
    .buffer=raw->handle,.offset=raw_offset,.size=4096};
  VkCommandBuffer primary=vk_render_pending_primary();CHECK(primary && primary!=V.cmd);
  vkCmdPipelineBarrier(primary,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,1,&host,0,NULL);
  vkCmdFillBuffer(primary,raw->handle,raw_offset,4096,written_word);
  vk_buffer_transfer_written_range((uintptr_t)raw_source,4096);
  vk_buffer_writeback_stage((uintptr_t)raw_source,4096,VK_PIPELINE_STAGE_TRANSFER_BIT);
  if(!test)CHECK(!vk_pool_cpu_source_ready((uintptr_t)raw_source,4096));
  else {
    vk_shader *current_shaders[6]={0};vk_program_shaders(S.program,current_shaders);
    CHECK(vk_shader_cpu_sources_ready(current_shaders,&S,0)); // no image reason to reject
    CHECK(vk_pool_pending_writes((uintptr_t)raw_source,20));
    CHECK(vk_pool_cpu_unknown((uintptr_t)raw_source,20)==(test==1));
    CHECK(!vk_ghost_cpu_draw_ready(current_shaders,&S,&draw,NULL));
    // A CPU-authoritative pool can still have an in-flight writer. Even the
    // optional cover wait reference must reject it without submitting here.
    vk_buffer_sync_stats prior;vk_buffer_sync_snapshot(&prior);
    int prior_cover=prior.cover_reference;
    if(prior_cover<0){const char *c=getenv("DM_VULKAN_COVER_WAIT_REFERENCE");prior_cover=c&&!strcmp(c,"1");}
    uint32_t cover_words[5];memset(cover_words,0x5a,sizeof cover_words);vk_cover_wait_reference(1);
    CHECK(!vk_cover_read((uintptr_t)raw_source,sizeof cover_words,cover_words));
    for(unsigned k=0;k<5;k++)CHECK(cover_words[k]==0x5a5a5a5au);
    vk_cover_wait_reference(prior_cover);
  }
  vk_ghost_flush();
  CHECK(V.draws==draws && V.passes==pass && V.cmd==command && vk_recording_serial()==recording && mtl_last_fence()==fence);
  float rect[4];CHECK(!mtl_ui_ghost_rect(rect));mtl_ui_ghost(0,0,0,NULL,0,0);
  vk_internal_wait(vk_internal_commit());
  uint8_t pixels[1024];NVNcopyRegion region={0,0,0,16,16,1};CALL(nvnTextureReadTexels,&textures[0],NULL,&region,pixels);
  for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++) {
    int original=x>=2&&x<6&&y>=2&&y<6;
    CHECK(!memcmp(pixels+(y*16+x)*4,original?(uint8_t[]){0,255,128,255}:(uint8_t[]){0,0,0,255},4));
  }
  // Subsequent ordinary NVN drawing still consumes the now-published source.
  // Its original packaged upload sees red, with every original pixel checked.
  mtl_sub_begin();clear.tex=(const tex_t *)&textures[0];clear.r.width=clear.r.height=16;
  memcpy(clear.c.f,(float[]){0,0,0,1},16);vk_texture_clear(&clear);vk_draw(&draw,NULL);
  vk_internal_wait(vk_internal_commit());CALL(nvnTextureReadTexels,&textures[0],NULL,&region,pixels);
  for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++) {
    int original=x>=2&&x<6&&y>=2&&y<6;
    const uint8_t red[]={255,0,0,255},green[]={0,255,128,255},black[]={0,0,0,255};
    CHECK(!memcmp(pixels+(y*16+x)*4,original?(test?green:red):black,4));
  }
  if(test) {for(unsigned k=0;k<20;k+=4){uint32_t got;memcpy(&got,raw_source+k,4);CHECK(got==written_word);}vk_vertex_divisor_fallback(0);}
  S=saved;vk_pipeline_state_changed();mtl_set_tex_pool_n(4);
  CALL(nvnProgramFinalize,&program);for(unsigned n=0;n<2;n++)CALL(nvnTextureFinalize,&textures[n]);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);
  LOG("[pool publication ghost fixture] test=%u candidate then GPU %s writer: optional replay rejected without commit; original and later ordinary pixels plus source bytes passed",test,test?"instance-expansion VBO":"image-source");
}
static void pool_publication_test(void) {
  mtl_init();if(!V.options.pool_defer_publication)return;
  CHECK(mtl_pool_defer_publication_mode()==1);
  vk_pool_sync_reference(1);vk_submission_barrier_reference(1);
  mtl_sub_begin();uint64_t full,used,protected_bytes;int sync_ref;
  vk_pool_sync_policy_stats(&full,&used,&protected_bytes,&sync_ref);
  CHECK(sync_ref==0 && vk_submission_barrier_mode()==0 && mtl_pool_defer_publication_mode()==1);
  vk_internal_wait(vk_internal_commit());
  uint64_t old_window=V.options.pool_window_bytes;vk_state saved=S;
  enum {N=2<<20};uint8_t *memory;CHECK(!posix_memalign((void **)&memory,65536,N));
  for(unsigned windows=0;windows<2;windows++) {
    vk_test_pool_window_bytes(windows?65536:0);
    dm_guest_write_prepare(memory,N);memset(memory,0xcc,N);
    NVNmemoryPoolBuilder pb;NVNmemoryPool pool;
    CALL(nvnMemoryPoolBuilderSetDefaults,&pb);CALL(nvnMemoryPoolBuilderSetDevice,&pb,&device);
    CALL(nvnMemoryPoolBuilderSetStorage,&pb,memory,N);CALL(nvnMemoryPoolBuilderSetFlags,&pb,NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
    CHECK(CALL(nvnMemoryPoolInitialize,&pool,&pb));
    // The ordinary driver-known copy stays eagerly published even though its
    // destination page otherwise qualifies for deferred raw writeback.
    uint8_t *known_source=memory+65536,*known_dest=memory+98304;
    dm_guest_write_prepare(known_source,160);for(unsigned k=0;k<160;k++)known_source[k]=(uint8_t)(k*7+3);
    mtl_sub_begin();mtl_sub_copy((uintptr_t)known_source,(uintptr_t)known_dest,160);
    vk_internal_wait(vk_internal_commit());CHECK(!memcmp(known_source,known_dest,160));
    publication_meta(known_dest,160,1,0,0);
    // Real CS SSBO producer, not only transfer commands: output stays GPU-owned
    // until a required observation, while the existing WB metadata survives.
    uint8_t cs_control[0x880];memset(cs_control,2,sizeof cs_control);NVNprogram compute;
    CHECK(CALL(nvnProgramInitialize,&compute,&device));NVNshaderData sd={.control=cs_control};
    CHECK(CALL(nvnProgramSetShaders,&compute,1,&sd));
    uint32_t *cs_source=(void *)(memory+1048576),*cs_dest=(void *)(memory+1114112),cs_expected[64];
    dm_guest_write_prepare(cs_source,256);for(unsigned k=0;k<64;k++){cs_source[k]=k+11;cs_expected[k]=(k+11)*3+7;}
    mtl_sub_begin();S=(vk_state){.program=(const prog_t *)&compute};
    S.ssbo[5][0]=(p_bind_buf){5,0,(uintptr_t)cs_source,256};S.ssbo[5][1]=(p_bind_buf){5,1,(uintptr_t)cs_dest,256};
    vk_dispatch(&(p_dispatch){1,1,1});vk_internal_wait(vk_internal_commit());
    for(unsigned k=0;k<64;k++)CHECK(cs_dest[k]==0xccccccccu);
    publication_meta((uint8_t *)cs_dest,256,1,1,0);publication_gpu_check((uint8_t *)cs_dest,256,(const uint8_t *)cs_expected);
    mtl_sub_begin();uint32_t cs_read[64];CHECK(vk_pool_read((uintptr_t)cs_dest,256,cs_read)>=0);
    vk_internal_wait(vk_internal_commit());CHECK(!memcmp(cs_read,cs_expected,256));
    CALL(nvnProgramFinalize,&compute);S=saved;
    uint8_t *a=memory+131072+12;uint32_t expected[16],read[16];
    for(unsigned k=0;k<16;k++)expected[k]=0x12345000u+windows;
    publication_write(a,sizeof expected,expected[0]);
    for(unsigned k=0;k<16;k++)CHECK(((uint32_t *)a)[k]==0xccccccccu);
    publication_meta(a,sizeof expected,1,1,0);publication_gpu_check(a,sizeof expected,(const uint8_t *)expected);
    if(windows) {CHECK(vk_test_pool_migrate_whole((uintptr_t)memory));publication_meta(a,sizeof expected,1,1,0);publication_gpu_check(a,sizeof expected,(const uint8_t *)expected);}
    mtl_sub_begin();memset(read,0x5a,sizeof read);
    CHECK(vk_pool_try_read((uintptr_t)a,sizeof read,read)==0);CHECK(!vk_cover_read((uintptr_t)a,sizeof read,read));
    for(unsigned k=0;k<16;k++)CHECK(read[k]==0x5a5a5a5au);
    // Strong partial read restores only the requested bytes; completion does
    // not wash the unknown neighbors into CPU-authoritative data.
    uint64_t guest_fence=mtl_last_fence();CHECK(vk_pool_read((uintptr_t)a+4,4,read)>=0);CHECK(read[0]==expected[1]);CHECK(mtl_last_fence()==guest_fence);
    CHECK(((uint32_t *)a)[0]==0xccccccccu&&((uint32_t *)a)[1]==expected[1]);
    CHECK(vk_pool_try_read((uintptr_t)a+4,4,read)==1);CHECK(vk_pool_try_read((uintptr_t)a+8,4,read)==0);
    vk_internal_wait(vk_internal_commit());
    publication_meta(a,4,0,1,1); // exposure is page-conservative, authority exact
    publication_write(a,sizeof expected,0x8192a3b4u);for(unsigned k=0;k<16;k++)CHECK(((uint32_t *)a)[k]==0x8192a3b4u);
    publication_meta(a,sizeof expected,0,0,1);
    // A sparse later CPU edit must not be lost when required visibility pulls
    // old GPU-only neighbors through the normal changed-run upload path.
    uint8_t *late=memory+262144+128;publication_write(late,32,0x33445566u);
    dm_guest_write_prepare(late+8,4);uint32_t cpu=0x89abcdefu;memcpy(late+8,&cpu,4);
    mtl_sub_begin();CHECK(vk_pool_read((uintptr_t)late,32,read)>=0);vk_internal_wait(vk_internal_commit());
    for(unsigned k=0;k<8;k++)CHECK(read[k]==(k==2?cpu:0x33445566u));
    publication_gpu_check(late,32,(const uint8_t *)read);
    // Late EventInitialize is a real guest HLE exposure job, not an owner
    // mutation of guest memory. It repairs prior skipped bytes before GetValue.
    uint8_t *event_bytes=memory+393216+64;publication_write(event_bytes,16,7);
    NVNeventBuilder eb;NVNevent event;CALL(nvnEventBuilderSetDefaults,&eb);CALL(nvnEventBuilderSetStorage,&eb,&pool,event_bytes-memory);
    CHECK(CALL(nvnEventInitialize,&event,&eb));CHECK(CALL(nvnEventGetValue,&event)==7);
    publication_meta(event_bytes,4,0,0,1);
    mtl_sub_begin();vk_pool_requirement en=pool_window_need(event_bytes,16,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0);int eo=vk_pool_operation_begin_ranges(&en,1);
    pool_window_fill(event_bytes,16,12);vk_buffer_writeback_stage((uintptr_t)event_bytes,16,VK_PIPELINE_STAGE_TRANSFER_BIT);vk_pool_operation_end(eo);
    mtl_sub_event((volatile uint32_t *)event_bytes,NVN_EVENT_SIGNAL_MODE_ADD,5);vk_internal_wait(vk_internal_commit());
    CHECK(CALL(nvnEventGetValue,&event)==17);CALL(nvnEventFinalize,&event);
    // A late nonbuffer texture registration repairs its CPU source before the
    // first image/dirty proof. Uniform raw block data gives an exact oracle.
    uint8_t *texmem=memory+524288;publication_write(texmem,4096,0xff00ff00u);
    NVNtextureBuilder tb;NVNtexture texture;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
    CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&tb,8);CALL(nvnTextureBuilderSetHeight,&tb,8);CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);
    NVNpackagedTextureLayout packaged={{0}};CALL(nvnTextureBuilderSetPackagedTextureLayout,&tb,&packaged);
    CALL(nvnTextureBuilderSetStorage,&tb,&pool,texmem-memory);CHECK(CALL(nvnTextureInitialize,&texture,&tb));
    uint8_t pixels[8*8*4];NVNcopyRegion r={0,0,0,8,8,1};CALL(nvnTextureReadTexels,&texture,NULL,&r,pixels);
    for(unsigned k=0;k<64;k++) {CHECK(pixels[4*k]==0);CHECK(pixels[4*k+1]==255);CHECK(pixels[4*k+2]==0);CHECK(pixels[4*k+3]==255);}
    // Sticky exposure does not imply that a newly recorded GPU write has
    // reached guest bytes. The outer source preflight must publish it BEFORE
    // image_get/dirty early returns and preserve the already-created image.
    mtl_sub_begin();vk_pool_requirement tn=pool_window_need(texmem,4096,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0);
    int to=vk_pool_operation_begin_ranges(&tn,1);pool_window_fill(texmem,4096,0xff0000ffu);
    vk_buffer_writeback_stage((uintptr_t)texmem,4096,VK_PIPELINE_STAGE_TRANSFER_BIT);vk_pool_operation_end(to);
    CHECK(!vk_pool_cpu_source_ready((uintptr_t)texmem,4096));
    vk_texture_cpu_preflight((const tex_t *)&texture);CHECK(vk_pool_cpu_source_ready((uintptr_t)texmem,4096));
    vk_internal_wait(vk_internal_commit());
    CALL(nvnTextureReadTexels,&texture,NULL,&r,pixels);
    for(unsigned k=0;k<64;k++) {CHECK(pixels[4*k]==255);CHECK(pixels[4*k+1]==0);CHECK(pixels[4*k+2]==0);CHECK(pixels[4*k+3]==255);}
    CALL(nvnTextureFinalize,&texture);
    // BC CPU decode is an actual backend CPU reader of buffer bytes. Its
    // source recovery precedes the destination image/temp borrow, including
    // completed skipped data that is absent from current V.effects.
    uint8_t *bc_source=memory+1572864,*bc_target=memory+1638400;
    mtl_sub_begin();vk_pool_requirement bn=pool_window_need(bc_source,8,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0);
    int bo=vk_pool_operation_begin_ranges(&bn,1);pool_window_fill(bc_source,4,0x0000f800u);pool_window_fill(bc_source+4,4,0);
    vk_buffer_writeback_stage((uintptr_t)bc_source,8,VK_PIPELINE_STAGE_TRANSFER_BIT);vk_pool_operation_end(bo);vk_internal_wait(vk_internal_commit());
    CHECK(((uint32_t *)bc_source)[0]==0xccccccccu);publication_meta(bc_source,8,1,1,0);
    CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
    CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&tb,4);CALL(nvnTextureBuilderSetHeight,&tb,4);
    CALL(nvnTextureBuilderSetFormat,&tb,0x41);CALL(nvnTextureBuilderSetPackagedTextureLayout,&tb,&packaged);
    CALL(nvnTextureBuilderSetStorage,&tb,&pool,bc_target-memory);CHECK(CALL(nvnTextureInitialize,&texture,&tb));
    vk_texture_test_force_bc_decode(1);mtl_sub_begin();
    p_copy_b2t bc_copy={.src=(uintptr_t)bc_source,.tex=(const tex_t *)&texture,.r={0,0,0,4,4,1}};
    vk_texture_copy_b2t(&bc_copy,0,0);vk_internal_wait(vk_internal_commit());vk_texture_test_force_bc_decode(0);
    // Read the physical decoded RGBA image with a correctly sized owned GPU
    // buffer; generic NVN ReadTexels retains the original compressed footprint.
    mtl_sub_begin();VkImage decoded_image=vk_texture_native((const tex_t *)&texture);vk_barrier();
    vk_buffer *decoded_readback=vk_temp_buffer(64);VkBufferImageCopy decoded_copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={4,4,1}};
    vkCmdCopyImageToBuffer(V.cmd,decoded_image,VK_IMAGE_LAYOUT_GENERAL,decoded_readback->handle,1,&decoded_copy);
    VkBufferMemoryBarrier decoded_host={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,
      .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=decoded_readback->handle,.offset=0,.size=64};
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&decoded_host,0,NULL);
    vk_effect_copy(pixels,decoded_readback->map,64);vk_internal_wait(vk_internal_commit());
    for(unsigned k=0;k<16;k++){CHECK(pixels[4*k]==255);CHECK(pixels[4*k+1]==0);CHECK(pixels[4*k+2]==0);CHECK(pixels[4*k+3]==255);}
    publication_meta(bc_source,8,0,0,1);CALL(nvnTextureFinalize,&texture);
    // Finalize is itself an ordered CPU exposure. Historical unexposed GPU
    // bytes are restored before the old identity/mirror can be destroyed.
    uint8_t *last=memory+786432+32;publication_write(last,64,0xa5b6c7d8u);
    CHECK(((uint32_t *)last)[0]==0xccccccccu);
    CALL(nvnMemoryPoolFinalize,&pool);for(unsigned k=0;k<16;k++)CHECK(((uint32_t *)last)[k]==0xa5b6c7d8u);
    LOG("[pool publication fixture] windows=%u skip/realGPU/optional/partial/lateCPU/eventADD/lateTexture/finalize passed",windows);
  }
  // Authority collapse can turn a requested CPU observation into a single
  // range crossing existing windows. Admission must promote before its manual
  // lifetime pin; otherwise that pin would prevent quiescent migration.
  vk_test_pool_window_bytes(65536);dm_guest_write_prepare(memory,N);memset(memory,0xcc,N);
  mtl_pool_add_flags(memory,N,NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  mtl_sub_begin();vk_pool_requirement collapse_need=pool_window_need(memory+131072,4096,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0);
  int collapse_op=vk_pool_operation_begin_ranges(&collapse_need,1);
  for(unsigned k=0;k<257;k++){pool_window_fill(memory+131072+k*8,4,0x12560000u+k);vk_buffer_writeback_stage((uintptr_t)memory+131072+k*8,4,VK_PIPELINE_STAGE_TRANSFER_BIT);}
  vk_pool_operation_end(collapse_op);vk_internal_wait(vk_internal_commit());
  mtl_sub_begin();uint32_t cold_cross[2];CHECK(vk_pool_read((uintptr_t)memory+65532,8,cold_cross)>=0);
  CHECK(cold_cross[0]==0xccccccccu&&cold_cross[1]==0xccccccccu);vk_internal_wait(vk_internal_commit());
  vk_pool_span_info promoted;CHECK(vk_pool_span_query((uintptr_t)memory+65532,8,&promoted)==VK_POOL_SPAN_SINGLE);
  uint32_t survivor=0x12560000u;publication_gpu_check(memory+131072,4,(const uint8_t *)&survivor);
  mtl_pool_del(memory);
  // Known zero, CPU_CACHED, and legacy unknown flags never select this policy.
  const uint32_t ordinary_flags[]={0,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED};
  for(unsigned k=0;k<3;k++) {
    dm_guest_write_prepare(memory,N);memset(memory,0xcc,N);
    if(k<2)mtl_pool_add_flags(memory,N,ordinary_flags[k]);else mtl_pool_add(memory,N);
    publication_write(memory+4096,16,0x76540000u+k);
    for(unsigned word=0;word<4;word++)CHECK(((uint32_t *)(memory+4096))[word]==0x76540000u+k);
    publication_meta(memory+4096,16,0,0,0);mtl_pool_del(memory);
  }
  // Arbitrary CPU UPDATE source can overlap only part of a pool or cross two
  // adjacent pools. The HLE bridge clips EVERY intersection, not pool_find's
  // single-owner success. Ordinary prefix bytes retain their original value.
  vk_test_pool_window_bytes(0);dm_guest_write_prepare(memory,N);memset(memory,0xcc,N);
  mtl_pool_add_flags(memory+65536,65536,NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  mtl_pool_add_flags(memory+131072,65536,NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  NVNmemoryPoolBuilder command_builder;NVNmemoryPool command_pool;NVNcommandBuffer local_command;
  CALL(nvnMemoryPoolBuilderSetDefaults,&command_builder);CALL(nvnMemoryPoolBuilderSetDevice,&command_builder,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&command_builder,memory+262144,65536);
  CALL(nvnMemoryPoolBuilderSetFlags,&command_builder,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&command_pool,&command_builder));
  CHECK(CALL(nvnCommandBufferInitialize,&local_command,&device));CALL(nvnCommandBufferAddCommandMemory,&local_command,&command_pool,0,4096);
  publication_write(memory+65536,16,0x12349876u);publication_write(memory+131056,16,0x11223344u);publication_write(memory+131072,16,0x55667788u);
  CHECK(!vk_pool_cpu_source_ready((uintptr_t)memory+65520,32));
  CHECK(!vk_pool_cpu_source_ready((uintptr_t)memory+131056,32));
  CALL(nvnCommandBufferBeginRecording,&local_command);
  CALL(nvnCommandBufferUpdateUniformBuffer,&local_command,(uintptr_t)memory+266240,128,0,32,memory+131056);
  CALL(nvnCommandBufferUpdateUniformBuffer,&local_command,(uintptr_t)memory+266240,128,64,32,memory+65520);
  NVNcommandHandle update=CALL(nvnCommandBufferEndRecording,&local_command);
  CALL(nvnQueueSubmitCommands,&queue,1,&update);CALL(nvnQueueFinish,&queue);
  for(unsigned k=0;k<8;k++)CHECK(((uint32_t *)(memory+266240))[k]==(k<4?0x11223344u:0x55667788u));
  for(unsigned k=0;k<8;k++)CHECK(((uint32_t *)(memory+266304))[k]==(k<4?0xccccccccu:0x12349876u));
  CALL(nvnCommandBufferFinalize,&local_command);CALL(nvnMemoryPoolFinalize,&command_pool);
  mtl_pool_del(memory+65536);mtl_pool_del(memory+131072);
  for(unsigned test=0;test<3;test++)publication_ghost_unknown_test(test);
  vk_test_pool_window_bytes(old_window);S=saved;vk_pipeline_state_changed();free(memory);
  CHECK(vk_validation_error_count()==0);
}
#else
static void pool_publication_test(void) {}
#endif
