// Real GPU mip writes check exact logical sequence numbers. These are access
// history invariants, not CPU/GPU timers or performance samples. The existing
// mip_stamp_test separately covers uncovered/covered shader reads and eviction.
extern int vk_texture_test_mip_clocks(const tex_t *,uint64_t[20]);
static void mip_stamp_write_test(void) {
  uint8_t *storage;
  CHECK(!posix_memalign((void **)&storage,65536,1<<20));
  memset(storage,0,1<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);
  CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,
      NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtextureBuilder tb;NVNtexture texture;
  CALL(nvnTextureBuilderSetDefaults,&tb);
  CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);
  CALL(nvnTextureBuilderSetWidth,&tb,16);
  CALL(nvnTextureBuilderSetHeight,&tb,16);
  CALL(nvnTextureBuilderSetLevels,&tb,3);
  CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);
  CALL(nvnTextureBuilderSetStorage,&tb,&pool,0);
  CHECK(CALL(nvnTextureInitialize,&texture,&tb));
  // Allocate/upload all sibling mips before measuring the logical increments
  // of one actual transfer write. No initialization work remains in the loop.
  mtl_sub_begin();
  for(unsigned level=0;level<3;level++) {
    p_clear_tex clear={.tex=(const tex_t *)&texture,.has_view=1,
      .view={.set_levels=1,.base_level=level,.nlevels=1},
      .r={0,0,0,16>>level,16>>level,1},.mask=15,.c.f={0,0,0,1}};
    vk_texture_clear(&clear);
  }
  vk_internal_wait(vk_internal_commit());
  const float colors[3][4]={{1,0,0,1},{0,1,0,1},{0,0,1,1}};
  for(unsigned level=0;level<3;level++) {
    uint64_t before[20],after[20];
    CHECK(vk_texture_test_mip_clocks((const tex_t *)&texture,before)==3);
    mtl_sub_begin();
    p_clear_tex clear={.tex=(const tex_t *)&texture,.has_view=1,
      .view={.set_levels=1,.base_level=level,.nlevels=1},
      .r={0,0,0,16>>level,16>>level,1},.mask=15};
    memcpy(clear.c.f,colors[level],sizeof clear.c.f);vk_texture_clear(&clear);
    CHECK(vk_texture_test_mip_clocks((const tex_t *)&texture,after)==3);
    // One whole-image use increment, followed by this one cell's writer
    // increment. The mip use stamp keeps the former value, not the latter.
    CHECK(after[2]==before[0]+1 && after[0]==after[2]+1);
    CHECK(after[4+level]==after[2]);
    for(unsigned sibling=0;sibling<3;sibling++)
      if(sibling!=level)CHECK(after[4+sibling]==before[4+sibling]);
    vk_internal_wait(vk_internal_commit());
  }
  for(unsigned level=0;level<3;level++) {
    NVNtextureView view;CALL(nvnTextureViewSetDefaults,&view);
    CALL(nvnTextureViewSetLevels,&view,level,1);
    unsigned width=16>>level;NVNcopyRegion region={0,0,0,width,width,1};
    uint8_t pixels[16*16*4];
    CALL(nvnTextureReadTexels,&texture,&view,&region,pixels);
    uint8_t expected[4]={0,0,0,255};expected[level]=255;
    for(unsigned p=0;p<width*width;p++)CHECK(!memcmp(pixels+p*4,expected,4));
  }
  CALL(nvnTextureFinalize,&texture);CALL(nvnMemoryPoolFinalize,&pool);free(storage);
  LOG("[vulkan mip stamp write fixture] exact whole-image/writer increments, sibling stamps and real GPU mip contents passed");
}
