// Owned Surface/WSI fixture. Original NVN recording, submission, acquisition
// and presentation only; no guest modules, game state or performance timers.
static int android_wsi_scope_test(void) {
  mtl_init();
  CHECK(vk_window_attached());CHECK(V.surface!=VK_NULL_HANDLE);
  CHECK(vk_async_submissions() && V.timeline_semaphore);
  NVNmemoryPoolBuilder mb={0};
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);
  CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,bytes,4<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&memory,&mb));
  CHECK(CALL(nvnCommandBufferInitialize,&cmd,&device));
  CALL(nvnCommandBufferAddCommandMemory,&cmd,&memory,0,1<<20);
  NVNtexture textures[2];NVNtexture *pointers[]={textures,textures+1};
  for(unsigned k=0;k<2;k++) {
    NVNtextureBuilder tb={0};CALL(nvnTextureBuilderSetDefaults,&tb);
    CALL(nvnTextureBuilderSetDevice,&tb,&device);
    CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);
    CALL(nvnTextureBuilderSetWidth,&tb,64);CALL(nvnTextureBuilderSetHeight,&tb,64);
    CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);
    CALL(nvnTextureBuilderSetStorage,&tb,&memory,(1<<20)+k*65536);
    CHECK(CALL(nvnTextureInitialize,&textures[k],&tb));
  }
  NVNwindowBuilder wb={0};NVNwindow window;NVNsync ready;
  CALL(nvnWindowBuilderSetDefaults,&wb);CALL(nvnWindowBuilderSetDevice,&wb,&device);
  CALL(nvnWindowBuilderSetPresentInterval,&wb,0);
  CALL(nvnWindowBuilderSetTextures,&wb,2,pointers);
  CHECK(CALL(nvnWindowInitialize,&window,&wb));
  CHECK(CALL(nvnSyncInitialize,&ready,&device));
  uint8_t *readback=malloc(64*64*4);CHECK(readback!=NULL);
  unsigned completed=0;
  for(unsigned frame=0;frame<6;frame++) {
    int index=-1;
    CHECK(CALL(nvnWindowAcquireTexture,&window,&ready,&index)==NVN_WINDOW_ACQUIRE_TEXTURE_RESULT_SUCCESS);
    CHECK(index==(int)(frame%2));
    CHECK(CALL(nvnSyncWait,&ready,0)==NVN_SYNC_WAIT_RESULT_ALREADY_SIGNALED);
    float color[4]={frame%2?0:1,frame%2?1:0,0,1};
    const NVNtexture *target=&textures[index];
    CALL(nvnCommandBufferBeginRecording,&cmd);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,&target,NULL,NULL,NULL);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,64,64);
    CALL(nvnCommandBufferClearColor,&cmd,0,color,15);
    NVNcommandHandle handle=end();
    CALL(nvnQueueSubmitCommands,&queue,1,&handle);
    CALL(nvnQueuePresentTexture,&queue,&window,index);
    CALL(nvnQueueFinish,&queue);
    // A real present must retain its original NVN source for subsequent reads.
    NVNcopyRegion region={0,0,0,64,64,1};
    CALL(nvnTextureReadTexels,target,NULL,&region,readback);
    for(unsigned p=0;p<64*64;p++) {
      CHECK(readback[p*4]==(frame%2?0:255));
      CHECK(readback[p*4+1]==(frame%2?255:0));
      CHECK(readback[p*4+2]==0);CHECK(readback[p*4+3]==255);
    }
    completed++;
  }
  free(readback);
  uint32_t gw,gh,sw,sh;vk_window_output_sizes(&gw,&gh,&sw,&sh);
  CHECK(sw>0 && sh>0);CHECK(gw==64 && gh==64);
  CHECK(vk_validation_error_count()==0);
  printf("{\"backend\":\"vulkan\",\"test\":\"android-surface-wsi\",\"checks\":%u,\"presents\":%u,\"surface\":[%u,%u],\"source\":[%u,%u],\"last_color\":\"green\",\"validation_enabled\":false,\"result\":\"passed\"}\n",checks,completed,sw,sh,gw,gh);
  fflush(NULL);
  // Keep Surface displayed for the owning controller's screenshot/force-stop.
  return 0;
}
