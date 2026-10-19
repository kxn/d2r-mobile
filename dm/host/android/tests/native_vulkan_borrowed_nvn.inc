#include <sys/mman.h>
// An independent owner supplies ordinary borrowed NVN storage. There is NO
// attach call: original Initialize -> operation preflight chooses import.
typedef struct {
  NVNmemoryPool backing;
  NVNtexture target;
  NVNcommandBuffer commands;
  NVNprogram program;
  uint8_t *storage;
  uint64_t uniform_bytes;
} borrowed_gpu_fixture;
static void borrowed_pool_init(NVNmemoryPool *pool,uint8_t *address,size_t size) {
  NVNmemoryPoolBuilder b;CALL(nvnMemoryPoolBuilderSetDefaults,&b);
  CALL(nvnMemoryPoolBuilderSetDevice,&b,&device);CALL(nvnMemoryPoolBuilderSetStorage,&b,address,size);
  CALL(nvnMemoryPoolBuilderSetFlags,&b,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,pool,&b));
}
static NVNcommandHandle borrowed_record_impl(borrowed_gpu_fixture *f,uint8_t *uniform,int writer,float value,const NVNprogram *vbo_program) {
  uint64_t uniform_bytes=f->uniform_bytes?f->uniform_bytes:65536;
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);
  CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);NVNchannelMaskState mask;CALL(nvnChannelMaskStateSetDefaults,&mask);
  const NVNtexture *target[]={&f->target};const float viewport[]={0,0,16,16};
  CALL(nvnCommandBufferBeginRecording,&f->commands);
  if(writer)CALL(nvnCommandBufferUpdateUniformBuffer,&f->commands,(uintptr_t)uniform,uniform_bytes,0,4,&value);
  CALL(nvnCommandBufferSetRenderTargets,&f->commands,1,target,NULL,NULL,NULL);
  CALL(nvnCommandBufferSetViewports,&f->commands,0,1,viewport);CALL(nvnCommandBufferSetScissor,&f->commands,0,0,16,16);
  CALL(nvnCommandBufferBindDepthStencilState,&f->commands,&ds);CALL(nvnCommandBufferBindPolygonState,&f->commands,&polygon);
  CALL(nvnCommandBufferBindColorState,&f->commands,&color);CALL(nvnCommandBufferBindChannelMaskState,&f->commands,&mask);
  if(vbo_program) {
    NVNvertexAttribState attr;CALL(nvnVertexAttribStateSetDefaults,&attr);
    CALL(nvnVertexAttribStateSetStreamIndex,&attr,0);CALL(nvnVertexAttribStateSetFormat,&attr,NVN_FORMAT_RG32F,0);
    NVNvertexStreamState stream;CALL(nvnVertexStreamStateSetDefaults,&stream);CALL(nvnVertexStreamStateSetStride,&stream,8);
    NVNbufferRange range={(uintptr_t)uniform,24};
    CALL(nvnCommandBufferBindVertexAttribState,&f->commands,1,&attr);CALL(nvnCommandBufferBindVertexStreamState,&f->commands,1,&stream);
    CALL(nvnCommandBufferBindVertexBuffers,&f->commands,0,1,&range);
    const float black[]={0,0,0,1};CALL(nvnCommandBufferClearColor,&f->commands,0,black,15);
  } else {
    CALL(nvnCommandBufferBindVertexAttribState,&f->commands,0,NULL);CALL(nvnCommandBufferBindVertexStreamState,&f->commands,0,NULL);
  }
  CALL(nvnCommandBufferBindProgram,&f->commands,vbo_program?vbo_program:&f->program,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
  if(!vbo_program)CALL(nvnCommandBufferBindUniformBuffer,&f->commands,NVN_SHADER_STAGE_FRAGMENT,0,(uintptr_t)uniform,uniform_bytes);
  CALL(nvnCommandBufferDrawArrays,&f->commands,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
  return CALL(nvnCommandBufferEndRecording,&f->commands);
}
static NVNcommandHandle borrowed_record(borrowed_gpu_fixture *f,uint8_t *uniform,int writer,float value) {
  return borrowed_record_impl(f,uniform,writer,value,NULL);
}
static void borrowed_pixels(borrowed_gpu_fixture *f,float value) {
  uint8_t pixels[1024];NVNcopyRegion region={0,0,0,16,16,1};
  CALL(nvnTextureReadTexels,&f->target,NULL,&region,pixels);
  for(unsigned p=0;p<256;p++) {
    CHECK(abs((int)pixels[p*4]-(int)lroundf(value*255))<=1);
    CHECK(pixels[p*4+1]==(value<.5f?255:0) && pixels[p*4+2]==(value<.5f?0:255) && pixels[p*4+3]==255);
  }
}
static void *borrowed_finalize_thread(void *pool) {
  crash_thread_init();CALL(nvnMemoryPoolFinalize,pool);return NULL;
}
static int borrowed_nvn_buffer_test(void) {
  mtl_init();CHECK(vk_test_pool_owned_supported());
  borrowed_gpu_fixture f={0};uint8_t vertex[0x880]={0},fragment[0x880];memset(fragment,39,sizeof fragment);
  NVNshaderData shaders[]={{.control=vertex},{.control=fragment}};
  CHECK(CALL(nvnProgramInitialize,&f.program,&device));CHECK(CALL(nvnProgramSetShaders,&f.program,2,shaders));
  CHECK(!posix_memalign((void **)&f.storage,65536,786432));memset(f.storage,0,786432);
  borrowed_pool_init(&f.backing,f.storage,786432);
  NVNtextureBuilder tb;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&tb,16);CALL(nvnTextureBuilderSetHeight,&tb,16);
  CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);CALL(nvnTextureBuilderSetStorage,&tb,&f.backing,524288);
  CHECK(CALL(nvnTextureInitialize,&f.target,&tb));CHECK(CALL(nvnCommandBufferInitialize,&f.commands,&device));
  CALL(nvnCommandBufferAddCommandMemory,&f.commands,&f.backing,131072,393216);
  uint8_t *uniform;CHECK(!posix_memalign((void **)&uniform,65536,65536));memset(uniform,0,65536);
  NVNmemoryPool source;borrowed_pool_init(&source,uniform,65536);
  uint64_t specialization[4];int enabled;vk_uniform_specialization_stats(specialization,specialization+1,specialization+2,specialization+3,&enabled);
  vk_uniform_specialization_enabled(0);vk_state saved=S;int properties=0;
  if(getenv("DM_BORROWED_NVN_ASYNC_REJECT_FIXTURE") || getenv("DM_BORROWED_NVN_WORKER_REJECT_FIXTURE")) {
    CHECK(!dm_gfx_borrow_async_ready());CHECK(!vk_test_pool_borrow_state((uintptr_t)uniform));
    uint64_t before,retired,after,retired_after;vk_test_pool_owned_stats(&before,&retired);
    *(float *)uniform=.25f;submit(borrowed_record(&f,uniform,0,0));borrowed_pixels(&f,.25f);
    CHECK(!vk_test_pool_owned_properties((uintptr_t)uniform));
    vk_test_pool_owned_stats(&after,&retired_after);CHECK(after==before && retired_after==retired);
    CALL(nvnMemoryPoolFinalize,&source);
    LOG("[borrowed NVN fixture] dedicated async owner unavailable: no lease/no GUP; ordinary mirrored UBO pixels passed");
    goto cleanup;
  }
  CHECK(dm_gfx_borrow_async_ready());CHECK(vk_test_pool_borrow_state((uintptr_t)uniform)==1);
  CHECK(!vk_test_pool_owned_attach((uintptr_t)uniform,65536)); // old provenance API cannot be substituted
  vk_pool_span_info first;CHECK(vk_pool_span_query((uintptr_t)uniform,65536,&first)==VK_POOL_SPAN_UNBACKED);
  uint64_t imports,retirements;vk_test_pool_owned_stats(&imports,&retirements);
  const float values[]={.25f,.75f,.125f,.875f};
  for(unsigned iteration=0;iteration<4;iteration++) {
    if(iteration)nvn_dirty_protect(uniform,65536); // active lease must skip protection
    if(iteration&1)dm_guest_write_prepare(uniform,4);
    float cpu=iteration==3?.375f:values[iteration];memcpy(uniform,&cpu,4);
    submit(borrowed_record(&f,uniform,iteration==3,values[iteration]));borrowed_pixels(&f,values[iteration]);
    CHECK(!memcmp(uniform,&values[iteration],4));
    vk_pool_span_info current;CHECK(vk_pool_span_query((uintptr_t)uniform,65536,&current)==VK_POOL_SPAN_SINGLE);
    if(!iteration) {
      first=current;properties=vk_test_pool_owned_properties((uintptr_t)uniform);
      CHECK((properties&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT))==
        (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT));
    }
    if(iteration<3) {
      CHECK(current.handle==first.handle && current.backing_generation==first.backing_generation);
      CHECK(vk_test_pool_owned_properties((uintptr_t)uniform)==properties);
    } else {
      CHECK(current.handle!=first.handle && !vk_test_pool_owned_properties((uintptr_t)uniform));
      CHECK(vk_test_pool_owned_shadow_matches((uintptr_t)uniform,&values[iteration],4));
    }
    uint64_t now_imports,now_retirements;vk_test_pool_owned_stats(&now_imports,&now_retirements);
    CHECK(now_imports==imports+1 && now_retirements==retirements+(iteration==3));
    printf("{\"test_details\":\"borrowed-nvn-ubo-cycle\",\"iteration\":%u,\"flags\":%d,"
      "\"same_import\":%s,\"retired\":%s,\"pixels_live_CPU_verified\":true,\"shadow_verified\":%s}\n",
      iteration,properties,iteration<3?"true":"false",iteration==3?"true":"false",iteration==3?"true":"false");
  }
  // Original dirty protection/fault is back after writer demotion, not a test
  // mprotect workaround. Retirement remains sticky for this lease.
  nvn_dirty_protect(uniform,4096);*(volatile float *)uniform=.5f;
  CHECK(*(float *)uniform==.5f);submit(borrowed_record(&f,uniform,0,0));borrowed_pixels(&f,.5f);
  CHECK(!vk_test_pool_owned_properties((uintptr_t)uniform));CALL(nvnMemoryPoolFinalize,&source);
  CHECK(!vk_test_pool_borrow_state((uintptr_t)uniform));
  // Same address and dirty slot, new logical pool/lease. Do not carry old
  // retirement/identity proof across Finalize.
  memset(uniform,0,65536);borrowed_pool_init(&source,uniform,65536);CHECK(vk_test_pool_borrow_state((uintptr_t)uniform)==1);
  *(float *)uniform=.25f;nvn_dirty_protect(uniform,65536);
  vk_test_pool_owned_stats(&imports,&retirements);
  submit(borrowed_record(&f,uniform,0,0));borrowed_pixels(&f,.25f);
  CHECK(!vk_test_pool_owned_properties((uintptr_t)uniform));CHECK(vk_test_pool_borrow_state((uintptr_t)uniform)&8);
  uint64_t after_imports,after_retirements;vk_test_pool_owned_stats(&after_imports,&after_retirements);
  CHECK(after_imports==imports && after_retirements==retirements); // zero GUP for initially PROT
  dm_guest_write_prepare(uniform,4);*(float *)uniform=.75f;
  submit(borrowed_record(&f,uniform,0,0));borrowed_pixels(&f,.75f);
  vk_test_pool_owned_stats(&after_imports,&after_retirements);CHECK(after_imports==imports); // no retry
  printf("{\"test_details\":\"borrowed-nvn-initial-prot\",\"vk_import_allocation_attempts\":0,\"unprotect_retry\":false,\"pixels_verified\":true}\n");
  CALL(nvnMemoryPoolFinalize,&source);
  // A finalized logical image still poisons historical eligibility BEFORE
  // first physical image creation. Then the ordinary UBO path stays mirrored.
  memset(uniform,0,65536);borrowed_pool_init(&source,uniform,65536);
  CALL(nvnTextureBuilderSetStorage,&tb,&source,0);NVNtexture alias;
  CHECK(CALL(nvnTextureInitialize,&alias,&tb));CALL(nvnTextureFinalize,&alias);
  CHECK(vk_test_pool_borrow_state((uintptr_t)uniform)==3);
  *(float *)uniform=.125f;submit(borrowed_record(&f,uniform,0,0));borrowed_pixels(&f,.125f);
  CHECK(!vk_test_pool_owned_properties((uintptr_t)uniform));CALL(nvnMemoryPoolFinalize,&source);
  // Buffer/texel texture objects are also aliases, even without a VkImage and
  // even when the object is finalized before the first UBO draw.
  memset(uniform,0,65536);borrowed_pool_init(&source,uniform,65536);
  CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_BUFFER);CALL(nvnTextureBuilderSetStorage,&tb,&source,0);
  CHECK(CALL(nvnTextureInitialize,&alias,&tb));CALL(nvnTextureFinalize,&alias);
  CHECK(vk_test_pool_borrow_state((uintptr_t)uniform)==3);
  *(float *)uniform=.75f;submit(borrowed_record(&f,uniform,0,0));borrowed_pixels(&f,.75f);
  CHECK(!vk_test_pool_owned_properties((uintptr_t)uniform));CALL(nvnMemoryPoolFinalize,&source);
  CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);
  // CPU completion destinations are historical too, not inferred from whether
  // a value actually changed or whether a particular event was signaled.
  memset(uniform,0,65536);borrowed_pool_init(&source,uniform,65536);
  NVNevent event;NVNeventBuilder eb;CALL(nvnEventBuilderSetDefaults,&eb);
  CALL(nvnEventBuilderSetStorage,&eb,&source,4096);CHECK(CALL(nvnEventInitialize,&event,&eb));
  CALL(nvnEventFinalize,&event);CHECK(vk_test_pool_borrow_state((uintptr_t)uniform)==3);
  *(float *)uniform=.25f;submit(borrowed_record(&f,uniform,0,0));borrowed_pixels(&f,.25f);
  CHECK(!vk_test_pool_owned_properties((uintptr_t)uniform));CALL(nvnMemoryPoolFinalize,&source);
  // Original GPU writer before any read cannot opportunistically create an
  // imported readonly backing and later try to repair it while borrowed.
  memset(uniform,0,65536);borrowed_pool_init(&source,uniform,65536);*(float *)uniform=.25f;
  vk_test_pool_owned_stats(&imports,&retirements);
  submit(borrowed_record(&f,uniform,1,.875f));borrowed_pixels(&f,.875f);
  vk_test_pool_owned_stats(&after_imports,&after_retirements);
  CHECK(after_imports==imports && after_retirements==retirements);
  CHECK(!vk_test_pool_owned_properties((uintptr_t)uniform));CALL(nvnMemoryPoolFinalize,&source);
  // Late original image publication retires an already imported buffer before
  // CPU swizzle/image access. GPU byte checks remain the original image oracle.
  memset(uniform,0,65536);borrowed_pool_init(&source,uniform,65536);*(float *)uniform=.25f;
  submit(borrowed_record(&f,uniform,0,0));borrowed_pixels(&f,.25f);
  CHECK(vk_test_pool_owned_properties((uintptr_t)uniform));
  const uint8_t pixel[]={13,51,99,255};for(unsigned p=0;p<4096;p+=4)memcpy(uniform+p,pixel,4);
  NVNpackagedTextureLayout packaged={{0}};CALL(nvnTextureBuilderSetPackagedTextureLayout,&tb,&packaged);
  CHECK(CALL(nvnTextureInitialize,&alias,&tb));CHECK(!vk_test_pool_owned_properties((uintptr_t)uniform));
  dirty_range_pixels(&alias,pixel);CALL(nvnTextureFinalize,&alias);CALL(nvnMemoryPoolFinalize,&source);
  printf("{\"test_details\":\"borrowed-nvn-late-image\",\"retired_before_image\":true,\"image_GPU_bytes_verified\":true}\n");
  // Original command memory admission blocks a lease even when that recorder
  // is discarded; do not guess purity from only submitted draws.
  memset(uniform,0,65536);borrowed_pool_init(&source,uniform,65536);
  NVNcommandBuffer discarded;CHECK(CALL(nvnCommandBufferInitialize,&discarded,&device));
  CALL(nvnCommandBufferAddCommandMemory,&discarded,&source,4096,4096);
  CHECK(vk_test_pool_borrow_state((uintptr_t)uniform)==3);CALL(nvnCommandBufferFinalize,&discarded);
  *(float *)uniform=.25f;submit(borrowed_record(&f,uniform,0,0));borrowed_pixels(&f,.25f);
  CHECK(!vk_test_pool_owned_properties((uintptr_t)uniform));CALL(nvnMemoryPoolFinalize,&source);
  // Deterministically stop the translator AFTER it chose this lease and BEFORE
  // import. Concurrent ORIGINAL Finalize closes admission, queues owner close,
  // and cannot return until prior draw + all GPU refs/effects have completed.
  memset(uniform,0,65536);borrowed_pool_init(&source,uniform,65536);*(float *)uniform=.75f;
  vk_test_pool_owned_stats(&imports,&retirements);vk_test_pool_borrow_gate((uintptr_t)uniform,0);
  NVNcommandHandle pending=borrowed_record(&f,uniform,0,0);CALL(nvnQueueSubmitCommands,&queue,1,&pending);
  vk_test_pool_borrow_gate_wait();pthread_t finalizer;
  CHECK(!pthread_create(&finalizer,NULL,borrowed_finalize_thread,&source));
  vk_test_pool_borrow_wait_closing((uintptr_t)uniform);
  CHECK(vk_test_pool_borrow_state((uintptr_t)uniform)&4);vk_test_pool_borrow_gate(0,1);
  CHECK(!pthread_join(finalizer,NULL));CHECK(!vk_test_pool_borrow_state((uintptr_t)uniform));borrowed_pixels(&f,.75f);
  vk_test_pool_owned_stats(&after_imports,&after_retirements);CHECK(after_imports==imports && after_retirements==retirements);
  printf("{\"test_details\":\"borrowed-nvn-closing\",\"late_import_allocations\":0,\"pool_removed\":true,\"prior_GPU_pixels_verified\":true}\n");
  // A new pool at the same address imports normally, then Finalize itself
  // must retire it (not merely the writer/image side entrances above).
  memset(uniform,0,65536);borrowed_pool_init(&source,uniform,65536);*(float *)uniform=.125f;
  submit(borrowed_record(&f,uniform,0,0));borrowed_pixels(&f,.125f);CHECK(vk_test_pool_owned_properties((uintptr_t)uniform));
  CALL(nvnMemoryPoolFinalize,&source);CHECK(!vk_test_pool_borrow_state((uintptr_t)uniform));
  // Existing HLE mapping-change callback: retire first, then the caller can
  // change its owned VMA. This exercises the hook with self-owned pages, NOT
  // any guest fixed-AS SVC or arbitrary malloc/allocator interception.
  memset(uniform,0,65536);borrowed_pool_init(&source,uniform,65536);*(float *)uniform=.25f;
  submit(borrowed_record(&f,uniform,0,0));borrowed_pixels(&f,.25f);CHECK(vk_test_pool_owned_properties((uintptr_t)uniform));
  dm_gfx_borrow_before_cpu_change((uintptr_t)uniform,65536);CHECK(!vk_test_pool_owned_properties((uintptr_t)uniform));
  CHECK(!mprotect(uniform,65536,PROT_READ));CHECK(!mprotect(uniform,65536,PROT_READ|PROT_WRITE));
  dm_guest_write_prepare(uniform,4);*(float *)uniform=.75f;
  submit(borrowed_record(&f,uniform,0,0));borrowed_pixels(&f,.75f);CALL(nvnMemoryPoolFinalize,&source);
  // Actual VBO fetch: vertex bytes control full coverage -> fully clipped ->
  // full coverage. Same imported handle/generation across legal CPU edits.
  // Existing marker14 position shader and marker11 vary-color fragment only.
  memset(uniform,0,65536);borrowed_pool_init(&source,uniform,65536);
  uint8_t vc[0x880],fc[0x880];memset(vc,14,sizeof vc);memset(fc,11,sizeof fc);
  NVNshaderData vd[]={{.control=vc},{.control=fc}};NVNprogram vbo_program;
  CHECK(CALL(nvnProgramInitialize,&vbo_program,&device));CHECK(CALL(nvnProgramSetShaders,&vbo_program,2,vd));
  const float positions[][6]={{-1,-1,3,-1,-1,3},{4,4,6,4,4,6},{-1,-1,3,-1,-1,3}};
  vk_pool_span_info initial_vbo={0};uint64_t vi,vr;vk_test_pool_owned_stats(&vi,&vr);
  for(unsigned k=0;k<3;k++) {
    if(k)nvn_dirty_protect(uniform,65536);
    if(k==1)dm_guest_write_prepare(uniform,24);
    memcpy(uniform,positions[k],24);
    submit(borrowed_record_impl(&f,uniform,0,0,&vbo_program));
    uint8_t rgba[1024];NVNcopyRegion region={0,0,0,16,16,1};CALL(nvnTextureReadTexels,&f.target,NULL,&region,rgba);
    const uint8_t expected[]={0,k==1?0:128,k==1?0:255,255};
    for(unsigned p=0;p<256;p++)CHECK(!memcmp(rgba+4*p,expected,4));
    vk_pool_span_info span;CHECK(vk_pool_span_query((uintptr_t)uniform,24,&span)==VK_POOL_SPAN_SINGLE);
    CHECK(vk_test_pool_owned_properties((uintptr_t)uniform));
    if(!k)initial_vbo=span;else CHECK(span.handle==initial_vbo.handle && span.backing_generation==initial_vbo.backing_generation);
    vk_test_pool_owned_stats(&after_imports,&after_retirements);CHECK(after_imports==vi+1 && after_retirements==vr);
    printf("{\"test_details\":\"borrowed-nvn-vbo-cycle\",\"iteration\":%u,\"flags\":%d,\"same_import\":true,\"coverage_verified\":\"%s\"}\n",
      k,vk_test_pool_owned_properties((uintptr_t)uniform),k==1?"clipped":"full");
  }
  CALL(nvnProgramFinalize,&vbo_program);CALL(nvnMemoryPoolFinalize,&source);
  // Force stricter driver requirements only. Never force support. Both the
  // external DEDICATED_ONLY and buffer requiresDedicated paths must fall back
  // with zero allocation/GUP and retain original byte/dirty semantics.
  for(int rejection=1;rejection<=2;rejection++) {
    memset(uniform,0,65536);borrowed_pool_init(&source,uniform,65536);*(float *)uniform=.75f;
    vk_test_pool_import_dedicated_reject(rejection);vk_test_pool_owned_stats(&vi,&vr);
    submit(borrowed_record(&f,uniform,0,0));borrowed_pixels(&f,.75f);
    CHECK(!vk_test_pool_owned_properties((uintptr_t)uniform));
    vk_test_pool_owned_stats(&after_imports,&after_retirements);CHECK(after_imports==vi && after_retirements==vr);
    printf("{\"test_details\":\"borrowed-nvn-dedicated-reject\",\"strict_injection\":%d,\"vk_import_allocation_attempts\":0,\"mirror_GPU_pixels_verified\":true}\n",rejection);
    vk_test_pool_import_dedicated_reject(0);CALL(nvnMemoryPoolFinalize,&source);
  }
cleanup:
  vk_uniform_specialization_enabled(enabled);S=saved;vk_pipeline_state_changed();
  CALL(nvnCommandBufferFinalize,&f.commands);CALL(nvnTextureFinalize,&f.target);CALL(nvnProgramFinalize,&f.program);
  CALL(nvnMemoryPoolFinalize,&f.backing);free(f.storage);free(uniform);
  if(properties)LOG("[borrowed NVN fixture] original lease/UBO/VBO/draw/writer/image/command history/Finalize race/ABA/VMA/dedicated fallback passed; flags=%#x",properties);
  else LOG("[borrowed NVN fixture] async-unavailable mirror fallback suite complete; no shared import enabled");
  return properties;
}
