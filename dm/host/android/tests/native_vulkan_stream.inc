// One original NVN submit, several actual Vulkan batches: counter prefixes,
// reset boundaries, GPU uniform updates and final CPU publication must agree.
static void stream_submit_test(const void *vertex,const void *fragment) {
  if(!vk_async_submissions()) { LOG("[vulkan streaming fixture] serial reference: split case requires async");return; }
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,1<<20));memset(storage,0,1<<20);
  NVNmemoryPoolBuilder mb={0};NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtexture textures[2];
  for(unsigned i=0;i<2;i++) {
    NVNtextureBuilder b={0};CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&b,16);
    CALL(nvnTextureBuilderSetHeight,&b,16);CALL(nvnTextureBuilderSetFormat,&b,NVN_FORMAT_RGBA8);
    CALL(nvnTextureBuilderSetStorage,&b,&pool,i*65536);CHECK(CALL(nvnTextureInitialize,&textures[i],&b));
  }
  NVNprogram program;NVNshaderData shaders[]={{.control=vertex},{.control=fragment}};
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);
  CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);
  CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);
  NVNchannelMaskState channels;CALL(nvnChannelMaskStateSetDefaults,&channels);
  NVNeventBuilder eb;NVNevent event;
  CALL(nvnEventBuilderSetDefaults,&eb);CALL(nvnEventBuilderSetStorage,&eb,&pool,0x60100);
  CHECK(CALL(nvnEventInitialize,&event,&eb));
  const float colors[][4]={{1,0,0,1},{0,1,0,1},{0,0,1,1}},viewport[]={0,0,16,16};
  uint64_t split0,boundaries,batched;int original;
  vk_stream_stats(&split0,&boundaries,&batched,&original);
  const int modes[]={1,0,original};
  const unsigned draws_per_pass=288;
  for(unsigned run=0;run<3;run++) {
    int reference=modes[run];vk_stream_submit_reference(reference);
    uint64_t *reports=(void *)(storage+0x60000);memset(reports,0,32);
    CALL(nvnEventSignal,&event,NVN_EVENT_SIGNAL_MODE_WRITE,0);
    uint64_t before_fence=mtl_last_fence();
    vk_stream_stats(&split0,&boundaries,&batched,&original);
    CALL(nvnCommandBufferBeginRecording,&cmd);
    CALL(nvnCommandBufferSetViewports,&cmd,0,1,viewport);CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);
    CALL(nvnCommandBufferBindVertexAttribState,&cmd,0,NULL);CALL(nvnCommandBufferBindVertexStreamState,&cmd,0,NULL);
    CALL(nvnCommandBufferBindPolygonState,&cmd,&polygon);CALL(nvnCommandBufferBindDepthStencilState,&cmd,&ds);
    CALL(nvnCommandBufferBindColorState,&cmd,&color);CALL(nvnCommandBufferBindChannelMaskState,&cmd,&channels);
    CALL(nvnCommandBufferBindProgram,&cmd,&program,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferBindUniformBuffer,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,(uintptr_t)(storage+0x40000),16);
    CALL(nvnCommandBufferResetCounter,&cmd,NVN_COUNTER_TYPE_SAMPLES_PASSED);
    for(unsigned pass=0;pass<6;pass++) {
      const NVNtexture *target[]={&textures[pass&1]};
      CALL(nvnCommandBufferSetRenderTargets,&cmd,1,target,NULL,NULL,NULL);
      CALL(nvnCommandBufferUpdateUniformBuffer,&cmd,(uintptr_t)(storage+0x40000),16,0,16,colors[pass%3]);
      for(unsigned draw=0;draw<draws_per_pass;draw++) CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
      if(pass==1) CALL(nvnCommandBufferReportCounter,&cmd,NVN_COUNTER_TYPE_SAMPLES_PASSED,(uintptr_t)reports);
      if(pass==2) {
        CALL(nvnCommandBufferSignalEvent,&cmd,&event,NVN_EVENT_SIGNAL_MODE_WRITE,NVN_EVENT_SIGNAL_LOCATION_BOTTOM,0,5);
        CALL(nvnCommandBufferResetCounter,&cmd,NVN_COUNTER_TYPE_SAMPLES_PASSED);
      }
    }
    CALL(nvnCommandBufferReportCounter,&cmd,NVN_COUNTER_TYPE_SAMPLES_PASSED,(uintptr_t)(reports+2));
    CALL(nvnCommandBufferSignalEvent,&cmd,&event,NVN_EVENT_SIGNAL_MODE_WRITE,NVN_EVENT_SIGNAL_LOCATION_BOTTOM,0,9);
    NVNcommandHandle handle=end();
    CALL(nvnQueueSubmitCommands,&queue,1,&handle);
    NVNsync sync;CHECK(CALL(nvnSyncInitialize,&sync,&device));
    CALL(nvnQueueFenceSync,&queue,&sync,NVN_SYNC_CONDITION_ALL_GPU_COMMANDS_COMPLETE,1);
    NVNsyncWaitResult waited=CALL(nvnSyncWait,&sync,UINT64_MAX);
    CHECK(waited==NVN_SYNC_WAIT_RESULT_ALREADY_SIGNALED || waited==NVN_SYNC_WAIT_RESULT_CONDITION_SATISFIED);
    CALL(nvnQueueFinish,&queue);
    CHECK(mtl_last_fence()==before_fence+1);
    CHECK(reports[0]==2*draws_per_pass*256 && reports[2]==3*draws_per_pass*256);
    CHECK(reports[1]>0 && reports[3]>=reports[1]);
    CHECK(CALL(nvnEventGetValue,&event)==9);
    uint64_t splits;int actual;vk_stream_stats(&splits,&boundaries,&batched,&actual);
    // More than four natural boundaries must reach the GPU. Each pass exceeds
    // the chunk quota while its query is still open; prefix values above prove
    // those scopes were not cut merely to reach the quota.
    CHECK(actual==reference && (reference?splits==split0:splits-split0>=5));
    NVNcopyRegion region={0,0,0,16,16,1};uint8_t pixels[16*16*4];
    for(unsigned i=0;i<2;i++) {
      CALL(nvnTextureReadTexels,&textures[i],NULL,&region,pixels);
      for(unsigned p=0;p<256;p++) CHECK(!memcmp(pixels+p*4,i?(uint8_t[]){0,0,255,255}:(uint8_t[]){0,255,0,255},4));
    }
    CALL(nvnSyncFinalize,&sync);
    LOG("[vulkan streaming fixture] reference=%d internal_splits=%llu one guest fence, counter prefixes/reset, GPU updates, final event and all pixels passed",
        reference,(unsigned long long)(splits-split0));
  }
  CALL(nvnEventFinalize,&event);CALL(nvnProgramFinalize,&program);
  for(unsigned i=0;i<2;i++) CALL(nvnTextureFinalize,&textures[i]);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);
}
