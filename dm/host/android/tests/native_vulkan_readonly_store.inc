// STORE_NONE is a no-access operation only for an attachment that received no
// writes. Use real NVN draws, both DS aspects, selected array layers and later
// partial writes; compare with the GENERAL/STORE fallback in the same process.
static void readonly_store_contents(const NVNtexture *depth,int format,int changed) {
  mtl_sub_begin();VkImage native=vk_texture_native((const tex_t *)depth);
  vk_buffer *readback=vk_temp_buffer(2560);
  VkBufferImageCopy copies[]={
    {.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,2},.imageExtent={16,16,1}},
    {.bufferOffset=2048,.imageSubresource={VK_IMAGE_ASPECT_STENCIL_BIT,0,0,2},.imageExtent={16,16,1}}};
  vk_barrier();vkCmdCopyImageToBuffer(V.cmd,native,VK_IMAGE_LAYOUT_GENERAL,readback->handle,2,copies);
  vk_internal_wait(vk_internal_commit());
  for(unsigned layer=0;layer<2;layer++) for(unsigned y=0;y<16;y++) for(unsigned x=0;x<16;x++) {
    unsigned p=layer*256+y*16+x;double expected=layer?.5:.25;unsigned stencil=0x20+layer;
    if(changed && layer==1 && x>=4 && x<12 && y>=4 && y<12) { expected=0;stencil=0x55; }
    if(changed && layer==0 && x>=2 && x<6 && y>=2 && y<6) { expected=.75;stencil=0x77; }
    uint32_t bits;memcpy(&bits,readback->map+p*4,4);double actual;
    if(format==NVN_FORMAT_DEPTH24_STENCIL8) actual=(bits&0xffffff)/16777215.;
    else { float z;memcpy(&z,&bits,4);actual=z; }
    CHECK(fabs(actual-expected)<1e-6);CHECK(readback->map[2048+p]==stencil);
  }
}
static void readonly_store_variant(const void *vertex,const void *fragment,int format) {
  vk_state saved=S;
  uint64_t transitions,safe,fallback;int saved_layout,depth_reference,readonly_reference;
  vk_image_layout_stats(&transitions,&safe,&fallback,&saved_layout);
  uint64_t unused;vk_depth_layout_stats(&unused,&unused,&depth_reference);
  vk_readonly_depth_stats(&unused,&readonly_reference);
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,2<<20));memset(storage,0,2<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,2<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNcommandBuffer local;CHECK(CALL(nvnCommandBufferInitialize,&local,&device));
  CALL(nvnCommandBufferAddCommandMemory,&local,&pool,1<<20,1<<20);
  NVNtexture depth,color;
  for(unsigned n=0;n<2;n++) {
    NVNtextureBuilder b;CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,n?NVN_TEXTURE_TARGET_2D:NVN_TEXTURE_TARGET_2D_ARRAY);
    CALL(nvnTextureBuilderSetWidth,&b,16);CALL(nvnTextureBuilderSetHeight,&b,16);
    if(!n) CALL(nvnTextureBuilderSetDepth,&b,2);
    CALL(nvnTextureBuilderSetFormat,&b,n?NVN_FORMAT_RGBA8:format);
    CALL(nvnTextureBuilderSetStorage,&b,&pool,n*65536);
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
  NVNcolorState cs;CALL(nvnColorStateSetDefaults,&cs);
  NVNchannelMaskState channels;CALL(nvnChannelMaskStateSetDefaults,&channels);
  NVNdepthStencilState reader,writer;CALL(nvnDepthStencilStateSetDefaults,&reader);
  CALL(nvnDepthStencilStateSetDepthTestEnable,&reader,1);CALL(nvnDepthStencilStateSetDepthWriteEnable,&reader,0);
  CALL(nvnDepthStencilStateSetDepthFunc,&reader,NVN_DEPTH_FUNC_ALWAYS);
  CALL(nvnDepthStencilStateSetStencilTestEnable,&reader,1);
  CALL(nvnDepthStencilStateSetStencilFunc,&reader,NVN_FACE_FRONT_AND_BACK,NVN_STENCIL_FUNC_ALWAYS);
  CALL(nvnDepthStencilStateSetStencilOp,&reader,NVN_FACE_FRONT_AND_BACK,NVN_STENCIL_OP_KEEP,NVN_STENCIL_OP_KEEP,NVN_STENCIL_OP_KEEP);
  writer=reader;CALL(nvnDepthStencilStateSetDepthWriteEnable,&writer,1);
  CALL(nvnDepthStencilStateSetStencilOp,&writer,NVN_FACE_FRONT_AND_BACK,NVN_STENCIL_OP_KEEP,NVN_STENCIL_OP_KEEP,NVN_STENCIL_OP_REPLACE);
  const NVNtexture *colors[]={&color};
  for(unsigned reference=0;reference<2;reference++) {
    vk_image_layout_reference(reference);
    CALL(nvnCommandBufferBeginRecording,&local);
    CALL(nvnCommandBufferSetViewports,&local,0,1,viewport);CALL(nvnCommandBufferSetScissor,&local,0,0,16,16);
    CALL(nvnCommandBufferSetDepthRange,&local,0,1);
    CALL(nvnCommandBufferBindVertexAttribState,&local,0,NULL);CALL(nvnCommandBufferBindVertexStreamState,&local,0,NULL);
    CALL(nvnCommandBufferBindPolygonState,&local,&polygon);CALL(nvnCommandBufferBindColorState,&local,&cs);
    CALL(nvnCommandBufferBindChannelMaskState,&local,&channels);
    CALL(nvnCommandBufferBindProgram,&local,&program,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferBindUniformBuffer,&local,NVN_SHADER_STAGE_FRAGMENT,0,(uintptr_t)(storage+0x30000),16);
    CALL(nvnCommandBufferSetStencilMask,&local,NVN_FACE_FRONT_AND_BACK,255);
    for(unsigned layer=0;layer<2;layer++) {
      CALL(nvnCommandBufferSetRenderTargets,&local,1,colors,NULL,&depth,&views[layer]);
      CALL(nvnCommandBufferClearDepthStencil,&local,layer?.5:.25,1,0x20+layer,255);
    }
    submit(CALL(nvnCommandBufferEndRecording,&local));
    uint64_t before=vk_render_readonly_store_omissions();
    // Separate physical submissions cannot inherit or discard previous tile
    // contents. Both aspects must survive all three read-only passes.
    for(unsigned pass=0;pass<3;pass++) {
      CALL(nvnCommandBufferBeginRecording,&local);
      CALL(nvnCommandBufferBindDepthStencilState,&local,&reader);
      for(unsigned layer=0;layer<2;layer++) {
        CALL(nvnCommandBufferSetRenderTargets,&local,1,colors,NULL,&depth,&views[layer]);
        CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
      }
      submit(CALL(nvnCommandBufferEndRecording,&local));
    }
    uint64_t after=vk_render_readonly_store_omissions();
    CHECK((after>before)==(V.load_store_op_none && !reference && !depth_reference && !readonly_reference));
    readonly_store_contents(&depth,format,0);
    NVNcopyRegion region={0,0,0,16,16,1};uint8_t pixels[1024];
    CALL(nvnTextureReadTexels,&color,NULL,&region,pixels);
    for(unsigned p=0;p<256;p++) CHECK(pixels[p*4]==255 && pixels[p*4+1]==0 && pixels[p*4+2]==0 && pixels[p*4+3]==255);
    CALL(nvnCommandBufferBeginRecording,&local);
    CALL(nvnCommandBufferSetRenderTargets,&local,1,colors,NULL,&depth,&views[1]);
    CALL(nvnCommandBufferBindDepthStencilState,&local,&reader);
    CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    // An identical logical rebind ends the planning proof but may retain a
    // physical pass. A genuine later writer must first close read-only use.
    CALL(nvnCommandBufferSetRenderTargets,&local,1,colors,NULL,&depth,&views[1]);
    CALL(nvnCommandBufferBindDepthStencilState,&local,&writer);
    CALL(nvnCommandBufferSetStencilRef,&local,NVN_FACE_FRONT_AND_BACK,0x55);
    CALL(nvnCommandBufferSetScissor,&local,4,4,8,8);
    CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    CALL(nvnCommandBufferSetRenderTargets,&local,1,colors,NULL,&depth,&views[0]);
    CALL(nvnCommandBufferSetScissor,&local,2,2,4,4);
    CALL(nvnCommandBufferClearDepthStencil,&local,.75,1,0x77,255);
    submit(CALL(nvnCommandBufferEndRecording,&local));
    CALL(nvnCommandBufferBeginRecording,&local);
    CALL(nvnCommandBufferBindDepthStencilState,&local,&reader);CALL(nvnCommandBufferSetScissor,&local,0,0,16,16);
    for(unsigned layer=0;layer<2;layer++) {
      CALL(nvnCommandBufferSetRenderTargets,&local,1,colors,NULL,&depth,&views[layer]);
      CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    }
    submit(CALL(nvnCommandBufferEndRecording,&local));
    readonly_store_contents(&depth,format,1);
  }
  vk_image_layout_reference(saved_layout);S=saved;vk_pipeline_state_changed();
  CALL(nvnCommandBufferFinalize,&local);CALL(nvnProgramFinalize,&program);
  CALL(nvnTextureFinalize,&depth);CALL(nvnTextureFinalize,&color);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);
}
static void readonly_store_test(const void *vertex,const void *fragment) {
  readonly_store_variant(vertex,fragment,NVN_FORMAT_DEPTH24_STENCIL8);
  readonly_store_variant(vertex,fragment,NVN_FORMAT_DEPTH32F_STENCIL8);
  LOG("[vulkan readonly store fixture] NONE support=%d; read-only passes preserve both DS aspects/layers, later partial writer and clear survive; GENERAL/STORE comparison passed",V.load_store_op_none);
}
