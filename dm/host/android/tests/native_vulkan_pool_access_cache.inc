#ifdef DM_VULKAN_TEST_EMBED
extern uintptr_t vk_test_pool_access_bitmap(uint64_t);
extern int vk_test_pool_access_cached(uintptr_t,uint64_t *,unsigned *);
static void access_cache_pool(NVNmemoryPool *pool,uint8_t *data,unsigned size) {
  NVNmemoryPoolBuilder b;CALL(nvnMemoryPoolBuilderSetDefaults,&b);CALL(nvnMemoryPoolBuilderSetDevice,&b,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&b,data,size);
  CALL(nvnMemoryPoolBuilderSetFlags,&b,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,pool,&b));
}
static void pool_access_cache_test(void) {
  mtl_init();mtl_finish();CHECK(mtl_trim_idle_resources());vk_state saved=S;
  int reference=V.options.pool_access_cache_reference;
  enum {page=4096,size=130*page+193,allocation=size+65536};
  uint8_t *a,*b;CHECK(!posix_memalign((void **)&a,65536,allocation));CHECK(!posix_memalign((void **)&b,65536,allocation));
  memset(a,0x11,allocation);memset(b,0x22,allocation);NVNmemoryPool pa,pb;access_cache_pool(&pa,a,size);access_cache_pool(&pb,b,size);
  uint8_t warm[page],warm64[page],tail[193],mid[64];
  // Initialize B before the stale-overwrite test, so a fresh materialization
  // cannot hide a later missing dirty upload.
  mtl_sub_begin();initialized_span_gpu_read(b,page,warm);initialized_span_gpu_read(b+64*page,page,warm64);
  initialized_span_gpu_read(b+130*page,193,tail);vk_internal_wait(vk_internal_commit());mtl_finish();
  for(unsigned i=0;i<page;i++)CHECK(warm[i]==0x22&&warm64[i]==0x22);
  uint8_t control[0x880];memset(control,2,sizeof control);NVNprogram program;NVNshaderData shader={.control=control};
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,1,&shader));
  mtl_sub_begin();pool_window_fill(a,page,0x45454545);pool_window_fill(a+64*page,page,0x67676767);
  vk_buffer *buffer;uint64_t off;
  CHECK(mtl_addr((uintptr_t)a+130*page,193,(void **)&buffer,&off));
  initialized_span_gpu_read(a,page,warm);initialized_span_gpu_read(a+64*page,page,warm64);
  initialized_span_gpu_read(a+130*page,193,tail);
  uintptr_t bitmap_a=vk_test_pool_access_bitmap((uintptr_t)a);CHECK(bitmap_a);
  int gated=vk_async_submissions()&&V.timeline_semaphore;VkSemaphore gate=VK_NULL_HANDLE;
  if(gated) {gate=pool_copy_gate();vk_command_reset_test_gate(1);}
  uint64_t first=vk_internal_commit();uint64_t idle_bytes;unsigned idle_count;
  if(gated) {
    CHECK(vk_completed_serial()<first);CHECK(mtl_trim_idle_resources());
    CHECK(!vk_test_pool_access_cached(bitmap_a,&idle_bytes,&idle_count));
    // Another recording while A is really pending cannot borrow its bitmaps.
    mtl_sub_begin();initialized_span_gpu_read(b+64*page,64,mid);
    CHECK(vk_test_pool_access_bitmap((uintptr_t)b)!=bitmap_a);uint64_t second=vk_internal_commit();
    CHECK(second>first);CHECK(!vk_test_pool_access_cached(bitmap_a,&idle_bytes,&idle_count));
    pool_copy_signal(&gate);CHECK(vk_command_reset_test_wait_blocked(first)>=first);
    uint64_t done,ready;unsigned sets;vk_command_reset_test_snapshot(&done,&ready,&sets);
    CHECK(done>=first&&ready<first);CHECK(mtl_trim_idle_resources());
    CHECK(!vk_test_pool_access_cached(bitmap_a,&idle_bytes,&idle_count));
    vk_command_reset_test_gate(0);mtl_finish();vkDestroySemaphore(V.device,gate,NULL);
    for(unsigned i=0;i<64;i++)CHECK(mid[i]==0x22);
  } else mtl_finish();
  for(unsigned i=0;i<page;i++)CHECK(warm[i]==0x45&&warm64[i]==0x67);
  for(unsigned i=0;i<193;i++)CHECK(tail[i]==0x11);
  // Worker has released the original pool and reset its commands, but owner
  // reclaim has not run. Delete A now: cached capacity must not read owner.
  CALL(nvnMemoryPoolFinalize,&pa);
  dm_guest_write_prepare(b,size);for(unsigned i=0;i<64;i++)((uint32_t *)b)[i]=i*31+9;
  memset(b+64*page,0x9c,page);memset(b+130*page,0x8a,193);
  mtl_sub_begin();uint64_t before_bytes;unsigned before_count;
  int cached=vk_test_pool_access_cached(bitmap_a,&before_bytes,&before_count);
  if(!reference)CHECK(cached);
  CHECK(!vk_pool_address_stage((uintptr_t)b+size-16,32,(void **)&buffer,&off,VK_PIPELINE_STAGE_TRANSFER_BIT));
  CHECK(vk_test_pool_access_cached(bitmap_a,&idle_bytes,&idle_count)==cached);
  CHECK(idle_bytes==before_bytes&&idle_count==before_count);
  S=(vk_state){.program=(const prog_t *)&program};
  S.ssbo[5][0]=(p_bind_buf){.stage=5,.index=0,.addr=(uintptr_t)b,.size=256};
  S.ssbo[5][1]=(p_bind_buf){.stage=5,.index=1,.addr=(uintptr_t)b+page,.size=256};
  p_dispatch dispatch={1,1,1,0};vk_dispatch(&dispatch);
  if(!reference)CHECK(vk_test_pool_access_bitmap((uintptr_t)b)==bitmap_a);
  initialized_span_gpu_read(b+64*page,page,warm64);initialized_span_gpu_read(b+130*page,193,tail);
  if(gated)gate=pool_copy_gate();uint64_t third=vk_internal_commit();
  if(gated) {
    CHECK(vk_pool_pending_stages((uintptr_t)b,256)==VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    CHECK(vk_pool_pending_stages((uintptr_t)b+64*page,64)==VK_PIPELINE_STAGE_TRANSFER_BIT);
    CHECK(vk_pool_pending_stages((uintptr_t)b+130*page,193)==VK_PIPELINE_STAGE_TRANSFER_BIT);
    CHECK(vk_pool_pending_stages((uintptr_t)b+2*page,64)==0);
    pool_copy_signal(&gate);
  }
  vk_internal_wait(third);mtl_finish();if(gated)vkDestroySemaphore(V.device,gate,NULL);
  for(unsigned i=0;i<64;i++)CHECK(((uint32_t *)(b+page))[i]==(i*31+9)*3+7);
  for(unsigned i=0;i<page;i++)CHECK(warm64[i]==0x9c);
  for(unsigned i=0;i<193;i++)CHECK(tail[i]==0x8a);
  // Exact same guest address, fresh pool identity, and old non-transfer stage
  // arrays: preserve normal initialization and no stale stage visibility.
  dm_guest_write_prepare(a,allocation);memset(a,0x6d,allocation);access_cache_pool(&pa,a,size);
  mtl_sub_begin();initialized_span_gpu_read(a,page,warm);initialized_span_gpu_read(a+130*page,193,tail);
  if(!reference)CHECK(vk_test_pool_access_bitmap((uintptr_t)a)==bitmap_a);
  if(gated)gate=pool_copy_gate();uint64_t fourth=vk_internal_commit();
  if(gated) {
    CHECK(vk_pool_pending_stages((uintptr_t)a,page)==VK_PIPELINE_STAGE_TRANSFER_BIT);
    CHECK(vk_pool_pending_stages((uintptr_t)a+130*page,193)==VK_PIPELINE_STAGE_TRANSFER_BIT);
    pool_copy_signal(&gate);
  }
  vk_internal_wait(fourth);mtl_finish();if(gated)vkDestroySemaphore(V.device,gate,NULL);
  for(unsigned i=0;i<page;i++)CHECK(warm[i]==0x6d);
  for(unsigned i=0;i<193;i++)CHECK(tail[i]==0x6d);
  CALL(nvnProgramFinalize,&program);CALL(nvnMemoryPoolFinalize,&pa);CALL(nvnMemoryPoolFinalize,&pb);free(a);free(b);
  CHECK(mtl_trim_idle_resources());vk_test_pool_access_cached(0,&idle_bytes,&idle_count);CHECK(!idle_bytes&&!idle_count);
  // More completed uses than the bounded idle entry limit: surplus nodes must
  // fall back to free, not grow a new unbounded pool cache.
  enum {small=65536,N=17};uint8_t *data[N];NVNmemoryPool pools[N];uintptr_t bitmaps[N];uint8_t result[N][16];
  for(unsigned i=0;i<N;i++) {CHECK(!posix_memalign((void **)&data[i],small,small));memset(data[i],0x80+i,small);access_cache_pool(&pools[i],data[i],small);}
  mtl_sub_begin();for(unsigned i=0;i<N;i++) {initialized_span_gpu_read(data[i],16,result[i]);bitmaps[i]=vk_test_pool_access_bitmap((uintptr_t)data[i]);CHECK(bitmaps[i]);}
  vk_internal_wait(vk_internal_commit());mtl_finish();
  for(unsigned i=0;i<N;i++)for(unsigned k=0;k<16;k++)CHECK(result[i][k]==0x80+i);
  // Destroy all owners before reclaim to exercise the same lifetime boundary
  // across an entire list, then observe actual bounded retired storage.
  for(unsigned i=0;i<N;i++){CALL(nvnMemoryPoolFinalize,&pools[i]);free(data[i]);}
  mtl_sub_begin();int first_cached=vk_test_pool_access_cached(bitmaps[0],&idle_bytes,&idle_count);
  CHECK(idle_count==(reference?0:16));CHECK(idle_bytes<=(512u<<10));CHECK(!first_cached);
  if(!reference)CHECK(vk_test_pool_access_cached(bitmaps[N-1],&idle_bytes,&idle_count));
  CHECK(!mtl_trim_idle_resources());vk_internal_wait(vk_internal_commit());mtl_finish();
  CHECK(mtl_trim_idle_resources());vk_test_pool_access_cached(0,&idle_bytes,&idle_count);CHECK(!idle_bytes&&!idle_count);
  S=saved;vk_pipeline_state_changed();
  LOG("[pool access cache fixture] reference=%d GPU bytes, pool recreation, pending/reset protection, stage isolation, entry cap and pressure trim passed",reference);
}
#else
static void pool_access_cache_test(void) {}
#endif
