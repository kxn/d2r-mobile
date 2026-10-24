#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
static int source_group_gpu_test(void) {
  mtl_init();CHECK(!V.options.pool_defer_publication && vk_legacy_astc_copy_mode());
  vk_test_image_source_flow(1);source_flow_observe=0;vk_gpu_profile_enabled(0);vk_sample_liveness_reference(0);
  NVNmemoryPoolBuilder pb;CALL(nvnMemoryPoolBuilderSetDefaults,&pb);CALL(nvnMemoryPoolBuilderSetDevice,&pb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&pb,bytes,4u<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&pb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&memory,&pb));
  CHECK(CALL(nvnCommandBufferInitialize,&cmd,&device));CALL(nvnCommandBufferAddCommandMemory,&cmd,&memory,0,1u<<20);
  NVNtextureBuilder tb;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetWidth,&tb,16);CALL(nvnTextureBuilderSetHeight,&tb,16);CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);
  NVNtexture source,output,packaged;NVNtexture *texture[]={&source,&output,&packaged};
  for(unsigned n=0;n<3;n++) {
    if(n==2) {NVNpackagedTextureLayout layout={{0}};CALL(nvnTextureBuilderSetPackagedTextureLayout,&tb,&layout);}
    CALL(nvnTextureBuilderSetStorage,&tb,&memory,(3u<<20)+n*65536);
    CHECK(CALL(nvnTextureInitialize,texture[n],&tb));
  }
  tex_t *p=(tex_t *)&packaged;CHECK(p->has_layout && p->L.size<=65536);
  dirty_range_fill(p->mem,p->L.size,(const uint8_t[]){0,255,0,255});
  NVNtexturePool tp;CHECK(CALL(nvnTexturePoolInitialize,&tp,&memory,(3u<<20)+196608,4));
  CALL(nvnTexturePoolRegisterTexture,&tp,1,&source,NULL);
  NVNsamplerBuilder sb;NVNsampler sampler;NVNsamplerPool sp;
  CALL(nvnSamplerBuilderSetDefaults,&sb);CALL(nvnSamplerBuilderSetDevice,&sb,&device);
  CALL(nvnSamplerBuilderSetMinMagFilter,&sb,NVN_MIN_FILTER_NEAREST,NVN_MAG_FILTER_NEAREST);
  CALL(nvnSamplerBuilderSetWrapMode,&sb,NVN_WRAP_MODE_CLAMP_TO_EDGE,NVN_WRAP_MODE_CLAMP_TO_EDGE,NVN_WRAP_MODE_CLAMP_TO_EDGE);
  CHECK(CALL(nvnSamplerInitialize,&sampler,&sb));CHECK(CALL(nvnSamplerPoolInitialize,&sp,&memory,(3u<<20)+197632,4));
  CALL(nvnSamplerPoolRegisterSampler,&sp,1,&sampler);
  NVNseparateTextureHandle th=CALL(nvnDeviceGetSeparateTextureHandle,&device,1);
  NVNseparateSamplerHandle sh=CALL(nvnDeviceGetSeparateSamplerHandle,&device,1);
  uint8_t controls[2][0x880];memset(controls[0],3,sizeof controls[0]);memset(controls[1],4,sizeof controls[1]);
  NVNprogram graphics;NVNshaderData sd[2]={{.control=controls[0]},{.control=controls[1]}};
  CHECK(CALL(nvnProgramInitialize,&graphics,&device));CHECK(CALL(nvnProgramSetShaders,&graphics,2,sd));
  NVNprogram alternative;uint8_t alt_control[2][0x880];
  memset(alt_control[0],0,sizeof alt_control[0]);memset(alt_control[1],9,sizeof alt_control[1]);
  NVNshaderData alt_data[2]={{.control=alt_control[0]},{.control=alt_control[1]}};
  CHECK(CALL(nvnProgramInitialize,&alternative,&device));CHECK(CALL(nvnProgramSetShaders,&alternative,2,alt_data));
  float *vertex=(void *)(bytes+(2u<<20));const float vertices[12]={-1,1,0,0,3,1,2,0,-1,-3,0,2};memcpy(vertex,vertices,sizeof vertices);
  uint8_t *upload=bytes+(2u<<20)+65536;
  for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++)memcpy(upload+(y*16+x)*4,(uint8_t[]){x*17,y*17,64,255},4);
  NVNvertexAttribState attrs[2];for(unsigned n=0;n<2;n++) {CALL(nvnVertexAttribStateSetDefaults,&attrs[n]);CALL(nvnVertexAttribStateSetFormat,&attrs[n],NVN_FORMAT_RG32F,n*8);}
  NVNvertexStreamState stream;CALL(nvnVertexStreamStateSetDefaults,&stream);CALL(nvnVertexStreamStateSetStride,&stream,16);
  NVNbufferRange vr={(uintptr_t)vertex,sizeof vertices};NVNcopyRegion region={0,0,0,16,16,1};
  NVNcolorState cs;NVNpolygonState ps;CALL(nvnColorStateSetDefaults,&cs);CALL(nvnPolygonStateSetDefaults,&ps);
  uint64_t *report=(void *)(bytes+(2u<<20)+131072);uint8_t pixels[1024];const NVNtexture *targets[]={&output};
  CALL(nvnCommandBufferBeginRecording,&cmd);CALL(nvnCommandBufferSetCopyRowStride,&cmd,64);
  CALL(nvnCommandBufferCopyBufferToTexture,&cmd,(uintptr_t)upload,&source,NULL,&region,0);
  CALL(nvnCommandBufferSetRenderTargets,&cmd,1,targets,NULL,NULL,NULL);
  CALL(nvnCommandBufferBindProgram,&cmd,&graphics,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
  CALL(nvnCommandBufferBindColorState,&cmd,&cs);CALL(nvnCommandBufferBindPolygonState,&cmd,&ps);
  CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);CALL(nvnCommandBufferBindVertexAttribState,&cmd,2,attrs);
  CALL(nvnCommandBufferBindVertexStreamState,&cmd,1,&stream);CALL(nvnCommandBufferBindVertexBuffers,&cmd,0,1,&vr);
  CALL(nvnCommandBufferBindSeparateTextures,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&th);
  CALL(nvnCommandBufferBindSeparateSamplers,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&sh);
  uint64_t before[3],after[3];vk_test_source_group_stats(before);
  source_group_threshold=before[0];source_group_acted=0;source_group_action=atoi(getenv("DM_SOURCE_GROUP_FIXTURE"));
  source_group_replacement=(const tex_t *)&packaged;
  for(unsigned n=0;n<12;n++) {
    if(source_group_action==5 && (n==4 || n==5))
      CALL(nvnCommandBufferBindProgram,&cmd,n==4?&alternative:&graphics,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
  }
  CALL(nvnCommandBufferResetCounter,&cmd,NVN_COUNTER_TYPE_SAMPLES_PASSED);
  CALL(nvnCommandBufferReportCounter,&cmd,NVN_COUNTER_TYPE_SAMPLES_PASSED,(uintptr_t)report);
  submit(end());CHECK(report[0]==0);vk_test_source_group_stats(after);
  int on=getenv("DM_VULKAN_SOURCE_READ_GROUP") && !strcmp(getenv("DM_VULKAN_SOURCE_READ_GROUP"),"1");
  CHECK(on?after[0]>before[0]:after[0]==before[0]);
  if(on && source_group_action && source_group_action!=5)CHECK(source_group_acted);
  if(on && (source_group_action==1 || source_group_action==2 || source_group_action==4))CHECK(after[2]==before[2]+1);
  if(on && !source_group_action)CHECK(after[1]>before[1] && after[2]==before[2]);
  CALL(nvnTextureReadTexels,&output,NULL,&region,pixels);
  for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++) {
    uint8_t *q=pixels+(y*16+x)*4;
    if(on && (source_group_action==1 || source_group_action==2))CHECK(q[0]==0 && q[1]==255 && q[2]==0 && q[3]==255);
    else CHECK(q[0]==x*17 && q[1]==y*17 && q[2]==64 && q[3]==255);
  }
  if(on && (source_group_action==1 || source_group_action==2)) {
    dm_guest_write_prepare(p->mem,p->L.size);dirty_range_fill(p->mem,p->L.size,(const uint8_t[]){0,0,255,255});
    CALL(nvnCommandBufferBeginRecording,&cmd);CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);submit(end());
    CALL(nvnTextureReadTexels,&output,NULL,&region,pixels);
    for(unsigned n=0;n<256;n++)CHECK(!memcmp(pixels+4*n,(const uint8_t[]){0,0,255,255},4));
  }
  CHECK(vk_validation_error_count()==0);
  LOG("[ordinary group source GPU] action=%d enabled=%d skipped=%llu confirmed=%llu fallback=%llu callback=%d pixels=passed",
    source_group_action,on,(unsigned long long)(after[0]-before[0]),(unsigned long long)(after[1]-before[1]),
    (unsigned long long)(after[2]-before[2]),source_group_acted);
  printf("{\"test\":\"ordinary-group-source\",\"result\":\"passed\",\"game_modules_executed\":false}\n");return 0;
}
#endif
