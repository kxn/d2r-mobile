// Owned packaged BC upload: exact endpoint pixels through the real CPU
// deswizzle/decode + GPU upload/sampling path, with native BC as a control.
#ifdef DM_VULKAN_TEST_EMBED
void vk_texture_test_force_bc_decode(int force);
int vk_texture_test_bc_decoded(const tex_t *texture);
static void bc_scratch_color(int format,unsigned level,unsigned layer,unsigned phase,uint8_t out[4]) {
  unsigned c=(level+layer*2+(phase && level==1 && layer==1))%4;
  static const uint8_t colors[4][4]={{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,255,255}};
  memcpy(out,colors[c],4);
  if(format==NVN_FORMAT_RGTC1_UNORM){out[0]=c*85;out[1]=out[2]=0;}
  if(format==NVN_FORMAT_RGTC2_UNORM){out[0]=c*85;out[1]=255-c*85;out[2]=0;}
}
static void bc_scratch_seed(NVNtexture *texture,int format,unsigned phase) {
  const tex_t *t=(const tex_t *)texture;dm_guest_write_prepare(t->mem,t->L.size);
  for(unsigned level=0;level<3;level++)for(unsigned layer=0;layer<2;layer++) {
    unsigned width=16>>level;uint8_t pixels[256]={0},color[4];bc_scratch_color(format,level,layer,phase,color);
    int bw,bh;unsigned bpb=nvn_fmt_bpb(format,&bw,&bh),pitch=((width+bw-1)/bw)*bpb;
    CHECK(pitch*((width+bh-1)/bh)<=sizeof pixels);
    for(unsigned b=0;b<(width+bw-1)/bw*((width+bh-1)/bh);b++) {
      uint8_t *block=pixels+b*bpb;
      if(format==NVN_FORMAT_RGTC1_UNORM||format==NVN_FORMAT_RGTC2_UNORM) {
        block[0]=color[0];if(format==NVN_FORMAT_RGTC2_UNORM)block[8]=color[1];
      } else {
        unsigned offset=format==NVN_FORMAT_RGBA_DXT5?8:0;
        if(offset)block[0]=255;
        uint16_t endpoint=(uint16_t)(((color[0]>>3)<<11)|((color[1]>>2)<<5)|(color[2]>>3));
        memcpy(block+offset,&endpoint,2);
      }
    }
    CHECK(!nvn_swizzle(&t->d,level,layer,pixels,pitch,t->mem));
  }
}
static void bc_scratch_atlas(NVNtexture *source,NVNtexture *output,int format,unsigned phase,int force) {
  enum {id=71};p_draw draw={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};
  mtl_sub_begin();
  for(unsigned level=0;level<3;level++)for(unsigned layer=0;layer<2;layer++) {
    unsigned tile=level*2+layer;NVNtextureView view;CALL(nvnTextureViewSetDefaults,&view);
    CALL(nvnTextureViewSetLevels,&view,level,1);CALL(nvnTextureViewSetLayers,&view,layer,1);
    CALL(nvnTextureViewSetTarget,&view,NVN_TEXTURE_TARGET_2D);mtl_reg_tex(id,(const tex_t *)source,(const view_t *)&view);
    S.viewport[0][0]=tile*16;S.viewport[0][1]=0;S.viewport[0][2]=S.viewport[0][3]=16;
    S.has_viewport=S.has_scissor=1;S.scissor=(p_scissor){tile*16,0,16,16};
    vk_draw(&draw,NULL);
    // Exact path proof, not a timing/sample counter. Capability masking is
    // test-only; it does not claim host native BC was unsupported.
    CHECK(vk_texture_test_bc_decoded((const tex_t *)source)==force);
  }
  vk_internal_wait(vk_internal_commit());
  uint8_t pixels[96*16*4];NVNcopyRegion region={0,0,0,96,16,1};
  CALL(nvnTextureReadTexels,output,NULL,&region,pixels);
  for(unsigned level=0;level<3;level++)for(unsigned layer=0;layer<2;layer++) {
    uint8_t color[4];bc_scratch_color(format,level,layer,phase,color);unsigned tile=level*2+layer;
    for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++)for(unsigned ch=0;ch<4;ch++) {
      uint8_t got=pixels[(y*96+tile*16+x)*4+ch];
      if(got!=color[ch])LOG("[BC CPU scratch mismatch] format=%d force=%d phase=%u mip=%u layer=%u xy=%u,%u ch=%u got=%u expected=%u",format,force,phase,level,layer,x,y,ch,got,color[ch]);
      CHECK(got==color[ch]);
    }
  }
}
static void bc_cpu_scratch_case(int format,int force,const void *vertex,const void *fragment) {
  enum {bytes=4<<20,output_offset=2<<20,vertex_offset=3<<20,id=71,sampler_id=72};
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,bytes));memset(storage,0,bytes);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,bytes);CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtexture source,output;
  for(unsigned n=0;n<2;n++) {
    NVNtextureBuilder b;CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,n?NVN_TEXTURE_TARGET_2D:NVN_TEXTURE_TARGET_2D_ARRAY);
    CALL(nvnTextureBuilderSetWidth,&b,n?96:16);CALL(nvnTextureBuilderSetHeight,&b,16);
    if(!n)CALL(nvnTextureBuilderSetDepth,&b,2);
    CALL(nvnTextureBuilderSetLevels,&b,n?1:3);CALL(nvnTextureBuilderSetFormat,&b,n?NVN_FORMAT_RGBA8:format);
    if(!n){NVNpackagedTextureLayout layout={{0}};CALL(nvnTextureBuilderSetPackagedTextureLayout,&b,&layout);}
    CALL(nvnTextureBuilderSetStorage,&b,&pool,n?output_offset:0);CHECK(CALL(nvnTextureBuilderGetStorageSize,&b)<=output_offset);
    CHECK(CALL(nvnTextureInitialize,n?&output:&source,&b));
  }
  bc_scratch_seed(&source,format,0);
  NVNprogram program;NVNshaderData shaders[]={{.control=vertex},{.control=fragment}};
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  NVNsamplerBuilder sb;NVNsampler sampler;CALL(nvnSamplerBuilderSetDefaults,&sb);CALL(nvnSamplerBuilderSetDevice,&sb,&device);
  CALL(nvnSamplerBuilderSetMinMagFilter,&sb,NVN_MIN_FILTER_NEAREST,NVN_MAG_FILTER_NEAREST);
  CALL(nvnSamplerBuilderSetWrapMode,&sb,NVN_WRAP_MODE_CLAMP_TO_EDGE,NVN_WRAP_MODE_CLAMP_TO_EDGE,NVN_WRAP_MODE_CLAMP_TO_EDGE);
  CHECK(CALL(nvnSamplerInitialize,&sampler,&sb));mtl_set_tex_pool_n(256);mtl_reg_samp(sampler_id,&((const samp_t *)&sampler)->p);
  const float vertices[]={-1,-1,0,0,3,-1,2,0,-1,3,0,2};memcpy(storage+vertex_offset,vertices,sizeof vertices);
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);
  CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  S=(vk_state){.program=(const prog_t *)&program,.depth_range={0,1},.chmask={~0u}};
  S.poly=*(const poly_st *)&polygon;S.ds=*(const ds_st *)&ds;S.color=*(const color_st *)&color;
  S.targets=(p_set_rt){.n=1,.color={(const tex_t *)&output}};S.tex[1][0]=id;S.samp[1][0]=(uint64_t)sampler_id<<20;
  for(unsigned n=0;n<2;n++) {
    NVNvertexAttribState attr;CALL(nvnVertexAttribStateSetDefaults,&attr);CALL(nvnVertexAttribStateSetStreamIndex,&attr,0);
    CALL(nvnVertexAttribStateSetFormat,&attr,NVN_FORMAT_RG32F,n*8);S.attr[n]=*(const va_st *)&attr;
  }
  NVNvertexStreamState stream;CALL(nvnVertexStreamStateSetDefaults,&stream);CALL(nvnVertexStreamStateSetStride,&stream,16);
  S.stream[0]=*(const vs_st *)&stream;S.vertex[0]=(NVNbufferRange){(uintptr_t)storage+vertex_offset,sizeof vertices};vk_pipeline_state_changed();
  vk_texture_test_force_bc_decode(force);
  bc_scratch_atlas(&source,&output,format,0,force); // first packaged upload
  bc_scratch_atlas(&source,&output,format,0,force); // unchanged stamp, same pixels
  bc_scratch_seed(&source,format,1);bc_scratch_atlas(&source,&output,format,1,force); // one subresource changed
  vk_texture_test_force_bc_decode(0);
  CALL(nvnSamplerFinalize,&sampler);CALL(nvnProgramFinalize,&program);CALL(nvnTextureFinalize,&output);CALL(nvnTextureFinalize,&source);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);
  LOG("[vulkan BC CPU scratch fixture] format=%d forced_decode=%d mips=3 layers=2 first/unchanged/dirty pixels passed",format,force);
}
static void bc_cpu_scratch_test(void) {
  mtl_init();vk_state saved=S;uint8_t vertex[0x880],fragment[0x880];memset(vertex,3,sizeof vertex);memset(fragment,4,sizeof fragment);
  const int formats[]={NVN_FORMAT_RGB_DXT1,NVN_FORMAT_RGBA_DXT5,NVN_FORMAT_RGTC1_UNORM,NVN_FORMAT_RGTC2_UNORM};
  for(unsigned k=0;k<4;k++)for(unsigned force=0;force<2;force++) {
    VkFormatProperties props;vk_format_properties(vk_nvn_format(formats[k]),&props);
    if(!force&&!(props.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))continue;
    bc_cpu_scratch_case(formats[k],force,vertex,fragment);
  }
  S=saved;vk_pipeline_state_changed();CHECK(vk_validation_error_count()==0);
}
#endif
