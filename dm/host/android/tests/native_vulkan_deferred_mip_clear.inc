// Default-off generic transaction: original mip0 draw, two real dual-color
// mip clear groups, then an original-target draw. Independent output and both
// DS aspects are verified; consumers and unsupported boundaries retain drains.
static void deferred_mip_clear_test(const void *vertex,const void *fragment,
                                    const void *storage_shader,const void *sampled,
                                    const void *compute) {
  enum { bytes=4<<20 };
  vk_state saved=S;int old_tbdr=vk_tbdr_enabled(),old_layout,old_enabled,query_reference;
  int old_barrier=vk_submission_barrier_mode(),old_binding_reference;
  uint64_t a,b,c,d,transitions,safe,fallback,elided;
  vk_mip_clear_stats(&a,&b,&c,&d,&old_enabled);
  vk_texture_binding_stats(&a,&b,&c,&old_binding_reference);
  vk_texture_binding_reference(0);
  vk_image_layout_stats(&transitions,&safe,&fallback,&old_layout);
  vk_sample_query_elision_stats(&elided,&query_reference);
  vk_tbdr_reference(0);vk_image_layout_reference(0);vk_sample_liveness_reference(0);
  vk_submission_barrier_reference(0);
  uint8_t *data;CHECK(!posix_memalign((void **)&data,65536,bytes));memset(data,0,bytes);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,data,bytes);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNcommandBuffer local;CHECK(CALL(nvnCommandBufferInitialize,&local,&device));
  CALL(nvnCommandBufferAddCommandMemory,&local,&pool,1<<20,3<<20);
  NVNtexture texture[8];
  for(unsigned t=0;t<4;t++) {
    NVNtextureBuilder tb;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
    CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);
    CALL(nvnTextureBuilderSetWidth,&tb,16);CALL(nvnTextureBuilderSetHeight,&tb,16);
    CALL(nvnTextureBuilderSetLevels,&tb,t==2?1:3);
    CALL(nvnTextureBuilderSetFormat,&tb,t==2?NVN_FORMAT_DEPTH24_STENCIL8:NVN_FORMAT_RGBA8);
    CALL(nvnTextureBuilderSetStorage,&tb,&pool,t==3?0:t*65536);
    CHECK(CALL(nvnTextureInitialize,&texture[t],&tb));
  }
  NVNtextureView view[3];
  for(unsigned l=0;l<3;l++) { CALL(nvnTextureViewSetDefaults,&view[l]);CALL(nvnTextureViewSetLevels,&view[l],l,1); }
  // A distinct ordinary single-mip descriptor whose guest range starts at
  // the original texture's actual mip1 storage. It qualifies for the logical
  // preflight shortcut, unlike the existing multi-mip alias descriptor.
  ptrdiff_t alias_offset=CALL(nvnTextureGetViewOffset,&texture[0],&view[1]);
  CHECK(alias_offset>0 && (uint64_t)alias_offset<((tex_t *)&texture[0])->L.size);
  NVNtextureBuilder alias_builder;CALL(nvnTextureBuilderSetDefaults,&alias_builder);
  CALL(nvnTextureBuilderSetDevice,&alias_builder,&device);
  CALL(nvnTextureBuilderSetTarget,&alias_builder,NVN_TEXTURE_TARGET_2D);
  CALL(nvnTextureBuilderSetWidth,&alias_builder,8);CALL(nvnTextureBuilderSetHeight,&alias_builder,8);
  CALL(nvnTextureBuilderSetLevels,&alias_builder,1);CALL(nvnTextureBuilderSetFormat,&alias_builder,NVN_FORMAT_RGBA8);
  CALL(nvnTextureBuilderSetStorage,&alias_builder,&pool,alias_offset);
  CHECK(CALL(nvnTextureInitialize,&texture[4],&alias_builder));
  for(unsigned t=5;t<7;t++) {
    NVNtextureBuilder db;CALL(nvnTextureBuilderSetDefaults,&db);CALL(nvnTextureBuilderSetDevice,&db,&device);
    CALL(nvnTextureBuilderSetTarget,&db,NVN_TEXTURE_TARGET_2D);
    CALL(nvnTextureBuilderSetWidth,&db,16>>(t-4));CALL(nvnTextureBuilderSetHeight,&db,16>>(t-4));
    CALL(nvnTextureBuilderSetLevels,&db,1);CALL(nvnTextureBuilderSetFormat,&db,NVN_FORMAT_DEPTH24_STENCIL8);
    CALL(nvnTextureBuilderSetStorage,&db,&pool,(t+2)*65536);
    CHECK(CALL(nvnTextureInitialize,&texture[t],&db));
  }
  // A separately created VkImage alias of the second color allocation. The
  // original alias above covers color0; the batch must also prove color1.
  NVNtextureBuilder second_alias;CALL(nvnTextureBuilderSetDefaults,&second_alias);
  CALL(nvnTextureBuilderSetDevice,&second_alias,&device);
  CALL(nvnTextureBuilderSetTarget,&second_alias,NVN_TEXTURE_TARGET_2D);
  CALL(nvnTextureBuilderSetWidth,&second_alias,16);CALL(nvnTextureBuilderSetHeight,&second_alias,16);
  CALL(nvnTextureBuilderSetLevels,&second_alias,3);CALL(nvnTextureBuilderSetFormat,&second_alias,NVN_FORMAT_RGBA8);
  CALL(nvnTextureBuilderSetStorage,&second_alias,&pool,65536);
  CHECK(CALL(nvnTextureInitialize,&texture[7],&second_alias));
  NVNtexturePool tp;NVNsamplerPool sp;
  CHECK(CALL(nvnTexturePoolInitialize,&tp,&pool,0x50000,32));
  CHECK(CALL(nvnSamplerPoolInitialize,&sp,&pool,0x60000,32));
  CALL(nvnTexturePoolRegisterTexture,&tp,1,&texture[0],&view[1]);
  CALL(nvnTexturePoolRegisterTexture,&tp,2,&texture[3],&view[1]);
  CALL(nvnTexturePoolRegisterTexture,&tp,3,&texture[3],&view[0]);
  CALL(nvnTexturePoolRegisterTexture,&tp,4,&texture[4],NULL);
  CALL(nvnTexturePoolRegisterTexture,&tp,5,&texture[0],&view[2]);
  CALL(nvnTexturePoolRegisterTexture,&tp,6,&texture[1],&view[2]);
  CALL(nvnTexturePoolRegisterTexture,&tp,7,&texture[7],&view[0]);
  NVNsamplerBuilder sb;NVNsampler sampler;
  CALL(nvnSamplerBuilderSetDefaults,&sb);CALL(nvnSamplerBuilderSetDevice,&sb,&device);
  CHECK(CALL(nvnSamplerInitialize,&sampler,&sb));CALL(nvnSamplerPoolRegisterSampler,&sp,1,&sampler);
  NVNseparateSamplerHandle sh=CALL(nvnDeviceGetSeparateSamplerHandle,&device,1);
  uint8_t image_control[0x880];memset(image_control,8,sizeof image_control);
  NVNprogram program[5];
  const void *fs[]={fragment,storage_shader,sampled};
  for(unsigned n=0;n<5;n++) {
    NVNshaderData shaders[]={{.control=n==3?compute:vertex},{.control=n<3?fs[n]:n==4?image_control:NULL}};
    CHECK(CALL(nvnProgramInitialize,&program[n],&device));CHECK(CALL(nvnProgramSetShaders,&program[n],n==3?1:2,shaders));
  }
  NVNpolygonState poly;CALL(nvnPolygonStateSetDefaults,&poly);CALL(nvnPolygonStateSetCullFace,&poly,NVN_FACE_NONE);
  NVNdepthStencilState writer,reader;CALL(nvnDepthStencilStateSetDefaults,&writer);
  CALL(nvnDepthStencilStateSetDepthTestEnable,&writer,1);CALL(nvnDepthStencilStateSetDepthWriteEnable,&writer,1);
  CALL(nvnDepthStencilStateSetDepthFunc,&writer,NVN_DEPTH_FUNC_ALWAYS);
  CALL(nvnDepthStencilStateSetStencilTestEnable,&writer,0);reader=writer;
  CALL(nvnDepthStencilStateSetDepthWriteEnable,&reader,0);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);
  NVNchannelMaskState mask;CALL(nvnChannelMaskStateSetDefaults,&mask);
  CALL(nvnChannelMaskStateSetChannelMask,&mask,1,0,0,0,0);
  NVNmultisampleState ms;CALL(nvnMultisampleStateSetDefaults,&ms);
  NVNeventBuilder eb;NVNevent event;
  CALL(nvnEventBuilderSetDefaults,&eb);CALL(nvnEventBuilderSetStorage,&eb,&pool,0x43000);
  CHECK(CALL(nvnEventInitialize,&event,&eb));
  const float red[]={1,0,0,1},green[]={0,1,0,1},blue[]={0,0,1,1},yellow[]={1,1,0,1};
  const float cyan[]={0,1,1,1},magenta[]={1,0,1,1},white[]={1,1,1,1};
  memcpy(data+0x30000,red,16);memcpy(data+0x30100,white,16);
  uint32_t *src=(void *)(data+0x40000),*dst=(void *)(data+0x41000);
  for(unsigned k=0;k<64;k++)src[k]=k+1;
  const NVNtexture *colors[]={&texture[0],&texture[1]};
  const NVNtextureView *baseviews[]={&view[0],&view[0]};
  for(unsigned enabled=0;enabled<2;enabled++)for(unsigned test=0;test<28;test++) {
    vk_mip_clear_enable(0);
    CALL(nvnEventSignal,&event,NVN_EVENT_SIGNAL_MODE_WRITE,0);
    CALL(nvnCommandBufferBeginRecording,&local);
    for(unsigned l=0;l<3;l++) {
      const NVNtextureView *views[]={&view[l],&view[l]};
      CALL(nvnCommandBufferSetRenderTargets,&local,2,colors,views,NULL,NULL);
      CALL(nvnCommandBufferSetScissor,&local,0,0,16>>l,16>>l);
      CALL(nvnCommandBufferClearColor,&local,0,red,15);CALL(nvnCommandBufferClearColor,&local,1,green,15);
    }
    for(unsigned l=0;l<3;l++) {
      const NVNtexture *alias_targets[]={&texture[3]};const NVNtextureView *av[]={&view[l]};
      CALL(nvnCommandBufferSetRenderTargets,&local,1,alias_targets,av,NULL,NULL);
      CALL(nvnCommandBufferSetScissor,&local,0,0,16>>l,16>>l);
      CALL(nvnCommandBufferClearColor,&local,0,red,15);
    }
    for(unsigned l=0;l<3;l++) {
      const NVNtexture *alias_targets[]={&texture[7]};const NVNtextureView *av[]={&view[l]};
      CALL(nvnCommandBufferSetRenderTargets,&local,1,alias_targets,av,NULL,NULL);
      CALL(nvnCommandBufferSetScissor,&local,0,0,16>>l,16>>l);
      CALL(nvnCommandBufferClearColor,&local,0,green,15);
    }
    if(test==16) {
      const NVNtexture *ordinary_alias[]={&texture[4]};
      CALL(nvnCommandBufferSetRenderTargets,&local,1,ordinary_alias,NULL,NULL,NULL);
      CALL(nvnCommandBufferSetScissor,&local,0,0,8,8);
      CALL(nvnCommandBufferClearColor,&local,0,red,15);
    }
    if(test>=17)for(unsigned di=1;di<3;di++) {
      CALL(nvnCommandBufferSetRenderTargets,&local,0,NULL,NULL,&texture[4+di],NULL);
      CALL(nvnCommandBufferSetScissor,&local,0,0,16>>di,16>>di);
      CALL(nvnCommandBufferClearDepthStencil,&local,di==1?.125f:.875f,1,di==1?0x5a:0xc3,255);
    }
    CALL(nvnCommandBufferSetRenderTargets,&local,2,colors,baseviews,&texture[2],NULL);
    CALL(nvnCommandBufferSetScissor,&local,0,0,16,16);
    CALL(nvnCommandBufferClearDepthStencil,&local,.25f,1,0xa7,255);
    submit(CALL(nvnCommandBufferEndRecording,&local));
    if(!enabled && !test) {
      // Bounds failures are metadata-only: never submit an illegal mip to
      // Vulkan merely to test the front-proof's conservative rejection.
      view_t later=*(const view_t *)&view[2];
      const view_t *batch[]={ (const view_t *)&view[1],&later,
                              (const view_t *)&view[1],(const view_t *)&view[2] };
      VkImage unused=VK_NULL_HANDLE;
      const int invalid[]={-1,3,16,32,INT32_MIN,INT32_MAX};
      for(unsigned n=0;n<sizeof invalid/sizeof *invalid;n++) {
        later.base_level=invalid[n];
        CHECK(!vk_texture_mip_clear_admit_views((const tex_t *)&texture[0],batch,4,&unused));
      }
      CHECK(!vk_texture_mip_clear_admit_views((const tex_t *)&texture[0],batch,0,&unused));
      CHECK(!vk_texture_mip_clear_admit_views((const tex_t *)&texture[0],batch,5,&unused));
    }
    vk_mip_clear_enable(enabled);
    uint64_t a0,b0,c0,d0;int mode;vk_mip_clear_stats(&a0,&b0,&c0,&d0,&mode);
    CALL(nvnCommandBufferBeginRecording,&local);
    CALL(nvnCommandBufferSetTexturePool,&local,&tp);CALL(nvnCommandBufferSetSamplerPool,&local,&sp);
    CALL(nvnCommandBufferSetRenderTargets,&local,2,colors,baseviews,&texture[2],NULL);
    CALL(nvnCommandBufferSetScissor,&local,0,0,16,16);CALL(nvnCommandBufferSetViewports,&local,0,1,(float[]){0,0,16,16});
    CALL(nvnCommandBufferSetDepthRange,&local,.5f,.5f);
    CALL(nvnCommandBufferBindVertexAttribState,&local,0,NULL);CALL(nvnCommandBufferBindVertexStreamState,&local,0,NULL);
    CALL(nvnCommandBufferBindPolygonState,&local,&poly);CALL(nvnCommandBufferBindColorState,&local,&color);
    CALL(nvnCommandBufferBindChannelMaskState,&local,&mask);CALL(nvnCommandBufferBindDepthStencilState,&local,&writer);
    CALL(nvnCommandBufferBindMultisampleState,&local,&ms);
    CALL(nvnCommandBufferBindProgram,&local,&program[0],NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferBindUniformBuffer,&local,NVN_SHADER_STAGE_FRAGMENT,0,(uintptr_t)(data+0x30000),16);
    if(test==16) {
      // Fill id4's physical recipe in this same submission and epoch, but in
      // an earlier completed physical pass. The later mip0 pass must not
      // inherit that old alias use as a current-pass conflict.
      const NVNtexture *prewarm[]={&texture[1]};const NVNtextureView *pv[]={&view[2]};
      NVNseparateTextureHandle th=CALL(nvnDeviceGetSeparateTextureHandle,&device,4);
      CALL(nvnCommandBufferSetRenderTargets,&local,1,prewarm,pv,NULL,NULL);
      CALL(nvnCommandBufferSetScissor,&local,0,0,4,4);
      CALL(nvnCommandBufferSetViewports,&local,0,1,(float[]){0,0,4,4});
      CALL(nvnCommandBufferBindProgram,&local,&program[2],NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
      CALL(nvnCommandBufferBindSeparateTextures,&local,NVN_SHADER_STAGE_FRAGMENT,0,1,&th);
      CALL(nvnCommandBufferBindSeparateSamplers,&local,NVN_SHADER_STAGE_FRAGMENT,0,1,&sh);
      CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
      CALL(nvnCommandBufferSetRenderTargets,&local,2,colors,baseviews,&texture[2],NULL);
      CALL(nvnCommandBufferSetScissor,&local,0,0,16,16);
      CALL(nvnCommandBufferSetViewports,&local,0,1,(float[]){0,0,16,16});
      CALL(nvnCommandBufferBindProgram,&local,&program[0],NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    }
    if(test==15) {
      NVNseparateTextureHandle th=CALL(nvnDeviceGetSeparateTextureHandle,&device,3);
      CALL(nvnCommandBufferBindProgram,&local,&program[2],NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
      CALL(nvnCommandBufferBindSeparateTextures,&local,NVN_SHADER_STAGE_FRAGMENT,0,1,&th);
      CALL(nvnCommandBufferBindSeparateSamplers,&local,NVN_SHADER_STAGE_FRAGMENT,0,1,&sh);
    }
    for(unsigned k=0;k<(test==10?65:1);k++)CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    if(test>=25) {
      // Use the later requested mip in this same physical pass, after its
      // first full-size draw. The first requested mip remains untouched, so
      // accepting only the first view of a batch would incorrectly admit.
      // Test27 instead uses a distinct image aliasing color1's allocation.
      unsigned side=test==27?16:4,id=test==25?5:test==26?6:7;
      NVNseparateTextureHandle th=CALL(nvnDeviceGetSeparateTextureHandle,&device,id);
      CALL(nvnCommandBufferSetScissor,&local,0,0,side,side);
      CALL(nvnCommandBufferSetViewports,&local,0,1,(float[]){0,0,side,side});
      CALL(nvnCommandBufferBindProgram,&local,&program[2],NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
      CALL(nvnCommandBufferBindSeparateTextures,&local,NVN_SHADER_STAGE_FRAGMENT,0,1,&th);
      CALL(nvnCommandBufferBindSeparateSamplers,&local,NVN_SHADER_STAGE_FRAGMENT,0,1,&sh);
      CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
      CALL(nvnCommandBufferSetScissor,&local,0,0,16,16);
      CALL(nvnCommandBufferSetViewports,&local,0,1,(float[]){0,0,16,16});
      CALL(nvnCommandBufferBindProgram,&local,&program[0],NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    }
    for(unsigned l=1;l<3;l++) {
      const NVNtextureView *views[]={&view[l],&view[l]};
      CALL(nvnCommandBufferSetRenderTargets,&local,2,colors,views,&texture[test>=17?4+l:2],NULL);
      int size=16>>l,sx=test==11?1:0,sy=0,sw=size-(test==11?1:0),st=size;
      if(test==19)sw=st=32768;
      if(test==20) { sx=-2;sy=-3;sw=size+2;st=size+3; }
      if(test==21) { sx=-2;sy=-3;sw=size+1;st=size+2; }
      if(test==22)sw=0;
      if(test==23) { sx=INT32_MIN;sw=INT32_MAX; }
      if(test==24) { sx=1;sw=INT32_MAX; }
      CALL(nvnCommandBufferSetScissor,&local,sx,sy,sw,st);
      CALL(nvnCommandBufferClearColor,&local,0,l==1?blue:cyan,15);
      if(test>=19) {
        CALL(nvnCommandBufferSetStencilRef,&local,NVN_FACE_FRONT,0x51);
        CALL(nvnCommandBufferSetStencilRef,&local,NVN_FACE_BACK,0x62);
      }
      if(test==18 && l==1)CALL(nvnCommandBufferClearDepthStencil,&local,.625f,1,0x39,255);
      if(test==8 && l==1)CALL(nvnCommandBufferBarrier,&local,NVN_BARRIER_INVALIDATE_TEXTURE_BIT);
      if(test==9 && l==1)for(unsigned k=0;k<257;k++)CALL(nvnCommandBufferSetScissor,&local,0,0,8,8);
      CALL(nvnCommandBufferClearColor,&local,1,l==1?yellow:magenta,15);
    }
    CALL(nvnCommandBufferSetRenderTargets,&local,2,colors,baseviews,&texture[2],NULL);
    CALL(nvnCommandBufferSetScissor,&local,0,0,16,16);
    CALL(nvnCommandBufferBindDepthStencilState,&local,&reader);
    CALL(nvnCommandBufferBindProgram,&local,&program[0],NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    if(test==1||test==2||test==16) {
      // The owned FS uses integer FragCoord fetches. Limit the consumer to
      // the actual 8x8 source mip; no robustness-dependent OOB image access.
      CALL(nvnCommandBufferSetViewports,&local,0,1,(float[]){0,0,8,8});
      CALL(nvnCommandBufferSetScissor,&local,0,0,8,8);
      NVNseparateTextureHandle th=CALL(nvnDeviceGetSeparateTextureHandle,&device,test==16?4:test==2?2:1);
      CALL(nvnCommandBufferBindProgram,&local,&program[2],NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
      CALL(nvnCommandBufferBindSeparateTextures,&local,NVN_SHADER_STAGE_FRAGMENT,0,1,&th);
      CALL(nvnCommandBufferBindSeparateSamplers,&local,NVN_SHADER_STAGE_FRAGMENT,0,1,&sh);
    } else if(test==3||test==4) {
      CALL(nvnCommandBufferBindProgram,&local,&program[1],NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
      CALL(nvnCommandBufferBindStorageBuffer,&local,NVN_SHADER_STAGE_FRAGMENT,0,
           (uintptr_t)(test==3?data:data+0x30100),16);
    } else CALL(nvnCommandBufferBindUniformBuffer,&local,NVN_SHADER_STAGE_FRAGMENT,0,(uintptr_t)(data+0x30100),16);
    if(test==5)CALL(nvnCommandBufferCopyBufferToBuffer,&local,(uintptr_t)src,(uintptr_t)dst,4,0);
    if(test==6) {
      CALL(nvnCommandBufferBindProgram,&local,&program[3],NVN_SHADER_STAGE_COMPUTE_BIT);
      CALL(nvnCommandBufferBindStorageBuffer,&local,NVN_SHADER_STAGE_COMPUTE,0,(uintptr_t)src,256);
      CALL(nvnCommandBufferBindStorageBuffer,&local,NVN_SHADER_STAGE_COMPUTE,1,(uintptr_t)dst,256);
      CALL(nvnCommandBufferDispatchCompute,&local,1,1,1);
      CALL(nvnCommandBufferBindProgram,&local,&program[0],NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    }
    if(test==7)CALL(nvnCommandBufferReportCounter,&local,NVN_COUNTER_TYPE_TIMESTAMP,(uintptr_t)(data+0x42000));
    if(test==12)CALL(nvnCommandBufferClearDepthStencil,&local,.75f,1,0x39,255);
    if(test==13)CALL(nvnCommandBufferSignalEvent,&local,&event,NVN_EVENT_SIGNAL_MODE_WRITE,NVN_EVENT_SIGNAL_LOCATION_BOTTOM,0,13);
    if(test==14) {
      NVNimageHandle ih=CALL(nvnDeviceGetImageHandle,&device,1);
      CALL(nvnCommandBufferSetViewports,&local,0,1,(float[]){0,0,8,8});CALL(nvnCommandBufferSetScissor,&local,0,0,8,8);
      CALL(nvnCommandBufferBindProgram,&local,&program[4],NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
      CALL(nvnCommandBufferBindImages,&local,NVN_SHADER_STAGE_FRAGMENT,0,1,&ih);
    }
    CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    // Make both actual draws' samples dead; the query subsystem must not be
    // disabled globally just to obtain a continuous-pass test.
    CALL(nvnCommandBufferResetCounter,&local,NVN_COUNTER_TYPE_SAMPLES_PASSED);
    submit(CALL(nvnCommandBufferEndRecording,&local));
    vk_mip_clear_stats(&a,&b,&c,&d,&mode);
    LOG("[vulkan deferred mip clear fixture] enabled=%u test=%u admitted=%llu materialized=%llu continued=%llu",enabled,test,
      (unsigned long long)(a-a0),(unsigned long long)(b-b0),(unsigned long long)(c-c0));
    uint64_t splits,boundaries,draws;int stream_reference;
    vk_stream_stats(&splits,&boundaries,&draws,&stream_reference);
    int admitted=enabled && test!=8 && test!=9 && (test!=10||stream_reference) && test!=11 && test!=15 && test!=18 && test<21;
    int continued=admitted && (test==0||test==4||test==10||test==17||test==19||test==20);
    CHECK(a-a0==(unsigned)admitted);CHECK(b-b0==(admitted?4u:0u));CHECK(c-c0==(unsigned)continued);
    CHECK(!vk_mip_clear_pending());
    if(test>=19) { CHECK(S.stencil_ref[0]==0x51);CHECK(S.stencil_ref[1]==0x62); }
    if(test==13)CHECK(CALL(nvnEventGetValue,&event)==13);
    if(test==5)CHECK(dst[0]==src[0]);
    if(test==6)for(unsigned k=0;k<64;k++)CHECK(dst[k]==src[k]*3+7);
    for(unsigned t=0;t<2;t++)for(unsigned l=0;l<3;l++) {
      unsigned size=16>>l;NVNcopyRegion region={0,0,0,size,size,1};uint8_t pixels[1024];
      CALL(nvnTextureReadTexels,&texture[t],&view[l],&region,pixels);
      for(unsigned y=0;y<size;y++)for(unsigned x=0;x<size;x++) {
        uint8_t expected[4];
        if(!l) {
          if(t)memcpy(expected,(uint8_t[]){0,255,0,255},4);
          else if(test==1)memcpy(expected,(x<8&&y<8)?(uint8_t[]){0,0,0,255}:(uint8_t[]){255,0,0,255},4);
          else if(test==2||test==16)memcpy(expected,(uint8_t[]){255,0,0,255},4);
          else if(test==3)memset(expected,0,4);
          else if(test==14)memcpy(expected,(uint8_t[]){255,0,0,255},4);
          else memcpy(expected,(uint8_t[]){255,255,255,255},4);
        } else if(test==14 && t==0 && l==1)memcpy(expected,(uint8_t[]){0,64,255,255},4);
        else if((test==11 && !x) || (test==21 && (x==size-1 || y==size-1)) ||
                test==22 || test==23 || (test==24 && !x))
          memcpy(expected,t?(uint8_t[]){0,255,0,255}:(uint8_t[]){255,0,0,255},4);
        else memcpy(expected,l==1?(t?(uint8_t[]){255,255,0,255}:(uint8_t[]){0,0,255,255}):
                            (t?(uint8_t[]){255,0,255,255}:(uint8_t[]){0,255,255,255}),4);
        CHECK(!memcmp(pixels+(y*size+x)*4,expected,4));
      }
    }
    for(unsigned di=0;di<(test>=17?3u:1u);di++) {
      unsigned side=16>>di,count=side*side,ds=di?4+di:2;
      double expected_depth=!di?(test==12?.75:.5):di==1?(test==18?.625:.125):.875;
      uint8_t expected_stencil=!di?(test==12?0x39:0xa7):di==1?(test==18?0x39:0x5a):0xc3;
      mtl_sub_begin();VkImage depth=vk_texture_native((const tex_t *)&texture[ds]);vk_buffer *rb=vk_temp_buffer(count*5);
      VkBufferImageCopy copies[]={
        {.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1},.imageExtent={side,side,1}},
        {.bufferOffset=count*4,.imageSubresource={VK_IMAGE_ASPECT_STENCIL_BIT,0,0,1},.imageExtent={side,side,1}}};
      vk_barrier();vkCmdCopyImageToBuffer(V.cmd,depth,VK_IMAGE_LAYOUT_GENERAL,rb->handle,2,copies);
      vk_internal_wait(vk_internal_commit());
      for(unsigned k=0;k<count;k++) { uint32_t z;memcpy(&z,rb->map+k*4,4);
        CHECK(fabs((z&0xffffff)/16777215.-expected_depth)<1e-6);
        CHECK(rb->map[count*4+k]==expected_stencil); }
    }
    if(test==14) {
      // The storage test permanently promotes this physical image. Recreate
      // its original generic descriptor before testing the remaining cases; do not
      // pretend that STORAGE usage can be removed in place.
      CALL(nvnTextureFinalize,&texture[0]);NVNtextureBuilder tb;
      CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
      CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);
      CALL(nvnTextureBuilderSetWidth,&tb,16);CALL(nvnTextureBuilderSetHeight,&tb,16);
      CALL(nvnTextureBuilderSetLevels,&tb,3);CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);
      CALL(nvnTextureBuilderSetStorage,&tb,&pool,0);CHECK(CALL(nvnTextureInitialize,&texture[0],&tb));
      CALL(nvnTexturePoolRegisterTexture,&tp,1,&texture[0],&view[1]);
      CALL(nvnTexturePoolRegisterTexture,&tp,5,&texture[0],&view[2]);
    }
  }
  vk_mip_clear_enable(old_enabled);vk_tbdr_reference(!old_tbdr);vk_image_layout_reference(old_layout);
  vk_sample_liveness_reference(query_reference);vk_submission_barrier_reference(old_barrier);
  vk_texture_binding_reference(old_binding_reference);
  CALL(nvnCommandBufferFinalize,&local);CALL(nvnEventFinalize,&event);for(unsigned n=0;n<5;n++)CALL(nvnProgramFinalize,&program[n]);
  CALL(nvnSamplerFinalize,&sampler);CALL(nvnTexturePoolFinalize,&tp);CALL(nvnSamplerPoolFinalize,&sp);
  for(unsigned t=0;t<8;t++)CALL(nvnTextureFinalize,&texture[t]);CALL(nvnMemoryPoolFinalize,&pool);free(data);
  S=saved;vk_pipeline_state_changed();
  LOG("[vulkan deferred mip clear fixture] real mip0 continuation, dual clears, sampler/buffer aliases, transfers/compute/query/depth, later-mip/second-image alias rejection and bounded fallbacks passed");
}
