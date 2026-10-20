// Same program/buffer/layout, consecutive draws change only the Uniform byte
// offset. Pixel checks catch stale dynamic offsets even when the immutable set
// is reused, and switching static/dynamic checks both PSO cache paths.
static void dynamic_uniform_variant_test(const void *vertex,unsigned marker) {
  uint8_t control[0x880];memset(control,marker,sizeof control);
  NVNshaderData shaders[]={{.control=vertex},{.control=control}};
  NVNprogram program;CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,786432));memset(storage,0,786432);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,786432);CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtextureBuilder tb;NVNtexture texture;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&tb,16);CALL(nvnTextureBuilderSetHeight,&tb,16);
  CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);CALL(nvnTextureBuilderSetStorage,&tb,&pool,524288);CHECK(CALL(nvnTextureInitialize,&texture,&tb));
  NVNcommandBuffer local_cmd;CHECK(CALL(nvnCommandBufferInitialize,&local_cmd,&device));
  CALL(nvnCommandBufferAddCommandMemory,&local_cmd,&pool,131072,393216);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);NVNchannelMaskState mask;CALL(nvnChannelMaskStateSetDefaults,&mask);
  const NVNtexture *target[]={&texture};const float viewport[]={0,0,16,16};uint8_t pixels[1024];NVNcopyRegion region={0,0,0,16,16,1};

  vk_state saved=S;uint64_t ignored[4];int enabled;
  vk_uniform_specialization_stats(ignored,ignored+1,ignored+2,ignored+3,&enabled);
  vk_uniform_specialization_enabled(0);
  float left=.25f,right=.75f;memcpy(storage,&left,4);memcpy(storage+65536,&right,4);
  uint64_t hits0,misses0;vk_descriptor_content_stats(&hits0,&misses0);
  uint64_t history_hits0,history_misses0,history_bytes;int history_reference;
  vk_descriptor_history_stats(&history_hits0,&history_misses0,&history_bytes,&history_reference);
  uint64_t lookup_hits0,lookup_ranges0,lookup_slow0;int lookup_reference;
  vk_pool_lookup_stats(&lookup_hits0,&lookup_ranges0,&lookup_slow0,&lookup_reference);
  for(unsigned iteration=0;iteration<64;iteration++) {
    vk_dynamic_uniform_narrow_reference(!(iteration&32));
    vk_pool_lookup_reference(!(iteration&8));
    vk_descriptor_history_reference(!(iteration&16));
    vk_descriptor_history_test_hash(!!(iteration&4));
    vk_descriptor_reuse_reference(!(iteration&8));
    vk_descriptor_batch_reference(!(iteration&2));
    vk_descriptor_template_reference(!(iteration&4));
    vk_dynamic_uniform_reference(!(iteration&1));
    CALL(nvnCommandBufferBeginRecording,&local_cmd);
    CALL(nvnCommandBufferSetRenderTargets,&local_cmd,1,target,NULL,NULL,NULL);
    CALL(nvnCommandBufferSetViewports,&local_cmd,0,1,viewport);
    CALL(nvnCommandBufferBindDepthStencilState,&local_cmd,&ds);CALL(nvnCommandBufferBindPolygonState,&local_cmd,&polygon);
    CALL(nvnCommandBufferBindColorState,&local_cmd,&color);CALL(nvnCommandBufferBindChannelMaskState,&local_cmd,&mask);
    CALL(nvnCommandBufferBindVertexAttribState,&local_cmd,0,NULL);CALL(nvnCommandBufferBindVertexStreamState,&local_cmd,0,NULL);
    CALL(nvnCommandBufferBindProgram,&local_cmd,&program,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    for(unsigned draw=0;draw<4;draw++) {
      unsigned half=draw&1;
      CALL(nvnCommandBufferSetScissor,&local_cmd,half*8,0,8,16);
      CALL(nvnCommandBufferBindUniformBuffer,&local_cmd,NVN_SHADER_STAGE_FRAGMENT,0,(uintptr_t)(storage+65536*(half^((iteration>>1)&1))),65536);
      if(marker==47)
        CALL(nvnCommandBufferBindUniformBuffer,&local_cmd,NVN_SHADER_STAGE_FRAGMENT,5,(uintptr_t)(storage+65536*(1^half^((iteration>>1)&1))),65536);
      CALL(nvnCommandBufferDrawArrays,&local_cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    }
    submit(CALL(nvnCommandBufferEndRecording,&local_cmd));CALL(nvnTextureReadTexels,&texture,NULL,&region,pixels);
    for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++) {
      unsigned selection=(x>=8)^((iteration>>1)&1);
      const uint8_t *p=pixels+(y*16+x)*4;CHECK(abs((int)p[0]-(selection?191:64))<=1);
      if(marker==47) {
        CHECK(abs((int)p[1]-(selection?64:191))<=1);
        CHECK(abs((int)p[2]-(selection?64:191))<=1&&p[3]==255);
      } else CHECK(p[1]==(selection?0:255)&&p[2]==(selection?255:0)&&p[3]==255);
    }
    if(marker==47) {
      vk_shader *resolved[6];vk_program_shaders(S.program,resolved);
      CHECK(resolved[1]->dynamic_uniform_count[0]==1&&resolved[1]->dynamic_uniform_count[1]==2);
      CHECK(vk_program_dynamic_uniforms(resolved)==(!(iteration&1)?0:(iteration&32)?2:1));
    }
  }
  uint64_t hits,misses;vk_descriptor_content_stats(&hits,&misses);CHECK(hits>hits0);
  uint64_t history_hits,history_misses;
  vk_descriptor_history_stats(&history_hits,&history_misses,&history_bytes,&history_reference);
  CHECK(history_hits>history_hits0);CHECK(history_misses>history_misses0);
  uint64_t lookup_hits,lookup_ranges,lookup_slow;
  vk_pool_lookup_stats(&lookup_hits,&lookup_ranges,&lookup_slow,&lookup_reference);
  CHECK(lookup_hits>lookup_hits0);CHECK(lookup_slow>lookup_slow0);
  if(marker==47)CHECK(lookup_ranges>lookup_ranges0);
  vk_pool_lookup_reference(0);
  vk_descriptor_history_reference(0);vk_descriptor_history_test_hash(0);
  vk_descriptor_reuse_reference(0);
  vk_descriptor_template_reference(0);
  vk_descriptor_batch_reference(0);
  vk_dynamic_uniform_reference(0);vk_dynamic_uniform_narrow_reference(0);
  vk_uniform_specialization_enabled(enabled);S=saved;vk_pipeline_state_changed();
  CALL(nvnCommandBufferFinalize,&local_cmd);CALL(nvnTextureFinalize,&texture);CALL(nvnProgramFinalize,&program);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);
  LOG("[vulkan dynamic Uniform fixture] marker=%u static/narrow/extended, independent offsets in binding order, A/B/A history collisions and retained allocation passed",marker);
}
static void dynamic_uniform_test(const void *vertex) {
  int original_reference=vk_dynamic_uniform_reference_mode();
  int original_narrow=vk_dynamic_uniform_narrow_reference_mode();
  int original_template=vk_descriptor_template_reference_mode();
  dynamic_uniform_variant_test(vertex,39);
  dynamic_uniform_variant_test(vertex,47);
  // Programs that exceed the complete layout budget fall back before any
  // invalid Vulkan layout is used, even when each stage fits independently.
  vk_shader fake={.dynamic_uniform_count={1,V.props.limits.maxDescriptorSetUniformBuffersDynamic+1}};
  vk_shader *list[6]={0};list[1]=&fake;
  CHECK(vk_program_dynamic_uniforms(list)==1);
  fake.dynamic_uniform_count[0]=fake.dynamic_uniform_count[1];
  CHECK(vk_program_dynamic_uniforms(list)==0);
  // Preserve the requested control for later fixtures. The internal matrix
  // intentionally tests both modes, but must not silently turn a whole-suite
  // static-descriptor comparison back into the default dynamic mode.
  vk_dynamic_uniform_reference(original_reference);
  vk_dynamic_uniform_narrow_reference(original_narrow);
  vk_descriptor_template_reference(original_template);
}

