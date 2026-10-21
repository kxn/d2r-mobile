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
#ifndef DM_ANDROID_APP
// The real GPU/effect/reset gates establish states, without elapsed timing or
// production waiter instrumentation. The host runner bounds a lost wakeup.
typedef struct {
  pthread_mutex_t mu;
  pthread_cond_t cv;
  uint64_t fence;
  uint32_t readback[64],published;
  unsigned started,entered,release;
} guest_fence_wait_context;
typedef struct {guest_fence_wait_context *shared;uint64_t timeout;int done,valid;} guest_fence_wait_result;
static void guest_fence_wait_effect(void *opaque) {
  guest_fence_wait_context *c=opaque;
  pthread_mutex_lock(&c->mu);c->entered=1;pthread_cond_broadcast(&c->cv);
  while(!c->release)pthread_cond_wait(&c->cv,&c->mu);
  c->published=UINT32_C(0x49ab32cd);
  pthread_mutex_unlock(&c->mu);
}
static void *guest_fence_wait_thread(void *opaque) {
  guest_fence_wait_result *r=opaque;guest_fence_wait_context *c=r->shared;
  pthread_mutex_lock(&c->mu);c->started++;pthread_cond_broadcast(&c->cv);
  pthread_mutex_unlock(&c->mu);
  r->done=mtl_wait(c->fence,r->timeout);
  // The completed predicate's publication orders the real CPU effects. Do
  // not use our fixture lock as a substitute for that happens-before edge.
  r->valid=c->published==UINT32_C(0x49ab32cd);
  for(unsigned n=0;n<64;n++)r->valid&=c->readback[n]==UINT32_C(0x12349876);
  return NULL;
}
static void guest_fence_wait_scope_test(void) {
  CHECK(!V.cmd && V.timeline_semaphore);mtl_finish();
  guest_fence_wait_context c={.mu=PTHREAD_MUTEX_INITIALIZER,.cv=PTHREAD_COND_INITIALIZER};
  VkSemaphore gate=pool_copy_gate();vk_command_reset_test_gate(1);
  mtl_sub_begin();uint64_t serial=vk_recording_serial();
  vk_buffer *source=vk_temp_buffer(sizeof c.readback);
  vkCmdFillBuffer(V.cmd,source->handle,0,sizeof c.readback,UINT32_C(0x12349876));
  vk_barrier();vk_effect_copy(c.readback,source->map,sizeof c.readback);
  vk_completion(guest_fence_wait_effect,&c);c.fence=mtl_sub_commit();
  CHECK(!mtl_wait(c.fence,0));CHECK(!mtl_wait(c.fence,1));
  guest_fence_wait_result results[2]={{.shared=&c,.timeout=UINT64_MAX},{.shared=&c,.timeout=30000000000ull}};pthread_t waiters[2];
  for(unsigned n=0;n<2;n++)CHECK(!pthread_create(&waiters[n],NULL,guest_fence_wait_thread,&results[n]));
  pthread_mutex_lock(&c.mu);while(c.started!=2)pthread_cond_wait(&c.cv,&c.mu);pthread_mutex_unlock(&c.mu);
  pool_copy_signal(&gate);
  pthread_mutex_lock(&c.mu);while(!c.entered)pthread_cond_wait(&c.cv,&c.mu);
  CHECK(!c.published);pthread_mutex_unlock(&c.mu);
  // GPU/copy finished, but the CPU callback has not published completion.
  CHECK(!mtl_wait(c.fence,0));CHECK(!mtl_wait(c.fence,1));
  pthread_mutex_lock(&c.mu);c.release=1;pthread_cond_broadcast(&c.cv);pthread_mutex_unlock(&c.mu);
  for(unsigned n=0;n<2;n++){CHECK(!pthread_join(waiters[n],NULL));CHECK(results[n].done&&results[n].valid);}
  CHECK(mtl_wait(c.fence,0));CHECK(mtl_wait(c.fence,1));CHECK(mtl_wait(c.fence,UINT64_MAX));
  CHECK(vk_command_reset_test_wait_blocked(serial)==serial);
  uint64_t done,ready;unsigned sets;vk_command_reset_test_snapshot(&done,&ready,&sets);
  CHECK(done==serial && ready<serial); // completion is not reset readiness
  vk_command_reset_test_gate(0);mtl_finish();vkDestroySemaphore(V.device,gate,NULL);
  CHECK(!pthread_cond_destroy(&c.cv));CHECK(!pthread_mutex_destroy(&c.mu));
  LOG("[guest fence wait scope fixture] pending zero/finite timeouts, finite/infinite waiters, real GPU bytes+CPUeffect before completion and reset-independent return passed");
}
#endif
static void command_pool_reset_test(void) {
  CHECK(V.cmd==VK_NULL_HANDLE);
  mtl_finish();
#ifndef DM_ANDROID_APP
  guest_fence_wait_scope_test();
#endif
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
