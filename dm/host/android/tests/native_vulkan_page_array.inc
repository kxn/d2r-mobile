// Focused state/lifetime oracle, reusing the existing BC3 mip/layer byte oracle.
// Lavapipe BC3 is a restoration stand-in ONLY: no ASTC feature is fabricated.
#if !defined(DM_ANDROID_APP)
extern void vk_test_page_array_bc_restoration(const tex_t *);
extern int vk_test_page_array_state(const tex_t *);
extern void vk_test_page_array_restore(const tex_t *);
extern unsigned vk_test_page_array_policy(const tex_t *);
static void compressed_page_array_test(void) {
  CHECK(!vk_pool_shared_mode() && !vk_legacy_astc_copy_mode());
  CHECK(V.features.textureCompressionBC);
  if(!V.features.textureCompressionASTC_LDR)
    printf("{\"test\":\"page-array-ASTC-canonical\",\"result\":\"unsupported\",\"actual_ASTC_GPU\":false}\n");
  const char *trigger=getenv("DM_PAGE_ARRAY_RESTORE_TRIGGER");if(!trigger)trigger="explicit";
  int srgb=getenv("DM_PAGE_ARRAY_SRGB")!=NULL;
  uint8_t *data=bytes;memset(data,0x91,4u<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,data,4u<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtexture texture;NVNtextureBuilder b;
  CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
  CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_2D_ARRAY);
  CALL(nvnTextureBuilderSetWidth,&b,16);CALL(nvnTextureBuilderSetHeight,&b,16);
  CALL(nvnTextureBuilderSetDepth,&b,3);CALL(nvnTextureBuilderSetLevels,&b,3);
  CALL(nvnTextureBuilderSetFormat,&b,srgb?NVN_FORMAT_RGBA_DXT5_SRGB:NVN_FORMAT_RGBA_DXT5);
  CALL(nvnTextureBuilderSetStorage,&b,&pool,65536);CHECK(CALL(nvnTextureInitialize,&texture,&b));
  tex_t *t=(tex_t *)&texture;
  for(unsigned l=0;l<3;l++)for(unsigned m=0;m<3;m++) {
    uint8_t *raw=data+(1u<<20)+l*4096+m*512;
    for(unsigned at=0;at<256;at+=16)memcpy(raw+at,mixed_bc_colors[(l+m)%3],16);
  }
  mtl_sub_begin();CHECK(vk_test_page_array_policy(t)>=18);
  for(unsigned l=0;l<3;l++)for(unsigned m=0;m<3;m++) {
    p_copy_b2t copy={.tex=t,.src=(uintptr_t)data+(1u<<20)+l*4096+m*512,.has_view=1,
      .view={.set_levels=1,.base_level=m,.set_layers=1,.min_layer=l},.r={0,0,0,16>>m,16>>m,1}};
    vk_texture_copy_b2t(&copy,0,0);
  }
  vk_internal_wait(vk_internal_commit());mtl_sub_begin();
  unsigned state[5];VkImage old;
  CHECK(vk_texture_test_astc_copy_info(t,state,&old));CHECK(!state[4]);
  vk_buffer *old_bytes=vk_temp_buffer(256);
  VkBufferImageCopy read={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={16,16,1}};
  vkCmdCopyImageToBuffer(V.cmd,old,vk_texture_test_bc_copy_layout(t),old_bytes->handle,1,&read);
  mtl_set_tex_pool_n(256);mtl_reg_tex(71,t,NULL);vk_texture_shader_begin();
  VkImageView old_view=vk_texture_view_binding(71,0,VK_SHADER_STAGE_FRAGMENT_BIT,NULL);
  vk_texture_shader_abandon();CHECK(old_view);
  vk_test_page_array_bc_restoration(t);CHECK(vk_test_page_array_state(t)==1);
  int incoming=0;NVNtexture source;int has_source=0;
  if(!strcmp(trigger,"cpu")) {
    // Same-value writes must revoke the representation and cached view too.
    dm_guest_write_prepare(t->mem,t->L.size);t->mem[0]=t->mem[0];
    vk_texture_shader_begin();
    VkImageView now=vk_texture_view_binding(71,0,VK_SHADER_STAGE_FRAGMENT_BIT,NULL);
    vk_texture_shader_abandon();CHECK(now && now!=old_view);
  } else if(!strcmp(trigger,"storage")) {
    CHECK(!srgb);vk_texture_prepare_binding(71,1,VK_SHADER_STAGE_COMPUTE_BIT);
    vk_texture_shader_begin();CHECK(vk_texture_view_binding(71,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL));
    vk_texture_shader_abandon();
  } else if(!strcmp(trigger,"attachment")) vk_texture_prepare_attachment(t,NULL);
  else if(!strcmp(trigger,"clear")) {
    p_clear_tex clear={.tex=t,.has_view=1,.view={.set_layers=1,.min_layer=1},
      .r={0,0,0,16,16,1},.mask=15,.c.f={0,1,0,1}};
    vk_texture_clear(&clear);
  } else if(!strcmp(trigger,"mixed-copy")) {
    CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetDepth,&b,1);
    CALL(nvnTextureBuilderSetLevels,&b,1);CALL(nvnTextureBuilderSetStorage,&b,&pool,131072);
    NVNpackagedTextureLayout layout={{0}};CALL(nvnTextureBuilderSetPackagedTextureLayout,&b,&layout);
    CHECK(CALL(nvnTextureInitialize,&source,&b));has_source=1;
    tex_t *s=(tex_t *)&source;uint8_t raw[256];
    for(unsigned at=0;at<sizeof raw;at+=16)memcpy(raw+at,mixed_bc_colors[0],16);
    dm_guest_write_prepare(s->mem,s->L.size);CHECK(!nvn_swizzle(&s->d,0,0,raw,64,s->mem));
    p_copy_t2t pair={.src=s,.dst=t,.sr={4,4,0,8,8,1},.dr={4,4,1,8,8,1}};
    vk_texture_copy_t2t(&pair);incoming=1;
  } else if(!strcmp(trigger,"raw")) CHECK(vk_texture_native(t)!=old);
  else if(!strcmp(trigger,"b2t")) {
    p_copy_b2t copy={.tex=t,.src=(uintptr_t)data+(1u<<20),.has_view=1,
      .view={.set_layers=1,.min_layer=1},.r={4,4,0,8,8,1}};
    vk_texture_copy_b2t(&copy,0,0);incoming=1;
  } else {CHECK(!strcmp(trigger,"explicit"));vk_test_page_array_restore(t);}
  CHECK(vk_test_page_array_state(t)==2);
  vk_internal_wait(vk_internal_commit());
  for(unsigned at=0;at<256;at+=16)CHECK(!memcmp(old_bytes->map+at,mixed_bc_colors[0],16));
  for(unsigned l=0;l<3;l++)for(unsigned m=0;m<3;m++)mixed_check_rgba(t,m,l,incoming,0);
  // Sticky restoration, repeated callbacks and new recording do not restore
  // the compressed representation or lose any non-overwritten mip/layer.
  mtl_sub_begin();vk_texture_test_astc_copy_upload(t);CHECK(vk_test_page_array_state(t)==2);
  vk_internal_wait(vk_internal_commit());
  if(has_source)CALL(nvnTextureFinalize,&source);
  CALL(nvnTextureFinalize,&texture);CALL(nvnMemoryPoolFinalize,&pool);CHECK(vk_validation_error_count()==0);
  printf("{\"test\":\"page-array-restoration\",\"result\":\"passed\",\"trigger\":\"%s\",\"checks\":%u,\"actual_BC3_GPU_restoration\":true,\"ASTC_canonical_admission_exercised\":false,\"game_modules_executed\":false}\n",trigger,checks);
}
#endif