#ifndef DM_ANDROID_APP
// The original NVN draw/readback path, with real A/B/A shaders and full-value
// changes after a hint has been established. This is not a timing benchmark.
static void descriptor_shader_hint_test(const void *vertex) {
  extern unsigned vk_test_descriptor_shader_hint_edges(void);
  extern unsigned vk_test_descriptor_shader_hint_hits(void);
  if(!getenv("DM_DESCRIPTOR_SHADER_HINT_GPU_ONLY"))checks+=vk_test_descriptor_shader_hint_edges();
  uint8_t controls[2][0x880];NVNprogram programs[2];
  for(unsigned n=0;n<2;n++) {
    memset(controls[n],n?47:39,sizeof controls[n]);
    NVNshaderData data[]={{.control=vertex},{.control=controls[n]}};
    CHECK(CALL(nvnProgramInitialize,&programs[n],&device));
    CHECK(CALL(nvnProgramSetShaders,&programs[n],2,data));
  }
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,786432));memset(storage,0,786432);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,786432);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtextureBuilder tb;NVNtexture texture;
  CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&tb,16);
  CALL(nvnTextureBuilderSetHeight,&tb,16);CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);
  CALL(nvnTextureBuilderSetStorage,&tb,&pool,524288);CHECK(CALL(nvnTextureInitialize,&texture,&tb));
  NVNcommandBuffer local;CHECK(CALL(nvnCommandBufferInitialize,&local,&device));
  CALL(nvnCommandBufferAddCommandMemory,&local,&pool,131072,393216);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);
  CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);
  CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNpolygonState poly;CALL(nvnPolygonStateSetDefaults,&poly);CALL(nvnPolygonStateSetCullFace,&poly,NVN_FACE_NONE);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);NVNchannelMaskState mask;CALL(nvnChannelMaskStateSetDefaults,&mask);
  const NVNtexture *targets[]={&texture};float viewport[]={0,0,16,16};
  NVNcopyRegion region={0,0,0,16,16,1};uint8_t pixels[1024];
  vk_state saved=S;uint64_t ignored[4];int specialization;
  vk_uniform_specialization_stats(ignored,ignored+1,ignored+2,ignored+3,&specialization);
  vk_uniform_specialization_enabled(0);
  unsigned initial_hits=vk_test_descriptor_shader_hint_hits();
  for(unsigned iteration=0;iteration<24;iteration++) {
    // Include real same-address CPU changes, static/narrow/extended offsets,
    // both update paths, forced history collisions and disabled history.
    float values[2]={iteration&1?.75f:.25f,iteration&1?.25f:.75f};
    for(unsigned n=0;n<2;n++){dm_guest_write_prepare(storage+n*65536,4);memcpy(storage+n*65536,values+n,4);}
    vk_dynamic_uniform_reference(iteration%3==0);vk_dynamic_uniform_narrow_reference(iteration%3!=2);
    vk_descriptor_history_reference(iteration%6==4);vk_descriptor_history_test_hash(iteration%6==3);
    vk_descriptor_template_reference(iteration&1);vk_descriptor_reuse_reference(iteration%6==5);
    CALL(nvnCommandBufferBeginRecording,&local);
    CALL(nvnCommandBufferSetRenderTargets,&local,1,targets,NULL,NULL,NULL);
    CALL(nvnCommandBufferSetViewports,&local,0,1,viewport);
    CALL(nvnCommandBufferBindDepthStencilState,&local,&ds);CALL(nvnCommandBufferBindPolygonState,&local,&poly);
    CALL(nvnCommandBufferBindColorState,&local,&color);CALL(nvnCommandBufferBindChannelMaskState,&local,&mask);
    CALL(nvnCommandBufferBindVertexAttribState,&local,0,NULL);CALL(nvnCommandBufferBindVertexStreamState,&local,0,NULL);
    for(unsigned draw=0;draw<8;draw++) {
      unsigned which=draw&1,bank=(draw>=4)^((iteration>>1)&1);
      CALL(nvnCommandBufferBindProgram,&local,&programs[which],NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
      CALL(nvnCommandBufferSetScissor,&local,draw*2,0,2,16);
      CALL(nvnCommandBufferBindUniformBuffer,&local,NVN_SHADER_STAGE_FRAGMENT,0,(uintptr_t)(storage+bank*65536),65536);
      CALL(nvnCommandBufferBindUniformBuffer,&local,NVN_SHADER_STAGE_FRAGMENT,5,(uintptr_t)(storage+(1-bank)*65536),65536);
      CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    }
    submit(CALL(nvnCommandBufferEndRecording,&local));
    CALL(nvnTextureReadTexels,&texture,NULL,&region,pixels);
    for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++) {
      unsigned draw=x/2,which=draw&1,bank=(draw>=4)^((iteration>>1)&1);
      unsigned high=values[bank]>.5f;const uint8_t *pixel=pixels+(y*16+x)*4;
      CHECK(abs((int)pixel[0]-(high?191:64))<=1);
      if(which)CHECK(abs((int)pixel[1]-(high?64:191))<=1 && abs((int)pixel[2]-(high?64:191))<=1 && pixel[3]==255);
      else CHECK(pixel[1]==(high?0:255) && pixel[2]==(high?255:0) && pixel[3]==255);
    }
  }
  CHECK(vk_test_descriptor_shader_hint_hits()>initial_hits);
  CHECK(vk_validation_error_count()==0);
  vk_descriptor_history_reference(0);vk_descriptor_history_test_hash(0);
  vk_descriptor_template_reference(0);vk_descriptor_reuse_reference(0);
  vk_dynamic_uniform_reference(0);vk_dynamic_uniform_narrow_reference(0);
  vk_uniform_specialization_enabled(specialization);S=saved;vk_pipeline_state_changed();
  CALL(nvnCommandBufferFinalize,&local);CALL(nvnTextureFinalize,&texture);
  for(unsigned n=0;n<2;n++)CALL(nvnProgramFinalize,&programs[n]);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);
}

#endif
