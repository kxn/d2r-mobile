// Real multi-mip imported RGBA dependency oracle, using the original NVN FS/CS
// shaders. Production ASTC table validation is not relaxed for this host test.
static void native_acquire_scope_gpu_test(void) {
  int resident=getenv("DM_NATIVE_RESIDENT_OWNER_FIXTURE")!=NULL;
  int window_fixture=V.options.pool_window_bytes!=0;
  CHECK(vk_pool_shared_mode() && (!window_fixture || (resident && V.options.pool_window_bytes==65536)));
  extern int vk_test_native_resident_provider(int);
  extern int vk_test_native_image_owner(uint64_t);
  extern VkPipelineStageFlags vk_test_native_mip_scope(uint64_t,unsigned,uint64_t *);
  int previous_provider=vk_test_native_resident_provider(resident);
  NVNmemoryPoolBuilder pb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&pb);CALL(nvnMemoryPoolBuilderSetDevice,&pb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&pb,bytes,4u<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&pb,(resident?NVN_MEMORY_POOL_FLAGS_CPU_UNCACHED:
    NVN_MEMORY_POOL_FLAGS_CPU_CACHED)|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
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
  mtl_sub_begin();
  vk_pool_requirement initial_range={address,needs.size,0,VK_PIPELINE_STAGE_TRANSFER_BIT,1,VK_POOL_REQUIRE_NATIVE_IMAGE};
  int initial_operation=vk_pool_operation_begin_ranges(&initial_range,1);
  VkDeviceMemory memory;void *allocation=vk_pool_native_image_bind(address,needs.size,raw,&needs,&memory);
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
  // Preserve the actual finite opaque payload for a later legal CPU producer.
  // No optimal-image permutation is guessed and no runtime payload is patched.
  VkMemoryBarrier initial_host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,
    0,1,&initial_host,0,NULL,0,NULL);
  vk_pool_native_image_writeback(allocation,address,needs.size,VK_PIPELINE_STAGE_TRANSFER_BIT);
  init.oldLayout=VK_IMAGE_LAYOUT_GENERAL;init.srcQueueFamilyIndex=V.queue_family;init.dstQueueFamilyIndex=VK_QUEUE_FAMILY_FOREIGN_EXT;
  init.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;init.dstAccessMask=0;
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,NULL,0,NULL,1,&init);
  vk_pool_operation_end(initial_operation);vk_internal_wait(vk_internal_commit());
  uint8_t *original_payload=malloc(needs.size);CHECK(original_payload);
  memcpy(original_payload,(void *)(uintptr_t)address,needs.size);
  nvn_native_entry entry={0};entry.recipe.image_size=needs.size;entry.recipe.usage=create.usage;entry.recipe.levels=3;
  // Conservative full-image byte coverage for every mip is sufficient for this
  // dependency oracle. No physical mip layout, codec or opaque payload is baked.
  for(unsigned n=0;n<3;n++)entry.recipe.mips[n].size=needs.size;
  vk_test_native_rgba_adopt((const tex_t *)&source,raw,memory,allocation,&create,&entry);
  uint8_t controls[6][0x880];const unsigned markers[]={3,4,23,0,1,26};NVNprogram graphics,compute,primer,copy_compute;
  for(unsigned n=0;n<6;n++)memset(controls[n],markers[n],sizeof controls[n]);
  NVNshaderData stages[]={{.control=controls[0]},{.control=controls[1]}};
  CHECK(CALL(nvnProgramInitialize,&graphics,&device));CHECK(CALL(nvnProgramSetShaders,&graphics,2,stages));
  stages[0].control=controls[2];CHECK(CALL(nvnProgramInitialize,&compute,&device));CHECK(CALL(nvnProgramSetShaders,&compute,1,stages));
  stages[0].control=controls[3];stages[1].control=controls[4];
  CHECK(CALL(nvnProgramInitialize,&primer,&device));CHECK(CALL(nvnProgramSetShaders,&primer,2,stages));
  stages[0].control=controls[5];CHECK(CALL(nvnProgramInitialize,&copy_compute,&device));
  CHECK(CALL(nvnProgramSetShaders,&copy_compute,1,stages));
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
  int previous_writer=getenv("DM_NATIVE_PREVIOUS_WRITER_FIXTURE")!=NULL;
  extern VkPipelineStageFlags vk_test_native_writer_stages(uint64_t,uint64_t);
  // The first three orders retain the complete original multi-mip oracle.
  // Three additional real producer chunks test older TRANSFER and COMPUTE
  // writers; the sampler must not silently succeed by a broad history clear.
  for(unsigned order=0;order<(previous_writer?6u:3u);order++) {
    unsigned producer=order>=3?order-3:0;
    VkPipelineStageFlags producer_stage=producer?VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT:VK_PIPELINE_STAGE_TRANSFER_BIT;
    if(resident && order==5) {
      // A genuinely different payload is already visible before the unknown
      // compute writer restores red, in the SAME recording as its FS consumer.
      uint8_t *green=bytes+(3u<<20)+262144,*saved=bytes+(3u<<20)+327680;
      for(unsigned n=0;n<4096;n+=4){green[n]=green[n+2]=0;green[n+1]=green[n+3]=255;}
      memcpy(saved,original_payload,needs.size);
      CALL(nvnMemoryPoolFlushMappedRange,&pool,(3u<<20)+262144,4096);
      CALL(nvnMemoryPoolFlushMappedRange,&pool,(3u<<20)+327680,needs.size);
      mtl_sub_begin();NVNtextureView v=nr_view(0);p_copy_b2t green_copy={.tex=(const tex_t *)&source,
        .src=(uintptr_t)green,.view=*(const view_t *)&v,.has_view=1,.r={0,0,0,32,32,1}};
      vk_texture_copy_b2t(&green_copy,0,0);vk_internal_wait(vk_internal_commit());
      uint8_t pixels[1024];S=graphics_state;nr_draw(&source,0,&output,pixels);
      for(unsigned n=0;n<sizeof pixels;n+=4)CHECK(!pixels[n] && pixels[n+1]==255 && !pixels[n+2] && pixels[n+3]==255);
    }
    if(order>=3) {
      mtl_sub_begin();
      // Original ReadTexels above deliberately records an unbounded staging
      // transfer. Retire that unrelated unknown state BEFORE the new real
      // producer, so these cases isolate known older writes without weakening
      // the production unknown-state rejection or clearing the tested writer.
      vk_barrier();
      if(!producer) {
        uint8_t *linear=bytes+(3u<<20)+262144;
        for(unsigned n=0;n<4096;n+=4){linear[n]=linear[n+3]=255;linear[n+1]=linear[n+2]=0;}
        CALL(nvnMemoryPoolFlushMappedRange,&pool,(3u<<20)+262144,4096);
        NVNtextureView v=nr_view(0);p_copy_b2t copy={.tex=(const tex_t *)&source,
          .src=(uintptr_t)linear,.view=*(const view_t *)&v,.has_view=1,.r={0,0,0,32,32,1}};
        vk_texture_copy_b2t(&copy,0,0);
      } else {
        // One original compute-copy invocation reads/writes the SAME finite
        // native backing bytes. No opaque layout/codec is guessed or copied
        // to a second payload: its actual image contents remain unchanged.
        S=(vk_state){.program=(const prog_t *)&copy_compute};
        S.ssbo[5][0]=(p_bind_buf){.addr=resident && order==5?(uintptr_t)bytes+(3u<<20)+327680:address,.size=needs.size};
        S.ssbo[5][1]=(p_bind_buf){.addr=address,.size=needs.size};
        vk_pipeline_state_changed();vk_dispatch(&(p_dispatch){1,1,1});
        // Conservative unknown-stage metadata for this REAL GPU write must
        // retain the split. ALL is a superset of its actual compute stage;
        // no fictitious payload write or relaxed data assertion is added.
        if(producer==2)vk_buffer_shader_written(address,needs.size,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
      }
      // The very same recording has a real producer page, so moving an
      // acquire before it must still be rejected. This check has GPU work,
      // and the subsequent cross-chunk sample verifies the actual pixels.
      CHECK(!vk_pool_native_acquire_independent(address,needs.size));
      if(!(resident && order==5))vk_internal_wait(vk_internal_commit());
      CHECK(vk_test_native_writer_stages(address,needs.size)&producer_stage);
    }
    if(!(resident && order==5))mtl_sub_begin();
    if(resident && order) {
      uint64_t retained_clock=0;
      CHECK(vk_test_native_image_owner(((tex_t *)&source)->serial)==2);
      CHECK(vk_test_native_mip_scope(((tex_t *)&source)->serial,1,&retained_clock)==VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
      CHECK(retained_clock); // Real earlier CS use survived chunk retirement.
    }
    if(!order || order>=3) {
      S=graphics_state;S.program=(const prog_t *)&primer;
      S.ubo[1][0]=(p_bind_buf){.addr=(uintptr_t)primer_color,.size=16};
      vk_pipeline_state_changed();vk_draw(&(p_draw){.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3},NULL);
      CHECK(vk_render_active());
      if(order>=3) {
        CHECK(vk_test_native_writer_stages(address,needs.size)&producer_stage);
        // A native-only B2T writer has no canonical VkBuffer identity in
        // window mode. Its conservative negative acquire proof is correct;
        // residency must retain the real dependency without requiring a
        // redundant first-use optimization to become newly admissible.
        CHECK(vk_pool_native_acquire_independent(address,needs.size)==
          (producer!=2 && !(window_fixture && !producer)));
      }
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
    if(!order || order>=3) {
      extern VkPipelineStageFlags vk_test_native_unused_mip_source(uint64_t,unsigned);
      // Unvisited mip1 must retain the acquire's real directed source. This
      // semantic invariant catches the old memset-to-TOP gap independently of
      // whether a serial software GPU happens to return the right pixel values.
      if(resident) {
        uint64_t actual_clock=0;
        CHECK(vk_test_native_mip_scope(((tex_t *)&source)->serial,1,&actual_clock)==VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        if(order)CHECK(actual_clock);
      } else CHECK(vk_test_native_unused_mip_source(((tex_t *)&source)->serial,1)==
        (order==5?VK_PIPELINE_STAGE_ALL_COMMANDS_BIT:VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT));
      NVNtextureView v=nr_view(1);mtl_reg_tex(71,(const tex_t *)&source,(const view_t *)&v);
      S=(vk_state){.program=(const prog_t *)&compute};S.tex[5][0]=71;S.samp[5][0]=(uint64_t)72<<20;
      S.ssbo[5][0]=(p_bind_buf){.addr=(uintptr_t)computed,.size=256};vk_pipeline_state_changed();vk_dispatch(&(p_dispatch){1,1,1});
      v=nr_view(2);p_copy_b2t copy={.tex=(const tex_t *)&source,.src=(uintptr_t)transferred,
        .view=*(const view_t *)&v,.has_view=1,.r={0,0,0,8,8,1}};vk_texture_copy_t2b(&copy,0,0);
    }
    vk_internal_wait(vk_internal_commit());
    CHECK(vk_test_native_image_owner(((tex_t *)&source)->serial)==(resident?2:0));
    if(order!=2)for(unsigned n=0;n<64;n++)CHECK(computed[n]==128);
    if(order!=1)for(unsigned n=0;n<256;n+=4)CHECK(transferred[n]==64 && !transferred[n+1] && !transferred[n+2] && transferred[n+3]==255);
    uint8_t rendered[1024];NVNcopyRegion region={0,0,0,16,16,1};CALL(nvnTextureReadTexels,&output,NULL,&region,rendered);
    for(unsigned n=0;n<sizeof rendered;n+=4)CHECK(rendered[n]==255 && !rendered[n+1] && !rendered[n+2] && rendered[n+3]==255);
  }
  if(resident) {
    vk_pool_span_info before_late;
    int late_unbacked=window_fixture && !previous_writer;
    extern int vk_test_pool_has_buffer_intersection(uint64_t,uint64_t);
    if(late_unbacked)CHECK(!vk_test_pool_has_buffer_intersection(address,needs.size));
    // Three actual queued chunks without a host wait between them. Their
    // source mip histories and the reused output image must stay valid while
    // earlier chunks remain in flight. CPU checks occur after the final fence.
    uint64_t last_chunk=0;
    for(unsigned mip=0;mip<3;mip++) {
      mtl_sub_begin();NVNtextureView view=nr_view(mip);
      mtl_reg_tex(71,(const tex_t *)&source,(const view_t *)&view);
      S=graphics_state;vk_pipeline_state_changed();
      vk_draw(&(p_draw){.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3},NULL);
      p_copy_b2t read={.tex=(const tex_t *)&output,
        .src=(uintptr_t)bytes+(3u<<20)+196608+mip*4096,.r={0,0,0,16,16,1}};
      vk_texture_copy_t2b(&read,0,0);last_chunk=vk_internal_commit();
      CHECK(vk_test_native_image_owner(((tex_t *)&source)->serial)==2);
    }
    vk_internal_wait(last_chunk);
    for(unsigned mip=0;mip<3;mip++) {
      const uint8_t *pixels=bytes+(3u<<20)+196608+mip*4096;
      for(unsigned n=0;n<1024;n+=4)CHECK(pixels[n]==red[mip] && !pixels[n+1] && !pixels[n+2] && pixels[n+3]==255);
    }
    // Four independent real submissions: mutate native bytes via the image,
    // sample, restore the opaque original via CPU, then sample again. Repeat
    // with explicit Flush, no-Flush coherent writes, and same-value publication.
    // The resident oracle uses NVN CPU_UNCACHED plus an actual coherent import,
    // so its no-Flush case is legal; execution synchronization is still needed.
    // This is not a promise that arbitrary racing/no-sync host writes are safe.
    for(unsigned round=0;round<4;round++) {
      mtl_sub_begin();NVNtextureView view=nr_view(0);
      uint8_t *linear=bytes+(3u<<20)+262144;
      for(unsigned n=0;n<4096;n+=4) {linear[n]=0;linear[n+1]=linear[n+3]=255;linear[n+2]=0;}
      CALL(nvnMemoryPoolFlushMappedRange,&pool,(3u<<20)+262144,4096);
      p_copy_b2t copy={.tex=(const tex_t *)&source,.src=(uintptr_t)linear,
        .view=*(const view_t *)&view,.has_view=1,.r={0,0,0,32,32,1}};
      vk_texture_copy_b2t(&copy,0,0);
      // The existing Buffer backing must not turn the Image producer into a
      // named-Buffer visibility proof. Read the complete real opaque payload
      // with compute in THIS recording, before any submit/fence/host read.
      uint8_t *image_to_buffer=bytes+(3u<<20)+327680;
      if(previous_writer) {
        S=(vk_state){.program=(const prog_t *)&copy_compute};
        S.ssbo[5][0]=(p_bind_buf){.addr=address,.size=needs.size};
        S.ssbo[5][1]=(p_bind_buf){.addr=(uintptr_t)image_to_buffer,.size=needs.size};
        vk_pipeline_state_changed();vk_dispatch(&(p_dispatch){1,1,1});
      }
      vk_internal_wait(vk_internal_commit());
      if(previous_writer)CHECK(!memcmp(image_to_buffer,(void *)(uintptr_t)address,needs.size));
      S=graphics_state;vk_pipeline_state_changed();
      uint8_t rendered[1024];nr_draw(&source,0,&output,rendered);
      for(unsigned n=0;n<sizeof rendered;n+=4)CHECK(!rendered[n] && rendered[n+1]==255 && !rendered[n+2] && rendered[n+3]==255);
      CHECK(vk_test_native_image_owner(((tex_t *)&source)->serial)==2);
      // nr_draw waited its fence. This is a real CPU write of saved original
      // imported payload, including all three mip levels and padding.
      dm_guest_write_prepare((void *)(uintptr_t)address,needs.size);
      memcpy((void *)(uintptr_t)address,original_payload,needs.size);
      if(round!=1)CALL(nvnMemoryPoolFlushMappedRange,&pool,address-(uintptr_t)bytes,needs.size);
      if(round==2)CALL(nvnMemoryPoolFlushMappedRange,&pool,address-(uintptr_t)bytes,needs.size);
      S=graphics_state;vk_pipeline_state_changed();nr_draw(&source,0,&output,rendered);
      for(unsigned n=0;n<sizeof rendered;n+=4)CHECK(rendered[n]==255 && !rendered[n+1] && !rendered[n+2] && rendered[n+3]==255);
      CHECK(vk_test_native_image_owner(((tex_t *)&source)->serial)==2);
      // Late Buffer materialization reads the same native physical bytes;
      // the image stays resident and real alias histories must still order it.
      if(late_unbacked && !round)CHECK(!vk_test_pool_has_buffer_intersection(address,needs.size));
      mtl_sub_begin();void *raw_pointer;uint64_t raw_offset;
      vk_pool_requirement late_range={address,needs.size,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0};
      int late_operation=vk_pool_operation_begin_ranges(&late_range,1);
      CHECK(vk_pool_address_stage(address,needs.size,&raw_pointer,&raw_offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
      vk_transfer_buffer_dependencies((vk_buffer_use[]){{address,needs.size,0}},1);
      vk_buffer *readback=vk_temp_buffer(needs.size);
      VkBufferCopy read={.srcOffset=raw_offset,.size=needs.size};
      vkCmdCopyBuffer(V.cmd,((vk_buffer *)raw_pointer)->handle,readback->handle,1,&read);
      VkBufferMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .buffer=readback->handle,.size=needs.size};
      vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&host,0,NULL);
      uint8_t *copied=malloc(needs.size);CHECK(copied);
      native_lane_readback publication={copied,readback->map,needs.size};
      vk_completion_write(native_lane_readback_done,&publication,copied,needs.size);
      vk_pool_operation_end(late_operation);
      vk_internal_wait(vk_internal_commit());CHECK(!memcmp(copied,original_payload,needs.size));free(copied);
      CHECK(vk_test_native_image_owner(((tex_t *)&source)->serial)==2);
      if(late_unbacked)CHECK(vk_pool_span_query(address,needs.size,&before_late)==VK_POOL_SPAN_SINGLE);
    }
    printf("{\"test\":\"native-resident-owner-host-oracle\",\"result\":\"passed\",\"actual_imported_coherent_RGBA\":true,\"provider_override_test_only\":true,\"ASTC_executed\":false,\"CPU_restore_rounds\":4,\"explicit_and_noFlush\":true,\"same_value_flush\":true,\"Buffer_GPU_readback\":true,\"three_chunks_without_explicit_host_wait\":true,\"first_Buffer_materialized_late\":%s,\"previous_writers\":%s,\"Image_writer_to_Buffer_CS_full_payload\":%s}\n",late_unbacked?"true":"false",previous_writer?"true":"false",previous_writer?"true":"false");
  }
  free(original_payload);
  CALL(nvnTextureFinalize,&source);CALL(nvnTextureFinalize,&output);
  CALL(nvnProgramFinalize,&graphics);CALL(nvnProgramFinalize,&compute);CALL(nvnProgramFinalize,&primer);
  CALL(nvnProgramFinalize,&copy_compute);CALL(nvnSamplerFinalize,&sampler);
  CALL(nvnMemoryPoolFinalize,&pool);CHECK(!vk_validation_error_count());
  vk_test_native_resident_provider(previous_provider);
  printf("{\"test\":\"native-acquire-cross-mip-directed-scopes\",\"checks\":%u,\"result\":\"passed\",\"FS0_then_CS1_and_TRANSFER2\":true,\"CS1_then_FS0\":true,\"TRANSFER2_then_FS0\":true,\"previous_TRANSFER_COMPUTE_pixels_and_unknown_negative\":%s,\"actual_imported_RGBA\":true,\"ASTC_executed\":false}\n",checks,previous_writer?"true":"false");
}
