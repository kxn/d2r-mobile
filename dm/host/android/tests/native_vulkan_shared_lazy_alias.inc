// Actual backend GPU byte/lifetime oracle for the startup-qualified coherent
// lazy-import policy. No time measurements, game logic or performance counts.
#if defined(DM_VULKAN_SHARED_IMPORT_WINDOWS) && defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
static VkBuffer lazy_alias_old,lazy_alias_cover;
static _Atomic unsigned lazy_alias_old_destroyed,lazy_alias_cover_destroyed;
static PFN_vkDestroyBuffer lazy_alias_destroy_original;
static PFN_vkCmdPipelineBarrier lazy_alias_barrier_original;
static unsigned lazy_alias_named,lazy_alias_global_transfer_fs,lazy_alias_global_host,lazy_alias_global_transfer_vertex;
static void VKAPI_PTR lazy_alias_destroy(VkDevice device,VkBuffer buffer,const VkAllocationCallbacks *allocation) {
  if(buffer==lazy_alias_old)atomic_fetch_add(&lazy_alias_old_destroyed,1);
  if(buffer==lazy_alias_cover)atomic_fetch_add(&lazy_alias_cover_destroyed,1);
  lazy_alias_destroy_original(device,buffer,allocation);
}
static void VKAPI_PTR lazy_alias_barrier(VkCommandBuffer command,VkPipelineStageFlags source,
    VkPipelineStageFlags destination,VkDependencyFlags flags,uint32_t memory_count,
    const VkMemoryBarrier *memory,uint32_t buffer_count,const VkBufferMemoryBarrier *buffers,
    uint32_t image_count,const VkImageMemoryBarrier *images) {
  for(unsigned n=0;n<buffer_count;n++)
    if(buffers[n].buffer==lazy_alias_old || buffers[n].buffer==lazy_alias_cover)lazy_alias_named++;
  for(unsigned n=0;n<memory_count;n++) {
    if(source==VK_PIPELINE_STAGE_TRANSFER_BIT && destination==VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT &&
        (memory[n].srcAccessMask&VK_ACCESS_TRANSFER_WRITE_BIT) &&
        (memory[n].dstAccessMask&VK_ACCESS_SHADER_READ_BIT))lazy_alias_global_transfer_fs++;
    if(source==VK_PIPELINE_STAGE_TRANSFER_BIT &&
        destination==(VK_PIPELINE_STAGE_VERTEX_INPUT_BIT|VK_PIPELINE_STAGE_VERTEX_SHADER_BIT) &&
        (memory[n].srcAccessMask&VK_ACCESS_TRANSFER_WRITE_BIT) &&
        (memory[n].dstAccessMask&VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT))lazy_alias_global_transfer_vertex++;
    if((destination&VK_PIPELINE_STAGE_HOST_BIT) &&
        (memory[n].srcAccessMask&(VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_MEMORY_WRITE_BIT)) &&
        (memory[n].dstAccessMask&VK_ACCESS_HOST_READ_BIT))lazy_alias_global_host++;
  }
  lazy_alias_barrier_original(command,source,destination,flags,memory_count,memory,
      buffer_count,buffers,image_count,images);
}
static vk_buffer lazy_alias_resolve(uint8_t *address,uint64_t size,uint64_t *offset) {
  vk_buffer *buffer=NULL;
  CHECK(vk_pool_address_stage((uintptr_t)address,size,(void **)&buffer,offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
  CHECK(buffer && buffer->handle && buffer->shared_import &&
      (buffer->properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
  CHECK(*offset<=buffer->size && size<=buffer->size-*offset);
  return *buffer; // retain the actual physical handle, never re-resolve it at use
}
static void lazy_alias_fill(vk_buffer buffer,uint64_t offset,uint8_t *address,unsigned size,uint32_t word) {
  vk_buffer_use use={(uintptr_t)address,size,1};vk_transfer_buffer_dependencies(&use,1);
  vkCmdFillBuffer(V.cmd,buffer.handle,offset,size,word);
  vk_pool_overwritten((uintptr_t)address,size);
  // Intentionally keep the real borrowed physical handle even when the
  // canonical resolver now selects a different covering import.
  vk_buffer_transfer_written_range((uintptr_t)address,size);
  vk_buffer_writeback_stage((uintptr_t)address,size,VK_PIPELINE_STAGE_TRANSFER_BIT);
}
static void lazy_alias_read(vk_buffer buffer,uint64_t offset,uint8_t *address,unsigned size,uint8_t *result) {
  vk_buffer *readback=vk_temp_buffer(size);vk_buffer_use use={(uintptr_t)address,size,0};
  vk_transfer_buffer_dependencies(&use,1);
  VkBufferCopy copy={.srcOffset=offset,.size=size};
  vkCmdCopyBuffer(V.cmd,buffer.handle,readback->handle,1,&copy);
  VkBufferMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,
    .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
    .buffer=readback->handle,.offset=0,.size=size};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,
      0,0,NULL,1,&host,0,NULL);vk_effect_copy(result,readback->map,size);
}
static void lazy_alias_words(const uint8_t *memory,unsigned size,uint32_t value) {
  for(unsigned n=0;n<size;n+=4) {uint32_t actual;memcpy(&actual,memory+n,4);CHECK(actual==value);}
}
static unsigned lazy_alias_collect_calls;
static int lazy_alias_collect(void *context,vk_pool_requirement_sink sink,void *out) {
  lazy_alias_collect_calls++;return sink(out,context);
}
static PFN_vkCmdDrawIndexed lazy_empty_draw_original;
static unsigned lazy_empty_draws;
static void VKAPI_PTR lazy_empty_draw(VkCommandBuffer command,uint32_t count,uint32_t instances,
    uint32_t first,int32_t base,uint32_t base_instance) {
  CHECK(!count && instances==1);lazy_empty_draws++;
  lazy_empty_draw_original(command,count,instances,first,base,base_instance);
}
static void shared_lazy_empty_binding_test(void) {
  enum { W=65536 };
  vk_state saved=S;NVNmemoryPoolBuilder pb;NVNmemoryPool owned;
  CALL(nvnMemoryPoolBuilderSetDefaults,&pb);CALL(nvnMemoryPoolBuilderSetDevice,&pb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&pb,bytes,4u<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&pb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&owned,&pb));
  NVNtexture target;NVNtextureBuilder tb;CALL(nvnTextureBuilderSetDefaults,&tb);
  CALL(nvnTextureBuilderSetDevice,&tb,&device);CALL(nvnTextureBuilderSetWidth,&tb,4);
  CALL(nvnTextureBuilderSetHeight,&tb,4);CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);
  CALL(nvnTextureBuilderSetStorage,&tb,&owned,0);CHECK(CALL(nvnTextureInitialize,&target,&tb));
  uint8_t controls[3][0x880];const unsigned markers[]={12,24,1};NVNprogram programs[2];
  for(unsigned n=0;n<3;n++)memset(controls[n],markers[n],sizeof controls[n]);
  for(unsigned n=0;n<2;n++) {
    NVNshaderData code[]={{.control=controls[n]},{.control=controls[2]}};
    CHECK(CALL(nvnProgramInitialize,&programs[n],&device));
    CHECK(CALL(nvnProgramSetShaders,&programs[n],2,code));
  }
  dm_guest_write_prepare(bytes+2*W,16);memcpy(bytes+2*W,(float[]){1,0,0,1},16);
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState depth;CALL(nvnDepthStencilStateSetDefaults,&depth);
  CALL(nvnDepthStencilStateSetDepthTestEnable,&depth,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&depth,0);
  CALL(nvnDepthStencilStateSetStencilTestEnable,&depth,0);NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);
  NVNvertexAttribState attributes[2];for(unsigned n=0;n<2;n++) {
    CALL(nvnVertexAttribStateSetDefaults,&attributes[n]);CALL(nvnVertexAttribStateSetStreamIndex,&attributes[n],0);
    CALL(nvnVertexAttribStateSetFormat,&attributes[n],n?NVN_FORMAT_RG32F:NVN_FORMAT_RGB32F,n?12:0);
  }
  NVNvertexStreamState stream;CALL(nvnVertexStreamStateSetDefaults,&stream);CALL(nvnVertexStreamStateSetStride,&stream,20);
  mtl_sub_begin();S=(vk_state){.depth_range={0,1},.chmask={~0u}};
  S.targets=(p_set_rt){.n=1,.color={(const tex_t *)&target}};
  S.poly=*(const poly_st *)&polygon;S.ds=*(const ds_st *)&depth;S.color=*(const color_st *)&color;
  for(unsigned n=0;n<2;n++)S.attr[n]=*(const va_st *)&attributes[n];
  S.stream[0]=*(const vs_st *)&stream;S.vertex[0]=(NVNbufferRange){(uintptr_t)bytes+W,80};
  S.ubo[1][0]=(p_bind_buf){.addr=(uintptr_t)bytes+2*W,.size=16};
  // These are real reflected VS UBO/SSBO consumers with a valid nonzero
  // address and zero size, not unknown-extent vertex streams.
  S.ubo[0][0]=(p_bind_buf){.addr=(uintptr_t)bytes+3*W,.size=0};
  S.ssbo[0][0]=(p_bind_buf){.addr=(uintptr_t)bytes+4*W,.size=0};
  S.has_viewport=S.has_scissor=1;S.viewport[0][2]=S.viewport[0][3]=4;S.scissor=(p_scissor){0,0,4,4};
  p_clear_tex clear={.tex=(const tex_t *)&target,.r={0,0,0,4,4,1},.mask=15,.c.f={.25f,.5f,.75f,1}};
  vk_texture_clear(&clear);
  vk_pool_span_info span;lazy_empty_draw_original=vkCmdDrawIndexed;vkCmdDrawIndexed=lazy_empty_draw;
  for(unsigned n=0;n<2;n++) {
    S.program=(const prog_t *)&programs[n];vk_pipeline_state_changed();
    CHECK(!vk_pool_shared_materialized((uintptr_t)bytes+(5+n)*W,1));
    p_draw_idx empty={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,
      .itype=n?NVN_INDEX_TYPE_UNSIGNED_INT:NVN_INDEX_TYPE_UNSIGNED_SHORT,
      .count=0,.instances=1,.ibuf=(uintptr_t)bytes+(5+n)*W};
#ifdef DM_VULKAN_VERTEX_REQUIRE_WRAP
    vertex_req_observed_program=(const prog_t *)&programs[n];vertex_req_calls=0;
#endif
    vk_draw(NULL,&empty);
#ifdef DM_VULKAN_VERTEX_REQUIRE_WRAP
    vertex_req_observed_program=NULL;
    CHECK(vertex_req_calls==(n?0:1));
    if(!n)CHECK(vertex_req_addresses[0]==S.vertex[0].address && vertex_req_sizes[0]==80);
#endif
    CHECK(lazy_empty_draws==n+1);
    CHECK(vk_pool_span_query(empty.ibuf,1,&span)==VK_POOL_SPAN_SINGLE);
    CHECK(span.logical_begin==(5+n)*W && span.logical_end==(6+n)*W);
    CHECK(!vk_pool_shared_materialized((uintptr_t)bytes+3*W,1));
    CHECK(!vk_pool_shared_materialized((uintptr_t)bytes+4*W,1));
    CHECK(!vk_pool_shared_materialized((uintptr_t)bytes+7*W,1));
  }
  vkCmdDrawIndexed=lazy_empty_draw_original;vk_internal_wait(vk_internal_commit());
  uint8_t pixels[64];NVNcopyRegion region={0,0,0,4,4,1};CALL(nvnTextureReadTexels,&target,NULL,&region,pixels);
  for(unsigned n=0;n<16;n++)CHECK(!memcmp(pixels+n*4,(uint8_t[]){64,128,191,255},4));
  S=saved;vk_pipeline_state_changed();for(unsigned n=0;n<2;n++)CALL(nvnProgramFinalize,&programs[n]);
  CALL(nvnTextureFinalize,&target);CALL(nvnMemoryPoolFinalize,&owned);
  CHECK(!vk_validation_error_count());
}

