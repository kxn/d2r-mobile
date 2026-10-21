// Independently check store proofs at real command positions, then execute
// the same CALL/JUMP stream on the GPU. No unknown shader is fabricated.
static void unknown_stage_visit(const uint8_t *cursor,const tex_t *source,unsigned *draws,unsigned depth) {
  CHECK(depth<8);
  for(unsigned guard=0;guard<256;guard++) {
    op_hdr h;memcpy(&h,cursor,sizeof h);const void *payload=cursor+8;
    vk_pass_plan_op(cursor);
    if(h.op==OP_END)return;
    if(h.op==OP_CALL || h.op==OP_JUMP) {
      uint64_t handle;memcpy(&handle,payload,8);
      unknown_stage_visit((const void *)(uintptr_t)handle,source,draws,depth+1);
      if(h.op==OP_JUMP)return;
    } else if(h.op==OP_SET_RT) S.targets=*(const p_set_rt *)payload;
    else if(h.op==OP_SCISSOR) { S.scissor=*(const p_scissor *)payload;S.has_scissor=1; }
    else if(h.op==OP_DRAW) {
      vk_pass_plan_done(payload);
      CHECK(vk_pass_store_needed(source,NULL,VK_IMAGE_ASPECT_COLOR_BIT)==(*draws<2 || *draws==5));
      ++*draws;
    } else if(h.op==OP_CLEAR_COLOR) {
      vk_pass_plan_done(payload);CHECK(vk_pass_store_needed(source,NULL,VK_IMAGE_ASPECT_COLOR_BIT)==1);
    } else if(h.op!=OP_BEGIN) vk_state_op(h.op,payload);
    cursor+=h.size;
  }
  CHECK(0);
}
static void unknown_stage_reuse_test(const void *vertex,const void *fragment) {
  vk_state saved=S;uint64_t ignored;int original_reference;
  vk_store_descriptor_stats(&ignored,&ignored,&original_reference);
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,2<<20));memset(storage,0,2<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,2<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNcommandBuffer local;CHECK(CALL(nvnCommandBufferInitialize,&local,&device));
  CALL(nvnCommandBufferAddCommandMemory,&local,&pool,1<<20,1<<20);
  NVNtexture textures[2];
  for(unsigned i=0;i<2;i++) {
    NVNtextureBuilder b;CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&b,16);
    CALL(nvnTextureBuilderSetHeight,&b,16);CALL(nvnTextureBuilderSetFormat,&b,NVN_FORMAT_RGBA8);
    CALL(nvnTextureBuilderSetStorage,&b,&pool,i*65536);CHECK(CALL(nvnTextureInitialize,&textures[i],&b));
  }
  enum { descriptor=37 };mtl_set_tex_pool_n(256);mtl_reg_tex(descriptor,(const tex_t *)&textures[0],NULL);
  NVNprogram program;NVNshaderData shaders[]={{.control=vertex},{.control=fragment}};
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  const float red[]={1,0,0,1},blue[]={0,0,1,1},viewport[]={0,0,16,16};memcpy(storage+0x20000,red,16);
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);
  CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNcolorState cs;CALL(nvnColorStateSetDefaults,&cs);NVNchannelMaskState channels;CALL(nvnChannelMaskStateSetDefaults,&channels);
  const NVNtexture *source[]={&textures[0]},*output[]={&textures[1]};
  for(unsigned reference=0;reference<2;reference++) for(unsigned images=0;images<2;images++) {
    vk_store_descriptor_reference(reference);
    memset(S.tex,0,sizeof S.tex);memset(S.img,0,sizeof S.img);vk_pipeline_state_changed();
    CALL(nvnCommandBufferBeginRecording,&local);CALL(nvnCommandBufferSetRenderTargets,&local,1,source,NULL,NULL,NULL);
    CALL(nvnCommandBufferSetScissor,&local,0,0,16,16);CALL(nvnCommandBufferClearColor,&local,0,red,15);
    submit(CALL(nvnCommandBufferEndRecording,&local));
    NVNcommandHandle children[2];
    for(unsigned nonzero=0;nonzero<2;nonzero++) {
      CALL(nvnCommandBufferBeginRecording,&local);
      if(images) {
        NVNimageHandle handle=nonzero?CALL(nvnDeviceGetImageHandle,&device,descriptor):0;
        CALL(nvnCommandBufferBindImages,&local,3,31,1,&handle);
      } else {
        NVNseparateTextureHandle handle={0};
        if(nonzero) handle=CALL(nvnDeviceGetSeparateTextureHandle,&device,descriptor);
        CALL(nvnCommandBufferBindSeparateTextures,&local,2,127,1,&handle);
      }
      CALL(nvnCommandBufferSetRenderTargets,&local,1,output,NULL,NULL,NULL);
      CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
      children[nonzero]=CALL(nvnCommandBufferEndRecording,&local);
    }
    struct { op_hdr header;uint64_t target; } jump={{OP_JUMP,16},children[0]};
    NVNcommandHandle jump_handle=(uintptr_t)&jump;
    CALL(nvnCommandBufferBeginRecording,&local);
    CALL(nvnCommandBufferSetViewports,&local,0,1,viewport);CALL(nvnCommandBufferSetScissor,&local,0,0,16,16);
    CALL(nvnCommandBufferBindVertexAttribState,&local,0,NULL);CALL(nvnCommandBufferBindVertexStreamState,&local,0,NULL);
    CALL(nvnCommandBufferBindPolygonState,&local,&polygon);CALL(nvnCommandBufferBindDepthStencilState,&local,&ds);
    CALL(nvnCommandBufferBindColorState,&local,&cs);CALL(nvnCommandBufferBindChannelMaskState,&local,&channels);
    CALL(nvnCommandBufferBindProgram,&local,&program,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferBindUniformBuffer,&local,NVN_SHADER_STAGE_FRAGMENT,0,(uintptr_t)storage+0x20000,16);
    CALL(nvnCommandBufferCopyCommands,&local,1,&children[0]);CALL(nvnCommandBufferCopyCommands,&local,1,&children[0]);
    CALL(nvnCommandBufferCopyCommands,&local,1,&children[1]);
    CALL(nvnCommandBufferCopyCommands,&local,1,&jump_handle);CALL(nvnCommandBufferCopyCommands,&local,1,&jump_handle);
    CALL(nvnCommandBufferSetRenderTargets,&local,1,source,NULL,NULL,NULL);
    CALL(nvnCommandBufferClearColor,&local,0,blue,15);CALL(nvnCommandBufferCopyCommands,&local,1,&children[1]);
    NVNcommandHandle handle=CALL(nvnCommandBufferEndRecording,&local);vk_state initial=S;
    mtl_sub_index(&handle,1);unsigned draws=0;unknown_stage_visit((const void *)(uintptr_t)handle,(const tex_t *)&textures[0],&draws,0);
    CHECK(draws==6);mtl_exec_rest(NULL,0);S=initial;vk_pipeline_state_changed();
    submit(handle);
    for(unsigned t=0;t<2;t++) {
      NVNcopyRegion region={0,0,0,16,16,1};uint8_t pixels[1024];CALL(nvnTextureReadTexels,&textures[t],NULL,&region,pixels);
      for(unsigned p=0;p<256;p++) CHECK(!memcmp(pixels+p*4,t?(uint8_t[]){255,0,0,255}:(uint8_t[]){0,0,255,255},4));
    }
  }
  vk_store_descriptor_reference(original_reference);mtl_set_tex_pool_n(4);
  CALL(nvnCommandBufferFinalize,&local);CALL(nvnProgramFinalize,&program);
  for(unsigned i=0;i<2;i++)CALL(nvnTextureFinalize,&textures[i]);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);S=saved;vk_pipeline_state_changed();
  LOG("[vulkan lazy unknown-stage fixture] TEX/IMG last slots, empty/nonempty/empty proofs, repeated CALL and JUMP ordering match independent store scan and actual GPU pixels");
}

