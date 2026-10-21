// Original NVN fixture only. No feature substitution, game code or new renderer.
// The ordinary decoded reference is initialized by an actual clear, so its
// later full copies exercise baseline RGBA instead of fresh ASTC admission.
static int page_array_hardware_format(VkFormat f,int source) {
  VkFormatProperties props;vk_format_properties(f,&props);
  VkFormatFeatureFlags need=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|
      VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT|
      (source?VK_FORMAT_FEATURE_BLIT_SRC_BIT:VK_FORMAT_FEATURE_BLIT_DST_BIT);
  VkImageFormatProperties p;
  return (props.optimalTilingFeatures&need)==need &&
    vkGetPhysicalDeviceImageFormatProperties(V.physical,f,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,0,&p)==VK_SUCCESS &&
    p.maxExtent.width>=16 && p.maxExtent.height>=16 && p.maxArrayLayers>=3 && p.maxMipLevels>=3 &&
    (p.sampleCounts&VK_SAMPLE_COUNT_1_BIT);
}
static void page_array_seed(const tex_t *s,unsigned layer) {
  // The source is ONE real packaged texture. CPU reuploads change colors
  // between destination layers; staging/dirty/hazards remain the original path.
  dm_guest_write_prepare(s->mem,s->L.size);
  for(unsigned level=0;level<3;level++) {
    unsigned width=16>>level;int bw,bh;unsigned bpb=nvn_fmt_bpb(s->d.format,&bw,&bh);
    unsigned pitch=(width+bw-1)/bw*bpb,rows=(width+bh-1)/bh;
    uint8_t blocks[256],block[16];CHECK(pitch*rows<=sizeof blocks);
    memcpy(block,astc_decode_constant,16);
    for(unsigned c=0;c<4;c++)block[8+2*c]=block[9+2*c]=mixed_rgba_colors[(layer+level)%3][c];
    for(unsigned at=0;at<pitch*rows;at+=16)memcpy(blocks+at,block,16);
    CHECK(!nvn_swizzle(&s->d,level,0,blocks,pitch,s->mem));
  }
}
static void page_array_physical(const tex_t *t,VkFormat format,int decoded,VkImage *raw) {
  unsigned state[5];CHECK(vk_texture_test_astc_copy_info(t,state,raw));
  CHECK(!state[0] && state[1]==format && state[2]==(unsigned)t->d.format && state[4]==(unsigned)decoded);
}
static void page_array_draw_begin(const tex_t *t,unsigned layer,unsigned level,NVNtexture *out,
    NVNprogram *program,NVNsampler *sampler,uint8_t *data) {
  NVNtextureView view;CALL(nvnTextureViewSetDefaults,&view);
  CALL(nvnTextureViewSetTarget,&view,NVN_TEXTURE_TARGET_2D);
  CALL(nvnTextureViewSetLayers,&view,layer,1);CALL(nvnTextureViewSetLevels,&view,level,1);
  mtl_reg_tex(71,t,(view_t *)&view);astc_copy_sample_setup(program,sampler,out,data);
  mtl_sub_begin();p_draw draw={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};vk_draw(&draw,NULL);
}
static void page_array_output(NVNtexture *out,uint8_t pixels[1024]) {
  NVNcopyRegion region={0,0,0,16,16,1};CALL(nvnTextureReadTexels,out,NULL,&region,pixels);
}
static void page_array_sample_pair(const tex_t *a,const tex_t *b,NVNtexture out[2],
    NVNprogram *program,NVNsampler *sampler,uint8_t *data) {
  for(unsigned layer=0;layer<3;layer++)for(unsigned level=0;level<3;level++) {
    uint8_t pixels[2][1024];
    page_array_draw_begin(a,layer,level,&out[0],program,sampler,data);vk_internal_wait(vk_internal_commit());
    page_array_output(&out[0],pixels[0]);
    page_array_draw_begin(b,layer,level,&out[1],program,sampler,data);vk_internal_wait(vk_internal_commit());
    page_array_output(&out[1],pixels[1]);
    for(unsigned p=0;p<256;p++)for(unsigned c=0;c<4;c++) {
      CHECK(abs((int)pixels[0][4*p+c]-(int)pixels[1][4*p+c])<=1);
      CHECK(abs((int)pixels[0][4*p+c]-(int)mixed_rgba_colors[(layer+level)%3][c])<=1);
    }
  }
}
static void page_array_hardware_case(int format,int srgb,int optimized,uint8_t *data) {
  unsigned start=checks;vk_state saved=S;memset(data,0x91,4u<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,data,4u<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  // source, fresh candidate, initialized reference, two ordinary RGBA outputs,
  // and a final fresh target whose source certificate has been revoked on GPU.
  NVNtexture textures[6];const unsigned offsets[]={0,65536,131072,2u<<20,(2u<<20)+65536,196608};
  NVNtextureBuilder b;
  for(unsigned n=0;n<6;n++) {
    int array=n==1 || n==2 || n==5,output=n==3 || n==4;
    CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,array?NVN_TEXTURE_TARGET_2D_ARRAY:NVN_TEXTURE_TARGET_2D);
    CALL(nvnTextureBuilderSetWidth,&b,16);CALL(nvnTextureBuilderSetHeight,&b,16);
    CALL(nvnTextureBuilderSetDepth,&b,array?3:1);CALL(nvnTextureBuilderSetLevels,&b,output?1:3);
    CALL(nvnTextureBuilderSetFormat,&b,n==0?format:output?(srgb?NVN_FORMAT_RGBA8_SRGB:NVN_FORMAT_RGBA8):
      (srgb?NVN_FORMAT_RGBA_DXT5_SRGB:NVN_FORMAT_RGBA_DXT5));
    if(!n) {NVNpackagedTextureLayout layout={{0}};CALL(nvnTextureBuilderSetPackagedTextureLayout,&b,&layout);}
    CALL(nvnTextureBuilderSetStorage,&b,&pool,offsets[n]);CHECK(CALL(nvnTextureBuilderGetStorageSize,&b)<=65536);
    CHECK(CALL(nvnTextureInitialize,&textures[n],&b));
  }
  tex_t *s=(tex_t *)&textures[0],*d=(tex_t *)&textures[1],*ref=(tex_t *)&textures[2];
  VkFormat astc=vk_nvn_format(format),rgba=srgb?VK_FORMAT_R8G8B8A8_SRGB:VK_FORMAT_R8G8B8A8_UNORM;
  mtl_sub_begin();
  for(unsigned level=0;level<3;level++) {
    p_clear_tex clear={.tex=ref,.has_view=1,.view={.set_levels=1,.base_level=level},
      .r={0,0,0,16>>level,16>>level,3},.mask=15,.c.f={0,0,0,0}};
    vk_texture_clear(&clear);
  }
  for(unsigned layer=0;layer<3;layer++) {
    page_array_seed(s,layer);vk_texture_test_astc_copy_upload(s);
    unsigned source[5];CHECK(vk_texture_test_astc_info(s,VK_NULL_HANDLE,source));
    CHECK(source[0]==(unsigned)optimized && !source[1]);
    for(unsigned level=0;level<3;level++) {
      p_copy_t2t p={.src=s,.dst=d,.has_sv=1,.has_dv=1,
        .sv={.set_levels=1,.base_level=level},.dv={.set_levels=1,.base_level=level},
        .sr={0,0,0,16>>level,16>>level,1},.dr={0,0,(int)layer,16>>level,16>>level,1}};
      vk_texture_copy_t2t(&p);p.dst=ref;vk_texture_copy_t2t(&p);
    }
  }
  page_array_physical(d,optimized?astc:rgba,!optimized,NULL);page_array_physical(ref,rgba,1,NULL);
  vk_internal_wait(vk_internal_commit());
  uint8_t vertex[0x880],fragment[0x880];memset(vertex,3,sizeof vertex);memset(fragment,4,sizeof fragment);
  NVNprogram program;NVNshaderData shaders[]={{.control=vertex},{.control=fragment}};
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  NVNsamplerBuilder sb;NVNsampler sampler;CALL(nvnSamplerBuilderSetDefaults,&sb);CALL(nvnSamplerBuilderSetDevice,&sb,&device);
  CALL(nvnSamplerBuilderSetMinMagFilter,&sb,NVN_MIN_FILTER_NEAREST,NVN_MAG_FILTER_NEAREST);
  CALL(nvnSamplerBuilderSetWrapMode,&sb,NVN_WRAP_MODE_CLAMP_TO_EDGE,NVN_WRAP_MODE_CLAMP_TO_EDGE,NVN_WRAP_MODE_CLAMP_TO_EDGE);
  CHECK(CALL(nvnSamplerInitialize,&sampler,&sb));
  const float vertices[]={-1,-1,0,0,3,-1,2,0,-1,3,0,2};memcpy(data+(3u<<20),vertices,sizeof vertices);
  page_array_sample_pair(d,ref,&textures[3],&program,&sampler,data);
  // A real FS read of the OLD compressed image is still in this submission
  // when a later BC Buffer upload forces all-mip/layer restoration.
  page_array_draw_begin(d,1,0,&textures[3],&program,&sampler,data);
  uint8_t *raw=data+(1u<<20);dm_guest_write_prepare(raw,256);for(unsigned at=0;at<256;at+=16)memcpy(raw+at,mixed_bc_colors[0],16);
  p_copy_b2t input={.tex=d,.src=(uintptr_t)raw,.has_view=1,
    .view={.set_layers=1,.min_layer=1},.r={4,4,0,8,8,1}};
  vk_texture_copy_b2t(&input,0,0);input.tex=ref;vk_texture_copy_b2t(&input,0,0);
  page_array_physical(d,rgba,1,NULL);vk_internal_wait(vk_internal_commit());
  uint8_t old_pixels[1024];page_array_output(&textures[3],old_pixels);
  for(unsigned p=0;p<256;p++)for(unsigned c=0;c<4;c++)CHECK(old_pixels[4*p+c]==mixed_rgba_colors[1][c]);
  for(unsigned layer=0;layer<3;layer++)for(unsigned level=0;level<3;level++) {
    mixed_check_rgba(d,level,layer,1,0);mixed_check_rgba(ref,level,layer,1,0);
  }
  // Sticky restore: a compatible later ASTC copy must not oscillate back.
  mtl_sub_begin();p_copy_t2t again={.src=s,.dst=d,.sr={0,0,0,16,16,1},.dr={0,0,0,16,16,1}};
  vk_texture_copy_t2t(&again);page_array_physical(d,rgba,1,NULL);vk_internal_wait(vk_internal_commit());
  // Same valid source bytes written by GPU revoke the whole-upload proof.
  // A new fresh array must take the ordinary decoded path, not gain a claim
  // that all its remaining, undefined mips/layers contain LDR bytes.
  mtl_sub_begin();int bw,bh;nvn_fmt_bpb(format,&bw,&bh);
  unsigned pitch=(16+bw-1)/bw*16,rows=(16+bh-1)/bh;
  dm_guest_write_prepare(raw,pitch*rows);
  for(unsigned at=0;at<pitch*rows;at+=16) {
    memcpy(raw+at,astc_decode_constant,16);
    for(unsigned c=0;c<4;c++)raw[at+8+2*c]=raw[at+9+2*c]=mixed_rgba_colors[0][c];
  }
  input=(p_copy_b2t){.tex=s,.src=(uintptr_t)raw,.r={0,0,0,16,16,1}};
  vk_texture_copy_b2t(&input,0,0);unsigned proof[5];CHECK(vk_texture_test_astc_info(s,VK_NULL_HANDLE,proof));CHECK(!proof[0]);
  again.dst=(tex_t *)&textures[5];vk_texture_copy_t2t(&again);
  page_array_physical((tex_t *)&textures[5],rgba,1,NULL);vk_internal_wait(vk_internal_commit());
  mixed_check_rgba((tex_t *)&textures[5],0,0,0,0);
  S=saved;vk_pipeline_state_changed();CALL(nvnSamplerFinalize,&sampler);CALL(nvnProgramFinalize,&program);
  for(unsigned n=0;n<6;n++)CALL(nvnTextureFinalize,&textures[n]);
  CALL(nvnMemoryPoolFinalize,&pool);
  CHECK(vk_validation_error_count()==0);
  printf("{\"test_details\":\"page-array-hardware-case\",\"format\":%d,\"sRGB\":%s,\"optimized\":%s,\"actual_ASTC_GPU\":true,\"admitted\":%s,\"source_LDR_certificate\":%s,\"mips\":3,\"layers\":3,\"checks\":%u,\"sample_and_baseline_pixels\":true,\"late_BC_restore_all_subresources\":true,\"old_FS_read_inflight\":true,\"unknown_source_fallback\":true,\"result\":\"passed\"}\n",
    format,srgb?"true":"false",optimized?"true":"false",optimized?"true":"false",optimized?"true":"false",checks-start);
}
static void page_array_hardware_test(void) {
  CHECK(!vk_pool_shared_mode() && !vk_legacy_astc_copy_mode() && !V.astc_decode_mode);
  const char *mode=getenv("DM_VULKAN_COMPRESSED_PAGE_ARRAYS");CHECK(mode && (!strcmp(mode,"0") || !strcmp(mode,"1")));
  int optimized=!strcmp(mode,"1");
  if(!V.features.textureCompressionASTC_LDR || V.props.deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU ||
      V.features.textureCompressionBC) {
    printf("{\"test\":\"page-array-hardware-oracle\",\"result\":\"unsupported\",\"actual_ASTC_GPU\":false,\"requires_actual_ASTC_and_baseline_BC_decode\":true,\"game_modules_executed\":false}\n");return;
  }
  const int formats[]={NVN_FORMAT_RGBA_ASTC_4x4,NVN_FORMAT_RGBA_ASTC_6x6,NVN_FORMAT_RGBA_ASTC_4x4_SRGB,NVN_FORMAT_RGBA_ASTC_6x6_SRGB};
  for(unsigned n=0;n<4;n++)if(!page_array_hardware_format(vk_nvn_format(formats[n]),1) ||
      !page_array_hardware_format(n>=2?VK_FORMAT_R8G8B8A8_SRGB:VK_FORMAT_R8G8B8A8_UNORM,0)) {
    printf("{\"test\":\"page-array-hardware-oracle\",\"result\":\"unsupported\",\"format\":%d,\"missing_actual_blit_or_image_caps\":true,\"game_modules_executed\":false}\n",formats[n]);return;
  }
  // Distinct addresses: finalized logical images retain their original
  // translation grace. Reusing the same 4MiB VA would correctly block a later
  // fresh admission against those still-live, defined aliases.
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,16u<<20));
  for(unsigned n=0;n<4;n++)page_array_hardware_case(formats[n],n>=2,optimized,storage+((size_t)n<<22));
  free(storage);
  printf("{\"test\":\"page-array-hardware-oracle\",\"result\":\"passed\",\"optimized\":%s,\"cases\":4,\"actual_ASTC_GPU\":true,\"checks\":%u,\"game_modules_executed\":false}\n",optimized?"true":"false",checks);
}