static void shared_lazy_alias_gpu_test(void) {
  enum {W=65536,SIZE=64};
#ifdef DM_VULKAN_NATIVE_RESOURCES
  extern unsigned vk_test_shared_buffer_coverage_index(void);
  checks+=vk_test_shared_buffer_coverage_index();
  extern unsigned vk_test_shared_lazy_physical_index(void);
  checks+=vk_test_shared_lazy_physical_index();
#endif
  CHECK(vk_pool_shared_mode() && V.options.pool_window_bytes==W);
  CHECK(getenv("DM_SHARED_GUEST_HEAP_FIXTURE") && getenv("DM_VK_SHARED_LAZY_WINDOWS") &&
      !strcmp(getenv("DM_VK_SHARED_LAZY_WINDOWS"),"1"));
  NVNmemoryPoolBuilder builder;NVNmemoryPool owned;
  CALL(nvnMemoryPoolBuilderSetDefaults,&builder);CALL(nvnMemoryPoolBuilderSetDevice,&builder,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&builder,bytes,4u<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&builder,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&owned,&builder));
  // No physical ASTC is requested on the host. Real NVN Builder descriptors
  // and Pool provenance exercise the actual T2T collector/observation path.
  // This Pool has never had a Buffer, not merely a hole beside another span.
  NVNtexture cold_source,cold_destination;NVNtextureBuilder texture_builder;
  for(unsigned n=0;n<2;n++) {
    CALL(nvnTextureBuilderSetDefaults,&texture_builder);
    CALL(nvnTextureBuilderSetDevice,&texture_builder,&device);
    CALL(nvnTextureBuilderSetWidth,&texture_builder,16);CALL(nvnTextureBuilderSetHeight,&texture_builder,16);
    CALL(nvnTextureBuilderSetFormat,&texture_builder,n?NVN_FORMAT_RGBA_DXT5:NVN_FORMAT_RGBA_ASTC_4x4);
    CALL(nvnTextureBuilderSetStorage,&texture_builder,&owned,n?W:0);
    CHECK(CALL(nvnTextureInitialize,n?&cold_destination:&cold_source,&texture_builder));
  }
  p_copy_t2t cold={.src=(const tex_t *)&cold_source,.dst=(const tex_t *)&cold_destination,
    .sr={0,0,0,16,16,1},.dr={0,0,0,16,16,1}};
  extern void vk_test_astc_copy_cold_admission(const p_copy_t2t *);
  vk_test_astc_copy_cold_admission(&cold);
  CALL(nvnTextureFinalize,&cold_source);CALL(nvnTextureFinalize,&cold_destination);
  CALL(nvnMemoryPoolFinalize,&owned);
  CHECK(CALL(nvnMemoryPoolInitialize,&owned,&builder));
  // Actual copy/dispatch admission must establish full backing BEFORE an
  // observation; draw alone is allowed to leave the same untouched range cold.
  vk_pool_requirement need={(uintptr_t)bytes+5*W-64,192,0,VK_PIPELINE_STAGE_TRANSFER_BIT,1,0};
  vk_pool_span_info prerequisite;
  CHECK(vk_pool_span_query(need.address,need.size,&prerequisite)==VK_POOL_SPAN_UNBACKED);
  int draw_only=vk_pool_draw_operation_begin(lazy_alias_collect,&need);
  CHECK(!lazy_alias_collect_calls);
  CHECK(vk_pool_span_query(need.address,need.size,&prerequisite)==VK_POOL_SPAN_UNBACKED);
  vk_pool_operation_end(draw_only);
  // A later malformed range must reject the WHOLE batch without importing
  // the earlier valid range. Also retain native-kind/prefix validation.
  vk_pool_requirement invalid[2]={need,need};invalid[1].prefix_alignment=3;
  vk_pool_reserve_result rejected;
  CHECK(vk_pool_reserve_ranges(invalid,2,W,&rejected)==VK_POOL_RESERVE_INVALID);
  CHECK(vk_pool_span_query(need.address,need.size,&prerequisite)==VK_POOL_SPAN_UNBACKED);
