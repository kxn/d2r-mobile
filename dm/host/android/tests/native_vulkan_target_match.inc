// Target equality is local to one draw, rather than inferred from the PSO
// epoch. Direct target restores intentionally leave that epoch unchanged.
static void target_fields_pixels(const NVNtexture *texture,unsigned level,unsigned layer,
                                 unsigned size,int split,const uint8_t left[4],const uint8_t right[4]) {
  NVNtextureView view;CALL(nvnTextureViewSetDefaults,&view);
  CALL(nvnTextureViewSetLevels,&view,level,1);CALL(nvnTextureViewSetLayers,&view,layer,1);
  NVNcopyRegion region={0,0,0,size,size,1};uint8_t pixels[1024];
  CALL(nvnTextureReadTexels,texture,&view,&region,pixels);
  for(unsigned y=0;y<size;y++)for(unsigned x=0;x<size;x++)
    CHECK(!memcmp(pixels+(y*size+x)*4,split&&x<size/2?left:right,4));
}
static void target_fields_depth(const NVNtexture *texture,double expected,unsigned stencil) {
  mtl_sub_begin();VkImage image=vk_texture_native((const tex_t *)texture);vk_buffer *readback=vk_temp_buffer(1280);
  VkBufferImageCopy copies[]={
    {.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1},.imageExtent={16,16,1}},
    {.bufferOffset=1024,.imageSubresource={VK_IMAGE_ASPECT_STENCIL_BIT,0,0,1},.imageExtent={16,16,1}}};
  vk_barrier();vkCmdCopyImageToBuffer(V.cmd,image,VK_IMAGE_LAYOUT_GENERAL,readback->handle,2,copies);
  vk_internal_wait(vk_internal_commit());
  for(unsigned i=0;i<256;i++) {
    uint32_t bits;memcpy(&bits,readback->map+i*4,4);
    CHECK(fabs((bits&0xffffff)/16777215.-expected)<1e-6);CHECK(readback->map[1024+i]==stencil);
  }
}
static void target_fields_semantics_test(const void *vertex,const void *fragment) {
  vk_state saved=S;uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,1<<20));memset(storage,0,1<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));NVNtexture textures[5];
  for(unsigned i=0;i<5;i++) {
    int depth=i==2||i==4;NVNtextureBuilder b;
    CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,i==3?NVN_TEXTURE_TARGET_2D_ARRAY:NVN_TEXTURE_TARGET_2D);
    CALL(nvnTextureBuilderSetDepth,&b,i==3?2:1);CALL(nvnTextureBuilderSetWidth,&b,16);CALL(nvnTextureBuilderSetHeight,&b,16);
    CALL(nvnTextureBuilderSetLevels,&b,depth?1:2);
    CALL(nvnTextureBuilderSetFormat,&b,depth?NVN_FORMAT_DEPTH24_STENCIL8:NVN_FORMAT_RGBA8);
    CALL(nvnTextureBuilderSetStorage,&b,&pool,i*65536);CHECK(CALL(nvnTextureInitialize,&textures[i],&b));
  }
  NVNprogram program;NVNshaderData shaders[]={{.control=vertex},{.control=fragment}};
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);
  CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);
  CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  const float red[]={1,0,0,1},green[]={0,1,0,1},blue[]={0,0,1,1};
  const uint8_t red8[]={255,0,0,255},green8[]={0,255,0,255},blue8[]={0,0,255,255};
  memcpy(storage+0x80000,red,16);memcpy(storage+0x80100,green,16);
  const char *setting=getenv("DM_VULKAN_DRAW_STATE_REFERENCE");int reference=setting&&!strcmp(setting,"1");
  p_draw draw={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};
  for(unsigned test=0;test<8;test++) {
    mtl_sub_begin();
    S=(vk_state){.program=(const prog_t *)&program,.depth_range={0,1},.chmask={~0u},.has_viewport=1,.has_scissor=1};
    S.poly=*(const poly_st *)&polygon;S.ds=*(const ds_st *)&ds;S.color=*(const color_st *)&color;
    S.viewport[0][2]=S.viewport[0][3]=16;S.scissor=(p_scissor){0,0,8,16};vk_pipeline_state_changed();
    // Initialize every observed subresource independently of the compared RT
    // state, so a missed real target/view change leaves unmistakable pixels.
    for(unsigned i=0;i<4;i++)if(i!=2)for(unsigned level=0;level<2;level++) {
      p_clear_tex clear={.tex=(const tex_t *)&textures[i],.has_view=1,.mask=15,
        .view={.set_levels=1,.base_level=level,.nlevels=1},.r={0,0,0,16>>level,16>>level,i==3?2:1}};
      memcpy(clear.c.f,blue,16);vk_texture_clear(&clear);
    }
    for(unsigned i=2;i<=4;i+=2) {
      p_set_rt depth={.depth=(const tex_t *)&textures[i]};
      p_clear_ds clear={.depth=i==2?.25f:.75f,.depth_mask=1,.stencil=i==2?0xa7:0xb8,.stencil_mask=255};
      vk_target_clear_depth(&depth,NULL,&clear);vk_render_end();
    }
    S.targets=(p_set_rt){.n=test==1?3:1,.color={(const tex_t *)&textures[test==7?3:0]},
      .depth=test==2?(const tex_t *)&textures[2]:NULL};
    if(test==3||test==6||test==7) {
      S.targets.has_cview[0]=1;S.targets.cview[0]=(view_t){.set_levels=1,.nlevels=1};
      if(test==7) { S.targets.cview[0].set_layers=1;S.targets.cview[0].nlayers=1; }
    }
    S.ubo[1][0]=(p_bind_buf){.stage=1,.index=0,.addr=(uintptr_t)storage+0x80000,.size=16};
    uint64_t passes=V.passes,epoch=vk_pipeline_state_epoch();vk_draw(&draw,NULL);
    if(test<4) {
      // Poison padding, every unused color slot, absent-view payload and the
      // views of actual null slots. None may redirect an attachment or split
      // the semantic path's pass. The raw reference deliberately starts anew.
      uint8_t *raw=(void *)&S.targets;
      memset(raw+sizeof S.targets.n,0x91,offsetof(p_set_rt,color)-sizeof S.targets.n);
      size_t tail=offsetof(p_set_rt,has_dview)+sizeof S.targets.has_dview;
      memset(raw+tail,0x73,sizeof S.targets-tail);
      for(unsigned k=S.targets.n;k<8;k++) {
        S.targets.color[k]=(const tex_t *)&textures[1];S.targets.has_cview[k]=1;memset(&S.targets.cview[k],0xa5,sizeof(view_t));
      }
      if(!S.targets.has_cview[0])memset(&S.targets.cview[0],0xb6,sizeof(view_t));
      if(test==1)for(unsigned k=1;k<3;k++) { S.targets.has_cview[k]=1;memset(&S.targets.cview[k],0xc7,sizeof(view_t)); }
      memset(&S.targets.dview,0xd8,sizeof(view_t));if(!S.targets.depth)S.targets.has_dview=1;
      S.scissor=(p_scissor){8,0,8,16};
    } else {
      S.scissor=(p_scissor){0,0,test==5||test==6?8:16,test==5||test==6?8:16};
      if(test==4)S.targets.color[0]=(const tex_t *)&textures[1];
      if(test==5||test==6) {
        S.targets.has_cview[0]=1;S.targets.cview[0]=(view_t){.set_levels=1,.base_level=1,.nlevels=1};
        S.viewport[0][2]=S.viewport[0][3]=8;
      }
      if(test==7)S.targets.cview[0].min_layer=1;
    }
    S.ubo[1][0].addr=(uintptr_t)storage+0x80100;
    if(test&1) {
      p_clear_color clear={.mask=15};memcpy(clear.rgba,green,16);
      vk_target_clear_color(&S.targets,&S.scissor,&clear);
    } else vk_draw(&draw,NULL);
    CHECK(vk_pipeline_state_epoch()==epoch);
    CHECK(V.passes-passes==(test<4&&!reference?1:2));
    vk_internal_wait(vk_internal_commit());
    unsigned first=test==7?3:0;
    target_fields_pixels(&textures[first],0,0,16,1,red8,test<4?green8:blue8);
    if(test<4)target_fields_pixels(&textures[first],1,0,8,0,blue8,blue8);
    if(test==4)target_fields_pixels(&textures[1],0,0,16,0,green8,green8);
    if(test==5||test==6)target_fields_pixels(&textures[0],1,0,8,0,green8,green8);
    if(test==7)target_fields_pixels(&textures[3],0,1,16,0,green8,green8);
    target_fields_depth(&textures[2],.25,0xa7);target_fields_depth(&textures[4],.75,0xb8);
  }
  // A real depth target change must clear the new image, never the old pass's
  // attachment. Read both depth and stencil planes through Vulkan afterwards.
  mtl_sub_begin();S.targets=(p_set_rt){.depth=(const tex_t *)&textures[2]};
  p_clear_ds clear={.depth=.125f,.depth_mask=1,.stencil=0x39,.stencil_mask=255};
  uint64_t passes=V.passes;vk_target_clear_depth(&S.targets,NULL,&clear);
  S.targets.depth=(const tex_t *)&textures[4];clear.depth=.875f;clear.stencil=0xe2;
  vk_target_clear_depth(&S.targets,NULL,&clear);CHECK(V.passes-passes==2);
  vk_internal_wait(vk_internal_commit());target_fields_depth(&textures[2],.125,0x39);target_fields_depth(&textures[4],.875,0xe2);
  CALL(nvnProgramFinalize,&program);for(unsigned i=0;i<5;i++)CALL(nvnTextureFinalize,&textures[i]);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);S=saved;vk_pipeline_state_changed();
  LOG("[vulkan target fields fixture] raw-reference=%d real draw/clear pixels: ignored padding/unused and null slots/absent views reuse; actual object, view presence, mip, array layer and depth changes preserved",reference);
}
static void target_match_test(const void *vertex,const void *fragment) {
  vk_state saved=S;
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,1<<20));memset(storage,0,1<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtexture textures[3];
  for(unsigned i=0;i<3;i++) {
    NVNtextureBuilder b;CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_2D);
    CALL(nvnTextureBuilderSetWidth,&b,16);CALL(nvnTextureBuilderSetHeight,&b,16);
    CALL(nvnTextureBuilderSetLevels,&b,i==2?1:3);
    CALL(nvnTextureBuilderSetFormat,&b,i==2?NVN_FORMAT_DEPTH24_STENCIL8:NVN_FORMAT_RGBA8);
    CALL(nvnTextureBuilderSetStorage,&b,&pool,i*65536);CHECK(CALL(nvnTextureInitialize,&textures[i],&b));
  }
  NVNprogram program;NVNshaderData shaders[]={{.control=vertex},{.control=fragment}};
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);
  CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);
  CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  const float palette[6][4]={{1,0,0,1},{0,1,0,1},{0,0,1,1},{1,1,0,1},{0,1,1,1},{1,0,1,1}};
  for(unsigned i=0;i<6;i++) memcpy(storage+0x40000+i*256,palette[i],16);
  p_draw draw={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};
  // This uses the ordinary NVN backend and GPU, with no game module/keys.
  for(unsigned mode=1;mode<=2;mode++) {
    vk_graphics_prepare_mode(mode);mtl_sub_begin();
    S=(vk_state){.program=(const prog_t *)&program,.depth_range={0,1},.chmask={~0u}};
    S.poly=*(const poly_st *)&polygon;S.ds=*(const ds_st *)&ds;S.color=*(const color_st *)&color;
    vk_pipeline_state_changed();
    p_set_rt depth={.depth=(const tex_t *)&textures[2]};
    p_clear_ds clear={.depth=.25f,.depth_mask=1,.stencil=0xa7,.stencil_mask=255};
    vk_target_clear_depth(&depth,NULL,&clear);vk_render_end();
    for(unsigned phase=0;phase<6;phase++) {
      unsigned level=phase%3,size=16>>level;
      // Alternate real objects and selected mips, sometimes retaining DS.
      // No epoch update here: OP_SET_RT has the same direct-copy behavior.
      S.targets=(p_set_rt){.n=1,.color={(const tex_t *)&textures[phase/3]},
        .has_cview={1},.cview={{.set_levels=1,.base_level=level,.nlevels=1}},
        .depth=phase&1?(const tex_t *)&textures[2]:NULL};
      uint64_t epoch=vk_pipeline_state_epoch();
      S.ubo[1][0]=(p_bind_buf){.stage=1,.index=0,.addr=(uintptr_t)storage+0x40000+phase*256,.size=16};
      S.has_viewport=S.has_scissor=1;
      S.viewport[0][0]=S.viewport[0][1]=0;S.viewport[0][2]=phase?size:size/2;S.viewport[0][3]=size;
      S.scissor=(p_scissor){0,0,phase?size:size/2,size};
      uint64_t passes=V.passes;vk_draw(&draw,NULL);
      CHECK(vk_pipeline_state_epoch()==epoch);
      // Pass area follows viewport coverage, not the scissor rectangle.
      // Grow that coverage to force a pass end, then draw every target pixel.
      S.viewport[0][2]=size;S.scissor.w=size;vk_draw(&draw,NULL);
      CHECK(V.passes-passes==(phase?1:2));
      CHECK(vk_pipeline_state_epoch()==epoch);
    }
    vk_internal_wait(vk_internal_commit());
    for(unsigned phase=0;phase<6;phase++) {
      unsigned level=phase%3,size=16>>level;
      NVNtextureView view;CALL(nvnTextureViewSetDefaults,&view);CALL(nvnTextureViewSetLevels,&view,level,1);
      NVNcopyRegion region={0,0,0,size,size,1};uint8_t pixels[1024];
      CALL(nvnTextureReadTexels,&textures[phase/3],&view,&region,pixels);
      for(unsigned p=0;p<size*size;p++) for(unsigned c=0;c<4;c++)
        CHECK(pixels[p*4+c]==(uint8_t)(palette[phase][c]*255));
    }
    mtl_sub_begin();VkImage native=vk_texture_native((const tex_t *)&textures[2]);vk_buffer *rb=vk_temp_buffer(1280);
    VkBufferImageCopy copies[]={
      {.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1},.imageExtent={16,16,1}},
      {.bufferOffset=1024,.imageSubresource={VK_IMAGE_ASPECT_STENCIL_BIT,0,0,1},.imageExtent={16,16,1}}};
    vk_barrier();vkCmdCopyImageToBuffer(V.cmd,native,VK_IMAGE_LAYOUT_GENERAL,rb->handle,2,copies);
    vk_internal_wait(vk_internal_commit());
    for(unsigned p=0;p<256;p++) {
      uint32_t bits;memcpy(&bits,rb->map+p*4,4);CHECK(fabs((bits&0xffffff)/16777215.-.25)<1e-6);
      CHECK(rb->map[1024+p]==0xa7);
    }
  }
  vk_graphics_prepare_mode(2);CALL(nvnProgramFinalize,&program);
  for(unsigned i=0;i<3;i++) CALL(nvnTextureFinalize,&textures[i]);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);S=saved;vk_pipeline_state_changed();
  LOG("[vulkan target match fixture] direct target/mip restores without PSO epoch, depth inclusion, area growth and repeated pass reuse preserve GPU pixels/DS in both prepare modes");
  target_fields_semantics_test(vertex,fragment);
}
