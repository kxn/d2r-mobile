// Original NVN metadata propagation only: no new access/visibility policy.
#ifdef DM_VULKAN_TEST_EMBED
extern int vk_test_pool_flags(uint64_t,uint32_t *,int *,uint64_t *);
static void pool_flags_test(void) {
  mtl_init();enum {W=65536,N=128};uint8_t *storage,out[N],expected[N];
  CHECK(!posix_memalign((void **)&storage,W,W));
  const uint32_t words[]={0,0,0x24,0x80000424u,0,0};
  const int knowns[]={0,1,1,1,0,1};
  uint64_t previous_identity=0;
  for(unsigned test=0;test<sizeof words/sizeof *words;test++) {
    dm_guest_write_prepare(storage,W);memset(storage,0x31+test,W);
    for(unsigned k=0;k<N;k++)expected[k]=storage[k];
    NVNmemoryPoolBuilder builder;NVNmemoryPool original;
    int hle=test>=1&&test<=3;
    if(hle) {
      CALL(nvnMemoryPoolBuilderSetDefaults,&builder);CALL(nvnMemoryPoolBuilderSetDevice,&builder,&device);
      CALL(nvnMemoryPoolBuilderSetStorage,&builder,storage,W);CALL(nvnMemoryPoolBuilderSetFlags,&builder,(int)words[test]);
      CHECK(CALL(nvnMemoryPoolInitialize,&original,&builder));
      CHECK(((const pool_t *)&original)->flags==words[test]);
    } else if(knowns[test])mtl_pool_add_flags(storage,W,(int)words[test]);
    else mtl_pool_add(storage,W);
    uint32_t raw=0xdeadbeef;int known=-1;uint64_t identity=0;
    CHECK(vk_test_pool_flags((uintptr_t)storage,&raw,&known,&identity));
    CHECK(raw==words[test]&&known==knowns[test]&&identity>previous_identity);previous_identity=identity;
    // Metadata lookup alone must not instantiate a mirror or upload bytes.
    vk_pool_span_info span;int status=vk_pool_span_query((uintptr_t)storage,N,&span);
    CHECK(status==(V.options.pool_mirror_reference?VK_POOL_SPAN_SINGLE:VK_POOL_SPAN_UNBACKED));
    // Exact same original GPU read/write + fence/CPU publication for legacy,
    // known-zero, ordinary and unknown-high-bit metadata. No flag interpretation.
    vk_pool_requirement need=pool_window_need(storage,N,0,VK_PIPELINE_STAGE_TRANSFER_BIT,0);
    mtl_sub_begin();int operation=vk_pool_operation_begin_ranges(&need,1);
    pool_window_read(storage,N,out);vk_pool_operation_end(operation);vk_internal_wait(vk_internal_commit());
    for(unsigned k=0;k<N;k++)CHECK(out[k]==expected[k]);
    uint32_t word=0x7182a000u+test;for(unsigned k=32;k<48;k+=4)memcpy(expected+k,&word,4);
    mtl_sub_begin();operation=vk_pool_operation_begin_ranges(&need,1);
    pool_window_fill(storage+32,16,word);vk_buffer_writeback_stage((uintptr_t)storage+32,16,VK_PIPELINE_STAGE_TRANSFER_BIT);
    vk_pool_operation_end(operation);vk_internal_wait(vk_internal_commit());
    for(unsigned k=0;k<N;k++)CHECK(storage[k]==expected[k]);
    mtl_sub_begin();operation=vk_pool_operation_begin_ranges(&need,1);
    pool_window_read(storage,N,out);vk_pool_operation_end(operation);vk_internal_wait(vk_internal_commit());
    for(unsigned k=0;k<N;k++)CHECK(out[k]==expected[k]);
    CHECK(vk_test_pool_flags((uintptr_t)storage,&raw,&known,&identity));
    CHECK(raw==words[test]&&known==knowns[test]&&identity==previous_identity);
    if(hle)CALL(nvnMemoryPoolFinalize,&original);else mtl_pool_del(storage);
    raw=0x9abcdeff;known=-7;identity=0;
    CHECK(!vk_test_pool_flags((uintptr_t)storage,&raw,&known,&identity));
    CHECK(raw==0x9abcdeff&&known==-7&&!identity);
  }
  free(storage);CHECK(vk_validation_error_count()==0);
  LOG("[NVN pool flags fixture] HLE knownzero/ordinary/unknownbits, legacyunknown, exactidentity recreation and original GPU read/write/fence publication passed");
}
#else
static void pool_flags_test(void) {}
#endif
