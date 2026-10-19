// Explicitly owned NVN pool: legal shared byte synchronization, not an alias
// of game storage and not a test of concurrent same-byte snapshot semantics.
#ifdef DM_VULKAN_TEST_EMBED
extern void vk_test_pool_host_limits(uint64_t,uint64_t);
extern void vk_test_pool_host_fail_once(void);
extern uint64_t vk_test_pool_host_attempts(void);
extern int vk_test_pool_reserve_metadata(uint64_t);
static void pool_host_read(uint8_t *source,unsigned size,uint8_t *result) {
  vk_pool_requirement need={(uintptr_t)source,size,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0};
  int operation=vk_pool_operation_begin_ranges(&need,1);
  // This helper writes a recycled fixture readback temporary, outside the
  // production draw/copy dependency graph. Order its previous transfer write.
  VkMemoryBarrier recycled={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
    0,1,&recycled,0,NULL,0,NULL);
  pool_window_read(source,size,result);vk_pool_operation_end(operation);
}
static void pool_host_fill(uint8_t *destination,unsigned size,uint32_t value) {
  vk_pool_requirement need={(uintptr_t)destination,size,0,VK_PIPELINE_STAGE_TRANSFER_BIT,1,0};
  int operation=vk_pool_operation_begin_ranges(&need,1);
  pool_window_fill(destination,size,value);vk_pool_operation_end(operation);
}
static void pool_host_prebind_test(void) {
  CHECK(V.options.pool_host_prebind);vk_test_pool_host_limits(4096,8192);
  uint8_t *storage=mmap(NULL,65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
  CHECK(storage!=MAP_FAILED);memset(storage,0x41,65536);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;NVNcommandBuffer local;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,65536);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_UNCACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));CHECK(CALL(nvnCommandBufferInitialize,&local,&device));
  CALL(nvnCommandBufferAddCommandMemory,&local,&pool,32768,16384);
  CALL(nvnCommandBufferBeginRecording,&local);
  CALL(nvnCommandBufferBindUniformBuffer,&local,0,0,(uintptr_t)storage+128,256);
  NVNbufferRange vertex={(uintptr_t)storage+4096+64,128};
  CALL(nvnCommandBufferBindVertexBuffers,&local,0,1,&vertex);
  CALL(nvnCommandBufferBindStorageBuffer,&local,0,0,(uintptr_t)storage+8192,128);
  CALL(nvnCommandBufferBindUniformBuffer,&local,0,1,(uintptr_t)storage+12288,128);
  CALL(nvnCommandBufferBindUniformBuffer,&local,0,2,(uintptr_t)storage+16384,8192);
  NVNseparateTextureHandle texture={0};
  CALL(nvnCommandBufferBindSeparateTextures,&local,0,0,1,&texture);
  NVNcommandHandle handle=CALL(nvnCommandBufferEndRecording,&local);
  CHECK(vk_pool_host_pinned_bytes()==0);CHECK(vk_test_pool_reserve_metadata((uintptr_t)storage)==0);
  uint64_t attempts=vk_test_pool_host_attempts();
  mtl_sub_begin();mtl_exec((const void *)(uintptr_t)handle);vk_internal_wait(vk_internal_commit());
  CHECK(vk_pool_host_pinned_bytes()==8192);CHECK(vk_test_pool_host_attempts()==attempts+2);
  CHECK(vk_pool_host_imported_range((uintptr_t)storage,4096));
  CHECK(vk_pool_host_imported_range((uintptr_t)storage+4096,4096));
  CHECK(!vk_pool_host_imported_range((uintptr_t)storage+8192,4096));
  CHECK(!vk_pool_host_imported_range((uintptr_t)storage+12288,4096));
  CHECK(!vk_pool_host_imported_range((uintptr_t)storage+16384,8192));
  CHECK(vk_test_pool_reserve_metadata((uintptr_t)storage)==0);
  // No Draw was recorded: these explicitly bound buffers still consume pins.
  // The same replay cannot duplicate them or eagerly create mirror metadata.
  mtl_sub_begin();mtl_exec((const void *)(uintptr_t)handle);vk_internal_wait(vk_internal_commit());
  CHECK(vk_test_pool_host_attempts()==attempts+2 && vk_pool_host_pinned_bytes()==8192);
  CHECK(vk_test_pool_reserve_metadata((uintptr_t)storage)==0);
  CALL(nvnCommandBufferFinalize,&local);
  CHECK(vk_pool_host_pinned_bytes()==8192);
  // Protection now occurs only AFTER successful writable pin, through the
  // original dirty API, followed by a legal CPU publish and actual GPU read.
  nvn_dirty_protect(storage,4096);dm_guest_write_prepare(storage+128,32);
  memset(storage+128,0x6d,32);uint8_t out[256],expected[256];memcpy(expected,storage+128,256);
  mtl_sub_begin();pool_host_read(storage+128,256,out);vk_internal_wait(vk_internal_commit());
  CHECK(!memcmp(out,expected,256));CHECK(vk_test_pool_host_attempts()==attempts+2);
  // A later writer still uses the original sticky demotion and byte oracle.
  mtl_sub_begin();pool_host_fill(storage+128,64,0x76543210);
  vk_buffer_writeback_stage((uintptr_t)storage+128,64,VK_PIPELINE_STAGE_TRANSFER_BIT);
  vk_internal_wait(vk_internal_commit());
  for(unsigned k=0;k<16;k++)CHECK(((uint32_t *)(storage+128))[k]==0x76543210);
  CHECK(!vk_pool_host_imported_range((uintptr_t)storage,4096));
  CHECK(vk_pool_host_pinned_bytes()==4096);
  CALL(nvnMemoryPoolFinalize,&pool);CHECK(vk_pool_host_pinned_bytes()==0);
  // Failed pre-registration does not create ordinary buffers, shadow or dirty
  // metadata; sticky rejection survives replay, then ordinary use falls back.
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  CHECK(CALL(nvnCommandBufferInitialize,&local,&device));
  CALL(nvnCommandBufferAddCommandMemory,&local,&pool,32768,16384);
  CALL(nvnCommandBufferBeginRecording,&local);
  CALL(nvnCommandBufferBindUniformBuffer,&local,0,0,(uintptr_t)storage+16384,128);
  handle=CALL(nvnCommandBufferEndRecording,&local);attempts=vk_test_pool_host_attempts();
  vk_test_pool_host_fail_once();
  mtl_sub_begin();mtl_exec((const void *)(uintptr_t)handle);vk_internal_wait(vk_internal_commit());
  CHECK(vk_test_pool_host_attempts()==attempts+1 && vk_pool_host_pinned_bytes()==0);
  CHECK(vk_test_pool_reserve_metadata((uintptr_t)storage)==0);
  mtl_sub_begin();mtl_exec((const void *)(uintptr_t)handle);vk_internal_wait(vk_internal_commit());
  CHECK(vk_test_pool_host_attempts()==attempts+1 && vk_pool_host_pinned_bytes()==0);
  CHECK(vk_test_pool_reserve_metadata((uintptr_t)storage)==0);
  vk_pool_requirement need={(uintptr_t)storage+16384,128,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0};
  vk_pool_reserve_result reserved;
  CHECK(vk_pool_reserve_ranges(&need,1,4096,&reserved)==VK_POOL_RESERVE_OOM);
  CHECK(vk_pool_reserve_ranges(&need,1,4096,&reserved)==VK_POOL_RESERVE_OK);
  CHECK(vk_test_pool_host_attempts()==attempts+1);
  mtl_sub_begin();pool_host_read(storage+16384,128,out);vk_internal_wait(vk_internal_commit());
  CHECK(!memcmp(out,storage+16384,128));
  CALL(nvnCommandBufferFinalize,&local);CALL(nvnMemoryPoolFinalize,&pool);
  CHECK(vk_pool_host_pinned_bytes()==0);
  // An earlier image-only protection can already have happened before the
  // first translated Buffer bind. This hint never relaxes it for a GUP retry.
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  CHECK(CALL(nvnCommandBufferInitialize,&local,&device));
  CALL(nvnCommandBufferAddCommandMemory,&local,&pool,32768,16384);
  CALL(nvnCommandBufferBeginRecording,&local);
  CALL(nvnCommandBufferBindUniformBuffer,&local,0,0,(uintptr_t)storage+20480,128);
  handle=CALL(nvnCommandBufferEndRecording,&local);
  nvn_dirty_protect(storage+20480,4096);attempts=vk_test_pool_host_attempts();
  mtl_sub_begin();mtl_exec((const void *)(uintptr_t)handle);vk_internal_wait(vk_internal_commit());
  CHECK(vk_test_pool_host_attempts()==attempts+1);
  CHECK(vk_test_pool_reserve_metadata((uintptr_t)storage)==0);
  int protected_imported=vk_pool_host_imported_range((uintptr_t)storage+20480,4096);
  mtl_sub_begin();mtl_exec((const void *)(uintptr_t)handle);vk_internal_wait(vk_internal_commit());
  CHECK(vk_test_pool_host_attempts()==attempts+1);
  CHECK(vk_test_pool_reserve_metadata((uintptr_t)storage)==0);
  mtl_sub_begin();pool_host_read(storage+20480,128,out);vk_internal_wait(vk_internal_commit());
  CHECK(!memcmp(out,storage+20480,128));CHECK(vk_test_pool_host_attempts()==attempts+1);
  printf("{\"test\":\"readonly-prebind-already-protected\",\"imported\":%s,\"attempts\":1,\"retry_imports\":0}\n",
    protected_imported?"true":"false");
  CALL(nvnCommandBufferFinalize,&local);CALL(nvnMemoryPoolFinalize,&pool);
  CHECK(vk_pool_host_pinned_bytes()==0);
  vk_test_pool_host_limits(16<<20,32<<20);CHECK(munmap(storage,65536)==0);
  CHECK(vk_validation_error_count()==0);
  printf("{\"test\":\"readonly-translation-prebind\",\"recording_pins\":0,\"translation_pins\":8192,\"eager_mirror_metadata\":false,\"result\":\"passed\"}\n");
}
static void pool_host_readonly_test(void) {
  mtl_init();
  CHECK(V.options.pool_host_readonly && V.options.pool_window_bytes==4096);
  vk_test_pool_host_limits(4096,8192);
  uint8_t *storage=mmap(NULL,65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
  CHECK(storage!=MAP_FAILED);for(unsigned k=0;k<65536;k++)storage[k]=(uint8_t)(k*13+17);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;CALL(nvnMemoryPoolBuilderSetDefaults,&mb);
  CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,65536);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_UNCACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));CHECK(vk_pool_host_pinned_bytes()==0);
  vk_pool_copy_stats copy_before,copy_after;vk_pool_copy_snapshot(&copy_before);
  uint8_t out[512],expected[512];memcpy(expected,storage+128,256);
  VkSemaphore gate=VK_NULL_HANDLE;
  if(vk_async_submissions()&&V.timeline_semaphore)gate=pool_copy_gate();
  mtl_sub_begin();pool_host_read(storage+128,256,out);uint64_t serial=vk_internal_commit();
  CHECK(vk_pool_host_imported_range((uintptr_t)storage,4096));CHECK(vk_pool_host_pinned_bytes()==4096);
  vk_pool_copy_snapshot(&copy_after);
  if(copy_before.enabled) {
    CHECK(copy_after.initial_mirror_bytes-copy_before.initial_mirror_bytes==0);
    CHECK(copy_after.initial_shadow_bytes-copy_before.initial_shadow_bytes==4096);
  }
  printf("{\"test\":\"readonly-first-copy-bytes\",\"accounting\":%s,\"mirror\":%llu,\"shadow\":%llu}\n",
    copy_before.enabled?"true":"false",(unsigned long long)(copy_after.initial_mirror_bytes-copy_before.initial_mirror_bytes),
    (unsigned long long)(copy_after.initial_shadow_bytes-copy_before.initial_shadow_bytes));
  // CPU may edit unrelated bytes on the same physical page while the GPU
  // reads this immutable interval. No whole-page CPU/GPU handoff is needed.
  dm_guest_write_prepare(storage+1024,4);memset(storage+1024,0x7b,4);
  if(gate)pool_copy_signal(&gate);
  vk_internal_wait(serial);if(gate)vkDestroySemaphore(V.device,gate,NULL);CHECK(!memcmp(out,expected,256));
  // Successful writable pin -> original dirty protect -> host write-prepare
  // is a different path from importing an already protected mapping. The
  // original imported handle must remain; no GUP retry or replacement allowed.
  vk_pool_span_info pinned_before,pinned_after;
  CHECK(vk_pool_span_query((uintptr_t)storage+128,256,&pinned_before)==VK_POOL_SPAN_SINGLE);
  uint64_t successful_attempts=vk_test_pool_host_attempts(),dirty_stamp=nvn_dirty_now();
  nvn_dirty_protect(storage,4096);
  // Overlapping CPU edits are made only AFTER completion, then published by
  // the subsequent ordinary QueueSubmit, exactly as shared NoCopy requires.
  dm_guest_write_prepare(storage+128,32);memset(storage+128,0xa6,32);memcpy(expected,storage+128,256);
  mtl_sub_begin();pool_host_read(storage+128,256,out);vk_internal_wait(vk_internal_commit());
  CHECK(!memcmp(out,expected,256));CHECK(vk_pool_host_pinned_bytes()==4096);
  CHECK(nvn_dirty_since(storage,4096,dirty_stamp));
  CHECK(vk_pool_span_query((uintptr_t)storage+128,256,&pinned_after)==VK_POOL_SPAN_SINGLE);
  CHECK(pinned_before.handle==pinned_after.handle &&
    pinned_before.backing_generation==pinned_after.backing_generation);
  CHECK(vk_test_pool_host_attempts()==successful_attempts);
  // Also exercise an ordinary guest-style CPU store through the original
  // dirty SIGSEGV hook. No custom fault handler, page remap or private copy.
  dirty_stamp=nvn_dirty_now();nvn_dirty_protect(storage,4096);
  storage[128]^=0x37;storage[159]^=0x6a;memcpy(expected,storage+128,256);
  CHECK(nvn_dirty_since(storage,4096,dirty_stamp));
  mtl_sub_begin();pool_host_read(storage+128,256,out);vk_internal_wait(vk_internal_commit());
  CHECK(!memcmp(out,expected,256));
  CHECK(vk_pool_span_query((uintptr_t)storage+128,256,&pinned_after)==VK_POOL_SPAN_SINGLE);
  CHECK(pinned_before.handle==pinned_after.handle &&
    pinned_before.backing_generation==pinned_after.backing_generation);
  CHECK(vk_test_pool_host_attempts()==successful_attempts && vk_pool_host_pinned_bytes()==4096);
  printf("{\"test\":\"readonly-successful-pin-dirty-cycle\",\"host_prepare_cycles\":1,\"fault_cycles\":1,\"same_backing_handle\":true,\"repeat_imports\":0}\n");
  // A real writable requirement demotes before obtaining its destination
  // handle; subsequent legacy known/readback publication remains unchanged.
  mtl_sub_begin();pool_host_fill(storage+128,64,0x12345678);
  CHECK(!vk_pool_host_imported_range((uintptr_t)storage,4096));CHECK(vk_pool_host_pinned_bytes()==0);
  vk_buffer_writeback_stage((uintptr_t)storage+128,64,VK_PIPELINE_STAGE_TRANSFER_BIT);
  vk_internal_wait(vk_internal_commit());for(unsigned k=0;k<16;k++)CHECK(((uint32_t *)(storage+128))[k]==0x12345678);
  // Readonly source -> ordinary destination cannot use its intentionally stale
  // upload shadow as a known-copy proof; the original GPU byte oracle remains.
  mtl_sub_begin();pool_host_read(storage+4096+64,128,out);vk_internal_wait(vk_internal_commit());
  CHECK(vk_pool_host_imported_range((uintptr_t)storage+4096,4096));
  dm_guest_write_prepare(storage+4096+64,32);memset(storage+4096+64,0xe1,32);
  memcpy(expected,storage+4096+64,128);
  mtl_sub_begin();mtl_sub_copy((uintptr_t)storage+4096+64,(uintptr_t)storage+256,128);
  vk_internal_wait(vk_internal_commit());CHECK(!memcmp(storage+256,expected,128));
  // A delayed host event destination is also a writer. It must become a
  // mirror before recording the effect, not overwrite directly shared bytes.
  mtl_sub_begin();mtl_sub_event((uint32_t *)(storage+4096+64),0,0x98765432);
  CHECK(!vk_pool_host_imported_range((uintptr_t)storage+4096,4096));
  vk_internal_wait(vk_internal_commit());CHECK(*(uint32_t *)(storage+4096+64)==0x98765432);
  // Query report snapshots are another host-completion destination. Their
  // admission happens before recording/capturing the query source objects.
  mtl_sub_begin();pool_host_read(storage+32768+128,64,out);vk_internal_wait(vk_internal_commit());
  CHECK(vk_pool_host_imported_range((uintptr_t)storage+32768,4096));
  uint8_t query_guard[32];memcpy(query_guard,storage+32768+248,32);
  mtl_sub_begin();mtl_sub_counter((uintptr_t)storage+32768+256,NVN_COUNTER_TYPE_TIMESTAMP);
  CHECK(!vk_pool_host_imported_range((uintptr_t)storage+32768,4096));
  vk_internal_wait(vk_internal_commit());
  CHECK(!memcmp(query_guard,storage+32768+248,8));
  CHECK(!memcmp(query_guard+24,storage+32768+272,8));
  // Lifetime references keep the imported mapping through queued real work;
  // pool removal frees only VkDeviceMemory and leaves our original mmap alive.
  mtl_sub_begin();pool_host_read(storage+8192+32,128,out);serial=vk_internal_commit();
  CHECK(vk_pool_host_imported_range((uintptr_t)storage+8192,4096));
  vk_internal_wait(serial);
  mtl_sub_begin();pool_host_read(storage+12288+32,128,out);vk_internal_wait(vk_internal_commit());
  CHECK(vk_pool_host_pinned_bytes()==8192);
  mtl_sub_begin();pool_host_read(storage+16384+32,128,out);vk_internal_wait(vk_internal_commit());
  CHECK(!vk_pool_host_imported_range((uintptr_t)storage+16384,4096));CHECK(vk_pool_host_pinned_bytes()==8192);
  CALL(nvnMemoryPoolFinalize,&pool);CHECK(vk_pool_host_pinned_bytes()==0);
  // Import failure followed by mirror OOM leaves no backing and returns the
  // pin reservation. A retry must be sticky mirror, not a second GUP attempt.
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  uint64_t attempts=vk_test_pool_host_attempts();vk_test_pool_host_fail_once();
  vk_pool_requirement need={(uintptr_t)storage+24576,128,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0};
  vk_pool_reserve_result reserved;
  CHECK(vk_pool_reserve_ranges(&need,1,4096,&reserved)==VK_POOL_RESERVE_OOM);
  CHECK(vk_pool_host_pinned_bytes()==0 && vk_test_pool_host_attempts()==attempts+1);
  CHECK(vk_pool_reserve_ranges(&need,1,4096,&reserved)==VK_POOL_RESERVE_OK);
  CHECK(vk_pool_host_pinned_bytes()==0 && vk_test_pool_host_attempts()==attempts+1);
  mtl_sub_begin();pool_host_read(storage+24576,128,out);vk_internal_wait(vk_internal_commit());
  CHECK(!memcmp(out,storage+24576,128));
  mtl_sub_begin();pool_host_read(storage+28672,128,out);serial=vk_internal_commit();
  CHECK(vk_pool_host_imported_range((uintptr_t)storage+28672,4096));
  CALL(nvnMemoryPoolFinalize,&pool);vk_internal_wait(serial);CHECK(vk_pool_host_pinned_bytes()==0);
  // Single-window cap is independent of the cumulative budget. A fresh 8KiB
  // backing cannot import under a 4KiB single-window cap even with zero live.
  vk_test_pool_window_bytes(8192);CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  attempts=vk_test_pool_host_attempts();mtl_sub_begin();pool_host_read(storage+64,128,out);
  vk_internal_wait(vk_internal_commit());CHECK(vk_pool_host_pinned_bytes()==0);
  CHECK(vk_test_pool_host_attempts()==attempts);CALL(nvnMemoryPoolFinalize,&pool);
  vk_test_pool_window_bytes(4096);
  // A new owned NVN pool page is protected through the original dirty API,
  // before its first buffer use. Try writable import once, never unprotect it
  // to make GUP succeed. Drivers may reject or support this mapping; record
  // the real result and prove that mirror/read bytes and retries stay correct.
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  nvn_dirty_protect(storage+40960,4096);attempts=vk_test_pool_host_attempts();
  mtl_sub_begin();pool_host_read(storage+40960,128,out);vk_internal_wait(vk_internal_commit());
  CHECK(!memcmp(out,storage+40960,128));
  int protected_imported=vk_pool_host_imported_range((uintptr_t)storage+40960,4096);
  CHECK(vk_test_pool_host_attempts()==attempts+1);
  mtl_sub_begin();pool_host_read(storage+40960,128,out);vk_internal_wait(vk_internal_commit());
  CHECK(!memcmp(out,storage+40960,128));CHECK(vk_test_pool_host_attempts()==attempts+1);
  printf("{\"test\":\"readonly-protected-owned-page\",\"imported\":%s,\"attempts\":1,\"retry_imports\":0,\"pinned\":%llu}\n",
    protected_imported?"true":"false",(unsigned long long)vk_pool_host_pinned_bytes());
  CALL(nvnMemoryPoolFinalize,&pool);CHECK(vk_pool_host_pinned_bytes()==0);
  vk_test_pool_host_limits(16<<20,32<<20);
  storage[8192]=0x57;CHECK(storage[8192]==0x57);CHECK(munmap(storage,65536)==0);
  CHECK(vk_validation_error_count()==0);
  if(V.options.pool_host_prebind)pool_host_prebind_test();
}
#endif
