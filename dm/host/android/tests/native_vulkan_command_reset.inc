// A real GPU/effects fence must become visible while its exclusive command
// pool is still unavailable. The logical gate covers that ownership gap; it
// is not a timer or a replacement for vendor profiling.
#ifdef DM_VULKAN_TEST_EMBED
extern void vk_command_reset_test_gate(int enabled);
extern uint64_t vk_command_reset_test_wait_blocked(uint64_t serial);
extern uint64_t vk_command_reset_test_release_when_waiting(int pressure);
extern void vk_command_reset_test_snapshot(uint64_t *done,uint64_t *ready,unsigned *sets);
typedef struct {
  int pressure;
  uint64_t observed;
} command_reset_release;
static void *command_reset_release_waiter(void *context) {
  command_reset_release *release=context;
  release->observed=vk_command_reset_test_release_when_waiting(release->pressure);
  return NULL;
}
static uint64_t command_reset_fill(uint8_t *data,unsigned slot,uint32_t value,
                                   uint64_t *queue_serial) {
  uint64_t address=(uintptr_t)(data+4096+slot*64),offset;
  mtl_sub_begin();
  *queue_serial=vk_recording_serial();
  vk_buffer *buffer;
  CHECK(mtl_addr(address,16,(void **)&buffer,&offset));
  vkCmdFillBuffer(V.cmd,buffer->handle,offset,16,value);
  vk_buffer_transfer_written_range(address,16);
  vk_buffer_writeback(address,16);
  return mtl_sub_commit();
}
static void command_reset_pixels(const uint8_t *data,unsigned slots,uint32_t base) {
  for(unsigned slot=0;slot<slots;slot++)
    for(unsigned word=0;word<16;word++) {
      uint32_t value;memcpy(&value,data+4096+slot*64+word*4,4);
      CHECK(value==(word<4?base+slot:UINT32_C(0xa5a5a5a5)));
    }
  for(unsigned n=0;n<64;n++)CHECK(data[4096-64+n]==0xa5);
  for(unsigned n=0;n<64;n++)CHECK(data[4096+slots*64+n]==0xa5);
}
static void command_pool_reset_test(void) {
  CHECK(V.cmd==VK_NULL_HANDLE);
  mtl_finish();
  uint8_t *data;CHECK(!posix_memalign((void **)&data,65536,65536));
  memset(data,0xa5,65536);
  NVNmemoryPoolBuilder builder;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&builder);
  CALL(nvnMemoryPoolBuilderSetDevice,&builder,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&builder,data,65536);
  CALL(nvnMemoryPoolBuilderSetFlags,&builder,
       NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&builder));

  // No pending submission remains after this single effects publication, but
  // its pool cannot be reused until the worker returns from reset. Ordinary
  // guest/internal waits must return; explicit Finish must still wait ready.
  vk_command_reset_test_gate(1);
  uint64_t serial,guest=command_reset_fill(data,0,UINT32_C(0x91ab1234),&serial);
  CHECK(mtl_wait(guest,UINT64_MAX));
  vk_internal_wait(serial);
  CHECK(vk_command_reset_test_wait_blocked(serial)==serial);
  uint64_t done,ready;unsigned sets;
  vk_command_reset_test_snapshot(&done,&ready,&sets);
  CHECK(done==serial&&ready<serial&&sets<=16);
  command_reset_pixels(data,1,UINT32_C(0x91ab1234));
  command_reset_release release={0};pthread_t helper;
  CHECK(!pthread_create(&helper,NULL,command_reset_release_waiter,&release));
  mtl_finish();
  CHECK(!pthread_join(helper,NULL));CHECK(release.observed==serial);
  vk_command_reset_test_snapshot(&done,&ready,&sets);
  CHECK(done==serial&&ready==serial&&sets<=16);

  if(vk_async_submissions()) {
    // Consume all 16 bounded contexts while the first completed pool remains
    // inside the reset gate. The 17th begin must wait the ready predicate, not
    // queue completion or an empty pending list. Only the fixture helper
    // releases the gate after observing that actual cap wait.
    vk_command_reset_test_gate(1);
    const uint32_t base=UINT32_C(0x6ac30000);
    uint64_t first=0,last=0;
    for(unsigned slot=0;slot<16;slot++) {
      command_reset_fill(data,slot,base+slot,&last);
      if(!slot) {
        first=last;
        CHECK(vk_command_reset_test_wait_blocked(first)==first);
      }
    }
    vk_command_reset_test_snapshot(&done,&ready,&sets);
    CHECK(done==first&&ready<first&&sets==16);
    release=(command_reset_release){.pressure=1};
    CHECK(!pthread_create(&helper,NULL,command_reset_release_waiter,&release));
    guest=command_reset_fill(data,16,base+16,&last);
    CHECK(!pthread_join(helper,NULL));CHECK(release.observed==first);
    CHECK(mtl_wait(guest,UINT64_MAX));mtl_finish();
    vk_command_reset_test_snapshot(&done,&ready,&sets);
    CHECK(done==last&&ready==last&&sets==16);
    command_reset_pixels(data,17,base);
  }
  // Subsequent recording must use properly reset primary/secondary ownership;
  // actual GPU bytes and untouched neighbors stay valid through pool teardown.
  command_reset_fill(data,0,UINT32_C(0x5b4a3021),&serial);mtl_finish();
  for(unsigned word=0;word<4;word++) {
    uint32_t value;memcpy(&value,data+4096+word*4,4);
    CHECK(value==UINT32_C(0x5b4a3021));
  }
  CALL(nvnMemoryPoolFinalize,&pool);free(data);
  LOG("[vulkan command reset fixture] GPU/effects visible before reset, Finish waits ready, bounded cap16 ownership gap and subsequent GPU bytes passed: async=%d",vk_async_submissions());
}
#else
static void command_pool_reset_test(void) {
  LOG("[vulkan command reset fixture] skipped: test embedding unavailable");
}
#endif
