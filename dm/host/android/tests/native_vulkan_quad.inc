// 对照原 Metal 非索引 QUADS 的共享索引展开；检查 GPU 像素和原 first 语义。
// Original Metal's shared-index expansion of non-indexed QUADS, verified by
// GPU pixels and original first-vertex semantics. No game shaders or modules.
static void quad_draw_test(const void *fragment_control) {
  uint64_t reuse0,lookup0,check0;vk_graphics_prepare_stats(&reuse0,&lookup0,&check0);
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,1<<20));memset(storage,0,1<<20);
  NVNmemoryPoolBuilder mb={0};NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtextureBuilder tb={0};NVNtexture target;
  CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&tb,16);
  CALL(nvnTextureBuilderSetHeight,&tb,16);CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);
  CALL(nvnTextureBuilderSetStorage,&tb,&pool,0);CHECK(CALL(nvnTextureInitialize,&target,&tb));
  uint8_t vertex_control[0x880];memset(vertex_control,14,sizeof vertex_control);
  NVNprogram program;NVNshaderData shaders[]={{.control=vertex_control},{.control=fragment_control}};
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  float *vertices=(void *)(storage+0x10000);
  memcpy(vertices,(float[]){-.75,.25, -.25,.25, -.25,.75, -.75,.75,
                            .125,-.625, .75,-.625, .75,0, .125,0,
                            -1,-1, 3,-1, -1,3},22*sizeof(float));
  uint32_t *indices=(void *)(storage+0x20000);memcpy(indices,(uint32_t[]){4,5,6,4,6,7},24);
  NVNvertexAttribState attr;CALL(nvnVertexAttribStateSetDefaults,&attr);
  CALL(nvnVertexAttribStateSetFormat,&attr,NVN_FORMAT_RG32F,0);CALL(nvnVertexAttribStateSetStreamIndex,&attr,0);
  NVNvertexStreamState stream;CALL(nvnVertexStreamStateSetDefaults,&stream);CALL(nvnVertexStreamStateSetStride,&stream,8);
  NVNbufferRange range={(uintptr_t)vertices,22*sizeof(float)};
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);
  CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);NVNchannelMaskState channels;CALL(nvnChannelMaskStateSetDefaults,&channels);
  const int first[]={0,0,0,0,0,4,4,0,0,0,8},count[]={0,3,4,7,8,4,7,8,8,4,3};
  const NVNtexture *colors[]={&target};NVNcopyRegion region={0,0,0,16,16,1};uint8_t pixels[16*16*4];
  const float viewport[]={0,0,16,16},black[]={0,0,0,1};
  for (unsigned test=0;test<sizeof first/sizeof *first;test++) {
    CALL(nvnPolygonStateSetCullFace,&polygon,test==7||test==8?NVN_FACE_BACK:NVN_FACE_NONE);
    // 测试顶点在裁剪空间逆时针：CCW 保留、CW 背面剔除。
    // Fixture vertices are CCW in clip space: CCW survives, CW is back-culled.
    CALL(nvnPolygonStateSetFrontFace,&polygon,test==8?NVN_FRONT_FACE_CW:NVN_FRONT_FACE_CCW);
    CALL(nvnCommandBufferBeginRecording,&cmd);CALL(nvnCommandBufferSetRenderTargets,&cmd,1,colors,NULL,NULL,NULL);
    CALL(nvnCommandBufferSetViewports,&cmd,0,1,viewport);CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);
    CALL(nvnCommandBufferBindProgram,&cmd,&program,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferBindVertexAttribState,&cmd,1,&attr);CALL(nvnCommandBufferBindVertexStreamState,&cmd,1,&stream);
    CALL(nvnCommandBufferBindVertexBuffers,&cmd,0,1,&range);CALL(nvnCommandBufferBindPolygonState,&cmd,&polygon);
    CALL(nvnCommandBufferBindDepthStencilState,&cmd,&ds);CALL(nvnCommandBufferBindColorState,&cmd,&color);
    CALL(nvnCommandBufferBindChannelMaskState,&cmd,&channels);CALL(nvnCommandBufferClearColor,&cmd,0,black,15);
    CALL(nvnCommandBufferDrawArrays,&cmd,test==10?NVN_DRAW_PRIMITIVE_TRIANGLES:NVN_DRAW_PRIMITIVE_QUADS,first[test],count[test]);
    if (test==9) {
      CALL(nvnCommandBufferDrawElementsInstanced,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,NVN_INDEX_TYPE_UNSIGNED_INT,6,(uintptr_t)indices,0,0,1);
      CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_QUADS,4,4);
    }
    NVNcommandHandle handle=end();uint32_t pipeline_total=0;
    // 同一命令依次经两种桶索引回放；比较独立像素并禁止重复编译管线。
    // Replay identical commands through both indexes: independent pixel
    // checks and stable pipeline count cover switching/reuse, not just hashes.
    for(unsigned pass=0;pass<4;pass++) {
    vk_graphics_byte_hash((test+pass)%2);
    vk_graphics_prepare_mode(pass%3);
    submit(handle);CALL(nvnTextureReadTexels,&target,NULL,&region,pixels);
    if(!pass) pipeline_total=vk_graphics_pipeline_count();
    else CHECK(vk_graphics_pipeline_count()==pipeline_total);
    for (int y=0;y<16;y++) for (int x=0;x<16;x++) {
      int left=x>=2&&x<6&&y>=2&&y<6,right=x>=9&&x<14&&y>=8&&y<13;
      int visible=test==10||((test==2||test==3||test==4||test==7||test==9)&&left)||
          ((test==4||test==5||test==6||test==7||test==9)&&right);
      uint8_t red=test==10?128:(first[test]==4||(test==9&&right))?64:0;
      uint8_t expected[]={visible?red:0,visible?128:0,visible?255:0,255};
      if (memcmp(pixels+(y*16+x)*4,expected,4)) fprintf(stderr,"quad test=%u pixel=%d,%d got=%u,%u,%u expected=%u,%u,%u\n",test,x,y,pixels[(y*16+x)*4],pixels[(y*16+x)*4+1],pixels[(y*16+x)*4+2],expected[0],expected[1],expected[2]);
      CHECK(!memcmp(pixels+(y*16+x)*4,expected,4));
    }
    }
  }
  vk_graphics_byte_hash(0);
  vk_graphics_prepare_mode(0);
  uint64_t reuse1,lookup1,check1;vk_graphics_prepare_stats(&reuse1,&lookup1,&check1);
  CHECK(reuse1>reuse0);CHECK(lookup1>lookup0);CHECK(check1>check0);
  CALL(nvnProgramFinalize,&program);CALL(nvnTextureFinalize,&target);CALL(nvnMemoryPoolFinalize,&pool);free(storage);
}
