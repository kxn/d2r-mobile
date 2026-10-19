// 原同提交写回前瞻：整块覆盖可丢，读者/拷贝/局部清除/其他 mip 必须保留。
// Upstream same-submit store lookahead: full overwrites kill old contents;
// readers, copies, partial clears and other mips retain them. Compare both modes.
static void store_overwrite_test(const void *vertex,const void *sample_fragment,int format) {
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,1<<20));memset(storage,0,1<<20);
  NVNmemoryPoolBuilder mb={0};NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));NVNtexture textures[4];
  for(unsigned i=0;i<4;i++) {
    NVNtextureBuilder b={0};CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,i==3?NVN_TEXTURE_TARGET_2D_ARRAY:NVN_TEXTURE_TARGET_2D);
    CALL(nvnTextureBuilderSetDepth,&b,i==3?2:1);CALL(nvnTextureBuilderSetWidth,&b,16);
    CALL(nvnTextureBuilderSetHeight,&b,16);CALL(nvnTextureBuilderSetLevels,&b,i==0?2:1);
    CALL(nvnTextureBuilderSetFormat,&b,i==2?format:NVN_FORMAT_RGBA8);CALL(nvnTextureBuilderSetStorage,&b,&pool,i*65536);
    CHECK(CALL(nvnTextureInitialize,&textures[i],&b));
  }
  NVNtexturePool tp;CHECK(CALL(nvnTexturePoolInitialize,&tp,&pool,0x40000,16));
  CALL(nvnTexturePoolRegisterTexture,&tp,1,&textures[0],NULL);
  NVNsamplerBuilder sb;CALL(nvnSamplerBuilderSetDefaults,&sb);CALL(nvnSamplerBuilderSetDevice,&sb,&device);
  NVNsampler sampler;CHECK(CALL(nvnSamplerInitialize,&sampler,&sb));NVNsamplerPool sp;
  CHECK(CALL(nvnSamplerPoolInitialize,&sp,&pool,0x50000,16));CALL(nvnSamplerPoolRegisterSampler,&sp,1,&sampler);
  NVNseparateTextureHandle th=CALL(nvnDeviceGetSeparateTextureHandle,&device,1);
  NVNseparateSamplerHandle sh=CALL(nvnDeviceGetSeparateSamplerHandle,&device,1);
  NVNprogram program;NVNshaderData shaders[]={{.control=vertex},{.control=sample_fragment}};
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);
  CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);NVNchannelMaskState channels;CALL(nvnChannelMaskStateSetDefaults,&channels);
  const float red[]={1,0,0,1},blue[]={0,0,1,1},viewport[]={0,0,16,16};
  const NVNtexture *dst[]={&textures[1]};NVNcopyRegion region={0,0,0,16,16,1};uint8_t pixels[1024];
  int saved=vk_tbdr_enabled();
  for(unsigned baseline=0;baseline<2;baseline++) { vk_tbdr_reference(baseline);
  for(unsigned test=0;test<15;test++) {
    int depth_case=test>=8&&test<12;
    const NVNtexture *initial[]={test>=12?&textures[3]:&textures[0]};
    NVNtextureView first_layer;CALL(nvnTextureViewSetDefaults,&first_layer);
    CALL(nvnTextureViewSetLayers,&first_layer,test==14?1:0,1);const NVNtextureView *initial_views[]={&first_layer};
    CALL(nvnCommandBufferBeginRecording,&cmd);CALL(nvnCommandBufferSetRenderTargets,&cmd,0,NULL,NULL,&textures[2],NULL);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);CALL(nvnCommandBufferClearDepthStencil,&cmd,.25,1,0xa7,255);
    if(test>=12) { NVNcopyRegion array={0,0,0,16,16,2};CALL(nvnCommandBufferClearTexture,&cmd,&textures[3],NULL,&array,blue,15); }
    submit(end());
    uint64_t before=vk_render_store_omissions();
    CALL(nvnCommandBufferBeginRecording,&cmd);CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,initial,test>=12?initial_views:NULL,depth_case?&textures[2]:NULL,NULL);
    if(depth_case) CALL(nvnCommandBufferClearDepthStencil,&cmd,.25,1,0xa7,255);
    CALL(nvnCommandBufferClearColor,&cmd,0,red,15);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,dst,NULL,NULL,NULL);CALL(nvnCommandBufferClearColor,&cmd,0,blue,15);
    if(test==2) {
      CALL(nvnCommandBufferSetViewports,&cmd,0,1,viewport);CALL(nvnCommandBufferBindProgram,&cmd,&program,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
      CALL(nvnCommandBufferBindVertexAttribState,&cmd,0,NULL);CALL(nvnCommandBufferBindVertexStreamState,&cmd,0,NULL);
      CALL(nvnCommandBufferBindPolygonState,&cmd,&polygon);CALL(nvnCommandBufferBindDepthStencilState,&cmd,&ds);
      CALL(nvnCommandBufferBindColorState,&cmd,&color);CALL(nvnCommandBufferBindChannelMaskState,&cmd,&channels);
      CALL(nvnCommandBufferBindSeparateTextures,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&th);
      CALL(nvnCommandBufferBindSeparateSamplers,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&sh);
      CALL(nvnCommandBufferBarrier,&cmd,NVN_BARRIER_INVALIDATE_TEXTURE_BIT);
      CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    }
    if(test==3) CALL(nvnCommandBufferCopyTextureToTexture,&cmd,&textures[0],NULL,&region,&textures[1],NULL,&region,0);
    NVNcommandHandle commands[2];int split=test==5||test==6;
    if(split) { commands[0]=end();CALL(nvnCommandBufferBeginRecording,&cmd); }
    if(test!=7) {
      NVNtextureView mip;const NVNtextureView *views[]={&mip};
      CALL(nvnTextureViewSetDefaults,&mip);CALL(nvnTextureViewSetLevels,&mip,1,1);
      if(test>=12) { CALL(nvnTextureViewSetLevels,&mip,0,1);CALL(nvnTextureViewSetLayers,&mip,test==12?1:0,1); }
      CALL(nvnCommandBufferSetRenderTargets,&cmd,1,initial,test==4||test>=12?views:NULL,depth_case?&textures[2]:NULL,NULL);
      CALL(nvnCommandBufferSetScissor,&cmd,0,0,test==1||test==11?8:16,16);
      if(depth_case) CALL(nvnCommandBufferClearDepthStencil,&cmd,.5,test!=9,0x77,test!=8?255:0);
      else CALL(nvnCommandBufferClearColor,&cmd,0,blue,15);
    }
    if(split) {
      commands[1]=end();
      if(test==5) { CALL(nvnCommandBufferBeginRecording,&cmd);CALL(nvnCommandBufferCopyCommands,&cmd,2,commands);submit(end()); }
      else { CALL(nvnQueueSubmitCommands,&queue,2,commands);CALL(nvnQueueFinish,&queue); }
    } else submit(end());
    uint64_t expected_dead=baseline?0:test==0||test==5||test==6||test==8||test==9||test==13?1:test==10?2:0;
    CHECK(vk_render_store_omissions()-before==expected_dead);
    if(test<12) {
    CALL(nvnTextureReadTexels,&textures[0],NULL,&region,pixels);
    for(unsigned y=0;y<16;y++) for(unsigned x=0;x<16;x++) {
      int blue_pixel=test<7&&test!=4&&(test!=1||x<8);
      uint8_t expected[]={blue_pixel?0:255,0,blue_pixel?255:0,255};CHECK(!memcmp(pixels+(y*16+x)*4,expected,4));
    }
    } else for(unsigned layer=0;layer<2;layer++) {
      NVNtextureView view;CALL(nvnTextureViewSetDefaults,&view);CALL(nvnTextureViewSetLayers,&view,layer,1);
      CALL(nvnTextureReadTexels,&textures[3],&view,&region,pixels);
      int keep_red=(test==12&&layer==0)||(test==14&&layer==1);
      for(unsigned i=0;i<256;i++) { uint8_t expected[]={keep_red?255:0,0,keep_red?0:255,255};CHECK(!memcmp(pixels+i*4,expected,4)); }
    }
    if(test==2||test==3) {
      CALL(nvnTextureReadTexels,&textures[1],NULL,&region,pixels);
      for(unsigned i=0;i<256;i++) CHECK(!memcmp(pixels+i*4,(uint8_t[]){255,0,0,255},4));
    }
    if(test==4) {
      NVNtextureView view;CALL(nvnTextureViewSetDefaults,&view);CALL(nvnTextureViewSetLevels,&view,1,1);
      NVNcopyRegion small={0,0,0,8,8,1};CALL(nvnTextureReadTexels,&textures[0],&view,&small,pixels);
      for(unsigned i=0;i<64;i++) CHECK(!memcmp(pixels+i*4,(uint8_t[]){0,0,255,255},4));
    }
    mtl_sub_begin();VkImage image=vk_texture_native((tex_t *)&textures[2]);vk_buffer *readback=vk_temp_buffer(1280);
    VkBufferImageCopy copies[]={
      {.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1},.imageExtent={16,16,1}},
      {.bufferOffset=1024,.imageSubresource={VK_IMAGE_ASPECT_STENCIL_BIT,0,0,1},.imageExtent={16,16,1}}};
    vk_barrier();vkCmdCopyImageToBuffer(V.cmd,image,VK_IMAGE_LAYOUT_GENERAL,readback->handle,2,copies);
    vk_internal_wait(vk_internal_commit());
    for(unsigned i=0;i<256;i++) {
      int changed=depth_case&&(test!=11||i%16<8);uint32_t bits;memcpy(&bits,readback->map+i*4,4);double z;
      if(format==NVN_FORMAT_DEPTH24_STENCIL8) z=(bits&0xffffff)/16777215.;else { float f;memcpy(&f,&bits,4);z=f; }
      CHECK(fabs(z-(changed&&test!=9?.5:.25))<1e-6);
      CHECK(readback->map[1024+i]==(changed&&test!=8?0x77:0xa7));
    }
  }}
  // 仅数据流检查：共享存储的不同快照、无关拷贝、未知池边界及描述符变更。
  // Metadata-only checks: distinct snapshots sharing storage, unrelated copies,
  // pool boundaries and descriptor mutation. These streams are not GPU draws.
  vk_tbdr_reference(0);
  for(unsigned test=0;test<5;test++) {
    NVNtextureBuilder b;NVNtexture alias;CALL(nvnTextureBuilderSetDefaults,&b);
    CALL(nvnTextureBuilderSetDevice,&b,&device);CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_2D);
    CALL(nvnTextureBuilderSetWidth,&b,16);CALL(nvnTextureBuilderSetHeight,&b,16);
    CALL(nvnTextureBuilderSetFormat,&b,NVN_FORMAT_RGBA8);CALL(nvnTextureBuilderSetStorage,&b,&pool,test==4?512:test?0x60000:0);
    CHECK(CALL(nvnTextureInitialize,&alias,&b));
    CALL(nvnCommandBufferBeginRecording,&cmd);CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,(const NVNtexture *[]){&textures[0]},NULL,NULL,NULL);
    if(test<2||test==4) CALL(nvnCommandBufferCopyTextureToTexture,&cmd,&alias,NULL,&region,&textures[1],NULL,&region,0);
    if(test==2) CALL(nvnCommandBufferSetTexturePool,&cmd,&tp);
    if(test==1) for(unsigned i=0;i<64;i++) CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);
    CALL(nvnCommandBufferClearColor,&cmd,0,blue,15);NVNcommandHandle h=end();
    vk_pass_index_stats before,after;vk_pass_index_snapshot(&before);mtl_sub_index(&h,1);
    vk_pass_plan_op((const uint8_t *)(uintptr_t)h);
    if(test==3) CALL(nvnTexturePoolRegisterTexture,&tp,1,&textures[0],NULL);
    CHECK(vk_pass_store_needed((tex_t *)&textures[0],NULL,VK_IMAGE_ASPECT_COLOR_BIT)==(test==0||test==2||test==4));
    // This metadata-only stream has no physical commit to publish diagnostics.
    // Explicitly publish from its recording owner; getters remain read-only.
    vk_pass_stats_publish();vk_pass_index_snapshot(&after);
    const char *reference=getenv("DM_VULKAN_STORE_REFERENCE");
    if(test==3 && !(reference&&!strcmp(reference,"1"))) CHECK(after.fallbacks>before.fallbacks);
    if(test==1 && !(reference&&!strcmp(reference,"1"))) {
      CHECK(after.reference_ops-before.reference_ops>=64);
      CHECK(after.query_events-before.query_events<=2);
    }
    mtl_exec_rest(NULL,0);CALL(nvnTextureFinalize,&alias);
  }
  // Metadata-only: a same-resource copy repeats the same whole-image read in
  // one sequence. Reads of an overlapping snapshot on BOTH sides of a full
  // clear must remain at their original positions. No undefined self-copy is
  // sent to the GPU; index decisions are checked against the independent scan.
  {
    NVNtextureBuilder b;NVNtexture alias;CALL(nvnTextureBuilderSetDefaults,&b);
    CALL(nvnTextureBuilderSetDevice,&b,&device);CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_2D);
    CALL(nvnTextureBuilderSetWidth,&b,16);CALL(nvnTextureBuilderSetHeight,&b,16);
    CALL(nvnTextureBuilderSetFormat,&b,NVN_FORMAT_RGBA8);CALL(nvnTextureBuilderSetStorage,&b,&pool,0);
    CHECK(CALL(nvnTextureInitialize,&alias,&b));
    CALL(nvnCommandBufferBeginRecording,&cmd);CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,(const NVNtexture *[]){&textures[0]},NULL,NULL,NULL);
    CALL(nvnCommandBufferCopyTextureToTexture,&cmd,&alias,NULL,&region,&alias,NULL,&region,0);
    // Re-establish state inside the later query's suffix: these streams are
    // analyzed without executing their commands, so S still belongs to the
    // preceding GPU fixture. Both index and reference must see the same state.
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,(const NVNtexture *[]){&textures[0]},NULL,NULL,NULL);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);
    CALL(nvnCommandBufferClearColor,&cmd,0,blue,15);
    CALL(nvnCommandBufferCopyTextureToTexture,&cmd,&alias,NULL,&region,&alias,NULL,&region,0);
    NVNcommandHandle h=end();uint64_t events0,duplicates0,events1,duplicates1;
    vk_store_event_stats(&events0,&duplicates0);mtl_sub_index(&h,1);
    const uint8_t *cursor=(const uint8_t *)(uintptr_t)h;vk_pass_plan_op(cursor);
    CHECK(vk_pass_store_needed((tex_t *)&textures[0],NULL,VK_IMAGE_ASPECT_COLOR_BIT)==1);
    op_hdr header;memcpy(&header,cursor,sizeof header);cursor+=header.size;unsigned targets=0;
    for(;;) {
      memcpy(&header,cursor,sizeof header);CHECK(header.op!=OP_END);
      vk_pass_plan_op(cursor);if(header.op==OP_SET_RT&&++targets==2)break;cursor+=header.size;
    }
    CHECK(vk_pass_store_needed((tex_t *)&textures[0],NULL,VK_IMAGE_ASPECT_COLOR_BIT)==0);
    vk_store_event_stats(&events1,&duplicates1);
    const char *reference=getenv("DM_VULKAN_STORE_REFERENCE");
    if(!reference||strcmp(reference,"1")) { CHECK(events1>events0);CHECK(duplicates1>=duplicates0+2); }
    mtl_exec_rest(NULL,0);CALL(nvnTextureFinalize,&alias);
  }
  // One descriptor id changes meaning across builds. Cached hits must not keep
  // the previous texture (including after toggling back to uncached lookup).
  uint64_t cache_hits,cache_misses;int original_descriptor_reference;
  vk_store_descriptor_stats(&cache_hits,&cache_misses,&original_descriptor_reference);
  uint64_t slot_stages,slot_skipped;vk_store_slot_stats(&slot_stages,&slot_skipped);
  for(unsigned mode=0;mode<3;mode++) {
    vk_store_descriptor_reference(mode!=1);
    for(unsigned version=0;version<3;version++) {
      CALL(nvnTexturePoolRegisterTexture,&tp,1,&textures[version==1?1:0],NULL);
      CALL(nvnCommandBufferBeginRecording,&cmd);
      CALL(nvnCommandBufferBindProgram,&cmd,&program,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
      CALL(nvnCommandBufferBindSeparateTextures,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&th);
      CALL(nvnCommandBufferSetRenderTargets,&cmd,1,dst,NULL,NULL,NULL);
      CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
      CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
      CALL(nvnCommandBufferSetRenderTargets,&cmd,1,(const NVNtexture *[]){&textures[0]},NULL,NULL,NULL);
      CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);
      CALL(nvnCommandBufferClearColor,&cmd,0,blue,15);
      NVNcommandHandle h=end();mtl_sub_index(&h,1);
      vk_pass_plan_op((const uint8_t *)(uintptr_t)h);
      CHECK(vk_pass_store_needed((tex_t *)&textures[0],NULL,VK_IMAGE_ASPECT_COLOR_BIT)==(version!=1));
      mtl_exec_rest(NULL,0);
    }
  }
  uint64_t new_hits,new_misses;int current_reference;
  vk_store_descriptor_stats(&new_hits,&new_misses,&current_reference);
  uint64_t new_slot_stages,new_slot_skipped;vk_store_slot_stats(&new_slot_stages,&new_slot_skipped);
  if(!getenv("DM_VULKAN_STORE_REFERENCE") || strcmp(getenv("DM_VULKAN_STORE_REFERENCE"),"1")) {
    // Immutable shader slot classification elides non-image bindings; each
    // actual descriptor still resolves at every original read position.
    CHECK(new_slot_stages>slot_stages);CHECK(new_slot_skipped>slot_skipped);
    CHECK(new_hits>cache_hits);CHECK(new_misses>cache_misses);CHECK(current_reference);
  }
  vk_store_descriptor_reference(original_descriptor_reference);
  // Cached shader slots must still resolve changed descriptor handles. Query
  // after the first draw, so its earlier read cannot hide a stale cache.
  CALL(nvnTexturePoolRegisterTexture,&tp,1,&textures[0],NULL);
  CALL(nvnTexturePoolRegisterTexture,&tp,2,&textures[1],NULL);
  NVNseparateTextureHandle other=CALL(nvnDeviceGetSeparateTextureHandle,&device,2);
  for(unsigned variant=0;variant<3;variant++) {
    NVNseparateTextureHandle next={0};if(variant==0) next=other;else if(variant==1) next=th;
    CALL(nvnCommandBufferBeginRecording,&cmd);
    CALL(nvnCommandBufferBindProgram,&cmd,&program,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferBindSeparateTextures,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&th);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,dst,NULL,NULL,NULL);
    CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    CALL(nvnCommandBufferBindSeparateTextures,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&next);
    CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,(const NVNtexture *[]){&textures[0]},NULL,NULL,NULL);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);
    CALL(nvnCommandBufferClearColor,&cmd,0,blue,15);
    NVNcommandHandle handle=end();mtl_sub_index(&handle,1);
    const uint8_t *cursor=(const void *)(uintptr_t)handle;unsigned binds=0;
    for(;;) {
      op_hdr header;memcpy(&header,cursor,sizeof header);CHECK(header.op!=OP_END);
      vk_pass_plan_op(cursor);
      if(header.op==OP_BIND_TEX && ++binds==2) break;
      cursor+=header.size;
    }
    CHECK(vk_pass_store_needed((tex_t *)&textures[0],NULL,VK_IMAGE_ASPECT_COLOR_BIT)==(variant==1));
    mtl_exec_rest(NULL,0);
  }
  // Metadata only: repeated attachment events, first use by clear or draw,
  // sparse/reordered/repeated slots and RT changes are checked against the
  // independent raw command scan. No potentially aliased draw reaches Vulkan.
  {
    vk_state saved_state=S;
    NVNtextureBuilder b;NVNtexture alias;CALL(nvnTextureBuilderSetDefaults,&b);
    CALL(nvnTextureBuilderSetDevice,&b,&device);CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_2D);
    CALL(nvnTextureBuilderSetWidth,&b,16);CALL(nvnTextureBuilderSetHeight,&b,16);
    CALL(nvnTextureBuilderSetLevels,&b,2);CALL(nvnTextureBuilderSetFormat,&b,NVN_FORMAT_RGBA8);
    CALL(nvnTextureBuilderSetStorage,&b,&pool,0);CHECK(CALL(nvnTextureInitialize,&alias,&b));
    NVNtextureView mip;CALL(nvnTextureViewSetDefaults,&mip);CALL(nvnTextureViewSetLevels,&mip,1,1);
    vk_pass_index_stats before,after;vk_pass_index_snapshot(&before);
    for(unsigned version=0;version<3;version++) {
      S.program=NULL;memset(S.tex,0,sizeof S.tex);memset(S.img,0,sizeof S.img);
      S.targets=(p_set_rt){.n=1,.color={(const tex_t *)&textures[version==1?1:0]},.depth=(const tex_t *)&textures[2]};
      S.scissor=(p_scissor){0,0,16,16};S.has_scissor=1;
      CALL(nvnCommandBufferBeginRecording,&cmd);
      // Uses the inherited RT: cache must start unresolved on every build.
      CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
      CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
      CALL(nvnCommandBufferClearDepthStencil,&cmd,.5,1,0x77,255);
      const NVNtexture *pair[]={&textures[0],&textures[1]};
      CALL(nvnCommandBufferSetRenderTargets,&cmd,2,pair,NULL,&textures[2],NULL);
      // Slot 1 resolves before slot 0; do not eagerly reorder the buckets.
      CALL(nvnCommandBufferClearColor,&cmd,1,blue,15);
      for(unsigned draw=0;draw<3;draw++)CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
      const NVNtexture *sparse[]={&textures[1],NULL,&textures[0],&textures[0]};
      CALL(nvnCommandBufferSetRenderTargets,&cmd,4,sparse,NULL,NULL,NULL);
      CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
      CALL(nvnCommandBufferClearColor,&cmd,2,red,15);
      CALL(nvnCommandBufferClearColor,&cmd,1,red,15); // NULL slot remains empty.
      CALL(nvnCommandBufferSetRenderTargets,&cmd,1,(const NVNtexture *[]){&textures[3]},NULL,NULL,NULL);
      // This selected RT is never used and must not create an eager bucket.
      const NVNtexture *aliased[]={&alias,&textures[0]};const NVNtextureView *views[]={NULL,&mip};
      CALL(nvnCommandBufferSetRenderTargets,&cmd,2,aliased,views,NULL,NULL);
      CALL(nvnCommandBufferClearColor,&cmd,1,blue,15);
      CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
      CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
      CALL(nvnCommandBufferSetRenderTargets,&cmd,1,(const NVNtexture *[]){&textures[0]},NULL,&textures[2],NULL);
      CALL(nvnCommandBufferClearColor,&cmd,0,blue,15);
      CALL(nvnCommandBufferClearDepthStencil,&cmd,.25,1,0xa7,255);
      NVNcommandHandle handle=end();mtl_sub_index(&handle,1);
      const uint8_t *cursor=(const void *)(uintptr_t)handle;
      for(;;) {
        op_hdr header;memcpy(&header,cursor,sizeof header);vk_pass_plan_op(cursor);
        if(header.op==OP_SET_RT)S.targets=*(const p_set_rt *)(cursor+8);
        // Query both the current operation and its tail, retaining every draw
        // event. STORE_CHECK independently scans raw commands for each query.
        for(unsigned tail=0;tail<2;tail++) {
          if(tail)vk_pass_plan_done(cursor+8);
          for(unsigned n=0;n<2;n++) {
            (void)vk_pass_store_needed((const tex_t *)&textures[n],NULL,VK_IMAGE_ASPECT_COLOR_BIT);
            if(!n)(void)vk_pass_store_needed((const tex_t *)&textures[n],(const view_t *)&mip,VK_IMAGE_ASPECT_COLOR_BIT);
          }
          (void)vk_pass_store_needed((const tex_t *)&alias,NULL,VK_IMAGE_ASPECT_COLOR_BIT);
          (void)vk_pass_store_needed((const tex_t *)&textures[2],NULL,VK_IMAGE_ASPECT_DEPTH_BIT);
          (void)vk_pass_store_needed((const tex_t *)&textures[2],NULL,VK_IMAGE_ASPECT_STENCIL_BIT);
        }
        if(header.op==OP_END)break;cursor+=header.size;
      }
      mtl_exec_rest(NULL,0);
    }
    vk_pass_index_snapshot(&after);
    const char *reference=getenv("DM_VULKAN_STORE_REFERENCE");
    if(!reference||strcmp(reference,"1"))CHECK(after.comparisons>before.comparisons);
    CHECK(after.mismatches==before.mismatches);
    S=saved_state;CALL(nvnTextureFinalize,&alias);
    LOG("[vulkan store target fixture] repeated/sparse/reordered attachment slots, lazy clear/draw first uses, mip/guest aliases and new-build reset agree with raw command scan");
  }
  vk_tbdr_reference(!saved);
  CALL(nvnProgramFinalize,&program);CALL(nvnSamplerFinalize,&sampler);
  for(unsigned i=0;i<4;i++) CALL(nvnTextureFinalize,&textures[i]);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);
}
