#include <sys/mman.h>
#ifdef DM_VULKAN_TEST_EMBED
#ifdef DM_VULKAN_SCAN_PAIR_ORACLE
extern unsigned vk_test_upload_word_runs(const uint8_t *,const uint8_t *,uint64_t,size_t,VkBufferCopy *);
#endif
// Literal regions, independent of the scanner implementation. In particular,
// contiguous edits join across both64B and128B, while equal words stay absent.
static void pool_upload_pair_boundaries_test(void) {
#ifdef DM_VULKAN_SCAN_PAIR_ORACLE
  static const struct {
    unsigned size,edits[6],n,starts[4],lengths[4],count;
  } tests[]={
    {0,{0},0,{0},{0},0}, {4,{0},1,{0},{4},1},
    {60,{56},1,{56},{4},1}, {64,{60},1,{60},{4},1},
    {68,{60,64},2,{60},{8},1}, {124,{60,64,120},3,{60,120},{8,4},2},
    {128,{60,64,124},3,{60,124},{8,4},2},
    {132,{60,64,124,128},4,{60,124},{8,8},2},
    {192,{124,128,188},3,{124,188},{8,4},2},
    {256,{124,128,188,192,252},5,{124,188,252},{8,8,4},3},
    {4092,{60,64,124,128,4088},5,{60,124,4088},{8,8,4},3},
    {4096,{60,64,124,128,4092},5,{60,124,4092},{8,8,4},3}
  };
  uint8_t guest[4098],shadow[4098];VkBufferCopy runs[513];
  for(unsigned t=0;t<sizeof tests/sizeof *tests;t++) {
    memset(guest,0x31,sizeof guest);memset(shadow,0x31,sizeof shadow);
    guest[0]^=0x80;guest[1+tests[t].size]^=0x40; // just outside the bounded range
    for(unsigned i=0;i<tests[t].n;i++)memset(guest+1+tests[t].edits[i],0x72,4);
    memset(runs,0xa5,sizeof runs);
    unsigned count=vk_test_upload_word_runs(guest+1,shadow+1,0x1004,tests[t].size,runs);
    CHECK(count==tests[t].count);
    for(unsigned i=0;i<count;i++) {
      CHECK(runs[i].srcOffset==tests[t].starts[i]);
      CHECK(runs[i].dstOffset==0x1004+tests[t].starts[i]);
      CHECK(runs[i].size==tests[t].lengths[i]);
    }
    CHECK(runs[count].srcOffset==UINT64_C(0xa5a5a5a5a5a5a5a5));
  }
  memset(shadow,0,sizeof shadow);memset(guest,0,sizeof guest);
  for(unsigned word=0;word<1024;word+=2)memset(guest+1+word*4,0x41,4);
  unsigned count=vk_test_upload_word_runs(guest+1,shadow+1,0,4096,runs);CHECK(count==512);
  for(unsigned i=0;i<count;i++)CHECK(runs[i].srcOffset==i*8&&runs[i].dstOffset==i*8&&runs[i].size==4);
  // A real inaccessible guard page catches overreads, including the original
  // bounded tail and an empty input pointing directly at the guard.
  size_t page=(size_t)getpagesize();CHECK(page>=4096);
  uint8_t *a=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
  uint8_t *b=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
  CHECK(a!=MAP_FAILED&&b!=MAP_FAILED);memset(a,0x61,page);memset(b,0x61,page);
  CHECK(!mprotect(a+page,page,PROT_NONE));CHECK(!mprotect(b+page,page,PROT_NONE));
  for(unsigned t=0;t<sizeof tests/sizeof *tests;t++) {
    unsigned size=tests[t].size;
    CHECK(vk_test_upload_word_runs(a+page-size,b+page-size,0,size,runs)==0);
    if(size) {
      memset(a+page-4,0x42,4);
      CHECK(vk_test_upload_word_runs(a+page-size,b+page-size,0,size,runs)==1);
      CHECK(runs[0].srcOffset==size-4&&runs[0].dstOffset==size-4&&runs[0].size==4);
      memset(a+page-4,0x61,4);
    }
  }
  CHECK(!munmap(a,page*2));CHECK(!munmap(b,page*2));
  LOG("[vulkan scan pair boundary fixture] literal128 joins, partial64/tails,512runs and inaccessible guard bounds passed");
#endif
}
#endif
#ifdef DM_VULKAN_TEST_EMBED
// Real partial final pool pages force128/pair+64/pair+tail traversal instead
// of merely testing a short transfer inside a full4096B synchronized page.
static void pool_upload_pair_gpu_tails_test(void) {
  if(V.options.pool_mirror_reference)return;
  const unsigned tails[]={128,132,192,196,256};
  uint64_t prior_window=V.options.pool_window_bytes;int prior_scan=vk_upload_scan_reference_mode();
  mtl_finish();vk_test_pool_window_bytes(65536);
  for(unsigned mode=0;mode<2;mode++) for(unsigned t=0;t<sizeof tails/sizeof *tails;t++) {
    unsigned size=tails[t];uint8_t *data;CHECK(!posix_memalign((void **)&data,65536,131072));
    memset(data,0x31,131072);vk_upload_scan_reference(mode);mtl_pool_add(data,65536+size);
    vk_pool_requirement need=pool_window_need(data+65536,size,2,VK_PIPELINE_STAGE_TRANSFER_BIT,0);
    vk_pool_reserve_result reserve;CHECK(vk_pool_reserve_ranges(&need,1,65536,&reserve)==VK_POOL_RESERVE_OK);
    uint8_t expected[256],actual[256];memset(expected,0x31,size);
    mtl_sub_begin();pool_window_read(data+65536,size,actual);vk_internal_wait(vk_internal_commit());CHECK(!memcmp(actual,expected,size));
    int sparse=vk_async_submissions()&&vk_submission_barrier_mode()==0;
    if(sparse) {
      uint32_t gpu=0x87654321;mtl_sub_begin();pool_window_fill(data+65536+64,4,gpu);vk_internal_wait(vk_internal_commit());
      CHECK(memcmp(data+65536+64,&gpu,4));memcpy(expected+64,&gpu,4);
    }
    const unsigned edits[]={0,60,68,124,size-4};
    for(unsigned i=0;i<sizeof edits/sizeof *edits;i++) {
      uint32_t value=0xabc02300+i;dm_guest_write_prepare(data+65536+edits[i],4);
      memcpy(data+65536+edits[i],&value,4);memcpy(expected+edits[i],&value,4);
    }
    mtl_sub_begin();pool_window_read(data+65536,size,actual);vk_internal_wait(vk_internal_commit());CHECK(!memcmp(actual,expected,size));
    if(sparse)CHECK(!memcmp(data+65536+64,(uint8_t[]){0x31,0x31,0x31,0x31},4));
    mtl_pool_del(data);free(data);
  }
  vk_test_pool_window_bytes(prior_window);vk_upload_scan_reference(prior_scan);
  LOG("[vulkan scan pair GPU tails]128/132/192/196/256 final pool bytes and GPU-only equal gap match in original reference/default paths");
}
#endif
