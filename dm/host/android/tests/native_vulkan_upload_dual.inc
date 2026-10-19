// Test-only selection exercises the production direct-copy branch on real
// mapped Vulkan buffers. The game default remains experimental OFF.
#ifdef DM_VULKAN_TEST_EMBED
#include <sys/mman.h>
extern int vk_test_upload_dual_enable(int);
extern void vk_test_upload_copy_dual(uint8_t *,uint8_t *,const uint8_t *,size_t);
static int upload_dual_fixture_begin(void) {
  const unsigned sizes[]={0,1,2,3,4,7,8,15,16,17,31,32,63,64,65,127,128,129,193,255,256,4096};
  const unsigned inputs[]={0,1,3,7,15},outputs[]={0,3,15};
  uint8_t source[4160],a[4160],b[4160],expected_a[4160],expected_b[4160];
  for(unsigned k=0;k<sizeof source;k++)source[k]=(uint8_t)(k*17+k/63);
  for(unsigned si=0;si<sizeof sizes/sizeof *sizes;si++)
    for(unsigned i=0;i<sizeof inputs/sizeof *inputs;i++)
      for(unsigned j=0;j<sizeof outputs/sizeof *outputs;j++)for(unsigned n=1;n<=7;n+=6) {
        memset(a,0x5a,sizeof a);memset(b,0xa7,sizeof b);
        memcpy(expected_a,a,sizeof a);memcpy(expected_b,b,sizeof b);
        memcpy(expected_a+outputs[j],source+inputs[i],sizes[si]);
        memcpy(expected_b+n,source+inputs[i],sizes[si]);
        vk_test_upload_copy_dual(a+outputs[j],b+n,source+inputs[i],sizes[si]);
        CHECK(!memcmp(a,expected_a,sizeof a));CHECK(!memcmp(b,expected_b,sizeof b));
      }
  // Inaccessible following pages reject any vector read/store beyond a tail.
  size_t pg=getpagesize();uint8_t *guard[3];
  for(unsigned k=0;k<3;k++) {
    guard[k]=mmap(NULL,2*pg,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(guard[k]!=MAP_FAILED);CHECK(!mprotect(guard[k]+pg,pg,PROT_NONE));
  }
  for(unsigned size=0;size<=129;size++) {
    memset(guard[0],0x39,pg);memset(guard[1],0x5a,pg);memset(guard[2],0xa7,pg);
    vk_test_upload_copy_dual(guard[1]+pg-size,guard[2]+pg-size,guard[0]+pg-size,size);
    CHECK(!memcmp(guard[1]+pg-size,guard[0]+pg-size,size));
    CHECK(!memcmp(guard[2]+pg-size,guard[0]+pg-size,size));
    CHECK(guard[1][pg-size-1]==0x5a);CHECK(guard[2][pg-size-1]==0xa7);
  }
  for(unsigned k=0;k<3;k++)CHECK(!munmap(guard[k],2*pg));

  int old=vk_test_upload_dual_enable(0);uint64_t full,used,protected;int reference;
  vk_pool_sync_policy_stats(&full,&used,&protected,&reference);vk_pool_sync_reference(0);
  for(unsigned candidate=0;candidate<2;candidate++) {
    vk_test_upload_dual_enable(candidate);mtl_finish();
    enum {SIZE=65536+193,SPLIT=8192,READ=16384};uint8_t *storage;
    CHECK(!posix_memalign((void **)&storage,65536,SIZE));dm_guest_write_prepare(storage,SIZE);
    for(unsigned k=0;k<SIZE;k++)storage[k]=(uint8_t)(k*11+k/4096+0x31);
    NVNmemoryPoolBuilder builder;NVNmemoryPool pool;
    CALL(nvnMemoryPoolBuilderSetDefaults,&builder);CALL(nvnMemoryPoolBuilderSetDevice,&builder,&device);
    CALL(nvnMemoryPoolBuilderSetStorage,&builder,storage,SIZE);
    CALL(nvnMemoryPoolBuilderSetFlags,&builder,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
    CHECK(CALL(nvnMemoryPoolInitialize,&pool,&builder));
    if(!V.options.pool_mirror_reference)CHECK(vk_test_pool_split_backings((uintptr_t)storage,SPLIT));
    uint8_t result[READ+193],expected[READ+193];memcpy(expected,storage,READ);memcpy(expected+READ,storage+65536,193);
    mtl_sub_begin();pool_span_gpu_read(storage,SPLIT,result);
    pool_span_gpu_read(storage+SPLIT,READ-SPLIT,result+SPLIT);pool_span_gpu_read(storage+65536,193,result+READ);
    vk_internal_wait(vk_internal_commit());CHECK(!memcmp(result,expected,sizeof result));mtl_finish();
    uint32_t gpu=0x91a2b3c4;mtl_sub_begin();pool_span_gpu_fill(storage+256,4,gpu);
    vk_internal_wait(vk_internal_commit());mtl_finish();CHECK(memcmp(storage+256,&gpu,4)!=0);
    dm_guest_write_prepare(storage,READ);memset(storage+4,0xb9,4);memset(storage+60,0x62,128);
    memset(storage+4092,0x81,8);memset(storage+SPLIT-4,0x43,8);
    dm_guest_write_prepare(storage+65536,193);memset(storage+65536+7,0x17,181);
    memcpy(expected,storage,READ);memcpy(expected+256,&gpu,4);memcpy(expected+READ,storage+65536,193);
    mtl_sub_begin();pool_span_gpu_read(storage,SPLIT,result);
    pool_span_gpu_read(storage+SPLIT,READ-SPLIT,result+SPLIT);pool_span_gpu_read(storage+65536,193,result+READ);
    // A guest write after command recording must still be uploaded at commit.
    dm_guest_write_prepare(storage+1300,4);memset(storage+1300,0xe4,4);memset(expected+1300,0xe4,4);
    // Serial/global reference intentionally uploads whole pages and can
    // supersede the raw GPU-only neighbor; preserve its independent semantics.
    if(!vk_async_submissions() || vk_submission_barrier_mode()!=0)memcpy(expected+256,storage+256,4);
    vk_internal_wait(vk_internal_commit());CHECK(!memcmp(result,expected,sizeof result));mtl_finish();
    CALL(nvnMemoryPoolFinalize,&pool);free(storage);
  }
  vk_pool_sync_reference(reference);vk_test_upload_dual_enable(1);
  LOG("[vulkan dual upload fixture] %s path: scalar/vector lengths0..4096, unaligned/guard tails, real canonical nonzero offsets/two spans, GPU-only neighbor and late CPU edits passed; candidate enabled for remaining strict GPU oracles",
#if defined(__aarch64__)
      "ARM64 NEON"
#else
      "portable host (does not validate ARM64 NEON)"
#endif
  );
  return old;
}
#else
static int upload_dual_fixture_begin(void) { return 0; }
static int vk_test_upload_dual_enable(int enabled) { (void)enabled;return 0; }
#endif
