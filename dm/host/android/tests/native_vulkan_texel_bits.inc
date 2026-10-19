// Typed SFLOAT fetches can feed floatBitsToUint in real shaders. Compare the
// actual base and alternative GPU results, including exceptional encodings;
// do not assume an implementation preserves every NaN payload from memory.
static void texel_storage_bits_test(const void *vertex) {
  static const uint32_t patterns[]={
    0x00000000,0x80000000,0x3f800000,0xbf800000, // signed zero and ordinary
    0x7f800000,0xff800000,0x7fc00000,0xffc00000, // infinities and quiet NaNs
    0x7fc12345,0xffc54321,0x7f800001,0xff800001, // NaN payloads/signaling
    0x00000001,0x80000001,0x007fffff,0x807fffff, // subnormal extremes
    0x00800000,0x80800000,0x7f7fffff,0xff7fffff, // normal range boundaries
    0xffffffff,0x01234567,0x89abcdef,0xdeadbeef}; // packed flags/ordinary
  vk_state saved=S;uint8_t control[0x880],copy_control[0x880];
  memset(control,54,sizeof control);memset(copy_control,26,sizeof copy_control);
  vk_texel_storage_enabled(1);vk_shader *base=vk_shader_get(control),*alt=base->texel_storage_alternative;
  CHECK(alt && alt->module!=base->module && alt->descriptors!=base->descriptors);
  CHECK(!base->specialization_module && !alt->specialization_module);
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,1<<20));memset(storage,0,1<<20);
  uint32_t words[2][64];
  for(unsigned n=0;n<64;n++) {
    words[0][n]=patterns[n%(sizeof patterns/sizeof *patterns)];
    words[1][n]=patterns[(n*7+3)%(sizeof patterns/sizeof *patterns)];
  }
  memcpy(storage+0x20000,words[0],sizeof words[0]);memcpy(storage+0x30000,words[1],sizeof words[1]);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtexture texture[2];
  for(unsigned i=0;i<2;i++) {
    NVNtextureBuilder b;CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,i?NVN_TEXTURE_TARGET_2D:NVN_TEXTURE_TARGET_BUFFER);
    CALL(nvnTextureBuilderSetWidth,&b,16);if(i)CALL(nvnTextureBuilderSetHeight,&b,4);
    CALL(nvnTextureBuilderSetFormat,&b,i?NVN_FORMAT_RGBA32UI:NVN_FORMAT_RGBA32F);
    CALL(nvnTextureBuilderSetStorage,&b,&pool,i?0x40000:0);CHECK(CALL(nvnTextureInitialize,&texture[i],&b));
  }
  NVNprogram graphics,compute;NVNshaderData stages[]={{.control=vertex},{.control=control}},cs={.control=copy_control};
  CHECK(CALL(nvnProgramInitialize,&graphics,&device));CHECK(CALL(nvnProgramSetShaders,&graphics,2,stages));
  CHECK(CALL(nvnProgramInitialize,&compute,&device));CHECK(CALL(nvnProgramSetShaders,&compute,1,&cs));
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);
  CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);NVNchannelMaskState channels;CALL(nvnChannelMaskStateSetDefaults,&channels);
  enum { slot=62 };mtl_set_tex_pool_n(256);mtl_reg_tex(slot,(const tex_t *)&texture[0],NULL);
  p_draw draw={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};
  for(unsigned prepare=1;prepare<=2;prepare++) {
    vk_graphics_prepare_mode(prepare);mtl_sub_begin();mtl_sub_copy((uintptr_t)storage+0x20000,(uintptr_t)storage,sizeof words[0]);
    S=(vk_state){.program=(const prog_t *)&graphics,.depth_range={0,1},.chmask={~0u}};
    S.poly=*(const poly_st *)&polygon;S.ds=*(const ds_st *)&ds;S.color=*(const color_st *)&color;
    S.targets=(p_set_rt){.n=1,.color={(const tex_t *)&texture[1]}};
    S.has_viewport=S.has_scissor=1;S.viewport[0][2]=16;S.viewport[0][3]=4;S.tex[1][0]=slot;
    vk_pipeline_state_changed();
    for(unsigned phase=0;phase<4;phase++) {
      LOG("[vulkan texel bits fixture] prepare=%u phase=%u",prepare,phase);
      if(phase==2) {
        S.program=(const prog_t *)&compute;
        S.ssbo[5][0]=(p_bind_buf){.stage=5,.index=0,.addr=(uintptr_t)storage+0x30000,.size=sizeof words[1]};
        S.ssbo[5][1]=(p_bind_buf){.stage=5,.index=1,.addr=(uintptr_t)storage,.size=sizeof words[1]};
        vk_pipeline_state_changed();p_dispatch dispatch={1,1,1,0};vk_dispatch(&dispatch);
        S.program=(const prog_t *)&graphics;vk_pipeline_state_changed();
      }
      vk_texel_storage_enabled(phase&1);S.scissor=(p_scissor){0,phase,16,1};vk_draw(&draw,NULL);
    }
    vk_internal_wait(vk_internal_commit());
    uint32_t output[4][64];NVNcopyRegion region={0,0,0,16,4,1};CALL(nvnTextureReadTexels,&texture[1],NULL,&region,output);
    for(unsigned source=0;source<2;source++) {
      unsigned changed=0,mismatch=0;for(unsigned n=0;n<64;n++) {
        if(output[source*2][n]!=words[source][n])changed++;
        if(output[source*2][n]!=output[source*2+1][n]) {
          if(mismatch<16)LOG("[vulkan texel bits mismatch] prepare=%u source=%u word=%u input=%#010x base=%#010x alternative=%#010x",
                            prepare,source,n,words[source][n],output[source*2][n],output[source*2+1][n]);
          mismatch++;
        }
      }
      LOG("[vulkan texel bits fixture] prepare=%u source=%u base_input_changed_words=%u alternative_mismatches=%u",prepare,source,changed,mismatch);
      CHECK(mismatch==0); // Base output is the exact behavioral reference.
    }
  }
  vk_texel_storage_enabled(0);vk_graphics_prepare_mode(2);
  CALL(nvnProgramFinalize,&graphics);CALL(nvnProgramFinalize,&compute);
  for(unsigned n=0;n<2;n++)CALL(nvnTextureFinalize,&texture[n]);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);S=saved;vk_pipeline_state_changed();
  LOG("[vulkan texel bits fixture] floatBitsToUint integer attachments, NaN payloads/infinities/signed-zero/subnormal/boundary words, typed-base comparison and same-pool GPU producer passed");
}
