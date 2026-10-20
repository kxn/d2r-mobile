// Exercises original NVN logical BC3 arrays with upstream ASTC packaged pages.
// Hardware ASTC availability is checked, never invented for a software driver.
extern int vk_texture_test_astc_copy_info(const tex_t *,unsigned[5],VkImage *);
extern unsigned vk_texture_test_astc_copy_policy(void *,size_t);
extern void vk_texture_test_astc_copy_upload(const tex_t *);
static void astc_copy_seed(NVNtexture *texture,const uint8_t block[16]) {
  const tex_t *t=(const tex_t *)texture;uint8_t linear[128*128];
  dm_guest_write_prepare(t->mem,t->L.size);
  for(unsigned b=0;b<sizeof linear;b+=16)memcpy(linear+b,block,16);
  for(unsigned level=0;level<t->d.levels;level++) {
    unsigned width=t->d.width>>level;if(!width)width=1;
    CHECK(!nvn_swizzle(&t->d,level,0,linear,((width+3)/4)*16,t->mem));
  }
}
static void astc_copy_blocks(const tex_t *t,unsigned level,unsigned layer,const uint8_t base[16],const uint8_t *roi) {
  mtl_sub_begin();unsigned state[5];VkImage physical;
  CHECK(vk_texture_test_astc_copy_info(t,state,&physical));CHECK(state[0]==1);
  CHECK(state[1]==VK_FORMAT_ASTC_4x4_UNORM_BLOCK && state[2]==NVN_FORMAT_RGBA_DXT5 && !state[3] && !state[4]);
  unsigned width=t->d.width>>level;if(!width)width=1;
  unsigned blocks=(width+3)/4,nbytes=blocks*blocks*16;
  vk_buffer *result=vk_temp_buffer(nbytes);
  VkBufferImageCopy read={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level,layer,1},.imageExtent={width,width,1}};
  vkCmdCopyImageToBuffer(V.cmd,physical,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,result->handle,1,&read);
  vk_internal_wait(vk_internal_commit());
  for(unsigned y=0;y<blocks;y++)for(unsigned x=0;x<blocks;x++)
    CHECK(!memcmp(result->map+16*(y*blocks+x),roi && x==1 && y==1?roi:base,16));
}
static void astc_copy_sample_setup(NVNprogram *program,NVNsampler *sampler,NVNtexture *output,uint8_t *data) {
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);
  CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);
  S=(vk_state){.program=(const prog_t *)program,.depth_range={0,1},.chmask={~0u}};
  S.poly=*(const poly_st *)&polygon;S.ds=*(const ds_st *)&ds;S.color=*(const color_st *)&color;
  S.targets=(p_set_rt){.n=1,.color={(const tex_t *)output}};S.tex[1][0]=71;S.samp[1][0]=(uint64_t)72<<20;
  mtl_reg_samp(72,&((const samp_t *)sampler)->p);
  S.has_viewport=S.has_scissor=1;S.viewport[0][2]=S.viewport[0][3]=16;S.scissor=(p_scissor){0,0,16,16};
  for(unsigned n=0;n<2;n++) {
    NVNvertexAttribState attr;CALL(nvnVertexAttribStateSetDefaults,&attr);CALL(nvnVertexAttribStateSetStreamIndex,&attr,0);
    CALL(nvnVertexAttribStateSetFormat,&attr,NVN_FORMAT_RG32F,n*8);S.attr[n]=*(const va_st *)&attr;
  }
  NVNvertexStreamState stream;CALL(nvnVertexStreamStateSetDefaults,&stream);CALL(nvnVertexStreamStateSetStride,&stream,16);
  S.stream[0]=*(const vs_st *)&stream;S.vertex[0]=(NVNbufferRange){(uintptr_t)data+(3u<<20),48};vk_pipeline_state_changed();
}
// Metadata/real HLE oracle only: registered rejection regions stand for a
// propagated encoding; this does not pretend to create unsupported ASTC GPU.
static void legacy_astc_cpu_escape_test(void) {
  size_t bytes=65536;uint8_t *data=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
  CHECK(data!=MAP_FAILED);*(uint32_t *)data=0x12345678;*(uint32_t *)(data+4096)=0xabcdef01;
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,data,bytes);CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNeventBuilder eb;NVNevent before,ordinary;
  CALL(nvnEventBuilderSetDefaults,&eb);CALL(nvnEventBuilderSetStorage,&eb,&pool,4096);CHECK(CALL(nvnEventInitialize,&before,&eb));
  CALL(nvnEventBuilderSetStorage,&eb,&pool,0);CHECK(CALL(nvnEventInitialize,&ordinary,&eb));
  CHECK(vk_pool_legacy_encoding_guard_add((uintptr_t)data+4096,4096));
  uint64_t begin=0,length=0,id=mtl_pool_cpu_access_needed((uintptr_t)data+4094,4,&begin,&length);
  CHECK(id && begin==(uintptr_t)data+4096 && length==2);
  CHECK(!mtl_pool_cpu_access_needed((uintptr_t)data,4,&begin,&length));
  CHECK(CALL(nvnEventGetValue,&ordinary)==0x12345678);
  const char *negative=getenv("DM_LEGACY_ASTC_CPU_ESCAPE_NEGATIVE");
  if(negative && !strcmp(negative,"event-prior"))CALL(nvnEventGetValue,&before);
  if(negative && !strcmp(negative,"event-init")) {CALL(nvnEventBuilderSetStorage,&eb,&pool,4096);NVNevent event;CALL(nvnEventInitialize,&event,&eb);}
  if(negative && !strcmp(negative,"invalidate"))CALL(nvnMemoryPoolInvalidateMappedRange,&pool,4096,4);
  NVNcommandBuffer command;CHECK(CALL(nvnCommandBufferInitialize,&command,&device));
  CALL(nvnCommandBufferAddCommandMemory,&command,&pool,32768,16384);CALL(nvnCommandBufferBeginRecording,&command);
  CALL(nvnCommandBufferUpdateUniformBuffer,&command,(uintptr_t)data+16384,4,0,4,
      negative && !strcmp(negative,"literal")?data+4096:data);
  CALL(nvnCommandBufferEndRecording,&command);CALL(nvnCommandBufferFinalize,&command);
  if(negative)die("Legacy ASTC CPU escape unexpectedly accepted: %s",negative);
  // Exact guards have a hard bound, and registration fails BEFORE a caller
  // changes physical encoding. Ordinary disjoint bytes remain accessible.
  for(unsigned k=0;k<255;k++)CHECK(vk_pool_legacy_encoding_guard_add((uintptr_t)data+8192+k*4,4));
  CHECK(!vk_pool_legacy_encoding_guard_add((uintptr_t)data+12288,4));
  CHECK(!vk_pool_legacy_encoding_guard_add((uintptr_t)data+4096,4096));
  CALL(nvnEventFinalize,&before);CALL(nvnEventFinalize,&ordinary);CALL(nvnMemoryPoolFinalize,&pool);
  // Pool close retires metadata; the same VA has no stale encoding exclusion.
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));CHECK(!mtl_pool_cpu_access_needed((uintptr_t)data+4096,4,&begin,&length));
  CALL(nvnMemoryPoolFinalize,&pool);CHECK(!munmap(data,bytes));
  printf("{\"test_details\":\"legacy-ASTC-CPU-escape-guard\",\"metadata_only\":true,\"nonoverlap_no_owner_job\":true,\"exact_clipping\":true,\"capacity_before_adopt\":true,\"retained_until_pool_close\":true,\"same_VA_new_pool_not_excluded\":true,\"result\":\"passed\"}\n");
}
static void astc_copy_encoding_test(void) {
  int legacy=vk_legacy_astc_copy_mode() && !vk_pool_shared_active();
  int real_window_heap=!legacy && V.options.pool_window_bytes && getenv("DM_SHARED_GUEST_HEAP_FIXTURE");
  size_t size=(4u<<20)+65536;uint8_t *raw=NULL,*data=bytes;
  if(!real_window_heap) {
    raw=mmap(NULL,size,PROT_READ|PROT_WRITE,(legacy?MAP_PRIVATE:MAP_SHARED)|MAP_ANONYMOUS,-1,0);
    CHECK(raw!=MAP_FAILED);data=(void *)(((uintptr_t)raw+65535)&~(uintptr_t)65535);
  }
  memset(data,0xa7,4u<<20);
  unsigned policy=vk_texture_test_astc_copy_policy(data,8192);CHECK(policy>=20);
  printf("{\"test_details\":\"ASTC4-copy-admission-policy\",\"checks\":%u,\"same_value_CPU_prepare_and_fault_rejected\":true,\"unrelated_page_keeps_proof\":true}\n",policy);
  if(!V.features.textureCompressionASTC_LDR || V.props.deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU) {
    if(raw)CHECK(!munmap(raw,size));
    printf("{\"test_details\":\"ASTC4-copy-physical-encoding\",\"actual_ASTC_GPU\":false,\"result\":\"unsupported\"}\n");return;
  }
  if(!legacy && !real_window_heap)vk_test_shared_image_backing(data,4u<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,data,4u<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtexture source,destination,output[2];NVNtextureBuilder b;
  for(unsigned n=0;n<4;n++) {
    CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,n==1?NVN_TEXTURE_TARGET_2D_ARRAY:NVN_TEXTURE_TARGET_2D);
    CALL(nvnTextureBuilderSetWidth,&b,n<2?128:16);CALL(nvnTextureBuilderSetHeight,&b,n<2?128:16);
    CALL(nvnTextureBuilderSetDepth,&b,n==1?42:1);CALL(nvnTextureBuilderSetLevels,&b,n==0?8:n==1?7:1);
    CALL(nvnTextureBuilderSetFormat,&b,n==0?NVN_FORMAT_RGBA_ASTC_4x4:n==1?NVN_FORMAT_RGBA_DXT5:n==2?NVN_FORMAT_RGBA8:NVN_FORMAT_RGBA8_SRGB);
    if(!n) {NVNpackagedTextureLayout layout={{0}};CALL(nvnTextureBuilderSetPackagedTextureLayout,&b,&layout);}
    CALL(nvnTextureBuilderSetStorage,&b,&pool,n==0?0:n==1?65536:(2u<<20)+(n-2)*65536);
    CHECK(CALL(nvnTextureInitialize,n==0?&source:n==1?&destination:&output[n-2],&b));
  }
  tex_t *s=(tex_t *)&source,*d=(tex_t *)&destination;
  CHECK(!d->has_layout && d->d.format==NVN_FORMAT_RGBA_DXT5 && d->L.size<=(2u<<20)-65536);
  astc_copy_seed(&source,astc_decode_constant);
  const float vertices[]={-1,-1,0,0,3,-1,2,0,-1,3,0,2};memcpy(data+(3u<<20),vertices,sizeof vertices);
  mtl_sub_begin();vk_texture_cpu_preflight(s);vk_texture_test_astc_copy_upload(s);
  vk_internal_wait(vk_internal_commit());
  if(!legacy) {
    // 0b defers CPU-only image sources. A REAL Buffer copy establishes the
    // old required imported provenance without assuming image upload pins it.
    mtl_sub_begin();vk_pool_requirement need={(uintptr_t)data,16,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0};
    int op=vk_pool_operation_begin_ranges(&need,1);uint8_t proof[16];
    pool_window_read(data,16,proof);vk_pool_operation_end(op);vk_internal_wait(vk_internal_commit());
    CHECK(!memcmp(proof,data,16));
    if(real_window_heap) {
      uint64_t w=V.options.pool_window_bytes;CHECK(d->L.size>w);
      vk_pool_requirement adjacent[2]={
        {(uintptr_t)d->mem,16,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0},
        {(uintptr_t)d->mem+w,16,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0}};
      vk_pool_reserve_result result;CHECK(vk_pool_reserve_ranges(adjacent,2,w,&result)==VK_POOL_RESERVE_OK);
      CHECK(!vk_pool_shared_materialized((uintptr_t)d->mem,d->L.size));
    }
  }
  int initially_decoded=0;
  if(legacy) {
    // Reuse the existing owned selector, not a false device capability.
    vk_texture_test_force_bc_decode(1);mtl_sub_begin();vk_texture_test_astc_copy_upload(d);
    unsigned initial[5];CHECK(vk_texture_test_astc_copy_info(d,initial,NULL));
    CHECK(!initial[0] && initial[1]==VK_FORMAT_R8G8B8A8_UNORM && initial[4]);initially_decoded=1;
    CHECK(!((uintptr_t)d->mem&(getpagesize()-1)) && !(d->L.size&(getpagesize()-1)));
    vk_texture_test_force_bc_decode(0);vk_internal_wait(vk_internal_commit());
  }
  const char *negative=getenv("DM_ASTC_COPY_NEGATIVE");
  if(negative && !strcmp(negative,"existing")) {
    dm_guest_write_prepare(data+(3u<<20)+4096,16384);memset(data+(3u<<20)+4096,0,16384);
    mtl_sub_begin();p_copy_b2t old={.src=(uintptr_t)data+(3u<<20)+4096,.tex=d,.r={0,0,0,128,128,1}};
    vk_texture_copy_b2t(&old,0,0);vk_internal_wait(vk_internal_commit());
  }
  p_copy_t2t pair={.src=s,.dst=d,.sr={0,0,0,128,128,1},.dr={0,0,41,128,128,1}};
  mtl_sub_begin();vk_texture_copy_t2t(&pair);vk_internal_wait(vk_internal_commit());
  if(negative && !strcmp(negative,"existing"))die("ASTC copy accepted an already-defined BC3 destination");
  astc_copy_blocks(d,0,41,astc_decode_constant,NULL);
  // Same physical encoding across multiple destination layers/mips, and a
  // nonzero block ROI. Every byte outside that ROI must remain unchanged.
  mtl_sub_begin();pair.dr.zoffset=40;vk_texture_copy_t2t(&pair);
  pair.has_sv=pair.has_dv=1;pair.sv=(view_t){.set_levels=1,.base_level=1};pair.dv=pair.sv;
  pair.sr=pair.dr=(NVNcopyRegion){0,0,0,64,64,1};pair.dr.zoffset=1;
  vk_texture_copy_t2t(&pair);vk_internal_wait(vk_internal_commit());astc_copy_blocks(d,1,1,astc_decode_constant,NULL);
  uint8_t alternate[16];memcpy(alternate,astc_decode_constant,16);alternate[8]=alternate[9]=255;
  astc_copy_seed(&source,alternate);
  mtl_sub_begin();pair.has_sv=pair.has_dv=0;
  pair.sr=(NVNcopyRegion){4,4,0,4,4,1};pair.dr=(NVNcopyRegion){4,4,40,4,4,1};
  vk_texture_copy_t2t(&pair);vk_internal_wait(vk_internal_commit());
  astc_copy_blocks(d,0,40,astc_decode_constant,alternate);
  // The compatibility path does not pretend that logical BC3 Pool bytes now
  // contain either recompressed BC3 or ASTC. Optional CPU proofs must decline.
  for(size_t k=0;k<d->L.size;k++)CHECK(((uint8_t *)d->mem)[k]==0xa7);
  mtl_sub_begin();uint32_t untouched=0x12345678;
  CHECK(vk_pool_try_read((uintptr_t)d->mem,4,&untouched)==0 && untouched==0x12345678);
  for(unsigned ref=0;ref<2;ref++){vk_cover_wait_reference(ref);CHECK(!vk_cover_read((uintptr_t)d->mem,4,&untouched));CHECK(untouched==0x12345678);}
  vk_cover_wait_reference(0);vk_internal_wait(vk_internal_commit());
  if(negative) {
    if(!strcmp(negative,"cpu-same") || !strcmp(negative,"cpu-other-mip")) {
      volatile uint8_t *at=(uint8_t *)d->mem+(!strcmp(negative,"cpu-other-mip")?nvn_tex_level_offset(&d->d,6):0);
      uint8_t old=*at;*at=old;mtl_sub_begin();unsigned info[5];vk_texture_test_astc_copy_info(d,info,NULL);
    } else if(!strcmp(negative,"required-linear")) {mtl_sub_begin();vk_pool_read((uintptr_t)d->mem,4,&untouched);}
    else if(!strcmp(negative,"gpu-linear")) {mtl_sub_begin();vk_buffer *buffer;uint64_t offset;vk_pool_address_stage((uintptr_t)d->mem,4,(void **)&buffer,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT);}
    else if(!strcmp(negative,"b2t")) {mtl_sub_begin();p_copy_b2t in={.tex=d,.src=(uintptr_t)data+(3u<<20),.r={0,0,0,4,4,1}};vk_texture_copy_b2t(&in,0,0);}
    else if(!strcmp(negative,"attachment")) {mtl_sub_begin();vk_texture_prepare_attachment(d,NULL);}
    else if(!strcmp(negative,"untyped")) {mtl_sub_begin();vk_texture_native(d);}
    else if(!strcmp(negative,"reinterpret")) {NVNtextureView v;CALL(nvnTextureViewSetDefaults,&v);CALL(nvnTextureViewSetFormat,&v,NVN_FORMAT_RGB_DXT1);mtl_reg_tex(71,d,(const view_t *)&v);mtl_sub_begin();vk_texture_view_binding(71,0,VK_SHADER_STAGE_FRAGMENT_BIT,NULL);}
    else if(!strcmp(negative,"storage")) {mtl_reg_tex(71,d,NULL);mtl_sub_begin();vk_texture_view_binding(71,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL);}
    else if(!strcmp(negative,"late-image-writer")) {NVNtexture other;CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);CALL(nvnTextureBuilderSetWidth,&b,128);CALL(nvnTextureBuilderSetHeight,&b,128);CALL(nvnTextureBuilderSetFormat,&b,NVN_FORMAT_RGBA_DXT5);CALL(nvnTextureBuilderSetStorage,&b,&pool,65536);CHECK(CALL(nvnTextureInitialize,&other,&b));mtl_sub_begin();p_copy_b2t in={.tex=(const tex_t *)&other,.src=(uintptr_t)data+(3u<<20),.r={0,0,0,4,4,1}};vk_texture_copy_b2t(&in,0,0);}
    else if(!strcmp(negative,"t2b")) {mtl_sub_begin();p_copy_b2t out={.tex=d,.src=(uintptr_t)data+(3u<<20),.r={0,0,41,4,4,1}};vk_texture_copy_t2b(&out,0,0);}
    else if(!strcmp(negative,"readtexels")) {uint8_t out[16];NVNcopyRegion roi={0,0,41,4,4,1};CALL(nvnTextureReadTexels,&destination,NULL,&roi,out);}
    else die("Unknown ASTC copy negative");
    die("ASTC copy negative unexpectedly accepted: %s",negative);
  }
  vk_state saved=S;uint8_t vertex[0x880],fragment[0x880];memset(vertex,3,sizeof vertex);memset(fragment,4,sizeof fragment);
  NVNprogram program;NVNshaderData shaders[]={{.control=vertex},{.control=fragment}};
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  NVNsamplerBuilder sb;NVNsampler sampler;CALL(nvnSamplerBuilderSetDefaults,&sb);CALL(nvnSamplerBuilderSetDevice,&sb,&device);
  CALL(nvnSamplerBuilderSetMinMagFilter,&sb,NVN_MIN_FILTER_NEAREST,NVN_MAG_FILTER_NEAREST);
  CALL(nvnSamplerBuilderSetWrapMode,&sb,NVN_WRAP_MODE_CLAMP_TO_EDGE,NVN_WRAP_MODE_CLAMP_TO_EDGE,NVN_WRAP_MODE_CLAMP_TO_EDGE);
  CHECK(CALL(nvnSamplerInitialize,&sampler,&sb));mtl_set_tex_pool_n(256);
  for(unsigned mode=0;mode<3;mode++) {
    NVNtextureView view;CALL(nvnTextureViewSetDefaults,&view);CALL(nvnTextureViewSetTarget,&view,NVN_TEXTURE_TARGET_2D);
    CALL(nvnTextureViewSetLayers,&view,41,1);CALL(nvnTextureViewSetLevels,&view,0,1);
    if(mode)CALL(nvnTextureViewSetFormat,&view,mode==2?NVN_FORMAT_RGBA_DXT5_SRGB:NVN_FORMAT_RGBA_DXT5);
    mtl_reg_tex(71,d,(const view_t *)&view);astc_copy_sample_setup(&program,&sampler,&output[mode==2],data);
    astc_decode_draw(&output[mode==2]);
  }
  S=saved;vk_pipeline_state_changed();CALL(nvnSamplerFinalize,&sampler);CALL(nvnProgramFinalize,&program);
  CALL(nvnTextureFinalize,&source);CALL(nvnTextureFinalize,&destination);
  for(unsigned n=0;n<2;n++)CALL(nvnTextureFinalize,&output[n]);
  CALL(nvnMemoryPoolFinalize,&pool);if(!legacy && !real_window_heap)vk_test_shared_image_backing(NULL,0);if(raw)CHECK(!munmap(raw,size));
  CHECK(!vk_validation_error_count());
  if(legacy)printf("{\"test_details\":\"legacy-ASTC4-copy-storage\",\"actual_ASTC_GPU\":true,\"initial_decoded_RGBA\":%s,\"original_whole_dirty_tracker\":true,\"no_host_import\":true,\"complete_hostpage_destination\":true,\"result\":\"passed\"}\n",initially_decoded?"true":"false");
  printf("{\"test_details\":\"ASTC4-copy-physical-encoding\",\"actual_ASTC_GPU\":true,\"fresh_BC3_array_became_ASTC4\":true,\"logical_BC3_unchanged\":true,\"mip_layer_ROI_preserved\":true,\"default_and_BC3_srgb_views\":true,\"CPU_version_observer\":true,\"canonical_pool_bytes_unchanged\":true,\"result\":\"passed\"}\n");
}
