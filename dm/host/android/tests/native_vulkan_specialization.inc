// Exact selectors must be read for every draw, including unchanged UBO binding.
// A GPU producer keeps the original shader until CPU visibility is proven.
static void uniform_specialization_test(const void *vertex,int raw) {
  uint8_t control[0x880],compute_control[0x880];memset(control,raw?40:39,sizeof control);memset(compute_control,2,sizeof compute_control);
  NVNshaderData shaders[]={{.control=vertex},{.control=control}},cs={.control=compute_control};
  NVNprogram program,compute;CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  CHECK(CALL(nvnProgramInitialize,&compute,&device));CHECK(CALL(nvnProgramSetShaders,&compute,1,&cs));
  vk_shader *shader=vk_shader_get(control);CHECK(shader->specialization_module!=VK_NULL_HANDLE);CHECK(shader->specialization_integer_only==raw);
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,262144));memset(storage,0,262144);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,262144);CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtextureBuilder tb;NVNtexture texture;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&tb,16);CALL(nvnTextureBuilderSetHeight,&tb,16);
  CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);CALL(nvnTextureBuilderSetStorage,&tb,&pool,196608);CHECK(CALL(nvnTextureInitialize,&texture,&tb));
  NVNcommandBuffer local_cmd;CHECK(CALL(nvnCommandBufferInitialize,&local_cmd,&device));
  CALL(nvnCommandBufferAddCommandMemory,&local_cmd,&pool,131072,65536);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);NVNchannelMaskState mask;CALL(nvnChannelMaskStateSetDefaults,&mask);
  const NVNtexture *target[]={&texture};const float viewport[]={0,0,16,16};uint8_t pixels[1024];NVNcopyRegion region={0,0,0,16,16,1};
  vk_state saved=S;uint64_t hits0,pending0,limits0,variants0;int enabled;
  vk_uniform_specialization_stats(&hits0,&pending0,&limits0,&variants0,&enabled);
  for(unsigned step=0;step<24;step++) {
    LOG("[vulkan Uniform specialization fixture] step=%u",step);
    float value=step==0 || step==1 || step==3?.25f:step==2 || step==4?.75f:(step-5)/32.f;
    if(step==6)value=.75f;
    uint32_t encoded=step==0||step==1||step==3||step==9?1:step==2||step==4||step==6?3:100+step;
    if(step==7)encoded=UINT32_C(0x7fc12345);if(step==8)encoded=UINT32_C(0x7f800000);
    if(raw)value=encoded==1?.25f:.75f;
    vk_uniform_specialization_enabled(step!=0 && step!=4);
    dm_guest_write_prepare(storage,256);memset(storage,0,256);memcpy(storage,raw?(const void *)&encoded:(const void *)&value,4);
    if(step==6) {
      dm_guest_write_prepare(storage+65536,256);
      for(unsigned i=0;i<64;i++)((uint32_t *)(storage+65536))[i]=((raw?3:UINT32_C(0x3f400000))-7)*UINT32_C(0xaaaaaaab);
    }
    CALL(nvnCommandBufferBeginRecording,&local_cmd);
    if(step==6) {
      CALL(nvnCommandBufferBindProgram,&local_cmd,&compute,NVN_SHADER_STAGE_COMPUTE_BIT);
      CALL(nvnCommandBufferBindStorageBuffer,&local_cmd,NVN_SHADER_STAGE_COMPUTE,0,(uintptr_t)(storage+65536),256);
      CALL(nvnCommandBufferBindStorageBuffer,&local_cmd,NVN_SHADER_STAGE_COMPUTE,1,(uintptr_t)storage,256);
      CALL(nvnCommandBufferDispatchCompute,&local_cmd,1,1,1);
    }
    CALL(nvnCommandBufferSetRenderTargets,&local_cmd,1,target,NULL,NULL,NULL);CALL(nvnCommandBufferSetViewports,&local_cmd,0,1,viewport);
    CALL(nvnCommandBufferSetScissor,&local_cmd,0,0,16,16);
    CALL(nvnCommandBufferBindDepthStencilState,&local_cmd,&ds);CALL(nvnCommandBufferBindPolygonState,&local_cmd,&polygon);
    CALL(nvnCommandBufferBindColorState,&local_cmd,&color);CALL(nvnCommandBufferBindChannelMaskState,&local_cmd,&mask);
    CALL(nvnCommandBufferBindVertexAttribState,&local_cmd,0,NULL);CALL(nvnCommandBufferBindVertexStreamState,&local_cmd,0,NULL);
    CALL(nvnCommandBufferBindProgram,&local_cmd,&program,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferBindUniformBuffer,&local_cmd,NVN_SHADER_STAGE_FRAGMENT,0,(uintptr_t)storage,65536);
    CALL(nvnCommandBufferDrawArrays,&local_cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);submit(CALL(nvnCommandBufferEndRecording,&local_cmd));
    CALL(nvnTextureReadTexels,&texture,NULL,&region,pixels);
    for(unsigned i=0;i<256;i++) {
      CHECK(abs((int)pixels[i*4]-(int)lroundf(value*255))<=1);
      CHECK(pixels[i*4+1]==(value<.5f?255:0));CHECK(pixels[i*4+2]==(value<.5f?0:255));CHECK(pixels[i*4+3]==255);
    }
  }
  // Same program, source binding and raster state: only the recorded Uniform
  // update changes the selector between two draws in the same submission.
  CALL(nvnCommandBufferBeginRecording,&local_cmd);
  for(unsigned half=0;half<2;half++) {
    float v[4]={half?.75f:.25f,0,0,0};uint32_t words[4]={half?3:1,0,0,0};
    CALL(nvnCommandBufferUpdateUniformBuffer,&local_cmd,(uintptr_t)storage,65536,0,16,raw?(const void *)words:(const void *)v);
    CALL(nvnCommandBufferSetScissor,&local_cmd,half*8,0,8,16);
    CALL(nvnCommandBufferDrawArrays,&local_cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
  }
  submit(CALL(nvnCommandBufferEndRecording,&local_cmd));CALL(nvnTextureReadTexels,&texture,NULL,&region,pixels);
  for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++) {
    const uint8_t *p=pixels+(y*16+x)*4;CHECK(abs((int)p[0]-(x<8?64:191))<=1);
    CHECK(p[1]==(x<8?255:0)&&p[2]==(x<8?0:255)&&p[3]==255);
  }
  uint64_t hits,pending,limits,variants;vk_uniform_specialization_stats(&hits,&pending,&limits,&variants,&enabled);
  CHECK(hits>hits0);
  if(!shader->specialization_disabled) { CHECK(pending>pending0);CHECK(limits>limits0);CHECK(variants-variants0==16); }
  mtl_sub_begin();
  if(!raw)for(unsigned i=0;i<3;i++) {
    const uint32_t exceptional[]={UINT32_C(0x7fc12345),UINT32_C(0x7f800000),1};
    dm_guest_write_prepare(storage,4);memcpy(storage,&exceptional[i],4);
    uint32_t bits;CHECK(vk_uniform_specialization_pick(shader,&bits)==shader->module);
  }
  if(raw && !shader->specialization_disabled) {
    const uint32_t exceptional=UINT32_C(0x7fc12345);uint32_t bits;
    dm_guest_write_prepare(storage,4);memcpy(storage,&exceptional,4);
    CHECK(vk_uniform_specialization_pick(shader,&bits)==shader->specialization_module);CHECK(bits==exceptional);
  }
  p_bind_buf bound=S.ubo[1][shader->specialization_slot];
  S.ubo[1][shader->specialization_slot].addr=UINT64_MAX-65536;
  uint32_t unavailable_bits;CHECK(vk_uniform_specialization_pick(shader,&unavailable_bits)==shader->module);
  S.ubo[1][shader->specialization_slot]=bound;
  vk_internal_wait(vk_internal_commit());
  vk_uniform_specialization_enabled(0);S=saved;vk_pipeline_state_changed();
  CALL(nvnCommandBufferFinalize,&local_cmd);
  CALL(nvnTextureFinalize,&texture);CALL(nvnProgramFinalize,&program);CALL(nvnProgramFinalize,&compute);CALL(nvnMemoryPoolFinalize,&pool);free(storage);
  LOG("[vulkan Uniform specialization fixture] integer_only=%d driver_variant_disabled=%d",raw,shader->specialization_disabled);
  LOG("[vulkan Uniform specialization fixture] exact per-draw values, same-submission updates, full/prepared PSO keys, GPU-producer fallback and bounded variants passed");
}

