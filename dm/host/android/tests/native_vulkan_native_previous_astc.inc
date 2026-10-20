// Test-only extension of the ORIGINAL native-family oracle. Same authenticated
// real ASTC payload/table, reference image and shaders; no new file protocol.
static void nr_previous_writer_astc(NVNtexture *native,NVNtexture *reference,NVNtexture *output,
    NVNmemoryPool *pool,uint8_t *storage,const nr_input *input,const uint8_t *linear,
    uint64_t native_offset,uint64_t payload_size,uint64_t linear_offset,uint64_t vertex_offset,
    uint64_t scratch,const vk_state *graphics) {
  vk_state graphics_snapshot=*graphics;graphics=&graphics_snapshot;
  unsigned begin=checks;
  CHECK(input->levels==9 && input->width==256 && input->height==256);
  CHECK(V.options.pool_window_bytes==65536 && V.features.textureCompressionASTC_LDR);
  uint8_t controls[4][0x880];const unsigned markers[]={0,1,23,26};NVNprogram primer,compute,copy_compute;
  for(unsigned n=0;n<4;n++)memset(controls[n],markers[n],sizeof controls[n]);
  NVNshaderData stages[]={{.control=controls[0]},{.control=controls[1]}};
  CHECK(CALL(nvnProgramInitialize,&primer,&device));CHECK(CALL(nvnProgramSetShaders,&primer,2,stages));
  stages[0].control=controls[2];CHECK(CALL(nvnProgramInitialize,&compute,&device));
  CHECK(CALL(nvnProgramSetShaders,&compute,1,stages));
  stages[0].control=controls[3];CHECK(CALL(nvnProgramInitialize,&copy_compute,&device));
  CHECK(CALL(nvnProgramSetShaders,&copy_compute,1,stages));
  uint8_t expected_pixels[1024],actual_pixels[1024];S=*graphics;nr_draw(reference,4,output,expected_pixels);
  uint32_t expected_compute[64];uint32_t *computed=(void *)(storage+scratch);
  uint8_t *compressed=storage+scratch+4096;
  NVNtextureView compute_view=nr_view(5);mtl_reg_tex(71,(const tex_t *)reference,(const view_t *)&compute_view);
  mtl_sub_begin();S=(vk_state){.program=(const prog_t *)&compute};S.tex[5][0]=71;S.samp[5][0]=(uint64_t)72<<20;
  S.ssbo[5][0]=(p_bind_buf){.addr=(uintptr_t)computed,.size=sizeof expected_compute};
  vk_pipeline_state_changed();vk_dispatch(&(p_dispatch){1,1,1});vk_internal_wait(vk_internal_commit());
  memcpy(expected_compute,computed,sizeof expected_compute);
  float *black=(void *)(storage+vertex_offset+32768);
  dm_guest_write_prepare(black,16);black[0]=black[1]=black[2]=0;black[3]=1;
  CALL(nvnMemoryPoolFlushMappedRange,pool,vertex_offset+32768,16);
  uint64_t address=(uintptr_t)storage+native_offset,serial=((const tex_t *)native)->serial;
  extern VkPipelineStageFlags vk_test_native_writer_stages(uint64_t,uint64_t);
  extern VkPipelineStageFlags vk_test_native_unused_mip_source(uint64_t,unsigned);
  for(unsigned mode=0;mode<3;mode++) {
    mtl_sub_begin();
    // Clear only unrelated reference/readback setup BEFORE the real producer.
    // The candidate must keep and synchronize the writer created below.
    vk_barrier();
    VkPipelineStageFlags producer=mode?VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT:VK_PIPELINE_STAGE_TRANSFER_BIT;
    if(!mode) {
      NVNtextureView v=nr_view(4);const nr_input_mip *m=&input->mips[4];
      p_copy_b2t copy={.tex=(const tex_t *)native,.src=(uintptr_t)storage+linear_offset+m->offset,
        .view=*(const view_t *)&v,.has_view=1,.r={0,0,0,m->width,m->height,1}};
      vk_texture_copy_b2t(&copy,0,0);
    } else {
      // Original compute26 copies the same native bytes in-place. It does not
      // guess any vendor ASTC layout or allocate another native payload.
      S=(vk_state){.program=(const prog_t *)&copy_compute};
      S.ssbo[5][0]=(p_bind_buf){.addr=address,.size=payload_size};
      S.ssbo[5][1]=(p_bind_buf){.addr=address,.size=payload_size};
      vk_pipeline_state_changed();vk_dispatch(&(p_dispatch){1,1,1});
      if(mode==2)vk_buffer_shader_written(address,payload_size,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    }
    CHECK(!vk_pool_native_acquire_independent(address,payload_size));
    vk_internal_wait(vk_internal_commit());
    CHECK(vk_test_native_writer_stages(address,payload_size)&producer);
    mtl_sub_begin();S=*graphics;S.program=(const prog_t *)&primer;
    S.ubo[1][0]=(p_bind_buf){.addr=(uintptr_t)black,.size=16};S.targets=(p_set_rt){.n=1,.color={(const tex_t *)output}};
    vk_pipeline_state_changed();vk_draw(&(p_draw){.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3},NULL);
    CHECK(vk_render_active());
    CHECK(vk_test_native_writer_stages(address,payload_size)&producer);
    CHECK(vk_pool_native_acquire_independent(address,payload_size)==(mode!=2));
    NVNtextureView first=nr_view(4);mtl_reg_tex(71,(const tex_t *)native,(const view_t *)&first);
    S=*graphics;S.targets=(p_set_rt){.n=1,.color={(const tex_t *)output}};
    vk_pipeline_state_changed();vk_draw(&(p_draw){.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3},NULL);
    CHECK(vk_test_native_unused_mip_source(serial,5)==
      (mode==2?VK_PIPELINE_STAGE_ALL_COMMANDS_BIT:VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT));
    compute_view=nr_view(5);mtl_reg_tex(71,(const tex_t *)native,(const view_t *)&compute_view);
    S=(vk_state){.program=(const prog_t *)&compute};S.tex[5][0]=71;S.samp[5][0]=(uint64_t)72<<20;
    S.ssbo[5][0]=(p_bind_buf){.addr=(uintptr_t)computed,.size=sizeof expected_compute};
    vk_pipeline_state_changed();vk_dispatch(&(p_dispatch){1,1,1});
    const nr_input_mip *m=&input->mips[6];NVNtextureView transfer_view=nr_view(6);
    p_copy_b2t transfer={.tex=(const tex_t *)native,.src=(uintptr_t)compressed,
      .view=*(const view_t *)&transfer_view,.has_view=1,.r={0,0,0,m->width,m->height,1}};
    vk_texture_copy_t2b(&transfer,0,0);vk_internal_wait(vk_internal_commit());
    CHECK(!memcmp(expected_compute,computed,sizeof expected_compute));
    CHECK(!memcmp(compressed,linear+m->offset,m->size));
    NVNcopyRegion rectangle={0,0,0,16,16,1};CALL(nvnTextureReadTexels,output,NULL,&rectangle,actual_pixels);
    CHECK(!memcmp(expected_pixels,actual_pixels,sizeof actual_pixels));
  }
  S=*graphics;vk_pipeline_state_changed();
  CALL(nvnProgramFinalize,&primer);CALL(nvnProgramFinalize,&compute);CALL(nvnProgramFinalize,&copy_compute);
  unsigned delta=checks-begin;CHECK(delta==35);
  printf("{\"test\":\"native-previous-writer-ASTC-pending-FS\",\"checks\":%u,\"positive_cases\":2,\"unknown_negative_cases\":1,\"mips\":[4,5,6],\"old_TRANSFER_and_COMPUTE_histories_retained\":true,\"current_recording_denied\":true,\"FS_pixels_reference_equal\":true,\"CS_pixels_reference_equal\":true,\"T2B_compressed_blocks_exact\":true,\"result\":\"passed\"}\n",checks-begin);
}