#ifdef DM_VULKAN_NATIVE_RESOURCES
  invalid[1].prefix_alignment=W;invalid[1].binding_kind=VK_POOL_REQUIRE_NATIVE_IMAGE;
  CHECK(vk_pool_reserve_ranges(invalid,2,W,&rejected)==VK_POOL_RESERVE_INVALID);
  CHECK(vk_pool_span_query(need.address,need.size,&prerequisite)==VK_POOL_SPAN_UNBACKED);
#endif
  int copy_ready=vk_pool_operation_begin(lazy_alias_collect,&need);
  CHECK(lazy_alias_collect_calls==1);
  CHECK(vk_pool_span_query(need.address,need.size,&prerequisite)==VK_POOL_SPAN_SINGLE);
  CHECK(vk_pool_shared_materialized(need.address,need.size));
#ifdef DM_VULKAN_NATIVE_RESOURCES
  extern int vk_test_native_import_coverage(uint64_t,uint64_t);
  CHECK(vk_test_native_import_coverage((uintptr_t)bytes+4*W,2*W));
  CHECK(!vk_test_native_import_coverage((uintptr_t)bytes+4*W-1,2));
#endif
  CHECK(vk_pool_shared_cpu_observe_prepare(need.address,need.size));
  nvn_dirty_scope *observed_scope=nvn_dirty_scope_acquire((void *)(uintptr_t)need.address,need.size);
  CHECK(observed_scope);vk_shared_cpu_observation observation;
  CHECK(vk_pool_shared_cpu_observe_begin(need.address,need.size,&observation));
  CHECK(observation.stamp);vk_pool_shared_cpu_observe_end(&observation);
  nvn_dirty_scope_release(observed_scope);vk_pool_operation_end(copy_ready);
  uint8_t *address=bytes+W+128;uint64_t old_offset,cover_offset,cross_offset;
  uint8_t forward[SIZE],reverse[SIZE],nested[SIZE],later[SIZE];
  // Original timeline gate pattern: keep a real earlier submit pending while
  // the owner imports a covering alias. No sleep or target timing is involved.
  int pending_gate=vk_async_submissions();VkSemaphore gate=VK_NULL_HANDLE;
  if(pending_gate) {
    CHECK(V.timeline_semaphore && V.signal_timeline);
    VkSemaphoreTypeCreateInfoKHR type={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR,
      .semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE_KHR};
    VkSemaphoreCreateInfo create={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,.pNext=&type};
    VK_CHECK(vkCreateSemaphore(V.device,&create,NULL,&gate));uint64_t gate_value=1;
    VkTimelineSemaphoreSubmitInfoKHR timeline={.sType=VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR,
      .waitSemaphoreValueCount=1,.pWaitSemaphoreValues=&gate_value};
    VkPipelineStageFlags gate_stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo blocked={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.pNext=&timeline,
      .waitSemaphoreCount=1,.pWaitSemaphores=&gate,.pWaitDstStageMask=&gate_stage};
    pthread_mutex_lock(&V.queue_mu);VK_CHECK(vkQueueSubmit(V.queue,1,&blocked,VK_NULL_HANDLE));pthread_mutex_unlock(&V.queue_mu);
  }
  mtl_sub_begin();int live_operation=vk_pool_operation_begin_ranges(NULL,0);
  vk_buffer old=lazy_alias_resolve(address,SIZE,&old_offset);
