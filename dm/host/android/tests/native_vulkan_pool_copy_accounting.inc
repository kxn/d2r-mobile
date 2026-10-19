// Exact logical byte oracle against real GPU buffers. No clocks or profiler.
#ifdef DM_VULKAN_TEST_EMBED
#include "../../../gfx/vulkan/nvn_vk_pool_copy_stats.h"
extern void vk_test_pool_copy_wait(void (*)(void *),void *);
static VkSemaphore pool_copy_gate(void) {
  VkSemaphoreTypeCreateInfoKHR type={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR,
    .semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE_KHR};
  VkSemaphoreCreateInfo create={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,.pNext=&type};VkSemaphore gate;
  VK_CHECK(vkCreateSemaphore(V.device,&create,NULL,&gate));uint64_t value=1;
  VkTimelineSemaphoreSubmitInfoKHR timeline={.sType=VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR,
    .waitSemaphoreValueCount=1,.pWaitSemaphoreValues=&value};VkPipelineStageFlags stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  VkSubmitInfo block={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.pNext=&timeline,
    .waitSemaphoreCount=1,.pWaitSemaphores=&gate,.pWaitDstStageMask=&stage};
  pthread_mutex_lock(&V.queue_mu);VK_CHECK(vkQueueSubmit(V.queue,1,&block,VK_NULL_HANDLE));pthread_mutex_unlock(&V.queue_mu);
  return gate;
}
static void pool_copy_signal(void *context) {
  VkSemaphoreSignalInfoKHR signal={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO_KHR,
    .semaphore=*(VkSemaphore *)context,.value=1};
  VK_CHECK(V.signal_timeline(V.device,&signal));vk_test_pool_copy_wait(NULL,NULL);
}
static void pool_copy_accounting_test(void) {
  mtl_init();if(V.options.pool_mirror_reference)return;
  enum {W=65536,SIZE=4*W+193};uint8_t *storage,*dest;uint8_t out[4096],old[64],next[64];
  CHECK(!posix_memalign((void **)&storage,W,SIZE));CHECK(!posix_memalign((void **)&dest,W,W));
  dm_guest_write_prepare(storage,SIZE);dm_guest_write_prepare(dest,W);memset(storage,0x37,SIZE);memset(dest,0x49,W);
  uint64_t previous_window=V.options.pool_window_bytes,full,used,protected;int reference;
  vk_pool_sync_policy_stats(&full,&used,&protected,&reference);vk_pool_sync_reference(0);mtl_finish();vk_test_pool_window_bytes(W);
  mtl_sub_begin();vk_internal_wait(vk_internal_commit());mtl_pool_add(storage,SIZE);mtl_pool_add(dest,W);
  vk_pool_requirement needs[]={pool_window_need(storage,4096,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0),
    pool_window_need(storage+4*W,193,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0),
    pool_window_need(dest,4096,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0)};
  vk_pool_reserve_result reserved;CHECK(vk_pool_reserve_ranges(needs,3,W,&reserved)==VK_POOL_RESERVE_OK);
  vk_pool_copy_stats a,b;vk_pool_copy_snapshot(&a);int enabled=a.enabled;
#define COPY_BEFORE() vk_pool_copy_snapshot(&a)
#define COPY_EXPECT(name,bytes) do {vk_pool_copy_snapshot(&b);CHECK(b.name-a.name==(enabled?(uint64_t)(bytes):0));}while(0)
  // Reservation alone never initializes/copies. Only first actual page lookup.
  COPY_BEFORE();mtl_sub_begin();pool_window_read(storage+64,64,out);vk_internal_wait(vk_internal_commit());CHECK(!memcmp(out,storage+64,64));
  COPY_EXPECT(initial_mirror_bytes,4096);COPY_EXPECT(initial_shadow_bytes,4096);COPY_EXPECT(mirror_bytes,0);COPY_EXPECT(shadow_bytes,0);
  COPY_BEFORE();mtl_sub_begin();pool_window_read(storage+64,64,out);vk_internal_wait(vk_internal_commit());CHECK(!memcmp(out,storage+64,64));
  COPY_EXPECT(initial_mirror_bytes,0);COPY_EXPECT(mirror_bytes,0);COPY_EXPECT(shadow_bytes,0);COPY_EXPECT(snapshot_bytes,0);
  COPY_EXPECT(scan_bytes,vk_async_submissions()?4096:0);COPY_EXPECT(fallback_compare_span_bytes,vk_async_submissions()?0:4096);
  // Exact sparse and full-page direct updates, including the experimental dual
  // copy. Serial mode remains a full-page reference and never selects dual.
  int old_dual=vk_test_upload_dual_enable(0);
  for(unsigned dual=0;dual<2;dual++) {
    vk_test_upload_dual_enable(dual);uint32_t word=0xaabbcc00+dual;
    dm_guest_write_prepare(storage+64,4);memcpy(storage+64,&word,4);
    COPY_BEFORE();mtl_sub_begin();pool_window_read(storage+64,64,out);vk_internal_wait(vk_internal_commit());CHECK(!memcmp(out,storage+64,64));
    COPY_EXPECT(mirror_bytes,vk_async_submissions()?4:4096);COPY_EXPECT(shadow_bytes,vk_async_submissions()?4:4096);COPY_EXPECT(snapshot_bytes,0);COPY_EXPECT(gpu_upload_recorded_bytes,0);
    COPY_EXPECT(scan_bytes,vk_async_submissions()?4096:0);COPY_EXPECT(fallback_compare_span_bytes,vk_async_submissions()?0:4096);
  }
  vk_test_upload_dual_enable(0);
  dm_guest_write_prepare(storage,4096);memset(storage,0x72,4096);
  COPY_BEFORE();mtl_sub_begin();pool_window_read(storage,4096,out);vk_internal_wait(vk_internal_commit());CHECK(!memcmp(out,storage,4096));
  COPY_EXPECT(mirror_bytes,4096);COPY_EXPECT(shadow_bytes,4096);
  // Scalar reference comparison retains its explicit page-span label. Its
  // repeated/short-circuit word memcmp traffic is deliberately not invented.
  vk_upload_scan_reference(1);dm_guest_write_prepare(storage+64,4);storage[64]^=0x29;
  COPY_BEFORE();mtl_sub_begin();pool_window_read(storage+64,64,out);vk_internal_wait(vk_internal_commit());CHECK(!memcmp(out,storage+64,64));
  COPY_EXPECT(reference_page_span_bytes,vk_async_submissions()?4096:0);COPY_EXPECT(scan_bytes,0);
  COPY_EXPECT(mirror_bytes,vk_async_submissions()?4:4096);COPY_EXPECT(shadow_bytes,vk_async_submissions()?4:4096);vk_upload_scan_reference(0);
  // Last pool page is 193B, so original unaligned synchronous fallback copies
  // exactly the clipped span, never rounded physical allocation or 4096B.
  mtl_sub_begin();pool_window_read(storage+4*W,193,out);vk_internal_wait(vk_internal_commit());CHECK(!memcmp(out,storage+4*W,193));
  dm_guest_write_prepare(storage+4*W+192,1);storage[4*W+192]^=0x31;
  COPY_BEFORE();mtl_sub_begin();pool_window_read(storage+4*W,193,out);vk_internal_wait(vk_internal_commit());CHECK(!memcmp(out,storage+4*W,193));
  COPY_EXPECT(mirror_bytes,193);COPY_EXPECT(shadow_bytes,193);COPY_EXPECT(fallback_compare_span_bytes,193);
  if(vk_async_submissions()&&V.timeline_semaphore) {
    // Keep an earlier real GPU reader pending. Compact one-run snapshot uses4B
    // despite its64B allocation class; fragmented2-run snapshot reads4096B but
    // only uploads8B. Count actual copies, not queue capacity or page envelope.
    for(unsigned fragmented=0;fragmented<2;fragmented++) {
      VkSemaphore gate=pool_copy_gate();memcpy(old,storage+64,64);
      mtl_sub_begin();pool_window_read(storage+64,64,out);uint64_t first=vk_internal_commit();CHECK(vk_completed_serial()<first);
      dm_guest_write_prepare(storage+64,12);storage[64]^=0x13;if(fragmented)storage[72]^=0x17;memcpy(next,storage+64,64);
      COPY_BEFORE();mtl_sub_begin();uint8_t newer[64];pool_window_read(storage+64,64,newer);uint64_t second=vk_internal_commit();
      COPY_EXPECT(snapshot_bytes,fragmented?4096:4);COPY_EXPECT(gpu_upload_recorded_bytes,fragmented?8:4);COPY_EXPECT(shadow_bytes,fragmented?8:4);COPY_EXPECT(mirror_bytes,0);
      pool_copy_signal(&gate);vk_internal_wait(second);CHECK(!memcmp(out,old,64)&&!memcmp(newer,next,64));vkDestroySemaphore(V.device,gate,NULL);
    }
    // Real pending GPU publication forces scanner retry. Signal only from its
    // test-only wait hook, after the actual overlap has been detected. First
    // comparison sees a CPU edit, completed writeback replaces guest+shadow,
    // repeated comparison is equal, and no guest->mirror copy occurs.
    VkSemaphore gate=pool_copy_gate();mtl_sub_begin();pool_window_fill(storage+64,32,0x12345678);
    vk_buffer_writeback_stage((uintptr_t)storage+64,32,VK_PIPELINE_STAGE_TRANSFER_BIT);
    uint64_t first=vk_internal_commit();CHECK(vk_completed_serial()<first);
    vk_pool_copy_snapshot(&a);COPY_EXPECT(readback_guest_completed_bytes,0);COPY_EXPECT(readback_shadow_completed_bytes,0);
    dm_guest_write_prepare(storage+64,4);uint32_t cpu=0x87654321;memcpy(storage+64,&cpu,4);
    COPY_BEFORE();vk_test_pool_copy_wait(pool_copy_signal,&gate);mtl_sub_begin();pool_window_read(storage+64,64,out);vk_internal_wait(vk_internal_commit());
    COPY_EXPECT(scan_bytes,8192);COPY_EXPECT(mirror_bytes,0);COPY_EXPECT(shadow_bytes,0);
    COPY_EXPECT(readback_guest_completed_bytes,32);COPY_EXPECT(readback_shadow_completed_bytes,32);
    CHECK(!memcmp(out,storage+64,64));uint32_t gpu;memcpy(&gpu,out,4);CHECK(gpu==0x12345678);vkDestroySemaphore(V.device,gate,NULL);
  }
  // Known snapshot fill is an actual separate copy; publication schedules two
  // CPU destinations. A gated fence proves completed counts cannot advance at
  // recording time. No GPU readback is charged for this known-value path.
  VkSemaphore gate=VK_NULL_HANDLE;if(vk_async_submissions()&&V.timeline_semaphore)gate=pool_copy_gate();
  COPY_BEFORE();mtl_sub_begin();mtl_sub_copy((uintptr_t)storage+128,(uintptr_t)dest+64,64);uint64_t serial=vk_internal_commit();
  COPY_EXPECT(known_snapshot_bytes,64);
  if(gate) {COPY_EXPECT(known_guest_completed_bytes,0);COPY_EXPECT(known_shadow_completed_bytes,0);pool_copy_signal(&gate);}
  vk_internal_wait(serial);COPY_EXPECT(known_guest_completed_bytes,64);COPY_EXPECT(known_shadow_completed_bytes,64);
  COPY_EXPECT(readback_guest_completed_bytes,0);COPY_EXPECT(readback_shadow_completed_bytes,0);CHECK(!memcmp(dest+64,storage+128,64));
  if(gate)vkDestroySemaphore(V.device,gate,NULL);
  // Unknown GPU bytes take the real staging readback and two CPU destination
  // effects in both serial and async execution; no known snapshot is charged.
  COPY_BEFORE();mtl_sub_begin();pool_window_fill(dest+64,64,0x0f1e2d3c);
  vk_buffer_writeback_stage((uintptr_t)dest+64,64,VK_PIPELINE_STAGE_TRANSFER_BIT);
  COPY_EXPECT(readback_guest_completed_bytes,0);vk_internal_wait(vk_internal_commit());
  COPY_EXPECT(readback_guest_completed_bytes,64);COPY_EXPECT(readback_shadow_completed_bytes,64);COPY_EXPECT(known_snapshot_bytes,0);
  uint32_t expected=0x0f1e2d3c;for(unsigned k=0;k<16;k++)CHECK(!memcmp(dest+64+4*k,&expected,4));
  // Immediate caller-proven snapshot uses the same completed categories but
  // is filled now, rather than deferred from source.shadow at commit.
  uint32_t immediate[8];for(unsigned k=0;k<8;k++)immediate[k]=0x8badf00d;
  COPY_BEFORE();mtl_sub_begin();pool_window_fill(dest+64,32,immediate[0]);
  vk_buffer_writeback_known((uintptr_t)dest+64,32,immediate);
  COPY_EXPECT(known_snapshot_bytes,32);COPY_EXPECT(known_guest_completed_bytes,0);vk_internal_wait(vk_internal_commit());
  COPY_EXPECT(known_guest_completed_bytes,32);COPY_EXPECT(known_shadow_completed_bytes,32);COPY_EXPECT(readback_guest_completed_bytes,0);
  mtl_sub_begin();pool_window_read(dest+64,32,out);vk_internal_wait(vk_internal_commit());CHECK(!memcmp(out,immediate,32)&&!memcmp(dest+64,immediate,32));
  // Cold migration preserves two initialized pages (4096+193), while the
  // unexposed holes do not become CPU copies or initialized data by migration.
  mtl_finish();COPY_BEFORE();CHECK(vk_test_pool_migrate_whole((uintptr_t)storage));
  COPY_EXPECT(migration_gpu_recorded_bytes,4096+193);COPY_EXPECT(initial_mirror_bytes,0);COPY_EXPECT(mirror_bytes,0);COPY_EXPECT(shadow_bytes,0);
  mtl_sub_begin();pool_window_read(storage+64,64,out);vk_internal_wait(vk_internal_commit());CHECK(!memcmp(out,storage+64,64));
  nvn_dirty_add_range(dest,W);
  mtl_sub_begin();pool_window_read(dest+64,64,out);vk_internal_wait(vk_internal_commit());
  COPY_BEFORE();mtl_sub_begin();pool_window_read(dest+64,64,out);vk_internal_wait(vk_internal_commit());CHECK(!memcmp(out,dest+64,64));
  COPY_EXPECT(scan_bytes,0);COPY_EXPECT(fallback_compare_span_bytes,0);COPY_EXPECT(mirror_bytes,0);COPY_EXPECT(shadow_bytes,0);
  nvn_dirty_remove_range(dest);
  if(!enabled) {
    vk_pool_copy_snapshot(&b);
#define COPY_ZERO(name) CHECK(b.name==0);
    VK_POOL_COPY_FIELDS(COPY_ZERO)
#undef COPY_ZERO
  }
  vk_test_upload_dual_enable(old_dual);mtl_pool_del(storage);mtl_pool_del(dest);free(storage);free(dest);mtl_finish();vk_test_pool_window_bytes(previous_window);vk_pool_sync_reference(reference);
#undef COPY_EXPECT
#undef COPY_BEFORE
  LOG("[pool copy accounting] real GPU initialization, sparse/full/no-change/dual/tail, hazard snapshot, publication retry, known/fence-completed effects and initialized migration passed; enabled=%d",enabled);
}
#else
static void pool_copy_accounting_test(void) {}
#endif
