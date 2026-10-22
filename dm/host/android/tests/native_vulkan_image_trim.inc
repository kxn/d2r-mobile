// Actual CPU uploads, GPU clear/copy writes, completed ownership and GPU pixels
// after eviction/recreation. No synthetic clock or fake completion watermark.
#ifdef DM_VULKAN_TEST_EMBED
extern int vk_test_texture_rebuildable(uint64_t);
extern void vk_test_texture_budget(uint64_t,uint64_t *);
extern void vk_texture_cache_stats(uint64_t *,uint64_t *,uint64_t *,uint64_t *);
static void image_trim_probe(NVNtexture *texture,int id) {
  mtl_reg_tex(id,(const tex_t *)texture,NULL);
  VkImageLayout layout;
  vk_texture_shader_begin();CHECK(vk_texture_view_binding(id,0,VK_SHADER_STAGE_FRAGMENT_BIT,&layout));
  vk_texture_shader_barrier();vk_texture_shader_record();
}
static void image_trim_pixels(NVNtexture *texture,unsigned level,unsigned layer,const uint8_t color[4]) {
  unsigned width=16>>level;uint8_t pixels[16*16*4];
  NVNcopyRegion region={0,0,0,width,width,1};
  mtl_read_texels((const tex_t *)texture,level,layer,&region,pixels,0,0);
  for(unsigned k=0;k<width*width;k++)CHECK(!memcmp(pixels+k*4,color,4));
}
static void image_pressure_trim_test(void) {
#if !defined(DM_ANDROID_APP)
  // Observe the real startup policy before this fixture overrides it with 0.
  uint64_t default_total,default_rebuildable,default_budget,default_released;
  vk_texture_cache_stats(&default_total,&default_rebuildable,&default_budget,&default_released);
  CHECK(default_budget==(64ull<<20));
  printf("{\"test_details\":\"private-image-cache-default-budget\",\"budget_bytes\":%llu}\n",
         (unsigned long long)default_budget);
#endif
  enum {stride=65536,pool_size=8*stride,id=80};
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,pool_size));
  memset(storage,0,pool_size);
  NVNmemoryPoolBuilder pb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&pb);CALL(nvnMemoryPoolBuilderSetDevice,&pb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&pb,storage,pool_size);
  CALL(nvnMemoryPoolBuilderSetFlags,&pb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&pb));
  NVNtexture textures[5];
  const uint8_t seed[4]={31,73,127,255},clear[4]={0,255,0,255},copy[4]={255,0,0,255};
  for(unsigned n=0;n<5;n++) {
    NVNtextureBuilder b;CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,n?NVN_TEXTURE_TARGET_2D:NVN_TEXTURE_TARGET_2D_ARRAY);
    CALL(nvnTextureBuilderSetWidth,&b,16);CALL(nvnTextureBuilderSetHeight,&b,16);
    if(!n)CALL(nvnTextureBuilderSetDepth,&b,2);
    CALL(nvnTextureBuilderSetLevels,&b,3);CALL(nvnTextureBuilderSetFormat,&b,NVN_FORMAT_RGBA8);
    NVNpackagedTextureLayout packaged={{0}};CALL(nvnTextureBuilderSetPackagedTextureLayout,&b,&packaged);
    CHECK(CALL(nvnTextureBuilderGetStorageSize,&b)<=stride);
    CALL(nvnTextureBuilderSetStorage,&b,&pool,n*stride);CHECK(CALL(nvnTextureInitialize,&textures[n],&b));
    const tex_t *t=(const tex_t *)&textures[n];
    for(unsigned level=0;level<3;level++)for(unsigned layer=0;layer<(n?1:2);layer++) {
      unsigned width=16>>level;uint8_t pixels[16*16*4];
      for(unsigned k=0;k<width*width;k++)memcpy(pixels+k*4,seed,4);
      CHECK(!nvn_swizzle(&t->d,level,layer,pixels,width*4,t->mem));
    }
  }
  mtl_set_tex_pool_n(256);mtl_sub_begin();
  for(unsigned n=0;n<5;n++)image_trim_probe(&textures[n],id+n);
  CHECK(!vk_texture_trim_idle()); // recording borrows physical resources
  vk_internal_wait(vk_internal_commit());
  for(unsigned n=0;n<5;n++)CHECK(vk_test_texture_rebuildable(((const tex_t *)&textures[n])->serial)==1);
  CHECK(!vk_texture_trim_idle()); // completed but still recently used
  // Raw-handle exposure is sticky across later full CPU uploads. A borrower
  // can write through the saved physical handle without an access callback.
  mtl_sub_begin();CHECK(vk_texture_native((const tex_t *)&textures[4]));
  vk_internal_wait(vk_internal_commit());
  const tex_t *exposed=(const tex_t *)&textures[4];
  dm_guest_write_prepare(exposed->mem,exposed->L.size);
  uint8_t changed[16*16*4];const uint8_t raw_color[4]={63,73,127,255};
  for(unsigned level=0;level<3;level++) {
    unsigned width=16>>level;
    for(unsigned k=0;k<width*width;k++)memcpy(changed+k*4,raw_color,4);
    CHECK(!nvn_swizzle(&exposed->d,level,0,changed,width*4,exposed->mem));
  }
  mtl_sub_begin();image_trim_probe(&textures[4],id+4);vk_internal_wait(vk_internal_commit());
  CHECK(!vk_test_texture_rebuildable(exposed->serial));
  // A render-target write must retain its GPU-only result, not recreate seed.
  CHECK(CALL(nvnCommandBufferInitialize,&cmd,&device));
  CALL(nvnCommandBufferAddCommandMemory,&cmd,&pool,6*stride,stride);
  CALL(nvnCommandBufferBeginRecording,&cmd);
  const NVNtexture *targets[]={&textures[1]};
  CALL(nvnCommandBufferSetRenderTargets,&cmd,1,targets,NULL,NULL,NULL);
  CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);
  CALL(nvnCommandBufferClearColor,&cmd,0,(float[]){0,1,0,1},15);submit(end());
  // A buffer-to-image write also revokes CPU reconstruction, including when
  // other mips still came from the packaged CPU source.
  dm_guest_write_prepare(storage+5*stride,16*16*4);
  for(unsigned k=0;k<16*16;k++)memcpy(storage+5*stride+k*4,copy,4);
  mtl_sub_begin();p_copy_b2t transfer={.src=(uintptr_t)storage+5*stride,
    .tex=(const tex_t *)&textures[2],.r={0,0,0,16,16,1}};
  vk_texture_copy_b2t(&transfer,0,0);vk_internal_wait(vk_internal_commit());
  CHECK(!vk_test_texture_rebuildable(((const tex_t *)&textures[1])->serial));
  CHECK(!vk_test_texture_rebuildable(((const tex_t *)&textures[2])->serial));
  for(unsigned n=0;n<257;n++) {mtl_sub_begin();vk_internal_wait(vk_internal_commit());}
  // Keep one CPU-authored texture recent; the logical descriptor remains valid
  // for both it and the cold texture that will disappear physically.
  mtl_sub_begin();image_trim_probe(&textures[3],id+3);vk_internal_wait(vk_internal_commit());
  uint32_t count=vk_texture_count();
  if(getenv("DM_IMAGE_CACHE_BUDGET_FIXTURE")) {
    uint64_t before,after;
    vk_test_texture_budget(0,&before);CHECK(before>0);
    // Internal reopen is deliberately inert even though the real GPU is done,
    // the cache is over budget and the CPU-backed texture is old enough.
    mtl_sub_begin();CHECK(vk_texture_count()==count);
    vk_texture_budget_maintain();CHECK(vk_texture_count()==count);
    vk_internal_wait(vk_internal_commit());
    // The actual next foreground guest-submit boundary runs maintenance;
    // no Android trim callback, synthetic GPU clock or finish is used there.
    mtl_sub_guest_begin();CHECK(vk_texture_count()==count-1);
    vk_texture_budget_maintain();CHECK(vk_texture_count()==count-1);
    vk_internal_wait(vk_internal_commit());
    vk_test_texture_budget(0,&after);CHECK(after<before);
    CHECK(vk_texture_trim_idle()==0);
  } else CHECK(vk_texture_trim_idle()==1);
  CHECK(vk_texture_count()==count-1);
  CHECK(vk_test_texture_rebuildable(((const tex_t *)&textures[0])->serial)==-1);
  CHECK(vk_test_texture_rebuildable(((const tex_t *)&textures[3])->serial)==1);
  mtl_sub_begin();image_trim_probe(&textures[0],id);vk_internal_wait(vk_internal_commit());
  CHECK(vk_texture_count()==count); // same logical descriptor recreates backing
  for(unsigned level=0;level<3;level++)for(unsigned layer=0;layer<2;layer++)
    image_trim_pixels(&textures[0],level,layer,seed);
  image_trim_pixels(&textures[1],0,0,clear);image_trim_pixels(&textures[2],0,0,copy);
  image_trim_pixels(&textures[4],0,0,raw_color);
  image_trim_pixels(&textures[1],1,0,seed);image_trim_pixels(&textures[2],2,0,seed);
  if(getenv("DM_IMAGE_CACHE_BUDGET_FIXTURE")) {
    // Distinct logical/physical images with identical immutable packaged
    // source exercise quota/cursor progress. No fake completion frontier.
    NVNtexture cold[40];uint64_t ignored;
    vk_test_texture_budget(UINT64_MAX,&ignored);
    for(unsigned n=0;n<40;n++) {
      NVNtextureBuilder b;CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
      CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_2D);
      CALL(nvnTextureBuilderSetWidth,&b,16);CALL(nvnTextureBuilderSetHeight,&b,16);
      CALL(nvnTextureBuilderSetLevels,&b,3);CALL(nvnTextureBuilderSetFormat,&b,NVN_FORMAT_RGBA8);
      NVNpackagedTextureLayout layout={{0}};CALL(nvnTextureBuilderSetPackagedTextureLayout,&b,&layout);
      CALL(nvnTextureBuilderSetStorage,&b,&pool,7*stride);CHECK(CALL(nvnTextureInitialize,&cold[n],&b));
      if(!n) {
        const tex_t *t=(const tex_t *)&cold[n];
        for(unsigned level=0;level<3;level++) {
          unsigned width=16>>level;uint8_t pixels[16*16*4];
          for(unsigned k=0;k<width*width;k++)memcpy(pixels+k*4,seed,4);
          CHECK(!nvn_swizzle(&t->d,level,0,pixels,width*4,t->mem));
        }
      }
      mtl_sub_begin();image_trim_probe(&cold[n],id+16+n);vk_internal_wait(vk_internal_commit());
    }
    for(unsigned n=0;n<257;n++){mtl_sub_begin();vk_internal_wait(vk_internal_commit());}
    uint32_t before=vk_texture_count();vk_test_texture_budget(0,&ignored);
    mtl_sub_begin();CHECK(vk_texture_count()==before);
    vk_internal_wait(vk_internal_commit());
    mtl_sub_guest_begin();CHECK(vk_texture_count()==before-4);
    vk_texture_budget_maintain();CHECK(vk_texture_count()==before-4); // borrowed recording
    vk_internal_wait(vk_internal_commit());
    for(unsigned n=0;n<16;n++){mtl_sub_guest_begin();vk_internal_wait(vk_internal_commit());}
    for(unsigned n=0;n<40;n++)CHECK(vk_test_texture_rebuildable(((const tex_t *)&cold[n])->serial)==-1);
    // Logical registration survives eviction and reconstructs the same mip
    // content; GPU-authored siblings are still their clear/copy results.
    mtl_sub_begin();image_trim_probe(&cold[0],id+16);
    VkSemaphore gate=VK_NULL_HANDLE;
    if(vk_async_submissions()&&V.timeline_semaphore)gate=pool_copy_gate();
    uint64_t pending=vk_internal_commit();
    if(gate) {
      CHECK(vk_completed_serial()<pending);
      vk_texture_budget_maintain();
      CHECK(vk_test_texture_rebuildable(((const tex_t *)&cold[0])->serial)==1);
      pool_copy_signal(&gate);
    }
    vk_internal_wait(pending);
    if(gate)vkDestroySemaphore(V.device,gate,NULL);
    for(unsigned level=0;level<3;level++)image_trim_pixels(&cold[0],level,0,seed);
    image_trim_pixels(&textures[1],0,0,clear);image_trim_pixels(&textures[2],0,0,copy);
    for(unsigned n=0;n<40;n++)CALL(nvnTextureFinalize,&cold[n]);
  }
  CHECK(vk_validation_error_count()==0);
  if(getenv("DM_IMAGE_CACHE_BUDGET_FIXTURE")) {
    uint64_t bytes;vk_test_texture_budget(512ull<<20,&bytes);
  }
  CALL(nvnCommandBufferFinalize,&cmd);
  for(unsigned n=0;n<5;n++)CALL(nvnTextureFinalize,&textures[n]);
  mtl_finish();
  if(getenv("DM_IMAGE_CACHE_BUDGET_FIXTURE")) {
    uint64_t total,rebuildable,budget,released;
    vk_texture_shutdown();vk_texture_cache_stats(&total,&rebuildable,&budget,&released);
    CHECK(!total&&!rebuildable&&released>0);
  }
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);
}
#else
static void image_pressure_trim_test(void) {}
#endif
