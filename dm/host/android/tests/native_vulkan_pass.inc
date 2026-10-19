// 通道前瞻必须跨调用返回/提交余句柄看到后续状态，且不改未使用的深度模板内容。
// Pass lookahead must follow call returns/remaining handles and preserve unused
// depth/stencil contents. GPU color and both aspects are independently read back.
#include "../../../gfx/vulkan/nvn_vk_plan.h"
static void pass_depth_test(const void *vertex,const void *fragment,int format) {
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,1<<20));memset(storage,0,1<<20);
  NVNmemoryPoolBuilder mb={0};NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNcommandBuffer cmd;CHECK(CALL(nvnCommandBufferInitialize,&cmd,&device));
  CALL(nvnCommandBufferAddCommandMemory,&cmd,&pool,0x40000,0xc0000);
  NVNtexture textures[2];
  for(unsigned i=0;i<2;i++) {
    NVNtextureBuilder b={0};CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&b,16);
    CALL(nvnTextureBuilderSetHeight,&b,16);CALL(nvnTextureBuilderSetFormat,&b,i?format:NVN_FORMAT_RGBA8);
    CALL(nvnTextureBuilderSetStorage,&b,&pool,i*65536);CHECK(CALL(nvnTextureInitialize,&textures[i],&b));
  }
  NVNprogram program;NVNshaderData shaders[]={{.control=vertex},{.control=fragment}};
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  const float red[]={1,0,0,1},blue[]={0,0,1,1},viewport[]={0,0,16,16};
  memcpy(storage+0x30000,red,16);memcpy(storage+0x30100,blue,16);
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState plain;CALL(nvnDepthStencilStateSetDefaults,&plain);
  CALL(nvnDepthStencilStateSetDepthTestEnable,&plain,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&plain,0);
  CALL(nvnDepthStencilStateSetStencilTestEnable,&plain,0);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);
  NVNchannelMaskState channels;CALL(nvnChannelMaskStateSetDefaults,&channels);
  const NVNtexture *colors[]={&textures[0]};NVNcopyRegion region={0,0,0,16,16,1};uint8_t pixels[16*16*4];
  int saved=vk_tbdr_enabled();
  for(unsigned baseline=0;baseline<2;baseline++) {
  vk_tbdr_reference(baseline);
  for(unsigned test=0;test<15;test++) {
    CALL(nvnCommandBufferBeginRecording,&cmd);CALL(nvnCommandBufferSetRenderTargets,&cmd,1,colors,NULL,&textures[1],NULL);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);CALL(nvnCommandBufferClearDepthStencil,&cmd,.25f,1,0xa7,255);
    submit(CALL(nvnCommandBufferEndRecording,&cmd));
    uint64_t before=vk_render_depth_omissions();
    uint64_t readonly_before,readonly_after;int readonly_reference;
    vk_readonly_depth_stats(&readonly_before,&readonly_reference);
    CALL(nvnCommandBufferBeginRecording,&cmd);CALL(nvnCommandBufferSetRenderTargets,&cmd,1,colors,NULL,&textures[1],NULL);
    CALL(nvnCommandBufferSetViewports,&cmd,0,1,viewport);CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);
    CALL(nvnCommandBufferBindVertexAttribState,&cmd,0,NULL);CALL(nvnCommandBufferBindVertexStreamState,&cmd,0,NULL);
    CALL(nvnCommandBufferBindPolygonState,&cmd,&polygon);CALL(nvnCommandBufferBindDepthStencilState,&cmd,&plain);
    CALL(nvnCommandBufferBindColorState,&cmd,&color);CALL(nvnCommandBufferBindChannelMaskState,&cmd,&channels);
    CALL(nvnCommandBufferBindProgram,&cmd,&program,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferBindUniformBuffer,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,(uintptr_t)(storage+0x30000),16);
    if(test==1 || test==8) CALL(nvnCommandBufferClearColor,&cmd,0,blue,15);
    CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    NVNcommandHandle first=0,commands[2];
    if(test==5 || test==6 || test==14) { first=CALL(nvnCommandBufferEndRecording,&cmd);CALL(nvnCommandBufferBeginRecording,&cmd); }
    if(test>=2) {
      NVNdepthStencilState late=plain;
      if(test==2 || test==4 || test==5 || test==6 || test==7) {
        CALL(nvnDepthStencilStateSetDepthTestEnable,&late,1);
        CALL(nvnDepthStencilStateSetDepthFunc,&late,test==4?NVN_DEPTH_FUNC_ALWAYS:NVN_DEPTH_FUNC_NEVER);
        CALL(nvnDepthStencilStateSetDepthWriteEnable,&late,test==4);
      }
      if(test==3 || test==9 || test>=10) {
        CALL(nvnDepthStencilStateSetStencilTestEnable,&late,1);
        CALL(nvnDepthStencilStateSetStencilFunc,&late,NVN_FACE_FRONT_AND_BACK,(test==9||test>=11)?NVN_STENCIL_FUNC_ALWAYS:NVN_STENCIL_FUNC_NEVER);
        CALL(nvnDepthStencilStateSetStencilOp,&late,NVN_FACE_FRONT_AND_BACK,NVN_STENCIL_OP_KEEP,NVN_STENCIL_OP_KEEP,(test==9||test>=11)?NVN_STENCIL_OP_REPLACE:NVN_STENCIL_OP_KEEP);
        CALL(nvnCommandBufferSetStencilMask,&cmd,NVN_FACE_FRONT_AND_BACK,test>=11?0:255);
        if(test==13) {
          CALL(nvnDepthStencilStateSetStencilOp,&late,NVN_FACE_FRONT,NVN_STENCIL_OP_KEEP,NVN_STENCIL_OP_KEEP,NVN_STENCIL_OP_KEEP);
          CALL(nvnCommandBufferSetStencilMask,&cmd,NVN_FACE_FRONT,255);
        }
        CALL(nvnCommandBufferSetStencilRef,&cmd,NVN_FACE_FRONT_AND_BACK,42);
      }
      if(test==7) CALL(nvnCommandBufferSetRenderTargets,&cmd,1,colors,NULL,&textures[1],NULL);
      if(test==8) CALL(nvnCommandBufferClearDepthStencil,&cmd,.5f,1,0x77,255);
      CALL(nvnCommandBufferBindDepthStencilState,&cmd,&late);
      CALL(nvnCommandBufferBindUniformBuffer,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,(uintptr_t)(storage+0x30100),16);
      CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
      if(test==12 || test==14) {
        // A later dynamic mask change makes this a real stencil writer.
        // Include it in the initial pass proof even through the child jump.
        CALL(nvnCommandBufferSetStencilMask,&cmd,NVN_FACE_FRONT_AND_BACK,255);
        CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
      }
    }
    if(test==5 || test==14) {
      NVNcommandHandle child=CALL(nvnCommandBufferEndRecording,&cmd);CALL(nvnCommandBufferBeginRecording,&cmd);
      CALL(nvnCommandBufferCopyCommands,&cmd,1,&child);child=CALL(nvnCommandBufferEndRecording,&cmd);
      struct { op_hdr h;uint64_t target; } jump={{OP_JUMP,16},child};
      CALL(nvnCommandBufferBeginRecording,&cmd);commands[0]=first;commands[1]=child;
      commands[1]=(uintptr_t)&jump;
      CALL(nvnCommandBufferCopyCommands,&cmd,2,commands);submit(CALL(nvnCommandBufferEndRecording,&cmd));
    } else if(test==6) {
      commands[0]=first;commands[1]=CALL(nvnCommandBufferEndRecording,&cmd);CALL(nvnQueueSubmitCommands,&queue,2,commands);CALL(nvnQueueFinish,&queue);
    } else submit(CALL(nvnCommandBufferEndRecording,&cmd));
    int enabled=vk_tbdr_enabled();
    CHECK((vk_render_depth_omissions()>before)==(enabled && (test==0 || test==1 || test==7)));
    vk_readonly_depth_stats(&readonly_after,&readonly_reference);
    if(enabled && !readonly_reference && test>=10)
      CHECK((readonly_after>readonly_before)==(test==10 || test==11 || test==13));
    CALL(nvnTextureReadTexels,&textures[0],NULL,&region,pixels);
    for(unsigned i=0;i<256;i++) {
      int blue_result=test==4||test==8||test==9||test>=11;
      uint8_t expected[]={blue_result?0:255,0,blue_result?255:0,255};
      CHECK(!memcmp(pixels+i*4,expected,4));
    }
    mtl_sub_begin();VkImage native=vk_texture_native((tex_t *)&textures[1]);vk_buffer *readback=vk_temp_buffer(1280);
    VkBufferImageCopy copies[]={
      {.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1},.imageExtent={16,16,1}},
      {.bufferOffset=1024,.imageSubresource={VK_IMAGE_ASPECT_STENCIL_BIT,0,0,1},.imageExtent={16,16,1}}};
    vk_barrier();vkCmdCopyImageToBuffer(V.cmd,native,VK_IMAGE_LAYOUT_GENERAL,readback->handle,2,copies);
    vk_internal_wait(vk_internal_commit());
    for(unsigned i=0;i<256;i++) {
      uint32_t bits;memcpy(&bits,readback->map+i*4,4);double z;
      if(format==NVN_FORMAT_DEPTH24_STENCIL8) z=(bits&0xffffff)/16777215.;
      else { float v;memcpy(&v,&bits,4);z=v; }
      CHECK(fabs(z-(test==4?0:test==8?.5:.25))<1e-6);
      CHECK(readback->map[1024+i]==((test==9||test==12||test==14)?42:test==8?0x77:0xa7));
    }
  }
  }
  vk_tbdr_reference(!saved);
  CALL(nvnProgramFinalize,&program);for(unsigned i=0;i<2;i++) CALL(nvnTextureFinalize,&textures[i]);
  CALL(nvnCommandBufferFinalize,&cmd);CALL(nvnMemoryPoolFinalize,&pool);free(storage);
}

