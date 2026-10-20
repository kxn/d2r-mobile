// Extension of the accepted original f830 ASTC oracle. All comparisons still
// use the independently uploaded compact reference, not a new layout decoder.
static unsigned nr_resident_astc(NVNtexture *native,NVNtexture *reference,NVNtexture *output,
    NVNmemoryPool *pool,uint8_t *storage,const nr_input *input,uint8_t *linear,
    uint64_t native_offset,uint64_t payload_size,uint64_t linear_offset,uint64_t scratch,
    const vk_state *graphics) {
  vk_state state=*graphics;unsigned begin=checks;
  extern int vk_test_native_image_owner(uint64_t);
  extern VkPipelineStageFlags vk_test_native_mip_scope(uint64_t,unsigned,uint64_t *);
  uint64_t serial=((const tex_t *)native)->serial;
  uint8_t copy_control[0x880];memset(copy_control,26,sizeof copy_control);
  NVNprogram copy_compute;NVNshaderData copy_stage={.control=copy_control};
  CHECK(CALL(nvnProgramInitialize,&copy_compute,&device));
  CHECK(CALL(nvnProgramSetShaders,&copy_compute,1,&copy_stage));
  CHECK(input->levels==9 && V.options.pool_window_bytes==65536);
  CHECK((((pool_t *)pool)->flags&7)==NVN_MEMORY_POOL_FLAGS_CPU_UNCACHED);
  CHECK(vk_test_native_image_owner(serial)==2);
  const nr_input_mip *changed=&input->mips[4];
  CHECK(changed->size>=32 && memcmp(linear+changed->offset,linear+changed->offset+16,16));
  uint8_t *saved=malloc(payload_size);CHECK(saved);
  // Original synchronous helpers have completed and published all GPU writes.
  // Snapshot real opaque physical bytes, including all nine mips/padding.
  CALL(nvnMemoryPoolInvalidateMappedRange,pool,native_offset,payload_size);
  memcpy(saved,storage+native_offset,payload_size);
  uint8_t original[1024],expected[1024],actual[1024];
  S=state;nr_draw(reference,4,output,original);
  uint64_t clock=0;VkPipelineStageFlags prior_scope=vk_test_native_mip_scope(serial,5,&clock);
  uint64_t last=0;
  // No explicit CPU wait between these actual primary command submissions.
  // Original T2B bookkeeping publishes each distinct result before the final
  // completion wait; no new fence, sampler or timing implementation is used.
  for(unsigned chunk=0;chunk<3;chunk++) {
    mtl_sub_begin();NVNtextureView v=nr_view(4);
    mtl_reg_tex(71,(const tex_t *)native,(const view_t *)&v);
    S=state;vk_pipeline_state_changed();
    vk_draw(&(p_draw){.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3},NULL);
    p_copy_b2t read={.tex=(const tex_t *)output,
      .src=(uintptr_t)storage+scratch+8192+chunk*4096,.r={0,0,0,16,16,1}};
    vk_texture_copy_t2b(&read,0,0);last=vk_internal_commit();
    CHECK(vk_test_native_image_owner(serial)==2);
  }
  vk_internal_wait(last);
  for(unsigned chunk=0;chunk<3;chunk++)
    CHECK(!memcmp(storage+scratch+8192+chunk*4096,original,sizeof original));
  for(unsigned round=0;round<3;round++) {
    uint8_t block[16];memcpy(block,linear+changed->offset,16);
    memcpy(linear+changed->offset,linear+changed->offset+16,16);
    memcpy(linear+changed->offset+16,block,16);
    dm_guest_write_prepare(storage+linear_offset+changed->offset,changed->size);
    memcpy(storage+linear_offset+changed->offset,linear+changed->offset,changed->size);
    CALL(nvnMemoryPoolFlushMappedRange,pool,linear_offset+changed->offset,changed->size);
    mtl_sub_begin();NVNtextureView v=nr_view(4);
    p_copy_b2t update={.tex=(const tex_t *)reference,
      .src=(uintptr_t)storage+linear_offset+changed->offset,.view=*(const view_t *)&v,
      .has_view=1,.r={0,0,0,changed->width,changed->height,1}};
    vk_texture_copy_b2t(&update,0,0);update.tex=(const tex_t *)native;
    vk_texture_copy_b2t(&update,0,0);
    // A distinct VkImage just wrote this native payload. The already-existing
    // Pool VkBuffer must use global alias visibility, not a named Buffer proof.
    // The real compute reads the entire opaque payload in the SAME recording:
    // no intermediate submit, wait, host invalidation or T2B consumer.
    S=(vk_state){.program=(const prog_t *)&copy_compute};
    S.ssbo[5][0]=(p_bind_buf){.addr=(uintptr_t)storage+native_offset,.size=payload_size};
    S.ssbo[5][1]=(p_bind_buf){.addr=(uintptr_t)storage+scratch,.size=payload_size};
    vk_pipeline_state_changed();vk_dispatch(&(p_dispatch){1,1,1});
    vk_internal_wait(vk_internal_commit());
    CALL(nvnMemoryPoolInvalidateMappedRange,pool,native_offset,payload_size);
    CHECK(!memcmp(storage+scratch,storage+native_offset,payload_size));
    S=state;nr_draw(reference,4,output,expected);
    S=state;nr_draw(native,4,output,actual);
    CHECK(memcmp(original,expected,sizeof expected));
    CHECK(!memcmp(expected,actual,sizeof actual));
    CHECK(vk_test_native_image_owner(serial)==2);
    // Every CPU producer occurs after those original GPU completion waits.
    // CPU_UNCACHED plus the actual coherent import makes round1 no-Flush legal.
    dm_guest_write_prepare(storage+native_offset,payload_size);
    memcpy(storage+native_offset,saved,payload_size);
    if(round!=1)CALL(nvnMemoryPoolFlushMappedRange,pool,native_offset,payload_size);
    if(round==2)CALL(nvnMemoryPoolFlushMappedRange,pool,native_offset,payload_size);
    memcpy(block,linear+changed->offset,16);
    memcpy(linear+changed->offset,linear+changed->offset+16,16);
    memcpy(linear+changed->offset+16,block,16);
    dm_guest_write_prepare(storage+linear_offset+changed->offset,changed->size);
    memcpy(storage+linear_offset+changed->offset,linear+changed->offset,changed->size);
    CALL(nvnMemoryPoolFlushMappedRange,pool,linear_offset+changed->offset,changed->size);
    mtl_sub_begin();update.tex=(const tex_t *)reference;
    vk_texture_copy_b2t(&update,0,0);vk_internal_wait(vk_internal_commit());
    S=state;nr_blocks(native,input,linear);
    S=state;nr_samples(native,reference,output,input->levels);
    uint64_t current_clock=0;
    CHECK(vk_test_native_image_owner(serial)==2 &&
      vk_test_native_mip_scope(serial,5,&current_clock)==prior_scope && current_clock>=clock);
    clock=current_clock;
  }
  CALL(nvnProgramFinalize,&copy_compute);free(saved);S=state;vk_pipeline_state_changed();
  unsigned delta=checks-begin;CHECK(delta==109);
  printf("{\"test\":\"native-resident-owner-original-ASTC\",\"result\":\"passed\",\"checks_added\":%u,\"actual_ASTC_GPU\":true,\"mips\":9,\"CPU_UNCACHED\":true,\"rounds\":3,\"GPU_changed_FS_reference_equal\":true,\"CPU_restore_all_mips_exact\":true,\"explicit_and_noFlush\":true,\"same_value_Flush\":true,\"three_chunks_without_explicit_host_wait\":true,\"owner_and_mip_history_retained\":true,\"Image_writer_to_Buffer_CS_full_payload\":true,\"game_modules_executed\":false}\n",checks-begin);
  return checks-begin;
}
