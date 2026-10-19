// Real NVN UPDATE_UB and GPU-byte oracle. No timer or new runtime policy.
#ifdef DM_VULKAN_TEST_EMBED
static void retained_update_test(void) {
  mtl_init();mtl_sub_begin();vk_internal_wait(vk_internal_commit());enum {W=65536};uint8_t *storage,out[128],expected[128],seed[37];
  CHECK(!posix_memalign((void **)&storage,W,2*W));memset(storage,0x37,2*W);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;CALL(nvnMemoryPoolBuilderSetDefaults,&mb);
  CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,2*W);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNcommandBuffer local;CHECK(CALL(nvnCommandBufferInitialize,&local,&device));
  CALL(nvnCommandBufferAddCommandMemory,&local,&pool,W,W);
  vk_pool_requirement need=pool_window_need(storage,W,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0);
  vk_pool_reserve_result reserved;CHECK(vk_pool_reserve_ranges(&need,1,W,&reserved)==VK_POOL_RESERVE_OK);
  uint64_t snapshots,published,fallbacks;int original_reference,active_reference;
  vk_cpu_copy_stats(&snapshots,&published,&fallbacks,&original_reference);
  vk_pool_copy_stats a,b;vk_pool_copy_snapshot(&a);int accounting=a.enabled;
  unsigned borrowed_updates=0,copied_updates=0;
  for(unsigned reference=0;reference<2;reference++) {
    vk_cpu_copy_reference(reference);
    dm_guest_write_prepare(storage+128,sizeof expected);memset(storage+128,0x37,sizeof expected);memset(expected,0x37,sizeof expected);
    mtl_sub_begin();pool_window_read(storage+128,sizeof out,out);vk_internal_wait(vk_internal_commit());CHECK(!memcmp(out,expected,sizeof out));
    for(unsigned k=0;k<sizeof seed;k++)seed[k]=(uint8_t)(0x81+k*7+reference);memcpy(expected+8,seed,sizeof seed);
    CALL(nvnCommandBufferBeginRecording,&local);
    CALL(nvnCommandBufferUpdateUniformBuffer,&local,(uintptr_t)(storage+128),128,8,sizeof seed,seed);
    NVNcommandHandle h=CALL(nvnCommandBufferEndRecording,&local);
    memset(seed,0,sizeof seed); // NVN records its own immutable caller snapshot.
    VkSemaphore gate=VK_NULL_HANDLE;if(vk_async_submissions()&&V.timeline_semaphore)gate=pool_copy_gate();
    vk_pool_copy_snapshot(&a);uint64_t before_snapshots,before_published;
    vk_cpu_copy_stats(&before_snapshots,&before_published,&fallbacks,&active_reference);
    mtl_sub_begin();mtl_exec((const void *)(uintptr_t)h);uint64_t serial=vk_internal_commit();
    vk_cpu_copy_stats(&snapshots,&published,&fallbacks,&active_reference);
    uint64_t allocated=snapshots-before_snapshots;
    CHECK(reference?allocated==0:(allocated==0 || allocated==sizeof seed));
    if(!reference) {if(allocated)copied_updates++;else borrowed_updates++;}
    CHECK(published-before_published==(reference?0:sizeof seed));
    vk_pool_copy_snapshot(&b);CHECK(b.known_snapshot_bytes-a.known_snapshot_bytes==(accounting?allocated:0));
    if(gate) {
      CHECK(b.known_guest_completed_bytes==a.known_guest_completed_bytes);
      CHECK(b.readback_guest_completed_bytes==a.readback_guest_completed_bytes);
      CHECK(storage[136]==0x37);pool_copy_signal(&gate);
    }
    vk_internal_wait(serial);if(gate)vkDestroySemaphore(V.device,gate,NULL);
    CHECK(!memcmp(storage+128,expected,sizeof expected));vk_pool_copy_snapshot(&b);
    CHECK(b.known_guest_completed_bytes-a.known_guest_completed_bytes==(accounting&&!reference?sizeof seed:0));
    CHECK(b.known_shadow_completed_bytes-a.known_shadow_completed_bytes==(accounting&&!reference?sizeof seed:0));
    CHECK(b.readback_guest_completed_bytes-a.readback_guest_completed_bytes==(accounting&&reference?sizeof seed:0));
    mtl_sub_begin();pool_window_read(storage+128,sizeof out,out);vk_internal_wait(vk_internal_commit());CHECK(!memcmp(out,expected,sizeof out));
    // An actual newer partial GPU writer invalidates known coverage. Later
    // neighboring CPU edits still compare against the completed shadow baseline.
    CALL(nvnCommandBufferBeginRecording,&local);memset(seed,0x66,sizeof seed);memcpy(expected+8,seed,sizeof seed);
    CALL(nvnCommandBufferUpdateUniformBuffer,&local,(uintptr_t)(storage+128),128,8,sizeof seed,seed);h=CALL(nvnCommandBufferEndRecording,&local);
    mtl_sub_begin();mtl_exec((const void *)(uintptr_t)h);uint32_t gpu=0x1234abcd;
    pool_window_fill(storage+144,4,gpu);vk_buffer_writeback_stage((uintptr_t)(storage+144),4,VK_PIPELINE_STAGE_TRANSFER_BIT);
    memcpy(expected+16,&gpu,4);vk_internal_wait(vk_internal_commit());CHECK(!memcmp(storage+128,expected,sizeof expected));
    dm_guest_write_prepare(storage+132,4);uint32_t cpu=0xbad00bad;memcpy(storage+132,&cpu,4);memcpy(expected+4,&cpu,4);
    mtl_sub_begin();pool_window_read(storage+128,sizeof out,out);vk_internal_wait(vk_internal_commit());CHECK(!memcmp(out,expected,sizeof out));
  }
  vk_cpu_copy_reference(0);
  // Arbitrary bytes keep the copying API. Borrow rejects a copied wrapper and
  // an older temporary, without recording publication or changing epochs.
  mtl_sub_begin();vk_buffer *older=vk_temp_buffer(64),*private=vk_temp_buffer(64);vk_buffer copied=*private;
  uint64_t epoch=V.buffer_epoch;CHECK(!vk_buffer_writeback_known_temporary((uintptr_t)(storage+256),64,&copied));
  CHECK(!vk_buffer_writeback_known_temporary((uintptr_t)(storage+256),64,older));
  CHECK(!vk_buffer_writeback_known_temporary((uintptr_t)(storage+256),65,private));CHECK(V.buffer_epoch==epoch);
  uint32_t arbitrary[16];for(unsigned k=0;k<16;k++)arbitrary[k]=0x7698abcd;
  pool_window_fill(storage+256,64,arbitrary[0]);vk_pool_copy_snapshot(&a);
  vk_buffer_writeback_known((uintptr_t)(storage+256),64,arbitrary);memset(arbitrary,0,sizeof arbitrary);
  vk_internal_wait(vk_internal_commit());vk_pool_copy_snapshot(&b);
  CHECK(b.known_snapshot_bytes-a.known_snapshot_bytes==(accounting?64:0));
  for(unsigned k=0;k<16;k++){uint32_t word;memcpy(&word,storage+256+4*k,4);CHECK(word==0x7698abcd);}
  if(vk_async_submissions()&&V.timeline_semaphore) {
    // An earlier blocked submission owns its source until effects finish;
    // another recording cannot borrow it. The source remains readable by the
    // genuine old effects and is recycled only after their real completion.
    VkSemaphore gate=pool_copy_gate();mtl_sub_begin();vk_buffer *source=vk_temp_buffer(64);memset(source->map,0x5a,64);
    pool_window_fill(storage+256,64,0x5a5a5a5a);
    int cached=(source->properties&(VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT))==
        (VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    int borrowed=vk_buffer_writeback_known_temporary((uintptr_t)(storage+256),64,source);CHECK(borrowed==cached);
    if(!borrowed)vk_buffer_writeback_known((uintptr_t)(storage+256),64,source->map);
    uint64_t first=vk_internal_commit();CHECK(vk_completed_serial()<first);
    mtl_sub_begin();epoch=V.buffer_epoch;CHECK(!vk_buffer_writeback_known_temporary((uintptr_t)(storage+320),64,source));CHECK(V.buffer_epoch==epoch);
    vk_buffer *other=vk_temp_buffer(64);memset(other->map,0xc3,64);uint64_t second=vk_internal_commit();
    pool_copy_signal(&gate);vk_internal_wait(second);vkDestroySemaphore(V.device,gate,NULL);
    for(unsigned k=0;k<64;k++)CHECK(storage[256+k]==0x5a);
    // Force normal reset/reclaim/reuse; no old copy effect can observe this fill.
    mtl_sub_begin();other=vk_temp_buffer(64);memset(other->map,0x19,64);vk_internal_wait(vk_internal_commit());
    for(unsigned k=0;k<64;k++)CHECK(storage[256+k]==0x5a);
  }
  vk_cpu_copy_reference(original_reference);CALL(nvnCommandBufferFinalize,&local);CALL(nvnMemoryPoolFinalize,&pool);free(storage);mtl_finish();
  LOG("[retained UPDATE_UB] NVN snapshot, cached/coherent retained source, fenced effects, generic fallback, partial writer and CPU-shadow baseline passed; borrowed_updates=%u copied_updates=%u",borrowed_updates,copied_updates);
}
#else
static void retained_update_test(void) {}
#endif
