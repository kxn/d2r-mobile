// Real physical image-request admission controls. Invalid feedback is never
// drawn: reject it before dependency publication, then abandon the batch.
static void quad_readonly_image_requests_test(void) {
  uint8_t *mem;CHECK(!posix_memalign((void **)&mem,65536,1<<20));memset(mem,0,1<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;CALL(nvnMemoryPoolBuilderSetDefaults,&mb);
  CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);CALL(nvnMemoryPoolBuilderSetStorage,&mb,mem,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtexturePool tp;CHECK(CALL(nvnTexturePoolInitialize,&tp,&pool,0x30000,32));
  NVNtextureBuilder tb;NVNtexture color,ds;
  CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&tb,16);
  CALL(nvnTextureBuilderSetHeight,&tb,16);CALL(nvnTextureBuilderSetDepth,&tb,1);CALL(nvnTextureBuilderSetLevels,&tb,2);
  CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);CALL(nvnTextureBuilderSetStorage,&tb,&pool,0);
  CHECK(CALL(nvnTextureInitialize,&color,&tb));
  CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_DEPTH24_STENCIL8);CALL(nvnTextureBuilderSetStorage,&tb,&pool,0x10000);
  CHECK(CALL(nvnTextureInitialize,&ds,&tb));
  NVNtextureView views[2];for(unsigned n=0;n<2;n++) {CALL(nvnTextureViewSetDefaults,&views[n]);CALL(nvnTextureViewSetLevels,&views[n],n,1);}
  mtl_sub_begin();vk_render_end();vk_texture_prepare_attachment((tex_t *)&color,(view_t *)&views[0]);
  for(unsigned test=0;test<3;test++) {
    // same mip/default all-mips reject, explicit disjoint mip accepts.
    const view_t *sample=test==0?(view_t *)&views[0]:test==1?(view_t *)&views[1]:NULL;
    mtl_reg_tex(21,(tex_t *)&color,sample);vk_texture_shader_begin();VkImageLayout layout;
    CHECK(vk_texture_view_binding_quad(21,0,VK_SHADER_STAGE_FRAGMENT_BIT,&layout));
    vk_texture_attachment_record((tex_t *)&color,(view_t *)&views[0],0);
    CHECK(!!vk_texture_quad_readonly_error()==(test!=1));
    vk_texture_shader_abandon();
  }
  // Readonly DS use is not a writer, unlike color attachment consumption.
  vk_texture_prepare_attachment((tex_t *)&ds,(view_t *)&views[0]);mtl_reg_tex(22,(tex_t *)&ds,(view_t *)&views[0]);
  vk_texture_shader_begin();VkImageLayout depth_layout;
  CHECK(vk_texture_view_binding_quad(22,0,VK_SHADER_STAGE_FRAGMENT_BIT,&depth_layout));
  vk_texture_attachment_read((tex_t *)&ds,(view_t *)&views[0]);CHECK(!vk_texture_quad_readonly_error());vk_texture_shader_abandon();
  vk_texture_shader_begin();
  CHECK(vk_texture_view_binding_quad(22,0,VK_SHADER_STAGE_FRAGMENT_BIT,&depth_layout));
  vk_texture_attachment_record((tex_t *)&ds,(view_t *)&views[0],0);
  CHECK(vk_texture_quad_readonly_error());vk_texture_shader_abandon(); // actual DS writer cannot overlap a sampled source
  if(getenv("DM_QUAD_READONLY_BAD_SHAPE")) {
    NVNtexture stencil;CALL(nvnTextureBuilderSetLevels,&tb,1);
    CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_STENCIL8);CALL(nvnTextureBuilderSetStorage,&tb,&pool,0x20000);
    CHECK(CALL(nvnTextureInitialize,&stencil,&tb));
    mtl_reg_tex(23,(tex_t *)&stencil,(view_t *)&views[0]);vk_texture_shader_begin();
    vk_texture_view_binding_quad(23,0,VK_SHADER_STAGE_FRAGMENT_BIT,&depth_layout);
    die("readonly quad source accepted a stencil view");
  }
  CHECK(vk_texture_quad_readonly_error()); // no descriptor batch is no proof
  vk_internal_wait(vk_internal_commit());
  CALL(nvnTextureFinalize,&color);CALL(nvnTextureFinalize,&ds);CALL(nvnTexturePoolFinalize,&tp);CALL(nvnMemoryPoolFinalize,&pool);free(mem);
  LOG("[quad readonly contract] actual same-mip/all-mip feedback rejected, disjoint mip/readonly DS admitted");
}
// Plain sampler2D is legal for a DEPTH view: Vulkan ignores OpTypeImage Depth.
// This source uses real OpImageFetch + OpImageQuerySizeLod, no Dref operation.
static void quad_readonly_depth_fetch_test(void) {
  vk_state saved=S;uint8_t *mem;CHECK(!posix_memalign((void **)&mem,65536,2<<20));memset(mem,0,2<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;CALL(nvnMemoryPoolBuilderSetDefaults,&mb);
  CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);CALL(nvnMemoryPoolBuilderSetStorage,&mb,mem,2<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNcommandBuffer cb;CHECK(CALL(nvnCommandBufferInitialize,&cb,&device));CALL(nvnCommandBufferAddCommandMemory,&cb,&pool,1<<20,1<<20);
  NVNtexture depth,color;NVNtextureBuilder tb;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&tb,16);CALL(nvnTextureBuilderSetHeight,&tb,16);
  CALL(nvnTextureBuilderSetDepth,&tb,1);CALL(nvnTextureBuilderSetLevels,&tb,1);
  CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_DEPTH24_STENCIL8);CALL(nvnTextureBuilderSetStorage,&tb,&pool,0);CHECK(CALL(nvnTextureInitialize,&depth,&tb));
  CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);CALL(nvnTextureBuilderSetStorage,&tb,&pool,65536);CHECK(CALL(nvnTextureInitialize,&color,&tb));
  NVNtextureView view;CALL(nvnTextureViewSetDefaults,&view);CALL(nvnTextureViewSetLevels,&view,0,1);
  NVNtexturePool tp;NVNsamplerPool sp;CHECK(CALL(nvnTexturePoolInitialize,&tp,&pool,0x30000,32));CHECK(CALL(nvnSamplerPoolInitialize,&sp,&pool,0x40000,32));
  CALL(nvnTexturePoolRegisterTexture,&tp,1,&depth,&view);
  NVNsamplerBuilder sb;NVNsampler sampler;CALL(nvnSamplerBuilderSetDefaults,&sb);CALL(nvnSamplerBuilderSetDevice,&sb,&device);
  CHECK(CALL(nvnSamplerInitialize,&sampler,&sb));CALL(nvnSamplerPoolRegisterSampler,&sp,1,&sampler);
  NVNseparateTextureHandle th=CALL(nvnDeviceGetSeparateTextureHandle,&device,1);NVNseparateSamplerHandle sh=CALL(nvnDeviceGetSeparateSamplerHandle,&device,1);
  uint8_t controls[2][0x880];memset(controls[0],0,0x880);memset(controls[1],43,0x880);
  NVNprogram program;NVNshaderData shaders[]={{.control=controls[0]},{.control=controls[1]}};CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNcolorState cs;CALL(nvnColorStateSetDefaults,&cs);CALL(nvnColorStateSetBlendEnable,&cs,0,0);
  NVNchannelMaskState mask;CALL(nvnChannelMaskStateSetDefaults,&mask);NVNmultisampleState ms;CALL(nvnMultisampleStateSetDefaults,&ms);
  CALL(nvnCommandBufferBeginRecording,&cb);CALL(nvnCommandBufferSetRenderTargets,&cb,0,NULL,NULL,&depth,&view);
  CALL(nvnCommandBufferSetScissor,&cb,0,0,16,16);CALL(nvnCommandBufferClearDepthStencil,&cb,.25f,1,0x5a,255);
  submit(CALL(nvnCommandBufferEndRecording,&cb));
  NVNcopyRegion region={0,0,0,16,16,1};uint8_t pixels[1024];
  CALL(nvnCommandBufferBeginRecording,&cb);const NVNtexture *targets[]={&color};const NVNtextureView *views[]={&view};
  CALL(nvnCommandBufferSetRenderTargets,&cb,1,targets,views,NULL,NULL);CALL(nvnCommandBufferSetViewports,&cb,0,1,(float[]){0,0,16,16});CALL(nvnCommandBufferSetScissor,&cb,0,0,16,16);
  CALL(nvnCommandBufferSetTexturePool,&cb,&tp);CALL(nvnCommandBufferSetSamplerPool,&cb,&sp);
  CALL(nvnCommandBufferBindProgram,&cb,&program,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);CALL(nvnCommandBufferBindVertexAttribState,&cb,0,NULL);CALL(nvnCommandBufferBindVertexStreamState,&cb,0,NULL);
  CALL(nvnCommandBufferBindPolygonState,&cb,&polygon);CALL(nvnCommandBufferBindDepthStencilState,&cb,&ds);CALL(nvnCommandBufferBindColorState,&cb,&cs);CALL(nvnCommandBufferBindChannelMaskState,&cb,&mask);CALL(nvnCommandBufferBindMultisampleState,&cb,&ms);
  CALL(nvnCommandBufferBindSeparateTextures,&cb,NVN_SHADER_STAGE_FRAGMENT,0,1,&th);CALL(nvnCommandBufferBindSeparateSamplers,&cb,NVN_SHADER_STAGE_FRAGMENT,0,1,&sh);
  CALL(nvnCommandBufferDrawArrays,&cb,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);submit(CALL(nvnCommandBufferEndRecording,&cb));
  CALL(nvnTextureReadTexels,&color,&view,&region,pixels);
  mtl_sub_begin();VkImage native=vk_texture_native((tex_t *)&depth);vk_buffer *rb=vk_temp_buffer(1280);
  VkBufferImageCopy copies[]={
    {.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1},.imageExtent={16,16,1}},
    {.bufferOffset=1024,.imageSubresource={VK_IMAGE_ASPECT_STENCIL_BIT,0,0,1},.imageExtent={16,16,1}}};
  vk_barrier();vkCmdCopyImageToBuffer(V.cmd,native,VK_IMAGE_LAYOUT_GENERAL,rb->handle,2,copies);vk_internal_wait(vk_internal_commit());
  for(unsigned p=0;p<256;p++) {CHECK(abs((int)pixels[p*4]-64)<=1);CHECK(pixels[p*4+1]==0 && pixels[p*4+2]==0 && pixels[p*4+3]==255);}
  for(unsigned n=0;n<256;n++) {uint32_t z;memcpy(&z,rb->map+n*4,4);CHECK(fabs((z&0xffffff)/16777215.-.25)<2./16777215.);CHECK(rb->map[1024+n]==0x5a);}
  S=saved;vk_pipeline_state_changed();CALL(nvnCommandBufferFinalize,&cb);CALL(nvnProgramFinalize,&program);CALL(nvnTexturePoolFinalize,&tp);CALL(nvnSamplerPoolFinalize,&sp);CALL(nvnSamplerFinalize,&sampler);CALL(nvnTextureFinalize,&depth);CALL(nvnTextureFinalize,&color);CALL(nvnMemoryPoolFinalize,&pool);free(mem);
  LOG("[quad readonly contract] plain2D depth texelFetch/query-size actual flagged draw + full DS sentinel preserved");
}
static int quad_readonly_contract_test(void) {
  crash_thread_init();mtl_init();
  const char *bad=getenv("DM_QUAD_TRANSFER_BAD_KEY");if(bad) {vk_shader_load_key(bad);die("invalid readonly trait accepted");}
  uint8_t control[2][0x880];memset(control[0],0,0x880);memset(control[1],1,0x880);
  vk_shader *vs=vk_shader_get(control[0]),*fs=vk_shader_get(control[1]);CHECK(fs->quad_transfer && fs->quad_readonly_sources);
  vk_shader *list[6]={vs,fs};CHECK(!vk_shader_quad_readonly_stages_error(list));
  VkPhysicalDeviceFeatures features=V.features;V.features.robustBufferAccess=0;CHECK(vk_shader_quad_readonly_stages_error(list));V.features=features;
  vk_shader writer=*vs;writer.has_resource_writes=1;list[0]=&writer;CHECK(vk_shader_quad_readonly_stages_error(list));list[0]=vs;
  writer=*fs;writer.has_resource_writes=1;list[1]=&writer;CHECK(vk_shader_quad_readonly_stages_error(list));list[1]=fs;
  for(const hle_force_ent *e=(const void *)__start_swforce;e<(const hle_force_ent *)(const void *)__stop_swforce;e++)if(!strcmp(e->name,"nvnBootstrapLoader"))bootstrap=e->fn;
  CHECK(bootstrap);NVNdeviceBuilder db;CALL(nvnDeviceBuilderSetDefaults,&db);CHECK(CALL(nvnDeviceInitialize,&device,&db));
  NVNqueueBuilder qb;CALL(nvnQueueBuilderSetDefaults,&qb);CALL(nvnQueueBuilderSetDevice,&qb,&device);CHECK(CALL(nvnQueueInitialize,&queue,&qb));
  unknown_stage_reuse_test(control[0],control[1]); // repeated actual flagged graphics PSO/pixels
  quad_readonly_image_requests_test();
  quad_readonly_depth_fetch_test();
  sampled_mip_load_cover_test(); // FS43 flagged; real mip0→1, mip1→0, alias & sibling/history pixels
  CHECK(vk_validation_error_count()==0);printf("{\"test\":\"quad-readonly-runtime-contract\",\"checks\":%u,\"result\":\"passed\"}\n",checks);return 0;
}
