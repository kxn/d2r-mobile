#include "../../../gfx/vulkan/nvn_vk_pixel_local_runtime.h"
// Owned GPU geometry + pixel oracle. No game modules or captured shaders.
// Optional deterministic fault injection, not a driver performance result.
// Installed only while a synchronous fixture submission is outstanding.
static PFN_vkCreateGraphicsPipelines pixel_local_create_original;
static unsigned pixel_local_create_failures;
static VKAPI_ATTR VkResult VKAPI_CALL pixel_local_create_fail_subpass1(
    VkDevice device_,VkPipelineCache cache,uint32_t count,
    const VkGraphicsPipelineCreateInfo *infos,const VkAllocationCallbacks *allocator,
    VkPipeline *pipelines_) {
  if(pixel_local_create_failures)for(uint32_t k=0;k<count;k++)if(infos[k].subpass==1) {
    pixel_local_create_failures--;
    for(uint32_t n=0;n<count;n++)pipelines_[n]=VK_NULL_HANDLE;
    return VK_ERROR_UNKNOWN;
  }
  return pixel_local_create_original(device_,cache,count,infos,allocator,pipelines_);
}
// Count an API memory dependency, not elapsed time. A real compute-only X
// writer must reach the eventual ordinary FS reader after a Y upload aborts
// descriptor preparation. Unrelated MRT TRANSFER/COLOR barriers cannot prove it.
static PFN_vkCmdPipelineBarrier pixel_local_batch_barrier_original;
static unsigned pixel_local_batch_compute_reads;
static int pixel_local_batch_dispatched;
static unsigned pixel_local_batch_draws,pixel_local_batch_dependencies_at_consumer;
static PFN_vkCmdDraw pixel_local_batch_draw_original;
static VKAPI_ATTR void VKAPI_CALL pixel_local_batch_draw(VkCommandBuffer command,uint32_t vertices,uint32_t instances,uint32_t first,uint32_t base) {
  if(pixel_local_batch_dispatched && ++pixel_local_batch_draws==2)
    pixel_local_batch_dependencies_at_consumer=pixel_local_batch_compute_reads;
  pixel_local_batch_draw_original(command,vertices,instances,first,base);
}
static PFN_vkCmdDispatch pixel_local_batch_dispatch_original;
static VKAPI_ATTR void VKAPI_CALL pixel_local_batch_dispatch(VkCommandBuffer command,uint32_t x,uint32_t y,uint32_t z) {
  pixel_local_batch_dispatch_original(command,x,y,z);pixel_local_batch_dispatched=1;
}
static VKAPI_ATTR void VKAPI_CALL pixel_local_batch_barrier(
    VkCommandBuffer command,VkPipelineStageFlags source,VkPipelineStageFlags target,
    VkDependencyFlags flags,uint32_t memory_count,const VkMemoryBarrier *memory,
    uint32_t buffer_count,const VkBufferMemoryBarrier *buffers,
    uint32_t image_count,const VkImageMemoryBarrier *images) {
  if(pixel_local_batch_dispatched && (source&(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)) &&
     (target&(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT|VK_PIPELINE_STAGE_ALL_COMMANDS_BIT))) {
    for(unsigned k=0;k<memory_count;k++)
      if((memory[k].srcAccessMask&(VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_MEMORY_WRITE_BIT)) &&
         (memory[k].dstAccessMask&(VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_MEMORY_READ_BIT)))pixel_local_batch_compute_reads++;

  }
  pixel_local_batch_barrier_original(command,source,target,flags,memory_count,memory,
      buffer_count,buffers,image_count,images);
}
static int pixel_local_render_test(void) {
  const char *fault=getenv("DM_TEST_PIXEL_LOCAL_PSO_FAILURE");
  int inject=fault && !strcmp(fault,"1");
  mtl_init(); // Startup options/Volk are lazy until the first pool or GPU use.
  if(inject) {CHECK(vk_pixel_local_enabled());CHECK(V.options.pool_window_bytes==(16u<<20));}
  vk_state saved=S;uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,2<<20));memset(storage,0,2<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,2<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtexture textures[11];for(unsigned k=0;k<11;k++) {
    NVNtextureBuilder b;CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&b,16);
    CALL(nvnTextureBuilderSetHeight,&b,16);CALL(nvnTextureBuilderSetLevels,&b,k==5 || (k>=7 && k!=9)?1:2);
    CALL(nvnTextureBuilderSetFormat,&b,k==6?NVN_FORMAT_RGBX8:k==5?NVN_FORMAT_DEPTH24_STENCIL8:(k==0 || k==7)?NVN_FORMAT_RGBA16F:k==2?NVN_FORMAT_R11G11B10F:NVN_FORMAT_RGBA8);CALL(nvnTextureBuilderSetStorage,&b,&pool,(k<7?k:k==9?7:k==10?14:k+3)*65536);
    if(k==10) {NVNpackagedTextureLayout packaged={{0}};CALL(nvnTextureBuilderSetPackagedTextureLayout,&b,&packaged);}
    CHECK(CALL(nvnTextureInitialize,&textures[k],&b));
    if(k==10) {const tex_t *t=(const tex_t *)&textures[k];for(size_t n=0;n<t->L.size;n+=4)memcpy((uint8_t *)t->mem+n,(uint8_t[]){16,16,16,255},4);}
  }
  NVNprogram producer,consumer,single,neighbor,alpha,vertex_alias,vertex_schema,producer_writer,consumer_writer,late_upload,x_writer;uint8_t controls[13][0x880];for(unsigned k=0;k<13;k++)memset(controls[k],180+k,0x880);
  NVNshaderData shaders[]={{.control=controls[0]},{.control=controls[1]}};
  CHECK(CALL(nvnProgramInitialize,&producer,&device));CHECK(CALL(nvnProgramSetShaders,&producer,2,shaders));
  shaders[1].control=controls[2];CHECK(CALL(nvnProgramInitialize,&consumer,&device));CHECK(CALL(nvnProgramSetShaders,&consumer,2,shaders));
  shaders[1].control=controls[3];CHECK(CALL(nvnProgramInitialize,&single,&device));CHECK(CALL(nvnProgramSetShaders,&single,2,shaders));
  shaders[1].control=controls[4];CHECK(CALL(nvnProgramInitialize,&neighbor,&device));CHECK(CALL(nvnProgramSetShaders,&neighbor,2,shaders));
  shaders[1].control=controls[5];CHECK(CALL(nvnProgramInitialize,&alpha,&device));CHECK(CALL(nvnProgramSetShaders,&alpha,2,shaders));
  shaders[0].control=controls[6];shaders[1].control=controls[7];
  CHECK(CALL(nvnProgramInitialize,&vertex_alias,&device));CHECK(CALL(nvnProgramSetShaders,&vertex_alias,2,shaders));
  shaders[0].control=controls[8];shaders[1].control=controls[2];
  CHECK(CALL(nvnProgramInitialize,&vertex_schema,&device));CHECK(CALL(nvnProgramSetShaders,&vertex_schema,2,shaders));
  shaders[0].control=controls[0];shaders[1].control=controls[9];
  CHECK(CALL(nvnProgramInitialize,&producer_writer,&device));CHECK(CALL(nvnProgramSetShaders,&producer_writer,2,shaders));
  shaders[1].control=controls[10];CHECK(CALL(nvnProgramInitialize,&consumer_writer,&device));CHECK(CALL(nvnProgramSetShaders,&consumer_writer,2,shaders));
  shaders[1].control=controls[11];CHECK(CALL(nvnProgramInitialize,&late_upload,&device));CHECK(CALL(nvnProgramSetShaders,&late_upload,2,shaders));
  shaders[0].control=controls[12];CHECK(CALL(nvnProgramInitialize,&x_writer,&device));CHECK(CALL(nvnProgramSetShaders,&x_writer,1,shaders));
  NVNcommandBuffer local;CHECK(CALL(nvnCommandBufferInitialize,&local,&device));
  CALL(nvnCommandBufferAddCommandMemory,&local,&pool,1<<20,1<<20);
  NVNvertexAttribState attrs[2];for(unsigned k=0;k<2;k++)CALL(nvnVertexAttribStateSetDefaults,&attrs[k]);
  CALL(nvnVertexAttribStateSetFormat,&attrs[0],NVN_FORMAT_RGB32F,0);CALL(nvnVertexAttribStateSetFormat,&attrs[1],NVN_FORMAT_RG32F,12);
  NVNvertexStreamState stream;CALL(nvnVertexStreamStateSetDefaults,&stream);CALL(nvnVertexStreamStateSetStride,&stream,20);
  float *vertices=(void *)(storage+0x80000);memcpy(vertices,(float[]){-1,-1,0,0,1,3,-1,0,2,1,-1,3,0,0,-1},60);
  NVNbufferRange vb={(uintptr_t)vertices,60};float viewport[]={0,0,16,16};
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);
  CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNdepthStencilState readonly=ds;CALL(nvnDepthStencilStateSetStencilTestEnable,&readonly,1);
  CALL(nvnDepthStencilStateSetStencilFunc,&readonly,NVN_FACE_FRONT_AND_BACK,NVN_STENCIL_FUNC_EQUAL);
  CALL(nvnDepthStencilStateSetStencilOp,&readonly,NVN_FACE_FRONT_AND_BACK,NVN_STENCIL_OP_KEEP,NVN_STENCIL_OP_KEEP,NVN_STENCIL_OP_KEEP);
  NVNcolorState cs;CALL(nvnColorStateSetDefaults,&cs);NVNchannelMaskState mask;CALL(nvnChannelMaskStateSetDefaults,&mask);
  const NVNtexture *source[]={&textures[0],&textures[1],&textures[2]},*output[]={&textures[4]};
  mtl_set_tex_pool_n(512);samp_par sampler={.minf=1,.magf=1,.aniso=1,.lod_max=1000};mtl_reg_samp(83,&sampler);
  NVNseparateTextureHandle handles[6];
  NVNseparateSamplerHandle sh=CALL(nvnDeviceGetSeparateSamplerHandle,&device,83);for(unsigned k=0;k<4;k++) {
    mtl_reg_tex(80+k,(const tex_t *)&textures[k],NULL);handles[k]=CALL(nvnDeviceGetSeparateTextureHandle,&device,80+k);
  }
  for(unsigned k=4;k<6;k++) {mtl_reg_tex(80+k,(const tex_t *)&textures[k+5],NULL);handles[k]=CALL(nvnDeviceGetSeparateTextureHandle,&device,80+k);}
  // Materialize input usage before the tested submission, matching steady
  // reuse. A just-promoted image deliberately keeps the ordinary GENERAL
  // recording policy for that submission and must take the safe fallback.
  mtl_sub_begin();for(unsigned k=0;k<9;k++) {
    if(k>=4 && k<=6)continue;
    vk_pixel_local_image token;CHECK(vk_texture_pixel_local_prepare((const tex_t *)&textures[k],k!=3 && k!=8,&token));
    CHECK(token.usage&VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);
    if(k==3 || k==8)CHECK(!(token.usage&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT));
  }
  vk_internal_wait(vk_internal_commit());
  const float blue[]={0,0,1,1},history[]={16/255.f,16/255.f,16/255.f,1},black[]={0,0,0,0};
  for(unsigned test=0;test<26;test++) {
    if(test>=23 && test<=24 && !V.features.fragmentStoresAndAtomics) {
      LOG("[pixel-local render fixture] case=%u not executed: fragmentStoresAndAtomics unavailable",test);continue;
    }
    if(test>=23 && test<=24)memset(storage+0xd0000,0,4);
    // Each change is made through the original CPU data/registry APIs.
    vertices[3]=test==7?.125f:0;
    unsigned first_source=test==10?6:(test==13 || test==15 || test==16)?7:0;
    unsigned history_source=(test==14 || test==15)?8:3;
    source[0]=&textures[first_source];
    view_t mip1={.set_levels=1,.base_level=1,.nlevels=1};
    mtl_reg_tex(80,(const tex_t *)&textures[first_source],test==6?&mip1:NULL);
    mtl_reg_tex(81,(const tex_t *)&textures[test==8?0:1],NULL);
    mtl_reg_tex(83,(const tex_t *)&textures[history_source],NULL);
    uint64_t before_plans,before_draws,before_aborts;vk_pixel_local_stats(&before_plans,&before_draws,&before_aborts);
    CALL(nvnCommandBufferBeginRecording,&local);
    for(unsigned k=0;k<9;k++) {
      if(k==5)continue;
      NVNcopyRegion region={0,0,0,16,16,1};CALL(nvnCommandBufferClearTexture,&local,&textures[k],NULL,&region,(k==3 || k==8)?history:blue,15);
      if(k>=7)continue; // Single-mip inputs have no sibling mip to clear.
      NVNtextureView v;CALL(nvnTextureViewSetDefaults,&v);CALL(nvnTextureViewSetLevels,&v,1,1);region.width=region.height=8;
      CALL(nvnCommandBufferClearTexture,&local,&textures[k],&v,&region,(k==3 || k==8)?history:blue,15);
    }
    CALL(nvnCommandBufferSetRenderTargets,&local,0,NULL,NULL,&textures[5],NULL);
    CALL(nvnCommandBufferSetScissor,&local,0,0,16,16);
    CALL(nvnCommandBufferClearDepthStencil,&local,.5f,1,0,255);
    submit(CALL(nvnCommandBufferEndRecording,&local));
    // test1 has a future copy/unknown operation and must retain two passes;
    // test2 introduces partial scissor and must not convert the sampling.
    if(test==25) {pixel_local_batch_draws=pixel_local_batch_dependencies_at_consumer=0;pixel_local_batch_draw_original=vkCmdDraw;vkCmdDraw=pixel_local_batch_draw;pixel_local_batch_dispatched=0;pixel_local_batch_compute_reads=0;pixel_local_batch_dispatch_original=vkCmdDispatch;vkCmdDispatch=pixel_local_batch_dispatch;pixel_local_batch_barrier_original=vkCmdPipelineBarrier;vkCmdPipelineBarrier=pixel_local_batch_barrier;}
    uint64_t passes=V.passes;
    CALL(nvnCommandBufferBeginRecording,&local);
    if(test==25) {
      NVNimageHandle x=CALL(nvnDeviceGetImageHandle,&device,84);
      CALL(nvnCommandBufferBindProgram,&local,&x_writer,NVN_SHADER_STAGE_COMPUTE_BIT);
      CALL(nvnCommandBufferBindImages,&local,NVN_SHADER_STAGE_COMPUTE,0,1,&x);
      CALL(nvnCommandBufferDispatchCompute,&local,16,16,1);
    }
    CALL(nvnCommandBufferSetViewports,&local,0,1,viewport);CALL(nvnCommandBufferSetScissor,&local,0,0,16,16);
    CALL(nvnCommandBufferBindPolygonState,&local,&polygon);CALL(nvnCommandBufferBindDepthStencilState,&local,&ds);
    CALL(nvnCommandBufferBindColorState,&local,&cs);CALL(nvnCommandBufferBindChannelMaskState,&local,&mask);
    CALL(nvnCommandBufferBindVertexAttribState,&local,2,attrs);CALL(nvnCommandBufferBindVertexStreamState,&local,1,&stream);
    CALL(nvnCommandBufferBindVertexBuffers,&local,0,1,&vb);
    if(test==3 || test==20)CALL(nvnCommandBufferResetCounter,&local,NVN_COUNTER_TYPE_SAMPLES_PASSED);
    CALL(nvnCommandBufferSetRenderTargets,&local,3,source,NULL,NULL,NULL);
    CALL(nvnCommandBufferBindProgram,&local,test==10?&alpha:test==23?&producer_writer:&producer,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    if(test>=23 && test<=24)CALL(nvnCommandBufferBindStorageBuffer,&local,NVN_SHADER_STAGE_FRAGMENT,0,(uintptr_t)storage+0xd0000,4);
    CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    if(test==4)CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    if(test==1)CALL(nvnCommandBufferBarrier,&local,NVN_BARRIER_ORDER_PRIMITIVES_BIT);
    CALL(nvnCommandBufferSetRenderTargets,&local,1,output,NULL,test==5?&textures[5]:NULL,NULL);
    if(test>=17)CALL(nvnCommandBufferBarrier,&local,
        test==18?NVN_BARRIER_ORDER_INDIRECT_DATA_BIT:test==19?NVN_BARRIER_INVALIDATE_SHADER_BIT:
        NVN_BARRIER_ORDER_PRIMITIVES_BIT|NVN_BARRIER_ORDER_FRAGMENTS_BIT|NVN_BARRIER_INVALIDATE_TEXTURE_BIT);
    CALL(nvnCommandBufferClearColor,&local,0,black,15);
    if(test==5) {
      CALL(nvnCommandBufferBindDepthStencilState,&local,&readonly);
      CALL(nvnCommandBufferSetStencilMask,&local,NVN_FACE_FRONT_AND_BACK,0);
      CALL(nvnCommandBufferSetStencilValueMask,&local,NVN_FACE_FRONT_AND_BACK,255);
      CALL(nvnCommandBufferSetStencilRef,&local,NVN_FACE_FRONT_AND_BACK,0);
    }
    if(test==2)CALL(nvnCommandBufferSetScissor,&local,0,0,8,16);
    CALL(nvnCommandBufferBindProgram,&local,(test==4 || test==10 || test==16)?&single:(test==9 || test==21)?&neighbor:test==11?&vertex_alias:(test==12 || test==22)?&vertex_schema:test==24?&consumer_writer:test==25?&late_upload:&consumer,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferBindSeparateTextures,&local,NVN_SHADER_STAGE_FRAGMENT,0,test==25?6:4,handles);
    CALL(nvnCommandBufferBindSeparateSamplers,&local,NVN_SHADER_STAGE_FRAGMENT,0,1,&sh);
    if(test==9 || test==21)CALL(nvnCommandBufferBindSeparateTextures,&local,NVN_SHADER_STAGE_FRAGMENT,4,1,&handles[0]);
    if(test==11 || test==12 || test==22) {
      CALL(nvnCommandBufferBindSeparateTextures,&local,NVN_SHADER_STAGE_VERTEX,0,1,&handles[0]);
      CALL(nvnCommandBufferBindSeparateSamplers,&local,NVN_SHADER_STAGE_VERTEX,0,1,&sh);
    }
    CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    if(test>=4 && test!=20)CALL(nvnCommandBufferResetCounter,&local,NVN_COUNTER_TYPE_SAMPLES_PASSED);
    if(test==3 || test==20)CALL(nvnCommandBufferReportCounter,&local,NVN_COUNTER_TYPE_SAMPLES_PASSED,(uintptr_t)storage+0x90000);
    if(inject && !test) {
      CHECK(!pixel_local_create_original);pixel_local_create_original=vkCreateGraphicsPipelines;
      CHECK(pixel_local_create_original!=NULL);pixel_local_create_failures=1;
      vkCreateGraphicsPipelines=pixel_local_create_fail_subpass1;
    }
    submit(CALL(nvnCommandBufferEndRecording,&local));
    if(test==25) {
      vkCmdPipelineBarrier=pixel_local_batch_barrier_original;vkCmdDispatch=pixel_local_batch_dispatch_original;vkCmdDraw=pixel_local_batch_draw_original;
      LOG("[pixel-local batch abandon] dispatch=%d draws=%u dependencies_before_consumer=%u",pixel_local_batch_dispatched,pixel_local_batch_draws,pixel_local_batch_dependencies_at_consumer);
      CHECK(pixel_local_batch_dispatched);CHECK(pixel_local_batch_draws==2);CHECK(pixel_local_batch_dependencies_at_consumer>0);
      CHECK(!vk_shader_operation_active());CHECK(!vk_texture_shader_collecting());
      if(V.options.pool_window_bytes) {int token=vk_pool_operation_begin_ranges(NULL,0);CHECK(token==1);vk_pool_operation_end(token);}
    }
    if(inject && !test) {
      vkCreateGraphicsPipelines=pixel_local_create_original;pixel_local_create_original=NULL;
      CHECK(pixel_local_create_failures==0);
      // A retry must have retired its operation before acquiring the next one.
      // Whole-pool mode returns0, so it cannot accidentally satisfy this proof.
      int operation=vk_pool_operation_begin_ranges(NULL,0);CHECK(operation==1);
      vk_pool_operation_end(operation);
    }
    if(test==3 || test==20) {uint64_t samples;memcpy(&samples,storage+0x90000,8);CHECK(samples==512);}
    LOG("[pixel-local render fixture] case=%u enabled=%d passes=%llu",test,vk_pixel_local_enabled(),(unsigned long long)(V.passes-passes));
    CHECK(V.passes-passes==(test==25?(vk_pixel_local_enabled()?2:3):inject && !test?2:vk_pixel_local_enabled() && (!test || test==4 || test==5 || (test>=13 && test<=17))?1:test==11 || test==24 || (!vk_pixel_local_enabled() && (test==12 || test==22))?3:2));
    uint64_t plans,draws,aborts;vk_pixel_local_stats(&plans,&draws,&aborts);
    if(vk_pixel_local_enabled() && (test==9 || test==12 || test==21 || test==22)) {CHECK(plans>before_plans);CHECK(aborts>before_aborts);CHECK(draws==before_draws);}
    if(test==25 && vk_pixel_local_enabled()) {CHECK(plans==before_plans+1);CHECK(aborts==before_aborts+1);CHECK(draws==before_draws);}
    if(inject && !test) {CHECK(plans==before_plans+1);CHECK(aborts==before_aborts+1);CHECK(draws==before_draws);}
    if(vk_pixel_local_enabled() && test>=13 && test<=17) {CHECK(plans==before_plans+1);CHECK(draws==before_draws+1);CHECK(aborts==before_aborts);}
    if(vk_pixel_local_enabled() && ((test>=18 && test<=20) || (test>=23 && test<=24))) {
      CHECK(plans==before_plans);CHECK(draws==before_draws);CHECK(aborts==before_aborts);
    }
    NVNcopyRegion region={0,0,0,16,16,1};uint8_t pixels[1024];CALL(nvnTextureReadTexels,&textures[4],NULL,&region,pixels);
    for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++)for(unsigned c=0;c<4;c++) {
      unsigned expected=test==2&&x>=8?0:c==3?255:(test==4 || test==10 || test==16)?(c==0?64:0):
        test==25?128:test==6?(c==0?16:c==1?80:255):test==8?(c==0?144:c==1?16:80):(test==9 || test==11 || test==21)?(c==0?144:80):80;CHECK(abs((int)pixels[(y*16+x)*4+c]-(int)expected)<=1);
    }
    if(test>=23 && test<=24) {uint32_t writes;memcpy(&writes,storage+0xd0000,4);CHECK(writes==256);}
    // These are later consumers outside the fused command stream. Tile-only
    // success cannot hide an accidentally discarded source STORE or sibling.
    if(test!=10) {
      uint16_t half[16*16*4];CALL(nvnTextureReadTexels,source[0],NULL,&region,half);
      for(unsigned i=0;i<256;i++) {CHECK(half[i*4]==0x3404);CHECK(half[i*4+1]==0);CHECK(half[i*4+2]==0);CHECK(half[i*4+3]==0x3c00);}
      if(first_source==0) {
        NVNtextureView v;CALL(nvnTextureViewSetDefaults,&v);CALL(nvnTextureViewSetLevels,&v,1,1);
        NVNcopyRegion small={0,0,0,8,8,1};CALL(nvnTextureReadTexels,&textures[0],&v,&small,half);
        for(unsigned i=0;i<64;i++) {CHECK(half[i*4]==0);CHECK(half[i*4+1]==0);CHECK(half[i*4+2]==0x3c00);CHECK(half[i*4+3]==0x3c00);}
      }
    }
    CALL(nvnTextureReadTexels,&textures[1],NULL,&region,pixels);
    for(unsigned i=0;i<256;i++) {CHECK(pixels[i*4]==0);CHECK(pixels[i*4+1]==64);CHECK(pixels[i*4+2]==0);CHECK(pixels[i*4+3]==255);}
    uint32_t packed[256];CALL(nvnTextureReadTexels,&textures[2],NULL,&region,packed);
    for(unsigned i=0;i<256;i++) {
      CHECK(!(packed[i]&0x3fffff));unsigned b=packed[i]>>22,e=b>>5,m=b&31;
      CHECK(e && e<31);CHECK(fabs(ldexp(1.+m/32.,(int)e-15)-64./255.)<.004);
    }
    CALL(nvnTextureReadTexels,&textures[history_source],NULL,&region,pixels);
    for(unsigned i=0;i<256;i++)for(unsigned c=0;c<4;c++)CHECK(pixels[i*4+c]==(c==3?255:16));
  }
  if(inject)LOG("[pixel-local render fixture] injected subpass1 PSO rejection: ordinary GPU pixels and operation lifetime passed");
  for(unsigned k=0;k<11;k++)CALL(nvnTextureFinalize,&textures[k]);
  CALL(nvnProgramFinalize,&producer);CALL(nvnProgramFinalize,&consumer);CALL(nvnProgramFinalize,&single);CALL(nvnProgramFinalize,&neighbor);CALL(nvnProgramFinalize,&alpha);CALL(nvnProgramFinalize,&vertex_alias);CALL(nvnProgramFinalize,&vertex_schema);CALL(nvnProgramFinalize,&producer_writer);CALL(nvnProgramFinalize,&consumer_writer);CALL(nvnProgramFinalize,&late_upload);CALL(nvnProgramFinalize,&x_writer);CALL(nvnCommandBufferFinalize,&local);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);S=saved;vk_pipeline_state_changed();
  CHECK(vk_validation_error_count()==0);printf("{\"test\":\"pixel-local-render\",\"checks\":%u,\"result\":\"passed\"}\n",checks);return 0;
}
