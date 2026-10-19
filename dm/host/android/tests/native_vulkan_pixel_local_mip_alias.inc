// Actual producer sampling, independent color/history pixels, and physical
// pass counts. Negative cases execute the original conservative fallback.
static int pixel_local_mip_alias_test(void) {
  const char *fault=getenv("DM_TEST_PIXEL_LOCAL_PSO_FAILURE");
  int inject=fault && !strcmp(fault,"1");
  vk_state saved=S;uint8_t *storage;
  CHECK(!posix_memalign((void **)&storage,65536,2<<20));memset(storage,0,2<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,2<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtexture textures[6];for(unsigned k=0;k<6;k++) {
    NVNtextureBuilder b;CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&b,16);
    CALL(nvnTextureBuilderSetHeight,&b,16);CALL(nvnTextureBuilderSetLevels,&b,k==4?1:2);
    CALL(nvnTextureBuilderSetFormat,&b,(k==0 || k==5)?NVN_FORMAT_RGBA16F:k==2?NVN_FORMAT_R11G11B10F:NVN_FORMAT_RGBA8);
    // Texture5 is a separately initialized logical identity at exactly the
    // same guest bytes as texture0. It must never gain the same-image proof.
    CALL(nvnTextureBuilderSetStorage,&b,&pool,(k==5?0:k)*65536);
    CHECK(CALL(nvnTextureInitialize,&textures[k],&b));
  }
  NVNprogram producer,consumer;uint8_t controls[3][0x880];
  memset(controls[0],180,sizeof controls[0]);memset(controls[1],193,sizeof controls[1]);memset(controls[2],194,sizeof controls[2]);
  NVNshaderData shaders[]={{.control=controls[0]},{.control=controls[1]}};
  CHECK(CALL(nvnProgramInitialize,&producer,&device));CHECK(CALL(nvnProgramSetShaders,&producer,2,shaders));
  shaders[1].control=controls[2];CHECK(CALL(nvnProgramInitialize,&consumer,&device));CHECK(CALL(nvnProgramSetShaders,&consumer,2,shaders));
  NVNcommandBuffer local;CHECK(CALL(nvnCommandBufferInitialize,&local,&device));CALL(nvnCommandBufferAddCommandMemory,&local,&pool,1<<20,1<<20);
  NVNvertexAttribState attrs[2];for(unsigned k=0;k<2;k++)CALL(nvnVertexAttribStateSetDefaults,&attrs[k]);
  CALL(nvnVertexAttribStateSetFormat,&attrs[0],NVN_FORMAT_RGB32F,0);CALL(nvnVertexAttribStateSetFormat,&attrs[1],NVN_FORMAT_RG32F,12);
  NVNvertexStreamState stream;CALL(nvnVertexStreamStateSetDefaults,&stream);CALL(nvnVertexStreamStateSetStride,&stream,20);
  float *vertices=(void *)(storage+0x80000);memcpy(vertices,(float[]){-1,-1,0,0,1,3,-1,0,2,1,-1,3,0,0,-1},60);
  NVNbufferRange vb={(uintptr_t)vertices,60};float viewport[]={0,0,16,16};
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);
  CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNcolorState cs;CALL(nvnColorStateSetDefaults,&cs);NVNchannelMaskState mask;CALL(nvnChannelMaskStateSetDefaults,&mask);
  const NVNtexture *source[]={&textures[0],&textures[1],&textures[2]},*output[]={&textures[4]};
  NVNtextureView attachment_view;CALL(nvnTextureViewSetDefaults,&attachment_view);CALL(nvnTextureViewSetLevels,&attachment_view,0,1);
  const NVNtextureView *source_views[]={&attachment_view,&attachment_view,&attachment_view};
  view_t mip0={.set_levels=1,.base_level=0,.nlevels=1},mip1={.set_levels=1,.base_level=1,.nlevels=1};
  view_t both={.set_levels=1,.base_level=0,.nlevels=2},remaining={.set_levels=1,.base_level=0,.nlevels=0};
  mtl_set_tex_pool_n(512);samp_par sampler={.minf=1,.magf=1,.aniso=1,.lod_max=1000};mtl_reg_samp(103,&sampler);
  NVNseparateSamplerHandle sh=CALL(nvnDeviceGetSeparateSamplerHandle,&device,103);
  NVNseparateTextureHandle handles[4];for(unsigned k=0;k<4;k++)handles[k]=CALL(nvnDeviceGetSeparateTextureHandle,&device,96+k);
  NVNseparateTextureHandle read_handle=CALL(nvnDeviceGetSeparateTextureHandle,&device,100);
  mtl_sub_begin();for(unsigned k=0;k<4;k++) {
    vk_pixel_local_image token;CHECK(vk_texture_pixel_local_prepare((const tex_t *)&textures[k],k!=3,&token));
    CHECK(token.usage&VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);
  }
  vk_texture_prepare_attachment((const tex_t *)&textures[4],NULL);
  vk_internal_wait(vk_internal_commit());
  const float blue[]={0,0,1,1},history[]={16/255.f,16/255.f,16/255.f,1},black[]={0,0,0,0};
  for(unsigned test=0;test<9;test++) {
    int external=test>=7;
    const view_t *read_view=test==1?&mip0:test==2?&both:test==3?NULL:test==4?&remaining:&mip1;
    unsigned read_texture=test==6?5:external?3:0;
    mtl_reg_tex(100,(const tex_t *)&textures[read_texture],read_view);
    for(unsigned k=0;k<4;k++)mtl_reg_tex(96+k,(const tex_t *)&textures[k],k==3 && test==7?&mip0:NULL);
    CALL(nvnCommandBufferBeginRecording,&local);
    for(unsigned k=0;k<6;k++) {
      NVNcopyRegion r={0,0,0,16,16,1};CALL(nvnCommandBufferClearTexture,&local,&textures[k],NULL,&r,k==3?history:blue,15);
      if(k==4)continue;
      NVNtextureView v;CALL(nvnTextureViewSetDefaults,&v);CALL(nvnTextureViewSetLevels,&v,1,1);r.width=r.height=8;
      CALL(nvnCommandBufferClearTexture,&local,&textures[k],&v,&r,blue,15);
    }submit(CALL(nvnCommandBufferEndRecording,&local));
    uint64_t before_plans,before_draws,before_aborts;vk_pixel_local_stats(&before_plans,&before_draws,&before_aborts);
    uint64_t passes=V.passes;
    CALL(nvnCommandBufferBeginRecording,&local);
    CALL(nvnCommandBufferSetViewports,&local,0,1,viewport);CALL(nvnCommandBufferSetScissor,&local,0,0,16,16);
    CALL(nvnCommandBufferBindPolygonState,&local,&polygon);CALL(nvnCommandBufferBindDepthStencilState,&local,&ds);
    CALL(nvnCommandBufferBindColorState,&local,&cs);CALL(nvnCommandBufferBindChannelMaskState,&local,&mask);
    CALL(nvnCommandBufferBindVertexAttribState,&local,2,attrs);CALL(nvnCommandBufferBindVertexStreamState,&local,1,&stream);CALL(nvnCommandBufferBindVertexBuffers,&local,0,1,&vb);
    CALL(nvnCommandBufferSetRenderTargets,&local,3,source,test==5?NULL:source_views,NULL,NULL);
    CALL(nvnCommandBufferBindProgram,&local,&producer,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferBindSeparateTextures,&local,NVN_SHADER_STAGE_FRAGMENT,0,1,&read_handle);
    CALL(nvnCommandBufferBindSeparateSamplers,&local,NVN_SHADER_STAGE_FRAGMENT,0,1,&sh);
    CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    CALL(nvnCommandBufferSetRenderTargets,&local,1,output,NULL,NULL,NULL);
    CALL(nvnCommandBufferBarrier,&local,NVN_BARRIER_ORDER_PRIMITIVES_BIT|NVN_BARRIER_ORDER_FRAGMENTS_BIT|NVN_BARRIER_INVALIDATE_TEXTURE_BIT);
    CALL(nvnCommandBufferClearColor,&local,0,black,15);
    CALL(nvnCommandBufferBindProgram,&local,&consumer,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferBindSeparateTextures,&local,NVN_SHADER_STAGE_FRAGMENT,0,4,handles);
    CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    // The enclosing original fixture deliberately leaves a sample span live.
    // Overwrite its unreported result after these draws, exactly as its
    // existing positive cases do; no live query spans the tested boundary.
    CALL(nvnCommandBufferResetCounter,&local,NVN_COUNTER_TYPE_SAMPLES_PASSED);
    if(inject && !test) {
      CHECK(!pixel_local_create_original);pixel_local_create_original=vkCreateGraphicsPipelines;
      CHECK(pixel_local_create_original!=NULL);pixel_local_create_failures=1;
      vkCreateGraphicsPipelines=pixel_local_create_fail_subpass1;
    }
    submit(CALL(nvnCommandBufferEndRecording,&local));
    if(inject && !test) {
      vkCreateGraphicsPipelines=pixel_local_create_original;pixel_local_create_original=NULL;
      CHECK(pixel_local_create_failures==0);
      int operation=vk_pool_operation_begin_ranges(NULL,0);CHECK(operation==1);vk_pool_operation_end(operation);
    }
    uint64_t plans,draws,aborts;vk_pixel_local_stats(&plans,&draws,&aborts);
    int failed=inject && !test;
    // External history is now ordinary sampled, not an attachment; its
    // default view may safely coexist with the producer reading history mip1.
    // Local feedback/default/alias negatives0..6 are unchanged.
    int eligible=vk_pixel_local_enabled() && (test==0 || test==7 || test==8);
    int fused=eligible && !failed;
    LOG("[pixel-local mip alias fixture] case=%u enabled=%d fused=%d passes=%llu",test,vk_pixel_local_enabled(),fused,(unsigned long long)(V.passes-passes));
    CHECK(V.passes-passes==(fused?1:2));CHECK(aborts-before_aborts==(unsigned)failed);
    CHECK(plans-before_plans==(unsigned)eligible);CHECK(draws-before_draws==(unsigned)fused);
    NVNcopyRegion region={0,0,0,16,16,1};uint8_t pixels[1024];CALL(nvnTextureReadTexels,&textures[4],NULL,&region,pixels);
    for(unsigned i=0;i<256;i++)for(unsigned c=0;c<4;c++)CHECK(abs((int)pixels[4*i+c]-(c==3?255:80))<=1);
    // Source STORE and sibling contents remain visible after the fused stream.
    uint16_t half[1024];CALL(nvnTextureReadTexels,&textures[0],NULL,&region,half);
    for(unsigned i=0;i<256;i++) {CHECK(half[4*i]==0x3404);CHECK(half[4*i+1]==0);CHECK(half[4*i+2]==0);CHECK(half[4*i+3]==0x3c00);}
    NVNtextureView v;CALL(nvnTextureViewSetDefaults,&v);CALL(nvnTextureViewSetLevels,&v,1,1);NVNcopyRegion small={0,0,0,8,8,1};
    CALL(nvnTextureReadTexels,&textures[0],&v,&small,half);
    for(unsigned i=0;i<64;i++) {CHECK(half[4*i]==0);CHECK(half[4*i+1]==0);CHECK(half[4*i+2]==0x3c00);CHECK(half[4*i+3]==0x3c00);}
    CALL(nvnTextureReadTexels,&textures[1],NULL,&region,pixels);
    for(unsigned i=0;i<256;i++)for(unsigned c=0;c<4;c++)CHECK(pixels[4*i+c]==(c==1?64:c==3?255:0));
    uint32_t packed[256];CALL(nvnTextureReadTexels,&textures[2],NULL,&region,packed);
    for(unsigned i=0;i<256;i++) {CHECK(!(packed[i]&0x3fffff));unsigned b=packed[i]>>22,e=b>>5,m=b&31;CHECK(e && e<31);CHECK(fabs(ldexp(1.+m/32.,(int)e-15)-64./255.)<.004);}
    CALL(nvnTextureReadTexels,&textures[3],NULL,&region,pixels);
    for(unsigned i=0;i<256;i++)for(unsigned c=0;c<4;c++)CHECK(pixels[4*i+c]==(c==3?255:16));
    CALL(nvnTextureReadTexels,&textures[3],&v,&small,pixels);
    for(unsigned i=0;i<64;i++)for(unsigned c=0;c<4;c++)CHECK(pixels[4*i+c]==(c>=2?255:0));
  }
  for(unsigned k=0;k<6;k++)CALL(nvnTextureFinalize,&textures[k]);
  CALL(nvnProgramFinalize,&producer);CALL(nvnProgramFinalize,&consumer);CALL(nvnCommandBufferFinalize,&local);CALL(nvnMemoryPoolFinalize,&pool);
  free(storage);S=saved;vk_pipeline_state_changed();CHECK(vk_validation_error_count()==0);return 0;
}
