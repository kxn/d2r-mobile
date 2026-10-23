// Real texture samples validate GPU contents; the test-only read-only snapshot
// checks private hazard sequence invariants unavailable in GPU profiling tools.
// These are logical sequence numbers, never CPU/GPU performance timestamps.
extern int vk_texture_test_mip_clocks(const tex_t *,uint64_t[20]);
static void mip_stamp_bind(uint64_t handle) {
  p_bind_handles p={.stage=1,.first=0,.n=1,.h={handle}};vk_state_op(OP_BIND_TEX,&p);
}
static void mip_stamp_snapshot(const NVNtexture *texture,uint64_t state[20]) {
  CHECK(vk_texture_test_mip_clocks((const tex_t *)texture,state)==3);
}
static void mip_stamp_pixels(const NVNtexture *texture,unsigned level,unsigned size,
                             const uint8_t expected[4]) {
  NVNtextureView view;CALL(nvnTextureViewSetDefaults,&view);
  CALL(nvnTextureViewSetLevels,&view,level,1);
  NVNcopyRegion region={0,0,0,size,size,1};uint8_t pixels[16*16*4];
  CALL(nvnTextureReadTexels,texture,&view,&region,pixels);
  for(unsigned p=0;p<size*size;p++)CHECK(!memcmp(pixels+p*4,expected,4));
}
static void mip_stamp_test(const void *vertex,const void *fragment) {
  vk_state saved=S;uint8_t *storage;
  CHECK(!posix_memalign((void **)&storage,65536,1<<20));memset(storage,0,1<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtexture textures[3];
  for(unsigned n=0;n<3;n++) {
    NVNtextureBuilder tb;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
    CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);
    CALL(nvnTextureBuilderSetWidth,&tb,16);CALL(nvnTextureBuilderSetHeight,&tb,16);
    CALL(nvnTextureBuilderSetLevels,&tb,n?1:3);CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);
    CALL(nvnTextureBuilderSetStorage,&tb,&pool,n*65536);CHECK(CALL(nvnTextureInitialize,&textures[n],&tb));
  }
  NVNprogram program;NVNshaderData shaders[]={{.control=vertex},{.control=fragment}};
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  const float vertices[]={-1,-1,0,0, 3,-1,2,0, -1,3,0,2};
  memcpy(storage+0x30000,vertices,sizeof vertices);
  NVNvertexAttribState attr[2];for(unsigned n=0;n<2;n++) {
    CALL(nvnVertexAttribStateSetDefaults,&attr[n]);CALL(nvnVertexAttribStateSetStreamIndex,&attr[n],0);
    CALL(nvnVertexAttribStateSetFormat,&attr[n],NVN_FORMAT_RG32F,n*8);
  }
  NVNvertexStreamState stream;CALL(nvnVertexStreamStateSetDefaults,&stream);CALL(nvnVertexStreamStateSetStride,&stream,16);
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);
  CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);NVNmultisampleState ms;CALL(nvnMultisampleStateSetDefaults,&ms);
  NVNsamplerBuilder sb;NVNsampler sampler;CALL(nvnSamplerBuilderSetDefaults,&sb);CALL(nvnSamplerBuilderSetDevice,&sb,&device);
  CALL(nvnSamplerBuilderSetMinMagFilter,&sb,NVN_MIN_FILTER_NEAREST,NVN_MAG_FILTER_NEAREST);
  CALL(nvnSamplerBuilderSetWrapMode,&sb,NVN_WRAP_MODE_CLAMP_TO_EDGE,NVN_WRAP_MODE_CLAMP_TO_EDGE,NVN_WRAP_MODE_CLAMP_TO_EDGE);
  CHECK(CALL(nvnSamplerInitialize,&sampler,&sb));mtl_reg_samp(1,&((const samp_t *)&sampler)->p);
  enum { whole=37,first=38,second=39,third=40 };mtl_set_tex_pool_n(64);
  mtl_reg_tex(whole,(const tex_t *)&textures[0],NULL);
  for(unsigned level=0;level<3;level++) {
    view_t view={.set_levels=1,.base_level=level,.nlevels=1};
    mtl_reg_tex(first+level,(const tex_t *)&textures[0],&view);
  }
  S=(vk_state){.program=(const prog_t *)&program,.depth_range={0,1},.chmask={~0u}};
  S.poly=*(const poly_st *)&polygon;S.ds=*(const ds_st *)&ds;S.color=*(const color_st *)&color;S.ms=*(const ms_st *)&ms;
  S.attr[0]=*(const va_st *)&attr[0];S.attr[1]=*(const va_st *)&attr[1];S.stream[0]=*(const vs_st *)&stream;
  S.vertex[0]=(NVNbufferRange){(uintptr_t)storage+0x30000,sizeof vertices};
  S.has_viewport=S.has_scissor=1;S.viewport[0][2]=S.viewport[0][3]=16;S.scissor=(p_scissor){0,0,16,16};
  mip_stamp_bind(whole);S.samp[1][0]=UINT64_C(1)<<20;
  S.targets=(p_set_rt){.n=1,.color={(const tex_t *)&textures[1]}};vk_pipeline_state_changed();
  p_draw draw={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};
  const float clear_colors[3][4]={{1,0,0,1},{0,1,0,1},{0,0,1,1}};
  mtl_sub_begin();
  for(unsigned level=0;level<3;level++) {
    p_clear_tex clear={.tex=(const tex_t *)&textures[0],.has_view=1,
      .view={.set_levels=1,.base_level=level,.nlevels=1},.r={0,0,0,16>>level,16>>level,1},.mask=15};
    memcpy(clear.c.f,clear_colors[level],16);vk_texture_clear(&clear);
  }
  for(unsigned n=1;n<3;n++) {
    p_clear_tex clear={.tex=(const tex_t *)&textures[n],.r={0,0,0,16,16,1},.mask=15,.c.f={0,0,0,1}};
    vk_texture_clear(&clear);
  }
  uint64_t a[20],b[20],c[20];vk_draw(&draw,NULL);mip_stamp_snapshot(&textures[0],a);
  uint64_t pass=V.passes;vk_draw(&draw,NULL);mip_stamp_snapshot(&textures[0],b);
  CHECK(V.passes==pass&&b[0]>a[0]&&b[2]>a[2]&&b[1]==a[1]&&b[3]==a[3]);
  for(unsigned level=0;level<3;level++)CHECK(a[4+level]>a[1]&&b[4+level]==a[4+level]);
  // New physical pass with the same still-valid read proof must restamp.
  vk_state changed=S;changed.targets.color[0]=(const tex_t *)&textures[2];
  vk_state_restore(&changed);vk_pipeline_state_changed();
  vk_draw(&draw,NULL);mip_stamp_snapshot(&textures[0],c);
  CHECK(V.passes==pass+1&&c[1]>b[1]&&c[2]>b[2]&&c[3]==b[3]);
  for(unsigned level=0;level<3;level++)CHECK(c[4+level]>b[4+level]&&c[4+level]>c[1]);
  // Three different exact views evict the bounded two-entry proof. Returning
  // to the whole view must not infer coverage from only one stamped mip.
  for(unsigned level=0;level<3;level++) { mip_stamp_bind(first+level);vk_draw(&draw,NULL); }
  mip_stamp_bind(whole);vk_draw(&draw,NULL);mip_stamp_snapshot(&textures[0],a);
  for(unsigned level=0;level<3;level++)CHECK(a[4+level]==a[2]&&a[4+level]>c[4+level]);
  CHECK(V.passes==pass+1);vk_internal_wait(vk_internal_commit());
  mip_stamp_pixels(&textures[1],0,16,(const uint8_t[]){255,0,0,255});
  mip_stamp_pixels(&textures[2],0,16,(const uint8_t[]){255,0,0,255});
  // Real producer and layout change invalidate the old image generation.
  mtl_sub_begin();p_clear_tex yellow={.tex=(const tex_t *)&textures[0],.has_view=1,
    .view={.set_levels=1,.base_level=1,.nlevels=1},.r={0,0,0,8,8,1},.mask=15,.c.f={1,1,0,1}};
  vk_texture_clear(&yellow);mip_stamp_bind(second);vk_draw(&draw,NULL);mip_stamp_snapshot(&textures[0],b);
  CHECK(b[3]>a[3]&&b[5]==b[2]&&b[5]>b[1]);
  vk_draw(&draw,NULL);mip_stamp_snapshot(&textures[0],c);
  // A single-mip range keeps the original one-cell stamp. Only multi-mip
  // ranges amortize the extra proof-stamp check and publication.
  CHECK(c[0]>b[0]&&c[2]>=b[2]&&c[5]==c[2]&&c[5]>=b[5]&&c[5]>c[1]);
  // A successful same-pass immutable group may omit the redundant read. Its
  // range remains after pass-begin; new pass/producer checks above and below
  // still require strict restamps. OFF retains the original strict check.
  if(!getenv("DM_VULKAN_IMAGE_BUFFER_SPLIT") || !strcmp(getenv("DM_VULKAN_IMAGE_BUFFER_SPLIT"),"0"))
    CHECK(c[2]>b[2]&&c[5]>b[5]);
  CHECK(c[4]==b[4]&&c[6]==b[6]);vk_internal_wait(vk_internal_commit());
  mip_stamp_pixels(&textures[2],0,16,(const uint8_t[]){255,255,0,255});
  // First STORAGE use promotes physical allocation; requests must use its
  // fresh generation and keep every sibling mip's actual GPU contents.
  mtl_sub_begin();vk_texture_view(whole,1);mip_stamp_bind(whole);
  vk_draw(&draw,NULL);mip_stamp_snapshot(&textures[0],a);
  CHECK(a[3]>c[3]);for(unsigned level=0;level<3;level++)CHECK(a[4+level]==a[2]);
  vk_internal_wait(vk_internal_commit());
  // A physical submission boundary alone is not a visibility invalidation,
  // but the following new render pass still requires all mips to be restamped.
  mtl_sub_begin();vk_draw(&draw,NULL);mip_stamp_snapshot(&textures[0],b);
  CHECK(b[0]>a[0]&&b[1]>a[1]&&b[2]>a[2]);
  for(unsigned level=0;level<3;level++)CHECK(b[4+level]>a[4+level]&&b[4+level]==b[2]);
  vk_internal_wait(vk_internal_commit());mip_stamp_pixels(&textures[2],0,16,(const uint8_t[]){255,0,0,255});
  for(unsigned level=0;level<3;level++)mip_stamp_pixels(&textures[0],level,16>>level,
    level==1?(const uint8_t[]){255,255,0,255}:level==2?(const uint8_t[]){0,0,255,255}:(const uint8_t[]){255,0,0,255});
  mtl_set_tex_pool_n(4);S=saved;vk_pipeline_state_changed();
  CALL(nvnProgramFinalize,&program);CALL(nvnSamplerFinalize,&sampler);
  for(unsigned n=0;n<3;n++)CALL(nvnTextureFinalize,&textures[n]);CALL(nvnMemoryPoolFinalize,&pool);free(storage);
  LOG("[vulkan mip stamp fixture] real GPU samples, exact-repeat logical clock classification, new pass, proof eviction, producer/layout invalidation, physical promotion and cross-submit restamps passed");
}