#ifdef DM_VULKAN_NATIVE_RESOURCES
  CHECK(vk_test_native_import_coverage((uintptr_t)bytes+W,W));
  CHECK(!vk_test_native_import_coverage((uintptr_t)bytes,2*W));
#endif
  size_t owned_pins,borrowed_pins;
  vk_test_pool_operation_pin_counts(&owned_pins,&borrowed_pins);
  CHECK(!owned_pins && borrowed_pins==1);
  lazy_alias_old=old.handle;lazy_alias_destroy_original=vkDestroyBuffer;vkDestroyBuffer=lazy_alias_destroy;
  lazy_alias_barrier_original=vkCmdPipelineBarrier;vkCmdPipelineBarrier=lazy_alias_barrier;
  lazy_alias_fill(old,old_offset,address,SIZE,0x13243546);
  // Warm visibility BEFORE any overlapping import exists. This must already
  // be a real global barrier, never a named-buffer proof inherited by a later alias.
  unsigned warm=lazy_alias_global_transfer_fs;
  vk_buffer_shader_read((uintptr_t)address,SIZE,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  CHECK(lazy_alias_global_transfer_fs>warm && !lazy_alias_named);
  unsigned host_before=lazy_alias_global_host;
  uint64_t first=vk_internal_commit(); // intentionally no CPU wait before alias import
  CHECK(lazy_alias_global_host>host_before && !lazy_alias_named);
  vk_test_pool_operation_pin_counts(&owned_pins,&borrowed_pins);
  CHECK(owned_pins==1 && !borrowed_pins); // original commit upgrades before publication
  mtl_sub_begin();
  if(getenv("DM_SHARED_LAZY_ALIAS_NEGATIVE")) {
    // Establish a real old source hint in THIS recording before publishing
    // its crossing alias; otherwise only the new cover has a current hint.
    vk_buffer warm_old=lazy_alias_resolve(bytes+W+64,192,&cross_offset);
    CHECK(warm_old.handle==old.handle);
  }
  vk_buffer cover=lazy_alias_resolve(bytes+W-64,192,&cross_offset);
  lazy_alias_cover=cover.handle;CHECK(cover.handle!=old.handle);
  vk_pool_span_info range;CHECK(vk_pool_span_query((uintptr_t)bytes+W-64,192,&range)==VK_POOL_SPAN_SINGLE);
  CHECK(range.handle==cover.handle && range.logical_begin==0 && range.logical_end==2*W);
#ifdef DM_VULKAN_NATIVE_RESOURCES
  CHECK(vk_test_native_import_coverage((uintptr_t)bytes,2*W));
  CHECK(!vk_test_native_import_coverage((uintptr_t)bytes+2*W-1,2));
#endif
  CHECK((uintptr_t)address>=(uintptr_t)bytes+range.logical_begin);
  cover_offset=(uintptr_t)address-((uintptr_t)bytes+range.logical_begin);
  CHECK(cover_offset<=cover.size && SIZE<=cover.size-cover_offset);
  if(getenv("DM_SHARED_LAZY_ALIAS_NEGATIVE")) {
    CHECK(!strcmp(getenv("DM_SHARED_LAZY_ALIAS_NEGATIVE"),"overlap-copy"));
    // Same logical Pool, but source's old complete span and destination's
    // crossing cover may be different buffers. Overlap remains forbidden.
    mtl_sub_copy((uintptr_t)bytes+W+64,(uintptr_t)bytes+W-64,192);
    die("Shared lazy overlapping copy unexpectedly returned");
  }
  CHECK(!atomic_load(&lazy_alias_old_destroyed) && !atomic_load(&lazy_alias_cover_destroyed));
  lazy_alias_read(cover,cover_offset,address,SIZE,forward);
  lazy_alias_fill(cover,cover_offset,address,SIZE,0x98a9bacb);
  lazy_alias_read(old,old_offset,address,SIZE,reverse);
  // A nested operation keeps its own source/texture scope. It may use a third
  // disjoint lazy span, but must not overwrite or destroy either parent's handle.
  vk_texture_lookup_scope_begin();int outer_source=vk_shader_image_sources_begin();
  vk_texture_binding_scope_begin();vk_shader_operation_begin();vk_texture_shader_begin();
  vk_texture_lookup_scope_begin();int source=vk_shader_image_sources_begin();
  vk_texture_binding_scope_begin();vk_shader_operation_begin();vk_texture_shader_begin();
  vk_buffer ignored=lazy_alias_resolve(bytes+3*W+64,SIZE,&cross_offset);CHECK(ignored.handle);
#ifdef DM_VULKAN_NATIVE_RESOURCES
  CHECK(vk_test_native_import_coverage((uintptr_t)bytes+3*W,3*W));
  CHECK(!vk_test_native_import_coverage((uintptr_t)bytes+2*W,2*W));
#endif
  lazy_alias_fill(old,old_offset,address,SIZE,0x25364758);
  vk_texture_shader_abandon();vk_shader_operation_abandon();vk_texture_binding_scope_end();
  vk_shader_image_sources_end(source);vk_texture_lookup_scope_end();
  warm=lazy_alias_global_transfer_fs;
  vk_buffer_shader_read((uintptr_t)address,SIZE,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  CHECK(lazy_alias_global_transfer_fs>warm); // real old writer cannot be renamed to cover
  lazy_alias_read(cover,cover_offset,address,SIZE,nested);
  vk_texture_shader_abandon();vk_shader_operation_abandon();vk_texture_binding_scope_end();
  vk_shader_image_sources_end(outer_source);vk_texture_lookup_scope_end();
  vk_pool_operation_end(live_operation);
  // ASYNC alone keeps the writer pending. Serial begin deliberately finishes
  // the previous submission; it must never wait a gate whose signal is below.
  if(pending_gate) {
    VkSemaphoreSignalInfoKHR signal={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO_KHR,
      .semaphore=gate,.value=1};VK_CHECK(V.signal_timeline(V.device,&signal));
  }
  host_before=lazy_alias_global_host;
  uint64_t second=vk_internal_commit();CHECK(second>first);vk_internal_wait(second);
  CHECK(lazy_alias_global_host>host_before);
  lazy_alias_words(forward,SIZE,0x13243546);lazy_alias_words(reverse,SIZE,0x98a9bacb);
  lazy_alias_words(nested,SIZE,0x25364758);lazy_alias_words(address,SIZE,0x25364758);
  CHECK(lazy_alias_global_host && !lazy_alias_named);
  // A completed recording/context is not permission to destroy a physical
  // object retained by this policy. Re-borrow logical Pool use in a new owner,
  // then consume the OLD physical buffer after new covering import publication.
  CHECK(!atomic_load(&lazy_alias_old_destroyed) && !atomic_load(&lazy_alias_cover_destroyed));
  mtl_sub_begin();ignored=lazy_alias_resolve(address,SIZE,&cross_offset);CHECK(ignored.handle);
  lazy_alias_fill(old,old_offset,address,SIZE,0x4b5c6d7e);
  // Preserve the original unknown-producer fallback instead of inheriting a
  // precise visibility certificate through the new alias.
  vk_buffer_shader_written((uintptr_t)address,SIZE,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
  vk_buffer_shader_read((uintptr_t)address,SIZE,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  lazy_alias_read(cover,cover_offset,address,SIZE,later);
  host_before=lazy_alias_global_host;vk_internal_wait(vk_internal_commit());
  CHECK(lazy_alias_global_host>host_before);lazy_alias_words(later,SIZE,0x4b5c6d7e);
  lazy_alias_words(address,SIZE,0x4b5c6d7e);
  CHECK(!lazy_alias_named && !atomic_load(&lazy_alias_old_destroyed) && !atomic_load(&lazy_alias_cover_destroyed));
  // Address-only VBOs expose the remaining Pool extent, not one window.
  // An empty operation must still acquire actual recording refs/page ledger.
  mtl_sub_begin();int address_only=vk_pool_operation_begin_ranges(NULL,0);
  uint64_t producer_offset;uint8_t address_only_read[SIZE];
  vk_buffer producer=lazy_alias_resolve(bytes+3*W+128,SIZE,&producer_offset);
  lazy_alias_fill(producer,producer_offset,bytes+3*W+128,SIZE,0x62738495);
  CHECK(vk_buffer_vertex_pending((uintptr_t)bytes+3*W+128,SIZE));
  unsigned vertex_before=lazy_alias_global_transfer_vertex;
  vk_buffer_vertex_require((uintptr_t)bytes+3*W+128,0);
  CHECK(lazy_alias_global_transfer_vertex>vertex_before && !lazy_alias_named);
  CHECK(!vk_buffer_vertex_pending((uintptr_t)bytes+3*W+128,SIZE));
#ifdef DM_VULKAN_NATIVE_RESOURCES
  CHECK(vk_pool_native_acquire_independent((uintptr_t)bytes+(4u<<20)-1,1));
#endif
  uint64_t address_offset;vk_buffer tail=lazy_alias_resolve(bytes+3*W+128,0,&address_offset);
  CHECK(tail.size-address_offset==(4u<<20)-(3*W+128));
#ifdef DM_VULKAN_NATIVE_RESOURCES
  CHECK(!vk_pool_native_acquire_independent((uintptr_t)bytes+(4u<<20)-1,1));
#endif
  lazy_alias_read(tail,address_offset,bytes+3*W+128,SIZE,address_only_read);
  tail=lazy_alias_resolve(bytes+(4u<<20),0,&address_offset);CHECK(address_offset==tail.size);
#ifdef DM_VULKAN_NATIVE_RESOURCES
  CHECK(vk_test_native_import_coverage((uintptr_t)bytes+3*W,(4u<<20)-3*W));
  CHECK(!vk_test_native_import_coverage((uintptr_t)bytes+2*W-1,2));
#endif
  vk_pool_operation_end(address_only);vk_internal_wait(vk_internal_commit());
  lazy_alias_words(address_only_read,SIZE,0x62738495);lazy_alias_words(bytes+3*W+128,SIZE,0x62738495);
  CHECK(!atomic_load(&lazy_alias_old_destroyed) && !atomic_load(&lazy_alias_cover_destroyed));
  CALL(nvnMemoryPoolFinalize,&owned);
  CHECK(atomic_load(&lazy_alias_old_destroyed)==1 && atomic_load(&lazy_alias_cover_destroyed)==1);
  vkCmdPipelineBarrier=lazy_alias_barrier_original;vkDestroyBuffer=lazy_alias_destroy_original;
  if(gate)vkDestroySemaphore(V.device,gate,NULL);
  CHECK(vk_pool_span_query((uintptr_t)address,SIZE,&range)==VK_POOL_SPAN_NONE);
#ifdef DM_VULKAN_NATIVE_RESOURCES
  CHECK(!vk_test_native_import_coverage((uintptr_t)address,SIZE));
#endif
  shared_lazy_empty_binding_test();
  CHECK(!vk_validation_error_count());
  printf("{\"test\":\"shared-lazy-coherent-alias\",\"checks\":%u,\"result\":\"passed\",\"T2T_unbacked_copy_collect_then_original_observation\":true,\"ASTC_metadata_only_not_GPU\":true,\"generic_admission_before_observation\":true,\"draw_only_demand_admission\":true,\"invalid_batch_unchanged_coverage\":true,\"actual_GPU_bidirectional_bytes\":true,\"warm_proof_global\":true,\"old_writer_pending_during_cover_import\":%s,\"unknown_visibility_fallback\":true,\"old_physical_writer_after_cover\":true,\"HOST_publication_bytes\":true,\"nested_logical_scopes\":true,\"address_only_remaining_extent_and_ledger\":true,\"address_only_RAW_precise_vertex_dependency\":true,\"old_buffers_retained_through_completion\":true,\"actual_borrow_to_commit_pin_upgrade\":true,\"pool_close_destroyed_once\":true,\"empty_index16_32_finite_address_proof\":true,\"zero_VS_UBO_SSBO_not_address_only_VBO\":true,\"empty_draw_GPU_target_preserved\":true,\"game_modules_executed\":false}\n",checks,pending_gate?"true":"false");
}
#endif
