// Owned real GPU preserving BC3 -> RGBA migration. ASTC input is exercised
// only with actual hardware support, never advertised by the software host.
extern void vk_texture_test_bc_copy_decode(const p_copy_t2t *);
extern VkImageLayout vk_texture_test_bc_copy_layout(const tex_t *);
static const uint8_t mixed_bc_colors[3][16]={
  {255,255,0,0,0,0,0,0,0,248,0,248,0,0,0,0},
  {255,255,0,0,0,0,0,0,224,7,224,7,0,0,0,0},
  {255,255,0,0,0,0,0,0,31,0,31,0,0,0,0,0}};
static const uint8_t mixed_rgba_colors[3][4]={{255,0,0,255},{0,255,0,255},{0,0,255,255}};
static void mixed_check_rgba(const tex_t *t,unsigned level,unsigned layer,int incoming,int astc) {
  unsigned width=t->d.width>>level;if(!width)width=1;
  mtl_sub_begin();unsigned state[5];VkImage physical;
  CHECK(vk_texture_test_astc_copy_info(t,state,&physical));
  CHECK(!state[0] && state[1]==(t->d.format==NVN_FORMAT_RGBA_DXT5_SRGB?VK_FORMAT_R8G8B8A8_SRGB:VK_FORMAT_R8G8B8A8_UNORM) && state[4]);
  vk_buffer *readback=vk_temp_buffer(width*width*4);
  VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level,layer,1},.imageExtent={width,width,1}};
  vkCmdCopyImageToBuffer(V.cmd,physical,vk_texture_test_bc_copy_layout(t),readback->handle,1,&copy);
  vk_internal_wait(vk_internal_commit());
  for(unsigned y=0;y<width;y++)for(unsigned x=0;x<width;x++) {
    const uint8_t *want=mixed_rgba_colors[(layer+level)%3];
    const uint8_t astc_color[]={31,63,127,255};
    if(incoming && level==0 && layer==1 && x>=4 && x<12 && y>=4 && y<12)
      want=astc?astc_color:mixed_rgba_colors[0];
    for(unsigned c=0;c<4;c++)CHECK(abs((int)readback->map[4*(y*width+x)+c]-(int)want[c])<=1);
  }
}
static void mixed_compressed_copy_test(void) {
  CHECK(vk_pool_shared_mode());CHECK(getenv("DM_SHARED_GUEST_HEAP_FIXTURE"));
  int astc=getenv("DM_MIXED_COMPRESSED_ASTC_SOURCE")!=NULL;
  int srgb=getenv("DM_MIXED_COMPRESSED_SRGB")!=NULL;
  if(astc && (!V.features.textureCompressionASTC_LDR || V.props.deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU)) {
    printf("{\"test\":\"mixed-compressed-array\",\"actual_ASTC_GPU\":false,\"result\":\"unsupported\"}\n");return;
  }
  uint8_t *data=bytes;memset(data,0x91,4u<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,data,4u<<20);CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtexture source,destination;NVNtextureBuilder builder;
  for(unsigned n=0;n<2;n++) {
    CALL(nvnTextureBuilderSetDefaults,&builder);CALL(nvnTextureBuilderSetDevice,&builder,&device);
    CALL(nvnTextureBuilderSetWidth,&builder,16);CALL(nvnTextureBuilderSetHeight,&builder,16);
    CALL(nvnTextureBuilderSetDepth,&builder,n?3:1);CALL(nvnTextureBuilderSetLevels,&builder,3);
    CALL(nvnTextureBuilderSetTarget,&builder,n?NVN_TEXTURE_TARGET_2D_ARRAY:NVN_TEXTURE_TARGET_2D);
    CALL(nvnTextureBuilderSetFormat,&builder,!n&&astc?(srgb?NVN_FORMAT_RGBA_ASTC_4x4_SRGB:NVN_FORMAT_RGBA_ASTC_4x4):(srgb?NVN_FORMAT_RGBA_DXT5_SRGB:NVN_FORMAT_RGBA_DXT5));
    if(!n) {NVNpackagedTextureLayout layout={{0}};CALL(nvnTextureBuilderSetPackagedTextureLayout,&builder,&layout);}
    CALL(nvnTextureBuilderSetStorage,&builder,&pool,n?65536:0);
    CHECK(CALL(nvnTextureInitialize,n?&destination:&source,&builder));
  }
  tex_t *s=(tex_t *)&source,*d=(tex_t *)&destination;
  CHECK(!d->has_layout);CHECK(d->L.size<=65536);
  uint8_t linear[256];for(unsigned b=0;b<sizeof linear;b+=16)memcpy(linear+b,astc?astc_decode_constant:mixed_bc_colors[0],16);
  dm_guest_write_prepare(s->mem,s->L.size);
  for(unsigned m=0;m<3;m++)CHECK(!nvn_swizzle(&s->d,m,0,linear,((16>>m)+3)/4*16,s->mem));
  for(unsigned l=0;l<3;l++)for(unsigned m=0;m<3;m++) {
    uint8_t *raw=data+(1u<<20)+l*4096+m*512;
    for(unsigned b=0;b<256;b+=16)memcpy(raw+b,mixed_bc_colors[(l+m)%3],16);
  }
  mtl_sub_begin();vk_texture_test_astc_copy_upload(s);
  // A genuine borrowed Buffer use establishes existing span provenance. It
  // does not label undefined bytes as CPU/GPU image content.
  vk_pool_requirement need={(uintptr_t)d->mem,d->L.size,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0};
  int operation=vk_pool_operation_begin_ranges(&need,1);uint8_t proof[16];pool_window_read(d->mem,16,proof);
  vk_pool_operation_end(operation);

  for(unsigned l=0;l<3;l++)for(unsigned m=0;m<3;m++) {
    p_copy_b2t copy={.tex=d,.src=(uintptr_t)data+(1u<<20)+l*4096+m*512,.has_view=1,
      .view={.set_levels=1,.base_level=m,.set_layers=1,.min_layer=l},.r={0,0,0,16>>m,16>>m,1}};
    vk_texture_copy_b2t(&copy,0,0);
  }
  vk_internal_wait(vk_internal_commit());
  const char *negative=getenv("DM_MIXED_COMPRESSED_NEGATIVE");
  NVNtexture alias;
  if(negative && !strcmp(negative,"prior-alias")) {
    CALL(nvnTextureInitialize,&alias,&builder);mtl_sub_begin();
    p_copy_b2t old={.tex=(tex_t *)&alias,.src=(uintptr_t)data+(1u<<20),.r={0,0,0,16,16,1}};
    vk_texture_copy_b2t(&old,0,0);vk_internal_wait(vk_internal_commit());
  }
  p_copy_t2t pair={.src=s,.dst=d,.sr={4,4,0,8,8,1},.dr={4,4,1,8,8,1}};
  mtl_sub_begin();
  // Keep an actual old VkImage read and cached view alive through replacement.
  unsigned old_state[5];VkImage old_image;
  CHECK(vk_texture_test_astc_copy_info(d,old_state,&old_image));CHECK(!old_state[4]);
  vk_buffer *old_blocks=vk_temp_buffer(256);
  VkBufferImageCopy old_copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={16,16,1}};
  vkCmdCopyImageToBuffer(V.cmd,old_image,vk_texture_test_bc_copy_layout(d),old_blocks->handle,1,&old_copy);
  mtl_set_tex_pool_n(256);mtl_reg_tex(71,d,NULL);vk_texture_shader_begin();
  VkImageView old_view=vk_texture_view_binding(71,0,VK_SHADER_STAGE_FRAGMENT_BIT,NULL);
  vk_texture_shader_abandon();CHECK(old_view);
  if(!astc)vk_texture_test_bc_copy_decode(&pair); // Same real preserving function, no invented ASTC support.
  vk_texture_copy_t2t(&pair);
  vk_texture_shader_begin();VkImageView new_view=vk_texture_view_binding(71,0,VK_SHADER_STAGE_FRAGMENT_BIT,NULL);
  CHECK(new_view && new_view!=old_view);vk_texture_shader_abandon();
  vk_internal_wait(vk_internal_commit());
  for(unsigned b=0;b<256;b+=16)CHECK(!memcmp(old_blocks->map+b,mixed_bc_colors[0],16));
  for(unsigned l=0;l<3;l++)for(unsigned m=0;m<3;m++)mixed_check_rgba(d,m,l,1,astc);
  if(negative) {
    if(!strcmp(negative,"cpu-same") || !strcmp(negative,"cpu-other-mip")) {volatile uint8_t *at=d->mem+(!strcmp(negative,"cpu-other-mip")?nvn_tex_level_offset(&d->d,2):0);uint8_t old=*at;*at=old;mtl_sub_begin();vk_texture_test_astc_copy_upload(d);}
    else if(!strcmp(negative,"b2t")) {mtl_sub_begin();p_copy_b2t in={.tex=d,.src=(uintptr_t)data+(1u<<20),.r={0,0,0,4,4,1}};vk_texture_copy_b2t(&in,0,0);}
    else if(!strcmp(negative,"storage")) {mtl_reg_tex(71,d,NULL);mtl_sub_begin();vk_texture_view_binding(71,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL);}
    else if(!strcmp(negative,"reinterpret")) {NVNtextureView view;CALL(nvnTextureViewSetDefaults,&view);CALL(nvnTextureViewSetFormat,&view,NVN_FORMAT_RGB_DXT1);mtl_reg_tex(71,d,(view_t *)&view);mtl_sub_begin();vk_texture_view_binding(71,0,VK_SHADER_STAGE_FRAGMENT_BIT,NULL);}
    else if(!strcmp(negative,"t2b")) {mtl_sub_begin();p_copy_b2t out={.tex=d,.src=(uintptr_t)data+(2u<<20),.r={0,0,0,4,4,1}};vk_texture_copy_t2b(&out,0,0);}
    else if(!strcmp(negative,"readtexels")) {uint8_t out[16];NVNcopyRegion r={0,0,0,4,4,1};CALL(nvnTextureReadTexels,&destination,NULL,&r,out);}
    else if(!strcmp(negative,"flush")) {CALL(nvnMemoryPoolFlushMappedRange,&pool,65536,16);}
    else if(!strcmp(negative,"invalidate")) {CALL(nvnMemoryPoolInvalidateMappedRange,&pool,65536,16);}
    else if(!strcmp(negative,"required-linear")) {uint8_t out[4];mtl_sub_begin();vk_pool_read((uintptr_t)d->mem,4,out);}
    else if(!strcmp(negative,"untyped")) {mtl_sub_begin();vk_texture_native(d);}
    else if(!strcmp(negative,"late-alias")) {CALL(nvnTextureInitialize,&alias,&builder);mtl_sub_begin();p_copy_b2t in={.tex=(tex_t *)&alias,.src=(uintptr_t)data+(1u<<20),.r={0,0,0,4,4,1}};vk_texture_copy_b2t(&in,0,0);}
    else if(!strcmp(negative,"prior-alias"))die("Mixed compressed prior alias unexpectedly passed");
    else die("Unknown mixed compressed negative");
    die("Mixed compressed negative unexpectedly passed: %s",negative);
  }
  uint32_t untouched=0x12345678;mtl_sub_begin();
  CHECK(!vk_pool_try_read((uintptr_t)d->mem,4,&untouched)&&untouched==0x12345678);
  vk_internal_wait(vk_internal_commit());
  // Exact nonoverlap cache publication is still permitted.
  CALL(nvnMemoryPoolFlushMappedRange,&pool,2u<<20,16);
  CALL(nvnTextureFinalize,&source);CALL(nvnTextureFinalize,&destination);CALL(nvnMemoryPoolFinalize,&pool);
  CHECK(vk_validation_error_count()==0);
  printf("{\"test\":\"mixed-compressed-array\",\"result\":\"passed\",\"checks\":%u,\"actual_ASTC_GPU\":%s,\"prior_BC_layers_mips_preserved\":true,\"ROI_only\":true,\"game_modules_executed\":false}\n",checks,astc?"true":"false");
}
