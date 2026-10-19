// Owned NVN/GPU fixture for the original bloom-quarter rewrite. No game code
// or game shader executes. Test facts only map these four owned fragment keys.
static float bloom_unsigned_float(uint32_t value,unsigned mantissa) {
  unsigned exponent=value>>mantissa,fraction=value&((1u<<mantissa)-1);
  return exponent?ldexpf(1.f+ldexpf(fraction,-(int)mantissa),(int)exponent-15):
      ldexpf(fraction,1-15-(int)mantissa);
}
static float bloom_r11_component(uint32_t pixel,unsigned component) {
  return bloom_unsigned_float(component==0?pixel&2047:component==1?(pixel>>11)&2047:pixel>>22,component==2?5:6);
}
static void bloom_quarter_test(void) {
  uint8_t controls[7][0x880];NVNprogram programs[6];const char *keys[4];
  memset(controls[0],41,0x880);
  for(unsigned i=0;i<4;i++) {
    memset(controls[i+1],42+i,0x880);
    NVNshaderData shaders[]={{.control=controls[0]},{.control=controls[i+1]}};
    CHECK(CALL(nvnProgramInitialize,&programs[i],&device));
    CHECK(CALL(nvnProgramSetShaders,&programs[i],2,shaders));
    keys[i]=vk_shader_get(controls[i+1])->key;
  }
  for(unsigned i=4;i<6;i++) {
    memset(controls[i+1],i==4?46:2,0x880);
    NVNshaderData shader[2]={{.control=controls[0]},{.control=controls[i+1]}};
    CHECK(CALL(nvnProgramInitialize,&programs[i],&device));
    CHECK(CALL(nvnProgramSetShaders,&programs[i],i==4?2:1,i==4?shader:shader+1));
  }
  const char *nan_key=vk_shader_get(controls[5])->key;
  vk_bloom_quarter_test_facts(keys);
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,1<<21));memset(storage,0,1<<21);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,1<<21);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNcommandBuffer cmd;CHECK(CALL(nvnCommandBufferInitialize,&cmd,&device));
  CALL(nvnCommandBufferAddCommandMemory,&cmd,&pool,0x90000,0x170000);
  NVNtexturePool texture_pool;NVNsamplerPool sampler_pool;
  CHECK(CALL(nvnTexturePoolInitialize,&texture_pool,&pool,0x88000,64));
  CHECK(CALL(nvnSamplerPoolInitialize,&sampler_pool,&pool,0x89000,64));
  const unsigned widths[]={64,32,16,8,64,64};NVNtexture textures[6];
  for(unsigned i=0;i<6;i++) {
    NVNtextureBuilder builder;CALL(nvnTextureBuilderSetDefaults,&builder);CALL(nvnTextureBuilderSetDevice,&builder,&device);
    CALL(nvnTextureBuilderSetTarget,&builder,NVN_TEXTURE_TARGET_2D);
    CALL(nvnTextureBuilderSetWidth,&builder,widths[i]);CALL(nvnTextureBuilderSetHeight,&builder,widths[i]);
    CALL(nvnTextureBuilderSetLevels,&builder,i==0?2:1);
    CALL(nvnTextureBuilderSetFormat,&builder,NVN_FORMAT_RGBA8);CALL(nvnTextureBuilderSetStorage,&builder,&pool,i*65536);
    CHECK(CALL(nvnTextureInitialize,&textures[i],&builder));
    view_t base={.set_levels=1,.base_level=0,.nlevels=1};
    mtl_reg_tex(40+i,(const tex_t *)&textures[i],i==0?&base:NULL);
  }
  NVNtexture depth_texture;NVNtextureBuilder db;
  CALL(nvnTextureBuilderSetDefaults,&db);CALL(nvnTextureBuilderSetDevice,&db,&device);
  CALL(nvnTextureBuilderSetTarget,&db,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&db,64);CALL(nvnTextureBuilderSetHeight,&db,64);
  CALL(nvnTextureBuilderSetFormat,&db,NVN_FORMAT_DEPTH24_STENCIL8);CALL(nvnTextureBuilderSetStorage,&db,&pool,0x84000);
  CHECK(CALL(nvnTextureInitialize,&depth_texture,&db));
  NVNsamplerBuilder sb;CALL(nvnSamplerBuilderSetDefaults,&sb);CALL(nvnSamplerBuilderSetDevice,&sb,&device);
  CALL(nvnSamplerBuilderSetMinMagFilter,&sb,NVN_MIN_FILTER_LINEAR,NVN_MAG_FILTER_LINEAR);
  CALL(nvnSamplerBuilderSetWrapMode,&sb,NVN_WRAP_MODE_CLAMP_TO_EDGE,NVN_WRAP_MODE_CLAMP_TO_EDGE,NVN_WRAP_MODE_CLAMP_TO_EDGE);
  NVNsampler sampler;CHECK(CALL(nvnSamplerInitialize,&sampler,&sb));mtl_reg_samp(40,&((const samp_t *)&sampler)->p);
  float *vertices=(void *)(storage+0x60000),*threshold=(void *)(storage+0x70000);
  memcpy(vertices,(float[]){-1,-1,0,1, 3,-1,0,1, -1,3,0,1},48);
  NVNvertexAttribState attr;CALL(nvnVertexAttribStateSetDefaults,&attr);CALL(nvnVertexAttribStateSetStreamIndex,&attr,0);CALL(nvnVertexAttribStateSetFormat,&attr,NVN_FORMAT_RGBA32F,0);
  NVNvertexStreamState stream;CALL(nvnVertexStreamStateSetDefaults,&stream);CALL(nvnVertexStreamStateSetStride,&stream,16);
  NVNbufferRange range={(uintptr_t)vertices,48};
  NVNdepthStencilState depth;CALL(nvnDepthStencilStateSetDefaults,&depth);CALL(nvnDepthStencilStateSetDepthTestEnable,&depth,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&depth,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&depth,0);
  NVNdepthStencilState stencil_depth=depth;CALL(nvnDepthStencilStateSetStencilTestEnable,&stencil_depth,1);
  CALL(nvnDepthStencilStateSetStencilFunc,&stencil_depth,NVN_FACE_FRONT_AND_BACK,NVN_STENCIL_FUNC_EQUAL);
  NVNblendState additive;CALL(nvnBlendStateSetDefaults,&additive);CALL(nvnBlendStateSetBlendTarget,&additive,0);
  CALL(nvnBlendStateSetBlendFunc,&additive,NVN_BLEND_FUNC_CONSTANT_COLOR,NVN_BLEND_FUNC_ONE,NVN_BLEND_FUNC_ONE,NVN_BLEND_FUNC_ZERO);
  NVNcolorState additive_color;CALL(nvnColorStateSetDefaults,&additive_color);CALL(nvnColorStateSetBlendEnable,&additive_color,0,1);
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);NVNchannelMaskState mask;CALL(nvnChannelMaskStateSetDefaults,&mask);
  NVNmultisampleState multisample;CALL(nvnMultisampleStateSetDefaults,&multisample);
  const float quadrants[4][4]={{.8f,.1f,.1f,1},{.1f,.8f,.1f,1},{.1f,.1f,.8f,1},{.8f,.8f,.8f,1}};
  uint8_t baseline[2][64*64*4],pixels[64*64*4];vk_state saved=S;
  int saved_query;uint64_t elided;vk_sample_query_elision_stats(&elided,&saved_query);vk_sample_liveness_reference(0);
  for(unsigned test=0;test<29;test++)for(unsigned enabled=0;enabled<2;enabled++) {
    if(test>=20)for(unsigned i=1;i<=3;i++) {
      CALL(nvnTextureFinalize,&textures[i]);
      NVNtextureBuilder builder;CALL(nvnTextureBuilderSetDefaults,&builder);CALL(nvnTextureBuilderSetDevice,&builder,&device);
      CALL(nvnTextureBuilderSetTarget,&builder,NVN_TEXTURE_TARGET_2D);
      CALL(nvnTextureBuilderSetWidth,&builder,widths[i]);CALL(nvnTextureBuilderSetHeight,&builder,widths[i]);
      unsigned levels=(test==24&&i==2)||(test==25&&i==1)?2:1;
      CALL(nvnTextureBuilderSetLevels,&builder,levels);
      int format=test==22 || test>=27?NVN_FORMAT_RGBA8:test==23 || (test==26&&i==2)?NVN_FORMAT_RGBA16F:NVN_FORMAT_R11G11B10F;
      CALL(nvnTextureBuilderSetFormat,&builder,format);CALL(nvnTextureBuilderSetStorage,&builder,&pool,i*65536);
      CHECK(CALL(nvnTextureInitialize,&textures[i],&builder));
      view_t base={.set_levels=1,.base_level=0,.nlevels=1};
      mtl_reg_tex(40+i,(const tex_t *)&textures[i],levels>1?&base:NULL);
    }
    const char *test_keys[]={test==10?nan_key:keys[0],keys[1],keys[2],keys[3]};
    vk_bloom_quarter_test_facts(test_keys);
    vk_bloom_quarter_test_blit_format(test>=27?VK_FORMAT_R8G8B8A8_UNORM:VK_FORMAT_UNDEFINED);
    view_t source_view={.set_levels=1,.base_level=test==8?1:0,.nlevels=1,
      .set_format=test==9,.format=NVN_FORMAT_RGBA8_SRGB};
    mtl_reg_tex(40,(const tex_t *)&textures[0],&source_view);
    vk_bloom_quarter_enabled(enabled);dm_guest_write_prepare(threshold,16);memcpy(threshold,(float[]){test==5?NAN:.2f,0,0,0},16);
    uint64_t chains,skipped,replaced,redirected,fallbacks,clear_a,clear_b1,c_blits,c_query,c_resource;int active;
    vk_bloom_quarter_blit_stats(&c_blits,&c_query,&c_resource);
    vk_bloom_quarter_clears(&clear_a,&clear_b1);
    vk_bloom_quarter_stats(&chains,&skipped,&replaced,&redirected,&fallbacks,&active);
    CALL(nvnCommandBufferBeginRecording,&cmd);
    CALL(nvnCommandBufferBindDepthStencilState,&cmd,&depth);CALL(nvnCommandBufferBindPolygonState,&cmd,&polygon);
    CALL(nvnCommandBufferBindColorState,&cmd,&color);CALL(nvnCommandBufferBindChannelMaskState,&cmd,&mask);CALL(nvnCommandBufferBindMultisampleState,&cmd,&multisample);
    CALL(nvnCommandBufferBindVertexAttribState,&cmd,1,&attr);CALL(nvnCommandBufferBindVertexStreamState,&cmd,1,&stream);CALL(nvnCommandBufferBindVertexBuffers,&cmd,0,1,&range);
    CALL(nvnCommandBufferBindUniformBuffer,&cmd,NVN_SHADER_STAGE_FRAGMENT,4,(uintptr_t)threshold,16);
    NVNseparateSamplerHandle sh={.value=(uint32_t)40<<20};CALL(nvnCommandBufferBindSeparateSamplers,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&sh);
    for(unsigned t=0;t<6;t++) {
      const NVNtexture *target[]={&textures[t]};CALL(nvnCommandBufferSetRenderTargets,&cmd,1,target,NULL,NULL,NULL);
      CALL(nvnCommandBufferSetScissor,&cmd,0,0,widths[t],widths[t]);CALL(nvnCommandBufferClearColor,&cmd,0,(float[]){1,0,1,1},15);
    }
    if(test==14 || test==18) {
      CALL(nvnCommandBufferSetRenderTargets,&cmd,0,NULL,NULL,&depth_texture,NULL);
      CALL(nvnCommandBufferSetScissor,&cmd,0,0,64,64);CALL(nvnCommandBufferClearDepthStencil,&cmd,1,0,1,255);
      CALL(nvnCommandBufferSetScissor,&cmd,0,0,32,64);CALL(nvnCommandBufferClearDepthStencil,&cmd,1,0,0,255);
    }
    const NVNtexture *source[]={&textures[0]};CALL(nvnCommandBufferSetRenderTargets,&cmd,1,source,NULL,NULL,NULL);
    for(unsigned q=0;q<4;q++) {
      CALL(nvnCommandBufferSetScissor,&cmd,(q&1)*32,(q>>1)*32,32,32);CALL(nvnCommandBufferClearColor,&cmd,0,quadrants[q],15);
    }
    NVNtextureView low_view;CALL(nvnTextureViewSetDefaults,&low_view);CALL(nvnTextureViewSetLevels,&low_view,1,1);
    NVNcopyRegion low_region={0,0,0,32,32,1};CALL(nvnCommandBufferClearTexture,&cmd,&textures[0],&low_view,&low_region,(float[]){0,.75f,0,1},15);
    if(test==11) {
      dm_guest_write_prepare(storage+0x80000,256);memset(storage+0x80000,0,256);
      CALL(nvnCommandBufferBindProgram,&cmd,&programs[5],NVN_SHADER_STAGE_COMPUTE_BIT);
      CALL(nvnCommandBufferBindStorageBuffer,&cmd,NVN_SHADER_STAGE_COMPUTE,0,(uintptr_t)(storage+0x80000),256);
      CALL(nvnCommandBufferBindStorageBuffer,&cmd,NVN_SHADER_STAGE_COMPUTE,1,(uintptr_t)threshold,256);
      CALL(nvnCommandBufferDispatchCompute,&cmd,1,1,1); // Unknown pending GPU producer of A's threshold.
    }
    // A -> B1 -> B2 -> retained C(B1) -> retained C(B0) -> D.
    const unsigned targets[]={1,2,3,2,1,4},inputs[]={0,1,2,3,2,1},which[]={0,1,1,2,2,3};
    for(unsigned step=0;step<6;step++) {
      if(test==1 && step==5)continue; // Missing final consumer: A cannot be discarded.
      if(test==3 && step==1) {
        CALL(nvnCommandBufferSetRenderTargets,&cmd,1,source,NULL,NULL,NULL);CALL(nvnCommandBufferSetScissor,&cmd,0,0,64,64);
        CALL(nvnCommandBufferClearColor,&cmd,0,(float[]){0,0,0,1},15); // Changes S after A.
      }
      const NVNtexture *target[]={&textures[targets[step]]};unsigned w=widths[targets[step]];
      CALL(nvnCommandBufferSetRenderTargets,&cmd,1,target,NULL,(test==14||test==18)&&step==5?&depth_texture:NULL,NULL);
      CALL(nvnCommandBufferSetViewports,&cmd,0,1,(float[]){0,0,w,w});CALL(nvnCommandBufferSetScissor,&cmd,0,0,(test==4&&step==1)||(test==15&&step==4)?w/2:w,w);
      // Full-overwrite A/B1 clear jobs are deferred until a real consumer.
      // Prefix side effects must survive even though A itself is omitted.
      if(step<=1) {
        if(test==17 && !step) {
          const NVNtexture *mrt[]={target[0],&textures[5]};
          CALL(nvnCommandBufferSetRenderTargets,&cmd,2,mrt,NULL,NULL,NULL);
          CALL(nvnCommandBufferClearColor,&cmd,1,(float[]){0,.75f,0,1},15);
        }
        if(test==18 && !step) {
          CALL(nvnCommandBufferSetRenderTargets,&cmd,1,target,NULL,&depth_texture,NULL);
          CALL(nvnCommandBufferClearDepthStencil,&cmd,.25f,1,77,255);
        }
        CALL(nvnCommandBufferClearColor,&cmd,0,(float[]){1,0,1,1},15);
        if((test==17 || test==18) && !step)
          CALL(nvnCommandBufferSetRenderTargets,&cmd,1,target,NULL,NULL,NULL);
        if(test==16 && !step) {
          CALL(nvnCommandBufferBindProgram,&cmd,&programs[3],NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
          NVNseparateTextureHandle prefix={.value=40};
          CALL(nvnCommandBufferBindSeparateTextures,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&prefix);
          CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
        }
        if(test==19 && !step) {
          NVNcopyRegion copied={0,0,0,32,32,1};
          CALL(nvnCommandBufferCopyTextureToBuffer,&cmd,&textures[1],NULL,&copied,(uintptr_t)(storage+0x81000),0);
        }
      }
      CALL(nvnCommandBufferBindDepthStencilState,&cmd,(test==14||test==18)&&step==5?&stencil_depth:&depth);
      if((test==14||test==18)&&step==5) {
        CALL(nvnCommandBufferSetStencilRef,&cmd,NVN_FACE_FRONT_AND_BACK,test==18?77:1);
        CALL(nvnCommandBufferSetStencilValueMask,&cmd,NVN_FACE_FRONT_AND_BACK,test==18?255:1);CALL(nvnCommandBufferSetStencilMask,&cmd,NVN_FACE_FRONT_AND_BACK,0);
      }
      CALL(nvnCommandBufferBindColorState,&cmd,test==14&&step>=3?&additive_color:&color);
      if(test==14&&step>=3) {
        CALL(nvnCommandBufferBindBlendState,&cmd,&additive);CALL(nvnCommandBufferSetBlendColor,&cmd,(float[]){step==5?.2f:1,step==5?.2f:1,step==5?.2f:1,1});
      }
      CALL(nvnCommandBufferBindProgram,&cmd,&programs[test==10&&step==0?4:which[step]],NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
      NVNseparateTextureHandle handle={.value=40+inputs[step]};CALL(nvnCommandBufferBindSeparateTextures,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&handle);
      if(test==2 && step==1) {
        // Unexpected known-but-wrong stage inside the chain.
        CALL(nvnCommandBufferBindProgram,&cmd,&programs[3],NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
        CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
        CALL(nvnCommandBufferBindProgram,&cmd,&programs[test==10&&step==0?4:which[step]],NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
      }
      if(test==12 && step==1)CALL(nvnCommandBufferSetTexturePool,&cmd,&texture_pool);
      if(test==13 && step==1)CALL(nvnCommandBufferSetSamplerPool,&cmd,&sampler_pool);
      if(test==7 && step==1) {
        // Unknown producer/state side effect must abort planning, even when
        // it would happen to write the original same threshold this time.
        CALL(nvnCommandBufferUpdateUniformBuffer,&cmd,(uintptr_t)threshold,16,0,16,(float[]){.2f,0,0,0});
      }
      if(test==27 && step==4)CALL(nvnCommandBufferClearColor,&cmd,0,(float[]){1,0,1,1},15);
      if((test==21 || test==28) && step==4)CALL(nvnCommandBufferResetCounter,&cmd,NVN_COUNTER_TYPE_SAMPLES_PASSED);
      CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    }
    // An additional D read is intentionally outside the rewrite action table.
    // It must see valid B0 from retained C, even in another submission below.
    if(test==6 || test==21 || test==28)CALL(nvnCommandBufferReportCounter,&cmd,NVN_COUNTER_TYPE_SAMPLES_PASSED,(uintptr_t)(storage+0x80000));
    CALL(nvnCommandBufferResetCounter,&cmd,NVN_COUNTER_TYPE_SAMPLES_PASSED);submit(CALL(nvnCommandBufferEndRecording,&cmd));
    uint64_t after_chains,after_skipped,after_replaced,after_redirected,after_fallbacks;
    vk_bloom_quarter_stats(&after_chains,&after_skipped,&after_replaced,&after_redirected,&after_fallbacks,&active);
    unsigned valid=test==0 || test==14 || test>=16;
    unsigned expected=enabled && valid;
    uint64_t after_clear_a,after_clear_b1;vk_bloom_quarter_clears(&after_clear_a,&after_clear_b1);
    CHECK(after_clear_a-clear_a==(expected && (test<16 || test>=20)));
    CHECK(after_clear_b1-clear_b1==expected);
    VkFormatProperties r11_features;vk_format_properties(VK_FORMAT_B10G11R11_UFLOAT_PACK32,&r11_features);
    VkFormatFeatureFlags blit_required=VK_FORMAT_FEATURE_BLIT_SRC_BIT|VK_FORMAT_FEATURE_BLIT_DST_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    int r11_blit=(r11_features.optimalTilingFeatures&blit_required)==blit_required;
    unsigned expected_blit=expected && ((test==20 && r11_blit) || test==27);
    unsigned expected_query=expected && (test==21 || test==28);
    unsigned expected_resource=expected && !expected_blit && !expected_query;
    uint64_t after_c_blits,after_c_query,after_c_resource;
    vk_bloom_quarter_blit_stats(&after_c_blits,&after_c_query,&after_c_resource);
    if(after_c_blits-c_blits!=expected_blit || after_c_query-c_query!=expected_query ||
        after_c_resource-c_resource!=expected_resource)
      fprintf(stderr,"bloom C test=%u enabled=%u chains=%llu blits=%llu query=%llu resource=%llu\n",test,enabled,
          (unsigned long long)(after_chains-chains),(unsigned long long)(after_c_blits-c_blits),
          (unsigned long long)(after_c_query-c_query),(unsigned long long)(after_c_resource-c_resource));
    CHECK(after_c_blits-c_blits==expected_blit);
    CHECK(after_c_query-c_query==expected_query);
    CHECK(after_c_resource-c_resource==expected_resource);
    if(test==21 || test==28)CHECK(*(uint64_t *)(storage+0x80000)==5120);
    if(expected_blit) {
      // Independent CPU oracle for standard full-image linear-clamp upscale.
      // This verifies the explicit approximation, not equality to four-tap C.
      uint32_t small[16*16],large[32*32];NVNcopyRegion sr={0,0,0,16,16,1},dr={0,0,0,32,32,1};
      CALL(nvnTextureReadTexels,&textures[2],NULL,&sr,small);CALL(nvnTextureReadTexels,&textures[1],NULL,&dr,large);
      for(unsigned y=0;y<32;y++)for(unsigned x=0;x<32;x++)for(unsigned c=0;c<3;c++) {
        float u=(x+.5f)*.5f-.5f,v=(y+.5f)*.5f-.5f;int ix=floorf(u),iy=floorf(v);float fx=u-ix,fy=v-iy,wanted=0;
        for(unsigned dy=0;dy<2;dy++)for(unsigned dx=0;dx<2;dx++) {
          int sx=ix+dx,sy=iy+dy;sx=sx<0?0:sx>15?15:sx;sy=sy<0?0:sy>15?15:sy;
          wanted+=(test==20?bloom_r11_component(small[sy*16+sx],c):((small[sy*16+sx]>>(c*8))&255)/255.f)*(dx?fx:1-fx)*(dy?fy:1-fy);
        }
        CHECK(fabsf((test==20?bloom_r11_component(large[y*32+x],c):((large[y*32+x]>>(c*8))&255)/255.f)-wanted)<.02f);
      }
    }
    if(test==17) {
      NVNcopyRegion sibling={0,0,0,32,32,1};uint8_t sibling_pixels[32*32*4];
      CALL(nvnTextureReadTexels,&textures[5],NULL,&sibling,sibling_pixels);
      for(unsigned k=0;k<32*32;k++)for(unsigned c=0;c<4;c++)
        CHECK(abs((int)sibling_pixels[k*4+c]-(c==1?191:c==3?255:0))<=1);
    }
    if(test==19)for(unsigned k=0;k<32*32;k++)for(unsigned c=0;c<4;c++)
      CHECK(storage[0x81000+k*4+c]==(c==1?0:255));
    if(after_chains-chains!=expected)fprintf(stderr,"bloom fixture test=%u enabled=%u chains=%llu expected=%u fallbacks=%llu\n",test,enabled,(unsigned long long)(after_chains-chains),expected,(unsigned long long)(after_fallbacks-fallbacks));
    CHECK(after_chains-chains==expected);CHECK(after_skipped-skipped==expected);CHECK(after_replaced-replaced==expected);CHECK(after_redirected-redirected==expected);
    // For additive C + stencil D, derive the expected optimized visual from
    // the already-generated B1, independently of the backend rewrite. Metal
    // feeds this B1 directly to D, without the final additive A+C B0 stage.
    uint8_t visual_b1[16*16*4];NVNcopyRegion b1_whole={0,0,0,16,16,1};
    if(test==14)CALL(nvnTextureReadTexels,&textures[2],NULL,&b1_whole,visual_b1);
    // Later-submission unknown consumer uses B0, without a cross-frame map.
    CALL(nvnCommandBufferBeginRecording,&cmd);
    const NVNtexture *unknown_target[]={&textures[5]};CALL(nvnCommandBufferSetRenderTargets,&cmd,1,unknown_target,NULL,NULL,NULL);
    CALL(nvnCommandBufferSetViewports,&cmd,0,1,(float[]){0,0,64,64});CALL(nvnCommandBufferSetScissor,&cmd,0,0,64,64);
    CALL(nvnCommandBufferBindDepthStencilState,&cmd,&depth);CALL(nvnCommandBufferBindColorState,&cmd,&color);
    CALL(nvnCommandBufferBindProgram,&cmd,&programs[3],NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    NVNseparateTextureHandle unknown_handle={.value=41};CALL(nvnCommandBufferBindSeparateTextures,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&unknown_handle);
    CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);submit(CALL(nvnCommandBufferEndRecording,&cmd));
    NVNcopyRegion whole={0,0,0,64,64,1};
    for(unsigned t=0;t<2;t++) {
      CALL(nvnTextureReadTexels,&textures[4+t],NULL,&whole,pixels);
      if(test==18 && !t) {
        // Ref 77 was written only by the pre-A depth/stencil clear. A later
        // stencil-tested D must accept its 32x32 region and reject siblings.
        CHECK(pixels[(16*64+16)*4+1]>0 && pixels[(16*64+16)*4+1]<128);
        CHECK(pixels[(16*64+48)*4]==255 && pixels[(16*64+48)*4+1]==0);
      }
      if(!enabled)memcpy(baseline[t],pixels,sizeof pixels);
      else if(!valid)CHECK(!memcmp(baseline[t],pixels,sizeof pixels));
      else if(test==14) {
        for(unsigned q=0;q<4;q++)for(unsigned c=0;c<4;c++) {
          unsigned x=(q&1)*32+16,y=(q>>1)*32+16,at=(y*64+x)*4+c;
          unsigned b1_at=(((q>>1)*8+4)*16+(q&1)*8+4)*4+c;
          int wanted=visual_b1[b1_at];
          if(t==0 && !(q&1))wanted=c==1?0:255; // Stencil rejects the left half.
          else if(t==0 && c<3)wanted=fmin(255,(c==1?0:255)+wanted*.2f);
          CHECK(abs((int)pixels[at]-wanted)<=3);
        }
      }
      else for(unsigned q=0;q<4;q++)for(unsigned c=0;c<4;c++) {
        unsigned x=(q&1)*32+16,y=(q>>1)*32+16,at=(y*64+x)*4+c;
        CHECK(abs((int)pixels[at]-(int)baseline[t][at])<=(test>=20?5:3));
      }
    }
  }
  vk_bloom_quarter_enabled(0);vk_bloom_quarter_test_facts(NULL);vk_sample_liveness_reference(saved_query);S=saved;vk_pipeline_state_changed();
  CALL(nvnTextureFinalize,&depth_texture);CALL(nvnSamplerFinalize,&sampler);for(unsigned i=0;i<6;i++)CALL(nvnTextureFinalize,&textures[i]);
  for(unsigned i=0;i<6;i++)CALL(nvnProgramFinalize,&programs[i]);CALL(nvnCommandBufferFinalize,&cmd);CALL(nvnTexturePoolFinalize,&texture_pool);CALL(nvnSamplerPoolFinalize,&sampler_pool);CALL(nvnMemoryPoolFinalize,&pool);free(storage);
  LOG("[vulkan bloom-quarter fixture] GPU A+B1 replacement, original C retention, D redirect, cross-submission B0 reader, additive C/stencil D visual, dead-clear cancellation with draw/MRT/depth/encoded guards, C linear-blit approximation and live-query/format/alpha/mip fallbacks, 14 conservative chain fallback cases (including GPU-produced threshold, mip/format view and non-finite coefficient) passed");
}
