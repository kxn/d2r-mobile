#ifdef DM_VULKAN_CPU_OBSERVE_WRAP
extern int __real_nvn_dirty_cpu_observation_enable(void);
int __wrap_nvn_dirty_cpu_observation_enable(void) {
  return getenv("DM_SHARED_WINDOW_DENY_CPU_OBSERVE")?0:__real_nvn_dirty_cpu_observation_enable();
}
#endif
// Functional byte/range oracle. No timers, residency sampling or game code.
#if defined(DM_VULKAN_SHARED_IMPORT_WINDOWS) && defined(DM_VULKAN_TEST_EMBED)
extern void vk_test_shared_union_observer_release(void (*)(void));
extern int vk_test_shared_union(uint64_t,uint64_t,const vk_pool_requirement *,uint64_t);
static pthread_mutex_t shared_window_observer_mu=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t shared_window_observer_cv=PTHREAD_COND_INITIALIZER;
static int shared_window_observer_ready,shared_window_observer_release,shared_window_observer_wait_seen;
static uint8_t *shared_window_observer_source;
static void *shared_window_observer(void *unused) {
  (void)unused;vk_shared_cpu_observation token;
  CHECK(vk_pool_shared_cpu_observe_begin((uintptr_t)shared_window_observer_source,4096,&token));
  pthread_mutex_lock(&shared_window_observer_mu);shared_window_observer_ready=1;
  pthread_cond_broadcast(&shared_window_observer_cv);
  while(!shared_window_observer_release)pthread_cond_wait(&shared_window_observer_cv,&shared_window_observer_mu);
  pthread_mutex_unlock(&shared_window_observer_mu);
  vk_pool_shared_cpu_observe_end(&token);return NULL;
}
static void shared_window_release_observer(void) {
  pthread_mutex_lock(&shared_window_observer_mu);shared_window_observer_wait_seen=1;
  shared_window_observer_release=1;pthread_cond_broadcast(&shared_window_observer_cv);
  pthread_mutex_unlock(&shared_window_observer_mu);
}
static void shared_window_copy_read(uint8_t *address,unsigned size,uint8_t *out) {
  vk_pool_requirement need={(uintptr_t)address,size,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0};
  int operation=vk_pool_operation_begin_ranges(&need,1);
  pool_window_read(address,size,out);vk_pool_operation_end(operation);
}
// Exact metadata publication overlap, not a scheduler/timing measurement.
// Hook pauses a REAL observer_begin at its snapshot while resources is held;
// owner proves exclusion with trylock, then appends while that begin/token live.
extern void vk_test_shared_host_snapshot(void (*)(int,int,VkMemoryPropertyFlags));
static pthread_mutex_t shared_append_mu=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t shared_append_cv=PTHREAD_COND_INITIALIZER;
static int shared_append_snapshot,shared_append_continue,shared_append_ready,shared_append_release;
static int shared_append_backed;
static VkMemoryPropertyFlags shared_append_properties;
static uint8_t *shared_append_source;
static void shared_append_snapshot_hook(int phase,int backed,VkMemoryPropertyFlags properties) {
  pthread_mutex_lock(&shared_append_mu);
  shared_append_backed=backed;shared_append_properties=properties;shared_append_snapshot=phase+1;
  pthread_cond_broadcast(&shared_append_cv);
  while(shared_append_continue<phase+1)pthread_cond_wait(&shared_append_cv,&shared_append_mu);
  pthread_mutex_unlock(&shared_append_mu);
}
static void *shared_append_observer(void *unused) {
  (void)unused;vk_shared_cpu_observation token;
  // Main owns the fixture's plain checks counter; worker success is published
  // under shared_append_mu rather than racing CHECK's counter increment.
  if(!vk_pool_shared_cpu_observe_begin((uintptr_t)shared_append_source,4096,&token))
    die("Shared append observer begin unexpectedly rejected");
  pthread_mutex_lock(&shared_append_mu);shared_append_ready=1;pthread_cond_broadcast(&shared_append_cv);
  while(!shared_append_release)pthread_cond_wait(&shared_append_cv,&shared_append_mu);
  pthread_mutex_unlock(&shared_append_mu);vk_pool_shared_cpu_observe_end(&token);return NULL;
}
static void shared_window_append_observer_test(uint8_t *data) {
  enum {W=65536};NVNmemoryPoolBuilder mb;NVNmemoryPool owner;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,data,4u<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&owner,&mb));
  shared_append_source=data+W;nvn_dirty_scope *scope=nvn_dirty_scope_acquire(shared_append_source,4096);CHECK(scope);
  vk_pool_span_info old_head,current;
  for(unsigned phase=0;phase<2;phase++) {
    CHECK(vk_pool_shared_cpu_observe_prepare((uintptr_t)shared_append_source,4096));
    pthread_mutex_lock(&shared_append_mu);
    shared_append_snapshot=shared_append_continue=shared_append_ready=shared_append_release=0;
    pthread_mutex_unlock(&shared_append_mu);
    vk_test_shared_host_snapshot(shared_append_snapshot_hook);
    pthread_t observer;CHECK(!pthread_create(&observer,NULL,shared_append_observer,NULL));
    pthread_mutex_lock(&shared_append_mu);
    while(!shared_append_snapshot)pthread_cond_wait(&shared_append_cv,&shared_append_mu);
    CHECK(shared_append_backed==(phase!=0));
    if(!phase)CHECK(!shared_append_properties);
    else CHECK(shared_append_properties&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    // The snapshot must actually own the SAME lock used by first/head publish.
    CHECK(pthread_mutex_trylock(&V.resources)==EBUSY);
    shared_append_continue=1;pthread_cond_broadcast(&shared_append_cv);
    while(shared_append_snapshot<2)pthread_cond_wait(&shared_append_cv,&shared_append_mu);
    // Real begin is still active, now outside resources: publish can proceed.
    CHECK(!pthread_mutex_trylock(&V.resources));pthread_mutex_unlock(&V.resources);
    pthread_mutex_unlock(&shared_append_mu);
    vk_pool_requirement need={(uintptr_t)data+(phase?W:4*W)+128,64,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0};
    vk_pool_reserve_result result;CHECK(vk_pool_reserve_ranges(&need,1,W,&result)==VK_POOL_RESERVE_OK);
    CHECK(result.added_spans==1 && result.added_buffer_bytes==W);
    pthread_mutex_lock(&shared_append_mu);
    CHECK(!shared_append_ready); // publication overlapped this actual begin
    shared_append_continue=2;pthread_cond_broadcast(&shared_append_cv);
    while(!shared_append_ready)pthread_cond_wait(&shared_append_cv,&shared_append_mu);
    pthread_mutex_unlock(&shared_append_mu);
    CHECK(vk_pool_span_query(need.address,64,&current)==VK_POOL_SPAN_SINGLE);
    if(!phase)old_head=current;
    else {
      CHECK(current.logical_begin==W);
      CHECK(vk_pool_span_query((uintptr_t)data+4*W+128,64,&current)==VK_POOL_SPAN_SINGLE);
      CHECK(current.handle==old_head.handle && current.backing_generation==old_head.backing_generation);
    }
    // Scope/token remains live through a real GPU read of the appended span.
    // Hook is one-shot for this observer; subsequent owner host phases pass.
    vk_test_shared_host_snapshot(NULL);
    uint8_t read[64];mtl_sub_begin();shared_window_copy_read((uint8_t *)(uintptr_t)need.address,64,read);
    vk_internal_wait(vk_internal_commit());for(unsigned k=0;k<64;k++)CHECK(read[k]==0x39);
    pthread_mutex_lock(&shared_append_mu);shared_append_release=1;pthread_cond_broadcast(&shared_append_cv);
    pthread_mutex_unlock(&shared_append_mu);CHECK(!pthread_join(observer,NULL));
  }
  nvn_dirty_scope_release(scope);CALL(nvnMemoryPoolFinalize,&owner);
  CHECK(!vk_validation_error_count());
  printf("{\"test_details\":\"shared-observer-first-and-head-append\",\"real_begin_token\":true,\"snapshot_resource_lock_exclusion\":true,\"actual_GPU_bytes\":true}\n");
}
static VkBufferView shared_window_old_view;
static VkBuffer shared_window_old_buffer;
static int shared_window_view_destroyed,shared_window_buffer_destroyed;
static PFN_vkDestroyBufferView shared_window_destroy_view_original;
static PFN_vkDestroyBuffer shared_window_destroy_buffer_original;
static void VKAPI_PTR shared_window_destroy_view(VkDevice device,VkBufferView view,const VkAllocationCallbacks *allocator) {
  if(view==shared_window_old_view)shared_window_view_destroyed=1;
  shared_window_destroy_view_original(device,view,allocator);
}
static void VKAPI_PTR shared_window_destroy_buffer(VkDevice device,VkBuffer buffer,const VkAllocationCallbacks *allocator) {
  if(buffer==shared_window_old_buffer) {CHECK(shared_window_view_destroyed);shared_window_buffer_destroyed=1;}
  shared_window_destroy_buffer_original(device,buffer,allocator);
}
static int shared_window_incomplete_collect(void *context,vk_pool_requirement_sink sink,void *out) {
  (void)context;(void)sink;(void)out;return 0;
}
static void shared_window_startup_fallback_test(void) {
  enum {W=65536,size=1<<20};
  CHECK(!vk_pool_shared_mode() && V.options.pool_window_bytes==W);
  uint8_t *data;CHECK(!posix_memalign((void **)&data,W,size));memset(data,0x57,size);
  NVNmemoryPoolBuilder mb;NVNmemoryPool owner;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,data,size);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&owner,&mb));
  vk_pool_requirement need={(uintptr_t)data+W+128,64,0,VK_PIPELINE_STAGE_TRANSFER_BIT,1,0};
  mtl_sub_begin();int operation=vk_pool_operation_begin_ranges(&need,1);
  pool_window_fill(data+W+128,64,0x28394a5b);
  vk_buffer_writeback_stage(need.address,64,VK_PIPELINE_STAGE_TRANSFER_BIT);
  vk_pool_operation_end(operation);vk_internal_wait(vk_internal_commit());
  vk_pool_requirement cross={(uintptr_t)data+2*W-16,32,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0};
  mtl_sub_begin();operation=vk_pool_operation_begin_ranges(&cross,1);vk_pool_operation_end(operation);
  uint8_t read[64];shared_window_copy_read(data+W+128,64,read);vk_internal_wait(vk_internal_commit());
  for(unsigned k=0;k<64;k+=4){uint32_t value;memcpy(&value,read+k,4);CHECK(value==0x28394a5b);}
  // This path used to consult the prematurely-published shared_window_bytes
  // despite shared_active=false. Actual legacy fallback/promotion must work.
  mtl_sub_begin();operation=vk_pool_operation_begin(shared_window_incomplete_collect,NULL);
  vk_pool_operation_end(operation);vk_pool_span_info span;
  CHECK(vk_pool_span_query((uintptr_t)data,size,&span)==VK_POOL_SPAN_SINGLE);
  CHECK(!span.logical_begin && span.logical_end==size);
  shared_window_copy_read(data+W+128,64,read);vk_internal_wait(vk_internal_commit());
  for(unsigned k=0;k<64;k+=4){uint32_t value;memcpy(&value,read+k,4);CHECK(value==0x28394a5b);}
  CALL(nvnMemoryPoolFinalize,&owner);free(data);CHECK(!vk_validation_error_count());
  printf("{\"test\":\"shared-window-cold-failure-legacy-recovery\",\"checks\":%u,\"actual_GPU_bytes\":true,\"legacy_crossing_and_whole_fallback\":true,\"result\":\"passed\"}\n",checks);
}
extern VkBuffer vk_test_recording_span_hint(uint64_t,uint64_t,unsigned *);
extern int vk_test_idle_span_hints_empty(void);
static void shared_window_recording_geometry_test(uint8_t *data) {
  enum {W=65536,N=9};NVNmemoryPoolBuilder mb;NVNmemoryPool owner;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,data,4u<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&owner,&mb));
  vk_pool_requirement need[N+1];vk_pool_span_info info[N];unsigned count;
  for(unsigned i=0;i<N+1;i++)need[i]=(vk_pool_requirement){(uintptr_t)data+(2*i+1)*W+64,4,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0};
  mtl_sub_begin();int token=vk_pool_operation_begin_ranges(need,N);
  CHECK(!vk_test_recording_span_hint(need[0].address,4,&count) && count==0); // admission is not a real use
  for(unsigned i=0;i<N;i++)CHECK(vk_pool_span_query(need[i].address,4,&info[i])==VK_POOL_SPAN_SINGLE);
  uint32_t expected[N],pixels[N];
  for(unsigned i=0;i<8;i++) {
    expected[i]=0x71820000+i;pool_window_fill((uint8_t *)(uintptr_t)need[i].address,4,expected[i]);
  }
  // Already-used spans must also serve a real nested admission: its first
  // locked hint is the ninth node, so the eight earlier spans need the set.
  int nested=vk_pool_operation_begin_ranges(need,8);CHECK(nested>token);
  vk_pool_operation_end(nested);
  for(unsigned i=0;i<8;i++)CHECK(vk_test_recording_span_hint(need[i].address,4,&count)==info[i].handle && count==8);
  CHECK(!vk_test_recording_span_hint(need[0].address-64+W-4,8,&count)); // a physical tail crossing
  CHECK(!vk_test_recording_span_hint((uintptr_t)data+64,4,&count)); // a hole
  CHECK(!vk_test_recording_span_hint(UINT64_MAX-3,8,&count));
  CHECK(!vk_test_recording_span_hint(need[0].address,0,&count)); // address-only never proves a range
  expected[8]=0x71820008;pool_window_fill((uint8_t *)(uintptr_t)need[8].address,4,expected[8]);
  CHECK(count==8 && !vk_test_recording_span_hint(need[0].address,4,&count)); // ninth evicts only geometry
  CHECK(vk_test_recording_span_hint(need[8].address,4,&count)==info[8].handle && count==8);
  // Only the last8 are cached. Reverse reads exercise retained multi-hint
  // hits AND the evicted original fallback, with different real GPU words.
  for(int i=N-1;i>=0;i--)pool_window_read((uint8_t *)(uintptr_t)need[i].address,4,(uint8_t *)&pixels[i]);
  vk_pool_operation_end(token);vk_internal_wait(vk_internal_commit());
  CHECK(!memcmp(pixels,expected,sizeof pixels)); // real GPU oracle before fault gates
  mtl_sub_begin();token=vk_pool_operation_begin_ranges(need,N);
  for(unsigned i=0;i<N;i++)pool_window_read((uint8_t *)(uintptr_t)need[i].address,4,(uint8_t *)&pixels[i]);
  vk_pool_operation_end(token);
  // Disjoint publication is legal while this recording retains old spans.
  // It invalidates geometric slots before dereference without moving old data.
  vk_pool_reserve_result added;CHECK(vk_pool_reserve_ranges(need+N,1,W,&added)==VK_POOL_RESERVE_OK && added.added_spans==1);
  CHECK(!vk_test_recording_span_hint(need[1].address,4,&count) && count==0);
  vk_internal_wait(vk_internal_commit());CHECK(!memcmp(pixels,expected,sizeof pixels));
  mtl_sub_begin();token=vk_pool_operation_begin_ranges(need,N);
  CHECK(!vk_test_recording_span_hint(need[1].address,4,&count) && count==0);
  for(unsigned i=0;i<N;i++)pool_window_read((uint8_t *)(uintptr_t)need[i].address,4,(uint8_t *)&pixels[i]);
  vk_pool_operation_end(token);vk_internal_wait(vk_internal_commit());CHECK(!memcmp(pixels,expected,sizeof pixels));
  // An unsubmitted lookup can already own immutable geometry. Reclaim/reset
  // must discard it even if the registry epoch itself did not change.
  mtl_sub_begin();token=vk_pool_operation_begin_ranges(need,1);
  pool_window_read((uint8_t *)(uintptr_t)need[0].address,4,(uint8_t *)&pixels[0]);
  CHECK(vk_test_recording_span_hint(need[0].address,4,&count)==info[0].handle);
  vk_pool_operation_end(token);vk_internal_wait(vk_internal_commit());
  mtl_finish();mtl_sub_begin();
  CHECK(vk_test_idle_span_hints_empty());
  CHECK(!vk_test_recording_span_hint(need[0].address,4,&count) && count==0);
  vk_internal_wait(vk_internal_commit());
  CALL(nvnMemoryPoolFinalize,&owner);
  printf("{\"test_details\":\"submission-retained-span-geometry\",\"nine_spans_real_GPU_words\":true,\"admission_does_not_fill\":true,\"ninth_eviction\":true,\"append_epoch\":true,\"reclaim_reset\":true,\"result\":\"passed\"}\n");
}
static void shared_import_windows_test(void) {
  enum {size=4<<20,W=65536};
  CHECK(vk_pool_shared_active() && V.options.pool_window_bytes==W);
  CHECK(getenv("DM_SHARED_GUEST_HEAP_FIXTURE"));
  uint8_t *data=bytes;memset(data,0x39,size);
  shared_window_append_observer_test(data);
  if(getenv("DM_SHARED_SPAN_GEOMETRY_FIXTURE"))shared_window_recording_geometry_test(data);
  NVNmemoryPoolBuilder mb;NVNmemoryPool owner;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,data,size);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&owner,&mb));
  vk_pool_requirement needs[]={
    {(uintptr_t)data+2*W+128,4096,256,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,0,1},
    {(uintptr_t)data+2*W+8192,2048,256,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,2},
    {(uintptr_t)data+10*W+512,8192,0,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,0,VK_POOL_REQUIRE_VERTEX},
    {(uintptr_t)data+10*W+32768,12,0,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,0,VK_POOL_REQUIRE_INDEX},
    {(uintptr_t)data+2*W+128,4096,256,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,1}};
  vk_pool_reserve_result result;vk_pool_span_info a,b,q;
  CHECK(vk_pool_reserve_ranges(needs,5,W,&result)==VK_POOL_RESERVE_OK);
  CHECK(result.added_spans==2 && result.added_buffer_bytes==2*W);
  CHECK(vk_pool_span_query(needs[0].address,4096,&a)==VK_POOL_SPAN_SINGLE && a.local_offset==128);
  CHECK(vk_pool_span_query(needs[2].address,8192,&b)==VK_POOL_SPAN_SINGLE && b.local_offset==512);
  CHECK(a.handle!=b.handle && a.pool_identity==b.pool_identity);
  CHECK(vk_test_pool_reserve_metadata((uintptr_t)data)==0);
  CHECK(vk_pool_reserve_ranges(needs,5,W,&result)==VK_POOL_RESERVE_OK && !result.added_spans);
  // Public Flush/Invalidate skip unimported holes rather than importing them.
  CALL(nvnMemoryPoolFlushMappedRange,&owner,0,12*W);
  CALL(nvnMemoryPoolInvalidateMappedRange,&owner,0,12*W);
  CHECK(vk_pool_span_query((uintptr_t)data+W,16,&q)==VK_POOL_SPAN_CROSSING);
  vk_pool_requirement negative[]={
    {(uintptr_t)data+12*W+32,32,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0},
    {UINT64_MAX-7,16,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0}};
  CHECK(vk_pool_reserve_ranges(negative,2,W,&result)==VK_POOL_RESERVE_INVALID && !result.added_spans);
  negative[1].address=(uintptr_t)data+13*W+32;
  vk_test_pool_reserve_fail_after(1);
  CHECK(vk_pool_reserve_ranges(negative,2,W,&result)==VK_POOL_RESERVE_OOM && !result.added_spans);
  vk_test_pool_reserve_fail_after(-1);
  CHECK(vk_pool_span_query(negative[0].address,32,&q)==VK_POOL_SPAN_CROSSING);
  CHECK(vk_pool_span_query(needs[0].address,4096,&q)==VK_POOL_SPAN_SINGLE && q.handle==a.handle);
  // Keep a true asynchronous reader incomplete behind a timeline semaphore.
  // This is a functional queue gate, never a delay/timer or GPU measurement.
  int asynchronous=vk_async_submissions();CHECK(V.timeline_semaphore);VkSemaphore gate=VK_NULL_HANDLE;
  VkSemaphoreTypeCreateInfoKHR type={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR,.semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE_KHR};
  VkSemaphoreCreateInfo semaphore={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,.pNext=&type};
  if(asynchronous)VK_CHECK(vkCreateSemaphore(V.device,&semaphore,NULL,&gate));
  uint64_t gate_value=1;
  VkTimelineSemaphoreSubmitInfoKHR timeline={.sType=VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR,.waitSemaphoreValueCount=1,.pWaitSemaphoreValues=&gate_value};
  VkPipelineStageFlags gate_stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  VkSubmitInfo blocked={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.pNext=&timeline,.waitSemaphoreCount=1,.pWaitSemaphores=&gate,.pWaitDstStageMask=&gate_stage};
  if(asynchronous) {pthread_mutex_lock(&V.queue_mu);VK_CHECK(vkQueueSubmit(V.queue,1,&blocked,VK_NULL_HANDLE));pthread_mutex_unlock(&V.queue_mu);}
  uint8_t read[8192];mtl_sub_begin();shared_window_copy_read(data+2*W+128,4096,read);
  // Add a disjoint import without closing a reader's current command buffer.
  uint64_t pending_reader=vk_internal_commit();if(asynchronous)CHECK(vk_completed_serial()<pending_reader);
  mtl_sub_begin();uint64_t serial=vk_recording_serial();
  int operation=vk_pool_operation_begin_ranges(negative,2);
  CHECK(vk_recording_serial()==serial);vk_pool_operation_end(operation);
  if(asynchronous)CHECK(vk_completed_serial()<pending_reader);
  VkSemaphoreSignalInfoKHR signal={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO_KHR,.semaphore=gate,.value=1};
  if(asynchronous)VK_CHECK(V.signal_timeline(V.device,&signal));
  vk_internal_wait(pending_reader);
  vk_internal_wait(vk_internal_commit());if(gate)vkDestroySemaphore(V.device,gate,NULL);
  for(unsigned k=0;k<4096;k++)CHECK(read[k]==0x39);
  // Actual GPU-only words remain original shared bytes through a union. No
  // copy, shadow snapshot or fake TRANSFER producer is permitted at rebinding.
  mtl_sub_begin();operation=vk_pool_operation_begin_ranges(needs,1);
  pool_window_fill(data+2*W+128,64,0x1728394a);
  vk_buffer_writeback_stage((uintptr_t)data+2*W+128,64,VK_PIPELINE_STAGE_TRANSFER_BIT);
  vk_pool_operation_end(operation);vk_internal_wait(vk_internal_commit());
  for(unsigned k=0;k<64;k+=4){uint32_t v;memcpy(&v,data+2*W+128+k,4);CHECK(v==0x1728394a);}
  // Keep a true CPU observation on a separate page while a crossing binding
  // forces consolidation. End does not need queue/owner work; cv releases lock.
  nvn_dirty_scope *scope=nvn_dirty_scope_acquire(data+2*W+8192,4096);CHECK(scope);
  shared_window_observer_source=data+2*W+8192;
  CHECK(vk_pool_shared_cpu_observe_prepare((uintptr_t)shared_window_observer_source,4096));
  pthread_t observer;CHECK(!pthread_create(&observer,NULL,shared_window_observer,NULL));
  pthread_mutex_lock(&shared_window_observer_mu);
  while(!shared_window_observer_ready)pthread_cond_wait(&shared_window_observer_cv,&shared_window_observer_mu);
  pthread_mutex_unlock(&shared_window_observer_mu);
  vk_test_shared_union_observer_release(shared_window_release_observer);
  vk_pool_requirement crossing={(uintptr_t)data+3*W-16,32,256,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0};
  mtl_sub_begin();operation=vk_pool_operation_begin_ranges(&crossing,1);
  CHECK(!pthread_join(observer,NULL));CHECK(shared_window_observer_wait_seen);
  vk_test_shared_union_observer_release(NULL);nvn_dirty_scope_release(scope);
  CHECK(vk_pool_span_query(crossing.address,crossing.size,&q)==VK_POOL_SPAN_SINGLE);
  CHECK(q.logical_begin==2*W && q.logical_end==4*W && q.handle!=a.handle);
  vk_pool_operation_end(operation);
  shared_window_copy_read(data+2*W+128,64,read);vk_internal_wait(vk_internal_commit());
  for(unsigned k=0;k<64;k+=4){uint32_t v;memcpy(&v,read+k,4);CHECK(v==0x1728394a);}
  // An actual cached typed VkBufferView must be retired before its old
  // imported Buffer; metadata epoch invalidation alone cannot free this view.
  NVNtexture typed;NVNtextureBuilder typed_builder;
  CALL(nvnTextureBuilderSetDefaults,&typed_builder);CALL(nvnTextureBuilderSetDevice,&typed_builder,&device);
  CALL(nvnTextureBuilderSetTarget,&typed_builder,NVN_TEXTURE_TARGET_BUFFER);
  CALL(nvnTextureBuilderSetWidth,&typed_builder,16);CALL(nvnTextureBuilderSetFormat,&typed_builder,NVN_FORMAT_R32UI);
  CALL(nvnTextureBuilderSetStorage,&typed_builder,&owner,2*W+128);
  CHECK(CALL(nvnTextureInitialize,&typed,&typed_builder));mtl_set_tex_pool_n(256);mtl_reg_tex(111,(const tex_t *)&typed,NULL);
  vk_pool_requirement typed_need={(uintptr_t)data+2*W+128,64,0,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0};
  mtl_sub_begin();operation=vk_pool_operation_begin_ranges(&typed_need,1);
  shared_window_old_view=vk_texel_buffer(111,0,0,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  CHECK(shared_window_old_view);CHECK(vk_pool_span_query(typed_need.address,64,&q)==VK_POOL_SPAN_SINGLE);
  shared_window_old_buffer=q.handle;vk_pool_operation_end(operation);
  shared_window_destroy_view_original=vkDestroyBufferView;vkDestroyBufferView=shared_window_destroy_view;
  shared_window_destroy_buffer_original=vkDestroyBuffer;vkDestroyBuffer=shared_window_destroy_buffer;
  vk_pool_requirement typed_cross={(uintptr_t)data+4*W-16,32,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0};
  operation=vk_pool_operation_begin_ranges(&typed_cross,1);vk_pool_operation_end(operation);
  CHECK(shared_window_view_destroyed && shared_window_buffer_destroyed);
  vkDestroyBuffer=shared_window_destroy_buffer_original;vkDestroyBufferView=shared_window_destroy_view_original;
  shared_window_copy_read(data+2*W+128,64,read);vk_internal_wait(vk_internal_commit());
  for(unsigned k=0;k<64;k+=4){uint32_t v;memcpy(&v,read+k,4);CHECK(v==0x1728394a);}
  CALL(nvnTextureFinalize,&typed);
  // Whole logical WB across adjacent distinct imports, with exact local offsets.
  vk_pool_requirement adjacent[]={
    {(uintptr_t)data+20*W,64,0,VK_PIPELINE_STAGE_TRANSFER_BIT,1,0},
    {(uintptr_t)data+21*W,64,0,VK_PIPELINE_STAGE_TRANSFER_BIT,1,0}};
  mtl_sub_begin();operation=vk_pool_operation_begin_ranges(adjacent,2);
  pool_window_fill(data+21*W-64,64,0x40516273);pool_window_fill(data+21*W,64,0x8495a6b7);
  vk_buffer_writeback_stage((uintptr_t)data+21*W-64,128,VK_PIPELINE_STAGE_TRANSFER_BIT);
  vk_pool_operation_end(operation);vk_internal_wait(vk_internal_commit());
  for(unsigned k=0;k<128;k+=4){uint32_t v;memcpy(&v,data+21*W-64+k,4);CHECK(v==(k<64?0x40516273:0x8495a6b7));}
  // Failed replacement allocation leaves both old mappings and GPU bytes.
  mtl_finish();vk_pool_span_info old_adjacent;
  CHECK(vk_pool_span_query(adjacent[0].address,64,&old_adjacent)==VK_POOL_SPAN_SINGLE);
  vk_pool_requirement union_oom={(uintptr_t)data+21*W-16,32,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0};
  vk_test_pool_reserve_fail_after(0);
  CHECK(!vk_test_shared_union((uintptr_t)data,old_adjacent.pool_identity,&union_oom,W));
  vk_test_pool_reserve_fail_after(-1);
  CHECK(vk_pool_span_query(adjacent[0].address,64,&q)==VK_POOL_SPAN_SINGLE && q.handle==old_adjacent.handle && q.backing_generation==old_adjacent.backing_generation);
  // Real NVN compute descriptor admission (full original shader ranges).
  NVNcommandBuffer commands;NVNprogram program;uint8_t control[0x880];memset(control,2,sizeof control);
  NVNshaderData shader={.control=control};CHECK(CALL(nvnProgramInitialize,&program,&device));
  CHECK(CALL(nvnProgramSetShaders,&program,1,&shader));
  CHECK(CALL(nvnCommandBufferInitialize,&commands,&device));
  CALL(nvnCommandBufferAddCommandMemory,&commands,&owner,0,W);
  uint32_t *input=(void *)(data+7*W+64),*output=(void *)(data+8*W+512);
  for(unsigned k=0;k<64;k++){input[k]=k*13+9;output[k]=0xa5a5a5a5;}
  CALL(nvnCommandBufferBeginRecording,&commands);
  CALL(nvnCommandBufferBindProgram,&commands,&program,NVN_SHADER_STAGE_COMPUTE_BIT);
  CALL(nvnCommandBufferBindStorageBuffer,&commands,NVN_SHADER_STAGE_COMPUTE,0,(uintptr_t)input,256);
  CALL(nvnCommandBufferBindStorageBuffer,&commands,NVN_SHADER_STAGE_COMPUTE,1,(uintptr_t)output,256);
  CALL(nvnCommandBufferDispatchCompute,&commands,1,1,1);
  NVNcommandHandle handle=CALL(nvnCommandBufferEndRecording,&commands);
  CALL(nvnQueueSubmitCommands,&queue,1,&handle);CALL(nvnQueueFinish,&queue);
  for(unsigned k=0;k<64;k++)CHECK(output[k]==input[k]*3+7);
  CALL(nvnCommandBufferFinalize,&commands);CALL(nvnProgramFinalize,&program);
  // Retile full destination straddles an existing import. Its source and
  // aligned storage prefix must be admitted before any image/temp borrow.
  NVNtexture textures[2];NVNtextureBuilder tb;
  CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetWidth,&tb,16);CALL(nvnTextureBuilderSetHeight,&tb,16);
  CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);
  NVNpackagedTextureLayout packaged={{0}};CALL(nvnTextureBuilderSetPackagedTextureLayout,&tb,&packaged);
  for(unsigned n=0;n<2;n++) {
    CALL(nvnTextureBuilderSetStorage,&tb,&owner,(30+n*2)*W-512);
    CHECK(CALL(nvnTextureInitialize,&textures[n],&tb));
    const tex_t *t=(const tex_t *)&textures[n];CHECK(t->L.size>512 && t->L.size<=W);
    dm_guest_write_prepare(t->mem,t->L.size);memset(t->mem,0,t->L.size);
  }
  // An adjacent pair alone may not claim the full retile/ASTC span ready.
  vk_pool_requirement before_retile[2]={
    {(uintptr_t)data+29*W,16,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0},
    {(uintptr_t)data+30*W,16,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0}};
  CHECK(vk_pool_reserve_ranges(before_retile,2,W,&result)==VK_POOL_RESERVE_OK);
  const tex_t *t0=(const tex_t *)&textures[0],*t1=(const tex_t *)&textures[1];
  CHECK(!vk_pool_shared_materialized((uintptr_t)t0->mem,t0->L.size));
  uint8_t *linear=data+26*W+128;
  for(unsigned k=0;k<1024;k+=4)memcpy(linear+k,(uint8_t[]){255,0,0,255},4);
  p_copy_b2t b2t={.tex=t0,.src=(uintptr_t)linear,.r={.width=16,.height=16,.depth=1}};
  mtl_sub_begin();vk_texture_copy_b2t(&b2t,0,0);
  CHECK(vk_pool_shared_materialized((uintptr_t)t0->mem,t0->L.size));
  vk_internal_wait(vk_internal_commit());
  uint8_t raw[1024];CHECK(!nvn_deswizzle(&t0->d,0,0,t0->mem,raw,64));
  for(unsigned k=0;k<1024;k+=4)CHECK(!memcmp(raw+k,(uint8_t[]){255,0,0,255},4));
  p_copy_t2t t2t={.src=t0,.dst=t1,.sr={.width=16,.height=16,.depth=1},.dr={.width=16,.height=16,.depth=1}};
  mtl_sub_begin();vk_texture_copy_t2t(&t2t);vk_internal_wait(vk_internal_commit());
  CHECK(vk_pool_shared_materialized((uintptr_t)t1->mem,t1->L.size));
  CHECK(!nvn_deswizzle(&t1->d,0,0,t1->mem,raw,64));
  for(unsigned k=0;k<1024;k+=4)CHECK(!memcmp(raw+k,(uint8_t[]){255,0,0,255},4));
  shared_image_pixels(&textures[0],(uint8_t[]){255,0,0,255});
  shared_image_pixels(&textures[1],(uint8_t[]){255,0,0,255});
  // Original T2B destination admission and publication retain GPU bytes too.
  p_copy_b2t t2b={.tex=t1,.src=(uintptr_t)data+40*W+128,.r={.width=16,.height=16,.depth=1}};
  mtl_sub_begin();vk_texture_copy_t2b(&t2b,0,0);vk_internal_wait(vk_internal_commit());
  for(unsigned k=0;k<1024;k+=4)CHECK(!memcmp(data+40*W+128+k,(uint8_t[]){255,0,0,255},4));
  CALL(nvnTextureFinalize,&textures[0]);CALL(nvnTextureFinalize,&textures[1]);
  vk_pool_sync_reference(1);uint64_t full,used,protected;int reference;
  mtl_sub_begin();vk_internal_wait(vk_internal_commit());
  vk_pool_sync_policy_stats(&full,&used,&protected,&reference);CHECK(!reference);
  CALL(nvnMemoryPoolFinalize,&owner);
  // Pool tail need not be a full window: it is still import/cache-page aligned.
  VkPhysicalDeviceExternalMemoryHostPropertiesEXT hp={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT};
  VkPhysicalDeviceProperties2 properties={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&hp};
  vkGetPhysicalDeviceProperties2(V.physical,&properties);uint64_t tail_unit=4096;
  if(tail_unit<hp.minImportedHostPointerAlignment)tail_unit=hp.minImportedHostPointerAlignment;
  if(tail_unit<(uint64_t)getpagesize())tail_unit=getpagesize();
  if(tail_unit<V.props.limits.nonCoherentAtomSize)tail_unit=V.props.limits.nonCoherentAtomSize;
  CHECK(tail_unit<=W);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,data,3*W+tail_unit);
  CHECK(CALL(nvnMemoryPoolInitialize,&owner,&mb));
  vk_pool_requirement tail={(uintptr_t)data+3*W+tail_unit-4,4,0,VK_PIPELINE_STAGE_TRANSFER_BIT,1,0};
  mtl_sub_begin();operation=vk_pool_operation_begin_ranges(&tail,1);
  CHECK(vk_pool_span_query(tail.address,4,&q)==VK_POOL_SPAN_SINGLE && q.logical_begin==3*W && q.logical_end==3*W+tail_unit && q.local_offset==tail_unit-4);
  pool_window_fill((void *)(uintptr_t)tail.address,4,0x91a2b3c4);
  vk_buffer_writeback_stage(tail.address,4,VK_PIPELINE_STAGE_TRANSFER_BIT);
  vk_pool_operation_end(operation);vk_internal_wait(vk_internal_commit());
  uint32_t tail_value;memcpy(&tail_value,(void *)(uintptr_t)tail.address,4);CHECK(tail_value==0x91a2b3c4);
  CALL(nvnMemoryPoolFinalize,&owner);CHECK(!vk_validation_error_count());
  astc_copy_encoding_test(); // actual GPU when supported; explicit unsupported on LVP
  printf("{\"test\":\"shared-import-windows\",\"checks\":%u,\"actual_memfd_heap\":true,\"no_shadow\":true,\"observer_union\":true,\"game_modules_executed\":false,\"result\":\"passed\"}\n",checks);
}
#else
static void shared_import_windows_test(void) {die("Shared import windows fixture requires its explicit experimental build");}
static void shared_window_startup_fallback_test(void) {die("Shared window fallback fixture requires experimental build");}
#endif
