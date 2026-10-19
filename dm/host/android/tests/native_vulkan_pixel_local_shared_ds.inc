// Exercise physical shared readonly DS with deliberately stale NVN tests,
// real FragDepth/discard, exact DS bytes and optional PSO creation failures.
static PFN_vkCreateGraphicsPipelines shared_ds_create_original;
static PFN_vkCreateRenderPass shared_ds_pass_original;
static PFN_vkCmdDraw shared_ds_draw_original;
static unsigned shared_ds_first_pipelines,shared_ds_same_depth_passes,shared_ds_draw_commands;
static unsigned shared_ds_fail_stage,shared_ds_fail_remaining,shared_ds_fail_after_draws;
static int shared_ds_unsafe_control;
static VkRenderPass shared_ds_known_passes[4];static unsigned shared_ds_known_count;
static int shared_ds_known(VkRenderPass pass) {
  for(unsigned k=0;k<shared_ds_known_count;k++)if(shared_ds_known_passes[k]==pass)return 1;
  return 0;
}
static VKAPI_ATTR void VKAPI_CALL shared_ds_draw(VkCommandBuffer cmd_,uint32_t vertices,uint32_t instances,uint32_t first,uint32_t base) {
  shared_ds_draw_commands++;shared_ds_draw_original(cmd_,vertices,instances,first,base);
}
static VKAPI_ATTR VkResult VKAPI_CALL shared_ds_create(VkDevice d,VkPipelineCache cache,uint32_t n,
    const VkGraphicsPipelineCreateInfo *ci,const VkAllocationCallbacks *a,VkPipeline *out) {
  for(unsigned k=0;k<n;k++)if(shared_ds_known(ci[k].renderPass) && ci[k].subpass==0 && ci[k].pColorBlendState->attachmentCount==3) {
    const VkPipelineDepthStencilStateCreateInfo *ds_=ci[k].pDepthStencilState;
    CHECK(!ds_->depthTestEnable && !ds_->depthWriteEnable && !ds_->stencilTestEnable && !ds_->depthBoundsTestEnable);
    CHECK(ds_->front.writeMask==0 && ds_->back.writeMask==0);shared_ds_first_pipelines++;
    if(shared_ds_fail_remaining && shared_ds_fail_stage==0 && shared_ds_draw_commands==shared_ds_fail_after_draws) {
      CHECK(shared_ds_draw_commands==shared_ds_fail_after_draws); // Only earlier real producer draws.
      shared_ds_fail_remaining--;for(unsigned j=0;j<n;j++)out[j]=VK_NULL_HANDLE;return VK_ERROR_UNKNOWN;
    }
    if(shared_ds_unsafe_control) {
      // Negative GPU control: attach the same DS but reuse stale NEVER flags.
      // Expected strict pixel failure, never a production fallback or oracle change.
      CHECK(n==1);VkGraphicsPipelineCreateInfo changed=*ci;VkPipelineDepthStencilStateCreateInfo bad=*ds_;
      bad.depthTestEnable=VK_TRUE;bad.depthWriteEnable=VK_FALSE;bad.depthCompareOp=VK_COMPARE_OP_NEVER;
      changed.pDepthStencilState=&bad;
      return shared_ds_create_original(d,cache,1,&changed,a,out);
    }
  }
  if(shared_ds_fail_remaining && shared_ds_fail_stage==1)for(unsigned k=0;k<n;k++)if(ci[k].subpass==1) {
    CHECK(shared_ds_draw_commands==1);shared_ds_fail_remaining--;
    for(unsigned j=0;j<n;j++)out[j]=VK_NULL_HANDLE;return VK_ERROR_UNKNOWN;
  }
  return shared_ds_create_original(d,cache,n,ci,a,out);
}
static VKAPI_ATTR VkResult VKAPI_CALL shared_ds_pass(VkDevice d,const VkRenderPassCreateInfo *ci,
    const VkAllocationCallbacks *a,VkRenderPass *out) {
  if(ci->subpassCount==2) {
    const VkAttachmentReference *x=ci->pSubpasses[0].pDepthStencilAttachment,*y=ci->pSubpasses[1].pDepthStencilAttachment;
    if(x && y && x->attachment==y->attachment) {
      CHECK(x->layout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL && x->layout==y->layout);
      for(unsigned k=0;k<ci->pSubpasses[0].preserveAttachmentCount;k++)CHECK(ci->pSubpasses[0].pPreserveAttachments[k]!=x->attachment);
      const VkAttachmentDescription *ds_=&ci->pAttachments[x->attachment];
      CHECK(ds_->loadOp==VK_ATTACHMENT_LOAD_OP_LOAD && ds_->stencilLoadOp==VK_ATTACHMENT_LOAD_OP_LOAD);
      CHECK(ds_->storeOp==(V.load_store_op_none?VK_ATTACHMENT_STORE_OP_NONE_EXT:VK_ATTACHMENT_STORE_OP_STORE));
      CHECK(ds_->stencilStoreOp==ds_->storeOp);
      for(unsigned k=0;k<ci->pSubpasses[1].colorAttachmentCount;k++) {
        unsigned attachment=ci->pSubpasses[1].pColorAttachments[k].attachment;
        CHECK(ci->pAttachments[attachment].loadOp==VK_ATTACHMENT_LOAD_OP_LOAD);
      }
      shared_ds_same_depth_passes++;
    }
  }
  VkResult result=shared_ds_pass_original(d,ci,a,out);
  if(result==VK_SUCCESS && ci->subpassCount==2 && ci->pSubpasses[0].pDepthStencilAttachment &&
      ci->pSubpasses[1].pDepthStencilAttachment &&
      ci->pSubpasses[0].pDepthStencilAttachment->attachment==ci->pSubpasses[1].pDepthStencilAttachment->attachment) {
    CHECK(shared_ds_known_count<4);shared_ds_known_passes[shared_ds_known_count++]=*out;
  }
  return result;
}
static PFN_vkCmdPipelineBarrier shared_ds_barrier_original;
static unsigned shared_ds_require_load_scope,shared_ds_load_scope_checks;
static VKAPI_ATTR void VKAPI_CALL shared_ds_barrier(VkCommandBuffer cmd_,VkPipelineStageFlags src,
    VkPipelineStageFlags dst,VkDependencyFlags flags,uint32_t nm,const VkMemoryBarrier *m,
    uint32_t nb,const VkBufferMemoryBarrier *b,uint32_t ni,const VkImageMemoryBarrier *images) {
  if(shared_ds_require_load_scope)for(unsigned k=0;k<ni;k++)
    if(images[k].oldLayout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL &&
        images[k].newLayout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
      const VkPipelineStageFlags tests=VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
      LOG("[pixel-local DS LOAD history] real later layout transition src=%#x dst=%#x old=%u next=%u",src,dst,images[k].oldLayout,images[k].newLayout);
      shared_ds_load_scope_checks++;
      CHECK((src&tests)==tests || (src&VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
    }
  shared_ds_barrier_original(cmd_,src,dst,flags,nm,m,nb,b,ni,images);
}
static int pixel_local_shared_depth_test(void) {
  vk_state saved=S;int original_none=V.load_store_op_none;
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,2<<20));memset(storage,0,2<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,2<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtexture textures[10];for(unsigned k=0;k<10;k++) {
    NVNtextureBuilder b;CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&b,16);
    CALL(nvnTextureBuilderSetHeight,&b,16);CALL(nvnTextureBuilderSetLevels,&b,1);
    CALL(nvnTextureBuilderSetFormat,&b,k>=6?NVN_FORMAT_DEPTH24_STENCIL8:k==0?NVN_FORMAT_RGBA32F:k==2?NVN_FORMAT_R11G11B10F:NVN_FORMAT_RGBA8);
    CALL(nvnTextureBuilderSetStorage,&b,&pool,(k==7?6:k>=8?k+2:k)*65536);CHECK(CALL(nvnTextureInitialize,&textures[k],&b));
  }
  NVNprogram producer,consumer,reader,alias,fault_consumer,depth_producer,second_producer;uint8_t controls[8][0x880];
  unsigned markers[]={180,181,182,9,193,194,195,196};for(unsigned k=0;k<8;k++)memset(controls[k],markers[k],sizeof controls[k]);
  NVNshaderData shaders[]={{.control=controls[0]},{.control=controls[1]}};
  CHECK(CALL(nvnProgramInitialize,&producer,&device));CHECK(CALL(nvnProgramSetShaders,&producer,2,shaders));
  shaders[1].control=controls[2];CHECK(CALL(nvnProgramInitialize,&consumer,&device));CHECK(CALL(nvnProgramSetShaders,&consumer,2,shaders));
  shaders[1].control=controls[3];CHECK(CALL(nvnProgramInitialize,&reader,&device));CHECK(CALL(nvnProgramSetShaders,&reader,2,shaders));
  shaders[1].control=controls[4];CHECK(CALL(nvnProgramInitialize,&alias,&device));CHECK(CALL(nvnProgramSetShaders,&alias,2,shaders));
  shaders[1].control=controls[5];CHECK(CALL(nvnProgramInitialize,&fault_consumer,&device));CHECK(CALL(nvnProgramSetShaders,&fault_consumer,2,shaders));
  shaders[1].control=controls[6];CHECK(CALL(nvnProgramInitialize,&depth_producer,&device));CHECK(CALL(nvnProgramSetShaders,&depth_producer,2,shaders));
  shaders[1].control=controls[7];CHECK(CALL(nvnProgramInitialize,&second_producer,&device));CHECK(CALL(nvnProgramSetShaders,&second_producer,2,shaders));
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
  NVNdepthStencilState stale=ds;
  CALL(nvnDepthStencilStateSetDepthTestEnable,&stale,1);CALL(nvnDepthStencilStateSetDepthWriteEnable,&stale,1);
  CALL(nvnDepthStencilStateSetDepthFunc,&stale,NVN_DEPTH_FUNC_NEVER);CALL(nvnDepthStencilStateSetStencilTestEnable,&stale,1);
  CALL(nvnDepthStencilStateSetStencilFunc,&stale,NVN_FACE_FRONT_AND_BACK,NVN_STENCIL_FUNC_NEVER);
  CALL(nvnDepthStencilStateSetStencilOp,&stale,NVN_FACE_FRONT_AND_BACK,NVN_STENCIL_OP_REPLACE,NVN_STENCIL_OP_REPLACE,NVN_STENCIL_OP_REPLACE);
  NVNpolygonState biased=polygon;CALL(nvnPolygonStateSetPolygonOffsetEnables,&biased,1);
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
  int experiment=vk_pixel_local_enabled() && vk_pixel_local_shared_ds_enabled();
  for(unsigned none=0;none<=(unsigned)original_none;none++)for(unsigned test=0;test<10;test++) {
    V.load_store_op_none=none;
    const NVNtexture *depth_target=test==9?&textures[8+none]:&textures[6];
    mtl_reg_tex(116,(const tex_t *)depth_target,NULL);
    CALL(nvnCommandBufferBeginRecording,&local);
    for(unsigned k=0;k<6;k++) {NVNcopyRegion r={0,0,0,16,16,1};CALL(nvnCommandBufferClearTexture,&local,&textures[k],NULL,&r,k==3?history:blue,15);}
    CALL(nvnCommandBufferSetScissor,&local,0,0,16,16);
    if(test==9) {
      // Fresh physical DS: only TRANSFER writer, never an E/L clear/store.
      union {float f[4];uint32_t u[4];} clear={.f={.5,0,0,0}};clear.u[1]=0x37;
      NVNcopyRegion region={0,0,0,16,16,1};CALL(nvnCommandBufferClearTexture,&local,depth_target,NULL,&region,clear.f,15);
    } else {
      CALL(nvnCommandBufferSetRenderTargets,&local,0,NULL,NULL,depth_target,NULL);
      CALL(nvnCommandBufferClearDepthStencil,&local,.5,1,0x37,255);
    }
    submit(CALL(nvnCommandBufferEndRecording,&local));
    CALL(nvnCommandBufferBeginRecording,&local);
    CALL(nvnCommandBufferSetViewports,&local,0,1,viewport);CALL(nvnCommandBufferSetScissor,&local,0,0,16,16);
    CALL(nvnCommandBufferSetDepthRange,&local,0,1);CALL(nvnCommandBufferBindPolygonState,&local,test==3?&biased:&polygon);
    CALL(nvnCommandBufferBindColorState,&local,&cs);CALL(nvnCommandBufferBindChannelMaskState,&local,&mask);
    CALL(nvnCommandBufferBindVertexAttribState,&local,2,attrs);CALL(nvnCommandBufferBindVertexStreamState,&local,1,&stream);CALL(nvnCommandBufferBindVertexBuffers,&local,0,1,&vb);
    // Unique ignored blend constants force actual compile for each owned fault;
    // blend stays disabled and the numerical oracle is independent of these.
    float blend_constant[]={test/16.f,none/8.f,.0625f,.375f};CALL(nvnCommandBufferSetBlendColor,&local,blend_constant);
    CALL(nvnCommandBufferSetStencilMask,&local,NVN_FACE_FRONT_AND_BACK,255);CALL(nvnCommandBufferSetStencilValueMask,&local,NVN_FACE_FRONT_AND_BACK,255);
    CALL(nvnCommandBufferSetStencilRef,&local,NVN_FACE_FRONT_AND_BACK,0x99);
    if(test==9) {
      // A shader-only prior DS reader keeps the layout's scope at FS. The
      // same-RO union preparation must not invent an E/L layout transition.
      CALL(nvnCommandBufferBindDepthStencilState,&local,&ds);
      CALL(nvnCommandBufferSetRenderTargets,&local,1,scratch,NULL,NULL,NULL);
      CALL(nvnCommandBufferBindProgram,&local,&reader,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
      CALL(nvnCommandBufferBindSeparateTextures,&local,NVN_SHADER_STAGE_FRAGMENT,0,1,&dh);
      CALL(nvnCommandBufferBindSeparateSamplers,&local,NVN_SHADER_STAGE_FRAGMENT,0,1,&sh);
      CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    }
    CALL(nvnCommandBufferBindDepthStencilState,&local,&stale);
    CALL(nvnCommandBufferSetRenderTargets,&local,3,source,NULL,NULL,NULL);
    CALL(nvnCommandBufferBindProgram,&local,(test==1 || test==7)?&depth_producer:&producer,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    if(test==7)CALL(nvnCommandBufferResetCounter,&local,NVN_COUNTER_TYPE_SAMPLES_PASSED);
    CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    if(test==2) { // Every producer draw, not just the first, must ignore raw DS.
      CALL(nvnCommandBufferSetStencilRef,&local,NVN_FACE_FRONT_AND_BACK,0x81);
      CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    }
    if(test==8 || test==9) {
      CALL(nvnCommandBufferBindProgram,&local,&second_producer,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
      CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    }
    CALL(nvnCommandBufferSetRenderTargets,&local,1,output,NULL,depth_target,NULL);
    CALL(nvnCommandBufferClearColor,&local,0,black,15);
    CALL(nvnCommandBufferBindDepthStencilState,&local,test==9?&ds:&readonly);CALL(nvnCommandBufferSetStencilMask,&local,NVN_FACE_FRONT_AND_BACK,0);
    CALL(nvnCommandBufferSetStencilRef,&local,NVN_FACE_FRONT_AND_BACK,0x37);
    CALL(nvnCommandBufferBindProgram,&local,&consumer,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferBindSeparateTextures,&local,NVN_SHADER_STAGE_FRAGMENT,0,4,handles);
    CALL(nvnCommandBufferBindSeparateSamplers,&local,NVN_SHADER_STAGE_FRAGMENT,0,1,&sh);
    CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    if(test==7)CALL(nvnCommandBufferReportCounter,&local,NVN_COUNTER_TYPE_SAMPLES_PASSED,(uintptr_t)(storage+0x90000));
    CALL(nvnCommandBufferResetCounter,&local,NVN_COUNTER_TYPE_SAMPLES_PASSED);
    if(test==6 || test==9) {
      if(test==9)CALL(nvnCommandBufferSetRenderTargets,&local,0,NULL,NULL,depth_target,NULL);
      CALL(nvnCommandBufferSetScissor,&local,4,4,8,8);CALL(nvnCommandBufferClearDepthStencil,&local,.875,1,0x79,255);
    }
    uint64_t p0,d0,a0;vk_pixel_local_stats(&p0,&d0,&a0);uint64_t passes=V.passes,draws_before=V.draws;
    shared_ds_first_pipelines=shared_ds_same_depth_passes=shared_ds_draw_commands=0;
    shared_ds_fail_stage=(test==4 || test==8 || test==9)?0:1;shared_ds_fail_after_draws=test==9?2:test==8?1:0;
    shared_ds_fail_remaining=experiment && (test==4 || test==5 || test==8 || test==9);
    shared_ds_unsafe_control=experiment && test==0 && getenv("DM_TEST_PIXEL_LOCAL_UNSAFE_DS");
    shared_ds_require_load_scope=experiment && test==9;shared_ds_load_scope_checks=0;
    shared_ds_barrier_original=vkCmdPipelineBarrier;vkCmdPipelineBarrier=shared_ds_barrier;
    shared_ds_create_original=vkCreateGraphicsPipelines;shared_ds_pass_original=vkCreateRenderPass;shared_ds_draw_original=vkCmdDraw;
    if(experiment && test!=3 && test!=7)vkCreateGraphicsPipelines=shared_ds_create;
    vkCreateRenderPass=shared_ds_pass;vkCmdDraw=shared_ds_draw;
    submit(CALL(nvnCommandBufferEndRecording,&local));
    vkCreateGraphicsPipelines=shared_ds_create_original;vkCreateRenderPass=shared_ds_pass_original;vkCmdDraw=shared_ds_draw_original;
    vkCmdPipelineBarrier=shared_ds_barrier_original;
    CHECK(shared_ds_fail_remaining==0);
    if(experiment && test==9)CHECK(shared_ds_load_scope_checks>0);
    uint64_t p1,d1,a1;vk_pixel_local_stats(&p1,&d1,&a1);
    int fused=vk_pixel_local_enabled() && test!=7 && !(experiment && (test==3 || test==4));
    int consumer_failed=experiment && (test==5 || test==8 || test==9);
    LOG("[pixel-local shared DS] none=%u case=%u enabled=%d plans=%llu draws=%llu aborts=%llu passes=%llu cmd_draws=%u first_pso=%u same_DS_pass=%u",none,test,experiment,
      (unsigned long long)(p1-p0),(unsigned long long)(d1-d0),(unsigned long long)(a1-a0),(unsigned long long)(V.passes-passes),
      shared_ds_draw_commands,shared_ds_first_pipelines,shared_ds_same_depth_passes);
    CHECK(p1-p0==(unsigned)fused);CHECK(d1-d0==(unsigned)(fused && !consumer_failed));CHECK(a1-a0==(unsigned)consumer_failed);
    CHECK(V.draws-draws_before==(unsigned)(test==9?4:(test==2 || test==8)?3:2));CHECK(shared_ds_draw_commands==(test==9?4u:(test==2 || test==8)?3u:2u));
    if(experiment && test!=3 && test!=7)CHECK(shared_ds_first_pipelines>=(test>=8?2u:1u));
    if(experiment && test==8)CHECK(V.passes-passes==3);
    NVNcopyRegion region={0,0,0,16,16,1};uint8_t pixels[1024];CALL(nvnTextureReadTexels,&textures[4],NULL,&region,pixels);
    for(unsigned k=0;k<256;k++)for(unsigned c=0;c<4;c++) {
      int discarded=(test==1 || test==7) && k%16<8;
      int expected=c==3?255:discarded?(c==2?255:16):(test>=8 && k%16>=8)?144:80;
      CHECK(abs((int)pixels[4*k+c]-expected)<=1);
    }
    // Every source still STOREs, even after either optional PSO rejection.
    CALL(nvnTextureReadTexels,&textures[1],NULL,&region,pixels);
    for(unsigned k=0;k<256;k++)for(unsigned c=0;c<4;c++) {
      int discarded=(test==1 || test==7) && k%16<8;
      CHECK(abs((int)pixels[4*k+c]-(c==3?255:discarded?(c==2?255:0):(c==1?((test>=8 && k%16>=8)?128:64):0)))<=1);
    }
    CALL(nvnTextureReadTexels,&textures[3],NULL,&region,pixels);
    for(unsigned k=0;k<256;k++)for(unsigned c=0;c<4;c++)CHECK(pixels[4*k+c]==(c==3?255:16));
    pixel_local_depth_contents(depth_target,.5,0x37,test==6 || test==9);
    if(test==7) {uint64_t count;memcpy(&count,storage+0x90000,8);CHECK(count==384);}
  }
  V.load_store_op_none=original_none;
  for(unsigned k=0;k<10;k++)CALL(nvnTextureFinalize,&textures[k]);
  CALL(nvnProgramFinalize,&producer);CALL(nvnProgramFinalize,&consumer);CALL(nvnProgramFinalize,&reader);CALL(nvnProgramFinalize,&alias);CALL(nvnProgramFinalize,&fault_consumer);CALL(nvnProgramFinalize,&depth_producer);CALL(nvnProgramFinalize,&second_producer);
  CALL(nvnCommandBufferFinalize,&local);CALL(nvnMemoryPoolFinalize,&pool);free(storage);S=saved;vk_pipeline_state_changed();
  CHECK(vk_validation_error_count()==0);return 0;
}
