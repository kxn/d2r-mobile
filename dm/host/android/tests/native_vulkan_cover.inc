// 首绘覆盖：真实 DONT_CARE 与原 LOADPROBE 哨兵，分别和保留 LOAD 的 GPU 输出比较。
// First-draw coverage: compare real DONT_CARE and original LOADPROBE sentinels
// against retained-LOAD GPU output. No game modules or shaders execute here.
static void load_cover_test(void) {
  enum { cases=37,pixels_per_texture=640 };
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,1<<20));memset(storage,0,1<<20);
  NVNmemoryPoolBuilder mb={0};NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));NVNtexture textures[3];
  for(unsigned i=0;i<3;i++) {
    NVNtextureBuilder b={0};CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,i==2?NVN_TEXTURE_TARGET_2D:NVN_TEXTURE_TARGET_2D_ARRAY);
    CALL(nvnTextureBuilderSetWidth,&b,16);CALL(nvnTextureBuilderSetHeight,&b,16);CALL(nvnTextureBuilderSetDepth,&b,i==2?1:2);
    CALL(nvnTextureBuilderSetLevels,&b,i==2?1:2);CALL(nvnTextureBuilderSetFormat,&b,i==2?NVN_FORMAT_DEPTH24_STENCIL8:NVN_FORMAT_RGBA8);
    CALL(nvnTextureBuilderSetStorage,&b,&pool,i*65536);CHECK(CALL(nvnTextureInitialize,&textures[i],&b));
  }
  const unsigned vs_marker[]={15,16,15,15,15,15},fs_marker[]={19,19,17,18,20,21};
  uint8_t controls[6][2][0x880];NVNprogram programs[6];
  for(unsigned i=0;i<6;i++) {
    memset(controls[i][0],vs_marker[i],0x880);memset(controls[i][1],fs_marker[i],0x880);
    NVNshaderData sd[]={{.control=controls[i][0]},{.control=controls[i][1]}};
    CHECK(CALL(nvnProgramInitialize,&programs[i],&device));CHECK(CALL(nvnProgramSetShaders,&programs[i],2,sd));
  }
  float *vertices=(void *)(storage+0x40000),*other=(void *)(storage+0x41000),*color=(void *)(storage+0x70000);
  const float full[]={-1,-1,0,1, 3,-1,0,1, -1,3,0,1};
  const float small[]={-.625,-.625,0,1, .75,-.625,0,1, -.625,.6875,0,1};
  uint8_t *baseline=malloc(cases*2*pixels_per_texture*4),readback[2*pixels_per_texture*4];CHECK(baseline!=NULL);
  int saved=vk_tbdr_enabled(),saved_probe=vk_load_probe_mode();
  const float green[]={0,1,0,1};
  for(unsigned probe=0;probe<2;probe++) for(unsigned enabled=0;enabled<2;enabled++) for(unsigned test=0;test<cases;test++) {
    vk_tbdr_reference(!enabled);vk_load_probe_request(probe);
    memcpy(vertices,test==22?small:test==9||test==27||test==31?small:full,48);memcpy(other,full,48);
    if(test==3) { memset(vertices,0,48);memcpy(vertices+12,full,48); }
    if(test==19) for(unsigned i=0;i<3;i++) vertices[i*4+2]=1.5f;
    if(test==20) for(unsigned i=0;i<3;i++) vertices[i*4+3]=1.25f;
    memcpy(color,(float[]){1,0,0,test==12||test==28?.5f:1},16);
    memcpy(storage+0x50002,(uint16_t[]){3,4,5},6);memcpy(storage+0x51004,(uint32_t[]){10,11,12},12);
    CALL(nvnCommandBufferBeginRecording,&cmd);
    for(unsigned t=0;t<2;t++) for(unsigned level=0;level<2;level++) {
      NVNtextureView view;CALL(nvnTextureViewSetDefaults,&view);CALL(nvnTextureViewSetLevels,&view,level,1);
      NVNcopyRegion r={0,0,0,16>>level,16>>level,2};CALL(nvnCommandBufferClearTexture,&cmd,&textures[t],&view,&r,green,15);
    }
    CALL(nvnCommandBufferSetRenderTargets,&cmd,0,NULL,NULL,&textures[2],NULL);CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);
    CALL(nvnCommandBufferClearDepthStencil,&cmd,.5f,1,0x77,255);submit(end());
    uint64_t before=vk_render_load_omissions(),clear_before=vk_render_covered_clear_omissions(),fence=mtl_last_fence();
    vk_buffer_sync_stats sync_before,sync_after;vk_buffer_sync_snapshot(&sync_before);
    NVNtextureView view;CALL(nvnTextureViewSetDefaults,&view);CALL(nvnTextureViewSetLevels,&view,test==25?1:0,1);
    CALL(nvnTextureViewSetLayers,&view,test==26?1:0,1);
    const NVNtexture *colors[]={&textures[0],&textures[1]};const NVNtextureView *views[]={&view,&view};
    unsigned size=test==25?8:16;
    NVNvertexAttribState attr;CALL(nvnVertexAttribStateSetDefaults,&attr);CALL(nvnVertexAttribStateSetStreamIndex,&attr,0);
    CALL(nvnVertexAttribStateSetFormat,&attr,test==1?NVN_FORMAT_RG32F:test==2?NVN_FORMAT_RGB32F:NVN_FORMAT_RGBA32F,0);
    NVNvertexStreamState stream;CALL(nvnVertexStreamStateSetDefaults,&stream);CALL(nvnVertexStreamStateSetStride,&stream,16);
    NVNbufferRange range={(uintptr_t)vertices,9*16};NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);
    CALL(nvnPolygonStateSetCullFace,&polygon,test==7||test==8?NVN_FACE_BACK:NVN_FACE_NONE);
    CALL(nvnPolygonStateSetFrontFace,&polygon,test==8?NVN_FRONT_FACE_CW:NVN_FRONT_FACE_CCW);
    NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,test==14||test==33);
    CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetDepthFunc,&ds,NVN_DEPTH_FUNC_NEVER);
    CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,test==15||test==34);CALL(nvnDepthStencilStateSetStencilFunc,&ds,NVN_FACE_FRONT_AND_BACK,NVN_STENCIL_FUNC_NEVER);
    NVNcolorState cs;CALL(nvnColorStateSetDefaults,&cs);CALL(nvnColorStateSetBlendEnable,&cs,0,test==12);
    NVNmultisampleState ms;CALL(nvnMultisampleStateSetDefaults,&ms);CALL(nvnMultisampleStateSetAlphaToCoverageEnable,&ms,test==28);
    NVNblendState blend;CALL(nvnBlendStateSetDefaults,&blend);CALL(nvnBlendStateSetBlendTarget,&blend,0);
    CALL(nvnBlendStateSetBlendFunc,&blend,NVN_BLEND_FUNC_SRC_ALPHA,NVN_BLEND_FUNC_ONE_MINUS_SRC_ALPHA,NVN_BLEND_FUNC_ONE,NVN_BLEND_FUNC_ONE_MINUS_SRC_ALPHA);
    NVNchannelMaskState channels;CALL(nvnChannelMaskStateSetDefaults,&channels);
    if(test==13) CALL(nvnChannelMaskStateSetChannelMask,&channels,0,1,0,0,0);
    if(test==24) CALL(nvnChannelMaskStateSetChannelMask,&channels,1,1,0,0,0);
    int mrt=test==23||test==24||test==29;
    unsigned program=test==20?1:test==16?2:test==17?3:test==18?4:mrt?5:0;
    CALL(nvnCommandBufferBeginRecording,&cmd);CALL(nvnCommandBufferSetRenderTargets,&cmd,mrt&&test!=29?2:1,colors,views,test==14||test==15||test==33||test==34?&textures[2]:NULL,NULL);
    CALL(nvnCommandBufferSetViewports,&cmd,0,1,(float[]){0,0,test==10?8:size,size});
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,test==11?8:size,size);
    CALL(nvnCommandBufferBindProgram,&cmd,&programs[program],NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferBindVertexAttribState,&cmd,1,&attr);CALL(nvnCommandBufferBindVertexStreamState,&cmd,1,&stream);
    CALL(nvnCommandBufferBindVertexBuffers,&cmd,0,1,&range);CALL(nvnCommandBufferBindPolygonState,&cmd,&polygon);
    CALL(nvnCommandBufferBindDepthStencilState,&cmd,&ds);CALL(nvnCommandBufferBindColorState,&cmd,&cs);
    CALL(nvnCommandBufferBindMultisampleState,&cmd,&ms);
    CALL(nvnCommandBufferBindBlendState,&cmd,&blend);CALL(nvnCommandBufferBindChannelMaskState,&cmd,&channels);
    CALL(nvnCommandBufferBindUniformBuffer,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,(uintptr_t)color,16);
    if(test==21||test==22) CALL(nvnCommandBufferUpdateUniformBuffer,&cmd,(uintptr_t)vertices,9*16,0,48,test==21?small:full);
    if(test>=29) {
      // Prepare the query block on a different target before creating the
      // clear job; a reset must not manufacture an untested clear split.
      CALL(nvnCommandBufferBindProgram,&cmd,&programs[0],NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
      CALL(nvnCommandBufferSetRenderTargets,&cmd,1,colors+1,views+1,NULL,NULL);
      CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,0);
      CALL(nvnCommandBufferSetRenderTargets,&cmd,test==29?2:1,colors,views,
           test==33||test==34?&textures[2]:NULL,NULL);
      CALL(nvnCommandBufferClearColor,&cmd,0,(float[]){0,0,1,1},15);
      if(test==33||test==34)
        CALL(nvnCommandBufferClearDepthStencil,&cmd,.5f,1,0x77,255);
      if(test==32) {
        // A real intermediate reader must see blue even though the later
        // first draw writes red over every pixel of the original texture.
        NVNcopyRegion region={0,0,0,16,16,1};
        CALL(nvnCommandBufferCopyTextureToTexture,&cmd,&textures[0],&view,&region,
             &textures[1],&view,&region,0);
      }
      if(test==36) {
        CALL(nvnCommandBufferSetScissor,&cmd,0,0,8,16);
        CALL(nvnCommandBufferClearColor,&cmd,0,(float[]){0,1,1,1},15);
        CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);
      }
      CALL(nvnCommandBufferBindProgram,&cmd,&programs[program],NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    }
    if(test==4||test==5) CALL(nvnCommandBufferDrawElementsInstanced,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,test==4?NVN_INDEX_TYPE_UNSIGNED_SHORT:NVN_INDEX_TYPE_UNSIGNED_INT,3,(uintptr_t)(storage+(test==4?0x50002:0x51004)),test==4?-3:-10,0,1);
    else CALL(nvnCommandBufferDrawArrays,&cmd,test==6?NVN_DRAW_PRIMITIVE_TRIANGLE_STRIP:NVN_DRAW_PRIMITIVE_TRIANGLES,test==3?3:0,test==35?0:3);
    if(test==27||test==31) {
      range.address=(uintptr_t)other;CALL(nvnCommandBufferBindVertexBuffers,&cmd,0,1,&range);
      CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    }
    submit(end());CHECK(mtl_last_fence()==fence+1);
    unsigned expected=test==32?1:test<=7||test==22||test==24||test==25||test==26||test==29?1:test==23?2:0;
    vk_buffer_sync_snapshot(&sync_after);
    if(!sync_after.cover_reference&&test==22) expected=0;
    if(!sync_after.cover_reference&&(test==21||test==22))
      CHECK(sync_after.internal_submits==sync_before.internal_submits);
    CHECK(vk_render_load_omissions()-before==(enabled?expected:0));
    unsigned clear_expected=enabled && vk_discard_covered_clears_enabled() &&
        (test==29||test==30||test==36);
    CHECK(vk_render_covered_clear_omissions()-clear_before==clear_expected);
    unsigned cursor=0;
    for(unsigned t=0;t<2;t++) for(unsigned level=0;level<2;level++) for(unsigned layer=0;layer<2;layer++) {
      NVNtextureView v;CALL(nvnTextureViewSetDefaults,&v);CALL(nvnTextureViewSetLevels,&v,level,1);CALL(nvnTextureViewSetLayers,&v,layer,1);
      unsigned dim=16>>level;NVNcopyRegion r={0,0,0,dim,dim,1};
      CALL(nvnTextureReadTexels,&textures[t],&v,&r,readback+cursor);
      for(unsigned i=0;i<dim*dim;i++) {
        uint8_t *p=readback+cursor+i*4;
        int selected=level==(test==25?1:0)&&layer==(test==26?1:0)&&(t==0||mrt);
        if((test==32&&t==1&&level==0&&layer==0)||
           ((test==33||test==34||test==35)&&selected)) CHECK(!memcmp(p,(uint8_t[]){0,0,255,255},4));
        else if(!selected||test==8||test==14||test==15||test==17||test==19) CHECK(!memcmp(p,(uint8_t[]){0,255,0,255},4));
        else if(test<=7||test==18||test==22||(test>=23&&test!=28)) {
          const uint8_t *wanted=t==0?(uint8_t[]){255,0,0,255}:test==24?(uint8_t[]){64,255,0,255}:(uint8_t[]){64,128,191,255};
          for(unsigned c=0;c<4;c++) CHECK(abs(p[c]-wanted[c])<=1);
        }
      }
      cursor+=dim*dim*4;
    }
    CHECK(cursor==sizeof readback);
    if(test==33||test==34) {
      // Reuse the existing DS fixture's real per-aspect Vulkan readback;
      // NVN combined ReadTexels is not a fabricated packed D24S8 contract.
      mtl_sub_begin();VkImage ds_image=vk_texture_native((tex_t *)&textures[2]);
      vk_buffer *ds_pixels=vk_temp_buffer(1280);
      VkBufferImageCopy copies[]={
        {.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1},.imageExtent={16,16,1}},
        {.bufferOffset=1024,.imageSubresource={VK_IMAGE_ASPECT_STENCIL_BIT,0,0,1},.imageExtent={16,16,1}}};
      vk_barrier();vkCmdCopyImageToBuffer(V.cmd,ds_image,VK_IMAGE_LAYOUT_GENERAL,ds_pixels->handle,2,copies);
      vk_internal_wait(vk_internal_commit());
      for(unsigned i=0;i<16*16;i++) {
        uint32_t raw;memcpy(&raw,ds_pixels->map+4*i,4);
        CHECK(ds_pixels->map[1024+i]==0x77 && fabs((raw&0xffffff)/16777215.-.5)<1e-6);
      }
    }
    if(!enabled) memcpy(baseline+test*sizeof readback,readback,sizeof readback);
    else CHECK(!memcmp(baseline+test*sizeof readback,readback,sizeof readback));
  }
  LOG("[vulkan cover fixture] CLEAR+LOAD MRT, first-partial, intermediate T2T reader, DS rejection, zero draw and retained explicit clear passed; discard_clears=%d",vk_discard_covered_clears_enabled());
  vk_tbdr_reference(!saved);
  vk_load_probe_request(saved_probe);
  for(unsigned i=0;i<6;i++) CALL(nvnProgramFinalize,&programs[i]);
  for(unsigned i=0;i<3;i++) CALL(nvnTextureFinalize,&textures[i]);
  CALL(nvnMemoryPoolFinalize,&pool);free(baseline);free(storage);
}