// Isolating a clear must preserve both untouched color/depth attachments and
// pixels outside a partial clear. Exercise the ordinary and narrowed paths.
static void clear_sibling_preservation_test(void) {
  vk_state saved=S;
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,1<<20));memset(storage,0,1<<20);
  NVNmemoryPoolBuilder mb={0};NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtexture textures[3];
  for(unsigned i=0;i<3;i++) {
    NVNtextureBuilder b={0};CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&b,16);
    CALL(nvnTextureBuilderSetHeight,&b,16);CALL(nvnTextureBuilderSetFormat,&b,i==2?NVN_FORMAT_DEPTH32F_STENCIL8:NVN_FORMAT_RGBA8);
    CALL(nvnTextureBuilderSetStorage,&b,&pool,i*65536);CHECK(CALL(nvnTextureInitialize,&textures[i],&b));
  }
  const NVNtexture *colors[]={&textures[0],&textures[1]};
  const float red[]={1,0,0,1},green[]={0,1,0,1},blue[]={0,0,1,1},yellow[]={1,1,0,1};
  NVNcopyRegion region={0,0,0,16,16,1};uint8_t pixels[1024];int enabled=vk_tbdr_enabled();
  for(unsigned baseline=0;baseline<2;baseline++) {
    vk_tbdr_reference(0);vk_render_clear_reference(baseline);
    CALL(nvnCommandBufferBeginRecording,&cmd);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,2,colors,NULL,&textures[2],NULL);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);
    CALL(nvnCommandBufferClearColor,&cmd,0,red,15);CALL(nvnCommandBufferClearColor,&cmd,1,green,15);
    CALL(nvnCommandBufferClearDepthStencil,&cmd,.25f,1,0xa7,255);submit(end());
    uint64_t before=vk_render_clear_attachment_omissions();
    for(unsigned test=0;test<3;test++) {
      CALL(nvnCommandBufferBeginRecording,&cmd);
      CALL(nvnCommandBufferSetRenderTargets,&cmd,2,colors,NULL,&textures[2],NULL);
      CALL(nvnCommandBufferSetScissor,&cmd,test==1?4:0,test==1?4:0,test==1?8:16,test==1?8:16);
      if(test<2) CALL(nvnCommandBufferClearColor,&cmd,test,test?yellow:blue,15);
      else CALL(nvnCommandBufferClearDepthStencil,&cmd,.5f,1,0x77,255);
      submit(end());
      for(unsigned color=0;color<2;color++) {
        CALL(nvnTextureReadTexels,&textures[color],NULL,&region,pixels);
        for(unsigned y=0;y<16;y++) for(unsigned x=0;x<16;x++) {
          int inside=x>=4&&x<12&&y>=4&&y<12;
          const uint8_t expected0[]={0,0,255,255},expected1[]={255,255,0,255},expected2[]={0,255,0,255};
          CHECK(!memcmp(pixels+(y*16+x)*4,!color?expected0:test>=1&&inside?expected1:expected2,4));
        }
      }
      mtl_sub_begin();VkImage image=vk_texture_native((tex_t *)&textures[2]);vk_buffer *readback=vk_temp_buffer(1280);
      VkBufferImageCopy copies[]={
        {.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1},.imageExtent={16,16,1}},
        {.bufferOffset=1024,.imageSubresource={VK_IMAGE_ASPECT_STENCIL_BIT,0,0,1},.imageExtent={16,16,1}}};
      vk_barrier();vkCmdCopyImageToBuffer(V.cmd,image,VK_IMAGE_LAYOUT_GENERAL,readback->handle,2,copies);
      vk_internal_wait(vk_internal_commit());
      for(unsigned i=0;i<256;i++) {
        float depth;memcpy(&depth,readback->map+i*4,4);CHECK(depth==(test==2?.5f:.25f));
        CHECK(readback->map[1024+i]==(test==2?0x77:0xa7));
      }
    }
    CHECK(baseline?vk_render_clear_attachment_omissions()==before:vk_render_clear_attachment_omissions()>before);
  }
  vk_render_clear_reference(0);vk_tbdr_reference(!enabled);
  for(unsigned i=0;i<3;i++) CALL(nvnTextureFinalize,&textures[i]);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);S=saved;vk_pipeline_state_changed();
  LOG("[vulkan isolated clear fixture] untouched color/depth/stencil, partial rectangle, merged initialization and reference path passed");
}

