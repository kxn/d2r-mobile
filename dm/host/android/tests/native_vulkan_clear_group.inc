// The production proof is opcode-only and bounded. These negatives include a
// nested return followed by a real draw: END inside a flattened call is not a
// submission boundary. GPU cases below verify the physical attachment result.
#include "../../../gfx/vulkan/nvn_vk_clear_group.inc"
static void clear_group_depth_test(const void *vertex,const void *fragment,const void *compute) {
  const uint8_t good[]={OP_SCISSOR,OP_CLEAR_COLOR,OP_BIND_STATE,OP_END};
  CHECK(color_clear_group_without_depth(good,1,sizeof good));
  const uint8_t boundaries[]={OP_SET_RT,OP_COPY_B2B,OP_UPDATE_UB,OP_COPY_B2T,
    OP_COPY_T2B,OP_COPY_T2T,OP_CLEAR_TEX,OP_DISPATCH};
  for(unsigned i=0;i<sizeof boundaries;i++) {
    const uint8_t ops[]={OP_CLEAR_COLOR,boundaries[i],OP_DRAW};
    CHECK(color_clear_group_without_depth(ops,1,sizeof ops));
  }
  const uint8_t reject[]={OP_DRAW,OP_DRAW_IDX,OP_CLEAR_DS,OP_CALL,OP_JUMP,
    OP_SIGNAL_EVENT,OP_REPORT_COUNTER,OP_RESET_COUNTER,OP_BARRIER,OP_COUNT_,255};
  for(unsigned i=0;i<sizeof reject;i++) {
    const uint8_t ops[]={OP_CLEAR_COLOR,reject[i],OP_END};
    CHECK(!color_clear_group_without_depth(ops,1,sizeof ops));
  }
  const uint8_t nested[]={OP_CLEAR_COLOR,OP_END,OP_DRAW,OP_END};
  CHECK(!color_clear_group_without_depth(nested,1,sizeof nested));
  uint8_t long_tail[258];memset(long_tail,OP_SCISSOR,sizeof long_tail);
  long_tail[255]=OP_END;CHECK(color_clear_group_without_depth(long_tail,1,256));
  long_tail[255]=OP_SCISSOR;long_tail[256]=OP_END;
  CHECK(!color_clear_group_without_depth(long_tail,1,257));
  CHECK(!color_clear_group_without_depth(long_tail,1,128)); // no complete tail
  CHECK(!color_clear_group_without_depth(NULL,1,0));
  CHECK(!color_clear_group_without_depth(good,0,sizeof good));
  vk_state saved=S;int enabled=vk_tbdr_enabled();
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,2<<20));memset(storage,0,2<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,2<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNcommandBuffer local;CHECK(CALL(nvnCommandBufferInitialize,&local,&device));
  CALL(nvnCommandBufferAddCommandMemory,&local,&pool,1<<20,1<<20);
  NVNtexture textures[3];
  for(unsigned i=0;i<3;i++) {
    NVNtextureBuilder tb;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
    CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);
    CALL(nvnTextureBuilderSetWidth,&tb,16);CALL(nvnTextureBuilderSetHeight,&tb,16);
    CALL(nvnTextureBuilderSetFormat,&tb,i==2?NVN_FORMAT_DEPTH24_STENCIL8:NVN_FORMAT_RGBA8);
    CALL(nvnTextureBuilderSetStorage,&tb,&pool,i*65536);
    CHECK(CALL(nvnTextureInitialize,&textures[i],&tb));
  }
  NVNprogram graphics,cs;NVNshaderData shaders[]={{.control=vertex},{.control=fragment}},cs_shader={.control=compute};
  CHECK(CALL(nvnProgramInitialize,&graphics,&device));CHECK(CALL(nvnProgramSetShaders,&graphics,2,shaders));
  CHECK(CALL(nvnProgramInitialize,&cs,&device));CHECK(CALL(nvnProgramSetShaders,&cs,1,&cs_shader));
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState plain,never;CALL(nvnDepthStencilStateSetDefaults,&plain);
  CALL(nvnDepthStencilStateSetDepthTestEnable,&plain,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&plain,0);
  CALL(nvnDepthStencilStateSetStencilTestEnable,&plain,0);never=plain;
  CALL(nvnDepthStencilStateSetDepthTestEnable,&never,1);CALL(nvnDepthStencilStateSetDepthFunc,&never,NVN_DEPTH_FUNC_NEVER);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);
  NVNchannelMaskState channels;CALL(nvnChannelMaskStateSetDefaults,&channels);
  const NVNtexture *colors[]={&textures[0],&textures[1]};
  const float red[]={1,0,0,1},green[]={0,1,0,1},blue[]={0,0,1,1},yellow[]={1,1,0,1};
  memcpy(storage+0x30000,red,16);uint32_t *src=(void *)(storage+0x40000),*dst=(void *)(storage+0x41000);
  for(unsigned i=0;i<64;i++)src[i]=i+1;
  for(unsigned reference=0;reference<2;reference++) for(unsigned test=0;test<9;test++) {
    // Initialize all actual image contents independently before each case.
    vk_tbdr_reference(1);
    CALL(nvnCommandBufferBeginRecording,&local);
    CALL(nvnCommandBufferSetRenderTargets,&local,2,colors,NULL,&textures[2],NULL);
    CALL(nvnCommandBufferSetScissor,&local,0,0,16,16);
    CALL(nvnCommandBufferClearColor,&local,0,red,15);CALL(nvnCommandBufferClearColor,&local,1,green,15);
    CALL(nvnCommandBufferClearDepthStencil,&local,.25f,1,0xa7,255);
    submit(CALL(nvnCommandBufferEndRecording,&local));
    vk_tbdr_reference(reference);uint64_t before=vk_render_depth_omissions();
    CALL(nvnCommandBufferBeginRecording,&local);
    CALL(nvnCommandBufferSetRenderTargets,&local,2,colors,NULL,&textures[2],NULL);
    CALL(nvnCommandBufferSetScissor,&local,test==8?4:0,test==8?4:0,test==8?8:16,test==8?8:16);
    CALL(nvnCommandBufferBindDepthStencilState,&local,&plain);
    CALL(nvnCommandBufferClearColor,&local,0,blue,15);
    if(test==5)CALL(nvnCommandBufferBarrier,&local,NVN_BARRIER_INVALIDATE_TEXTURE_BIT);
    if(test==6)for(unsigned k=0;k<257;k++)CALL(nvnCommandBufferSetScissor,&local,0,0,16,16);
    CALL(nvnCommandBufferClearColor,&local,1,yellow,15);
    if(test==1)CALL(nvnCommandBufferSetRenderTargets,&local,0,NULL,NULL,NULL,NULL);
    if(test==2)CALL(nvnCommandBufferCopyBufferToBuffer,&local,(uintptr_t)src,(uintptr_t)dst,4,0);
    if(test==3) {
      CALL(nvnCommandBufferSetViewports,&local,0,1,(float[]){0,0,16,16});
      CALL(nvnCommandBufferBindVertexAttribState,&local,0,NULL);CALL(nvnCommandBufferBindVertexStreamState,&local,0,NULL);
      CALL(nvnCommandBufferBindPolygonState,&local,&polygon);CALL(nvnCommandBufferBindColorState,&local,&color);
      CALL(nvnCommandBufferBindChannelMaskState,&local,&channels);CALL(nvnCommandBufferBindDepthStencilState,&local,&never);
      CALL(nvnCommandBufferBindProgram,&local,&graphics,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
      CALL(nvnCommandBufferBindUniformBuffer,&local,NVN_SHADER_STAGE_FRAGMENT,0,(uintptr_t)(storage+0x30000),16);
      CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    }
    if(test==4)CALL(nvnCommandBufferClearDepthStencil,&local,.5f,1,0x77,255);
    if(test==7) {
      CALL(nvnCommandBufferBindProgram,&local,&cs,NVN_SHADER_STAGE_COMPUTE_BIT);
      CALL(nvnCommandBufferBindStorageBuffer,&local,NVN_SHADER_STAGE_COMPUTE,0,(uintptr_t)src,256);
      CALL(nvnCommandBufferBindStorageBuffer,&local,NVN_SHADER_STAGE_COMPUTE,1,(uintptr_t)dst,256);
      CALL(nvnCommandBufferDispatchCompute,&local,1,1,1);
    }
    submit(CALL(nvnCommandBufferEndRecording,&local));
    uint64_t delta=vk_render_depth_omissions()-before;
    int expected=!reference && (test==0||test==1||test==2||test==7||test==8);
    LOG("[vulkan color clear group fixture] reference=%u test=%u depth_omissions=%llu expected=%d",reference,test,(unsigned long long)delta,expected);
    CHECK(delta==(unsigned)expected);
    if(test==7)for(unsigned i=0;i<64;i++)CHECK(dst[i]==src[i]*3+7);
    if(test==2)CHECK(dst[0]==src[0]);
    NVNcopyRegion region={0,0,0,16,16,1};uint8_t pixels[1024];
    for(unsigned c=0;c<2;c++) {
      CALL(nvnTextureReadTexels,&textures[c],NULL,&region,pixels);
      for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++) {
        int inside=test!=8||(x>=4&&x<12&&y>=4&&y<12);
        const uint8_t *want=c?(inside?(uint8_t[]){255,255,0,255}:(uint8_t[]){0,255,0,255}):
            (inside?(uint8_t[]){0,0,255,255}:(uint8_t[]){255,0,0,255});
        CHECK(!memcmp(pixels+(y*16+x)*4,want,4));
      }
    }
    mtl_sub_begin();VkImage image=vk_texture_native((tex_t *)&textures[2]);vk_buffer *rb=vk_temp_buffer(1280);
    VkBufferImageCopy copies[]={
      {.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1},.imageExtent={16,16,1}},
      {.bufferOffset=1024,.imageSubresource={VK_IMAGE_ASPECT_STENCIL_BIT,0,0,1},.imageExtent={16,16,1}}};
    vk_barrier();vkCmdCopyImageToBuffer(V.cmd,image,VK_IMAGE_LAYOUT_GENERAL,rb->handle,2,copies);
    vk_internal_wait(vk_internal_commit());
    for(unsigned p=0;p<256;p++) {
      uint32_t bits;memcpy(&bits,rb->map+p*4,4);
      CHECK(fabs((bits&0xffffff)/16777215.-(test==4?.5:.25))<1e-6);
      CHECK(rb->map[1024+p]==(test==4?0x77:0xa7));
    }
  }
  vk_tbdr_reference(!enabled);CALL(nvnCommandBufferFinalize,&local);
  CALL(nvnProgramFinalize,&graphics);CALL(nvnProgramFinalize,&cs);
  for(unsigned i=0;i<3;i++)CALL(nvnTextureFinalize,&textures[i]);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);S=saved;vk_pipeline_state_changed();
  LOG("[vulkan color clear group fixture] double colors, DS preservation, real draw/DS clear/copy/compute, bounded/unknown proofs passed");
}
