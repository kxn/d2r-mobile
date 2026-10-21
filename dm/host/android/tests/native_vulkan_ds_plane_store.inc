// Actual API-parameter oracle plus independent GPU DS bytes. Wrappers exist
// only during this owned fixture, before any ordinary DS passes are created.
typedef struct { VkRenderPass handle; unsigned count; VkAttachmentDescription a[9]; } ds_plane_pass;
static ds_plane_pass ds_plane_passes[256];
static unsigned ds_plane_pass_count,ds_plane_observed;
static int ds_plane_armed,ds_plane_second_readonly;
static int ds_plane_check_load;
static VkImageAspectFlags ds_plane_expected_clear;
static VkAttachmentStoreOp ds_plane_expected_depth,ds_plane_expected_stencil;
static PFN_vkCreateRenderPass ds_plane_create_original;
static PFN_vkCmdBeginRenderPass ds_plane_begin_original;
static VKAPI_ATTR VkResult VKAPI_CALL ds_plane_create(VkDevice device_,const VkRenderPassCreateInfo *ci,
    const VkAllocationCallbacks *allocator,VkRenderPass *pass) {
  VkResult result=ds_plane_create_original(device_,ci,allocator,pass);
  if(result==VK_SUCCESS) {
    unsigned n=0;while(n<ds_plane_pass_count && ds_plane_passes[n].handle!=*pass)n++;
    CHECK(n<256 && ci->attachmentCount<=9);if(n==ds_plane_pass_count)ds_plane_pass_count++;
    ds_plane_passes[n].handle=*pass;ds_plane_passes[n].count=ci->attachmentCount;
    memcpy(ds_plane_passes[n].a,ci->pAttachments,ci->attachmentCount*sizeof *ci->pAttachments);
  }return result;
}
static VKAPI_ATTR void VKAPI_CALL ds_plane_begin(VkCommandBuffer command,const VkRenderPassBeginInfo *ci,VkSubpassContents contents) {
  if(ds_plane_armed) {
    unsigned n=0;while(n<ds_plane_pass_count && ds_plane_passes[n].handle!=ci->renderPass)n++;
    CHECK(n<ds_plane_pass_count);
    for(unsigned k=0;k<ds_plane_passes[n].count;k++) {
      const VkAttachmentDescription *a=&ds_plane_passes[n].a[k];
      if(a->format!=VK_FORMAT_D24_UNORM_S8_UINT && a->format!=VK_FORMAT_D32_SFLOAT_S8_UINT)continue;
      VkAttachmentStoreOp depth=ds_plane_expected_depth,stencil=ds_plane_expected_stencil;
      if(ds_plane_second_readonly && ds_plane_observed==1)
        depth=stencil=V.load_store_op_none?VK_ATTACHMENT_STORE_OP_NONE_EXT:VK_ATTACHMENT_STORE_OP_STORE;
      CHECK(a->storeOp==depth);CHECK(a->stencilStoreOp==stencil);
      if(ds_plane_check_load && !ds_plane_observed) {
        CHECK(a->loadOp==((ds_plane_expected_clear&VK_IMAGE_ASPECT_DEPTH_BIT)?VK_ATTACHMENT_LOAD_OP_CLEAR:VK_ATTACHMENT_LOAD_OP_LOAD));
        CHECK(a->stencilLoadOp==((ds_plane_expected_clear&VK_IMAGE_ASPECT_STENCIL_BIT)?VK_ATTACHMENT_LOAD_OP_CLEAR:VK_ATTACHMENT_LOAD_OP_LOAD));
      }
      ds_plane_observed++;
    }
  }ds_plane_begin_original(command,ci,contents);
}
static void ds_plane_bytes(const NVNtexture *depth,int format,uint32_t result[1024]) {
  mtl_sub_begin();VkImage image=vk_texture_native((const tex_t *)depth);
  vk_buffer *readback=vk_temp_buffer(2560);VkBufferImageCopy copies[]={
    {.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,2},.imageExtent={16,16,1}},
    {.bufferOffset=2048,.imageSubresource={VK_IMAGE_ASPECT_STENCIL_BIT,0,0,2},.imageExtent={16,16,1}}};
  vk_barrier();vkCmdCopyImageToBuffer(V.cmd,image,VK_IMAGE_LAYOUT_GENERAL,readback->handle,2,copies);
  VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
  vk_internal_wait(vk_internal_commit());
  for(unsigned p=0;p<512;p++) {
    memcpy(&result[p],readback->map+p*4,4);
    if(format==NVN_FORMAT_DEPTH24_STENCIL8)result[p]&=0xffffff;
    result[512+p]=readback->map[2048+p];
  }
}
static void ds_plane_store_variant(const void *vertex,const void *fragment,int format) {
  vk_state saved=S;int support=V.load_store_op_none;
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,2<<20));memset(storage,0,2<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;CALL(nvnMemoryPoolBuilderSetDefaults,&mb);
  CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,2<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNcommandBuffer local;CHECK(CALL(nvnCommandBufferInitialize,&local,&device));
  CALL(nvnCommandBufferAddCommandMemory,&local,&pool,1<<20,1<<20);
  NVNtexture depth,color;
  for(unsigned n=0;n<2;n++) {
    NVNtextureBuilder b;CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,n?NVN_TEXTURE_TARGET_2D:NVN_TEXTURE_TARGET_2D_ARRAY);
    CALL(nvnTextureBuilderSetWidth,&b,16);CALL(nvnTextureBuilderSetHeight,&b,16);
    if(!n)CALL(nvnTextureBuilderSetDepth,&b,2);
    CALL(nvnTextureBuilderSetFormat,&b,n?NVN_FORMAT_RGBA8:format);CALL(nvnTextureBuilderSetStorage,&b,&pool,n*65536);
    CHECK(CALL(nvnTextureInitialize,n?&color:&depth,&b));
  }
  NVNtextureView views[2];for(unsigned layer=0;layer<2;layer++) {
    CALL(nvnTextureViewSetDefaults,&views[layer]);CALL(nvnTextureViewSetLevels,&views[layer],0,1);
    CALL(nvnTextureViewSetLayers,&views[layer],layer,1);CALL(nvnTextureViewSetTarget,&views[layer],NVN_TEXTURE_TARGET_2D);
  }
  NVNprogram program;NVNshaderData shaders[]={{.control=vertex},{.control=fragment}};
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  const float red[]={1,0,0,1},viewport[]={0,0,16,16};memcpy(storage+0x30000,red,16);
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNcolorState cs;CALL(nvnColorStateSetDefaults,&cs);NVNchannelMaskState channels;CALL(nvnChannelMaskStateSetDefaults,&channels);
  NVNdepthStencilState depth_writer,stencil_writer;
  CALL(nvnDepthStencilStateSetDefaults,&depth_writer);CALL(nvnDepthStencilStateSetDepthTestEnable,&depth_writer,1);
  CALL(nvnDepthStencilStateSetDepthWriteEnable,&depth_writer,1);CALL(nvnDepthStencilStateSetDepthFunc,&depth_writer,NVN_DEPTH_FUNC_ALWAYS);
  stencil_writer=depth_writer;CALL(nvnDepthStencilStateSetDepthWriteEnable,&stencil_writer,0);
  CALL(nvnDepthStencilStateSetStencilTestEnable,&stencil_writer,1);
  CALL(nvnDepthStencilStateSetStencilFunc,&stencil_writer,NVN_FACE_FRONT_AND_BACK,NVN_STENCIL_FUNC_ALWAYS);
  CALL(nvnDepthStencilStateSetStencilOp,&stencil_writer,NVN_FACE_FRONT_AND_BACK,NVN_STENCIL_OP_REPLACE,NVN_STENCIL_OP_REPLACE,NVN_STENCIL_OP_REPLACE);
  const NVNtexture *colors[]={&color};uint32_t oracle[11][1024],actual[1024];
  for(unsigned optimized=0;optimized<2;optimized++) {
    V.load_store_op_none=optimized?support:0;
    CALL(nvnCommandBufferBeginRecording,&local);
    // Keep the existing zero-count operation while supplying C-valid sources
    // to the original HLE's unconditional zero-length memcpy under UBSan.
    CALL(nvnCommandBufferBindVertexAttribState,&local,0,&(NVNvertexAttribState){0});
    CALL(nvnCommandBufferBindVertexStreamState,&local,0,&(NVNvertexStreamState){0});
    CALL(nvnCommandBufferSetViewports,&local,0,1,viewport);CALL(nvnCommandBufferSetDepthRange,&local,0,1);
    CALL(nvnCommandBufferSetScissor,&local,0,0,16,16);CALL(nvnCommandBufferBindPolygonState,&local,&polygon);
    CALL(nvnCommandBufferBindColorState,&local,&cs);CALL(nvnCommandBufferBindChannelMaskState,&local,&channels);
    CALL(nvnCommandBufferBindProgram,&local,&program,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferBindUniformBuffer,&local,NVN_SHADER_STAGE_FRAGMENT,0,(uintptr_t)(storage+0x30000),16);
    CALL(nvnCommandBufferSetStencilMask,&local,NVN_FACE_FRONT_AND_BACK,255);
    for(unsigned layer=0;layer<2;layer++) {
      CALL(nvnCommandBufferSetRenderTargets,&local,1,colors,NULL,&depth,&views[layer]);
      CALL(nvnCommandBufferClearDepthStencil,&local,layer?.5:.25,1,0x20+layer,255);
    }submit(CALL(nvnCommandBufferEndRecording,&local));
    for(unsigned test=0;test<11;test++) {
      unsigned layer=(test==1||test==2||test==8)?1:0;
      CALL(nvnCommandBufferBeginRecording,&local);CALL(nvnCommandBufferSetRenderTargets,&local,1,colors,NULL,&depth,&views[layer]);
      CALL(nvnCommandBufferSetScissor,&local,0,0,16,16);
      CALL(nvnCommandBufferSetStencilMask,&local,NVN_FACE_FRONT_AND_BACK,255);
      CALL(nvnCommandBufferSetStencilRef,&local,NVN_FACE_FRONT_AND_BACK,test==1?0x55:test==4?0x77:0x88);
      if(test==8)CALL(nvnCommandBufferSetScissor,&local,4,4,8,8);
      if(test==9)CALL(nvnCommandBufferSetScissor,&local,2,2,4,4);
      if(test==2||test==8)CALL(nvnCommandBufferClearDepthStencil,&local,test==8?.875:.75,1,0,0);
      else if(test==3||test==9)CALL(nvnCommandBufferClearDepthStencil,&local,0,0,test==9?0x99:0x66,255);
      else {
        NVNdepthStencilState state=test==1?stencil_writer:depth_writer;
        if(test==5||test==6) {
          state=stencil_writer;CALL(nvnDepthStencilStateSetDepthWriteEnable,&state,1);
          CALL(nvnCommandBufferSetStencilMask,&local,NVN_FACE_FRONT_AND_BACK,0);
          if(test==6)CALL(nvnCommandBufferSetStencilMask,&local,NVN_FACE_BACK,255);
        }
        if(test==7) {
          state=stencil_writer;
          CALL(nvnDepthStencilStateSetDepthTestEnable,&state,0);
          CALL(nvnDepthStencilStateSetDepthWriteEnable,&state,1); // Disabled test cannot write depth.
        }
        CALL(nvnCommandBufferBindDepthStencilState,&local,&state);
        CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
        if(test==10) {
          CALL(nvnCommandBufferSetScissor,&local,4,4,8,8);
          CALL(nvnCommandBufferClearDepthStencil,&local,0,0,0xaa,255);
        }
        if(test==4) { // Later same-pass writer must prevent both omissions.
          CALL(nvnCommandBufferBindDepthStencilState,&local,&stencil_writer);
          CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
        }
      }
      int depth_only=test==0||test==2||test==5||test==8,stencil_only=test==1||test==3||test==7||test==9;
      ds_plane_expected_depth=stencil_only&&V.load_store_op_none?VK_ATTACHMENT_STORE_OP_NONE_EXT:VK_ATTACHMENT_STORE_OP_STORE;
      ds_plane_expected_stencil=depth_only&&V.load_store_op_none?VK_ATTACHMENT_STORE_OP_NONE_EXT:VK_ATTACHMENT_STORE_OP_STORE;
      ds_plane_observed=0;ds_plane_armed=1;submit(CALL(nvnCommandBufferEndRecording,&local));ds_plane_armed=0;
      if(ds_plane_observed!=1)LOG("[DS plane failure] test=%u optimized=%u observed=%u passCount=%u beginHook=%d",test,optimized,ds_plane_observed,ds_plane_pass_count,vkCmdBeginRenderPass==ds_plane_begin);
      CHECK(ds_plane_observed==1);ds_plane_bytes(&depth,format,actual);
      if(!optimized)memcpy(oracle[test],actual,sizeof actual);
      else for(unsigned n=0;n<1024;n++)CHECK(actual[n]==oracle[test][n]);
      // Independent known-value checks before the asymmetric-face case.
      if(test<6)for(unsigned p=0;p<512;p++) {
        unsigned ly=p/256;float expected=ly?.5:0;
        if(test>=2&&ly)expected=.75;
        unsigned stencil=ly?(test>=1?0x55:0x21):(test>=4?0x77:test>=3?0x66:0x20);
        double observed;if(format==NVN_FORMAT_DEPTH24_STENCIL8)observed=actual[p]/16777215.;
        else {float z;memcpy(&z,&actual[p],4);observed=z;}
        CHECK(fabs(observed-expected)<1e-6);CHECK(actual[512+p]==stencil);
      }
    }
  }
  V.load_store_op_none=support;S=saved;vk_pipeline_state_changed();CALL(nvnCommandBufferFinalize,&local);
  CALL(nvnProgramFinalize,&program);CALL(nvnTextureFinalize,&depth);CALL(nvnTextureFinalize,&color);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);
}
// A same-image writer->readonly transition is an independent known fusion
// limitation and must take real split passes. Existing readonly/shared dual
// positive and optional-PSO retry cases remain in pixel_local_render_test.
static void ds_plane_alias_fallback_test(void) {
  vk_state saved=S;int support=V.load_store_op_none;
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,2<<20));memset(storage,0,2<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;CALL(nvnMemoryPoolBuilderSetDefaults,&mb);
  CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,2<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtexture t[6];for(unsigned k=0;k<6;k++) {
    NVNtextureBuilder b;CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&b,16);CALL(nvnTextureBuilderSetHeight,&b,16);
    CALL(nvnTextureBuilderSetFormat,&b,k>=5?NVN_FORMAT_DEPTH24_STENCIL8:k==0?NVN_FORMAT_RGBA32F:k==2?NVN_FORMAT_R11G11B10F:NVN_FORMAT_RGBA8);
    CALL(nvnTextureBuilderSetStorage,&b,&pool,k*65536);CHECK(CALL(nvnTextureInitialize,&t[k],&b));
  }
  uint8_t controls[3][0x880];for(unsigned k=0;k<3;k++)memset(controls[k],180+k,sizeof controls[k]);
  NVNprogram producer,consumer;NVNshaderData shaders[]={{.control=controls[0]},{.control=controls[1]}};
  CHECK(CALL(nvnProgramInitialize,&producer,&device));CHECK(CALL(nvnProgramSetShaders,&producer,2,shaders));shaders[1].control=controls[2];
  CHECK(CALL(nvnProgramInitialize,&consumer,&device));CHECK(CALL(nvnProgramSetShaders,&consumer,2,shaders));
  NVNcommandBuffer local;CHECK(CALL(nvnCommandBufferInitialize,&local,&device));CALL(nvnCommandBufferAddCommandMemory,&local,&pool,1<<20,1<<20);
  NVNvertexAttribState attrs[2];for(unsigned k=0;k<2;k++)CALL(nvnVertexAttribStateSetDefaults,&attrs[k]);
  CALL(nvnVertexAttribStateSetFormat,&attrs[0],NVN_FORMAT_RGB32F,0);CALL(nvnVertexAttribStateSetFormat,&attrs[1],NVN_FORMAT_RG32F,12);
  NVNvertexStreamState stream;CALL(nvnVertexStreamStateSetDefaults,&stream);CALL(nvnVertexStreamStateSetStride,&stream,20);
  float *vertices=(void *)(storage+0x80000);memcpy(vertices,(float[]){-1,-1,0,0,1,3,-1,0,2,1,-1,3,0,0,-1},60);NVNbufferRange vb={(uintptr_t)vertices,60};
  NVNdepthStencilState writer,reader;CALL(nvnDepthStencilStateSetDefaults,&writer);
  CALL(nvnDepthStencilStateSetDepthTestEnable,&writer,1);CALL(nvnDepthStencilStateSetDepthWriteEnable,&writer,1);CALL(nvnDepthStencilStateSetDepthFunc,&writer,NVN_DEPTH_FUNC_ALWAYS);
  reader=writer;CALL(nvnDepthStencilStateSetDepthWriteEnable,&reader,0);
  NVNpolygonState poly;CALL(nvnPolygonStateSetDefaults,&poly);CALL(nvnPolygonStateSetCullFace,&poly,NVN_FACE_NONE);
  NVNcolorState cs;CALL(nvnColorStateSetDefaults,&cs);NVNchannelMaskState cm;CALL(nvnChannelMaskStateSetDefaults,&cm);
  const NVNtexture *source[]={&t[0],&t[1],&t[2]},*output[]={&t[4]};float viewport[]={0,0,16,16};
  samp_par sampler={.minf=1,.magf=1,.aniso=1,.lod_max=1000};mtl_set_tex_pool_n(512);mtl_reg_samp(122,&sampler);
  NVNseparateSamplerHandle sh=CALL(nvnDeviceGetSeparateSamplerHandle,&device,122);NVNseparateTextureHandle handles[4];
  for(unsigned k=0;k<4;k++){mtl_reg_tex(120+k,(const tex_t *)&t[k],NULL);handles[k]=CALL(nvnDeviceGetSeparateTextureHandle,&device,120+k);}
  mtl_sub_begin();for(unsigned k=0;k<3;k++){vk_pixel_local_image token;CHECK(vk_texture_pixel_local_prepare((const tex_t *)&t[k],1,&token));}
  vk_texture_prepare_attachment((const tex_t *)&t[4],NULL);vk_internal_wait(vk_internal_commit());
  // Case0 is the writer fallback. Cases1/2 keep a real ordinary loadCLEAR
  // pass active before the first readonly producer draw; cases3/4 put a DS
  // clear after that draw, which bounded lookahead must reject. This proves
  // prior CLEAR is not silently imported into a readonly dual render pass.
  for(unsigned none=0;none<=(unsigned)support;none++) for(unsigned test=0;test<5;test++) {
    V.load_store_op_none=none;
    CALL(nvnCommandBufferBeginRecording,&local);CALL(nvnCommandBufferSetScissor,&local,0,0,16,16);
    for(unsigned k=0;k<5;k++){NVNcopyRegion r={0,0,0,16,16,1};float c[]={k==3?16/255.f:0,k==3?16/255.f:0,k==3?16/255.f:0,1};CALL(nvnCommandBufferClearTexture,&local,&t[k],NULL,&r,c,15);}
    for(unsigned k=5;k<6;k++) {CALL(nvnCommandBufferSetRenderTargets,&local,0,NULL,NULL,&t[k],NULL);CALL(nvnCommandBufferClearDepthStencil,&local,.5,1,0x37,255);}
    submit(CALL(nvnCommandBufferEndRecording,&local));
    CALL(nvnCommandBufferBeginRecording,&local);CALL(nvnCommandBufferSetScissor,&local,0,0,16,16);CALL(nvnCommandBufferSetViewports,&local,0,1,viewport);CALL(nvnCommandBufferSetDepthRange,&local,0,1);
    CALL(nvnCommandBufferBindPolygonState,&local,&poly);CALL(nvnCommandBufferBindColorState,&local,&cs);CALL(nvnCommandBufferBindChannelMaskState,&local,&cm);
    CALL(nvnCommandBufferBindVertexAttribState,&local,2,attrs);CALL(nvnCommandBufferBindVertexStreamState,&local,1,&stream);CALL(nvnCommandBufferBindVertexBuffers,&local,0,1,&vb);
    float blend[]={.8125f,none/8.f,0,.6875f};CALL(nvnCommandBufferSetBlendColor,&local,blend);
    CALL(nvnCommandBufferBindDepthStencilState,&local,test?&reader:&writer);CALL(nvnCommandBufferSetRenderTargets,&local,3,source,NULL,&t[5],NULL);
    if(test==1 || test==2)CALL(nvnCommandBufferClearDepthStencil,&local,.75,test==1,0x66,test==2?255:0);
    CALL(nvnCommandBufferBindProgram,&local,&producer,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    if(test==3 || test==4)CALL(nvnCommandBufferClearDepthStencil,&local,.75,test==3,0x66,test==4?255:0);
    CALL(nvnCommandBufferSetRenderTargets,&local,1,output,NULL,&t[5],NULL);CALL(nvnCommandBufferBindDepthStencilState,&local,&reader);
    CALL(nvnCommandBufferBindProgram,&local,&consumer,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);CALL(nvnCommandBufferBindSeparateTextures,&local,NVN_SHADER_STAGE_FRAGMENT,0,4,handles);
    CALL(nvnCommandBufferBindSeparateSamplers,&local,NVN_SHADER_STAGE_FRAGMENT,0,1,&sh);CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    uint64_t p0,d0,a0,p1,d1,a1;vk_pixel_local_stats(&p0,&d0,&a0);
    int stencil_clear=test==2 || test==4;
    ds_plane_expected_depth=none && stencil_clear?VK_ATTACHMENT_STORE_OP_NONE_EXT:VK_ATTACHMENT_STORE_OP_STORE;
    ds_plane_expected_stencil=none && !stencil_clear?VK_ATTACHMENT_STORE_OP_NONE_EXT:VK_ATTACHMENT_STORE_OP_STORE;
    ds_plane_check_load=1;ds_plane_expected_clear=test==1?VK_IMAGE_ASPECT_DEPTH_BIT:test==2?VK_IMAGE_ASPECT_STENCIL_BIT:0;
    ds_plane_observed=0;ds_plane_second_readonly=1;ds_plane_armed=1;submit(CALL(nvnCommandBufferEndRecording,&local));
    ds_plane_armed=ds_plane_second_readonly=ds_plane_check_load=0;
    vk_pixel_local_stats(&p1,&d1,&a1);
    LOG("[DS plane alias fallback] none=%u case=%u plans=%llu draws=%llu aborts=%llu observed=%u",none,test,(unsigned long long)(p1-p0),(unsigned long long)(d1-d0),(unsigned long long)(a1-a0),ds_plane_observed);
    CHECK(p1==p0);CHECK(d1==d0);CHECK(a1==a0);CHECK(ds_plane_observed==2);
    NVNcopyRegion region={0,0,0,16,16,1};uint8_t pixels[1024];CALL(nvnTextureReadTexels,&t[4],NULL,&region,pixels);
    for(unsigned k=0;k<256;k++)for(unsigned c=0;c<4;c++)CHECK(abs((int)pixels[4*k+c]-(c==3?255:80))<=1);
    mtl_sub_begin();VkImage image=vk_texture_native((const tex_t *)&t[5]);vk_buffer *rb=vk_temp_buffer(1280);
    VkBufferImageCopy copies[]={{.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1},.imageExtent={16,16,1}},{.bufferOffset=1024,.imageSubresource={VK_IMAGE_ASPECT_STENCIL_BIT,0,0,1},.imageExtent={16,16,1}}};
    vk_barrier();vkCmdCopyImageToBuffer(V.cmd,image,VK_IMAGE_LAYOUT_GENERAL,rb->handle,2,copies);
    VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);vk_internal_wait(vk_internal_commit());
    double expected_z=!test?0:(test==1 || test==3)?.75:.5;
    for(unsigned k=0;k<256;k++){uint32_t z;memcpy(&z,rb->map+k*4,4);CHECK(fabs((z&0xffffff)/16777215.-expected_z)<1e-6);CHECK(rb->map[1024+k]==(stencil_clear?0x66:0x37));}
  }
  V.load_store_op_none=support;S=saved;vk_pipeline_state_changed();CALL(nvnCommandBufferFinalize,&local);CALL(nvnProgramFinalize,&producer);CALL(nvnProgramFinalize,&consumer);
  for(unsigned k=0;k<6;k++)CALL(nvnTextureFinalize,&t[k]);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);
}

static void ds_plane_store_test(void) {
  mtl_init(); // Initialize Volk before installing API-only fixture wrappers.
  uint8_t controls[2][0x880];memset(controls[0],0,sizeof controls[0]);memset(controls[1],1,sizeof controls[1]);
  ds_plane_create_original=vkCreateRenderPass;ds_plane_begin_original=vkCmdBeginRenderPass;
  vkCreateRenderPass=ds_plane_create;vkCmdBeginRenderPass=ds_plane_begin;
  ds_plane_store_variant(controls[0],controls[1],NVN_FORMAT_DEPTH24_STENCIL8);
  ds_plane_store_variant(controls[0],controls[1],NVN_FORMAT_DEPTH32F_STENCIL8);
  if(getenv("DM_PIXEL_LOCAL_RENDER_FIXTURE") && vk_pixel_local_enabled())ds_plane_alias_fallback_test();
  vkCreateRenderPass=ds_plane_create_original;vkCmdBeginRenderPass=ds_plane_begin_original;
  LOG("[vulkan DS plane store fixture] actual NONE/STORE API, independent both-plane/layer GPU bytes, late writer, clear, asymmetric mask and unsupported-NONE fallback passed");
}
