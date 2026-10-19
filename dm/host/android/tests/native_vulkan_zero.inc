// Owned shader and original NVN commands: prove zero draws can disappear,
// while partial clears, GPU producers, queries and unsafe samplers still draw.
static void zero_outline_test(const void *vertex) {
  uint8_t control[0x880];memset(control,36,sizeof control);
  NVNshaderData shaders[]={{.control=vertex},{.control=control}};
  NVNprogram program;CHECK(CALL(nvnProgramInitialize,&program,&device));
  CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  vk_zero_draw_test_fact(vk_shader_get(control)->key);
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,1<<20));
  memset(storage,0,1<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtexture textures[6];
  for(unsigned t=0;t<6;t++) {
    NVNtextureBuilder b;CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_2D);
    CALL(nvnTextureBuilderSetWidth,&b,16);CALL(nvnTextureBuilderSetHeight,&b,16);
    CALL(nvnTextureBuilderSetLevels,&b,t>=3?3:1);
    CALL(nvnTextureBuilderSetFormat,&b,t==3 || t==4?NVN_FORMAT_R11G11B10F:NVN_FORMAT_RGBA8);
    CALL(nvnTextureBuilderSetStorage,&b,&pool,t*65536);
    CHECK(CALL(nvnTextureInitialize,&textures[t],&b));
  }
  enum { descriptor=37 };mtl_set_tex_pool_n(64);
  mtl_reg_tex(descriptor,(const tex_t *)&textures[0],NULL);
  NVNsamplerBuilder sb;CALL(nvnSamplerBuilderSetDefaults,&sb);CALL(nvnSamplerBuilderSetDevice,&sb,&device);
  NVNsampler sampler;CHECK(CALL(nvnSamplerInitialize,&sampler,&sb));
  samp_par params=((const samp_t *)&sampler)->p;
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);
  CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);
  CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);
  CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);
  NVNchannelMaskState mask;CALL(nvnChannelMaskStateSetDefaults,&mask);
  const float zero[]={0,0,0,0},opaque_zero[]={0,0,0,1},red[]={1,0,0,1},viewport[]={0,0,16,16};
  const NVNtexture *copy_source[]={&textures[2]};
  NVNcopyRegion region={0,0,0,16,16,1};uint8_t pixels[16*16*4];
  vk_state saved=S;int saved_tbdr=vk_tbdr_enabled();vk_tbdr_reference(0);
  for(unsigned step=0;step<15;step++) {
    const NVNtexture *source[]={&textures[step>=11?5:step>=9?3:0]},*target[]={&textures[step==9?4:1]};
    view_t base_mip={.set_levels=1,.nlevels=1};
    mtl_reg_tex(descriptor,(const tex_t *)source[0],step>=9 && step!=11?&base_mip:NULL);
    CALL(nvnColorStateSetBlendEnable,&color,3,step==0 || step==9);
    vk_zero_draw_reference(step==1);
    samp_par current=params;
    if(step==6) { current.ws=4;current.border[3]=1; }
    if(step==11) { current.minf=5;current.lod_max=2; }
    mtl_reg_samp(descriptor,&current);
    dm_guest_write_prepare(storage+0x60000,65536);
    memset(storage+0x60000,0,65536);
    ((float *)(storage+0x60000))[4]=step==5?NAN:1;
    if(step==11) ((float *)(storage+0x60000))[0]=1;
    uint64_t before=vk_zero_draw_skipped();
    uint64_t copies_before=vk_texture_zero_copy_skipped();
    CALL(nvnCommandBufferBeginRecording,&cmd);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);
    if(step>=12) {
      const NVNtexture *producer[]={&textures[0]};
      CALL(nvnCommandBufferSetRenderTargets,&cmd,1,producer,NULL,NULL,NULL);
      CALL(nvnCommandBufferClearColor,&cmd,0,zero,15);
    }
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,source,NULL,NULL,NULL);
    CALL(nvnCommandBufferClearColor,&cmd,0,step==2 || step==3 || step==12 || step==13?red:step>=9 && step<11?opaque_zero:zero,15);
    if(step==11) {
      NVNtextureView mip_view;CALL(nvnTextureViewSetDefaults,&mip_view);
      CALL(nvnTextureViewSetLevels,&mip_view,1,1);
      NVNcopyRegion small={0,0,0,8,8,1};
      CALL(nvnCommandBufferClearTexture,&cmd,&textures[5],&mip_view,&small,red,15);
    }
    if(step>=12) {
      NVNcopyRegion copied={0,0,0,step==13?8:16,step==13?8:16,1};
      CALL(nvnCommandBufferCopyTextureToTexture,&cmd,&textures[0],NULL,&copied,&textures[5],NULL,&copied,0);
    }
    if(step==3) {
      CALL(nvnCommandBufferSetScissor,&cmd,0,0,8,8);
      CALL(nvnCommandBufferClearColor,&cmd,0,zero,15);
      CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);
    }
    if(step==8) {
      CALL(nvnCommandBufferSetRenderTargets,&cmd,1,copy_source,NULL,NULL,NULL);
      CALL(nvnCommandBufferClearColor,&cmd,0,red,15);
      CALL(nvnCommandBufferCopyTextureToTexture,&cmd,&textures[2],NULL,&region,&textures[0],NULL,&region,0);
    }
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,target,NULL,NULL,NULL);
    CALL(nvnCommandBufferClearColor,&cmd,0,step==4?red:step==9?opaque_zero:zero,15);
    if(step==4) {
      CALL(nvnCommandBufferSetScissor,&cmd,0,0,8,8);
      CALL(nvnCommandBufferClearColor,&cmd,0,zero,15);
      CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);
    }
    CALL(nvnCommandBufferSetViewports,&cmd,0,1,viewport);
    CALL(nvnCommandBufferBindDepthStencilState,&cmd,&ds);
    CALL(nvnCommandBufferBindPolygonState,&cmd,&polygon);
    CALL(nvnCommandBufferBindColorState,&cmd,&color);
    CALL(nvnCommandBufferBindChannelMaskState,&cmd,&mask);
    CALL(nvnCommandBufferBindVertexAttribState,&cmd,0,NULL);
    CALL(nvnCommandBufferBindVertexStreamState,&cmd,0,NULL);
    CALL(nvnCommandBufferBindProgram,&cmd,&program,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    NVNseparateTextureHandle th={.value=descriptor};
    NVNseparateSamplerHandle sh={.value=(uint32_t)descriptor<<20};
    CALL(nvnCommandBufferBindSeparateTextures,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&th);
    CALL(nvnCommandBufferBindSeparateSamplers,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&sh);
    CALL(nvnCommandBufferBindUniformBuffer,&cmd,NVN_SHADER_STAGE_FRAGMENT,4,
         (uintptr_t)(storage+0x60000),65536);
    if(step==7) CALL(nvnCommandBufferResetCounter,&cmd,NVN_COUNTER_TYPE_SAMPLES_PASSED);
    CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    if(step==7) CALL(nvnCommandBufferReportCounter,&cmd,NVN_COUNTER_TYPE_SAMPLES_PASSED,(uintptr_t)(storage+0x70000));
    CALL(nvnCommandBufferResetCounter,&cmd,NVN_COUNTER_TYPE_SAMPLES_PASSED);
    submit(end());
    CHECK(vk_zero_draw_skipped()-before==(step==0 || step==9 || step==12 || step==14?1:0));
    CHECK(vk_texture_zero_copy_skipped()-copies_before==(step==14?1:0));
    CALL(nvnTextureReadTexels,target[0],NULL,&region,pixels);
    if(step!=5) for(unsigned y=0;y<16;y++) for(unsigned x=0;x<16;x++) {
      int colored=step==2 || step==8 || step==11 || ((step==3 || step==13) && (x>=8 || y>=8));
      CHECK(!memcmp(pixels+(y*16+x)*4,colored?(uint8_t[]){255,0,0,255}:
          step==10?(uint8_t[]){0,0,0,255}:(uint8_t[]){0,0,0,0},4));
    }
    if(step==7) CHECK(*(uint64_t *)(storage+0x70000)==256);
    if(step==12) {
      NVNtextureView view;CALL(nvnTextureViewSetDefaults,&view);CALL(nvnTextureViewSetLevels,&view,1,1);
      NVNcopyRegion small={0,0,0,8,8,1};
      CALL(nvnTextureReadTexels,&textures[5],&view,&small,pixels);
      for(unsigned pixel=0;pixel<64;pixel++) CHECK(!memcmp(pixels+pixel*4,(uint8_t[]){255,0,0,255},4));
    }
  }
  vk_zero_draw_test_fact(NULL);vk_zero_draw_reference(0);vk_tbdr_reference(!saved_tbdr);
  S=saved;vk_pipeline_state_changed();
  CALL(nvnSamplerFinalize,&sampler);
  for(unsigned t=0;t<6;t++) CALL(nvnTextureFinalize,&textures[t]);
  CALL(nvnProgramFinalize,&program);CALL(nvnMemoryPoolFinalize,&pool);free(storage);
  LOG("[vulkan zero draw fixture] actual GPU pixels, full/partial clears/copies, finite coefficient, border sampler, live samples, RGB/RGBA, sampled mip coverage and unchanged other mips passed");
}
