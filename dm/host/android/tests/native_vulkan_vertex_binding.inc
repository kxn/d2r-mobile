// Exact API-state reuse must not cache guest contents or resource visibility.
// All shaders below are existing owned fixtures (markers14/11 and12/4).
static void vertex_binding_fixture_setup(NVNcommandBuffer *cmd,NVNprogram *program,NVNtexture *target,
    NVNvertexAttribState *attr,NVNvertexStreamState *stream,NVNdepthStencilState *ds,
    NVNpolygonState *polygon,NVNcolorState *color,NVNchannelMaskState *channels) {
  const NVNtexture *colors[]={target};const float viewport[]={0,0,20,20},black[]={0,0,0,1};
  CALL(nvnCommandBufferSetRenderTargets,cmd,1,colors,NULL,NULL,NULL);
  CALL(nvnCommandBufferSetViewports,cmd,0,1,viewport);CALL(nvnCommandBufferSetScissor,cmd,0,0,20,20);
  CALL(nvnCommandBufferBindProgram,cmd,program,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
  CALL(nvnCommandBufferBindVertexAttribState,cmd,1,attr);CALL(nvnCommandBufferBindVertexStreamState,cmd,1,stream);
  CALL(nvnCommandBufferBindDepthStencilState,cmd,ds);CALL(nvnCommandBufferBindPolygonState,cmd,polygon);
  CALL(nvnCommandBufferBindColorState,cmd,color);CALL(nvnCommandBufferBindChannelMaskState,cmd,channels);
  CALL(nvnCommandBufferClearColor,cmd,0,black,15);
}
static void vertex_binding_ghost_test(const void *vertex,const void *fragment) {
  void mtl_ui_ghost(int,float,float,const float[4],float,float);
  int mtl_ui_ghost_rect(float[4]);vk_state saved=S;
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,1<<20));memset(storage,0,1<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;CALL(nvnMemoryPoolBuilderSetDefaults,&mb);
  CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtexture textures[2];for(unsigned i=0;i<2;i++) {
    NVNtextureBuilder tb;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
    CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&tb,i?4:16);
    CALL(nvnTextureBuilderSetHeight,&tb,i?4:16);CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);
    CALL(nvnTextureBuilderSetStorage,&tb,&pool,i*65536);CHECK(CALL(nvnTextureInitialize,&textures[i],&tb));
  }
  const float quad[]={-.75f,-.75f,0,0,0,-.25f,-.75f,0,1,0,-.75f,-.25f,0,0,1,-.25f,-.25f,0,1,1};
  memcpy(storage+0x30000,quad,sizeof quad);memcpy(storage+0x40000,quad,sizeof quad);
  float *other=(float *)(storage+0x40000);for(unsigned n=0;n<4;n++)other[n*5]+=1;
  float *matrix=(float *)(storage+0x50000);matrix[0]=matrix[5]=matrix[10]=matrix[15]=1;
  NVNprogram program;NVNshaderData shaders[]={{.control=vertex},{.control=fragment}};
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  NVNvertexAttribState attr[2];for(unsigned n=0;n<2;n++) {
    CALL(nvnVertexAttribStateSetDefaults,&attr[n]);CALL(nvnVertexAttribStateSetStreamIndex,&attr[n],0);
    CALL(nvnVertexAttribStateSetFormat,&attr[n],n?NVN_FORMAT_RG32F:NVN_FORMAT_RGB32F,n?12:0);
  }
  NVNvertexStreamState stream;CALL(nvnVertexStreamStateSetDefaults,&stream);CALL(nvnVertexStreamStateSetStride,&stream,20);
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);
  CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);
  enum { texture_id=77 };mtl_set_tex_pool_n(256);mtl_reg_tex(texture_id,(const tex_t *)&textures[1],NULL);
  mtl_sub_begin();S=(vk_state){.program=(const prog_t *)&program,.depth_range={0,1},.chmask={~0u}};
  S.targets=(p_set_rt){.n=1,.color={(const tex_t *)&textures[0]}};
  S.poly=*(const poly_st *)&polygon;S.ds=*(const ds_st *)&ds;S.color=*(const color_st *)&color;
  S.attr[0]=*(const va_st *)&attr[0];S.attr[1]=*(const va_st *)&attr[1];S.stream[0]=*(const vs_st *)&stream;
  S.vertex[0]=(NVNbufferRange){(uintptr_t)storage+0x30000,80};
  S.ubo[0][0]=(p_bind_buf){.stage=0,.index=0,.addr=(uintptr_t)matrix,.size=64};S.tex[1][0]=texture_id;
  S.has_viewport=S.has_scissor=1;S.viewport[0][2]=S.viewport[0][3]=16;S.scissor=(p_scissor){0,0,16,16};
  vk_pipeline_state_changed();
  p_clear_tex clear={.tex=(const tex_t *)&textures[0],.r={0,0,0,16,16,1},.mask=15,.c.f={0,0,0,1}};
  vk_texture_clear(&clear);clear.tex=(const tex_t *)&textures[1];clear.r.width=clear.r.height=4;
  memcpy(clear.c.f,(float[]){0,1,.5f,1},16);vk_texture_clear(&clear);
  const float grid[]={.125f,.125f,.25f,.25f};mtl_ui_ghost(1,.25f,.25f,grid,.75f,.75f);
  p_draw draw={.prim=NVN_DRAW_PRIMITIVE_TRIANGLE_STRIP,.count=4};vk_draw(&draw,NULL);
  CHECK(vk_render_active());uint64_t pass=V.passes,draws=V.draws;
  // Outer state uses another physical offset; the nested original ghost must
  // bind its saved A, restore S, then the outer draw must bind B in this same
  // secondary. The three disjoint icons make stale cache state visible.
  S.vertex[0].address=(uintptr_t)storage+0x40000;vk_pipeline_state_changed();
  vk_ghost_flush();CHECK(V.draws==draws+1 && V.passes==pass && vk_render_active());
  float rect[4];CHECK(mtl_ui_ghost_rect(rect));mtl_ui_ghost(0,0,0,NULL,0,0);
  vk_draw(&draw,NULL);CHECK(V.draws==draws+2 && V.passes==pass);
  vk_internal_wait(vk_internal_commit());
  uint8_t pixels[1024];NVNcopyRegion region={0,0,0,16,16,1};CALL(nvnTextureReadTexels,&textures[0],NULL,&region,pixels);
  for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++) {
    int visible=(x>=2&&x<6&&y>=2&&y<6)||(x>=10&&x<14&&y>=2&&y<6)||(x>=10&&x<14&&y>=10&&y<14);
    CHECK(!memcmp(pixels+(y*16+x)*4,visible?(uint8_t[]){0,255,128,255}:(uint8_t[]){0,0,0,255},4));
  }
  S=saved;vk_pipeline_state_changed();mtl_set_tex_pool_n(4);
  CALL(nvnProgramFinalize,&program);for(unsigned n=0;n<2;n++)CALL(nvnTextureFinalize,&textures[n]);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);
  LOG("[vulkan vertex binding ghost fixture] same-secondary original nested draw and restored outer physical offset passed");
}
static void vertex_index_binding_test(const void *ui_vertex,const void *ui_fragment) {
  vk_state saved=S;uint8_t *storage[2];NVNmemoryPool pools[2];
  const float full[]={-1,-1,1,-1,1,1,-1,1},outside[]={3,3,5,3,5,5,3,5};
  for(unsigned n=0;n<2;n++) {
    CHECK(!posix_memalign((void **)&storage[n],65536,1<<20));memset(storage[n],0,1<<20);
    memcpy(storage[n]+0x10000,n?outside:full,sizeof full);memcpy(storage[n]+0x10080,outside,sizeof outside);
    memcpy(storage[n]+0x30000,full,sizeof full);
    if(!n) {
      memcpy(storage[n]+0x20000,(uint32_t[]){0,1,2,0,2,3},24);
      memcpy(storage[n]+0x20080,(uint32_t[]){0,1,2,0,2,3},24);
    }
    NVNmemoryPoolBuilder mb;CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
    CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage[n],1<<20);
    CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
    CHECK(CALL(nvnMemoryPoolInitialize,&pools[n],&mb));
  }
  NVNtexture target;NVNtextureBuilder tb;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&tb,20);CALL(nvnTextureBuilderSetHeight,&tb,20);
  CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);CALL(nvnTextureBuilderSetStorage,&tb,&pools[0],0);
  CHECK(CALL(nvnTextureInitialize,&target,&tb));
  uint8_t controls[2][0x880];memset(controls[0],14,sizeof controls[0]);memset(controls[1],11,sizeof controls[1]);
  NVNprogram program;NVNshaderData shaders[]={{.control=controls[0]},{.control=controls[1]}};
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  NVNvertexAttribState attr;CALL(nvnVertexAttribStateSetDefaults,&attr);CALL(nvnVertexAttribStateSetFormat,&attr,NVN_FORMAT_RG32F,0);
  CALL(nvnVertexAttribStateSetStreamIndex,&attr,0);
  NVNvertexStreamState streams[2];for(unsigned n=0;n<2;n++) {
    CALL(nvnVertexStreamStateSetDefaults,&streams[n]);CALL(nvnVertexStreamStateSetStride,&streams[n],8);
  }
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);
  CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);NVNchannelMaskState channels;CALL(nvnChannelMaskStateSetDefaults,&channels);
  NVNcommandBuffer local;CHECK(CALL(nvnCommandBufferInitialize,&local,&device));
  CALL(nvnCommandBufferAddCommandMemory,&local,&pools[0],0x50000,0x20000);
  CALL(nvnCommandBufferBeginRecording,&local);
  vertex_binding_fixture_setup(&local,&program,&target,&attr,streams,&ds,&polygon,&color,&channels);
  CALL(nvnCommandBufferBindVertexStreamState,&local,2,streams);
  const unsigned visible[]={1,1,0,1,0,1,1,0,1,0,0,1,1,0,1,0,1};
  for(unsigned test=0;test<17;test++) {
    unsigned vertex_pool=test==4?1:0;uint64_t vertex_offset=test==2?0x10080:0x10000;
    NVNbufferRange range={(uintptr_t)storage[vertex_pool]+vertex_offset,sizeof full};
    NVNbufferRange other={(uintptr_t)storage[1]+0x10000,sizeof outside};
    CALL(nvnCommandBufferBindVertexBuffers,&local,0,1,&range);CALL(nvnCommandBufferBindVertexBuffers,&local,1,1,&other);
    CALL(nvnVertexAttribStateSetStreamIndex,&attr,test==15?1:0);CALL(nvnCommandBufferBindVertexAttribState,&local,1,&attr);
    CALL(nvnCommandBufferSetScissor,&local,(test%5)*4,(test/5)*4,4,4);
    uint64_t index=(uintptr_t)storage[test==9?1:0]+0x20000;
    if(test==7||test==13)index+=64;if(test==10||test==11)index+=128;
    if(test==12||test==14)CALL(nvnCommandBufferDrawArrays,&local,NVN_DRAW_PRIMITIVE_QUADS,0,4);
    else CALL(nvnCommandBufferDrawElementsInstanced,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,
      test==10?NVN_INDEX_TYPE_UNSIGNED_SHORT:NVN_INDEX_TYPE_UNSIGNED_INT,6,index,0,0,1);
  }
  submit(CALL(nvnCommandBufferEndRecording,&local));
  uint8_t pixels[20*20*4];NVNcopyRegion region={0,0,0,20,20,1};CALL(nvnTextureReadTexels,&target,NULL,&region,pixels);
  for(unsigned y=0;y<20;y++)for(unsigned x=0;x<20;x++) {
    unsigned test=(y/4)*5+x/4;int drawn=test<17&&visible[test];
    CHECK(!memcmp(pixels+(y*20+x)*4,drawn?(uint8_t[]){0,128,255,255}:(uint8_t[]){0,0,0,255},4));
  }
  // A CPU edit at the same resolved binding must still upload current bytes.
  dm_guest_write_prepare(storage[0]+0x10000,sizeof outside);memcpy(storage[0]+0x10000,outside,sizeof outside);
  CALL(nvnCommandBufferBeginRecording,&local);CALL(nvnVertexAttribStateSetStreamIndex,&attr,0);
  vertex_binding_fixture_setup(&local,&program,&target,&attr,streams,&ds,&polygon,&color,&channels);
  NVNbufferRange range={(uintptr_t)storage[0]+0x10000,sizeof full};CALL(nvnCommandBufferBindVertexBuffers,&local,0,1,&range);
  CALL(nvnCommandBufferDrawElementsInstanced,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,NVN_INDEX_TYPE_UNSIGNED_INT,6,(uintptr_t)storage[0]+0x20000,0,0,1);
  submit(CALL(nvnCommandBufferEndRecording,&local));CALL(nvnTextureReadTexels,&target,NULL,&region,pixels);
  for(unsigned n=0;n<400;n++)CHECK(!memcmp(pixels+n*4,(uint8_t[]){0,0,0,255},4));
  // A real transfer updates the same physical vertex binding after a draw.
  // It must close the old secondary, preserve hazards and rebind the new one.
  uint64_t passes,draws;mtl_stats(&passes,&draws);
  CALL(nvnCommandBufferBeginRecording,&local);
  vertex_binding_fixture_setup(&local,&program,&target,&attr,streams,&ds,&polygon,&color,&channels);
  CALL(nvnCommandBufferBindVertexBuffers,&local,0,1,&range);
  CALL(nvnCommandBufferDrawElementsInstanced,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,NVN_INDEX_TYPE_UNSIGNED_INT,6,(uintptr_t)storage[0]+0x20000,0,0,1);
  CALL(nvnCommandBufferCopyBufferToBuffer,&local,(uintptr_t)storage[0]+0x30000,(uintptr_t)storage[0]+0x10000,sizeof full,0);
  CALL(nvnCommandBufferDrawElementsInstanced,&local,NVN_DRAW_PRIMITIVE_TRIANGLES,NVN_INDEX_TYPE_UNSIGNED_INT,6,(uintptr_t)storage[0]+0x20000,0,0,1);
  submit(CALL(nvnCommandBufferEndRecording,&local));
  uint64_t after_passes,after_draws;mtl_stats(&after_passes,&after_draws);CHECK(after_draws==draws+2);CHECK(after_passes>=passes+2);
  CALL(nvnTextureReadTexels,&target,NULL,&region,pixels);
  for(unsigned n=0;n<400;n++)CHECK(!memcmp(pixels+n*4,(uint8_t[]){0,128,255,255},4));
  // Explicit physical submission/reset boundaries, including recycled actual
  // secondary handles. Global16-context cap makes18 first-secondary records
  // sufficient to encounter reuse; each must bind from undefined state.
  vk_state state=S;VkCommandBuffer seen[18];unsigned seen_count=0,reused=0;
  p_draw_idx indexed={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.itype=NVN_INDEX_TYPE_UNSIGNED_INT,.count=6,.instances=1,.ibuf=(uintptr_t)storage[0]+0x20000};
  for(unsigned round=0;round<18;round++) {
    dm_guest_write_prepare(storage[0]+0x10000,sizeof full);memcpy(storage[0]+0x10000,round&1?outside:full,sizeof full);
    mtl_sub_begin();S=state;vk_pipeline_state_changed();
    p_clear_tex clear={.tex=(const tex_t *)&target,.r={0,0,0,20,20,1},.mask=15,.c.f={0,0,0,1}};vk_texture_clear(&clear);
    vk_draw(NULL,&indexed);CHECK(vk_render_active());VkCommandBuffer secondary=V.cmd;
    for(unsigned n=0;n<seen_count;n++)if(seen[n]==secondary)reused++;
    seen[seen_count++]=secondary;vk_internal_wait(vk_internal_commit());mtl_finish();
    CALL(nvnTextureReadTexels,&target,NULL,&region,pixels);
    for(unsigned n=0;n<400;n++)CHECK(!memcmp(pixels+n*4,round&1?(uint8_t[]){0,0,0,255}:(uint8_t[]){0,128,255,255},4));
  }
  CHECK(reused>0);
  S=saved;vk_pipeline_state_changed();CALL(nvnCommandBufferFinalize,&local);CALL(nvnProgramFinalize,&program);
  CALL(nvnTextureFinalize,&target);for(unsigned n=0;n<2;n++){CALL(nvnMemoryPoolFinalize,&pools[n]);free(storage[n]);}
  vertex_binding_ghost_test(ui_vertex,ui_fragment);
  LOG("[vulkan vertex/index binding fixture] exact handle/offset/type, unused slots, quads, CPU/GPU contents and submission/secondary reuse passed");
}