static void uniform_predicate_fixture_bind(NVNcommandBuffer *cmd,NVNprogram *program,NVNtexture *texture,
    NVNdepthStencilState *ds,NVNpolygonState *polygon,NVNcolorState *color,NVNchannelMaskState *mask,
    uint8_t *storage) {
  const NVNtexture *target[]={texture};const float viewport[]={0,0,16,16};
  CALL(nvnCommandBufferSetRenderTargets,cmd,1,target,NULL,NULL,NULL);
  CALL(nvnCommandBufferSetViewports,cmd,0,1,viewport);CALL(nvnCommandBufferSetScissor,cmd,0,0,16,16);
  CALL(nvnCommandBufferBindDepthStencilState,cmd,ds);CALL(nvnCommandBufferBindPolygonState,cmd,polygon);
  CALL(nvnCommandBufferBindColorState,cmd,color);CALL(nvnCommandBufferBindChannelMaskState,cmd,mask);
  CALL(nvnCommandBufferBindVertexAttribState,cmd,0,NULL);CALL(nvnCommandBufferBindVertexStreamState,cmd,0,NULL);
  CALL(nvnCommandBufferBindProgram,cmd,program,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
  CALL(nvnCommandBufferBindUniformBuffer,cmd,NVN_SHADER_STAGE_FRAGMENT,1,(uintptr_t)storage,65536);
  CALL(nvnCommandBufferBindUniformBuffer,cmd,NVN_SHADER_STAGE_FRAGMENT,2,(uintptr_t)(storage+65536),65536);
}
static void uniform_predicate_fixture_pixel(const uint32_t *p,uint32_t raw,uint32_t extra) {
  uint32_t limit=raw<4?raw:4;
  uint32_t expected=limit*(limit+1)/2+(raw==1?20:raw==2?30:40);
  if(extra&16)expected+=100;if(extra&2)expected+=200;
  CHECK(p[0]==expected);CHECK(p[1]==raw);
  // Fixture inputs are ordinary positive0.5/1.x floats. Multiplication by2
  // is exact and increments the exponent, retaining every mantissa flag bit.
  CHECK(p[2]==extra+UINT32_C(0x00800000));CHECK(p[3]==extra);
}
// Observe real driver creation arguments; fault mode deliberately rejects the
// first hybrid and old-spec creates before running the original GPU oracle.
static PFN_vkCreateGraphicsPipelines predicate_stage_create_original;
static vk_shader *predicate_stage_shader;
static VkShaderModule predicate_stage_vertex;
static unsigned predicate_stage_creates,predicate_stage_faults,predicate_stage_base;
static int predicate_stage_fault_mode;
static VkResult VKAPI_CALL predicate_stage_create(VkDevice device,VkPipelineCache cache,
    uint32_t count,const VkGraphicsPipelineCreateInfo *infos,const VkAllocationCallbacks *allocation,VkPipeline *pipelines) {
  CHECK(count==1);const VkGraphicsPipelineCreateInfo *ci=infos;
  CHECK(ci->stageCount==2 && ci->pStages);CHECK(ci->subpass==0);
  const VkPipelineShaderStageCreateInfo *vs=ci->pStages,*fs=ci->pStages+1;
  CHECK(vs->sType==VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO && !vs->pNext && !vs->flags);
  CHECK(vs->stage==VK_SHADER_STAGE_VERTEX_BIT && vs->module==predicate_stage_vertex);
  CHECK(!vs->pSpecializationInfo && !strcmp(vs->pName,"main"));
  CHECK(fs->sType==VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO && !fs->pNext && !fs->flags);
  CHECK(fs->stage==VK_SHADER_STAGE_FRAGMENT_BIT && !strcmp(fs->pName,"main"));
  vk_shader *s=predicate_stage_shader;predicate_stage_creates++;
  if(fs->module==s->module) {CHECK(!fs->pSpecializationInfo);predicate_stage_base++;}
  else {
    CHECK(fs->module==s->specialization_module || fs->module==s->predicate->module);
    const VkSpecializationInfo *sp=fs->pSpecializationInfo;CHECK(sp && sp->pMapEntries && sp->pData);
    int hybrid=fs->module==s->predicate->module;
    CHECK(sp->mapEntryCount==(hybrid?2:1) && sp->dataSize==(hybrid?8:4));
    CHECK(sp->pMapEntries[0].constantID==s->specialization_id && !sp->pMapEntries[0].offset && sp->pMapEntries[0].size==4);
    if(hybrid)CHECK(sp->pMapEntries[1].constantID==s->predicate->id && sp->pMapEntries[1].offset==4 && sp->pMapEntries[1].size==4);
    if(predicate_stage_fault_mode && predicate_stage_faults<2) {
      CHECK(hybrid==(predicate_stage_faults==0));predicate_stage_faults++;
      pipelines[0]=VK_NULL_HANDLE;return VK_ERROR_UNKNOWN;
    }
  }
  return predicate_stage_create_original(device,cache,count,infos,allocation,pipelines);
}
static void uniform_predicate_test(const void *vertex) {
  vk_state saved=S;uint8_t control[0x880],compute_control[0x880];
  memset(control,56,sizeof control);memset(compute_control,2,sizeof compute_control);
  vk_shader *shader=vk_shader_get(control);CHECK(shader->specialization_module && shader->predicate);
  CHECK(shader->specialization_integer_only);CHECK(shader->specialization_slot==1);
  CHECK(shader->predicate->slot==2 && shader->predicate->offset==320 && shader->predicate->count==2);
  CHECK(shader->predicate->id!=shader->specialization_id);
  CHECK(!predicate_stage_create_original);predicate_stage_create_original=vkCreateGraphicsPipelines;
  predicate_stage_shader=shader;predicate_stage_vertex=vk_shader_get(vertex)->module;
  predicate_stage_creates=predicate_stage_faults=predicate_stage_base=0;
  predicate_stage_fault_mode=getenv("DM_TEST_PSO_STAGE_FAILURE")!=NULL;
  vkCreateGraphicsPipelines=predicate_stage_create;
  uint64_t hits0,pending0,limits0,variants0,rejected0,masks0[16];int enabled0,spec_enabled;
  vk_uniform_predicate_stats(&hits0,&pending0,&limits0,&variants0,&rejected0,masks0,&enabled0);
  uint64_t ignored[4];vk_uniform_specialization_stats(ignored,ignored+1,ignored+2,ignored+3,&spec_enabled);
  vk_uniform_specialization_enabled(1);
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,524288));memset(storage,0,524288);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,524288);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtextureBuilder tb;NVNtexture texture;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&tb,16);CALL(nvnTextureBuilderSetHeight,&tb,16);
  CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA32UI);CALL(nvnTextureBuilderSetStorage,&tb,&pool,327680);
  CHECK(CALL(nvnTextureInitialize,&texture,&tb));
  NVNprogram program,compute;NVNshaderData shaders[]={{.control=vertex},{.control=control}},cs={.control=compute_control};
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  CHECK(CALL(nvnProgramInitialize,&compute,&device));CHECK(CALL(nvnProgramSetShaders,&compute,1,&cs));
  NVNcommandBuffer local;CHECK(CALL(nvnCommandBufferInitialize,&local,&device));
  CALL(nvnCommandBufferAddCommandMemory,&local,&pool,196608,131072);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);
  CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);NVNchannelMaskState mask;CALL(nvnChannelMaskStateSetDefaults,&mask);
  uint32_t pixels[256][4],reference[256][4];NVNcopyRegion region={0,0,0,16,16,1};
  // Sixteen complete {raw,mask} tuples, each with two numeric values sharing
  // its predicate mask. Old and hybrid modules independently render real
  // integer attachments. A frozen numeric load or incomplete PSO key fails.
  for(unsigned tuple=0;tuple<16;tuple++)for(unsigned numeric=0;numeric<2;numeric++) {
    uint32_t raw=tuple/4,m=tuple%4;
    uint32_t extra=(numeric?UINT32_C(0x3f800000):UINT32_C(0x3f000000))|((m&1)?16:0)|((m&2)?2:0);
    dm_guest_write_prepare(storage,4);memcpy(storage,&raw,4);
    dm_guest_write_prepare(storage+65536+320,4);memcpy(storage+65536+320,&extra,4);
    for(unsigned phase=0;phase<2;phase++) {
      unsigned hybrid=predicate_stage_fault_mode?1-phase:phase;
      unsigned creates_before=predicate_stage_creates;
      vk_uniform_predicate_enabled(hybrid);
      CALL(nvnCommandBufferBeginRecording,&local);
      uniform_predicate_fixture_bind(&local,&program,&texture,&ds,&polygon,&color,&mask,storage);
      CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
      submit(CALL(nvnCommandBufferEndRecording,&local));CALL(nvnTextureReadTexels,&texture,NULL,&region,pixels);
      for(unsigned n=0;n<256;n++)uniform_predicate_fixture_pixel(pixels[n],raw,extra);
      if(!phase)memcpy(reference,pixels,sizeof reference);else CHECK(!memcmp(reference,pixels,sizeof pixels));
      // Numeric changes leave the exact raw/mask tuple and static state equal.
      // Existing prepared/full-key hits must not call the driver again.
      if(numeric)CHECK(predicate_stage_creates==creates_before);
    }
  }
  vk_uniform_predicate_enabled(1);
  // Complete tuple17 must retain the old specialization, then an admitted
  // tuple still works after the cap. Neither component may be truncated.
  for(unsigned test=0;test<2;test++) {
    uint32_t raw=test?1:4,extra=UINT32_C(0x3f800012);
    dm_guest_write_prepare(storage,4);memcpy(storage,&raw,4);
    dm_guest_write_prepare(storage+65536+320,4);memcpy(storage+65536+320,&extra,4);
    CALL(nvnCommandBufferBeginRecording,&local);
    uniform_predicate_fixture_bind(&local,&program,&texture,&ds,&polygon,&color,&mask,storage);
    CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    submit(CALL(nvnCommandBufferEndRecording,&local));CALL(nvnTextureReadTexels,&texture,NULL,&region,pixels);
    for(unsigned n=0;n<256;n++)uniform_predicate_fixture_pixel(pixels[n],raw,extra);
  }
  // A real compute producer of either input prevents CPU projection. The
  // extra-word case may retain old.spec; a raw-word producer must retain base.
  for(unsigned source=0;source<2;source++) {
    uint32_t raw=source?2:1,extra=UINT32_C(0x3f800012),produced=source?raw:extra;
    uint32_t stale_raw=source?3:raw,stale_extra=source?extra:UINT32_C(0x3f000000);
    dm_guest_write_prepare(storage,4);memcpy(storage,&stale_raw,4);
    dm_guest_write_prepare(storage+65536+320,4);memcpy(storage+65536+320,&stale_extra,4);
    dm_guest_write_prepare(storage+131072,256);
    for(unsigned n=0;n<64;n++)((uint32_t *)(storage+131072))[n]=(produced-7)*UINT32_C(0xaaaaaaab);
    CALL(nvnCommandBufferBeginRecording,&local);
    CALL(nvnCommandBufferBindProgram,&local,&compute,NVN_SHADER_STAGE_COMPUTE_BIT);
    CALL(nvnCommandBufferBindStorageBuffer,&local,NVN_SHADER_STAGE_COMPUTE,0,(uintptr_t)(storage+131072),256);
    CALL(nvnCommandBufferBindStorageBuffer,&local,NVN_SHADER_STAGE_COMPUTE,1,(uintptr_t)(storage+(source?0:65536+320)),256);
    CALL(nvnCommandBufferDispatchCompute,&local,1,1,1);
    uniform_predicate_fixture_bind(&local,&program,&texture,&ds,&polygon,&color,&mask,storage);
    CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    submit(CALL(nvnCommandBufferEndRecording,&local));CALL(nvnTextureReadTexels,&texture,NULL,&region,pixels);
    for(unsigned n=0;n<256;n++)uniform_predicate_fixture_pixel(pixels[n],raw,extra);
  }
  uint32_t raw=1;dm_guest_write_prepare(storage,4);memcpy(storage,&raw,4);
  CALL(nvnCommandBufferBeginRecording,&local);
  uniform_predicate_fixture_bind(&local,&program,&texture,&ds,&polygon,&color,&mask,storage);
  for(unsigned half=0;half<2;half++) {
    uint32_t extra[4]={half?UINT32_C(0x3f800012):UINT32_C(0x3f000000),0,0,0};
    CALL(nvnCommandBufferUpdateUniformBuffer,&local,(uintptr_t)(storage+65536),65536,320,16,extra);
    CALL(nvnCommandBufferSetScissor,&local,half*8,0,8,16);
    CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
  }
  submit(CALL(nvnCommandBufferEndRecording,&local));CALL(nvnTextureReadTexels,&texture,NULL,&region,pixels);
  for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++)
    uniform_predicate_fixture_pixel(pixels[y*16+x],1,x<8?UINT32_C(0x3f000000):UINT32_C(0x3f800012));
  // No invalid binding is submitted to the GPU: these are functional selector
  // fallback checks after a real complete draw and before restoring the state.
  mtl_sub_begin();uint32_t values[2];p_bind_buf old=S.ubo[1][2];S.ubo[1][2].size=320;
  VkShaderModule picked=vk_uniform_hybrid_pick(shader,values);
  if(!shader->specialization_disabled)CHECK(picked==shader->specialization_module && values[0]==1 && !values[1]);
  S.ubo[1][2]=old;S.ubo[1][2].addr=UINT64_MAX-128;
  picked=vk_uniform_hybrid_pick(shader,values);
  if(!shader->specialization_disabled)CHECK(picked==shader->specialization_module && values[0]==1 && !values[1]);
  S.ubo[1][2]=old;old=S.ubo[1][1];S.ubo[1][1].addr=UINT64_MAX-128;
  CHECK(vk_uniform_hybrid_pick(shader,values)==shader->module);CHECK(!values[0]&&!values[1]);S.ubo[1][1]=old;
  vk_internal_wait(vk_internal_commit());
  uint64_t hits,pending,limits,variants,rejected,masks[16];int enabled;
  vk_uniform_predicate_stats(&hits,&pending,&limits,&variants,&rejected,masks,&enabled);
  if(!shader->specialization_disabled && !shader->predicate->disabled) {
    CHECK(shader->predicate->variants==16);CHECK(variants-variants0==16);CHECK(limits>limits0);CHECK(pending>pending0);
    CHECK(hits>hits0);for(unsigned n=0;n<4;n++)CHECK(masks[n]>masks0[n]);
  }
  vkCreateGraphicsPipelines=predicate_stage_create_original;predicate_stage_create_original=NULL;
  CHECK(predicate_stage_creates>0);
  if(predicate_stage_fault_mode) {CHECK(predicate_stage_faults==2);CHECK(predicate_stage_base>0);CHECK(shader->specialization_disabled);}
  else CHECK(!predicate_stage_faults);
  LOG("[vulkan PSO stage fixture] actual_driver_creates=%u injected_failures=%u base_creates=%u; stage/module/spec arguments and cache-hit GPU pixels passed",
      predicate_stage_creates,predicate_stage_faults,predicate_stage_base);
  LOG("[vulkan Uniform predicate fixture] hits=%llu pending=%llu limits=%llu variants=%llu rejected=%llu disabled=%d",
    (unsigned long long)(hits-hits0),(unsigned long long)(pending-pending0),(unsigned long long)(limits-limits0),
    (unsigned long long)(variants-variants0),(unsigned long long)(rejected-rejected0),shader->predicate->disabled);
  vk_uniform_predicate_enabled(enabled0);vk_uniform_specialization_enabled(spec_enabled);S=saved;vk_pipeline_state_changed();
  CALL(nvnCommandBufferFinalize,&local);CALL(nvnTextureFinalize,&texture);
  CALL(nvnProgramFinalize,&program);CALL(nvnProgramFinalize,&compute);CALL(nvnMemoryPoolFinalize,&pool);free(storage);
  LOG("[vulkan Uniform predicate fixture] complete tuple cap, numeric-use preservation, old/hybrid GPU equality, two producer fallbacks and same-submit updates passed");
}
