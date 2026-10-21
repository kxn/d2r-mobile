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

// Same original NVN/compute oracle, now all three mip/layer views, CPU upload,
// a partial GPU rewrite, real storage promotion and final physical retirement.
extern int vk_test_texture_comparison(uint64_t,uint64_t [8]);
static void color_comparison_array_test(void) {
  vk_state saved=S;uint8_t *mem;CHECK(!posix_memalign((void **)&mem,65536,4u<<20));memset(mem,0,4u<<20);
  uint64_t initial_total,cache,budget,released;
  vk_texture_cache_stats(&initial_total,&cache,&budget,&released);uint64_t initial_cache=cache;
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
  NVNseparateSamplerHandle sh=CALL(nvnDeviceGetSeparateSamplerHandle,&device,1);
  uint8_t control[0x880];memset(control,81,sizeof control);NVNshaderData shader={.control=control};NVNprogram program;
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,1,&shader));
  NVNtextureBuilder tb;NVNtexture tex;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D_ARRAY);CALL(nvnTextureBuilderSetWidth,&tb,16);CALL(nvnTextureBuilderSetHeight,&tb,16);
  CALL(nvnTextureBuilderSetDepth,&tb,3);CALL(nvnTextureBuilderSetLevels,&tb,3);CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_R16);
  NVNpackagedTextureLayout packaged={{0}};CALL(nvnTextureBuilderSetPackagedTextureLayout,&tb,&packaged);
  CHECK(CALL(nvnTextureBuilderGetStorageSize,&tb)<65536);CALL(nvnTextureBuilderSetStorage,&tb,&pool,0);
  CHECK(CALL(nvnTextureInitialize,&tex,&tb));const tex_t *t=(const tex_t *)&tex;uint64_t serial=t->serial;
  NVNtextureView views[9];NVNseparateTextureHandle th[9];
  for(unsigned layer=0;layer<3;layer++)for(unsigned level=0;level<3;level++) {
    unsigned at=layer*3+level,w=16>>level;uint16_t pixels[256];
    for(unsigned k=0;k<w*w;k++)pixels[k]=((layer+level)&1)?49151:16384;
    CHECK(!nvn_swizzle(&t->d,level,layer,pixels,w*2,t->mem));
    CALL(nvnTextureViewSetDefaults,&views[at]);CALL(nvnTextureViewSetLevels,&views[at],level,1);
    CALL(nvnTextureViewSetLayers,&views[at],layer,1);CALL(nvnTextureViewSetTarget,&views[at],NVN_TEXTURE_TARGET_2D);
    CALL(nvnTexturePoolRegisterTexture,&tp,at+1,&tex,&views[at]);th[at]=CALL(nvnDeviceGetSeparateTextureHandle,&device,at+1);
  }
  uint64_t proof[8],child=0;
  for(unsigned phase=0;phase<4;phase++) {
    if(phase==2) {
      // Promote the original physical R16 storage with an already sampled
      // D16 child alive. The retired old parent must not own/free that child.
      mtl_sub_begin();vk_texture_prepare_binding(1,1,VK_SHADER_STAGE_COMPUTE_BIT);
      vk_internal_wait(vk_internal_commit());
    }
    if(phase==3) {
      dm_guest_write_prepare(t->mem,t->L.size);
      for(unsigned layer=0;layer<3;layer++)for(unsigned level=0;level<3;level++) {
        unsigned w=16>>level;uint16_t pixels[256];
        for(unsigned k=0;k<w*w;k++)pixels[k]=((layer+level)&1)?16384:49151;
        CHECK(!nvn_swizzle(&t->d,level,layer,pixels,w*2,t->mem));
      }
    }
    CALL(nvnCommandBufferBeginRecording,&cb);
    if(phase==1) {
      NVNcopyRegion r={0,0,0,8,8,1};float value[4]={.75f,0,0,0};
      CALL(nvnCommandBufferClearTexture,&cb,&tex,&views[4],&r,value,15);
    }
    CALL(nvnCommandBufferSetTexturePool,&cb,&tp);CALL(nvnCommandBufferSetSamplerPool,&cb,&sp);
    CALL(nvnCommandBufferBindProgram,&cb,&program,NVN_SHADER_STAGE_COMPUTE_BIT);
    CALL(nvnCommandBufferBindSeparateSamplers,&cb,NVN_SHADER_STAGE_COMPUTE,0,1,&sh);
    for(unsigned at=0;at<9;at++) {
      CALL(nvnCommandBufferBindSeparateTextures,&cb,NVN_SHADER_STAGE_COMPUTE,0,1,&th[at]);
      CALL(nvnCommandBufferBindStorageBuffer,&cb,NVN_SHADER_STAGE_COMPUTE,0,(uintptr_t)mem+(3u<<20)+at*4,4);
      CALL(nvnCommandBufferDispatchCompute,&cb,1,1,1);
    }
    submit(CALL(nvnCommandBufferEndRecording,&cb));
    for(unsigned at=0;at<9;at++) {
      int high=((at/3+at%3)&1);if((phase==1 || phase==2)&&at==4)high=1;if(phase==3)high=!high;
      CHECK(((uint32_t *)(mem+(3u<<20)))[at]==(high?255u:0u));
      unsigned w=16>>(at%3);uint16_t pixels[256];NVNcopyRegion r={0,0,0,w,w,1};
      CALL(nvnTextureReadTexels,&tex,&views[at],&r,pixels);
      for(unsigned k=0;k<w*w;k++)CHECK(abs((int)pixels[k]-(high?49151:16384))<=1);
    }
    CHECK(vk_test_texture_comparison(serial,proof));
    CHECK(proof[0]==!V.color_r16_comparison);
    if(proof[0]) {CHECK(proof[1]==proof[2] && proof[4]>0);if(child)CHECK(child==proof[6]);child=proof[6];}
    if(proof[0] && (phase==0 || phase==3))CHECK(proof[5]);
    if(proof[0] && (phase==1 || phase==2))CHECK(!proof[5]);
  }
  // Age only through completed original chunks. Internal reopens must not
  // evict; the next real guest boundary performs the bounded cache deletion.
  for(unsigned n=0;n<257;n++){mtl_sub_begin();vk_internal_wait(vk_internal_commit());}
  CHECK(vk_test_texture_comparison(serial,proof));uint64_t physical=proof[3]+proof[4],old_budget=budget,unused;
  vk_texture_cache_stats(&unused,&cache,&budget,&released);uint64_t released_before=released;
  vk_test_texture_budget(1,&unused);
  CALL(nvnCommandBufferBeginRecording,&cb);submit(CALL(nvnCommandBufferEndRecording,&cb));
  CHECK(!vk_test_texture_comparison(serial,proof));
  vk_texture_cache_stats(&unused,&cache,&budget,&released);
  CHECK(unused==initial_total && cache==initial_cache && released-released_before==physical);
  vk_test_texture_budget(old_budget,&unused);
  CALL(nvnCommandBufferBeginRecording,&cb);CALL(nvnCommandBufferSetTexturePool,&cb,&tp);CALL(nvnCommandBufferSetSamplerPool,&cb,&sp);
  CALL(nvnCommandBufferBindProgram,&cb,&program,NVN_SHADER_STAGE_COMPUTE_BIT);
  CALL(nvnCommandBufferBindSeparateSamplers,&cb,NVN_SHADER_STAGE_COMPUTE,0,1,&sh);
  CALL(nvnCommandBufferBindSeparateTextures,&cb,NVN_SHADER_STAGE_COMPUTE,0,1,&th[0]);
  CALL(nvnCommandBufferBindStorageBuffer,&cb,NVN_SHADER_STAGE_COMPUTE,0,(uintptr_t)mem+(3u<<20),4);
  CALL(nvnCommandBufferDispatchCompute,&cb,1,1,1);submit(CALL(nvnCommandBufferEndRecording,&cb));
  CHECK(*(uint32_t *)(mem+(3u<<20))==255 && vk_test_texture_comparison(serial,proof));
  CALL(nvnTextureFinalize,&tex);
  // Retain the original eight translated-guest grace, never advance a fake
  // watermark or assume PoolFinalize immediately destroys private images.
  for(unsigned n=0;n<9;n++) {CALL(nvnCommandBufferBeginRecording,&cb);submit(CALL(nvnCommandBufferEndRecording,&cb));}
  mtl_sub_begin();vk_internal_wait(vk_internal_commit());
  CALL(nvnProgramFinalize,&program);CALL(nvnSamplerFinalize,&samp);
  CALL(nvnTexturePoolFinalize,&tp);CALL(nvnSamplerPoolFinalize,&sp);CALL(nvnCommandBufferFinalize,&cb);
  CALL(nvnMemoryPoolFinalize,&pool);free(mem);S=saved;
  // Original translated grace plus completed GPU refs destroy both parent
  // and child VMA allocations. The child is not a second registry identity.
  CHECK(!vk_test_texture_comparison(serial,proof));
  uint64_t after_total;vk_texture_cache_stats(&after_total,&cache,&budget,&released);CHECK(after_total==initial_total && cache==initial_cache);
  CHECK(vk_validation_error_count()==0);
  printf("{\"test\":\"NVN-color-depth-comparison-array\",\"mips\":3,\"layers\":3,\"GPU_rewrite_CPU_freshness_promotion_retirement\":true,\"checks\":%u,\"result\":\"passed\"}\n",checks);
}