// Re-enter the real texture binding path while ending a pass: the original
// drag-follow draw overwrites a colliding cache slot during an outer upload.
static void recipe_reentrant_test(const void *vertex,const void *fragment) {
#ifdef DM_VULKAN_TEST_EMBED
  unsigned vk_test_texture_binding_slot(int,int);
  // Test the implementation's bounded refinement, including storage requests
  // and the largest NVN descriptor id. The GPU cases below must still force a
  // real collision instead of accidentally testing independent cache slots.
  for(unsigned id=0;id<4096;id++) for(unsigned storage=0;storage<2;storage++) {
    unsigned slot=vk_test_texture_binding_slot((int)id,(int)storage);
    CHECK(slot<256);
    CHECK((slot>>2)==(((uint32_t)id*UINT32_C(2654435761)>>26)^storage));
  }
  CHECK(vk_test_texture_binding_slot(0xfffff,0)<256);
  CHECK(vk_test_texture_binding_slot(0xfffff,1)<256);
  CHECK(vk_test_texture_binding_slot(37,0)!=vk_test_texture_binding_slot(126,0));
  CHECK(vk_test_texture_binding_slot(37,0)==vk_test_texture_binding_slot(270,0));
#endif
  void mtl_ui_ghost(int,float,float,const float[4],float,float);
  int mtl_ui_ghost_rect(float[4]);
  vk_state saved=S;uint64_t hits0,prepares,misses;int saved_reference;
  vk_texture_binding_stats(&hits0,&prepares,&misses,&saved_reference);
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,1<<20));memset(storage,0,1<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtexture textures[3];
  for(unsigned i=0;i<3;i++) {
    NVNtextureBuilder b;CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&b,i?4:16);
    CALL(nvnTextureBuilderSetHeight,&b,i?4:16);CALL(nvnTextureBuilderSetFormat,&b,NVN_FORMAT_RGBA8);
    if(i==2) {
      NVNpackagedTextureLayout packaged={{0}};
      CALL(nvnTextureBuilderSetPackagedTextureLayout,&b,&packaged);
    }
    CALL(nvnTextureBuilderSetStorage,&b,&pool,i*65536);CHECK(CALL(nvnTextureInitialize,&textures[i],&b));
  }
  // These two ids share the current direct cache slot. Use distinct physical
  // images so a stale recipe produces a visibly different GPU sample.
  enum { outer_id=37,inner_id=270 };mtl_set_tex_pool_n(512);
  mtl_reg_tex(outer_id,(const tex_t *)&textures[2],NULL);mtl_reg_tex(inner_id,(const tex_t *)&textures[1],NULL);
  NVNprogram program;NVNshaderData shaders[]={{.control=vertex},{.control=fragment}};
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  float quad[]={-.75f,-.75f,0,0,0,-.25f,-.75f,0,1,0,-.75f,-.25f,0,0,1,-.25f,-.25f,0,1,1};
  memcpy(storage+0x30000,quad,sizeof quad);float *matrix=(float *)(storage+0x40000);
  matrix[0]=matrix[5]=matrix[10]=matrix[15]=1;
  NVNvertexAttribState attr[2];for(unsigned i=0;i<2;i++) {
    CALL(nvnVertexAttribStateSetDefaults,&attr[i]);CALL(nvnVertexAttribStateSetStreamIndex,&attr[i],0);
    CALL(nvnVertexAttribStateSetFormat,&attr[i],i?NVN_FORMAT_RG32F:NVN_FORMAT_RGB32F,i?12:0);
  }
  NVNvertexStreamState stream;CALL(nvnVertexStreamStateSetDefaults,&stream);CALL(nvnVertexStreamStateSetStride,&stream,20);
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);
  CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNcolorState cs;CALL(nvnColorStateSetDefaults,&cs);
  const tex_t *upload=(const tex_t *)&textures[2];const float grid[]={.125f,.125f,.25f,.25f};
  CHECK(upload->has_layout);
  for(unsigned reference=0;reference<2;reference++) {
    vk_texture_binding_reference(reference);
    dm_guest_write_prepare((void *)upload->mem,upload->L.size);memset((void *)upload->mem,0x11,upload->L.size);
    int before_mode;vk_texture_binding_stats(&hits0,&prepares,&misses,&before_mode);
    mtl_sub_begin();S=(vk_state){.program=(const prog_t *)&program,.depth_range={0,1},.chmask={~0u}};
    S.poly=*(const poly_st *)&polygon;S.ds=*(const ds_st *)&ds;S.color=*(const color_st *)&cs;
    S.attr[0]=*(const va_st *)&attr[0];S.attr[1]=*(const va_st *)&attr[1];S.stream[0]=*(const vs_st *)&stream;
    S.vertex[0]=(NVNbufferRange){(uintptr_t)storage+0x30000,80};
    S.ubo[0][0]=(p_bind_buf){.stage=0,.index=0,.addr=(uintptr_t)matrix,.size=64};
    S.tex[1][0]=inner_id;S.targets=(p_set_rt){.n=1,.color={(const tex_t *)&textures[0]}};
    S.has_viewport=S.has_scissor=1;S.viewport[0][2]=S.viewport[0][3]=16;S.scissor=(p_scissor){0,0,8,8};
    vk_pipeline_state_changed();
    p_clear_tex clear={.tex=(const tex_t *)&textures[0],.r={0,0,0,16,16,1},.mask=15,.c.f={0,0,0,1}};
    vk_texture_clear(&clear);clear.tex=(const tex_t *)&textures[1];clear.r.width=clear.r.height=4;
    memcpy(clear.c.f,(float[]){0,1,.5f,1},16);vk_texture_clear(&clear);
    VkImageView original=vk_texture_view(outer_id,0);
    mtl_ui_ghost(1,.25f,.25f,grid,.75f,.75f);
    p_draw draw={.prim=NVN_DRAW_PRIMITIVE_TRIANGLE_STRIP,.count=4};vk_draw(&draw,NULL);
    CHECK(vk_render_active());
    // The first draw replaced the colliding slot. Restore the outer recipe
    // without changing its layout or ending the active target's pass.
    CHECK(vk_texture_view(outer_id,0)==original);CHECK(vk_render_active());
    uint64_t draws=V.draws;
    dm_guest_write_prepare((void *)upload->mem,upload->L.size);memset((void *)upload->mem,0x33,upload->L.size);
    // Force an actual logical change then restore, so exact re-registration
    // no-ops cannot erase this intended outer preflight MISS. Its by-value
    // snapshot must survive the nested ghost's colliding physical recipe.
#ifndef DM_ANDROID_APP
    view_t forced_view={.set_levels=1,.base_level=0,.nlevels=1};
    mtl_reg_tex(outer_id,upload,&forced_view);
    // Observe the genuinely different logical value before restore. An old
    // matching snapshot must not be used to revalidate the original recipe.
    tex_t forced_texture;view_t forced_observed;int forced_has_view;
    vk_texture_lookup_scope_begin();
    CHECK(vk_texture_descriptor_view(outer_id,&forced_texture,&forced_observed,&forced_has_view));
    CHECK(forced_has_view && forced_observed.nlevels==1);
    vk_texture_lookup_scope_end();
#endif
    mtl_reg_tex(outer_id,upload,NULL);
    // Keep the logical parent live across the real nested ghost as well:
    // the child may evict the shared slot, never the copied caller value.
    vk_texture_lookup_scope_begin();
    tex_t logical;CHECK(vk_texture_descriptor(outer_id,&logical));CHECK(logical.serial==upload->serial);
    vk_texture_binding_scope_begin();
    vk_texture_prepare_binding(outer_id,0,VK_SHADER_STAGE_FRAGMENT_BIT);
    // CPU reupload ends the pass and actually replays the original ghost;
    // that nested shader resolves inner_id, replacing the outer cached slot.
    CHECK(vk_texture_view(outer_id,0)==original);CHECK(V.draws==draws+1);
    vk_texture_binding_scope_end();
    CHECK(vk_texture_descriptor(outer_id,&logical));CHECK(logical.serial==upload->serial);
    vk_texture_lookup_scope_end();
    CHECK(vk_texture_view(outer_id,0)==original);CHECK(vk_texture_view(outer_id,0)==original);
    float rect[4];CHECK(mtl_ui_ghost_rect(rect));mtl_ui_ghost(0,0,0,NULL,0,0);
    vk_internal_wait(vk_internal_commit());
    uint64_t hits;int mode;vk_texture_binding_stats(&hits,&prepares,&misses,&mode);
    // The explicitly invalidated outer access above is now a snapshot MISS;
    // only the two following unchanged physical recipes count as HITs.
    CHECK(mode==(int)reference);CHECK(hits-hits0==(reference?0:2));
    NVNcopyRegion region={0,0,0,16,16,1};uint8_t pixels[1024];CALL(nvnTextureReadTexels,&textures[0],NULL,&region,pixels);
    for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++) {
      int icon=(x>=2&&x<6&&y>=2&&y<6)||(x>=10&&x<14&&y>=10&&y<14);
      CHECK(!memcmp(pixels+(y*16+x)*4,icon?(uint8_t[]){0,255,128,255}:(uint8_t[]){0,0,0,255},4));
    }
    region.width=region.height=4;CALL(nvnTextureReadTexels,&textures[2],NULL,&region,pixels);
    for(unsigned p=0;p<64;p++) CHECK(pixels[p]==0x33);
    // A physical promotion and an explicit descriptor-format replacement are
    // different identities even when the logical descriptor id stays fixed.
    mtl_sub_begin();VkImageView before=vk_texture_view(outer_id,0);vk_texture_view(outer_id,1);
    VkImageView promoted=vk_texture_view(outer_id,0);CHECK(reference?promoted==before:promoted!=before);
    CHECK(vk_texture_view(outer_id,0)==promoted);
    view_t srgb={.set_format=1,.format=NVN_FORMAT_RGBA8_SRGB};mtl_reg_tex(outer_id,upload,&srgb);
    VkImageView formatted=vk_texture_view(outer_id,0);CHECK(formatted!=promoted);
    mtl_reg_tex(outer_id,upload,NULL);CHECK(vk_texture_view(outer_id,0)!=formatted);
    vk_internal_wait(vk_internal_commit());
    // A scope may survive an internal physical submit during preparation.
    // Its logical snapshot cannot bypass the new recording's registry path.
    mtl_sub_begin();vk_texture_binding_scope_begin();
    mtl_reg_tex(outer_id,upload,NULL);
    vk_texture_prepare_binding(outer_id,0,VK_SHADER_STAGE_FRAGMENT_BIT);
    vk_internal_wait(vk_internal_commit());
    mtl_sub_begin();CHECK(vk_texture_view(outer_id,0));
    vk_texture_binding_scope_end();vk_internal_wait(vk_internal_commit());
    CALL(nvnTextureReadTexels,&textures[2],NULL,&region,pixels);
    for(unsigned p=0;p<64;p++) CHECK(pixels[p]==0x33);
  }
  vk_texture_binding_reference(saved_reference);mtl_set_tex_pool_n(4);CALL(nvnProgramFinalize,&program);
  for(unsigned i=0;i<3;i++)CALL(nvnTextureFinalize,&textures[i]);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);S=saved;vk_pipeline_state_changed();
  LOG("[vulkan recipe replay fixture] cached outer upload, colliding nested ghost draw, post-callback reuse, physical/format promotion and reference GPU pixels passed");
}
