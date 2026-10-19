static void dirty_range_fill(uint8_t *,size_t,const uint8_t[4]);
// Original NVN API + unchanged pixel oracle, borrowed 2MiB pool / 64KiB nodes.
// No attach API, guest allocator, synthetic time or driver capability injection.
static int borrowed_nvn_windows_test(void) {
  mtl_init();CHECK(vk_test_pool_owned_supported());CHECK(dm_gfx_borrow_async_ready());
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
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,2u<<20));memset(storage,0,2u<<20);
  NVNmemoryPool source;borrowed_pool_init(&source,storage,2u<<20);
  uint64_t stats[4];int specialized;vk_uniform_specialization_stats(stats,stats+1,stats+2,stats+3,&specialized);
  vk_uniform_specialization_enabled(0);vk_state saved=S;
  uint8_t *a=storage+65536,*b=storage+5*65536+4096,*v=storage+9*65536+128;
  f.uniform_bytes=4096;*(float *)a=.25f;*(float *)b=.75f;
  submit(borrowed_record(&f,a,0,0));borrowed_pixels(&f,.25f);
  vk_pool_span_info first_a,first_b,first_v,current;
  CHECK(vk_pool_span_query((uintptr_t)a,4096,&first_a)==VK_POOL_SPAN_SINGLE);
  int flags=vk_test_pool_owned_properties((uintptr_t)a);CHECK(flags);
  submit(borrowed_record(&f,b,0,0));borrowed_pixels(&f,.75f);
  CHECK(vk_pool_span_query((uintptr_t)b,4096,&first_b)==VK_POOL_SPAN_SINGLE);
  CHECK(vk_test_pool_owned_properties((uintptr_t)b)==flags && first_b.handle!=first_a.handle);
  // Partial read/prefix uses original VkBuffer-relative offset, not guest base.
  CHECK(first_b.local_offset==4096);
  CHECK(!vk_test_pool_owned_properties((uintptr_t)storage));
  // Historical image declaration, even finalized before first buffer use,
  // blocks only its exact canonical node rather than this whole logical pool.
  CALL(nvnTextureBuilderSetStorage,&tb,&source,12*65536);
  NVNtexture history;CHECK(CALL(nvnTextureInitialize,&history,&tb));CALL(nvnTextureFinalize,&history);
  *(float *)(storage+12*65536)=.25f;submit(borrowed_record(&f,storage+12*65536,0,0));borrowed_pixels(&f,.25f);
  CHECK(!vk_test_pool_owned_properties((uintptr_t)storage+12*65536));
  CHECK(vk_test_pool_owned_properties((uintptr_t)a)==flags && vk_test_pool_owned_properties((uintptr_t)b)==flags);
  // One gap in the SAME pool remains an ordinary dirty-tracked image source.
  uint8_t *gap=storage+3*65536;nvn_dirty_scope_proof proof,next;uint64_t epoch;
  CHECK(nvn_dirty_scope_observe(gap,4096,&proof,&epoch));
  uint64_t stamp=nvn_dirty_now();nvn_dirty_protect(gap,4096);dm_guest_write_prepare(gap,4);*(float *)gap=.125f;
  CHECK(nvn_dirty_since(gap,4096,stamp));CHECK(nvn_dirty_scope_refresh(gap,4096,&proof,&next,&epoch));CHECK(next.changes!=proof.changes);
  // Initial PROT is rejected for this window only. Other existing nodes survive.
  nvn_dirty_protect(gap+4096,4096); // unrelated adjacent page is still protected
  submit(borrowed_record(&f,gap,0,0));borrowed_pixels(&f,.125f);
  CHECK(!vk_test_pool_owned_properties((uintptr_t)gap));
  CHECK(vk_test_pool_owned_properties((uintptr_t)a)==flags && vk_test_pool_owned_properties((uintptr_t)b)==flags);
  uint8_t *cpu_dest=storage+15*65536;dm_guest_write_prepare(cpu_dest,4);*(float *)cpu_dest=.25f;
  submit(borrowed_record(&f,cpu_dest,0,0));borrowed_pixels(&f,.25f);
  CHECK(vk_test_pool_owned_properties((uintptr_t)cpu_dest)==flags);
  uint8_t vc[0x880],fc[0x880];memset(vc,14,sizeof vc);memset(fc,11,sizeof fc);
  NVNshaderData vd[]={{.control=vc},{.control=fc}};NVNprogram vbo;
  CHECK(CALL(nvnProgramInitialize,&vbo,&device));CHECK(CALL(nvnProgramSetShaders,&vbo,2,vd));
  const float positions[][6]={{-1,-1,3,-1,-1,3},{4,4,6,4,4,6},{-1,-1,3,-1,-1,3}};
  for(unsigned k=0;k<3;k++) {
    if(k)nvn_dirty_protect(storage,2u<<20); // imports skip; ordinary gap still protects
    if(k==1)dm_guest_write_prepare(v,24);memcpy(v,positions[k],24);
    submit(borrowed_record_impl(&f,v,0,0,&vbo));
    uint8_t rgba[1024];NVNcopyRegion region={0,0,0,16,16,1};CALL(nvnTextureReadTexels,&f.target,NULL,&region,rgba);
    const uint8_t expected[]={0,k==1?0:128,k==1?0:255,255};
    for(unsigned p=0;p<256;p++)CHECK(!memcmp(rgba+4*p,expected,4));
    CHECK(vk_pool_span_query((uintptr_t)v,24,&current)==VK_POOL_SPAN_SINGLE);
    CHECK(vk_test_pool_owned_properties((uintptr_t)v)==flags);
    if(!k)first_v=current;else CHECK(current.handle==first_v.handle && current.backing_generation==first_v.backing_generation);
  }
  // All three independent imports coexist; legal direct CPU edit of one does
  // not alter any other's handle, while every submission still flushes reads.
  memcpy(a,&(float){.75f},4);submit(borrowed_record(&f,a,0,0));borrowed_pixels(&f,.75f);
  CHECK(vk_pool_span_query((uintptr_t)a,4096,&current)==VK_POOL_SPAN_SINGLE);CHECK(current.handle==first_a.handle);
  CHECK(vk_pool_span_query((uintptr_t)b,4096,&current)==VK_POOL_SPAN_SINGLE);CHECK(current.handle==first_b.handle);
  // Real packaged image in an unimported gap of the SAME pool retains the
  // original scoped negative proof, and a real dirty CPU write changes GPU
  // texels even while unrelated buffer nodes remain volatile/imported.
  uint8_t *gap_image=storage+2*65536;const uint8_t red[]={255,0,0,255},green[]={0,255,0,255};
  dm_guest_write_prepare(gap_image,4096);dirty_range_fill(gap_image,4096,red);
  CALL(nvnTextureBuilderSetStorage,&tb,&source,2*65536);
  NVNpackagedTextureLayout gap_packaged={{0}};CALL(nvnTextureBuilderSetPackagedTextureLayout,&tb,&gap_packaged);
  NVNtexture gap_texture;CHECK(CALL(nvnTextureInitialize,&gap_texture,&tb));
  dirty_range_pixels(&gap_texture,red);dirty_range_pixels(&gap_texture,red);dirty_range_pixels(&gap_texture,red);
  uint64_t hits_before,checks_before,hits_after,checks_after;vk_texture_dirty_stats(&hits_before,&checks_before);
  dirty_range_pixels(&gap_texture,red);vk_texture_dirty_stats(&hits_after,&checks_after);
  CHECK(hits_after>hits_before); // original renderer shortcut, not only API mock
  dm_guest_write_prepare(gap_image,4096);dirty_range_fill(gap_image,4096,green);dirty_range_pixels(&gap_texture,green);
  CALL(nvnTextureFinalize,&gap_texture);
  CHECK(vk_test_pool_owned_properties((uintptr_t)a)==flags && vk_test_pool_owned_properties((uintptr_t)b)==flags);
  // An actual original UpdateUniformBuffer writer retires only a's window.
  submit(borrowed_record(&f,a,1,.25f));borrowed_pixels(&f,.25f);
  CHECK(!vk_test_pool_owned_properties((uintptr_t)a));CHECK(vk_test_pool_owned_properties((uintptr_t)b)==flags);
  CHECK(vk_test_pool_owned_properties((uintptr_t)v)==flags);
  CHECK(vk_test_pool_owned_shadow_matches((uintptr_t)a,&(float){.25f},4));
  // Original late packaged ImageInitialize marks one imported node, waits for
  // original GPU/effects, restores mirror+shadow, then verifies real image bytes.
  uint8_t *image=storage+9*65536;
  const uint8_t pixel[]={13,51,99,255};for(unsigned n=0;n<4096;n+=4)memcpy(image+n,pixel,4);
  CALL(nvnTextureBuilderSetStorage,&tb,&source,9*65536);
  NVNpackagedTextureLayout packaged={{0}};CALL(nvnTextureBuilderSetPackagedTextureLayout,&tb,&packaged);
  NVNtexture alias;CHECK(CALL(nvnTextureInitialize,&alias,&tb));
  CHECK(!vk_test_pool_owned_properties((uintptr_t)v));CHECK(vk_test_pool_owned_properties((uintptr_t)b)==flags);
  dirty_range_pixels(&alias,pixel);CALL(nvnTextureFinalize,&alias);
  // CPU-effect destination revocation goes through the original NVN API,
  // not a direct fixture retire helper; unrelated b remains imported.
  NVNcopyRegion cpu_region={0,0,0,16,16,1};CALL(nvnTextureReadTexels,&f.target,NULL,&cpu_region,cpu_dest);
  CHECK(!vk_test_pool_owned_properties((uintptr_t)cpu_dest));
  CHECK(vk_test_pool_owned_properties((uintptr_t)b)==flags);
  for(unsigned n=0;n<256;n++)CHECK(cpu_dest[4*n]==64 && cpu_dest[4*n+1]==255 && !cpu_dest[4*n+2] && cpu_dest[4*n+3]==255);
  // Crossing two canonical nodes is never stitched into an invalid descriptor.
  // Original admission retires any intersecting import and consolidates; the
  // real shader reads from the complete resulting range through normal UBO.
  f.uniform_bytes=8192;uint8_t *cross=storage+6*65536-4096;
  dm_guest_write_prepare(cross,4);*(float *)cross=.125f;
  submit(borrowed_record(&f,cross,0,0));borrowed_pixels(&f,.125f);
  CHECK(!vk_test_pool_owned_properties((uintptr_t)b));
  CHECK(vk_pool_span_query((uintptr_t)cross,8192,&current)==VK_POOL_SPAN_SINGLE);
  CALL(nvnProgramFinalize,&vbo);CALL(nvnMemoryPoolFinalize,&source);
  CHECK(!vk_test_pool_borrow_state((uintptr_t)storage));
  // Fresh same-address logical lifetime must not inherit previous retirement
  // history, backing handles, page exclusions or Finalize certificate.
  memset(storage,0,2u<<20);borrowed_pool_init(&source,storage,2u<<20);f.uniform_bytes=4096;*(float *)a=.75f;
  submit(borrowed_record(&f,a,0,0));borrowed_pixels(&f,.75f);CHECK(vk_test_pool_owned_properties((uintptr_t)a)==flags);
  // Close a second, not-yet-imported node while the original recording owner
  // is gated on attach. Mark-closing prevents late GUP; prior work stays valid.
  uint8_t *late=storage+13*65536;*(float *)late=.25f;vk_test_pool_borrow_gate((uintptr_t)late,0);
  NVNcommandHandle pending=borrowed_record(&f,late,0,0);CALL(nvnQueueSubmitCommands,&queue,1,&pending);
  vk_test_pool_borrow_gate_wait();pthread_t finalizer;CHECK(!pthread_create(&finalizer,NULL,borrowed_finalize_thread,&source));
  vk_test_pool_borrow_wait_closing((uintptr_t)storage);vk_test_pool_borrow_gate(0,1);CHECK(!pthread_join(finalizer,NULL));
  borrowed_pixels(&f,.25f);CHECK(!vk_test_pool_borrow_state((uintptr_t)storage));
  // A page-complete final 4KiB tail is a legal canonical node even though
  // the whole Pool is not a multiple of the 64KiB window policy.
  uint8_t *tail_pool;CHECK(!posix_memalign((void **)&tail_pool,65536,(2u<<20)+4096));
  memset(tail_pool,0,(2u<<20)+4096);borrowed_pool_init(&source,tail_pool,(2u<<20)+4096);
  uint8_t *tail=tail_pool+(2u<<20);*(float *)tail=.125f;submit(borrowed_record(&f,tail,0,0));borrowed_pixels(&f,.125f);
  CHECK(vk_test_pool_owned_properties((uintptr_t)tail)==flags);
  CHECK(vk_pool_span_query((uintptr_t)tail,4096,&current)==VK_POOL_SPAN_SINGLE);CHECK(current.size==4096);
  CALL(nvnMemoryPoolFinalize,&source);free(tail_pool);
  vk_uniform_specialization_enabled(specialized);S=saved;vk_pipeline_state_changed();
  CALL(nvnCommandBufferFinalize,&f.commands);CALL(nvnTextureFinalize,&f.target);CALL(nvnProgramFinalize,&f.program);
  CALL(nvnMemoryPoolFinalize,&f.backing);free(f.storage);free(storage);
  LOG("[borrowed subwindows fixture] 2MiB pool, independent UBO/VBO/prefix/gap proof/writer/image/crossing/closing/ABA passed; flags=%#x",flags);
  return flags;
}
