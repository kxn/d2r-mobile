// Producer registration remains outside the recording owner's TLS scope.
typedef struct { int id; const tex_t *texture; int ok; } lookup_producer;
static void *lookup_producer_register(void *arg) {
  lookup_producer *p=arg;tex_t value;
  mtl_reg_tex(p->id,p->texture,NULL);
  p->ok=vk_texture_descriptor(p->id,&value) && value.serial==p->texture->serial;
  return NULL;
}
static void logical_lookup_boundaries(int id,const tex_t *a,const tex_t *b) {
  tex_t value;view_t view;int has_view;
  vk_texture_lookup_scope_begin();
  mtl_reg_tex(id,a,NULL);CHECK(vk_texture_descriptor(id,&value)&&value.serial==a->serial);
  vk_texture_footprint footprint;CHECK(vk_texture_descriptor_footprint(id,&footprint));
  CHECK(footprint.serial==a->serial && footprint.base==(uintptr_t)a->mem);
  // Nested ghost/retry tables cannot overwrite the parent; registry growth,
  // replacement and a limit change must invalidate every depth immediately.
  for(unsigned n=0;n<9;n++)vk_texture_lookup_scope_begin();
  mtl_set_tex_pool_n(4096);mtl_reg_tex(2048,b,NULL);
  const view_t srgb={.set_format=1,.format=NVN_FORMAT_RGBA8_SRGB};mtl_reg_tex(id,b,&srgb);
  CHECK(vk_texture_descriptor_view(id,&value,&view,&has_view));
  CHECK(value.serial==b->serial && has_view && view.format==NVN_FORMAT_RGBA8_SRGB);
  for(unsigned n=0;n<9;n++)vk_texture_lookup_scope_end();
  CHECK(vk_texture_descriptor(id,&value)&&value.serial==b->serial);
  lookup_producer producer={id,a,0};pthread_t thread;
  CHECK(!pthread_create(&thread,NULL,lookup_producer_register,&producer));
  CHECK(!pthread_join(thread,NULL));CHECK(producer.ok);
  CHECK(vk_texture_descriptor(id,&value)&&value.serial==a->serial);
  mtl_set_tex_pool_n(id);CHECK(!vk_texture_descriptor(id,&value));
  vk_texture_lookup_scope_end();
  // Reusing the same stack depth cannot reuse an old scope's descriptor.
  mtl_set_tex_pool_n(512);mtl_reg_tex(id,b,NULL);
  vk_texture_lookup_scope_begin();CHECK(vk_texture_descriptor(id,&value)&&value.serial==b->serial);
  vk_texture_lookup_scope_end();
  mtl_reg_tex(id,a,NULL);
}
// Consumer pixels remain the oracle: logical no-op preparation must never
// cache uploaded bytes, a physical image/view, a layout or a resource hazard.
static void logical_preflight_pixels(const NVNtexture *texture,const uint8_t color[4],unsigned tolerance) {
  uint8_t pixels[16*16*4];NVNcopyRegion region={0,0,0,16,16,1};
  CALL(nvnTextureReadTexels,texture,NULL,&region,pixels);
  for(unsigned p=0;p<256;p++)for(unsigned c=0;c<4;c++) {
    int delta=(int)pixels[p*4+c]-color[c];CHECK(delta>=-(int)tolerance&&delta<=(int)tolerance);
  }
}
static void logical_preflight_clear(const NVNtexture *texture,unsigned level,const float color[4]) {
  p_clear_tex clear={.tex=(const tex_t *)texture,.has_view=level!=0,
    .view={.set_levels=1,.base_level=level,.nlevels=1},
    .r={0,0,0,16>>level,16>>level,1},.mask=15};
  memcpy(clear.c.f,color,16);vk_texture_clear(&clear);
}
static void logical_preflight_draw(NVNtexture *target,int id) {
  S.targets=(p_set_rt){.n=1,.color={(const tex_t *)target}};S.tex[1][0]=id;vk_pipeline_state_changed();
  const p_draw draw={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};vk_draw(&draw,NULL);
}
static void logical_preflight_test(const void *vertex,const void *fragment) {
  vk_state saved=S;uint64_t hits,prepares,misses;int saved_reference;
  vk_texture_binding_stats(&hits,&prepares,&misses,&saved_reference);
  for(unsigned reference=0;reference<2;reference++) {
    vk_texture_binding_reference(reference);
    uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,1<<20));memset(storage,0,1<<20);
    NVNmemoryPoolBuilder mb;NVNmemoryPool pool;CALL(nvnMemoryPoolBuilderSetDefaults,&mb);
    CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,1<<20);
    CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
    CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));NVNtexture textures[4];
    for(unsigned n=0;n<4;n++) {
      NVNtextureBuilder tb;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
      CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&tb,16);
      CALL(nvnTextureBuilderSetHeight,&tb,16);CALL(nvnTextureBuilderSetLevels,&tb,n==3?2:1);
      CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);CALL(nvnTextureBuilderSetStorage,&tb,&pool,n*65536);
      if(n==1) { NVNpackagedTextureLayout packaged={{0}};CALL(nvnTextureBuilderSetPackagedTextureLayout,&tb,&packaged); }
      CHECK(CALL(nvnTextureInitialize,&textures[n],&tb));
    }
    const tex_t *cpu=(const tex_t *)&textures[1];CHECK(cpu->has_layout);
    for(uint64_t n=0;n<cpu->L.size;n+=4)memcpy((uint8_t *)cpu->mem+n,(uint8_t[]){128,128,128,255},4);
    const float vertices[]={-1,-1,0,0,3,-1,2,0,-1,3,0,2};memcpy(storage+0x60000,vertices,sizeof vertices);
    NVNprogram program;NVNshaderData shaders[]={{.control=vertex},{.control=fragment}};
    CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
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
    enum { ordinary=37,collision=270 };mtl_set_tex_pool_n(512);
    mtl_reg_tex(ordinary,cpu,NULL);mtl_reg_tex(collision,(const tex_t *)&textures[2],NULL);
    logical_lookup_boundaries(ordinary,cpu,(const tex_t *)&textures[2]);
    S=(vk_state){.program=(const prog_t *)&program,.depth_range={0,1},.chmask={~0u}};
    S.poly=*(const poly_st *)&polygon;S.ds=*(const ds_st *)&ds;S.color=*(const color_st *)&color;S.ms=*(const ms_st *)&ms;
    S.attr[0]=*(const va_st *)&attr[0];S.attr[1]=*(const va_st *)&attr[1];S.stream[0]=*(const vs_st *)&stream;
    S.vertex[0]=(NVNbufferRange){(uintptr_t)storage+0x60000,sizeof vertices};
    S.has_viewport=S.has_scissor=1;S.viewport[0][2]=S.viewport[0][3]=16;S.scissor=(p_scissor){0,0,16,16};
    S.samp[1][0]=UINT64_C(1)<<20;vk_pipeline_state_changed();
    // Prove the candidate's positive branch is actually reached: promote the
    // owner after caching a sampled recipe. Its logical descriptor stays the
    // same, but the old physical recipe must fail in the later consumer.
    uint64_t prepares_before;int before_reference;vk_texture_binding_stats(&hits,&prepares_before,&misses,&before_reference);
    mtl_sub_begin();VkImageView before=vk_texture_view(ordinary,0);
    vk_texture_view(ordinary,1);vk_texture_prepare_binding(ordinary,0,VK_SHADER_STAGE_FRAGMENT_BIT);
    VkImageView after=vk_texture_view(ordinary,0);CHECK(after!=before);
    vk_internal_wait(vk_internal_commit());
    int active_reference;vk_texture_binding_stats(&hits,&prepares,&misses,&active_reference);
    CHECK(active_reference==(int)reference);CHECK(prepares-prepares_before==(reference?0:1));
    // Make the first real draw resolve its logical value instead of being
    // satisfied solely by the physical recipe prepared above.
    mtl_reg_tex(ordinary,cpu,NULL);
    mtl_sub_begin();logical_preflight_draw(&textures[0],ordinary);vk_internal_wait(vk_internal_commit());
    logical_preflight_pixels(&textures[0],(uint8_t[]){128,128,128,255},0);
    // GPU then CPU changes at the exact same logical descriptor must still
    // take the consumer's image ownership/dirty upload path on every draw.
    mtl_sub_begin();logical_preflight_clear(&textures[1],0,(float[]){0,1,0,1});
    logical_preflight_draw(&textures[0],ordinary);vk_internal_wait(vk_internal_commit());
    logical_preflight_pixels(&textures[0],(uint8_t[]){0,255,0,255},0);
    dm_guest_write_prepare((void *)cpu->mem,cpu->L.size);
    for(uint64_t n=0;n<cpu->L.size;n+=4)memcpy((uint8_t *)cpu->mem+n,(uint8_t[]){128,128,128,255},4);
    mtl_sub_begin();logical_preflight_draw(&textures[0],ordinary);vk_internal_wait(vk_internal_commit());
    logical_preflight_pixels(&textures[0],(uint8_t[]){128,128,128,255},0);
    // A colliding id and same-id retarget are different logical facts, not
    // proof that the old source or its physical view remains usable.
    mtl_sub_begin();logical_preflight_clear(&textures[2],0,(float[]){1,0,0,1});
    logical_preflight_draw(&textures[0],collision);vk_internal_wait(vk_internal_commit());
    logical_preflight_pixels(&textures[0],(uint8_t[]){255,0,0,255},0);
    uint64_t epoch=vk_texture_descriptor_epoch();mtl_set_tex_pool_n(ordinary);
    tex_t rejected;CHECK(vk_texture_descriptor_epoch()!=epoch);CHECK(!vk_texture_descriptor(ordinary,&rejected));
    mtl_set_tex_pool_n(512);mtl_reg_tex(ordinary,(const tex_t *)&textures[2],NULL);
    mtl_sub_begin();logical_preflight_draw(&textures[0],ordinary);vk_internal_wait(vk_internal_commit());
    logical_preflight_pixels(&textures[0],(uint8_t[]){255,0,0,255},0);
    // An explicit format override is never an ordinary no-preflight recipe.
    const view_t srgb={.set_format=1,.format=NVN_FORMAT_RGBA8_SRGB};mtl_reg_tex(ordinary,cpu,&srgb);
    mtl_sub_begin();logical_preflight_draw(&textures[0],ordinary);vk_internal_wait(vk_internal_commit());
    logical_preflight_pixels(&textures[0],(uint8_t[]){55,55,55,255},1);
    // Same image sampled at mip1 while writing mip0 must retain the full
    // feedback preflight, even after this id previously had a positive fact.
    const view_t mip1={.set_levels=1,.base_level=1,.nlevels=1};mtl_reg_tex(ordinary,(const tex_t *)&textures[3],&mip1);
    mtl_sub_begin();logical_preflight_clear(&textures[3],0,(float[]){1,0,0,1});
    logical_preflight_clear(&textures[3],1,(float[]){0,1,0,1});
    logical_preflight_draw(&textures[3],ordinary);vk_internal_wait(vk_internal_commit());
    logical_preflight_pixels(&textures[3],(uint8_t[]){0,255,0,255},0);
    // STORAGE is a distinct request even with a previously positive sampled
    // id. Its real physical view and GPU producer remain normal operations.
    mtl_reg_tex(ordinary,cpu,NULL);mtl_sub_begin();vk_texture_prepare_binding(ordinary,1,VK_SHADER_STAGE_COMPUTE_BIT);
    CHECK(vk_texture_view(ordinary,1));logical_preflight_clear(&textures[1],0,(float[]){0,0,1,1});
    logical_preflight_draw(&textures[0],ordinary);vk_internal_wait(vk_internal_commit());
    logical_preflight_pixels(&textures[0],(uint8_t[]){0,0,255,255},0);
    // Cross physical recordings and recycled contexts may retain only the
    // logical fact; every consumer still obtains its own physical recipe.
    for(unsigned round=0;round<18;round++) {
      mtl_sub_begin();logical_preflight_clear(&textures[1],0,round&1?(float[]){0,1,0,1}:(float[]){0,0,1,1});
      logical_preflight_draw(&textures[0],ordinary);vk_internal_wait(vk_internal_commit());mtl_finish();
      logical_preflight_pixels(&textures[0],round&1?(uint8_t[]){0,255,0,255}:(uint8_t[]){0,0,255,255},0);
    }
    S=saved;vk_pipeline_state_changed();CALL(nvnSamplerFinalize,&sampler);CALL(nvnProgramFinalize,&program);
    for(unsigned n=0;n<4;n++)CALL(nvnTextureFinalize,&textures[n]);
    CALL(nvnMemoryPoolFinalize,&pool);free(storage);
  }
  vk_texture_binding_reference(saved_reference);mtl_set_tex_pool_n(4);
  LOG("[vulkan logical preflight fixture] positive no-owner branch, CPU/GPU updates, collision/retarget/epoch/limit, format, mip feedback, storage and recycled consumer pixels passed");
}
