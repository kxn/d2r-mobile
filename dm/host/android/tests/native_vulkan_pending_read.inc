// Real GPU producers and host publication exercise the negative metadata proof.
// Query results still read fresh guest bytes; no shader data or wait is cached.
static void pending_read_gpu_write(uint8_t *guest,const uint8_t values[16]) {
  vk_buffer *destination;uint64_t offset;
  CHECK(vk_pool_address_overwrite((uintptr_t)guest,16,(void **)&destination,&offset,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT));
  vk_buffer *source=vk_temp_buffer(16);
  memcpy(source->map,values,16);
  const vk_buffer_use use={(uintptr_t)guest,16,1};vk_transfer_buffer_dependencies(&use,1);
  VkBufferCopy copy={.dstOffset=offset,.size=16};
  vkCmdCopyBuffer(V.cmd,source->handle,destination->handle,1,&copy);
  vk_buffer_transfer_written_range((uintptr_t)guest,16);
  vk_pool_overwritten((uintptr_t)guest,16);
  vk_buffer_writeback_stage((uintptr_t)guest,16,VK_PIPELINE_STAGE_TRANSFER_BIT);
}
static void pending_cpu_read_test(void) {
  CHECK(V.cmd==VK_NULL_HANDLE);mtl_finish();
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,65536));
  uint8_t *target=storage+4096,*unrelated=storage+8192;
  for(unsigned lifetime=0;lifetime<2;lifetime++) {
    dm_guest_write_prepare(storage,65536);memset(storage,0x35+lifetime,65536);
    NVNmemoryPoolBuilder builder;NVNmemoryPool pool;
    CALL(nvnMemoryPoolBuilderSetDefaults,&builder);CALL(nvnMemoryPoolBuilderSetDevice,&builder,&device);
    CALL(nvnMemoryPoolBuilderSetStorage,&builder,storage,65536);
    CALL(nvnMemoryPoolBuilderSetFlags,&builder,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
    CHECK(CALL(nvnMemoryPoolInitialize,&pool,&builder));
    uint8_t first[16],second[16];
    for(unsigned i=0;i<16;i++) { first[i]=0xa0+lifetime+i;second[i]=0xd0+lifetime+i; }
    int gated=vk_async_submissions()&&V.timeline_semaphore;
    VkSemaphore gate=gated?block_gpu_queue():VK_NULL_HANDLE;
    mtl_sub_begin();pending_read_gpu_write(unrelated,first);
    uint64_t initial=vk_internal_commit();
    if(gated)CHECK(vk_completed_serial()<initial);
    mtl_sub_begin();
    // An earlier unrelated producer keeps pending metadata nonempty. Repeated
    // exact queries can share only its negative answer, never the guest value.
    for(unsigned n=0;n<32;n++) {
      uint32_t value=0x3e000000u+lifetime*0x10000u+n,actual=0;
      dm_guest_write_prepare(target,4);memcpy(target,&value,4);
      CHECK(vk_pool_try_read((uintptr_t)target,4,&actual)==1);CHECK(actual==value);
      CHECK(!vk_pool_pending_writes((uintptr_t)target,4));
    }
    // Different sizes/addresses must take their own exact query. These cover
    // byte endpoints without ever interpreting an out-of-pool CPU pointer.
    uint8_t bytes[17];CHECK(vk_pool_try_read((uintptr_t)target+1,3,bytes)==1);
    CHECK(!memcmp(bytes,target+1,3));
    CHECK(vk_pool_try_read((uintptr_t)target,17,bytes)==1);CHECK(!memcmp(bytes,target,17));
    uint32_t actual;CHECK(vk_pool_try_read((uintptr_t)target,4,&actual)==1);
    // The same recording now adds a real writer at the cached address. Its
    // current writeback must still be checked before any old-pending proof.
    pending_read_gpu_write(target,second);
    CHECK(vk_pool_pending_writes((uintptr_t)target,4));
    CHECK(vk_pool_try_read((uintptr_t)target,4,&actual)==0);
    uint64_t produced=vk_internal_commit();
    CHECK(produced>initial);mtl_sub_begin();
    // New internal enqueue and recording ownership invalidate the old proof.
    // The gate makes this positive answer deterministic on the async path.
    if(gated) {
      CHECK(vk_pool_pending_writes((uintptr_t)target,4));
      CHECK(vk_pool_try_read((uintptr_t)target,4,&actual)==0);
      VkSemaphoreSignalInfoKHR signal={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO_KHR,
        .semaphore=gate,.value=1};VK_CHECK(V.signal_timeline(V.device,&signal));
    }
    vk_internal_wait(produced);
    for(unsigned i=0;i<16;i++) {
      CHECK(target[i]==second[i]);CHECK(unrelated[i]==first[i]);
    }
    CHECK(vk_pool_try_read((uintptr_t)target,4,&actual)==1);
    uint32_t expected;memcpy(&expected,second,4);CHECK(actual==expected);
    // A real guest submit and context reuse must not retain CPU values either.
    uint64_t guest=mtl_sub_commit();CHECK(mtl_wait(guest,UINT64_MAX));mtl_finish();
    if(gated)vkDestroySemaphore(V.device,gate,NULL);
    mtl_sub_begin();
    uint32_t value=0x71020304u+lifetime;
    dm_guest_write_prepare(target,4);memcpy(target,&value,4);
    CHECK(vk_pool_try_read((uintptr_t)target,4,&actual)==1);CHECK(actual==value);
    guest=mtl_sub_commit();CHECK(mtl_wait(guest,UINT64_MAX));mtl_finish();
    CALL(nvnMemoryPoolFinalize,&pool);
    // Next lifetime re-registers the exact guest addresses with new bytes.
  }
  free(storage);
  LOG("[vulkan pending CPU read fixture] repeated negative earlier-range proof, fresh CPU bytes, exact address/size, current writer, internal enqueue, guest submit/context reuse and same-address pool recreation passed: async=%d",vk_async_submissions());
}
