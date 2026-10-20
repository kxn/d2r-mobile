// The original GLSL kernels provide the oracle: sampled red then src*3+7.
// Exercise actual operation admission and dynamic shader/buffer/descriptor
// changes; no copy of the new eligibility-classification algorithm is used.
// The existing final-schema constructor and production iterator must preserve
// literal binding order, including fallbacks; no duplicated mask algorithm.
static void image_binding_enumeration_test(void) {
#ifdef DM_VULKAN_TEST_EMBED
  void vk_test_shader_image_binding_mask_build(vk_shader *);
  unsigned vk_test_shader_image_binding_indices(const vk_shader *,unsigned *,unsigned,unsigned *);
  vk_binding bindings[129];for(unsigned k=0;k<129;k++)bindings[k]=(vk_binding){.kind=1,.reg=0,.count=1};
  bindings[0]=(vk_binding){.kind=3,.reg=127,.count=1};
  bindings[63]=(vk_binding){.kind=4,.reg=31,.count=1};
  bindings[64]=(vk_binding){.kind=6,.reg=0,.count=1};
  bindings[127]=(vk_binding){.kind=3,.reg=1,.count=1};
  bindings[20]=(vk_binding){.kind=5,.reg=0,.count=1};
  bindings[21]=(vk_binding){.kind=7,.reg=0,.count=1};
  bindings[22]=(vk_binding){.kind=9,.reg=0,.count=1};
  vk_shader shader={.nb=128,.bindings=bindings};unsigned indices[130],visited;
  const unsigned expected[]={0,63,64,127};
  CHECK(!shader.image_binding_mask_ready);
  CHECK(vk_test_shader_image_binding_indices(&shader,indices,130,&visited)==4);CHECK(visited==128);
  for(unsigned k=0;k<4;k++)CHECK(indices[k]==expected[k]);
  vk_test_shader_image_binding_mask_build(&shader);CHECK(shader.image_binding_mask_ready);
  CHECK(vk_test_shader_image_binding_indices(&shader,indices,130,&visited)==4);CHECK(visited==4);
  for(unsigned k=0;k<4;k++)CHECK(indices[k]==expected[k]);
  // Count/invalid register preserve the old full-scan validation path and
  // leave the earlier selected entries in their original order.
  bindings[63].count=2;vk_test_shader_image_binding_mask_build(&shader);CHECK(!shader.image_binding_mask_ready);
  CHECK(vk_test_shader_image_binding_indices(&shader,indices,130,&visited)==4);CHECK(visited==128);
  for(unsigned k=0;k<4;k++)CHECK(indices[k]==expected[k]);bindings[63].count=1;
  const int invalid_regs[]={-1,32};
  for(unsigned q=0;q<2;q++) {
    bindings[63].reg=invalid_regs[q];vk_test_shader_image_binding_mask_build(&shader);CHECK(!shader.image_binding_mask_ready);
    CHECK(vk_test_shader_image_binding_indices(&shader,indices,130,&visited)==4);CHECK(visited==128);
  }
  bindings[63].reg=31;shader.nb=129;vk_test_shader_image_binding_mask_build(&shader);CHECK(!shader.image_binding_mask_ready);
  CHECK(vk_test_shader_image_binding_indices(&shader,indices,130,&visited)==4);CHECK(visited==129);shader.nb=128;
  vk_test_shader_image_binding_mask_build(&shader);CHECK(shader.image_binding_mask_ready);
  // Model the real alternatives' by-value clone followed by final-schema
  // rebuild: converted sources must not inherit the old image index.
  vk_shader clone=shader;vk_binding alternate[128];memcpy(alternate,bindings,sizeof alternate);clone.bindings=alternate;
  alternate[63].kind=VK_BINDING_PIXEL_LOCAL;alternate[64].kind=VK_BINDING_TEXEL_STORAGE;
  vk_test_shader_image_binding_mask_build(&clone);
  CHECK(vk_test_shader_image_binding_indices(&clone,indices,130,&visited)==2);CHECK(visited==2);
  CHECK(indices[0]==0 && indices[1]==127);
  CHECK(vk_test_shader_image_binding_indices(&shader,indices,130,&visited)==4);CHECK(visited==4);
  for(unsigned k=0;k<128;k++)bindings[k]=(vk_binding){.kind=3,.reg=0,.count=1};
  vk_test_shader_image_binding_mask_build(&shader);CHECK(!shader.image_binding_mask_ready);
  CHECK(vk_test_shader_image_binding_indices(&shader,indices,130,&visited)==128);CHECK(visited==128);
  for(unsigned k=0;k<128;k++)CHECK(indices[k]==k);
  for(unsigned k=0;k<128;k++)bindings[k]=(vk_binding){.kind=1,.reg=0,.count=1};
  vk_test_shader_image_binding_mask_build(&shader);CHECK(shader.image_binding_mask_ready);
  CHECK(!vk_test_shader_image_binding_indices(&shader,indices,130,&visited));CHECK(!visited);
  shader.nb=0;vk_test_shader_image_binding_mask_build(&shader);CHECK(!shader.image_binding_mask_ready);
  CHECK(!vk_test_shader_image_binding_indices(&shader,indices,130,&visited));CHECK(!visited);
  shader.nb=1;shader.bindings=NULL;vk_test_shader_image_binding_mask_build(&shader);CHECK(!shader.image_binding_mask_ready);
  LOG("[vulkan image binding enumeration fixture] immutable mixed128 schema checks128->4; clone128->2, all-image contiguous128 and mock/invalid/count fallback preserved");
#endif
}
static void pool_binding_mask_gpu_test(void) {
  image_binding_enumeration_test();
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
  uint8_t controls_[2][0x880];NVNprogram programs[2];vk_shader *shaders_[2];unsigned image_mask_ready[2];
  const unsigned marker[]={23,2};
  for(unsigned k=0;k<2;k++) {
    memset(controls_[k],marker[k],0x880);NVNshaderData code={.control=controls_[k]};
    CHECK(CALL(nvnProgramInitialize,&programs[k],&device));CHECK(CALL(nvnProgramSetShaders,&programs[k],1,&code));
    shaders_[k]=vk_shader_get(controls_[k]);CHECK(shaders_[k]->pool_binding_mask_ready);
    image_mask_ready[k]=shaders_[k]->image_binding_mask_ready;
  }
  enum {descriptor=79};mtl_set_tex_pool_n(256);
  samp_par sampler={.minf=1,.magf=1,.aniso=1,.lod_max=1000};mtl_reg_samp(descriptor,&sampler);
  // Mode0 preserves the old scalar enumerator only in this owned test; no
  // production toggle exists. Restore the immutable value before returning.
  for(unsigned mode=0;mode<2;mode++) {
    for(unsigned k=0;k<2;k++) {shaders_[k]->pool_binding_mask_ready=mode;
      shaders_[k]->image_binding_mask_ready=mode?image_mask_ready[k]:0;}
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
  for(unsigned k=0;k<2;k++) {shaders_[k]->pool_binding_mask_ready=1;shaders_[k]->image_binding_mask_ready=image_mask_ready[k];CALL(nvnProgramFinalize,&programs[k]);CALL(nvnTextureFinalize,&tex[k]);}
  mtl_set_tex_pool_n(4);for(unsigned k=0;k<2;k++){CALL(nvnMemoryPoolFinalize,&pools_[k]);free(memory[k]);}
#ifdef DM_VULKAN_TEST_EMBED
  vk_test_pool_window_bytes(saved_window);
#else
  (void)saved_window;
#endif
  S=saved;vk_pipeline_state_changed();
  LOG("[vulkan pool binding mask fixture] actual sampled+sampler+SSBO schema, dynamic cross-pool addresses, descriptor retarget, compute RAW and later CPU edits match independent GPU scalar oracle in full-scan/masked modes");
}
