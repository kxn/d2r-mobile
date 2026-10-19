// Owned fixture: switch the fragment module with the same NVN program and
// descriptor state. Verify the prepared pipeline and full PSO key both switch.
static void nofxaa_test(void) {
  uint8_t controls[2][0x880];memset(controls[0],37,sizeof controls[0]);memset(controls[1],38,sizeof controls[1]);
  NVNshaderData shaders[]={{.control=controls[0]},{.control=controls[1]}};
  NVNprogram program;CHECK(CALL(nvnProgramInitialize,&program,&device));
  CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  vk_nofxaa_test_fact(vk_shader_get(controls[1])->key);
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,131072));memset(storage,0,131072);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,131072);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtexture textures[2];
  for(unsigned i=0;i<2;i++) {
    NVNtextureBuilder b;CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_2D);
    CALL(nvnTextureBuilderSetWidth,&b,16);CALL(nvnTextureBuilderSetHeight,&b,16);
    CALL(nvnTextureBuilderSetFormat,&b,NVN_FORMAT_RGBA8);CALL(nvnTextureBuilderSetStorage,&b,&pool,i*65536);
    CHECK(CALL(nvnTextureInitialize,&textures[i],&b));
  }
  enum { descriptor=37 };mtl_set_tex_pool_n(64);mtl_reg_tex(descriptor,(const tex_t *)&textures[0],NULL);
  NVNsamplerBuilder sb;CALL(nvnSamplerBuilderSetDefaults,&sb);CALL(nvnSamplerBuilderSetDevice,&sb,&device);
  NVNsampler sampler;CHECK(CALL(nvnSamplerInitialize,&sampler,&sb));mtl_reg_samp(descriptor,&((const samp_t *)&sampler)->p);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);
  CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);
  CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);
  NVNchannelMaskState mask;CALL(nvnChannelMaskStateSetDefaults,&mask);
  const uint8_t colors[4][4]={{64,0,0,255},{0,128,0,255},{0,0,192,255},{64,128,192,255}};
  const float viewport[]={0,0,16,16},clear[]={1,0,1,1};uint8_t pixels[1024];
  const NVNtexture *target[]={&textures[1]},*source[]={&textures[0]};NVNcopyRegion whole={0,0,0,16,16,1};
  vk_state saved=S;
  for(unsigned step=0;step<4;step++) {
    int enabled=step&1;vk_nofxaa_enabled(enabled);
    uint64_t before;int active;vk_nofxaa_stats(&before,&active);
    CALL(nvnCommandBufferBeginRecording,&cmd);
    if(!step) CALL(nvnCommandBufferSetRenderTargets,&cmd,1,source,NULL,NULL,NULL);
    if(!step) for(unsigned quadrant=0;quadrant<4;quadrant++) {
      float c[4];
      for(unsigned k=0;k<4;k++)c[k]=colors[quadrant][k]/255.f;
      CALL(nvnCommandBufferSetScissor,&cmd,(quadrant&1)*8,(quadrant>>1)*8,8,8);
      CALL(nvnCommandBufferClearColor,&cmd,0,c,15);
    }
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,target,NULL,NULL,NULL);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);CALL(nvnCommandBufferClearColor,&cmd,0,clear,15);
    CALL(nvnCommandBufferSetViewports,&cmd,0,1,viewport);
    CALL(nvnCommandBufferBindDepthStencilState,&cmd,&ds);CALL(nvnCommandBufferBindPolygonState,&cmd,&polygon);
    CALL(nvnCommandBufferBindColorState,&cmd,&color);CALL(nvnCommandBufferBindChannelMaskState,&cmd,&mask);
    CALL(nvnCommandBufferBindVertexAttribState,&cmd,0,NULL);CALL(nvnCommandBufferBindVertexStreamState,&cmd,0,NULL);
    CALL(nvnCommandBufferBindProgram,&cmd,&program,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    NVNseparateTextureHandle th={.value=descriptor};NVNseparateSamplerHandle sh={.value=(uint32_t)descriptor<<20};
    CALL(nvnCommandBufferBindSeparateTextures,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&th);
    CALL(nvnCommandBufferBindSeparateSamplers,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&sh);
    CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);submit(end());
    uint64_t after;vk_nofxaa_stats(&after,&active);CHECK(active==enabled);CHECK(after-before==(unsigned)enabled);
    CALL(nvnTextureReadTexels,&textures[1],NULL,&whole,pixels);
    for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++)for(unsigned c=0;c<4;c++) {
      unsigned expected=colors[(y/8)*2+x/8][c];if(!enabled && c<3)expected/=2;
      if(pixels[(y*16+x)*4+c]!=expected)
        fprintf(stderr,"no-FXAA fixture step=%u xy=%u,%u channel=%u got=%u expected=%u\n",step,x,y,c,pixels[(y*16+x)*4+c],expected);
      CHECK(pixels[(y*16+x)*4+c]==expected);
    }
  }
  vk_nofxaa_test_fact(NULL);vk_nofxaa_enabled(0);S=saved;vk_pipeline_state_changed();
  CALL(nvnSamplerFinalize,&sampler);for(unsigned i=0;i<2;i++)CALL(nvnTextureFinalize,&textures[i]);
  CALL(nvnProgramFinalize,&program);CALL(nvnMemoryPoolFinalize,&pool);free(storage);
  LOG("[vulkan no-FXAA fixture] actual GPU quadrants and original UV/alpha/sampler off/on/off/on pipeline switch passed");
}
