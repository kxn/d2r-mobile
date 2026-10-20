// Real multi-mip imported RGBA dependency oracle, using the original NVN FS/CS
// shaders. Production ASTC table validation is not relaxed for this host test.
static void native_acquire_scope_gpu_test(void) {
  CHECK(vk_pool_shared_mode() && !V.options.pool_window_bytes);
  NVNmemoryPoolBuilder pb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&pb);CALL(nvnMemoryPoolBuilderSetDevice,&pb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&pb,bytes,4u<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&pb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&pb));
  VkExternalMemoryImageCreateInfo ext={.sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
    .handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
  VkImageCreateInfo create={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.pNext=&ext,
    .imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,.extent={32,32,1},.mipLevels=3,.arrayLayers=1,
    .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
    .usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT};
  VkImage raw;VK_CHECK(vkCreateImage(V.device,&create,NULL,&raw));VkMemoryRequirements needs;
  vkGetImageMemoryRequirements(V.device,raw,&needs);CHECK(needs.size && needs.size<65536 && needs.alignment<=65536);
  uint64_t address=(uintptr_t)bytes+65536;CHECK(!(address%needs.alignment));
  NVNtexture source,output;NVNtextureBuilder tb;
  CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetWidth,&tb,32);CALL(nvnTextureBuilderSetHeight,&tb,32);CALL(nvnTextureBuilderSetLevels,&tb,3);
  CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);CALL(nvnTextureBuilderSetStorage,&tb,&pool,65536);
  CHECK(CALL(nvnTextureInitialize,&source,&tb));
  // This owned descriptor takes the actual imported requirements. It is not
  // accepted by the production file/table protocol, which remains ASTC-only.
  ((tex_t *)&source)->L.size=needs.size;((tex_t *)&source)->L.align=needs.alignment;
  CALL(nvnTextureBuilderSetWidth,&tb,16);CALL(nvnTextureBuilderSetHeight,&tb,16);CALL(nvnTextureBuilderSetLevels,&tb,1);
  CALL(nvnTextureBuilderSetStorage,&tb,&pool,2u<<20);CHECK(CALL(nvnTextureInitialize,&output,&tb));
  extern void vk_test_native_rgba_adopt(const tex_t *,VkImage,VkDeviceMemory,void *,const VkImageCreateInfo *,const nvn_native_entry *);
  mtl_sub_begin();vk_test_native_rgba_adopt((const tex_t *)&source,VK_NULL_HANDLE,VK_NULL_HANDLE,NULL,NULL,NULL);
  vk_internal_wait(vk_internal_commit());
  mtl_sub_begin();VkDeviceMemory memory;void *allocation=vk_pool_native_image_bind(address,needs.size,raw,&needs,&memory);
  VkImageMemoryBarrier init={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
    .oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_GENERAL,.image=raw,
    .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,3,0,1},.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&init);
  const unsigned red[]={255,128,64};
  for(unsigned n=0;n<3;n++) {
    VkClearColorValue color={.float32={red[n]/255.f,0,0,1}};VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,n,1,0,1};
    vkCmdClearColorImage(V.cmd,raw,VK_IMAGE_LAYOUT_GENERAL,&color,1,&range);
  }
  init.oldLayout=VK_IMAGE_LAYOUT_GENERAL;init.srcQueueFamilyIndex=V.queue_family;init.dstQueueFamilyIndex=VK_QUEUE_FAMILY_FOREIGN_EXT;
  init.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;init.dstAccessMask=0;
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,NULL,0,NULL,1,&init);
  vk_internal_wait(vk_internal_commit());
  nvn_native_entry entry={0};entry.recipe.image_size=needs.size;entry.recipe.usage=create.usage;entry.recipe.levels=3;
  // Conservative full-image byte coverage for every mip is sufficient for this
  // dependency oracle. No physical mip layout, codec or opaque payload is baked.
  for(unsigned n=0;n<3;n++)entry.recipe.mips[n].size=needs.size;
  vk_test_native_rgba_adopt((const tex_t *)&source,raw,memory,allocation,&create,&entry);
  uint8_t controls[5][0x880];const unsigned markers[]={3,4,23,0,1};NVNprogram graphics,compute,primer;
  for(unsigned n=0;n<5;n++)memset(controls[n],markers[n],sizeof controls[n]);
  NVNshaderData stages[]={{.control=controls[0]},{.control=controls[1]}};
  CHECK(CALL(nvnProgramInitialize,&graphics,&device));CHECK(CALL(nvnProgramSetShaders,&graphics,2,stages));
  stages[0].control=controls[2];CHECK(CALL(nvnProgramInitialize,&compute,&device));CHECK(CALL(nvnProgramSetShaders,&compute,1,stages));
  stages[0].control=controls[3];stages[1].control=controls[4];
  CHECK(CALL(nvnProgramInitialize,&primer,&device));CHECK(CALL(nvnProgramSetShaders,&primer,2,stages));
  NVNsamplerBuilder sb;NVNsampler sampler;CALL(nvnSamplerBuilderSetDefaults,&sb);CALL(nvnSamplerBuilderSetDevice,&sb,&device);
  CALL(nvnSamplerBuilderSetMinMagFilter,&sb,NVN_MIN_FILTER_NEAREST,NVN_MAG_FILTER_NEAREST);
  CHECK(CALL(nvnSamplerInitialize,&sampler,&sb));mtl_set_tex_pool_n(256);mtl_reg_samp(72,&((const samp_t *)&sampler)->p);
  const float vertices[]={-1,-1,0,0,3,-1,2,0,-1,3,0,2};memcpy(bytes+(3u<<20),vertices,sizeof vertices);
  CALL(nvnMemoryPoolFlushMappedRange,&pool,3u<<20,sizeof vertices);
  NVNpolygonState ps;NVNcolorState cs;NVNdepthStencilState ds;
  CALL(nvnPolygonStateSetDefaults,&ps);CALL(nvnPolygonStateSetCullFace,&ps,NVN_FACE_NONE);CALL(nvnColorStateSetDefaults,&cs);
  CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);
  CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  vk_state graphics_state={.program=(const prog_t *)&graphics,.depth_range={0,1},.chmask={~0u},.has_viewport=1,.has_scissor=1};
  graphics_state.poly=*(const poly_st *)&ps;graphics_state.color=*(const color_st *)&cs;graphics_state.ds=*(const ds_st *)&ds;
  graphics_state.viewport[0][2]=graphics_state.viewport[0][3]=16;graphics_state.scissor=(p_scissor){0,0,16,16};
  graphics_state.targets=(p_set_rt){.n=1,.color={(const tex_t *)&output}};
  graphics_state.tex[1][0]=71;graphics_state.samp[1][0]=(uint64_t)72<<20;
  for(unsigned n=0;n<2;n++){NVNvertexAttribState a;CALL(nvnVertexAttribStateSetDefaults,&a);
    CALL(nvnVertexAttribStateSetFormat,&a,NVN_FORMAT_RG32F,n*8);graphics_state.attr[n]=*(const va_st *)&a;}
  NVNvertexStreamState stream;CALL(nvnVertexStreamStateSetDefaults,&stream);CALL(nvnVertexStreamStateSetStride,&stream,16);
  graphics_state.stream[0]=*(const vs_st *)&stream;graphics_state.vertex[0]=(NVNbufferRange){(uintptr_t)bytes+(3u<<20),sizeof vertices};
  float *primer_color=(void *)(bytes+(3u<<20)+32768);
  primer_color[0]=primer_color[1]=primer_color[2]=0;primer_color[3]=1;
  CALL(nvnMemoryPoolFlushMappedRange,&pool,(3u<<20)+32768,16);
  uint32_t *computed=(void *)(bytes+(3u<<20)+65536);uint8_t *transferred=bytes+(3u<<20)+131072;
  for(unsigned order=0;order<3;order++) {
    mtl_sub_begin();
    if(!order) {
      S=graphics_state;S.program=(const prog_t *)&primer;
      S.ubo[1][0]=(p_bind_buf){.addr=(uintptr_t)primer_color,.size=16};
      vk_pipeline_state_changed();vk_draw(&(p_draw){.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3},NULL);
      CHECK(vk_render_active());
    }
    if(order==1) {
      NVNtextureView v=nr_view(1);mtl_reg_tex(71,(const tex_t *)&source,(const view_t *)&v);
      S=(vk_state){.program=(const prog_t *)&compute};S.tex[5][0]=71;S.samp[5][0]=(uint64_t)72<<20;
      S.ssbo[5][0]=(p_bind_buf){.addr=(uintptr_t)computed,.size=256};vk_pipeline_state_changed();vk_dispatch(&(p_dispatch){1,1,1});
    }
    if(order==2) {
      NVNtextureView v=nr_view(2);p_copy_b2t copy={.tex=(const tex_t *)&source,.src=(uintptr_t)transferred,
        .view=*(const view_t *)&v,.has_view=1,.r={0,0,0,8,8,1}};vk_texture_copy_t2b(&copy,0,0);
    }
    NVNtextureView first=nr_view(0);mtl_reg_tex(71,(const tex_t *)&source,(const view_t *)&first);
    S=graphics_state;vk_pipeline_state_changed();vk_draw(&(p_draw){.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3},NULL);
    if(!order) {
      extern VkPipelineStageFlags vk_test_native_unused_mip_source(uint64_t,unsigned);
      // Unvisited mip1 must retain the acquire's real directed source. This
      // semantic invariant catches the old memset-to-TOP gap independently of
      // whether a serial software GPU happens to return the right pixel values.
      CHECK(vk_test_native_unused_mip_source(((tex_t *)&source)->serial,1)==VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
      NVNtextureView v=nr_view(1);mtl_reg_tex(71,(const tex_t *)&source,(const view_t *)&v);
      S=(vk_state){.program=(const prog_t *)&compute};S.tex[5][0]=71;S.samp[5][0]=(uint64_t)72<<20;
      S.ssbo[5][0]=(p_bind_buf){.addr=(uintptr_t)computed,.size=256};vk_pipeline_state_changed();vk_dispatch(&(p_dispatch){1,1,1});
      v=nr_view(2);p_copy_b2t copy={.tex=(const tex_t *)&source,.src=(uintptr_t)transferred,
        .view=*(const view_t *)&v,.has_view=1,.r={0,0,0,8,8,1}};vk_texture_copy_t2b(&copy,0,0);
    }
    vk_internal_wait(vk_internal_commit());
    if(order!=2)for(unsigned n=0;n<64;n++)CHECK(computed[n]==128);
    if(order!=1)for(unsigned n=0;n<256;n+=4)CHECK(transferred[n]==64 && !transferred[n+1] && !transferred[n+2] && transferred[n+3]==255);
    uint8_t rendered[1024];NVNcopyRegion region={0,0,0,16,16,1};CALL(nvnTextureReadTexels,&output,NULL,&region,rendered);
    for(unsigned n=0;n<sizeof rendered;n+=4)CHECK(rendered[n]==255 && !rendered[n+1] && !rendered[n+2] && rendered[n+3]==255);
  }
  CALL(nvnTextureFinalize,&source);CALL(nvnTextureFinalize,&output);
  CALL(nvnProgramFinalize,&graphics);CALL(nvnProgramFinalize,&compute);CALL(nvnProgramFinalize,&primer);CALL(nvnSamplerFinalize,&sampler);
  CALL(nvnMemoryPoolFinalize,&pool);CHECK(!vk_validation_error_count());
  printf("{\"test\":\"native-acquire-cross-mip-directed-scopes\",\"checks\":%u,\"result\":\"passed\",\"FS0_then_CS1_and_TRANSFER2\":true,\"CS1_then_FS0\":true,\"TRANSFER2_then_FS0\":true,\"actual_imported_RGBA\":true,\"ASTC_executed\":false}\n",checks);
}
