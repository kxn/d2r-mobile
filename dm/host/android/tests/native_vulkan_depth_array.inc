// Shadow arrays change all layers' layout together; their data and hazards
// remain per layer. Exercise real attachment writes and real sampling through
// the original NVN recorder, using an independent command-memory pool.
static void depth_array_pixels(const NVNtexture *output,const NVNtextureView *view,
                               unsigned expected) {
  NVNcopyRegion region={0,0,0,16,16,1};uint8_t pixels[1024];
  CALL(nvnTextureReadTexels,output,view,&region,pixels);
  for(unsigned p=0;p<256;p++) {
    CHECK(abs((int)pixels[p*4]-(int)expected)<=1);
    CHECK(pixels[p*4+1]==0 && pixels[p*4+2]==0 && pixels[p*4+3]==255);
  }
}
static void depth_array_variant_test(const void *vertex,const void *fragment,
                                    int format,unsigned layers) {
  enum { pool_bytes=4<<20,texture_stride=1<<20,command_offset=2<<20 };
  vk_state saved=S;
  uint64_t transitions,safe,fallback;int saved_layout;
  vk_image_layout_stats(&transitions,&safe,&fallback,&saved_layout);
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,pool_bytes));
  memset(storage,0,pool_bytes);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,pool_bytes);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNcommandBuffer local_cmd;CHECK(CALL(nvnCommandBufferInitialize,&local_cmd,&device));
  CALL(nvnCommandBufferAddCommandMemory,&local_cmd,&pool,command_offset,pool_bytes-command_offset);
  NVNtexture depth,output;
  for(unsigned n=0;n<2;n++) {
    NVNtextureBuilder tb;CALL(nvnTextureBuilderSetDefaults,&tb);
    CALL(nvnTextureBuilderSetDevice,&tb,&device);CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D_ARRAY);
    CALL(nvnTextureBuilderSetWidth,&tb,16);CALL(nvnTextureBuilderSetHeight,&tb,16);
    CALL(nvnTextureBuilderSetDepth,&tb,layers);CALL(nvnTextureBuilderSetLevels,&tb,1);
    CALL(nvnTextureBuilderSetFormat,&tb,n?NVN_FORMAT_RGBA8:format);
    CHECK(CALL(nvnTextureBuilderGetStorageSize,&tb)<=texture_stride-65536);
    CALL(nvnTextureBuilderSetStorage,&tb,&pool,n*texture_stride);
    CHECK(CALL(nvnTextureInitialize,n?&output:&depth,&tb));
  }
  NVNtextureView views[17];
  // Keep descriptor pools clear of both textures and command memory.
  NVNtexturePool tp;CHECK(CALL(nvnTexturePoolInitialize,&tp,&pool,texture_stride-65536,32));
  NVNsamplerPool sp;CHECK(CALL(nvnSamplerPoolInitialize,&sp,&pool,(2*texture_stride)-65536,32));
  for(unsigned layer=0;layer<layers;layer++) {
    CALL(nvnTextureViewSetDefaults,&views[layer]);
    CALL(nvnTextureViewSetLevels,&views[layer],0,1);
    CALL(nvnTextureViewSetLayers,&views[layer],layer,1);
    CALL(nvnTextureViewSetTarget,&views[layer],NVN_TEXTURE_TARGET_2D);
    CALL(nvnTexturePoolRegisterTexture,&tp,layer+1,&depth,&views[layer]);
  }
  NVNsamplerBuilder sb;NVNsampler sampler;
  CALL(nvnSamplerBuilderSetDefaults,&sb);CALL(nvnSamplerBuilderSetDevice,&sb,&device);
  CHECK(CALL(nvnSamplerInitialize,&sampler,&sb));CALL(nvnSamplerPoolRegisterSampler,&sp,1,&sampler);
  NVNseparateSamplerHandle sh=CALL(nvnDeviceGetSeparateSamplerHandle,&device,1);
  NVNprogram program;NVNshaderData shaders[]={{.control=vertex},{.control=fragment}};
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);
  CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState plain,writer,reader;CALL(nvnDepthStencilStateSetDefaults,&plain);
  CALL(nvnDepthStencilStateSetDepthTestEnable,&plain,0);
  CALL(nvnDepthStencilStateSetDepthWriteEnable,&plain,0);
  CALL(nvnDepthStencilStateSetStencilTestEnable,&plain,0);
  writer=plain;CALL(nvnDepthStencilStateSetDepthTestEnable,&writer,1);
  CALL(nvnDepthStencilStateSetDepthWriteEnable,&writer,1);
  CALL(nvnDepthStencilStateSetDepthFunc,&writer,NVN_DEPTH_FUNC_ALWAYS);
  reader=writer;CALL(nvnDepthStencilStateSetDepthWriteEnable,&reader,0);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);
  NVNchannelMaskState channels;CALL(nvnChannelMaskStateSetDefaults,&channels);
  const float viewport[]={0,0,16,16};unsigned changed=layers/2;
  for(unsigned reference=0;reference<2;reference++) {
    vk_image_layout_reference(reference);
    CALL(nvnCommandBufferBeginRecording,&local_cmd);
    CALL(nvnCommandBufferSetTexturePool,&local_cmd,&tp);
    CALL(nvnCommandBufferSetSamplerPool,&local_cmd,&sp);
    CALL(nvnCommandBufferSetViewports,&local_cmd,0,1,viewport);
    CALL(nvnCommandBufferSetScissor,&local_cmd,0,0,16,16);
    CALL(nvnCommandBufferBindVertexAttribState,&local_cmd,0,NULL);
    CALL(nvnCommandBufferBindVertexStreamState,&local_cmd,0,NULL);
    CALL(nvnCommandBufferBindPolygonState,&local_cmd,&polygon);
    CALL(nvnCommandBufferBindDepthStencilState,&local_cmd,&plain);
    CALL(nvnCommandBufferBindColorState,&local_cmd,&color);
    CALL(nvnCommandBufferBindChannelMaskState,&local_cmd,&channels);
    CALL(nvnCommandBufferBindProgram,&local_cmd,&program,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferBindSeparateSamplers,&local_cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&sh);
    for(unsigned layer=0;layer<layers;layer++) {
      CALL(nvnCommandBufferSetRenderTargets,&local_cmd,0,NULL,NULL,&depth,&views[layer]);
      CALL(nvnCommandBufferClearDepthStencil,&local_cmd,(layer%6+1)*.125f,1,
           0x20+layer,format==NVN_FORMAT_DEPTH24_STENCIL8?255:0);
    }
    submit(CALL(nvnCommandBufferEndRecording,&local_cmd));
    // Each draw reads a distinct layer of the same real array. Neither the
    // output array nor the >16-layer negative case may acquire optimal layouts.
    for(unsigned phase=0;phase<2;phase++) {
      uint64_t sampled_before,sampled_after,attachment_before,attachment_after;int mode;
      vk_depth_layout_stats(&attachment_before,&sampled_before,&mode);
      CALL(nvnCommandBufferBeginRecording,&local_cmd);
      CALL(nvnCommandBufferBindDepthStencilState,&local_cmd,&plain);
      for(unsigned layer=0;layer<layers;layer++) {
        const NVNtexture *colors[]={&output};const NVNtextureView *cv[]={&views[layer]};
        NVNseparateTextureHandle th=CALL(nvnDeviceGetSeparateTextureHandle,&device,layer+1);
        CALL(nvnCommandBufferSetRenderTargets,&local_cmd,1,colors,cv,NULL,NULL);
        CALL(nvnCommandBufferBindSeparateTextures,&local_cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&th);
        CALL(nvnCommandBufferDrawArrays,&local_cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
      }
      submit(CALL(nvnCommandBufferEndRecording,&local_cmd));
      vk_depth_layout_stats(&attachment_after,&sampled_after,&mode);
      CHECK((sampled_after>sampled_before)==(!reference && !mode && layers<=16));
      for(unsigned layer=0;layer<layers;layer++)
        depth_array_pixels(&output,&views[layer],phase&&layer==changed?0:
                           (unsigned)lround((layer%6+1)*.125*255));
      if(!phase) {
        // A real depth attachment draw changes only the selected array layer.
        // Sampling layer0 while writing another layer keeps ranges disjoint;
        // the existing array alias policy still conservatively uses GENERAL.
        CALL(nvnCommandBufferBeginRecording,&local_cmd);
        const NVNtexture *colors[]={&output};const NVNtextureView *cv[]={&views[changed]};
        NVNseparateTextureHandle th=CALL(nvnDeviceGetSeparateTextureHandle,&device,1);
        CALL(nvnCommandBufferSetRenderTargets,&local_cmd,1,colors,cv,&depth,&views[changed]);
        CALL(nvnCommandBufferBindDepthStencilState,&local_cmd,&writer);
        CALL(nvnCommandBufferBindSeparateTextures,&local_cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&th);
        CALL(nvnCommandBufferDrawArrays,&local_cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
        submit(CALL(nvnCommandBufferEndRecording,&local_cmd));
      }
    }
    // Same-layer readonly attachment+sample must retain the original GENERAL
    // feedback fallback; introducing optimal array layouts must not bypass it.
    uint64_t a0,s0,a1,s1;int mode;vk_depth_layout_stats(&a0,&s0,&mode);
    CALL(nvnCommandBufferBeginRecording,&local_cmd);
    const NVNtexture *colors[]={&output};const NVNtextureView *cv[]={&views[changed]};
    NVNseparateTextureHandle th=CALL(nvnDeviceGetSeparateTextureHandle,&device,changed+1);
    CALL(nvnCommandBufferSetRenderTargets,&local_cmd,1,colors,cv,&depth,&views[changed]);
    CALL(nvnCommandBufferBindDepthStencilState,&local_cmd,&reader);
    CALL(nvnCommandBufferBindSeparateTextures,&local_cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&th);
    CALL(nvnCommandBufferDrawArrays,&local_cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    submit(CALL(nvnCommandBufferEndRecording,&local_cmd));
    vk_depth_layout_stats(&a1,&s1,&mode);CHECK(s1==s0 && a1==a0);
    depth_array_pixels(&output,&views[changed],0);
    // Check all depth values and (when present) all stencil layers after the
    // whole-array transitions, selected-layer draw, and feedback fallback.
    mtl_sub_begin();VkImage native=vk_texture_native((const tex_t *)&depth);
    size_t depth_bytes=layers*256*4;vk_buffer *readback=vk_temp_buffer(depth_bytes+layers*256);
    VkBufferImageCopy copies[]={
      {.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,layers},.imageExtent={16,16,1}},
      {.bufferOffset=depth_bytes,.imageSubresource={VK_IMAGE_ASPECT_STENCIL_BIT,0,0,layers},.imageExtent={16,16,1}}};
    vk_barrier();vkCmdCopyImageToBuffer(V.cmd,native,VK_IMAGE_LAYOUT_GENERAL,readback->handle,
        format==NVN_FORMAT_DEPTH24_STENCIL8?2:1,copies);
    vk_internal_wait(vk_internal_commit());
    for(unsigned layer=0;layer<layers;layer++) for(unsigned p=0;p<256;p++) {
      double z;
      if(format==NVN_FORMAT_DEPTH16) { uint16_t v;memcpy(&v,readback->map+(layer*256+p)*2,2);z=v/65535.; }
      else { uint32_t v;memcpy(&v,readback->map+(layer*256+p)*4,4);z=(v&0xffffff)/16777215.; }
      CHECK(fabs(z-(layer==changed?0:(layer%6+1)*.125))<2./65535.);
      if(format==NVN_FORMAT_DEPTH24_STENCIL8) CHECK(readback->map[depth_bytes+layer*256+p]==0x20+layer);
    }
  }
  vk_image_layout_reference(saved_layout);
  S=saved;vk_pipeline_state_changed();
  CALL(nvnCommandBufferFinalize,&local_cmd);CALL(nvnProgramFinalize,&program);
  CALL(nvnSamplerPoolFinalize,&sp);CALL(nvnTexturePoolFinalize,&tp);
  CALL(nvnSamplerFinalize,&sampler);CALL(nvnTextureFinalize,&output);CALL(nvnTextureFinalize,&depth);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);
}
static void depth_array_layout_test(const void *vertex,const void *fragment) {
  const unsigned layers[]={2,4,16,17};
  for(unsigned format=0;format<2;format++) for(unsigned n=0;n<4;n++)
    depth_array_variant_test(vertex,fragment,format?NVN_FORMAT_DEPTH24_STENCIL8:NVN_FORMAT_DEPTH16,layers[n]);
  LOG("[vulkan depth array fixture] D16/D24S8, 2/4/16 layers optimal and17-layer fallback, per-layer GPU sampling, selected-layer depth writes, stencil preservation and readonly feedback GENERAL passed");
}
