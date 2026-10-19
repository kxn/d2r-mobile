// Owned shaders 41/43: real disjoint-mip sampling/attachment draws. The
// negative same-mip/default-view proofs do not execute an undefined feedback
// loop. A different-serial guest-memory alias is exercised with real GPU draws.
extern int vk_cover_mip_ranges_disjoint(const tex_t *,const view_t *,const tex_t *,const view_t *);
static void sampled_mip_load_cover_test(void) {
  enum { pool_bytes=2<<20 };
  vk_state saved=S;int saved_cover=vk_tbdr_enabled(),saved_probe=vk_load_probe_mode();
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,pool_bytes));memset(storage,0,pool_bytes);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,pool_bytes);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNcommandBuffer local;CHECK(CALL(nvnCommandBufferInitialize,&local,&device));
  CALL(nvnCommandBufferAddCommandMemory,&local,&pool,1<<20,1<<20);
  NVNtexture textures[2];
  for(unsigned n=0;n<2;n++) {
    NVNtextureBuilder tb;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
    CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);
    CALL(nvnTextureBuilderSetWidth,&tb,16);CALL(nvnTextureBuilderSetHeight,&tb,16);
    CALL(nvnTextureBuilderSetDepth,&tb,1);CALL(nvnTextureBuilderSetLevels,&tb,3);
    CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);
    CHECK(CALL(nvnTextureBuilderGetStorageSize,&tb)<=65536);
    // Identical guest storage, distinct NVN serials and distinct VkImages.
    CALL(nvnTextureBuilderSetStorage,&tb,&pool,0);CHECK(CALL(nvnTextureInitialize,&textures[n],&tb));
  }
  NVNtextureView views[3];
  for(unsigned level=0;level<3;level++) {
    CALL(nvnTextureViewSetDefaults,&views[level]);CALL(nvnTextureViewSetLevels,&views[level],level,1);
  }
  const tex_t *t=(const tex_t *)&textures[0],*alias=(const tex_t *)&textures[1];
  const view_t *v0=(const view_t *)&views[0],*v1=(const view_t *)&views[1];
  CHECK(vk_cover_mip_ranges_disjoint(t,v1,t,v0));
  CHECK(vk_cover_mip_ranges_disjoint(t,NULL,t,v1));
  CHECK(!vk_cover_mip_ranges_disjoint(t,v1,t,v1));
  CHECK(!vk_cover_mip_ranges_disjoint(t,v1,t,NULL));
  CHECK(!vk_cover_mip_ranges_disjoint(t,v1,alias,v0));
  view_t bad=*v0;
  bad.nlevels=0;CHECK(!vk_cover_mip_ranges_disjoint(t,v1,t,&bad));
  bad.nlevels=3;CHECK(!vk_cover_mip_ranges_disjoint(t,v1,t,&bad));
  bad=*v0;bad.base_level=-1;CHECK(!vk_cover_mip_ranges_disjoint(t,v1,t,&bad));
  bad=*v0;bad.base_level=3;CHECK(!vk_cover_mip_ranges_disjoint(t,v1,t,&bad));
  bad=*v0;bad.nlevels=4;CHECK(!vk_cover_mip_ranges_disjoint(t,v1,t,&bad));
  bad=*v0;bad.set_levels=0;CHECK(!vk_cover_mip_ranges_disjoint(t,v1,t,&bad));
  bad=*v0;bad.set_format=1;bad.format=NVN_FORMAT_RGBA8_SRGB;
  CHECK(!vk_cover_mip_ranges_disjoint(t,v1,t,&bad));
  bad=*v0;bad.set_target=1;bad.target=NVN_TEXTURE_TARGET_2D_ARRAY;
  CHECK(!vk_cover_mip_ranges_disjoint(t,v1,t,&bad));
  bad=*v0;bad.set_layers=1;bad.min_layer=1;bad.nlayers=1;
  CHECK(!vk_cover_mip_ranges_disjoint(t,v1,t,&bad));
  bad=*v1;bad.nlevels=2;CHECK(!vk_cover_mip_ranges_disjoint(t,&bad,t,v0));
  tex_t changed=*t;changed.d.width++;
  CHECK(!vk_cover_mip_ranges_disjoint(t,v1,&changed,v0));
  changed=*t;changed.d.target=NVN_TEXTURE_TARGET_2D_ARRAY;
  CHECK(!vk_cover_mip_ranges_disjoint(&changed,v1,&changed,v0));
  changed=*t;changed.L.layers=2;
  CHECK(!vk_cover_mip_ranges_disjoint(&changed,v1,&changed,v0));
  NVNtexturePool tp;NVNsamplerPool sp;
  CHECK(CALL(nvnTexturePoolInitialize,&tp,&pool,0x20000,32));
  CHECK(CALL(nvnSamplerPoolInitialize,&sp,&pool,0x30000,32));
  NVNsamplerBuilder sb;NVNsampler sampler;
  CALL(nvnSamplerBuilderSetDefaults,&sb);CALL(nvnSamplerBuilderSetDevice,&sb,&device);
  CALL(nvnSamplerBuilderSetMinMagFilter,&sb,NVN_MIN_FILTER_NEAREST,NVN_MAG_FILTER_NEAREST);
  CALL(nvnSamplerBuilderSetWrapMode,&sb,NVN_WRAP_MODE_CLAMP_TO_EDGE,NVN_WRAP_MODE_CLAMP_TO_EDGE,NVN_WRAP_MODE_CLAMP_TO_EDGE);
  CHECK(CALL(nvnSamplerInitialize,&sampler,&sb));CALL(nvnSamplerPoolRegisterSampler,&sp,1,&sampler);
  NVNseparateSamplerHandle sh=CALL(nvnDeviceGetSeparateSamplerHandle,&device,1);
  uint8_t controls[2][0x880];memset(controls[0],41,0x880);memset(controls[1],43,0x880);
  NVNshaderData shaders[]={{.control=controls[0]},{.control=controls[1]}};NVNprogram program;
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  float *vertices=(void *)(storage+0x40000);
  memcpy(vertices,(float[]){-1,-1,0,1, 3,-1,0,1, -1,3,0,1},48);
  NVNvertexAttribState attr;CALL(nvnVertexAttribStateSetDefaults,&attr);
  CALL(nvnVertexAttribStateSetStreamIndex,&attr,0);CALL(nvnVertexAttribStateSetFormat,&attr,NVN_FORMAT_RGBA32F,0);
  NVNvertexStreamState stream;CALL(nvnVertexStreamStateSetDefaults,&stream);CALL(nvnVertexStreamStateSetStride,&stream,16);
  NVNbufferRange range={(uintptr_t)vertices,48};
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);
  CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);
  CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);CALL(nvnColorStateSetBlendEnable,&color,0,0);
  NVNchannelMaskState mask;CALL(nvnChannelMaskStateSetDefaults,&mask);
  NVNmultisampleState ms;CALL(nvnMultisampleStateSetDefaults,&ms);
  const uint8_t expected[2][3][4]={{{255,0,0,255},{0,255,0,255},{0,0,255,255}},
                                 {{255,0,255,255},{0,255,255,255},{255,255,0,255}}};
  for(unsigned probe=0;probe<2;probe++)for(unsigned enabled=0;enabled<2;enabled++)for(unsigned test=0;test<3;test++) {
    vk_tbdr_reference(!enabled);vk_load_probe_request(probe);
    CALL(nvnCommandBufferBeginRecording,&local);
    CALL(nvnCommandBufferSetTexturePool,&local,&tp);CALL(nvnCommandBufferSetSamplerPool,&local,&sp);
    for(unsigned n=0;n<2;n++)for(unsigned level=0;level<3;level++) {
      float clear[4];for(unsigned c=0;c<4;c++)clear[c]=expected[n][level][c]/255.f;
      NVNcopyRegion region={0,0,0,16>>level,16>>level,1};
      CALL(nvnCommandBufferClearTexture,&local,&textures[n],&views[level],&region,clear,15);
    }
    submit(CALL(nvnCommandBufferEndRecording,&local));
    unsigned source_level=test==1?1:0,target_level=test==1?0:1,target_texture=test==2;
    CALL(nvnTexturePoolRegisterTexture,&tp,1,&textures[0],&views[source_level]);
    NVNseparateTextureHandle th=CALL(nvnDeviceGetSeparateTextureHandle,&device,1);
    unsigned size=16>>target_level;const NVNtexture *targets[]={&textures[target_texture]};
    const NVNtextureView *target_views[]={&views[target_level]};uint64_t before=vk_render_load_omissions();
    CALL(nvnCommandBufferBeginRecording,&local);
    CALL(nvnCommandBufferSetRenderTargets,&local,1,targets,target_views,NULL,NULL);
    CALL(nvnCommandBufferSetViewports,&local,0,1,(float[]){0,0,size,size});
    CALL(nvnCommandBufferSetScissor,&local,0,0,size,size);
    CALL(nvnCommandBufferBindProgram,&local,&program,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferBindVertexAttribState,&local,1,&attr);CALL(nvnCommandBufferBindVertexStreamState,&local,1,&stream);
    CALL(nvnCommandBufferBindVertexBuffers,&local,0,1,&range);
    CALL(nvnCommandBufferBindPolygonState,&local,&polygon);CALL(nvnCommandBufferBindDepthStencilState,&local,&ds);
    CALL(nvnCommandBufferBindColorState,&local,&color);CALL(nvnCommandBufferBindChannelMaskState,&local,&mask);
    CALL(nvnCommandBufferBindMultisampleState,&local,&ms);
    CALL(nvnCommandBufferBindSeparateTextures,&local,NVN_SHADER_STAGE_FRAGMENT,0,1,&th);
    CALL(nvnCommandBufferBindSeparateSamplers,&local,NVN_SHADER_STAGE_FRAGMENT,0,1,&sh);
    CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    submit(CALL(nvnCommandBufferEndRecording,&local));
    uint64_t delta=vk_render_load_omissions()-before;
    if(delta!=(enabled&&test<2))fprintf(stderr,"mip cover probe=%u enabled=%u test=%u omissions=%llu\n",probe,enabled,test,(unsigned long long)delta);
    CHECK(delta==(enabled&&test<2));
    for(unsigned n=0;n<2;n++)for(unsigned level=0;level<3;level++) {
      unsigned dim=16>>level;NVNcopyRegion region={0,0,0,dim,dim,1};uint8_t pixels[16*16*4];
      CALL(nvnTextureReadTexels,&textures[n],&views[level],&region,pixels);
      const uint8_t *wanted=n==target_texture&&level==target_level?expected[0][source_level]:expected[n][level];
      for(unsigned p=0;p<dim*dim;p++)for(unsigned c=0;c<4;c++)CHECK(abs((int)pixels[p*4+c]-wanted[c])<=1);
    }
  }
  vk_tbdr_reference(!saved_cover);vk_load_probe_request(saved_probe);S=saved;vk_pipeline_state_changed();
  CALL(nvnProgramFinalize,&program);CALL(nvnSamplerFinalize,&sampler);
  CALL(nvnTexturePoolFinalize,&tp);CALL(nvnSamplerPoolFinalize,&sp);
  for(unsigned n=0;n<2;n++)CALL(nvnTextureFinalize,&textures[n]);
  CALL(nvnCommandBufferFinalize,&local);CALL(nvnMemoryPoolFinalize,&pool);free(storage);
  LOG("[vulkan mip-cover fixture] disjoint mip0-to-1/mip1-to-0 full draws, retained same-guest different-serial LOAD, all sibling mip pixels, load/probe modes and conservative view/range guards passed");
}
