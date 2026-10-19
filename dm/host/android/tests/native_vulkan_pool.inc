// CPU 上传与真实在途 GPU 访问按实际复制范围同步；无冲突页不等待。
// CPU uploads synchronize with real outstanding GPU accesses by actual copy
// spans; unrelated pages proceed without waiting for the blocked submission.
#include <stdatomic.h>
#include <errno.h>
// Texture storage alone must not allocate a duplicate whole-pool buffer. A
// later buffer binding still snapshots the latest CPU data, not registration.
static void lazy_pool_mirror_test(void) {
  uint64_t registered,backed,count,materialized;int reference;
  vk_pool_mirror_stats(&registered,&backed,&count,&materialized,&reference);
  uint8_t *data;CHECK(!posix_memalign((void **)&data,65536,65536));
  memset(data,0x11,65536);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);
  CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,data,65536);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  uint64_t r,b,c,m;int mode;
  vk_pool_mirror_stats(&r,&b,&c,&m,&mode);
  CHECK(r==registered+65536&&c==count+1&&mode==reference);
  CHECK(b==backed+(reference?65536:0)&&m==materialized+!!reference);
  NVNtextureBuilder tb;NVNtexture texture;
  CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);
  CALL(nvnTextureBuilderSetWidth,&tb,8);CALL(nvnTextureBuilderSetHeight,&tb,8);
  CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);
  CALL(nvnTextureBuilderSetStorage,&tb,&pool,0);
  CHECK(CALL(nvnTextureInitialize,&texture,&tb));
  NVNcopyRegion whole={0,0,0,8,8,1};const float green[4]={0,1,0,1};
  CALL(nvnCommandBufferBeginRecording,&cmd);
  CALL(nvnCommandBufferClearTexture,&cmd,&texture,NULL,&whole,green,15);
  submit(end());
  uint8_t pixels[8*8*4];
  CALL(nvnTextureReadTexels,&texture,NULL,&whole,pixels);
  for(unsigned i=0;i<64;i++) CHECK(!memcmp(pixels+i*4,(uint8_t[]){0,255,0,255},4));
  vk_pool_mirror_stats(&r,&b,&c,&m,&mode);
  CHECK(b==backed+(reference?65536:0)&&m==materialized+!!reference);
  dm_guest_write_prepare(data+4096,4096);memset(data+4096,0x33,4096);
  mtl_sub_begin();vk_buffer *buffer;uint64_t off;
  uint64_t initialized=vk_pool_initialized_bytes();
  CHECK(vk_pool_address_stage((uintptr_t)data+4096,4096,(void **)&buffer,&off,VK_PIPELINE_STAGE_TRANSFER_BIT));
  CHECK(vk_pool_initialized_bytes()==initialized+(reference?0:4096));
  vk_buffer *snapshot=vk_temp_buffer(4096);uint8_t result[4096];
  VkBufferCopy copy={.srcOffset=off,.size=4096};
  vk_barrier();vkCmdCopyBuffer(V.cmd,buffer->handle,snapshot->handle,1,&copy);vk_barrier();
  vk_effect_copy(result,snapshot->map,sizeof result);CHECK(mtl_wait(mtl_sub_commit(),UINT64_MAX));
  for(unsigned i=0;i<sizeof result;i++) CHECK(result[i]==0x33);
  vk_pool_mirror_stats(&r,&b,&c,&m,&mode);
  CHECK(b==backed+65536&&m==materialized+1);
  // Zero is real initial content too; comparing it against an untouched
  // calloc shadow must not leave undefined GPU allocation bytes visible.
  dm_guest_write_prepare(data+8192,4096);memset(data+8192,0,4096);
  mtl_sub_begin();
  CHECK(vk_pool_address_stage((uintptr_t)data+8192,4096,(void **)&buffer,&off,VK_PIPELINE_STAGE_TRANSFER_BIT));
  snapshot=vk_temp_buffer(4096);copy.srcOffset=off;
  vk_barrier();vkCmdCopyBuffer(V.cmd,buffer->handle,snapshot->handle,1,&copy);vk_barrier();
  vk_effect_copy(result,snapshot->map,sizeof result);CHECK(mtl_wait(mtl_sub_commit(),UINT64_MAX));
  for(unsigned i=0;i<sizeof result;i++) CHECK(result[i]==0);
  CALL(nvnTextureReadTexels,&texture,NULL,&whole,pixels);
  for(unsigned i=0;i<64;i++) CHECK(!memcmp(pixels+i*4,(uint8_t[]){0,255,0,255},4));
  CALL(nvnTextureFinalize,&texture);CALL(nvnMemoryPoolFinalize,&pool);free(data);
  vk_pool_mirror_stats(&r,&b,&c,&m,&mode);
  CHECK(r==registered&&b==backed&&c==count&&m==materialized);
  LOG("[vulkan lazy pool mirror fixture] image-only pool stays unbacked; first buffer use sees fresh CPU edits and preserves GPU texture");
}
#ifdef DM_VULKAN_TEST_EMBED
extern uint32_t vk_test_upload_word_mask(const uint8_t *,const uint8_t *,unsigned);
// Literal bit patterns are independent of the scanner's SIMD packing.
// Android executes real NEON; host tests its portable fallback. Every word's
// changed byte rotates across its four lanes. Changes just outside the input
// must not appear in a bounded tail's result.
static void pool_upload_mask_boundaries_test(void) {
  uint8_t guest[67],shadow[67];memset(shadow,0x3c,sizeof shadow);
  for(unsigned words=1;words<=16;words++)for(uint32_t mask=0;mask<(1u<<words);mask++) {
    memcpy(guest,shadow,sizeof guest);guest[0]^=0x42;guest[1+words*4]^=0x24;
    for(unsigned word=0;word<words;word++)if(mask&(1u<<word))guest[1+word*4+(word%4)]^=0x81;
    CHECK(vk_test_upload_word_mask(guest+1,shadow+1,words)==mask);
  }
  LOG("[vulkan upload scan mask fixture] all16-bit patterns, unaligned bounded tails and outside-range edits passed");
}
#endif
// Actual GPU reads after sparse/dense CPU edits. Boundaries exercise vector
// lanes, 64-byte block joins, the last word, and the maximum 512 changed runs.
static void pool_upload_patterns_test(void) {
#ifdef DM_VULKAN_TEST_EMBED
  pool_upload_mask_boundaries_test();
#endif
  uint8_t *data;CHECK(!posix_memalign((void **)&data,65536,65536));memset(data,0x11,65536);
  NVNmemoryPoolBuilder builder;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&builder);CALL(nvnMemoryPoolBuilderSetDevice,&builder,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&builder,data,65536);
  CALL(nvnMemoryPoolBuilderSetFlags,&builder,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&builder));
  for(unsigned iteration=0;iteration<12;iteration++) {
    vk_upload_scan_reference(iteration<6);
    dm_guest_write_prepare(data,4096);
    switch(iteration%6) {
      case 0:memset(data,0x11,4096);break;
      case 1: {
        const unsigned words[]={0,3,4,15,16,1023};
        for(unsigned k=0;k<sizeof words/sizeof *words;k++)memset(data+words[k]*4,0x77,4);
        break;
      }
      case 2:memset(data,0,4096);break;
      case 3:case 4:
        for(unsigned word=0;word<1024;word++)memset(data+word*4,
            ((word^(iteration%6==4))&1)?0:0x55,4);
        break;
      case 5:memset(data,0xff,4096);break;
    }
    uint8_t expected[4096],result[4096];memcpy(expected,data,sizeof expected);
    mtl_sub_begin();vk_buffer *buffer;uint64_t offset;
    CHECK(vk_upload_scan_reference_mode()==(iteration<6));
    CHECK(vk_pool_address_stage((uintptr_t)data,4096,(void **)&buffer,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
    vk_buffer *copy=vk_temp_buffer(sizeof result);VkBufferCopy region={.srcOffset=offset,.size=sizeof result};
    vk_barrier();vkCmdCopyBuffer(V.cmd,buffer->handle,copy->handle,1,&region);vk_barrier();
    vk_effect_copy(result,copy->map,sizeof result);CHECK(mtl_wait(mtl_sub_commit(),UINT64_MAX));
    for(unsigned byte=0;byte<sizeof result;byte++)CHECK(result[byte]==expected[byte]);
  }
  vk_upload_scan_reference(0);CALL(nvnMemoryPoolFinalize,&pool);free(data);
  LOG("[vulkan upload scan fixture] scalar/vector GPU contents match: sparse boundaries, zero, alternating512 and dense edits");
}
// Partial syncs leave independent page stamps. A combined read may omit an
// already-synchronized page, but must observe another CPU write to that page.
static void pool_page_generation_test(void) {
  if(!vk_async_submissions())return;
  for(unsigned reference=0;reference<2;reference++) {
    vk_upload_scan_reference(reference);
    uint8_t *data;CHECK(!posix_memalign((void **)&data,65536,65536));memset(data,0x11,65536);
    NVNmemoryPoolBuilder builder;NVNmemoryPool pool;
    CALL(nvnMemoryPoolBuilderSetDefaults,&builder);CALL(nvnMemoryPoolBuilderSetDevice,&builder,&device);
    CALL(nvnMemoryPoolBuilderSetStorage,&builder,data,65536);
    CALL(nvnMemoryPoolBuilderSetFlags,&builder,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
    CHECK(CALL(nvnMemoryPoolInitialize,&pool,&builder));
    uint64_t skipped=vk_upload_page_rechecks_skipped();
    for(unsigned step=0;step<4;step++) {
      if(step==1) {
        dm_guest_write_prepare(data,8192);memset(data,0x22,4096);memset(data+4096,0x33,4096);
      } else if(step==3) {
        dm_guest_write_prepare(data,4096);memset(data,0x44,4096);
      }
      size_t size=step==1?4096:8192;
      uint8_t result[8192];mtl_sub_begin();vk_buffer *buffer;uint64_t offset;
      CHECK(vk_pool_address_stage((uintptr_t)data,size,(void **)&buffer,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
      vk_buffer *copy=vk_temp_buffer(size);VkBufferCopy region={.srcOffset=offset,.size=size};
      vk_barrier();vkCmdCopyBuffer(V.cmd,buffer->handle,copy->handle,1,&region);vk_barrier();
      vk_effect_copy(result,copy->map,size);CHECK(mtl_wait(mtl_sub_commit(),UINT64_MAX));
      for(unsigned byte=0;byte<size;byte++)CHECK(result[byte]==
          (!step?0x11:byte>=4096?0x33:step==3?0x44:0x22));
      if(step==2)CHECK(reference?vk_upload_page_rechecks_skipped()==skipped:
                                   vk_upload_page_rechecks_skipped()>skipped);
    }
    CALL(nvnMemoryPoolFinalize,&pool);free(data);
  }
  vk_upload_scan_reference(0);
  LOG("[vulkan upload page generation fixture] partial/combined sync skips only a current page and preserves the next CPU edit");
}
// A transfer's complete write can make initial CPU contents irrelevant, but
// prior reads, partial pages and a later submission's CPU edits remain live.
static void complete_pool_overwrite_test(void) {
  if(!vk_async_submissions()) return;
  for(unsigned test=0;test<3;test++) {
    uint8_t *data;CHECK(!posix_memalign((void **)&data,65536,65536));
    memset(data,0x11,65536);
    NVNmemoryPoolBuilder builder;NVNmemoryPool pool;
    CALL(nvnMemoryPoolBuilderSetDefaults,&builder);
    CALL(nvnMemoryPoolBuilderSetDevice,&builder,&device);
    CALL(nvnMemoryPoolBuilderSetStorage,&builder,data,65536);
    CALL(nvnMemoryPoolBuilderSetFlags,&builder,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
    CHECK(CALL(nvnMemoryPoolInitialize,&pool,&builder));
    dm_guest_write_prepare(data,4096);memset(data,0x33,4096);
    uint8_t previous[4096]={0},result[4096]={0};
    uint64_t skipped=vk_pool_overwrite_skipped_pages(),off;vk_buffer *buffer;
    mtl_sub_begin();
    if(test==1) {
      CHECK(vk_pool_address_stage((uintptr_t)data,4096,(void **)&buffer,&off,VK_PIPELINE_STAGE_TRANSFER_BIT));
      vk_buffer *snapshot=vk_temp_buffer(4096);
      VkBufferCopy copy={.srcOffset=off,.size=4096};
      vk_barrier();vkCmdCopyBuffer(V.cmd,buffer->handle,snapshot->handle,1,&copy);vk_barrier();
      vk_effect_copy(previous,snapshot->map,4096);
    }
    uint64_t written=test==2?2048:4096;
    CHECK(vk_pool_address_overwrite((uintptr_t)data,written,(void **)&buffer,&off,VK_PIPELINE_STAGE_TRANSFER_BIT));
    vk_barrier();vkCmdFillBuffer(V.cmd,buffer->handle,off,written,0x44444444);
    vk_buffer_transfer_written_range((uintptr_t)data,written);
    vk_pool_overwritten((uintptr_t)data,written);
    vk_buffer_writeback_stage((uintptr_t)data,written,VK_PIPELINE_STAGE_TRANSFER_BIT);
    CHECK(vk_pool_address_stage((uintptr_t)data,4096,(void **)&buffer,&off,VK_PIPELINE_STAGE_TRANSFER_BIT));
    vk_buffer *snapshot=vk_temp_buffer(4096);VkBufferCopy copy={.srcOffset=off,.size=4096};
    vk_barrier();vkCmdCopyBuffer(V.cmd,buffer->handle,snapshot->handle,1,&copy);vk_barrier();
    vk_effect_copy(result,snapshot->map,4096);
    CHECK(mtl_wait(mtl_sub_commit(),UINT64_MAX));
    for(unsigned i=0;i<4096;i++) {
      CHECK(result[i]==(i<written?0x44:0x33));
      if(test==1) CHECK(previous[i]==0x33);
    }
    uint64_t full,used,protected_bytes;int pool_reference;
    vk_pool_sync_policy_stats(&full,&used,&protected_bytes,&pool_reference);
    CHECK(vk_pool_overwrite_skipped_pages()-skipped==
        (!test&&!V.options.pool_ownership_reference&&!pool_reference?1:0));
    // The proof cannot cross a submission boundary or hide a fresh CPU edit.
    dm_guest_write_prepare(data,4096);memset(data,0x55,4096);
    mtl_sub_begin();CHECK(vk_pool_address_stage((uintptr_t)data,4096,(void **)&buffer,&off,VK_PIPELINE_STAGE_TRANSFER_BIT));
    snapshot=vk_temp_buffer(4096);copy.srcOffset=off;
    vk_barrier();vkCmdCopyBuffer(V.cmd,buffer->handle,snapshot->handle,1,&copy);vk_barrier();
    vk_effect_copy(result,snapshot->map,4096);CHECK(mtl_wait(mtl_sub_commit(),UINT64_MAX));
    for(unsigned i=0;i<4096;i++) CHECK(result[i]==0x55);
    CALL(nvnMemoryPoolFinalize,&pool);free(data);
  }
  LOG("[vulkan pool overwrite fixture] full write then read skips initial upload; prior reads, partial writes and next-submission CPU edits preserved");
}
static VkSemaphore block_gpu_queue(void) {
  VkSemaphoreTypeCreateInfoKHR type={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR,
    .semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE_KHR};
  VkSemaphoreCreateInfo info={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,.pNext=&type};
  VkSemaphore gate;VK_CHECK(vkCreateSemaphore(V.device,&info,NULL,&gate));
  uint64_t value=1;VkPipelineStageFlags stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  VkTimelineSemaphoreSubmitInfoKHR timeline={.sType=VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR,
    .waitSemaphoreValueCount=1,.pWaitSemaphoreValues=&value};
  VkSubmitInfo wait={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.pNext=&timeline,
    .waitSemaphoreCount=1,.pWaitSemaphores=&gate,.pWaitDstStageMask=&stage};
  pthread_mutex_lock(&V.queue_mu);VK_CHECK(vkQueueSubmit(V.queue,1,&wait,VK_NULL_HANDLE));
  pthread_mutex_unlock(&V.queue_mu);return gate;
}
typedef struct {
  const tex_t *texture;
  pthread_mutex_t mutex;
  pthread_cond_t cv;
  int returned;
} dump_present_state;
static void *dump_present_waiter(void *opaque) {
  dump_present_state *s=opaque;
  mtl_present(s->texture,1.0/60,NULL);
  pthread_mutex_lock(&s->mutex);s->returned=1;pthread_cond_signal(&s->cv);
  pthread_mutex_unlock(&s->mutex);return NULL;
}
// The real GPU stays gated both before the one-shot dump and after it. A
// diagnostic that is not reading pixels must not turn presentation synchronous.
static void frame_dump_async_test(const tex_t *texture) {
  if(!vk_async_submissions()||!V.timeline_semaphore) {
    LOG("[vulkan one-shot dump fixture] gated regression requires timeline semaphores");return;
  }
  for(unsigned pass=0;pass<3;pass++) {
    if(pass==1) {
      CHECK(!mtl_present(texture,1.0/60,NULL));
      FILE *f=fopen(V.options.frame_dump,"rb");CHECK(f!=NULL);
      char header[14];CHECK(fread(header,1,13,f)==13&&!memcmp(header,"P6\n16 16\n255\n",13));
      for(unsigned i=0;i<256;i++) {
        uint8_t rgb[3];CHECK(fread(rgb,1,3,f)==3);
        CHECK(rgb[0]==255&&rgb[1]==128&&rgb[2]==0);
      }
      CHECK(fgetc(f)==EOF);CHECK(!fclose(f));continue;
    }
    VkSemaphoreTypeCreateInfoKHR type={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR,
      .semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE_KHR};
    VkSemaphoreCreateInfo info={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,.pNext=&type};
    VkSemaphore gate;VK_CHECK(vkCreateSemaphore(V.device,&info,NULL,&gate));
    uint64_t value=1;VkPipelineStageFlags stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkTimelineSemaphoreSubmitInfoKHR timeline={.sType=VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR,
      .waitSemaphoreValueCount=1,.pWaitSemaphoreValues=&value};
    VkSubmitInfo wait={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.pNext=&timeline,
      .waitSemaphoreCount=1,.pWaitSemaphores=&gate,.pWaitDstStageMask=&stage};
    pthread_mutex_lock(&V.queue_mu);VK_CHECK(vkQueueSubmit(V.queue,1,&wait,VK_NULL_HANDLE));
    pthread_mutex_unlock(&V.queue_mu);
    mtl_sub_begin();vk_buffer *buffer=vk_temp_buffer(16);
    vkCmdFillBuffer(V.cmd,buffer->handle,0,16,0x35791357);
    uint32_t result[4]={0};vk_effect_copy(result,buffer->map,sizeof result);
    uint64_t ticket=vk_internal_commit();
    dump_present_state state={.texture=texture,.mutex=PTHREAD_MUTEX_INITIALIZER,.cv=PTHREAD_COND_INITIALIZER};
    pthread_t thread;CHECK(!pthread_create(&thread,NULL,dump_present_waiter,&state));
    struct timespec deadline;clock_gettime(CLOCK_REALTIME,&deadline);deadline.tv_sec++;
    pthread_mutex_lock(&state.mutex);
    while(!state.returned) {
      int status=pthread_cond_timedwait(&state.cv,&state.mutex,&deadline);
      if(status==ETIMEDOUT) break;
      CHECK(!status);
    }
    int returned=state.returned;pthread_mutex_unlock(&state.mutex);
    CHECK(vk_completed_serial()<ticket&&result[0]==0);
    VkSemaphoreSignalInfoKHR signal={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO_KHR,
      .semaphore=gate,.value=1};VK_CHECK(V.signal_timeline(V.device,&signal));
    CHECK(!pthread_join(thread,NULL));vk_internal_wait(ticket);
    vkDestroySemaphore(V.device,gate,NULL);
    pthread_cond_destroy(&state.cv);pthread_mutex_destroy(&state.mutex);
    CHECK(returned);for(unsigned i=0;i<4;i++) CHECK(result[i]==0x35791357);
  }
  LOG("[vulkan one-shot dump fixture] present returns while GPU is gated before/after capture; actual capture RGB and fence publication passed");
}
// 超过 Vulkan 最低 4096 次分配上限的小资源，必须由少量实际内存块承载。
// More small resources than Vulkan's minimum 4096-allocation limit must share
// a bounded number of actual device-memory blocks, retaining GPU-visible bytes.
static void memory_suballocation_test(void) {
  enum { COUNT = 4104, BYTES = 64 };
  mtl_sub_begin();
  vk_buffer *buffers = calloc(COUNT, sizeof *buffers);
  CHECK(buffers != NULL);
  for (unsigned i = 0; i < COUNT; i++) {
    vk_buffer_create(&buffers[i], BYTES, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    memset(buffers[i].map, (i % 251) + 1, BYTES);
  }
  uint32_t blocks, allocations;
  uint64_t allocated_bytes;
  vk_memory_stats(&blocks, &allocations, &allocated_bytes);
  CHECK(allocations >= COUNT && blocks < COUNT / 8);
  const unsigned selected[] = {0, COUNT / 2, COUNT - 1};
  vk_buffer *destination = vk_temp_buffer(sizeof selected / sizeof selected[0] * BYTES);
  uint8_t result[3 * BYTES] = {0};
  vk_barrier();
  for (unsigned i = 0; i < 3; i++) {
    VkBufferCopy copy = {.dstOffset = i * BYTES, .size = BYTES};
    vkCmdCopyBuffer(V.cmd, buffers[selected[i]].handle, destination->handle, 1, &copy);
  }
  vk_barrier();
  vk_effect_copy(result, destination->map, sizeof result);
  CHECK(mtl_wait(mtl_sub_commit(), UINT64_MAX));
  for (unsigned i = 0; i < 3; i++)
    for (unsigned n = 0; n < BYTES; n++)
      CHECK(result[i * BYTES + n] == (selected[i] % 251) + 1);
  for (unsigned i = 0; i < COUNT; i++) vk_buffer_destroy(&buffers[i]);
  free(buffers);
  LOG("[vulkan memory fixture] passed: %u small buffers, %u device-memory blocks, %llu allocated bytes; GPU copied first/middle/last", COUNT, blocks, (unsigned long long)allocated_bytes);
}
typedef struct {
  VkSemaphore gate;
  uint64_t waits;
  const uint8_t *mirror;
  unsigned size;
  int valid;
} upload_gate_state;
static void *upload_gate_release(void *opaque) {
  upload_gate_state *state = opaque;
  uint64_t deadline = now_ns() + UINT64_C(5000000000);
  while (vk_pool_upload_waits() == state->waits && now_ns() < deadline) usleep(1000);
  state->valid = vk_pool_upload_waits() > state->waits;
  for (unsigned i = 0; i < state->size; i++) if (state->mirror[i] != 0x11) state->valid = 0;
  VkSemaphoreSignalInfoKHR signal = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO_KHR,
                                     .semaphore = state->gate, .value = 1};
  if (V.signal_timeline(V.device, &signal) != VK_SUCCESS) state->valid = 0;
  return NULL;
}
typedef struct {
  pthread_mutex_t mutex;
  pthread_cond_t cv;
  int returned;
} unchanged_upload_state;
static void *unchanged_upload_worker(void *opaque) {
  unchanged_upload_state *state=opaque;
  vk_pool_sync();
  pthread_mutex_lock(&state->mutex);state->returned=1;
  pthread_cond_signal(&state->cv);pthread_mutex_unlock(&state->mutex);
  return NULL;
}
// Dirty tracking records writes, including stores of the same value. Such a
// page needs no upload and must not wait for an unrelated pending publication.
// The queued GPU write must still reach both its next GPU consumer and the CPU.
static void unchanged_upload_publication_test(void) {
  if(!vk_async_submissions()||!V.timeline_semaphore) {
    LOG("[vulkan unchanged upload fixture] skipped: async/timeline required");return;
  }
  uint8_t *data;CHECK(!posix_memalign((void **)&data,65536,65536));
  memset(data,0x11,65536);
  NVNmemoryPoolBuilder builder;CALL(nvnMemoryPoolBuilderSetDefaults,&builder);
  CALL(nvnMemoryPoolBuilderSetDevice,&builder,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&builder,data,65536);
  CALL(nvnMemoryPoolBuilderSetFlags,&builder,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  NVNmemoryPool pool;CHECK(CALL(nvnMemoryPoolInitialize,&pool,&builder));
  vk_pool_sync();
  VkSemaphore gate=block_gpu_queue();
  mtl_sub_begin();vk_buffer *buffer;uint64_t offset;
  CHECK(mtl_addr((uintptr_t)data,64,(void **)&buffer,&offset));
  vkCmdFillBuffer(V.cmd,buffer->handle,offset,64,0x22222222);
  vk_buffer_writeback_stage((uintptr_t)data,64,VK_PIPELINE_STAGE_TRANSFER_BIT);
  uint64_t ticket=vk_internal_commit(),waits=vk_pool_upload_waits();
  // Force a dirty generation without changing any CPU bytes.
  dm_guest_write_prepare(data,1);*(volatile uint8_t *)data=0x11;
  unchanged_upload_state state={.mutex=PTHREAD_MUTEX_INITIALIZER,.cv=PTHREAD_COND_INITIALIZER};
  pthread_t thread;CHECK(!pthread_create(&thread,NULL,unchanged_upload_worker,&state));
  struct timespec deadline;clock_gettime(CLOCK_REALTIME,&deadline);deadline.tv_sec++;
  pthread_mutex_lock(&state.mutex);
  while(!state.returned) {
    int status=pthread_cond_timedwait(&state.cv,&state.mutex,&deadline);
    if(status==ETIMEDOUT) break;
    CHECK(!status);
  }
  int returned=state.returned;pthread_mutex_unlock(&state.mutex);
  CHECK(vk_completed_serial()<ticket&&data[0]==0x11);
  VkSemaphoreSignalInfoKHR signal={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO_KHR,
    .semaphore=gate,.value=1};VK_CHECK(V.signal_timeline(V.device,&signal));
  CHECK(!pthread_join(thread,NULL));vk_internal_wait(ticket);
  vkDestroySemaphore(V.device,gate,NULL);
  pthread_cond_destroy(&state.cv);pthread_mutex_destroy(&state.mutex);
  CHECK(returned);CHECK(vk_pool_upload_waits()==waits);
  for(unsigned i=0;i<64;i++) CHECK(data[i]==0x22&&buffer->map[offset+i]==0x22);
  // A subsequent GPU copy consumes the actual GPU-produced bytes, never the
  // stale CPU page that was correctly left alone while the queue was gated.
  mtl_sub_begin();CHECK(mtl_addr((uintptr_t)data,64,(void **)&buffer,&offset));
  vk_buffer *copy=vk_temp_buffer(64);VkBufferCopy region={.srcOffset=offset,.size=64};
  vk_barrier();vkCmdCopyBuffer(V.cmd,buffer->handle,copy->handle,1,&region);vk_barrier();
  uint8_t result[64]={0};vk_effect_copy(result,copy->map,64);
  CHECK(mtl_wait(mtl_sub_commit(),UINT64_MAX));
  for(unsigned i=0;i<64;i++) CHECK(result[i]==0x22);
  CALL(nvnMemoryPoolFinalize,&pool);free(data);
  LOG("[vulkan unchanged upload fixture] unchanged dirty CPU page proceeds before GPU publication; GPU and CPU results preserved");
}
typedef struct {
  unchanged_upload_state done;
  uint8_t *data;
  uint8_t result[192];
  uint64_t ticket;
} queued_upload_state;
static void *queued_upload_worker(void *opaque) {
  queued_upload_state *state=opaque;
  mtl_sub_begin();vk_buffer *source;uint64_t offset;
  CHECK(vk_pool_address_stage((uintptr_t)state->data,sizeof state->result,(void **)&source,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
  vk_buffer *copy=vk_temp_buffer(sizeof state->result);
  VkBufferCopy region={.srcOffset=offset,.size=sizeof state->result};
  vk_barrier();vkCmdCopyBuffer(V.cmd,source->handle,copy->handle,1,&region);vk_barrier();
  vk_effect_copy(state->result,copy->map,sizeof state->result);
  state->ticket=vk_internal_commit();
  pthread_mutex_lock(&state->done.mutex);state->done.returned=1;
  pthread_cond_signal(&state->done.cv);pthread_mutex_unlock(&state->done.mutex);
  return NULL;
}
static void queued_upload_overlap_test(void) {
  if(!vk_async_submissions()||!V.timeline_semaphore||vk_submission_barrier_mode()!=0) {
    LOG("[vulkan queued upload fixture] skipped: scoped async/timeline required");return;
  }
  uint8_t *data;CHECK(!posix_memalign((void **)&data,65536,65536));memset(data,0x11,65536);
  NVNmemoryPoolBuilder builder;CALL(nvnMemoryPoolBuilderSetDefaults,&builder);
  CALL(nvnMemoryPoolBuilderSetDevice,&builder,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&builder,data,65536);
  CALL(nvnMemoryPoolBuilderSetFlags,&builder,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  NVNmemoryPool pool;CHECK(CALL(nvnMemoryPoolInitialize,&pool,&builder));vk_pool_sync();
  VkSemaphore gate=block_gpu_queue();mtl_sub_begin();vk_buffer *buffer;uint64_t offset;
  CHECK(vk_pool_address_stage((uintptr_t)data,192,(void **)&buffer,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
  vkCmdFillBuffer(V.cmd,buffer->handle,offset,64,0x22222222);
  vk_buffer_writeback_stage((uintptr_t)data,64,VK_PIPELINE_STAGE_TRANSFER_BIT);
  vk_buffer *old_copy=vk_temp_buffer(192);uint8_t old_result[192]={0};
  VkBufferCopy old_region={.srcOffset=offset,.size=192};
  vk_barrier();vkCmdCopyBuffer(V.cmd,buffer->handle,old_copy->handle,1,&old_region);vk_barrier();
  vk_effect_copy(old_result,old_copy->map,192);
  uint64_t first=vk_internal_commit(),waits=vk_pool_upload_waits();
  // Same page, separate byte ownership: the new CPU edit must not wait for
  // the old GPU publication, nor copy stale CPU bytes over that GPU result.
  dm_guest_write_prepare(data+128,64);memset(data+128,0x33,64);
  queued_upload_state state={.done={.mutex=PTHREAD_MUTEX_INITIALIZER,
    .cv=PTHREAD_COND_INITIALIZER},.data=data};
  pthread_t thread;CHECK(!pthread_create(&thread,NULL,queued_upload_worker,&state));
  struct timespec deadline;clock_gettime(CLOCK_REALTIME,&deadline);deadline.tv_sec++;
  pthread_mutex_lock(&state.done.mutex);
  while(!state.done.returned) {
    int status=pthread_cond_timedwait(&state.done.cv,&state.done.mutex,&deadline);
    if(status==ETIMEDOUT) break;
    CHECK(!status);
  }
  int returned=state.done.returned;pthread_mutex_unlock(&state.done.mutex);
  CHECK(vk_completed_serial()<first&&data[0]==0x11);
  VkSemaphoreSignalInfoKHR signal={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO_KHR,
    .semaphore=gate,.value=1};VK_CHECK(V.signal_timeline(V.device,&signal));
  CHECK(!pthread_join(thread,NULL));vk_internal_wait(state.ticket);
  vkDestroySemaphore(V.device,gate,NULL);
  pthread_cond_destroy(&state.done.cv);pthread_mutex_destroy(&state.done.mutex);
  CHECK(returned);CHECK(vk_pool_upload_waits()==waits);
  for(unsigned i=0;i<192;i++) {
    uint8_t expected=i<64?0x22:i<128?0x11:0x33;
    CHECK(state.result[i]==expected&&data[i]==expected&&buffer->map[offset+i]==expected);
    CHECK(old_result[i]==(i<64?0x22:0x11));
  }
  CALL(nvnMemoryPoolFinalize,&pool);free(data);
  LOG("[vulkan queued upload fixture] CPU commit proceeds while GPU is gated; same-page GPU output and new CPU bytes survive in queue order");
}
static void pool_stage_ranges_test(void) {
  if(!vk_async_submissions()||!V.timeline_semaphore) {
    LOG("[vulkan pool stage fixture] skipped: async/timeline required");return;
  }
  uint8_t *data;CHECK(!posix_memalign((void **)&data,65536,65536));memset(data,0,65536);
  NVNmemoryPoolBuilder builder;CALL(nvnMemoryPoolBuilderSetDefaults,&builder);
  CALL(nvnMemoryPoolBuilderSetDevice,&builder,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&builder,data,65536);
  CALL(nvnMemoryPoolBuilderSetFlags,&builder,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  NVNmemoryPool pool;CHECK(CALL(nvnMemoryPoolInitialize,&pool,&builder));
  uint64_t lookup_hits0,lookup_ranges0,lookup_slow0;int lookup_reference;
  vk_pool_lookup_stats(&lookup_hits0,&lookup_ranges0,&lookup_slow0,&lookup_reference);
  VkSemaphore gate=block_gpu_queue();mtl_sub_begin();vk_buffer *buffer;uint64_t offset;
  // Declare three separate page ranges, including the safe unknown-use path.
  // The live queue gate prevents completion from erasing the access history.
  CHECK(vk_pool_address_stage((uintptr_t)data,64,(void **)&buffer,&offset,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT));
  // Distinct byte ranges on the same page may share bitmap work, but a new
  // stage must extend the proof rather than inherit the previous stage mask.
  CHECK(vk_pool_address_stage((uintptr_t)data+128,64,(void **)&buffer,&offset,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT));
  CHECK(vk_pool_address_stage((uintptr_t)data+256,64,(void **)&buffer,&offset,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT));
  CHECK(vk_pool_address_stage((uintptr_t)data+512,128,(void **)&buffer,&offset,
      VK_PIPELINE_STAGE_VERTEX_INPUT_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT));
  CHECK(vk_pool_address_stage((uintptr_t)data+4096,64,(void **)&buffer,&offset,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT));
  CHECK(mtl_addr((uintptr_t)data+8192,64,(void **)&buffer,&offset));
  uint64_t serial=vk_internal_commit();
  CHECK(vk_completed_serial()<serial);
  CHECK(vk_pool_pending_stages((uintptr_t)data,64)==
        (VK_PIPELINE_STAGE_VERTEX_INPUT_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT));
  CHECK(vk_pool_pending_stages((uintptr_t)data+4096,64)==VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  CHECK(vk_pool_pending_stages((uintptr_t)data+8192,64)==VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
  CHECK(vk_pool_pending_stages((uintptr_t)data+12288,64)==0);
  CHECK(vk_pool_pending_stages((uintptr_t)data+4092,8)==
        (VK_PIPELINE_STAGE_VERTEX_INPUT_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT));
  uint64_t lookup_hits,lookup_ranges,lookup_slow;
  vk_pool_lookup_stats(&lookup_hits,&lookup_ranges,&lookup_slow,&lookup_reference);
  if(!lookup_reference)CHECK(lookup_ranges>=lookup_ranges0+2);
  VkSemaphoreSignalInfoKHR signal={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO_KHR,.semaphore=gate,.value=1};
  VK_CHECK(V.signal_timeline(V.device,&signal));vk_internal_wait(serial);
  CHECK(vk_pool_pending_stages((uintptr_t)data,12288)==0);
  vkDestroySemaphore(V.device,gate,NULL);CALL(nvnMemoryPoolFinalize,&pool);free(data);
  LOG("[vulkan pool stage fixture] pending access scopes retain separate pages, union crossing ranges, preserve unknown fallback, and retire only after actual completion");
}
// Actual GPU copies after interleaved page declarations. The single-word
// proof must inspect all requested stage/initial bits, including a mixed
// range whose initial contents come partly from CPU and partly from GPU.
static void pool_word_coverage_test(void) {
  enum { page=4096,bytes=128*page,nreads=15 };
  const unsigned read_pages[nreads]={0,1,2,4,8,9,10,11,12,13,63,64,65,67,127};
  uint64_t hits,ranges,slow;int saved_reference;
  vk_pool_lookup_stats(&hits,&ranges,&slow,&saved_reference);
  uint8_t *data;CHECK(!posix_memalign((void **)&data,65536,bytes));memset(data,0x11,bytes);
  NVNmemoryPoolBuilder builder;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&builder);CALL(nvnMemoryPoolBuilderSetDevice,&builder,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&builder,data,bytes);
  CALL(nvnMemoryPoolBuilderSetFlags,&builder,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&builder));
  for(unsigned reference=0;reference<2;reference++) {
    vk_pool_lookup_reference(reference);
    // Establish old GPU contents first; missing an initial-content upgrade
    // below must expose stale bytes rather than freshly materialized storage.
    mtl_sub_begin();vk_buffer *buffer;uint64_t offset;
    CHECK(vk_pool_address_stage((uintptr_t)data,bytes,(void **)&buffer,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
    vk_buffer *warm=vk_temp_buffer(bytes);VkBufferCopy all={.srcOffset=offset,.size=bytes};
    vk_barrier();vkCmdCopyBuffer(V.cmd,buffer->handle,warm->handle,1,&all);vk_barrier();
    CHECK(mtl_wait(mtl_sub_commit(),UINT64_MAX));
    dm_guest_write_prepare(data,bytes);
    for(unsigned p=0;p<128;p++) memset(data+p*page,0x20+reference*0x10+p%16,page);
    int active_reference;vk_pool_lookup_stats(&hits,&ranges,&slow,&active_reference);
    CHECK(active_reference==(int)reference);
    uint64_t before=ranges;
    VkSemaphore gate=vk_async_submissions()&&V.timeline_semaphore?block_gpu_queue():VK_NULL_HANDLE;
    mtl_sub_begin();
    VkPipelineStageFlags fs=VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    VkPipelineStageFlags vs=VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
#define WORD_READ(p,n,s) CHECK(vk_pool_address_stage((uintptr_t)data+(p)*page,(n)*page,(void **)&buffer,&offset,(s)))
#define WORD_DEST(p,n) CHECK(vk_pool_address_overwrite((uintptr_t)data+(p)*page,(n),(void **)&buffer,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT))
    WORD_READ(0,1,fs);WORD_READ(2,1,fs);WORD_READ(0,1,fs);
    WORD_READ(1,1,fs);WORD_READ(0,3,fs); // union of separate earlier ranges
    WORD_READ(0,1,vs);WORD_READ(2,1,vs);WORD_READ(0,3,fs|vs); // page1 needs VS
    WORD_READ(4,1,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    WORD_READ(6,1,fs);WORD_READ(4,1,fs); // ALL does not populate the FS bitmap
    WORD_DEST(8,page);WORD_READ(9,1,VK_PIPELINE_STAGE_TRANSFER_BIT);
    WORD_READ(8,1,VK_PIPELINE_STAGE_TRANSFER_BIT); // initial=0 -> initial=1
    WORD_DEST(10,page);vk_barrier();vkCmdFillBuffer(V.cmd,buffer->handle,offset,page,0x44444444);
    vk_buffer_transfer_written_range((uintptr_t)data+10*page,page);vk_pool_overwritten((uintptr_t)data+10*page,page);
    WORD_READ(11,1,VK_PIPELINE_STAGE_TRANSFER_BIT);
    WORD_READ(10,2,VK_PIPELINE_STAGE_TRANSFER_BIT); // overwritten | upload union
    WORD_DEST(12,page/2);vk_barrier();vkCmdFillBuffer(V.cmd,buffer->handle,offset,page/2,0x66666666);
    vk_buffer_transfer_written_range((uintptr_t)data+12*page,page/2);vk_pool_overwritten((uintptr_t)data+12*page,page/2);
    WORD_READ(13,1,VK_PIPELINE_STAGE_TRANSFER_BIT);
    WORD_READ(12,2,VK_PIPELINE_STAGE_TRANSFER_BIT); // partial write still uploads
    WORD_READ(63,2,VK_PIPELINE_STAGE_TRANSFER_BIT); // two words: original path
    WORD_READ(65,1,VK_PIPELINE_STAGE_TRANSFER_BIT);WORD_READ(67,1,VK_PIPELINE_STAGE_TRANSFER_BIT);
    WORD_READ(65,1,VK_PIPELINE_STAGE_TRANSFER_BIT);WORD_READ(127,1,VK_PIPELINE_STAGE_TRANSFER_BIT);
#undef WORD_READ
#undef WORD_DEST
    vk_buffer *copy=vk_temp_buffer(nreads*page);VkBufferCopy regions[nreads];uint8_t result[nreads*page];
    for(unsigned k=0;k<nreads;k++) regions[k]=(VkBufferCopy){.srcOffset=read_pages[k]*page,.dstOffset=k*page,.size=page};
    vk_barrier();vkCmdCopyBuffer(V.cmd,buffer->handle,copy->handle,nreads,regions);vk_barrier();
    vk_effect_copy(result,copy->map,sizeof result);uint64_t serial=vk_internal_commit();
    if(gate) {
      CHECK(vk_pool_pending_stages((uintptr_t)data,page)==(fs|vs));
      CHECK(vk_pool_pending_stages((uintptr_t)data+page,page)==(fs|vs));
      CHECK(vk_pool_pending_stages((uintptr_t)data+2*page,page)==(fs|vs));
      CHECK(vk_pool_pending_stages((uintptr_t)data+3*page,page)==0);
      CHECK(vk_pool_pending_stages((uintptr_t)data+4*page,page)==(fs|VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
      CHECK(vk_pool_pending_stages((uintptr_t)data+66*page,page)==0);
      CHECK(vk_pool_pending_stages((uintptr_t)data+127*page,page)==VK_PIPELINE_STAGE_TRANSFER_BIT);
      VkSemaphoreSignalInfoKHR signal={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO_KHR,.semaphore=gate,.value=1};
      VK_CHECK(V.signal_timeline(V.device,&signal));
    }
    vk_internal_wait(serial);if(gate) vkDestroySemaphore(V.device,gate,NULL);
    int mode;vk_pool_lookup_stats(&hits,&ranges,&slow,&mode);CHECK(mode==(int)reference);
    CHECK(reference?ranges==before:ranges>=before+7);
    for(unsigned k=0;k<nreads;k++) for(unsigned b=0;b<page;b++) {
      unsigned p=read_pages[k];uint8_t expected=p==10?0x44:p==12&&b<page/2?0x66:0x20+reference*0x10+p%16;
      CHECK(result[k*page+b]==expected);
    }
  }
  vk_pool_lookup_reference(saved_reference);
  CALL(nvnMemoryPoolFinalize,&pool);free(data);
  LOG("[vulkan pool word fixture] interleaved/union pages, stage upgrades, initial upgrade, GPU/CPU union, partial write, word boundary/end and new submission match reference GPU bytes");
}
typedef struct {
  unchanged_upload_state done;
  uint8_t *data;
  uint8_t result[8192];
  uint64_t ticket;
} grouped_upload_state;
static void *grouped_upload_worker(void *opaque) {
  grouped_upload_state *state=opaque;
  mtl_sub_begin();vk_buffer *buffer;uint64_t offset;
  CHECK(vk_pool_address_stage((uintptr_t)state->data,4096,(void **)&buffer,&offset,
      VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT));
  CHECK(vk_pool_address_stage((uintptr_t)state->data+4096,4096,(void **)&buffer,&offset,
      VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_VERTEX_INPUT_BIT));
  vk_buffer *copy=vk_temp_buffer(sizeof state->result);
  VkBufferCopy region={.size=sizeof state->result};
  vk_barrier();vkCmdCopyBuffer(V.cmd,buffer->handle,copy->handle,1,&region);vk_barrier();
  vk_effect_copy(state->result,copy->map,sizeof state->result);
  state->ticket=vk_internal_commit();
  pthread_mutex_lock(&state->done.mutex);state->done.returned=1;
  pthread_cond_signal(&state->done.cv);pthread_mutex_unlock(&state->done.mutex);return NULL;
}
static void grouped_upload_ranges_test(void) {
  if(!vk_async_submissions()||!V.timeline_semaphore||vk_submission_barrier_mode()!=0) {
    LOG("[vulkan grouped upload fixture] skipped: scoped async/timeline required");return;
  }
  uint8_t *data;CHECK(!posix_memalign((void **)&data,65536,65536));memset(data,0x11,65536);
  NVNmemoryPoolBuilder builder;CALL(nvnMemoryPoolBuilderSetDefaults,&builder);
  CALL(nvnMemoryPoolBuilderSetDevice,&builder,&device);CALL(nvnMemoryPoolBuilderSetStorage,&builder,data,65536);
  CALL(nvnMemoryPoolBuilderSetFlags,&builder,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  NVNmemoryPool pool;CHECK(CALL(nvnMemoryPoolInitialize,&pool,&builder));
  VkSemaphore gate=block_gpu_queue();mtl_sub_begin();vk_buffer *buffer;uint64_t offset;
  // Distinct phase declarations are conservative supersets of the real copy
  // accesses. Their reversed page order exercises independent upload grouping.
  CHECK(vk_pool_address_stage((uintptr_t)data,4096,(void **)&buffer,&offset,
      VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT));
  CHECK(vk_pool_address_stage((uintptr_t)data+4096,4096,(void **)&buffer,&offset,
      VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_VERTEX_INPUT_BIT));
  vkCmdFillBuffer(V.cmd,buffer->handle,64,64,0x22222222);
  vk_buffer_writeback_stage((uintptr_t)data+64,64,VK_PIPELINE_STAGE_TRANSFER_BIT);
  vk_buffer *old_copy=vk_temp_buffer(8192);uint8_t old_result[8192]={0};
  VkBufferCopy region={.size=8192};vk_barrier();
  vkCmdCopyBuffer(V.cmd,buffer->handle,old_copy->handle,1,&region);vk_barrier();
  vk_effect_copy(old_result,old_copy->map,sizeof old_result);uint64_t first=vk_internal_commit();
  dm_guest_write_prepare(data,64);memset(data,0x33,64);
  dm_guest_write_prepare(data+4096,64);memset(data+4096,0x44,64);
  grouped_upload_state state={.done={.mutex=PTHREAD_MUTEX_INITIALIZER,.cv=PTHREAD_COND_INITIALIZER},.data=data};
  pthread_t thread;CHECK(!pthread_create(&thread,NULL,grouped_upload_worker,&state));
  struct timespec deadline;clock_gettime(CLOCK_REALTIME,&deadline);deadline.tv_sec++;
  pthread_mutex_lock(&state.done.mutex);
  while(!state.done.returned) {
    int status=pthread_cond_timedwait(&state.done.cv,&state.done.mutex,&deadline);
    if(status==ETIMEDOUT) break;
    CHECK(!status);
  }
  int returned=state.done.returned;pthread_mutex_unlock(&state.done.mutex);
  CHECK(vk_completed_serial()<first);
  VkSemaphoreSignalInfoKHR signal={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO_KHR,.semaphore=gate,.value=1};
  VK_CHECK(V.signal_timeline(V.device,&signal));CHECK(!pthread_join(thread,NULL));vk_internal_wait(state.ticket);
  CHECK(returned);
  for(unsigned i=0;i<8192;i++) {
    uint8_t old=i>=64&&i<128?0x22:0x11;
    uint8_t expected=i<64?0x33:i>=4096&&i<4160?0x44:old;
    CHECK(old_result[i]==old&&state.result[i]==expected&&buffer->map[i]==expected);
  }
  vkDestroySemaphore(V.device,gate,NULL);pthread_cond_destroy(&state.done.cv);pthread_mutex_destroy(&state.done.mutex);
  CALL(nvnMemoryPoolFinalize,&pool);free(data);
  LOG("[vulkan grouped upload fixture] independent page groups preserve earlier GPU reads, same-page GPU outputs and both CPU edits without waiting for the gate");
}
static void pool_upload_hazard_test(void) {
  if (!V.timeline_semaphore) {
    LOG("[vulkan pool upload fixture] skipped: timeline semaphore unavailable");
    return;
  }
  enum { POOL_BYTES = 1024 * 1024 };
  uint8_t *data;
  CHECK(!posix_memalign((void **)&data, 65536, POOL_BYTES));
  memset(data, 0, POOL_BYTES);
  NVNmemoryPoolBuilder builder;
  CALL(nvnMemoryPoolBuilderSetDefaults, &builder);
  CALL(nvnMemoryPoolBuilderSetDevice, &builder, &device);
  CALL(nvnMemoryPoolBuilderSetStorage, &builder, data, POOL_BYTES);
  CALL(nvnMemoryPoolBuilderSetFlags, &builder,
       NVN_MEMORY_POOL_FLAGS_CPU_CACHED | NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  NVNmemoryPool pool;
  CHECK(CALL(nvnMemoryPoolInitialize, &pool, &builder));
  // 首位、跨位图字、完整中间字、池尾；同一池跨提交复用以验证索引失效。
  // First bit, word boundary, complete interior words, tail, and the same pool
  // across submissions to verify recording-index invalidation.
  const struct { uint64_t offset, size, edit; unsigned edit_size; } cases[] = {
      {0, 64, 0, 64},
      {63 * 4096 + 4092, 8, 64 * 4096, 4},
      {2 * 4096 + 4, 128 * 4096, 80 * 4096 + 8, 64},
      {POOL_BYTES - 4, 4, POOL_BYTES - 4, 4}};
  for (unsigned test = 0; test < sizeof cases / sizeof cases[0]; test++) {
    uint64_t start = cases[test].offset, size = cases[test].size,
             edit = cases[test].edit;
    unsigned edit_size = cases[test].edit_size;
    memset(data, 0, POOL_BYTES);
    memset(data + start, 0x11, size);
    mtl_sub_begin();
    vk_buffer *source;
    uint64_t offset;
    CHECK(mtl_addr((uintptr_t)data + start, size, (void **)&source, &offset));
    vk_buffer *destination = vk_temp_buffer(size);
    VkBufferCopy copy = {.srcOffset = offset, .size = size};
    vkCmdCopyBuffer(V.cmd, source->handle, destination->handle, 1, &copy);
    vk_barrier();
    uint8_t *result = calloc(1, size);
    CHECK(result != NULL);
    vk_effect_copy(result, destination->map, size);
    VkSemaphoreTypeCreateInfoKHR type = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE_KHR};
    VkSemaphoreCreateInfo create = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                                   .pNext = &type};
    VkSemaphore gate;
    VK_CHECK(vkCreateSemaphore(V.device, &create, NULL, &gate));
    uint64_t value = 1;
    VkTimelineSemaphoreSubmitInfoKHR timeline = {
        .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR,
        .waitSemaphoreValueCount = 1, .pWaitSemaphoreValues = &value};
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo blocker = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .pNext = &timeline,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &gate, .pWaitDstStageMask = &stage};
    pthread_mutex_lock(&V.queue_mu);
    VK_CHECK(vkQueueSubmit(V.queue, 1, &blocker, VK_NULL_HANDLE));
    pthread_mutex_unlock(&V.queue_mu);
    uint64_t fence = mtl_sub_commit(), waits = vk_pool_upload_waits();
    CHECK(!mtl_wait(fence, 0));
    data[4096] = 0x33;
    vk_pool_sync();
    CHECK(!mtl_wait(fence, 0));
    CHECK(vk_pool_upload_waits() == waits);
    CHECK(source->map[offset - start + 4096] == 0x33);
    memset(data + edit, 0x22, edit_size);
    upload_gate_state state = {gate, waits, source->map + offset - start + edit,
                                edit_size, 0};
    pthread_t worker;
    CHECK(!pthread_create(&worker, NULL, upload_gate_release, &state));
    vk_pool_sync();
    CHECK(!pthread_join(worker, NULL));
    CHECK(state.valid);
    CHECK(vk_pool_upload_waits() == waits + 1);
    CHECK(mtl_wait(fence, 0));
    CHECK(mtl_last_fence() == fence);
    int old_gpu_data = 1;
    for (uint64_t i = 0; i < size; i++) old_gpu_data &= result[i] == 0x11;
    CHECK(old_gpu_data);
    for (unsigned i = 0; i < edit_size; i++) {
      CHECK(source->map[offset - start + edit + i] == 0x22);
      CHECK(data[edit + i] == 0x22);
    }
    free(result);
    vkDestroySemaphore(V.device, gate, NULL);
  }
  CALL(nvnMemoryPoolFinalize, &pool);
  free(data);
  LOG("[vulkan pool upload fixture] passed: unrelated page proceeds, overlapping upload waits; first bit, word boundary, interior words, tail and cross-submission reuse preserve old GPU/new CPU bytes");
}

// 同一个后端的两份真正提交同时在途，包含独立暂存、查询与 CPU 完成副作用。
// Two actual backend submissions in flight with independent staging, queries
// and CPU completion effects, rather than manually managed descriptor owners.
static void core_submission_overlap_test(void) {
  if (!vk_async_submissions() || !V.timeline_semaphore) {
    LOG("[vulkan core overlap fixture] skipped: async mode/timeline semaphore required");
    return;
  }
  uint8_t *data;
  CHECK(!posix_memalign((void **)&data, 65536, 65536));
  memset(data, 0, 65536);
  NVNmemoryPoolBuilder builder;
  CALL(nvnMemoryPoolBuilderSetDefaults, &builder);
  CALL(nvnMemoryPoolBuilderSetDevice, &builder, &device);
  CALL(nvnMemoryPoolBuilderSetStorage, &builder, data, 65536);
  CALL(nvnMemoryPoolBuilderSetFlags, &builder,
       NVN_MEMORY_POOL_FLAGS_CPU_CACHED | NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  NVNmemoryPool pool;
  CHECK(CALL(nvnMemoryPoolInitialize, &pool, &builder));
  memset(data, 0x31, 64);
  memset(data + 4096, 0x72, 64);
  VkSemaphoreTypeCreateInfoKHR type = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR,
      .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE_KHR};
  VkSemaphoreCreateInfo create = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                                 .pNext = &type};
  VkSemaphore gate;
  VK_CHECK(vkCreateSemaphore(V.device, &create, NULL, &gate));
  uint64_t value = 1;
  VkTimelineSemaphoreSubmitInfoKHR timeline = {
      .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR,
      .waitSemaphoreValueCount = 1, .pWaitSemaphoreValues = &value};
  VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  VkSubmitInfo blocker = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .pNext = &timeline,
      .waitSemaphoreCount = 1, .pWaitSemaphores = &gate, .pWaitDstStageMask = &stage};
  uint64_t fences[2];
  uint8_t result[2][64] = {{0}};
  VkBuffer staging[2];
  for (unsigned i = 0; i < 2; i++) {
    mtl_sub_begin();
    vk_buffer *source;
    uint64_t offset;
    CHECK(mtl_addr((uintptr_t)data + i * 4096, 64, (void **)&source, &offset));
    vk_buffer *destination = vk_temp_buffer(64);
    staging[i] = destination->handle;
    VkBufferCopy copy = {.srcOffset = offset, .size = 64};
    vkCmdCopyBuffer(V.cmd, source->handle, destination->handle, 1, &copy);
    vk_barrier();
    vk_effect_copy(result[i], destination->map, sizeof result[i]);
    if (!i) {
      pthread_mutex_lock(&V.queue_mu);
      VK_CHECK(vkQueueSubmit(V.queue, 1, &blocker, VK_NULL_HANDLE));
      pthread_mutex_unlock(&V.queue_mu);
    }
    fences[i] = mtl_sub_commit();
    CHECK(!mtl_wait(fences[i], 0));
  }
  CHECK(staging[0] != staging[1]);
  CHECK(vk_pool_pending_writes((uintptr_t)result[0], 64));
  CHECK(vk_pool_pending_writes((uintptr_t)result[1], 64));
  VkSemaphoreSignalInfoKHR signal = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO_KHR,
                                     .semaphore = gate, .value = 1};
  VK_CHECK(V.signal_timeline(V.device, &signal));
  CHECK(mtl_wait(fences[1], UINT64_MAX));
  for (unsigned i = 0; i < 64; i++) {
    CHECK(result[0][i] == 0x31);
    CHECK(result[1][i] == 0x72);
  }
  CHECK(!vk_pool_pending_writes((uintptr_t)result, sizeof result));
  vkDestroySemaphore(V.device, gate, NULL);
  CALL(nvnMemoryPoolFinalize, &pool);
  free(data);
  LOG("[vulkan core overlap fixture] passed: two actual backend submits pending together, independent staging/queries/effects and GPU pixels");
}

typedef struct {
  uint64_t ticket;
  const uint32_t *result;
  uint32_t expected;
  _Atomic int entered, returned;
  int effects_visible;
} present_ticket_state;
static void *present_ticket_waiter(void *opaque) {
  present_ticket_state *state = opaque;
  atomic_store(&state->entered, 1);
  mtl_present_wait(state->ticket);
  state->effects_visible = 1;
  for (unsigned i = 0; i < 64; i++)
    if (state->result[i] != state->expected) state->effects_visible = 0;
  atomic_store(&state->returned, 1);
  return NULL;
}
// 真 GPU 二进制信号链及内部出图票号；票号不能推进或误用客体栅栏域。
// Actual GPU binary semaphore chain and internal presentation tickets; neither
// advancing nor accidentally waiting in the guest-fence domain is allowed.
static void internal_present_chain_test(void) {
  if (!vk_async_submissions()) {
    LOG("[vulkan present chain fixture] skipped: async mode required");
    return;
  }
  if(V.timeline_semaphore) {
    uint64_t guest=mtl_last_fence();
    VkPipelineStageFlags stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    mtl_sub_begin();
    // Hold actual GPU execution. Present ownership must be returned after
    // submission while CPU-visible effects still await the real fence.
    VkSemaphore gate=VK_NULL_HANDLE;
    if(V.timeline_semaphore) {
      VkSemaphoreTypeCreateInfoKHR type={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR,
        .semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE_KHR};
      VkSemaphoreCreateInfo info={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,.pNext=&type};
      VK_CHECK(vkCreateSemaphore(V.device,&info,NULL,&gate));
      uint64_t value=1;
      VkTimelineSemaphoreSubmitInfoKHR timeline={.sType=VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR,
        .waitSemaphoreValueCount=1,.pWaitSemaphoreValues=&value};
      VkSubmitInfo wait={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.pNext=&timeline,
        .waitSemaphoreCount=1,.pWaitSemaphores=&gate,.pWaitDstStageMask=&stage};
      pthread_mutex_lock(&V.queue_mu);
      VK_CHECK(vkQueueSubmit(V.queue,1,&wait,VK_NULL_HANDLE));
      pthread_mutex_unlock(&V.queue_mu);
    }
    vk_buffer *destination=vk_temp_buffer(256);
    uint32_t value=0x679ab123, result[64]={0};
    vkCmdFillBuffer(V.cmd,destination->handle,0,256,value);
    vk_effect_copy(result,destination->map,sizeof result);
    uint64_t ticket=vk_internal_commit();
    present_ticket_state state={.ticket=ticket,.result=result,.expected=value};
    pthread_t waiter;
    CHECK(!pthread_create(&waiter,NULL,present_ticket_waiter,&state));
    uint64_t deadline=now_ns()+UINT64_C(1000000000);
    while(!atomic_load(&state.entered)&&now_ns()<deadline) usleep(1000);
    CHECK(atomic_load(&state.entered));
    if(gate) {
      if(vk_submission_barrier_mode()==0) {
        deadline=now_ns()+UINT64_C(1000000000);
        while(!atomic_load(&state.returned)&&now_ns()<deadline) usleep(1000);
        CHECK(atomic_load(&state.returned));
        CHECK(!state.effects_visible);
        CHECK(vk_completed_serial()<ticket);
        for(unsigned i=0;i<64;i++) CHECK(result[i]==0);
      }
      VkSemaphoreSignalInfoKHR signal={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO_KHR,
        .semaphore=gate,.value=1};
      VK_CHECK(V.signal_timeline(V.device,&signal));
    }
    vk_internal_wait(ticket);
    CHECK(!pthread_join(waiter,NULL));
    CHECK(atomic_load(&state.returned));
    CHECK(mtl_last_fence()==guest);
    for(unsigned i=0;i<64;i++) CHECK(result[i]==value);
    vkDestroySemaphore(V.device,gate,NULL);
  }
  VkSemaphore ready, finished;
  VkSemaphoreCreateInfo create = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VK_CHECK(vkCreateSemaphore(V.device, &create, NULL, &ready));
  VK_CHECK(vkCreateSemaphore(V.device, &create, NULL, &finished));
  vk_buffer source;
  vk_buffer_create(&source, 256, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  for (uint64_t round = 1; round <= 2; round++) {
    uint64_t guest = mtl_last_fence();
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    mtl_sub_begin();
    uint32_t value = 0x12ab0000u + round;
    vkCmdFillBuffer(V.cmd, source.handle, 0, 256, value);
    uint64_t first = vk_internal_commit_semaphores(VK_NULL_HANDLE, 0, ready);
    mtl_sub_begin();
    vk_buffer *destination = vk_temp_buffer(256);
    VkBufferCopy copy = {.size = 256};
    vkCmdCopyBuffer(V.cmd, source.handle, destination->handle, 1, &copy);
    uint32_t result[64] = {0};
    vk_effect_copy(result, destination->map, sizeof result);
    uint64_t ticket = vk_internal_commit_semaphores(ready, VK_PIPELINE_STAGE_TRANSFER_BIT, finished);
    CHECK(ticket > first);
    CHECK(mtl_last_fence() == guest);
    VkFence consumed;
    VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK_CHECK(vkCreateFence(V.device, &fence_info, NULL, &consumed));
    VkSubmitInfo consumer = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &finished, .pWaitDstStageMask = &stage};
    pthread_mutex_lock(&V.queue_mu);
    VK_CHECK(vkQueueSubmit(V.queue, 1, &consumer, consumed));
    pthread_mutex_unlock(&V.queue_mu);
    present_ticket_state state = {.ticket = ticket, .result = result, .expected = value};
    pthread_t waiter;
    CHECK(!pthread_create(&waiter, NULL, present_ticket_waiter, &state));
    uint64_t deadline = now_ns() + UINT64_C(1000000000);
    while (!atomic_load(&state.entered) && now_ns() < deadline) usleep(1000);
    CHECK(atomic_load(&state.entered));
    vk_internal_wait(ticket);
    deadline = now_ns() + UINT64_C(1000000000);
    while (!atomic_load(&state.returned) && now_ns() < deadline) usleep(1000);
    CHECK(atomic_load(&state.returned));
    CHECK(!pthread_join(waiter, NULL));
    CHECK(mtl_last_fence() == guest);
    for (unsigned i = 0; i < 64; i++) CHECK(result[i] == value);
    VK_CHECK(vkWaitForFences(V.device, 1, &consumed, VK_TRUE, UINT64_C(5000000000)));
    vkDestroyFence(V.device, consumed, NULL);
  }
  vkDestroySemaphore(V.device, finished, NULL);
  vkDestroySemaphore(V.device, ready, NULL);
  vk_buffer_destroy(&source);
  LOG("[vulkan present chain fixture] passed: submitted present ownership returns while GPU is gated; real fence retains CPU effects, semaphore consumption/reuse and guest fence domain");
}

// Cached negative texture-dirty proofs must expire on other-range writes,
// real write faults, host write preparation, and tracking removal. Untracked
// repeated edits remain conservative even when the publication epoch is stable.
static void texture_dirty_proof_test(void) {
  uint8_t *data;CHECK(!posix_memalign((void **)&data,65536,1<<20));memset(data,0,1<<20);
  uint8_t values[2][4]={{255,0,0,255},{0,255,0,255}};
  for(unsigned t=0;t<2;t++) for(unsigned b=0;b<65536;b+=4) memcpy(data+t*65536+b,values[t],4);
  NVNmemoryPoolBuilder builder;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&builder);CALL(nvnMemoryPoolBuilderSetDevice,&builder,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&builder,data,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&builder,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&builder));
  NVNtexture textures[2];
  for(unsigned t=0;t<2;t++) {
    NVNtextureBuilder b;CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&b,16);
    CALL(nvnTextureBuilderSetHeight,&b,16);CALL(nvnTextureBuilderSetFormat,&b,NVN_FORMAT_RGBA8);
    NVNpackagedTextureLayout packaged={{0}};CALL(nvnTextureBuilderSetPackagedTextureLayout,&b,&packaged);
    CALL(nvnTextureBuilderSetStorage,&b,&pool,t*65536);CHECK(CALL(nvnTextureInitialize,&textures[t],&b));
  }
  uint64_t hits_before,checks_before;vk_texture_dirty_stats(&hits_before,&checks_before);
  for(unsigned step=0;step<7;step++) {
    if(step==5) nvn_dirty_remove_range(data);
    if(step==1 || step==2 || step==3 || step==5 || step==6) {
      unsigned target=step==1?1:0;
      if(step==1 || step==3) dm_guest_write_prepare(data+target*65536,65536);
      values[target][0]=step*31;values[target][1]=step*17;values[target][2]=step*23;
      for(unsigned b=0;b<65536;b+=4) memcpy(data+target*65536+b,values[target],4);
    }
    mtl_sub_begin();vk_buffer *readback=vk_temp_buffer(2048);
    for(unsigned t=0;t<2;t++) {
      VkImage image=VK_NULL_HANDLE;
      for(unsigned repeat=0;repeat<4;repeat++) image=vk_texture_native((tex_t *)&textures[t]);
      VkBufferImageCopy copy={.bufferOffset=t*1024,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={16,16,1}};
      vk_barrier();vkCmdCopyImageToBuffer(V.cmd,image,VK_IMAGE_LAYOUT_GENERAL,readback->handle,1,&copy);
    }
    vk_internal_wait(vk_internal_commit());
    for(unsigned t=0;t<2;t++) for(unsigned pixel=0;pixel<256;pixel++) {
      const uint8_t *actual=readback->map+t*1024+pixel*4;
      if(memcmp(actual,values[t],4)) LOG("[vulkan texture dirty proof mismatch] step=%u texture=%u pixel=%u actual=%u,%u,%u,%u expected=%u,%u,%u,%u",step,t,pixel,actual[0],actual[1],actual[2],actual[3],values[t][0],values[t][1],values[t][2],values[t][3]);
      CHECK(!memcmp(actual,values[t],4));
    }
  }
  uint64_t hits_after,checks_after;vk_texture_dirty_stats(&hits_after,&checks_after);
  CHECK(checks_after>checks_before);
  const char *reference=getenv("DM_VULKAN_TEXTURE_DIRTY_PROOF_REFERENCE");
  if(!reference || strcmp(reference,"1")) CHECK(hits_after>hits_before);
  for(unsigned t=0;t<2;t++) CALL(nvnTextureFinalize,&textures[t]);
  CALL(nvnMemoryPoolFinalize,&pool);free(data);
  LOG("[vulkan texture dirty proof fixture] repeated reads, unrelated writes, faults, host edits and untracked repeated edits passed");
}
