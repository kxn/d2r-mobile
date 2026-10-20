// Real NVN color writes -> Shadow metadata -> GPU comparison -> original WB.
// CPU reads additionally prove the source stayed R16 and retained its bits.
static void color_comparison_test(void) {
  vk_state saved=S;uint8_t *mem;CHECK(!posix_memalign((void **)&mem,65536,4u<<20));memset(mem,0,4u<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,mem,4u<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNcommandBuffer cb;CHECK(CALL(nvnCommandBufferInitialize,&cb,&device));CALL(nvnCommandBufferAddCommandMemory,&cb,&pool,1u<<20,1u<<20);
  NVNtexturePool tp;NVNsamplerPool sp;
  CHECK(CALL(nvnTexturePoolInitialize,&tp,&pool,2u<<20,32));CHECK(CALL(nvnSamplerPoolInitialize,&sp,&pool,(2u<<20)+65536,32));
  NVNsamplerBuilder sb;NVNsampler samp;CALL(nvnSamplerBuilderSetDefaults,&sb);CALL(nvnSamplerBuilderSetDevice,&sb,&device);
  CALL(nvnSamplerBuilderSetCompare,&sb,NVN_COMPARE_MODE_COMPARE_R_TO_TEXTURE,NVN_COMPARE_FUNC_LESS);
  CHECK(CALL(nvnSamplerInitialize,&samp,&sb));CALL(nvnSamplerPoolRegisterSampler,&sp,1,&samp);
  for(unsigned variant=0;variant<3;variant++) {
    int native=variant==1,cube=variant==2;
    uint8_t control[0x880];memset(control,cube?82:81,sizeof control);NVNshaderData shader={.control=control};NVNprogram program;
    CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,1,&shader));
    NVNtextureBuilder tb;NVNtexture tex;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
    CALL(nvnTextureBuilderSetTarget,&tb,cube?NVN_TEXTURE_TARGET_CUBEMAP:NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&tb,16);CALL(nvnTextureBuilderSetHeight,&tb,16);
    CALL(nvnTextureBuilderSetFormat,&tb,native?NVN_FORMAT_DEPTH16:NVN_FORMAT_R16);CALL(nvnTextureBuilderSetStorage,&tb,&pool,0);
    CHECK(CALL(nvnTextureInitialize,&tex,&tb));CALL(nvnTexturePoolRegisterTexture,&tp,1,&tex,NULL);
    NVNseparateTextureHandle th=CALL(nvnDeviceGetSeparateTextureHandle,&device,1);NVNseparateSamplerHandle sh=CALL(nvnDeviceGetSeparateSamplerHandle,&device,1);
    for(unsigned phase=0;phase<3;phase++) {
      CALL(nvnCommandBufferBeginRecording,&cb);
      if(phase<2) {NVNcopyRegion r={0,0,0,16,16,cube?6:1};float v[4]={phase?.75f:.25f,0,0,0};CALL(nvnCommandBufferClearTexture,&cb,&tex,NULL,&r,v,15);}
      CALL(nvnCommandBufferSetTexturePool,&cb,&tp);CALL(nvnCommandBufferSetSamplerPool,&cb,&sp);
      CALL(nvnCommandBufferBindProgram,&cb,&program,NVN_SHADER_STAGE_COMPUTE_BIT);
      CALL(nvnCommandBufferBindSeparateTextures,&cb,NVN_SHADER_STAGE_COMPUTE,0,1,&th);
      CALL(nvnCommandBufferBindSeparateSamplers,&cb,NVN_SHADER_STAGE_COMPUTE,0,1,&sh);
      CALL(nvnCommandBufferBindStorageBuffer,&cb,NVN_SHADER_STAGE_COMPUTE,0,(uintptr_t)mem+(3u<<20),4);
      CALL(nvnCommandBufferDispatchCompute,&cb,1,1,1);submit(CALL(nvnCommandBufferEndRecording,&cb));
      CHECK(*(uint32_t *)(mem+(3u<<20))==(phase?255u:0u));
      if(!native) {
        NVNcopyRegion r={0,0,0,16,16,1};uint16_t pixels[256];CALL(nvnTextureReadTexels,&tex,NULL,&r,pixels);
        for(unsigned k=0;k<256;k++)CHECK(abs((int)pixels[k]-(phase?49151:16384))<=1);
      }
    }
    CALL(nvnTextureFinalize,&tex);CALL(nvnProgramFinalize,&program);
  }
  CALL(nvnSamplerFinalize,&samp);CALL(nvnTexturePoolFinalize,&tp);CALL(nvnSamplerPoolFinalize,&sp);
  CALL(nvnCommandBufferFinalize,&cb);CALL(nvnMemoryPoolFinalize,&pool);free(mem);S=saved;
  CHECK(vk_validation_error_count()==0);
  printf("{\"test\":\"NVN-color-depth-comparison\",\"GPU_R16_D16_compare\":true,\"source_bits_preserved\":true,\"GPU_write_refresh\":true,\"native_depth_preserved\":true,\"checks\":%u,\"result\":\"passed\"}\n",checks);
}
