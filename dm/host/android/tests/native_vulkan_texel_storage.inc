// Actual GPU pixels, same program/slot with changing typed views, real compute
// and transfer producers, both PSO paths and exact Uniform specialization.
static void texel_storage_test(const void *vertex) {
  vk_state saved=S;uint8_t control[0x880],copy_control[0x880];
  memset(control,53,sizeof control);memset(copy_control,26,sizeof copy_control);
  vk_texel_storage_enabled(1);uint64_t shits,spending,slimits,svariants;int spec_enabled;
  vk_uniform_specialization_stats(&shits,&spending,&slimits,&svariants,&spec_enabled);
  vk_uniform_specialization_enabled(1);
  vk_shader *base=vk_shader_get(control),*alt=base->texel_storage_alternative;
  CHECK(alt && alt!=base && alt->module!=base->module && alt->descriptors!=base->descriptors);
  CHECK(base->specialization_module && alt->specialization_module && base->specialization_module!=alt->specialization_module);
  CHECK(base->bindings[0].kind==7 && alt->bindings[0].kind==VK_BINDING_TEXEL_STORAGE);
  CHECK(base->image_read_count==alt->image_read_count);
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,1<<20));memset(storage,0,1<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  const float first[]={.25f,.5f,.75f,1},second[]={.75f,.25f,.5f,1};
  for(unsigned i=0;i<8;i++) {memcpy(storage+0x20000+i*16,first,16);memcpy(storage+0x30000+i*16,second,16);}
  float selector0[4]={0},selector1[4]={.75f,0,0,0};memcpy(storage+0x10000,selector0,16);memcpy(storage+0x10100,selector1,16);
  NVNtexture texture[3];
  for(unsigned i=0;i<3;i++) {
    NVNtextureBuilder b;CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,i==2?NVN_TEXTURE_TARGET_2D:NVN_TEXTURE_TARGET_BUFFER);
    CALL(nvnTextureBuilderSetWidth,&b,i==2?16:i==1?32:8);
    if(i==2)CALL(nvnTextureBuilderSetHeight,&b,6);
    CALL(nvnTextureBuilderSetFormat,&b,i==2?NVN_FORMAT_RGBA8:i==1?NVN_FORMAT_R32F:NVN_FORMAT_RGBA32F);
    CALL(nvnTextureBuilderSetStorage,&b,&pool,i==2?0x40000:0);
    CHECK(CALL(nvnTextureInitialize,&texture[i],&b));
  }
  NVNprogram graphics,compute;NVNshaderData data[]={{.control=vertex},{.control=control}},cs={.control=copy_control};
  CHECK(CALL(nvnProgramInitialize,&graphics,&device));CHECK(CALL(nvnProgramSetShaders,&graphics,2,data));
  CHECK(CALL(nvnProgramInitialize,&compute,&device));CHECK(CALL(nvnProgramSetShaders,&compute,1,&cs));
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);
  CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);NVNchannelMaskState channels;CALL(nvnChannelMaskStateSetDefaults,&channels);
  enum { slot=61 };mtl_set_tex_pool_n(256);p_draw draw={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};
  uint64_t hit0,fall0;vk_texel_storage_stats(&hit0,&fall0);
  for(unsigned prepare=1;prepare<=2;prepare++) {
    vk_graphics_prepare_mode(prepare);vk_texel_storage_enabled(1);mtl_sub_begin();
    mtl_sub_copy((uintptr_t)storage+0x20000,(uintptr_t)storage,128);
    S=(vk_state){.program=(const prog_t *)&graphics,.depth_range={0,1},.chmask={~0u}};
    S.poly=*(const poly_st *)&polygon;S.ds=*(const ds_st *)&ds;S.color=*(const color_st *)&color;
    S.targets=(p_set_rt){.n=1,.color={(const tex_t *)&texture[2]}};
    S.has_viewport=S.has_scissor=1;S.viewport[0][2]=16;S.viewport[0][3]=6;
    S.ubo[1][0]=(p_bind_buf){.stage=1,.index=0,.addr=(uintptr_t)storage+0x10000,.size=65536};
    S.tex[1][0]=slot;vk_pipeline_state_changed();
    for(unsigned phase=0;phase<6;phase++) {
      LOG("[vulkan texel storage fixture] prepare=%u phase=%u",prepare,phase);
      if(phase==2) {
        S.program=(const prog_t *)&compute;
        S.ssbo[5][0]=(p_bind_buf){.stage=5,.index=0,.addr=(uintptr_t)storage+0x30000,.size=128};
        S.ssbo[5][1]=(p_bind_buf){.stage=5,.index=1,.addr=(uintptr_t)storage,.size=128};
        vk_pipeline_state_changed();p_dispatch dispatch={1,1,1,0};vk_dispatch(&dispatch);
        S.program=(const prog_t *)&graphics;vk_pipeline_state_changed();
      }
      if(phase==3) {vk_render_end();mtl_sub_copy((uintptr_t)storage+0x20000,(uintptr_t)storage,128);}
      vk_texel_storage_enabled(phase!=4);
      if(phase==5) S.ubo[1][0].addr=(uintptr_t)storage+0x10100;
      mtl_reg_tex(slot,(const tex_t *)&texture[phase==1?1:0],NULL);
      S.scissor=(p_scissor){0,phase,16,1};
      vk_texel_storage_selection choice;
      vk_shader *picked=vk_shader_texel_pick(base,V.props.limits.maxDescriptorSetStorageBuffers,&choice);
      CHECK((picked==alt)==(phase!=1 && phase!=4));
      if(picked==alt) {
        CHECK(choice.count==1 && choice.reads[0].address==(uintptr_t)storage && choice.reads[0].size==128);
        // Retarget after proof: binding must consume the proved old address,
        // not reinterpret a later descriptor as an SSBO binding or NVN slot.
        mtl_reg_tex(slot,(const tex_t *)&texture[1],NULL);
        vk_shader_operation_begin();vk_uniform_offsets offsets;
        CHECK(vk_shader_bindings_selected(alt,0,&offsets,&choice)!=VK_NULL_HANDLE);
        vk_shader_writebacks();
        vk_texel_storage_selection fallback;CHECK(vk_shader_texel_pick(base,0,&fallback)==base);
        mtl_reg_tex(slot,(const tex_t *)&texture[0],NULL);
      }
      uint64_t epoch=vk_pipeline_state_epoch();vk_draw(&draw,NULL);CHECK(vk_pipeline_state_epoch()==epoch);
    }
    vk_internal_wait(vk_internal_commit());
    uint8_t pixels[16*6*4];NVNcopyRegion region={0,0,0,16,6,1};CALL(nvnTextureReadTexels,&texture[2],NULL,&region,pixels);
    for(unsigned y=0;y<6;y++)for(unsigned x=0;x<16;x++) {
      const uint8_t *p=pixels+(y*16+x)*4;
      if(y==1) {
        if(!x)continue; // Original R32 typed OOB alpha follows its format.
        CHECK(abs((int)p[0]-(int)lroundf(first[(x-1)%4]*255))<=1);
        CHECK(p[1]==0&&p[2]==0&&p[3]==255);
      } else if(x>=1 && x<=8) {
        const float *expected=y==2?second:first;
        for(unsigned c=0;c<4;c++)CHECK(abs((int)p[c]-(int)lroundf(expected[y==5&&c<3?2-c:c]*255))<=1);
      } else if(y!=4)CHECK(p[0]==0&&p[1]==0&&p[2]==0&&p[3]==0);
    }
  }
  uint64_t hits,fallback;vk_texel_storage_stats(&hits,&fallback);CHECK(hits>hit0&&fallback>fall0);
  CHECK(alt->specialization_count>=2 || alt->specialization_disabled);
  vk_texel_storage_enabled(0);vk_uniform_specialization_enabled(spec_enabled);vk_graphics_prepare_mode(2);
  CALL(nvnProgramFinalize,&graphics);CALL(nvnProgramFinalize,&compute);
  for(unsigned n=0;n<3;n++)CALL(nvnTextureFinalize,&texture[n]);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);S=saved;vk_pipeline_state_changed();
  LOG("[vulkan texel storage fixture] actual pixels, current format fallback, same-command retarget, GPU copy/compute aliases, guarded bounds and base/spec PSO identities passed");
}
