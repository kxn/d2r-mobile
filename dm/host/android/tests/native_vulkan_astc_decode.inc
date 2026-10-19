// Original NVN upload -> Vulkan sampled draw -> original NVN byte readback.
// This fixture never loads game modules or resources. ASTC-unsupported hosts
// execute only the separate private-metadata oracle and report GPU skipped.
int vk_texture_test_astc_info(const tex_t *,VkImageView,unsigned[5]);
static const uint8_t astc_decode_constant[16]={
  0xfc,0xfd,0xff,0xff,0xff,0xff,0xff,0xff,31,31,63,63,127,127,255,255};
static void astc_decode_seed(NVNtexture *texture,int hdr_last) {
  const tex_t *t=(const tex_t *)texture;
  dm_guest_write_prepare(t->mem,t->L.size);
  for(unsigned layer=0;layer<(unsigned)t->L.layers;layer++)
    for(unsigned level=0;level<(unsigned)t->d.levels;level++) {
      unsigned w=t->d.width>>level,h=t->d.height>>level;
      if(!w)w=1;
      if(!h)h=1;
      int bw,bh;unsigned bpb=nvn_fmt_bpb(t->d.format,&bw,&bh);
      unsigned pitch=((w+bw-1)/bw)*bpb,rows=(h+bh-1)/bh;
      uint8_t blocks[256];CHECK(pitch*rows<=sizeof blocks);
      for(unsigned b=0;b<pitch*rows;b+=16)memcpy(blocks+b,astc_decode_constant,16);
      if(hdr_last && level+1==(unsigned)t->d.levels)blocks[1]|=2;
      CHECK(!nvn_swizzle(&t->d,level,layer,blocks,pitch,t->mem));
    }
}
static VkImageView astc_decode_probe(NVNtexture *source,unsigned expected_proof) {
  mtl_sub_begin();
  VkImageView view=vk_texture_view(71,0);
  unsigned state[5];CHECK(vk_texture_test_astc_info((const tex_t *)source,view,state));
  CHECK(state[0]==expected_proof);CHECK(state[4]==expected_proof);
  vk_internal_wait(vk_internal_commit());
  return view;
}
static void astc_decode_draw(NVNtexture *output) {
  mtl_sub_begin();p_draw draw={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};
  vk_draw(&draw,NULL);vk_internal_wait(vk_internal_commit());
  uint8_t pixels[16*16*4];NVNcopyRegion region={0,0,0,16,16,1};
  CALL(nvnTextureReadTexels,output,NULL,&region,pixels);
  const uint8_t expected[4]={31,63,127,255};
  for(unsigned p=0;p<256;p++)for(unsigned ch=0;ch<4;ch++)
    CHECK(abs((int)pixels[p*4+ch]-(int)expected[ch])<=1);
}
static void astc_decode_gpu_case(int format,unsigned layers,const void *vertex,const void *fragment) {
  enum {size=4<<20,output_off=1<<20,vertex_off=3<<20};
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,size));memset(storage,0,size);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,size);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  int srgb=format==NVN_FORMAT_RGBA_ASTC_4x4_SRGB;
  NVNtexture source,output;
  for(unsigned n=0;n<2;n++) {
    NVNtextureBuilder b;CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetWidth,&b,n?16:12);CALL(nvnTextureBuilderSetHeight,&b,n?16:12);
    CALL(nvnTextureBuilderSetTarget,&b,!n && layers>1?NVN_TEXTURE_TARGET_2D_ARRAY:NVN_TEXTURE_TARGET_2D);
    CALL(nvnTextureBuilderSetDepth,&b,n?1:layers);CALL(nvnTextureBuilderSetLevels,&b,n?1:3);
    CALL(nvnTextureBuilderSetFormat,&b,n?(srgb?NVN_FORMAT_RGBA8_SRGB:NVN_FORMAT_RGBA8):format);
    if(!n) {NVNpackagedTextureLayout layout={{0}};CALL(nvnTextureBuilderSetPackagedTextureLayout,&b,&layout);}
    CALL(nvnTextureBuilderSetStorage,&b,&pool,n?output_off:0);CHECK(CALL(nvnTextureBuilderGetStorageSize,&b)<=output_off);
    CHECK(CALL(nvnTextureInitialize,n?&output:&source,&b));
  }
  astc_decode_seed(&source,0);
  NVNtextureView view;CALL(nvnTextureViewSetDefaults,&view);CALL(nvnTextureViewSetTarget,&view,NVN_TEXTURE_TARGET_2D);
  CALL(nvnTextureViewSetLayers,&view,0,1);
  mtl_set_tex_pool_n(256);mtl_reg_tex(71,(const tex_t *)&source,(const view_t *)&view);
  NVNprogram program;NVNshaderData shaders[]={{.control=vertex},{.control=fragment}};
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  NVNsamplerBuilder sb;NVNsampler sampler;CALL(nvnSamplerBuilderSetDefaults,&sb);CALL(nvnSamplerBuilderSetDevice,&sb,&device);
  CALL(nvnSamplerBuilderSetMinMagFilter,&sb,NVN_MIN_FILTER_NEAREST,NVN_MAG_FILTER_NEAREST);
  CALL(nvnSamplerBuilderSetWrapMode,&sb,NVN_WRAP_MODE_CLAMP_TO_EDGE,NVN_WRAP_MODE_CLAMP_TO_EDGE,NVN_WRAP_MODE_CLAMP_TO_EDGE);
  CHECK(CALL(nvnSamplerInitialize,&sampler,&sb));mtl_reg_samp(72,&((const samp_t *)&sampler)->p);
  const float vertices[]={-1,-1,0,0,3,-1,2,0,-1,3,0,2};memcpy(storage+vertex_off,vertices,sizeof vertices);
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);
  CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);
  S=(vk_state){.program=(const prog_t *)&program,.depth_range={0,1},.chmask={~0u}};
  S.poly=*(const poly_st *)&polygon;S.ds=*(const ds_st *)&ds;S.color=*(const color_st *)&color;
  S.targets=(p_set_rt){.n=1,.color={(const tex_t *)&output}};S.tex[1][0]=71;S.samp[1][0]=(uint64_t)72<<20;
  S.has_viewport=S.has_scissor=1;S.viewport[0][2]=S.viewport[0][3]=16;S.scissor=(p_scissor){0,0,16,16};
  for(unsigned n=0;n<2;n++) {
    NVNvertexAttribState attr;CALL(nvnVertexAttribStateSetDefaults,&attr);CALL(nvnVertexAttribStateSetStreamIndex,&attr,0);
    CALL(nvnVertexAttribStateSetFormat,&attr,NVN_FORMAT_RG32F,n*8);S.attr[n]=*(const va_st *)&attr;
  }
  NVNvertexStreamState stream;CALL(nvnVertexStreamStateSetDefaults,&stream);CALL(nvnVertexStreamStateSetStride,&stream,16);
  S.stream[0]=*(const vs_st *)&stream;S.vertex[0]=(NVNbufferRange){(uintptr_t)storage+vertex_off,sizeof vertices};vk_pipeline_state_changed();
  unsigned low=V.astc_decode_mode && !srgb && layers==1;
  VkImageView first=astc_decode_probe(&source,low);astc_decode_draw(&output);
  CHECK(astc_decode_probe(&source,low)==first); // ordinary unchanged cache hit
  astc_decode_seed(&source,1);
  VkImageView fallback=astc_decode_probe(&source,0); // no sampling HDR content
  if(low)CHECK(fallback!=first);
  astc_decode_seed(&source,0);CHECK(astc_decode_probe(&source,low)==first);astc_decode_draw(&output);
  // Actual GPU copy destination revokes the whole-image certificate, even
  // though this owned copy happens to contain the same valid LDR constant.
  dm_guest_write_prepare(storage+(2<<20),256);
  for(unsigned b=0;b<256;b+=16)memcpy(storage+(2<<20)+b,astc_decode_constant,16);
  mtl_sub_begin();p_copy_b2t copy={.tex=(const tex_t *)&source,.src=(uintptr_t)storage+(2<<20),.r={0,0,0,12,12,1}};
  vk_texture_copy_b2t(&copy,0,0);vk_internal_wait(vk_internal_commit());
  CHECK(astc_decode_probe(&source,0)==fallback);astc_decode_draw(&output);
  astc_decode_seed(&source,0);CHECK(astc_decode_probe(&source,low)==first);
  mtl_sub_begin();CHECK(vk_texture_native((const tex_t *)&source)!=VK_NULL_HANDLE);vk_internal_wait(vk_internal_commit());
  CHECK(astc_decode_probe(&source,0)==fallback);
  astc_decode_seed(&source,0);CHECK(astc_decode_probe(&source,0)==fallback);astc_decode_draw(&output);
  CALL(nvnSamplerFinalize,&sampler);CALL(nvnProgramFinalize,&program);CALL(nvnTextureFinalize,&output);CALL(nvnTextureFinalize,&source);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);
  LOG("[ASTC decode GPU fixture] format=%d layers=%u decode8=%u upload/cache/HDR-last-mip/GPU-copy/raw-escape sampling passed",format,layers,low);
}
static int astc_decode_gpu_test(void) {
  if(!V.features.textureCompressionASTC_LDR) return 0;
  vk_state saved=S;uint8_t vertex[0x880],fragment[0x880];memset(vertex,3,sizeof vertex);memset(fragment,4,sizeof fragment);
  astc_decode_gpu_case(NVN_FORMAT_RGBA_ASTC_4x4,1,vertex,fragment);
  astc_decode_gpu_case(NVN_FORMAT_RGBA_ASTC_6x6,1,vertex,fragment);
  astc_decode_gpu_case(NVN_FORMAT_RGBA_ASTC_4x4_SRGB,1,vertex,fragment);
  astc_decode_gpu_case(NVN_FORMAT_RGBA_ASTC_4x4,2,vertex,fragment);
  S=saved;vk_pipeline_state_changed();return 1;
}
