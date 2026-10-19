// Sample-only arrays keep one complete layout; per-cell data and access
// histories remain distinct. No new shader or game-derived input is used.
#include "../../../gfx/tex/nvn_deswizzle.h"
static NVNtextureView sampled_array_view(unsigned level,unsigned layer) {
  NVNtextureView view;CALL(nvnTextureViewSetDefaults,&view);
  CALL(nvnTextureViewSetLevels,&view,level,1);CALL(nvnTextureViewSetLayers,&view,layer,1);
  CALL(nvnTextureViewSetTarget,&view,NVN_TEXTURE_TARGET_2D);return view;
}
static void sampled_array_color(unsigned level,unsigned layer,int format,unsigned mutation,uint8_t color[4]) {
  color[0]=(layer*17+level*31+23+(layer>>8)*41)&255;
  color[1]=(layer*71+level*13+47+(layer>>8)*73)&255;
  color[2]=(layer*3+level*59+89+(layer>>8)*19)&255;color[3]=255;
  if(mutation==1 && !level && layer==1) {color[0]=255;color[1]=0;color[2]=128;}
  if(mutation==2 && level==1 && layer==1) {color[0]=0;color[1]=255;color[2]=255;}
  if(mutation==2 && !level && layer==2) {color[0]=23;color[1]=47;color[2]=89;}
  if(format==NVN_FORMAT_R8) {color[1]=color[2]=0;color[3]=255;}
}
static void sampled_array_seed(NVNtexture *texture,int format,unsigned layers) {
  uint8_t pixels[16*16*4];unsigned channels=format==NVN_FORMAT_R8?1:4;
  const tex_t *t=(const tex_t *)texture;dm_guest_write_prepare(t->mem,t->L.size);
  for(unsigned level=0;level<3;level++) for(unsigned layer=0;layer<layers;layer++) {
    uint8_t c[4];sampled_array_color(level,layer,format,0,c);unsigned width=16>>level;
    for(unsigned p=0;p<width*width;p++)memcpy(pixels+p*channels,c,channels);
    // The original backend has no TextureWriteTexels convenience entry.
    // Seed its original block-linear storage using the existing roundtrip
    // helper, then exercise the ordinary packaged-texture GPU upload path.
    CHECK(!nvn_swizzle(&t->d,level,layer,pixels,width*channels,t->mem));
  }
}
static VkImageView sampled_array_probe(int id,VkImageLayout expected) {
  VkImageLayout layout=VK_IMAGE_LAYOUT_MAX_ENUM;vk_texture_shader_begin();
  VkImageView view=vk_texture_view_binding(id,0,VK_SHADER_STAGE_FRAGMENT_BIT,&layout);
  if(layout!=expected)LOG("[vulkan sampled array layout mismatch] id=%d got=%d expected=%d",id,layout,expected);
  CHECK(layout==expected);vk_texture_shader_barrier();vk_texture_shader_record();return view;
}
static void sampled_array_tile(unsigned tile) {
  unsigned x=(tile%16)*16,y=(tile/16)*16;
  S.has_viewport=S.has_scissor=1;S.viewport[0][0]=x;S.viewport[0][1]=y;
  S.viewport[0][2]=S.viewport[0][3]=16;S.scissor=(p_scissor){x,y,16,16};
}
static void sampled_array_atlas(NVNtexture *source,NVNtexture *output,int format,unsigned layers,
                                int id,unsigned mutation,VkImageLayout expected) {
  S.targets=(p_set_rt){.n=1,.color={(const tex_t *)output}};
  p_draw draw={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};mtl_sub_begin();
  for(unsigned level=0;level<3;level++)for(unsigned layer=0;layer<layers;layer++) {
    // Bound the owned fixture's recording size; the existing diagnostic query
    // pool has512 regions. Preserve all pixels and the strict layout checks.
    unsigned tile=level*layers+layer;
    if(tile && !(tile%128)) {vk_internal_commit();mtl_sub_begin();}
    NVNtextureView v=sampled_array_view(level,layer);mtl_reg_tex(id,(const tex_t *)source,(const view_t *)&v);
    sampled_array_probe(id,expected);sampled_array_tile(tile);vk_draw(&draw,NULL);
  }
  vk_internal_wait(vk_internal_commit());
  unsigned height=((3*layers+15)/16)*16;size_t bytes=(size_t)256*height*4;
  uint8_t *pixels=malloc(bytes);CHECK(pixels);NVNcopyRegion region={0,0,0,256,height,1};
  CALL(nvnTextureReadTexels,output,NULL,&region,pixels);
  for(unsigned level=0;level<3;level++)for(unsigned layer=0;layer<layers;layer++) {
    uint8_t c[4];sampled_array_color(level,layer,format,mutation,c);unsigned tile=level*layers+layer;
    if(format==NVN_FORMAT_RGBA8_SRGB)for(unsigned k=0;k<3;k++) {
      double v=c[k]/255.;c[k]=(uint8_t)lround(255*(v<=.04045?v/12.92:pow((v+.055)/1.055,2.4)));
    }
    unsigned x=(tile%16)*16,y=(tile/16)*16;
    for(unsigned yy=0;yy<16;yy++)for(unsigned xx=0;xx<16;xx++)for(unsigned k=0;k<4;k++) {
      unsigned got=pixels[((y+yy)*256+x+xx)*4+k];
      if(abs((int)got-(int)c[k])>1)LOG("[vulkan sampled array mismatch] layers=%u mip=%u layer=%u xy=%u,%u channel=%u got=%u expected=%u mutation=%u",layers,level,layer,xx,yy,k,got,c[k],mutation);
      CHECK(abs((int)got-(int)c[k])<=1);
    }
  }
  free(pixels);
}
static void sampled_array_variant(const void *vertex,const void *fragment,int format,
                                   unsigned layers,unsigned reference) {
  enum { pool_bytes=8<<20,source_stride=2<<20,output_offset=4<<20,vertex_offset=6<<20,
         transfer_offset=(6<<20)+65536,texture_id=60,sampler_id=61 };
  LOG("[vulkan sampled array fixture] begin format=%d layers=%u mips=3 reference=%u",format,layers,reference);
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,pool_bytes));memset(storage,0,pool_bytes);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;CALL(nvnMemoryPoolBuilderSetDefaults,&mb);
  CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,pool_bytes);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtexture source[2],output;unsigned source_count=format==NVN_FORMAT_RGBA8 && layers==4?2:1;
  for(unsigned n=0;n<source_count+1;n++) {
    int out=n==source_count;NVNtextureBuilder b;CALL(nvnTextureBuilderSetDefaults,&b);
    CALL(nvnTextureBuilderSetDevice,&b,&device);CALL(nvnTextureBuilderSetTarget,&b,out?NVN_TEXTURE_TARGET_2D:NVN_TEXTURE_TARGET_2D_ARRAY);
    CALL(nvnTextureBuilderSetWidth,&b,out?256:16);CALL(nvnTextureBuilderSetHeight,&b,out?((3*layers+15)/16)*16:16);
    if(!out)CALL(nvnTextureBuilderSetDepth,&b,layers);
    CALL(nvnTextureBuilderSetLevels,&b,out?1:3);CALL(nvnTextureBuilderSetFormat,&b,out?NVN_FORMAT_RGBA8:format);
    if(!out) {NVNpackagedTextureLayout packaged={{0}};CALL(nvnTextureBuilderSetPackagedTextureLayout,&b,&packaged);}
    CHECK(CALL(nvnTextureBuilderGetStorageSize,&b)<=source_stride);
    CALL(nvnTextureBuilderSetStorage,&b,&pool,out?output_offset:n*source_stride);
    CHECK(CALL(nvnTextureInitialize,out?&output:&source[n],&b));
    if(!out)sampled_array_seed(&source[n],format,layers);
  }
  NVNprogram program;NVNshaderData stages[]={{.control=vertex},{.control=fragment}};
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,stages));
  NVNsamplerBuilder sb;NVNsampler sampler;CALL(nvnSamplerBuilderSetDefaults,&sb);CALL(nvnSamplerBuilderSetDevice,&sb,&device);
  CALL(nvnSamplerBuilderSetMinMagFilter,&sb,NVN_MIN_FILTER_NEAREST,NVN_MAG_FILTER_NEAREST);
  CALL(nvnSamplerBuilderSetWrapMode,&sb,NVN_WRAP_MODE_CLAMP_TO_EDGE,NVN_WRAP_MODE_CLAMP_TO_EDGE,NVN_WRAP_MODE_CLAMP_TO_EDGE);
  CHECK(CALL(nvnSamplerInitialize,&sampler,&sb));mtl_set_tex_pool_n(256);mtl_reg_samp(sampler_id,&((const samp_t *)&sampler)->p);
  const float vertices[]={-1,-1,0,0, 3,-1,2,0, -1,3,0,2};memcpy(storage+vertex_offset,vertices,sizeof vertices);
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);
  CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  S=(vk_state){.program=(const prog_t *)&program,.depth_range={0,1},.chmask={~0u}};
  S.poly=*(const poly_st *)&polygon;S.ds=*(const ds_st *)&ds;S.color=*(const color_st *)&color;
  S.targets=(p_set_rt){.n=1,.color={(const tex_t *)&output}};S.tex[1][0]=texture_id;S.samp[1][0]=(uint64_t)sampler_id<<20;
  for(unsigned a=0;a<2;a++) {
    NVNvertexAttribState attr;CALL(nvnVertexAttribStateSetDefaults,&attr);CALL(nvnVertexAttribStateSetStreamIndex,&attr,0);
    CALL(nvnVertexAttribStateSetFormat,&attr,NVN_FORMAT_RG32F,a*8);S.attr[a]=*(const va_st *)&attr;
  }
  NVNvertexStreamState stream;CALL(nvnVertexStreamStateSetDefaults,&stream);CALL(nvnVertexStreamStateSetStride,&stream,16);
  S.stream[0]=*(const vs_st *)&stream;S.vertex[0]=(NVNbufferRange){(uintptr_t)storage+vertex_offset,sizeof vertices};
  vk_pipeline_state_changed();vk_image_layout_reference(reference);
  NVNtextureView first=sampled_array_view(0,0);mtl_reg_tex(texture_id,(const tex_t *)&source[0],(const view_t *)&first);
  // Initial CPU upload really uses transfer, so its own submit stays GENERAL.
  mtl_sub_begin();sampled_array_probe(texture_id,VK_IMAGE_LAYOUT_GENERAL);vk_internal_wait(vk_internal_commit());
  VkImageLayout expected=!reference && layers<=256?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_GENERAL;
  sampled_array_atlas(&source[0],&output,format,layers,texture_id,0,expected);
  if(!reference && format==NVN_FORMAT_RGBA8 && layers==4) {
    // Runtime requests apply only at a new physical submission. Reuse the
    // same images/views and verify actual sampling of every layer and mip
    // through read-only -> GENERAL -> read-only transitions, not a new image.
    CHECK(!vk_sampled_array_layout_reference_state());
    mtl_reg_tex(texture_id,(const tex_t *)&source[0],(const view_t *)&first);mtl_sub_begin();
    VkImageView same=sampled_array_probe(texture_id,expected);
    vk_sampled_array_layout_reference(1);
    CHECK(!vk_sampled_array_layout_reference_state());
    CHECK(sampled_array_probe(texture_id,expected)==same);
    sampled_array_tile(0);p_draw switch_draw={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};vk_draw(&switch_draw,NULL);
    vk_internal_wait(vk_internal_commit());
    sampled_array_atlas(&source[0],&output,format,layers,texture_id,0,VK_IMAGE_LAYOUT_GENERAL);
    CHECK(vk_sampled_array_layout_reference_state());
    mtl_reg_tex(texture_id,(const tex_t *)&source[0],(const view_t *)&first);mtl_sub_begin();
    CHECK(sampled_array_probe(texture_id,VK_IMAGE_LAYOUT_GENERAL)==same);
    vk_sampled_array_layout_reference(0);
    CHECK(vk_sampled_array_layout_reference_state());
    CHECK(sampled_array_probe(texture_id,VK_IMAGE_LAYOUT_GENERAL)==same);
    sampled_array_tile(0);vk_draw(&switch_draw,NULL);vk_internal_wait(vk_internal_commit());
    sampled_array_atlas(&source[0],&output,format,layers,texture_id,0,expected);
    CHECK(!vk_sampled_array_layout_reference_state());
    LOG("[vulkan sampled array fixture] same images runtime RO -> GENERAL -> RO actual pixels passed");
  }
  // Reusing a cached immutable view must not reuse its former descriptor layout.
  mtl_reg_tex(texture_id,(const tex_t *)&source[0],(const view_t *)&first);mtl_sub_begin();
  VkImageView before=sampled_array_probe(texture_id,expected),again=sampled_array_probe(texture_id,expected);CHECK(before==again);
  sampled_array_tile(0);p_draw draw={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};vk_draw(&draw,NULL);
  p_copy_b2t copy={.src=(uintptr_t)storage+transfer_offset,.tex=(const tex_t *)&source[0],
      .view=*(const view_t *)&first,.has_view=1,.r={0,0,0,16,16,1}};
  vk_texture_copy_t2b(&copy,0,0);CHECK(sampled_array_probe(texture_id,VK_IMAGE_LAYOUT_GENERAL)==before);
  sampled_array_tile(1);vk_draw(&draw,NULL);vk_internal_wait(vk_internal_commit());
  uint8_t golden[4];sampled_array_color(0,0,format,0,golden);unsigned channels=format==NVN_FORMAT_R8?1:4;
  for(unsigned p=0;p<256;p++)for(unsigned k=0;k<channels;k++)CHECK(storage[transfer_offset+p*channels+k]==golden[k]);
  mtl_sub_begin();CHECK(sampled_array_probe(texture_id,expected)==before);vk_internal_wait(vk_internal_commit());
  sampled_array_atlas(&source[0],&output,format,layers,texture_id,0,expected);
  if(source_count==2) {
    // Actual image-store promotion copies every mip/layer and invalidates the
    // sampled recipe before any descriptors are emitted for the new storage.
    uint8_t control[0x880];memset(control,7,sizeof control);NVNprogram compute;NVNshaderData cs={.control=control};
    CHECK(CALL(nvnProgramInitialize,&compute,&device));CHECK(CALL(nvnProgramSetShaders,&compute,1,&cs));
    NVNtextureView written=sampled_array_view(0,1);mtl_reg_tex(texture_id+2,(const tex_t *)&source[0],(const view_t *)&written);
    mtl_reg_tex(texture_id,(const tex_t *)&source[0],(const view_t *)&first);mtl_sub_begin();
    before=sampled_array_probe(texture_id,expected);sampled_array_tile(0);vk_draw(&draw,NULL);
    vk_state graphics=S;S.program=(const prog_t *)&compute;S.img[5][0]=texture_id+2;vk_pipeline_state_changed();
    p_dispatch dispatch={4,4,1,0};vk_dispatch(&dispatch);S=graphics;vk_pipeline_state_changed();
    CHECK(sampled_array_probe(texture_id,VK_IMAGE_LAYOUT_GENERAL)!=before);sampled_array_tile(1);vk_draw(&draw,NULL);
    vk_internal_wait(vk_internal_commit());sampled_array_atlas(&source[0],&output,format,layers,texture_id,1,VK_IMAGE_LAYOUT_GENERAL);
    CALL(nvnProgramFinalize,&compute);
    // A separate pristine array proves attachment promotion independently of
    // storage promotion. Write one mip/layer; all untouched cells survive.
    mtl_reg_tex(texture_id,(const tex_t *)&source[1],(const view_t *)&first);mtl_sub_begin();
    sampled_array_probe(texture_id,VK_IMAGE_LAYOUT_GENERAL);vk_internal_wait(vk_internal_commit());
    mtl_sub_begin();before=sampled_array_probe(texture_id,expected);sampled_array_tile(0);vk_draw(&draw,NULL);
    NVNtextureView cv=sampled_array_view(1,1);p_set_rt target={.n=1,.color={(const tex_t *)&source[1]},.has_cview={1}};
    target.cview[0]=*(const view_t *)&cv;p_clear_color clear={.mask=15,.rgba={0,1,1,1}};
    vk_target_clear_color(&target,NULL,&clear);vk_render_end();
    CHECK(sampled_array_probe(texture_id,VK_IMAGE_LAYOUT_GENERAL)!=before);
    // Different-layer sampling+attachment is legal; the original array alias
    // policy conservatively pins the complete image GENERAL for this draw.
    cv=sampled_array_view(0,2);S.targets=target;S.targets.cview[0]=*(const view_t *)&cv;
    sampled_array_tile(0);vk_pipeline_state_changed();vk_draw(&draw,NULL);
    vk_internal_wait(vk_internal_commit());sampled_array_atlas(&source[1],&output,format,layers,texture_id,2,VK_IMAGE_LAYOUT_GENERAL);
  }
  LOG("[vulkan sampled array fixture] format=%d layers=%u mips=3 reference=%u actual RGBA sampling, cached descriptor transfer pin/recovery and promotions passed",format,layers,reference);
  CALL(nvnProgramFinalize,&program);CALL(nvnSamplerFinalize,&sampler);CALL(nvnTextureFinalize,&output);
  for(unsigned n=0;n<source_count;n++)CALL(nvnTextureFinalize,&source[n]);CALL(nvnMemoryPoolFinalize,&pool);free(storage);
}
static void sampled_color_array_layout_test(const void *vertex,const void *fragment) {
  vk_state saved=S;uint64_t count;int reference;vk_image_layout_stats(&count,&count,&count,&reference);
  int sampled_array_reference=vk_sampled_array_layout_reference_state();
  vk_sampled_array_layout_reference(0);
  uint64_t hits_before,hits_after,prepares,misses;int binding_reference,binding_active;
  vk_texture_binding_stats(&hits_before,&prepares,&misses,&binding_reference);vk_texture_binding_reference(0);
  unsigned layers[]={2,4,256,257};
  const int formats[]={NVN_FORMAT_RGBA8,NVN_FORMAT_R8,NVN_FORMAT_RGBA8_SRGB};
  for(unsigned format=0;format<3;format++)for(unsigned n=0;n<4;n++) {
    if(format==2 && layers[n]>4)continue; // Bounds already exercised in both scalar/RGBA cases.
    if(layers[n]>V.props.limits.maxImageArrayLayers) {LOG("[vulkan sampled array fixture] layers=%u exceeds physical limit=%u; no invalid image created",layers[n],V.props.limits.maxImageArrayLayers);continue;}
    for(unsigned mode=0;mode<2;mode++)sampled_array_variant(vertex,fragment,formats[format],layers[n],mode);
  }
  vk_texture_binding_stats(&hits_after,&prepares,&misses,&binding_active);CHECK(!binding_active && hits_after>hits_before);
  vk_texture_binding_reference(binding_reference);vk_image_layout_reference(reference);
  vk_sampled_array_layout_reference(sampled_array_reference);S=saved;vk_pipeline_state_changed();
}
