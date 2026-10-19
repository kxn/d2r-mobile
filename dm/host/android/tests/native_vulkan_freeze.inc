// 原冻结绘制：用真的 NVN 窗口登记、命令录制、模板与 GPU 回读验。
// Original frozen rendering through real NVN window registration, recording,
// stencil operations and GPU readback. No game modules execute in this fixture.
static void freeze_test(NVNprogram *compute, NVNtexturePool *pool,
                        const void *vertex_control, const void *fragment_control) {
  NVNprogram solid_program; NVNprogram *graphics=&solid_program;
  CHECK(CALL(nvnProgramInitialize,graphics,&device));
  NVNshaderData solid_shaders[]={{.control=vertex_control},{.control=fragment_control}};
  CHECK(CALL(nvnProgramSetShaders,graphics,2,solid_shaders));
  void *storage;
  CHECK(!posix_memalign(&storage,65536,1<<20)); memset(storage,0,1<<20);
  NVNmemoryPoolBuilder memory_builder={0}; NVNmemoryPool private_pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&memory_builder);
  CALL(nvnMemoryPoolBuilderSetDevice,&memory_builder,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&memory_builder,storage,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&memory_builder,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&private_pool,&memory_builder));
  // UI、世界、窗口、模板、三种探针、上传、缩小后新建的世界目标。
  // UI, world, window, stencil, three probes, upload, recreated smaller world.
  NVNtexture texture[9];
  for (unsigned i=0;i<9;i++) {
    NVNtextureBuilder builder={0};
    CALL(nvnTextureBuilderSetDefaults,&builder);
    CALL(nvnTextureBuilderSetDevice,&builder,&device);
    CALL(nvnTextureBuilderSetTarget,&builder,NVN_TEXTURE_TARGET_2D);
    CALL(nvnTextureBuilderSetWidth,&builder,i==8?8:16);
    CALL(nvnTextureBuilderSetHeight,&builder,i==8?4:8);
    CALL(nvnTextureBuilderSetFlags,&builder,NVN_TEXTURE_FLAGS_IMAGE);
    CALL(nvnTextureBuilderSetFormat,&builder,i==3?NVN_FORMAT_DEPTH32F_STENCIL8:NVN_FORMAT_RGBA8);
    CALL(nvnTextureBuilderSetStorage,&builder,&private_pool,i*65536);
    CHECK(CALL(nvnTextureInitialize,&texture[i],&builder));
  }
  NVNwindowBuilder window_builder={0}; NVNwindow window;
  NVNtexture *window_textures[]={&texture[2]};
  CALL(nvnWindowBuilderSetDefaults,&window_builder);
  CALL(nvnWindowBuilderSetDevice,&window_builder,&device);
  CALL(nvnWindowBuilderSetTextures,&window_builder,1,window_textures);
  CHECK(CALL(nvnWindowInitialize,&window,&window_builder));
  CALL(nvnTexturePoolRegisterTexture,pool,3,&texture[6],NULL);
  NVNimageHandle image=CALL(nvnDeviceGetImageHandle,&device,3);
  NVNprogram mask_program;
  CHECK(CALL(nvnProgramInitialize,&mask_program,&device));
  NVNshaderData shader={.control=vertex_control};
  CHECK(CALL(nvnProgramSetShaders,&mask_program,1,&shader));
  NVNdepthStencilState plain,mask,test;
  CALL(nvnDepthStencilStateSetDefaults,&plain);
  CALL(nvnDepthStencilStateSetDepthTestEnable,&plain,0);
  CALL(nvnDepthStencilStateSetDepthWriteEnable,&plain,0);
  CALL(nvnDepthStencilStateSetStencilTestEnable,&plain,0);
  mask=plain; test=plain;
  CALL(nvnDepthStencilStateSetStencilTestEnable,&mask,1);
  CALL(nvnDepthStencilStateSetStencilFunc,&mask,NVN_FACE_FRONT_AND_BACK,NVN_STENCIL_FUNC_ALWAYS);
  CALL(nvnDepthStencilStateSetStencilOp,&mask,NVN_FACE_FRONT_AND_BACK,NVN_STENCIL_OP_KEEP,NVN_STENCIL_OP_KEEP,NVN_STENCIL_OP_REPLACE);
  CALL(nvnDepthStencilStateSetStencilTestEnable,&test,1);
  CALL(nvnDepthStencilStateSetStencilFunc,&test,NVN_FACE_FRONT_AND_BACK,NVN_STENCIL_FUNC_EQUAL);
  NVNpolygonState polygon; CALL(nvnPolygonStateSetDefaults,&polygon);
  CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNcolorState color_state; CALL(nvnColorStateSetDefaults,&color_state);
  NVNchannelMaskState channel; CALL(nvnChannelMaskStateSetDefaults,&channel);
  NVNeventBuilder eb={0}; NVNevent event;
  CALL(nvnEventBuilderSetDefaults,&eb);
  CALL(nvnEventBuilderSetStorage,&eb,&private_pool,0xa2000);
  CHECK(CALL(nvnEventInitialize,&event,&eb));
  uint32_t *event_value=(void *)((uint8_t *)storage+0xa2000);
  uint64_t *counter=(void *)((uint8_t *)storage+0xa3000);
  float *uniform=(void *)((uint8_t *)storage+0xa0000);
  uint8_t *upload=(uint8_t *)storage+0xa1000;
  const NVNtexture *ui[]={&texture[0]}, *world[]={&texture[1]}, *screen[]={&texture[2]};
  const float red[]={1,0,0,1}, green[]={0,1,0,1}, blue[]={0,0,1,1}, yellow[]={1,1,0,1};
  NVNcopyRegion region={0,0,0,16,8,1}, small={0,0,0,8,4,1};
  const float viewport[]={0,0,16,8};
  mtl_world_frozen(0);
  CALL(nvnCommandBufferBeginRecording,&cmd);
  for (unsigned i=0;i<9;i++) if (i!=3)
    CALL(nvnCommandBufferClearTexture,&cmd,&texture[i],NULL,i==8?&small:&region,red,15);
  submit(end());
  // 请求按提交锁存；强制全画请求只取一次，冻结请求持续保留。
  // Submit latching: force-full is consumed once, freeze remains requested.
  mtl_world_frozen(1); mtl_rt_force_full(4);
  uint64_t latch=mtl_host_latch(); CHECK((uint32_t)latch==2 && (latch>>32)==4);
  CHECK(mtl_host_latch()==2);
  CALL(nvnCommandBufferBeginRecording,&cmd);
  CALL(nvnCommandBufferSetRenderTargets,&cmd,1,screen,NULL,NULL,NULL);
  submit(end()); // 原窗口目标确定帧末边界。 | The original window target marks frame end.
  vk_freeze_stats stats;
  uint8_t pixels[16*8*4];
  for (unsigned frame=0;frame<3;frame++) {
    NVNcommandHandle pieces[3];
    // 最后一帧解除冻结；中间帧缩小面板，必须露出完整帧画过的绿底。
    // Unfreeze last; shrinking the middle panel must reveal the full green world.
    mtl_world_frozen(frame<2);
    memset(upload,frame+33,16*8*4);
    CALL(nvnCommandBufferBeginRecording,&cmd);
    CALL(nvnCommandBufferSetCopyRowStride,&cmd,0);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,ui,NULL,NULL,NULL);
    CALL(nvnCommandBufferSetViewports,&cmd,0,1,viewport);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,8);
    CALL(nvnCommandBufferBindVertexAttribState,&cmd,0,NULL);
    CALL(nvnCommandBufferBindVertexStreamState,&cmd,0,NULL);
    CALL(nvnCommandBufferBindChannelMaskState,&cmd,&channel);
    CALL(nvnCommandBufferBindColorState,&cmd,&color_state);
    CALL(nvnCommandBufferBindPolygonState,&cmd,&polygon);
    CALL(nvnCommandBufferBindDepthStencilState,&cmd,&plain);
    CALL(nvnCommandBufferBindProgram,&cmd,graphics,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferBindUniformBuffer,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,(uintptr_t)uniform,16);
    CALL(nvnCommandBufferUpdateUniformBuffer,&cmd,(uintptr_t)uniform,16,0,16,blue);
    CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    if (frame==1) { pieces[0]=end(); CALL(nvnCommandBufferBeginRecording,&cmd); }
    CALL(nvnCommandBufferSetRenderTargets,&cmd,0,NULL,NULL,&texture[3],NULL);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,8);
    CALL(nvnCommandBufferClearDepthStencil,&cmd,1,1,0,255);
    CALL(nvnCommandBufferBindDepthStencilState,&cmd,&mask);
    CALL(nvnCommandBufferSetStencilRef,&cmd,NVN_FACE_FRONT_AND_BACK,1);
    CALL(nvnCommandBufferSetStencilMask,&cmd,NVN_FACE_FRONT_AND_BACK,255);
    CALL(nvnCommandBufferSetStencilValueMask,&cmd,NVN_FACE_FRONT_AND_BACK,255);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,frame==2?0:8,8);
    CALL(nvnCommandBufferBindProgram,&cmd,&mask_program,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,world,NULL,&texture[3],NULL);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,8);
    CALL(nvnCommandBufferClearColor,&cmd,0,red,15);
    CALL(nvnCommandBufferBindDepthStencilState,&cmd,&test);
    CALL(nvnCommandBufferSetStencilRef,&cmd,NVN_FACE_FRONT_AND_BACK,0);
    CALL(nvnCommandBufferSetStencilMask,&cmd,NVN_FACE_FRONT_AND_BACK,0);
    CALL(nvnCommandBufferBindProgram,&cmd,graphics,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferUpdateUniformBuffer,&cmd,(uintptr_t)uniform,16,0,16,frame==1?blue:green);
    CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    CALL(nvnCommandBufferClearTexture,&cmd,&texture[4],NULL,&region,frame?blue:green,15);
    CALL(nvnCommandBufferCopyTextureToTexture,&cmd,&texture[frame?0:1],NULL,&region,&texture[5],NULL,&region,0);
    // 冻结中若漏派发，会把完整帧留下的绿色探针改成红色。
    // A leaked frozen dispatch would overwrite the preserved green probe with red.
    if (frame==0) CALL(nvnCommandBufferClearTexture,&cmd,&texture[6],NULL,&region,green,15);
    else if (frame==1) {
      CALL(nvnCommandBufferBindProgram,&cmd,compute,NVN_SHADER_STAGE_COMPUTE_BIT);
      CALL(nvnCommandBufferBindImages,&cmd,NVN_SHADER_STAGE_COMPUTE,0,1,&image);
      CALL(nvnCommandBufferDispatchCompute,&cmd,4,2,1);
    }
    CALL(nvnCommandBufferCopyBufferToTexture,&cmd,(uintptr_t)upload,&texture[7],NULL,&region,0);
    CALL(nvnCommandBufferSignalEvent,&cmd,&event,NVN_EVENT_SIGNAL_MODE_WRITE,NVN_EVENT_SIGNAL_LOCATION_BOTTOM,0,frame+100);
    CALL(nvnCommandBufferReportCounter,&cmd,NVN_COUNTER_TYPE_TIMESTAMP,(uintptr_t)counter);
    if (frame==1) { pieces[1]=end(); CALL(nvnCommandBufferBeginRecording,&cmd); }
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,screen,NULL,NULL,NULL);
    CALL(nvnCommandBufferCopyTextureToTexture,&cmd,&texture[1],NULL,&region,&texture[2],NULL,&region,0);
    CALL(nvnCommandBufferBindDepthStencilState,&cmd,&plain);
    CALL(nvnCommandBufferBindProgram,&cmd,graphics,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferUpdateUniformBuffer,&cmd,(uintptr_t)uniform,16,0,16,yellow);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,frame==1?4:8,8);
    CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    if (frame==1) {
      pieces[2]=end(); CALL(nvnCommandBufferBeginRecording,&cmd);
      CALL(nvnCommandBufferCopyCommands,&cmd,3,pieces);
    }
    submit(end());
    CHECK(*event_value==frame+100 && counter[0]>0);
    CALL(nvnTextureReadTexels,&texture[2],NULL,&region,pixels);
    for (unsigned y=0;y<8;y++) for (unsigned x=0;x<16;x++) {
      const uint8_t *expected=x<(frame==1?4:8)?(uint8_t[]){255,255,0,255}:(uint8_t[]){0,255,0,255};
      if (memcmp(pixels+(y*16+x)*4,expected,4))
        fprintf(stderr,"freeze frame=%u pixel=%u,%u got=%u,%u,%u,%u expected=%u,%u,%u,%u\n",frame,x,y,
                pixels[(y*16+x)*4],pixels[(y*16+x)*4+1],pixels[(y*16+x)*4+2],pixels[(y*16+x)*4+3],
                expected[0],expected[1],expected[2],expected[3]);
      CHECK(!memcmp(pixels+(y*16+x)*4,x<(frame==1?4:8)?(uint8_t[]){255,255,0,255}:(uint8_t[]){0,255,0,255},4));
    }
    CALL(nvnTextureReadTexels,&texture[7],NULL,&region,pixels);
    for (unsigned i=0;i<sizeof pixels;i++) CHECK(pixels[i]==frame+33);
    for (unsigned t=4;t<=6;t++) if (frame==1) {
      CALL(nvnTextureReadTexels,&texture[t],NULL,&region,pixels);
      for (unsigned i=0;i<sizeof pixels;i+=4) CHECK(!memcmp(pixels+i,(uint8_t[]){0,255,0,255},4));
    }
    vk_freeze_snapshot(&stats);
    CHECK(stats.state==(frame==0?1:frame==1?2:0));
    if (frame==1) CHECK(stats.skipped_targets>=2 && stats.skipped_operations>=5);
  }
  // 冻结期间缩小并替换世界目标，沿原比例控制器发出的强制全画请求重建缓存。
  // Recreate a smaller world target while frozen, using the original scale
  // controller's force-full hint. Both requested frames must draw completely.
  uint64_t full_before=stats.full_frames;
  mtl_world_frozen(1);
  CALL(nvnCommandBufferBeginRecording,&cmd);
  CALL(nvnCommandBufferSetRenderTargets,&cmd,1,screen,NULL,NULL,NULL);
  submit(end());
  for (unsigned frame=0;frame<4;frame++) {
    if (frame==1) mtl_rt_force_full(2);
    unsigned target=frame?8:1;
    const NVNtexture *replacement[]={&texture[target]};
    CALL(nvnCommandBufferBeginRecording,&cmd);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,ui,NULL,NULL,NULL);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,8);
    CALL(nvnCommandBufferBindDepthStencilState,&cmd,&plain);
    CALL(nvnCommandBufferClearColor,&cmd,0,blue,15);
    if (frame==1) {
      // 一帧分两次提交，不能多消耗一次强制完整帧请求。
      // Two submits in this frame must not consume an extra force-full frame.
      submit(end()); CALL(nvnCommandBufferBeginRecording,&cmd);
    }
    CALL(nvnCommandBufferSetRenderTargets,&cmd,0,NULL,NULL,&texture[3],NULL);
    CALL(nvnCommandBufferClearDepthStencil,&cmd,1,1,0,255);
    CALL(nvnCommandBufferBindDepthStencilState,&cmd,&mask);
    CALL(nvnCommandBufferSetStencilRef,&cmd,NVN_FACE_FRONT_AND_BACK,1);
    CALL(nvnCommandBufferSetStencilMask,&cmd,NVN_FACE_FRONT_AND_BACK,255);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,8,8);
    CALL(nvnCommandBufferBindProgram,&cmd,&mask_program,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,replacement,NULL,&texture[3],NULL);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,8);
    CALL(nvnCommandBufferClearColor,&cmd,0,red,15);
    CALL(nvnCommandBufferBindDepthStencilState,&cmd,&test);
    CALL(nvnCommandBufferSetStencilRef,&cmd,NVN_FACE_FRONT_AND_BACK,0);
    CALL(nvnCommandBufferSetStencilMask,&cmd,NVN_FACE_FRONT_AND_BACK,0);
    CALL(nvnCommandBufferBindProgram,&cmd,graphics,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferUpdateUniformBuffer,&cmd,(uintptr_t)uniform,16,0,16,frame==3?blue:green);
    CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,screen,NULL,NULL,NULL);
    CALL(nvnCommandBufferCopyTextureToTexture,&cmd,&texture[target],NULL,frame?&small:&region,&texture[2],NULL,&region,0);
    submit(end());
    CALL(nvnTextureReadTexels,&texture[2],NULL,&region,pixels);
    if (memcmp(pixels,(uint8_t[]){0,255,0,255},4))
      fprintf(stderr,"freeze resized frame=%u got=%u,%u,%u,%u expected=0,255,0,255\n",
              frame,pixels[0],pixels[1],pixels[2],pixels[3]);
    for (unsigned i=0;i<sizeof pixels;i+=4)
      CHECK(!memcmp(pixels+i,(uint8_t[]){0,255,0,255},4));
    vk_freeze_snapshot(&stats);
    CHECK(stats.state==(frame==3?2:1));
    CHECK(stats.full_frames==full_before+(frame<3?frame+1:3));
  }
  // 缺少无颜色深度趟时，不能认成世界段并丢掉任何绘制。
  // Without a depth-only pass, retain every draw even when freeze is requested.
  uint64_t skipped=stats.skipped_targets, operations=stats.skipped_operations;
  mtl_world_frozen(1);
  CALL(nvnCommandBufferBeginRecording,&cmd);
  CALL(nvnCommandBufferSetRenderTargets,&cmd,1,screen,NULL,NULL,NULL);
  submit(end());
  for (unsigned frame=0;frame<2;frame++) {
    CALL(nvnCommandBufferBeginRecording,&cmd);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,ui,NULL,NULL,NULL);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,8);
    CALL(nvnCommandBufferClearColor,&cmd,0,green,15);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,world,NULL,NULL,NULL);
    CALL(nvnCommandBufferClearColor,&cmd,0,frame?blue:red,15);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,screen,NULL,NULL,NULL);
    CALL(nvnCommandBufferCopyTextureToTexture,&cmd,&texture[1],NULL,&region,&texture[2],NULL,&region,0);
    submit(end());
    CALL(nvnTextureReadTexels,&texture[2],NULL,&region,pixels);
    for (unsigned i=0;i<sizeof pixels;i+=4)
      CHECK(!memcmp(pixels+i,frame?(uint8_t[]){0,0,255,255}:(uint8_t[]){255,0,0,255},4));
    vk_freeze_snapshot(&stats);
    CHECK(stats.skipped_targets==skipped && stats.skipped_operations==operations);
  }
  mtl_world_frozen(0);
  CALL(nvnCommandBufferBeginRecording,&cmd);
  CALL(nvnCommandBufferSetRenderTargets,&cmd,1,ui,NULL,NULL,NULL);
  CALL(nvnCommandBufferSetRenderTargets,&cmd,1,screen,NULL,NULL,NULL);
  submit(end());
  vk_freeze_snapshot(&stats); CHECK(stats.state==0);
  CALL(nvnEventFinalize,&event);
  CALL(nvnWindowFinalize,&window);
  CALL(nvnProgramFinalize,&mask_program);
  CALL(nvnProgramFinalize,graphics);
  for (unsigned i=0;i<9;i++) CALL(nvnTextureFinalize,&texture[i]);
  CALL(nvnMemoryPoolFinalize,&private_pool); free(storage);
}