// SET_RT can leave a different pass active. Only the full, draw-free clear
// may narrow its new attachment set; do not split reusable/partial/drawn jobs.
static void clear_active_target_test(const void *vertex,const void *fragment) {
  vk_state saved=S;int enabled=vk_tbdr_enabled();
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,1<<21));memset(storage,0,1<<21);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,1<<21);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNcommandBuffer cmd;CHECK(CALL(nvnCommandBufferInitialize,&cmd,&device));
  CALL(nvnCommandBufferAddCommandMemory,&cmd,&pool,0x80000,0x180000);
  NVNtexture textures[4];NVNtextureView mip[3];
  for(unsigned i=0;i<4;i++) {
    NVNtextureBuilder b;CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&b,16);
    CALL(nvnTextureBuilderSetHeight,&b,16);CALL(nvnTextureBuilderSetLevels,&b,i?1:3);
    CALL(nvnTextureBuilderSetFormat,&b,i==2?NVN_FORMAT_DEPTH24_STENCIL8:NVN_FORMAT_RGBA8);
    CALL(nvnTextureBuilderSetStorage,&b,&pool,i*65536);CHECK(CALL(nvnTextureInitialize,&textures[i],&b));
  }
  for(unsigned level=0;level<3;level++) {
    CALL(nvnTextureViewSetDefaults,&mip[level]);CALL(nvnTextureViewSetLevels,&mip[level],level,1);
  }
  NVNprogram program;NVNshaderData shaders[]={{.control=vertex},{.control=fragment}};
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);
  CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);
  CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);
  NVNchannelMaskState channels;CALL(nvnChannelMaskStateSetDefaults,&channels);
  CALL(nvnChannelMaskStateSetChannelMask,&channels,1,0,0,0,0);
  const float red[]={1,0,0,1},green[]={0,1,0,1},blue[]={0,0,1,1},yellow[]={1,1,0,1};
  memcpy(storage+0x40000,red,16);
  const NVNtexture *targets[]={&textures[0],&textures[1]},*old[]={&textures[3]},*sibling[]={&textures[1]};
  const NVNtextureView *views[]={&mip[1],NULL};uint8_t pixels[1024];
  for(unsigned baseline=0;baseline<2;baseline++) for(unsigned test=0;test<5;test++) {
    vk_tbdr_reference(0);vk_render_clear_reference(1);
    // Warm actual attachment layouts before the measured active-pass switch.
    CALL(nvnCommandBufferBeginRecording,&cmd);
    for(unsigned level=0;level<3;level++) {
      const NVNtextureView *v[]={&mip[level]};
      CALL(nvnCommandBufferSetRenderTargets,&cmd,1,targets,v,NULL,NULL);
      CALL(nvnCommandBufferSetScissor,&cmd,0,0,16>>level,16>>level);
      CALL(nvnCommandBufferClearColor,&cmd,0,red,15);
    }
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,sibling,NULL,NULL,NULL);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);CALL(nvnCommandBufferClearColor,&cmd,0,green,15);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,old,NULL,NULL,NULL);CALL(nvnCommandBufferClearColor,&cmd,0,yellow,15);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,2,targets,views,&textures[2],NULL);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,8,8);CALL(nvnCommandBufferClearColor,&cmd,0,red,15);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);CALL(nvnCommandBufferClearDepthStencil,&cmd,.25f,1,0xa7,255);
    submit(CALL(nvnCommandBufferEndRecording,&cmd));
    vk_render_clear_reference(baseline);uint64_t before=vk_render_clear_attachment_omissions();
    CALL(nvnCommandBufferBeginRecording,&cmd);
    if(test==2) {
      CALL(nvnCommandBufferSetRenderTargets,&cmd,2,targets,views,&textures[2],NULL);
      CALL(nvnCommandBufferSetScissor,&cmd,0,0,8,8);CALL(nvnCommandBufferClearColor,&cmd,1,green,15);
    } else {
      CALL(nvnCommandBufferSetRenderTargets,&cmd,1,old,NULL,NULL,NULL);
      CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);CALL(nvnCommandBufferClearColor,&cmd,0,yellow,15);
    }
    if(test!=2) CALL(nvnCommandBufferSetRenderTargets,&cmd,2,targets,views,&textures[2],NULL);
    CALL(nvnCommandBufferSetScissor,&cmd,test==1?2:0,test==1?2:0,test==1?4:test==4?0:8,test==1?4:8);
    CALL(nvnCommandBufferClearColor,&cmd,0,blue,15);
    if(test==0) {
      const NVNtextureView *v[]={&mip[2],NULL};
      CALL(nvnCommandBufferSetRenderTargets,&cmd,2,targets,v,&textures[2],NULL);
      CALL(nvnCommandBufferSetScissor,&cmd,0,0,4,4);CALL(nvnCommandBufferClearColor,&cmd,0,yellow,15);
    } else if(test==3) {
      const float viewport[]={0,0,8,8};
      CALL(nvnCommandBufferSetViewports,&cmd,0,1,viewport);
      CALL(nvnCommandBufferBindVertexAttribState,&cmd,0,NULL);CALL(nvnCommandBufferBindVertexStreamState,&cmd,0,NULL);
      CALL(nvnCommandBufferBindPolygonState,&cmd,&polygon);CALL(nvnCommandBufferBindDepthStencilState,&cmd,&ds);
      CALL(nvnCommandBufferBindColorState,&cmd,&color);CALL(nvnCommandBufferBindChannelMaskState,&cmd,&channels);
      CALL(nvnCommandBufferBindProgram,&cmd,&program,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
      CALL(nvnCommandBufferBindUniformBuffer,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,(uintptr_t)(storage+0x40000),16);
      CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    }
    submit(CALL(nvnCommandBufferEndRecording,&cmd));
    LOG("[vulkan active clear fixture] reference=%u test=%u omissions=%llu",baseline,test,
        (unsigned long long)(vk_render_clear_attachment_omissions()-before));
    CHECK(vk_render_clear_attachment_omissions()-before==(!baseline&&test==0?4:0));
    for(unsigned level=0;level<3;level++) {
      unsigned size=16>>level;NVNcopyRegion region={0,0,0,size,size,1};
      CALL(nvnTextureReadTexels,&textures[0],&mip[level],&region,pixels);
      for(unsigned y=0;y<size;y++) for(unsigned x=0;x<size;x++) {
        int isblue=level==1&&test!=3&&test!=4&&(test!=1||(x>=2&&x<6&&y>=2&&y<6));
        const uint8_t expected[]={isblue?0:255,level==2&&test==0?255:0,isblue?255:0,255};
        CHECK(!memcmp(pixels+(y*size+x)*4,expected,4));
      }
    }
    for(unsigned i=1;i<=3;i+=2) {
      NVNcopyRegion region={0,0,0,16,16,1};CALL(nvnTextureReadTexels,&textures[i],NULL,&region,pixels);
      for(unsigned p=0;p<256;p++) CHECK(!memcmp(pixels+p*4,i==1?(uint8_t[]){0,255,0,255}:(uint8_t[]){255,255,0,255},4));
    }
    mtl_sub_begin();VkImage image=vk_texture_native((tex_t *)&textures[2]);vk_buffer *rb=vk_temp_buffer(1280);
    VkBufferImageCopy copies[]={
      {.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1},.imageExtent={16,16,1}},
      {.bufferOffset=1024,.imageSubresource={VK_IMAGE_ASPECT_STENCIL_BIT,0,0,1},.imageExtent={16,16,1}}};
    vk_barrier();vkCmdCopyImageToBuffer(V.cmd,image,VK_IMAGE_LAYOUT_GENERAL,rb->handle,2,copies);
    vk_internal_wait(vk_internal_commit());
    for(unsigned p=0;p<256;p++) {
      uint32_t bits;memcpy(&bits,rb->map+p*4,4);CHECK(fabs((bits&0xffffff)/16777215.-.25)<1e-6);
      CHECK(rb->map[1024+p]==0xa7);
    }
  }
  vk_render_clear_reference(0);vk_tbdr_reference(!enabled);
  CALL(nvnCommandBufferFinalize,&cmd);CALL(nvnProgramFinalize,&program);
  for(unsigned i=0;i<4;i++) CALL(nvnTextureFinalize,&textures[i]);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);S=saved;vk_pipeline_state_changed();
  LOG("[vulkan active clear fixture] different RT/mips, untouched color/depth/stencil, reusable/partial/drawn/empty fallbacks passed");
}
