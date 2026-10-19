// The original GLSL kernels provide the oracle: sampled red then src*3+7.
// Exercise actual operation admission and dynamic shader/buffer/descriptor
// changes; no copy of the new eligibility-classification algorithm is used.
static void pool_binding_mask_gpu_test(void) {
  vk_state saved=S;mtl_finish();
  uint64_t saved_window=V.options.pool_window_bytes;
#ifdef DM_VULKAN_TEST_EMBED
  void vk_test_pool_window_bytes(uint64_t);
  vk_test_pool_window_bytes(65536);
#endif
  uint8_t *memory[2];NVNmemoryPool pools_[2];
  for(unsigned k=0;k<2;k++) {
    CHECK(!posix_memalign((void **)&memory[k],65536,512<<10));memset(memory[k],0,512<<10);
    NVNmemoryPoolBuilder b;CALL(nvnMemoryPoolBuilderSetDefaults,&b);
    CALL(nvnMemoryPoolBuilderSetDevice,&b,&device);CALL(nvnMemoryPoolBuilderSetStorage,&b,memory[k],512<<10);
    CALL(nvnMemoryPoolBuilderSetFlags,&b,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
    CHECK(CALL(nvnMemoryPoolInitialize,&pools_[k],&b));
  }
  NVNtexture tex[2];for(unsigned k=0;k<2;k++) {
    NVNtextureBuilder b;CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&b,4);
    CALL(nvnTextureBuilderSetHeight,&b,4);CALL(nvnTextureBuilderSetFormat,&b,NVN_FORMAT_RGBA8);
    CALL(nvnTextureBuilderSetStorage,&b,&pools_[0],k*65536);CHECK(CALL(nvnTextureInitialize,&tex[k],&b));
  }
  uint8_t controls_[2][0x880];NVNprogram programs[2];vk_shader *shaders_[2];
  const unsigned marker[]={23,2};
  for(unsigned k=0;k<2;k++) {
    memset(controls_[k],marker[k],0x880);NVNshaderData code={.control=controls_[k]};
    CHECK(CALL(nvnProgramInitialize,&programs[k],&device));CHECK(CALL(nvnProgramSetShaders,&programs[k],1,&code));
    shaders_[k]=vk_shader_get(controls_[k]);CHECK(shaders_[k]->pool_binding_mask_ready);
  }
  enum {descriptor=79};mtl_set_tex_pool_n(256);
  samp_par sampler={.minf=1,.magf=1,.aniso=1,.lod_max=1000};mtl_reg_samp(descriptor,&sampler);
  // Mode0 preserves the old scalar enumerator only in this owned test; no
  // production toggle exists. Restore the immutable value before returning.
  for(unsigned mode=0;mode<2;mode++) {
    for(unsigned k=0;k<2;k++)shaders_[k]->pool_binding_mask_ready=mode;
    mtl_sub_begin();S=(vk_state){0};
    for(unsigned k=0;k<2;k++) {
      p_clear_tex c={.tex=(const tex_t *)&tex[k],.r={0,0,0,4,4,1},.mask=15,.c.f={0,0,0,1}};
      c.c.f[0]=(k?160.0f:32.0f)/255.0f;vk_texture_clear(&c);
    }
    uint32_t observed[4][64];
    for(unsigned seq=0;seq<4;seq++) {
      unsigned image=seq&1,bank=(seq>>1)&1;
      uint64_t sample=(uintptr_t)memory[bank]+0x20000+seq*512;
      uint64_t result=(uintptr_t)memory[bank^1]+0x30000+seq*512;
      mtl_reg_tex(descriptor,(const tex_t *)&tex[image],NULL);
      S.program=(const prog_t *)&programs[0];S.tex[5][0]=descriptor;S.samp[5][0]=(uint64_t)descriptor<<20;
      S.ssbo[5][0]=(p_bind_buf){.addr=sample,.size=256};
      pool_range_fixture ranges={.capacity=32};vk_shader *list[6]={0};list[5]=shaders_[0];
      CHECK(vk_shader_pool_ranges(list,1,pool_range_collect,&ranges));CHECK(ranges.count==1);
      CHECK(ranges.ranges[0].address==sample && ranges.ranges[0].size==256 && ranges.ranges[0].writable);
      vk_dispatch(&(p_dispatch){1,1,1});
      // A real COMPUTE writer feeds the next kernel in this same recording.
      // Changing the logical bank/offset cannot reuse another draw's range.
      S.program=(const prog_t *)&programs[1];S.ssbo[5][0]=(p_bind_buf){.addr=sample,.size=256};
      S.ssbo[5][1]=(p_bind_buf){.addr=result,.size=256};vk_dispatch(&(p_dispatch){1,1,1});
      initialized_span_gpu_read((uint8_t *)(uintptr_t)result,256,(uint8_t *)observed[seq]);
    }
    vk_internal_wait(vk_internal_commit());
    for(unsigned seq=0;seq<4;seq++)for(unsigned k=0;k<64;k++)CHECK(observed[seq][k]==((seq&1)?160u:32u)*3u+7u);
    // Later CPU edits to a previously GPU-written input must still go through
    // the original dirty upload and must not be hidden by the schema mask.
    uint32_t *src=(uint32_t *)(memory[0]+0x20000),result[64];
    dm_guest_write_prepare(src,256);for(unsigned k=0;k<64;k++)src[k]=k+mode*100;
    mtl_sub_begin();S.program=(const prog_t *)&programs[1];
    S.ssbo[5][0]=(p_bind_buf){.addr=(uintptr_t)src,.size=256};
    S.ssbo[5][1]=(p_bind_buf){.addr=(uintptr_t)memory[1]+0x40000,.size=256};
    vk_dispatch(&(p_dispatch){1,1,1});initialized_span_gpu_read(memory[1]+0x40000,256,(uint8_t *)result);
    vk_internal_wait(vk_internal_commit());for(unsigned k=0;k<64;k++)CHECK(result[k]==(k+mode*100)*3u+7u);
  }
  for(unsigned k=0;k<2;k++) {shaders_[k]->pool_binding_mask_ready=1;CALL(nvnProgramFinalize,&programs[k]);CALL(nvnTextureFinalize,&tex[k]);}
  mtl_set_tex_pool_n(4);for(unsigned k=0;k<2;k++){CALL(nvnMemoryPoolFinalize,&pools_[k]);free(memory[k]);}
#ifdef DM_VULKAN_TEST_EMBED
  vk_test_pool_window_bytes(saved_window);
#else
  (void)saved_window;
#endif
  S=saved;vk_pipeline_state_changed();
  LOG("[vulkan pool binding mask fixture] actual sampled+sampler+SSBO schema, dynamic cross-pool addresses, descriptor retarget, compute RAW and later CPU edits match independent GPU scalar oracle in full-scan/masked modes");
}
