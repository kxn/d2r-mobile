// Exercise actual prior DS STORE and sampled-read histories in the same
// recording as a union pass. API interception observes dependency scopes,
// never elapsed time. The color and both DS aspects have independent oracles.
static PFN_vkCmdPipelineBarrier pixel_local_depth_barrier_original;
static unsigned pixel_local_depth_write_scopes,pixel_local_depth_war_scopes;
static VKAPI_ATTR void VKAPI_CALL pixel_local_depth_barrier(VkCommandBuffer command,
    VkPipelineStageFlags source,VkPipelineStageFlags target,VkDependencyFlags flags,
    uint32_t memory_count,const VkMemoryBarrier *memory,uint32_t buffer_count,
    const VkBufferMemoryBarrier *buffers,uint32_t image_count,const VkImageMemoryBarrier *images) {
  const VkPipelineStageFlags ds=VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  if((target&ds)==ds)for(unsigned k=0;k<memory_count;k++) {
    if(memory[k].dstAccessMask&VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) {
      pixel_local_depth_write_scopes++;
      if(source&VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)pixel_local_depth_war_scopes++;
    }
  }
  pixel_local_depth_barrier_original(command,source,target,flags,memory_count,memory,
      buffer_count,buffers,image_count,images);
}
static void pixel_local_depth_contents(const NVNtexture *texture,double expected,unsigned stencil,int partial) {
  mtl_sub_begin();VkImage image=vk_texture_native((const tex_t *)texture);vk_buffer *b=vk_temp_buffer(1280);
  VkBufferImageCopy regions[]={
    {.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1},.imageExtent={16,16,1}},
    {.bufferOffset=1024,.imageSubresource={VK_IMAGE_ASPECT_STENCIL_BIT,0,0,1},.imageExtent={16,16,1}}};
  vk_barrier();vkCmdCopyImageToBuffer(V.cmd,image,VK_IMAGE_LAYOUT_GENERAL,b->handle,2,regions);
  vk_internal_wait(vk_internal_commit());
  for(unsigned k=0;k<256;k++) {
    int inside=partial && k%16>=4 && k%16<12 && k/16>=4 && k/16<12;
    uint32_t z;memcpy(&z,b->map+4*k,4);CHECK(fabs((z&0xffffff)/16777215.-(inside?.875:expected))<1e-6);
    CHECK(b->map[1024+k]==(inside?0x79:stencil));
  }
}
static int pixel_local_depth_preflight_test(void) {
  vk_state saved=S;int original_none=V.load_store_op_none;
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,2<<20));memset(storage,0,2<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,2<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtexture textures[8];for(unsigned k=0;k<8;k++) {
    NVNtextureBuilder b;CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&b,16);
    CALL(nvnTextureBuilderSetHeight,&b,16);CALL(nvnTextureBuilderSetLevels,&b,1);
    CALL(nvnTextureBuilderSetFormat,&b,k>=6?NVN_FORMAT_DEPTH24_STENCIL8:k==0?NVN_FORMAT_RGBA16F:k==2?NVN_FORMAT_R11G11B10F:NVN_FORMAT_RGBA8);
    CALL(nvnTextureBuilderSetStorage,&b,&pool,(k==7?6:k)*65536);CHECK(CALL(nvnTextureInitialize,&textures[k],&b));
  }
  NVNprogram producer,consumer,reader,alias,fault_consumer;uint8_t controls[6][0x880];
  unsigned markers[]={180,181,182,9,193,194};for(unsigned k=0;k<6;k++)memset(controls[k],markers[k],sizeof controls[k]);
  NVNshaderData shaders[]={{.control=controls[0]},{.control=controls[1]}};
  CHECK(CALL(nvnProgramInitialize,&producer,&device));CHECK(CALL(nvnProgramSetShaders,&producer,2,shaders));
  shaders[1].control=controls[2];CHECK(CALL(nvnProgramInitialize,&consumer,&device));CHECK(CALL(nvnProgramSetShaders,&consumer,2,shaders));
  shaders[1].control=controls[3];CHECK(CALL(nvnProgramInitialize,&reader,&device));CHECK(CALL(nvnProgramSetShaders,&reader,2,shaders));
  shaders[1].control=controls[4];CHECK(CALL(nvnProgramInitialize,&alias,&device));CHECK(CALL(nvnProgramSetShaders,&alias,2,shaders));
  shaders[1].control=controls[5];CHECK(CALL(nvnProgramInitialize,&fault_consumer,&device));CHECK(CALL(nvnProgramSetShaders,&fault_consumer,2,shaders));
  NVNcommandBuffer local;CHECK(CALL(nvnCommandBufferInitialize,&local,&device));CALL(nvnCommandBufferAddCommandMemory,&local,&pool,1<<20,1<<20);
  NVNvertexAttribState attrs[2];for(unsigned k=0;k<2;k++)CALL(nvnVertexAttribStateSetDefaults,&attrs[k]);
  CALL(nvnVertexAttribStateSetFormat,&attrs[0],NVN_FORMAT_RGB32F,0);CALL(nvnVertexAttribStateSetFormat,&attrs[1],NVN_FORMAT_RG32F,12);
  NVNvertexStreamState stream;CALL(nvnVertexStreamStateSetDefaults,&stream);CALL(nvnVertexStreamStateSetStride,&stream,20);
  float *vertices=(void *)(storage+0x80000);memcpy(vertices,(float[]){-1,-1,0,0,1,3,-1,0,2,1,-1,3,0,0,-1},60);
  NVNbufferRange vb={(uintptr_t)vertices,60};float viewport[]={0,0,16,16};
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState ds,readonly;CALL(nvnDepthStencilStateSetDefaults,&ds);
  CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  readonly=ds;CALL(nvnDepthStencilStateSetDepthTestEnable,&readonly,1);CALL(nvnDepthStencilStateSetDepthFunc,&readonly,NVN_DEPTH_FUNC_ALWAYS);
  CALL(nvnDepthStencilStateSetStencilTestEnable,&readonly,1);CALL(nvnDepthStencilStateSetStencilFunc,&readonly,NVN_FACE_FRONT_AND_BACK,NVN_STENCIL_FUNC_EQUAL);
  CALL(nvnDepthStencilStateSetStencilOp,&readonly,NVN_FACE_FRONT_AND_BACK,NVN_STENCIL_OP_KEEP,NVN_STENCIL_OP_KEEP,NVN_STENCIL_OP_KEEP);
  NVNcolorState cs;CALL(nvnColorStateSetDefaults,&cs);NVNchannelMaskState mask;CALL(nvnChannelMaskStateSetDefaults,&mask);
  const NVNtexture *source[]={&textures[0],&textures[1],&textures[2]},*output[]={&textures[4]},*scratch[]={&textures[5]};
  mtl_set_tex_pool_n(512);samp_par sampler={.minf=1,.magf=1,.aniso=1,.lod_max=1000};mtl_reg_samp(123,&sampler);
  NVNseparateSamplerHandle sh=CALL(nvnDeviceGetSeparateSamplerHandle,&device,123);NVNseparateTextureHandle handles[4];
  for(unsigned k=0;k<4;k++) {mtl_reg_tex(112+k,(const tex_t *)&textures[k],NULL);handles[k]=CALL(nvnDeviceGetSeparateTextureHandle,&device,112+k);}
  mtl_reg_tex(116,(const tex_t *)&textures[6],NULL);mtl_reg_tex(117,(const tex_t *)&textures[7],NULL);
  NVNseparateTextureHandle dh=CALL(nvnDeviceGetSeparateTextureHandle,&device,116),ah=CALL(nvnDeviceGetSeparateTextureHandle,&device,117);
  mtl_sub_begin();for(unsigned k=0;k<4;k++) {vk_pixel_local_image token;CHECK(vk_texture_pixel_local_prepare((const tex_t *)&textures[k],k!=3,&token));}
  vk_texture_prepare_attachment((const tex_t *)&textures[4],NULL);vk_internal_wait(vk_internal_commit());
  const float blue[]={0,0,1,1},history[]={16/255.f,16/255.f,16/255.f,1},black[]={0,0,0,0};
  // NONE=0 is always legal; NONE=1 is tested only when actually enabled.
  for(unsigned none=0;none<=(unsigned)original_none;none++)for(unsigned test=0;test<9;test++) {
    V.load_store_op_none=none;
    CALL(nvnCommandBufferBeginRecording,&local);
    for(unsigned k=0;k<6;k++) {NVNcopyRegion r={0,0,0,16,16,1};CALL(nvnCommandBufferClearTexture,&local,&textures[k],NULL,&r,k==3?history:blue,15);}
    CALL(nvnCommandBufferSetRenderTargets,&local,0,NULL,NULL,&textures[6],NULL);
    CALL(nvnCommandBufferSetScissor,&local,0,0,16,16);CALL(nvnCommandBufferClearDepthStencil,&local,.5,1,0x37,255);
    submit(CALL(nvnCommandBufferEndRecording,&local));
    CALL(nvnCommandBufferBeginRecording,&local);
    CALL(nvnCommandBufferSetViewports,&local,0,1,viewport);CALL(nvnCommandBufferSetScissor,&local,0,0,16,16);
    CALL(nvnCommandBufferSetDepthRange,&local,0,1);CALL(nvnCommandBufferBindPolygonState,&local,&polygon);
    CALL(nvnCommandBufferBindColorState,&local,&cs);CALL(nvnCommandBufferBindChannelMaskState,&local,&mask);
    CALL(nvnCommandBufferBindVertexAttribState,&local,2,attrs);CALL(nvnCommandBufferBindVertexStreamState,&local,1,&stream);CALL(nvnCommandBufferBindVertexBuffers,&local,0,1,&vb);
    CALL(nvnCommandBufferSetStencilMask,&local,NVN_FACE_FRONT_AND_BACK,0);CALL(nvnCommandBufferSetStencilValueMask,&local,NVN_FACE_FRONT_AND_BACK,255);
    CALL(nvnCommandBufferSetStencilRef,&local,NVN_FACE_FRONT_AND_BACK,0x37);
    // Old read-only DS pass still STOREs without NONE. In test1 the old use
    // instead samples DS in FS at the same DS_READ_ONLY image layout.
    CALL(nvnCommandBufferBindDepthStencilState,&local,test==1?&ds:&readonly);
    CALL(nvnCommandBufferSetRenderTargets,&local,1,scratch,NULL,test==1?NULL:&textures[6],NULL);
    CALL(nvnCommandBufferBindProgram,&local,test==1?&reader:&consumer,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferBindSeparateTextures,&local,NVN_SHADER_STAGE_FRAGMENT,0,test==1?1:4,test==1?&dh:handles);
    CALL(nvnCommandBufferBindSeparateSamplers,&local,NVN_SHADER_STAGE_FRAGMENT,0,1,&sh);
    CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    if(test==2) { // A real late writer in this same submission is preserved.
      CALL(nvnCommandBufferSetRenderTargets,&local,0,NULL,NULL,&textures[6],NULL);
      CALL(nvnCommandBufferClearDepthStencil,&local,.25,1,0x53,255);
      CALL(nvnCommandBufferSetStencilRef,&local,NVN_FACE_FRONT_AND_BACK,0x53);
    }
    CALL(nvnCommandBufferBindDepthStencilState,&local,test==4?&readonly:&ds);
    CALL(nvnCommandBufferSetRenderTargets,&local,3,source,NULL,test==4?&textures[6]:NULL,NULL);
    CALL(nvnCommandBufferBindProgram,&local,(test==5 || test==7)?&alias:&producer,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    if(test==5 || test==7)CALL(nvnCommandBufferBindSeparateTextures,&local,NVN_SHADER_STAGE_FRAGMENT,0,1,test==5?&ah:&dh);
    CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    CALL(nvnCommandBufferSetRenderTargets,&local,1,output,NULL,&textures[6],NULL);
    if(test==3) {CALL(nvnCommandBufferClearDepthStencil,&local,.75,1,0x61,255);CALL(nvnCommandBufferSetStencilRef,&local,NVN_FACE_FRONT_AND_BACK,0x61);}
    CALL(nvnCommandBufferClearColor,&local,0,black,15);CALL(nvnCommandBufferBindDepthStencilState,&local,&readonly);
    CALL(nvnCommandBufferBindProgram,&local,test==8?&fault_consumer:&consumer,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferBindSeparateTextures,&local,NVN_SHADER_STAGE_FRAGMENT,0,4,handles);
    CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    if(test==6) { // A later partial writer must invalidate preflight scope.
      CALL(nvnCommandBufferSetRenderTargets,&local,1,output,NULL,&textures[6],NULL);
      CALL(nvnCommandBufferSetScissor,&local,4,4,8,8);
      CALL(nvnCommandBufferClearDepthStencil,&local,.875,1,0x79,255);
    }
    CALL(nvnCommandBufferResetCounter,&local,NVN_COUNTER_TYPE_SAMPLES_PASSED);
    uint64_t plans0,draws0,aborts0;vk_pixel_local_stats(&plans0,&draws0,&aborts0);uint64_t passes=V.passes;
    pixel_local_depth_write_scopes=pixel_local_depth_war_scopes=0;
    pixel_local_depth_barrier_original=vkCmdPipelineBarrier;vkCmdPipelineBarrier=pixel_local_depth_barrier;
    int failed=vk_pixel_local_enabled() && none==0 && test==8;
    if(failed) {CHECK(!pixel_local_create_original);pixel_local_create_original=vkCreateGraphicsPipelines;pixel_local_create_failures=1;vkCreateGraphicsPipelines=pixel_local_create_fail_subpass1;}
    submit(CALL(nvnCommandBufferEndRecording,&local));
    if(failed) {vkCreateGraphicsPipelines=pixel_local_create_original;pixel_local_create_original=NULL;CHECK(pixel_local_create_failures==0);}
    vkCmdPipelineBarrier=pixel_local_depth_barrier_original;
    uint64_t plans,draws,aborts;vk_pixel_local_stats(&plans,&draws,&aborts);
    int eligible=vk_pixel_local_enabled() && test!=3 && test!=5 && test!=7;
    LOG("[pixel-local union DS] none=%u case=%u plans=%llu draws=%llu aborts=%llu passes=%llu ds_write_scopes=%u fs_war_scopes=%u",none,test,
      (unsigned long long)(plans-plans0),(unsigned long long)(draws-draws0),(unsigned long long)(aborts-aborts0),(unsigned long long)(V.passes-passes),pixel_local_depth_write_scopes,pixel_local_depth_war_scopes);
    CHECK(plans-plans0==(unsigned)eligible);CHECK(draws-draws0==(unsigned)(eligible && !failed));CHECK(aborts-aborts0==(unsigned)failed);
    CHECK(V.passes-passes==(unsigned)(1+((eligible && !failed)?1:2)+(test==2 || test==6)));
    if(vk_pixel_local_enabled() && !none && test==0)CHECK(pixel_local_depth_write_scopes>0);
    if(vk_pixel_local_enabled() && !none && test==1)CHECK(pixel_local_depth_war_scopes>0);
    NVNcopyRegion region={0,0,0,16,16,1};uint8_t pixels[1024];CALL(nvnTextureReadTexels,&textures[4],NULL,&region,pixels);
    for(unsigned k=0;k<256;k++)for(unsigned c=0;c<4;c++)CHECK(abs((int)pixels[4*k+c]-(c==3?255:((test==5 || test==7) && c==0)?16:80))<=1);
    pixel_local_depth_contents(&textures[6],test==2?.25:test==3?.75:.5,test==2?0x53:test==3?0x61:0x37,test==6);
    if(test==1) {
      CALL(nvnTextureReadTexels,&textures[5],NULL,&region,pixels);
      for(unsigned k=0;k<256;k++)for(unsigned c=0;c<4;c++)CHECK(abs((int)pixels[4*k+c]-(c==0?128:c==3?255:0))<=1);
    }
    // The external history remains readable; union STORE cannot discard it.
    CALL(nvnTextureReadTexels,&textures[3],NULL,&region,pixels);
    for(unsigned k=0;k<256;k++)for(unsigned c=0;c<4;c++)CHECK(pixels[4*k+c]==(c==3?255:16));
  }
  V.load_store_op_none=original_none;
  for(unsigned k=0;k<8;k++)CALL(nvnTextureFinalize,&textures[k]);
  CALL(nvnProgramFinalize,&producer);CALL(nvnProgramFinalize,&consumer);CALL(nvnProgramFinalize,&reader);CALL(nvnProgramFinalize,&alias);CALL(nvnProgramFinalize,&fault_consumer);
  CALL(nvnCommandBufferFinalize,&local);CALL(nvnMemoryPoolFinalize,&pool);free(storage);S=saved;vk_pipeline_state_changed();
  CHECK(vk_validation_error_count()==0);return 0;
}
