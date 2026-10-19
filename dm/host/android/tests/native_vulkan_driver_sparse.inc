// Independent GPU math/oracle; the shader reads both arms and two c0 banks.
// Same-key driver hits must not bypass the output buffer's changing offset.
static uint32_t sparse_address_oracle(unsigned reg,int ubo) {
  const p_bind_buf *b=ubo?&S.ubo[5][reg]:&S.ssbo[5][reg];
  return (uint32_t)b->addr^(uint32_t)(b->addr>>32)^(uint32_t)b->size;
}
static void driver_sparse_key_test(void) {
  vk_state saved=S;uint64_t last,content,misses;int reference;
  vk_driver_constant_stats(&last,&content,&misses,&reference);
  int original_reference=reference;
  uint8_t controls[6][0x880];NVNprogram programs[6];
  for(unsigned n=0;n<6;n++) {
    memset(controls[n],59+n,sizeof controls[n]);
    CHECK(CALL(nvnProgramInitialize,&programs[n],&device));
    NVNshaderData shader={.control=controls[n]};CHECK(CALL(nvnProgramSetShaders,&programs[n],1,&shader));
#ifdef DM_VULKAN_TEST_EMBED
    vk_shader *s=vk_shader_get(controls[n]);
    CHECK(vk_driver_schema_words(s,2)==(n==2||n>=4?UINT32_MAX:n==3?0:4));
    if(n<3)CHECK(vk_driver_schema_words(s,0)==6);
#endif
  }
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,65536));memset(storage,0,65536);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,65536);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  uint32_t expected[12][4];uint64_t mode_misses[2]={0};
  int original_template=vk_descriptor_template_reference_mode();
  for(unsigned mode=0;mode<3;mode++) {
    vk_driver_constant_reference(mode==0);vk_descriptor_template_reference(mode==2);
    dm_guest_write_prepare(storage,65536);memset(storage,0,65536);
    uint64_t before_last,before_content,before_misses;
    vk_driver_constant_stats(&before_last,&before_content,&before_misses,&reference);
    mtl_sub_begin();S=(vk_state){0};driver_fixture_seed(5,0,storage);
    S.tex[5][0]=2;S.tex[5][1]=0x10203040;S.tex[5][4]=0x50607080;S.samp[5][0]=0xabcdef;
    vk_state initial=S;
    for(unsigned seq=0;seq<12;seq++) {
      unsigned which=seq>=10?seq-6:seq==3?1:seq==6||seq==7?2:seq>=8?3:0;
      if(seq==1) {
        // Same shader/read-set, every other binding can legitimately change.
        S.tex[5][87]^=0x1223;S.samp[5][37]^=0x4567;S.img[5][6]^=0x8899;
        S.ssbo[5][2].addr^=UINT64_C(1)<<40;S.ubo[5][8].size^=0x7788;
      }
      if(seq==2){S.tex[5][0]^=1;S.tex[5][1]^=0x31415;S.ubo[5][3].size^=0x9292;}
      if(seq==4)S=initial; // Direct upstream-style restore: no dirty epoch.
      if(seq==5){S.tex[5][0]^=UINT64_C(1)<<60;S.ssbo[5][4].size^=UINT64_C(1)<<48;}
      if(seq==6)S.tex[5][0]=18;
      if(seq==7){S.tex[5][0]=23;S.tex[5][87]^=0x5a5a;} // Unknown indexing keeps full-key path.
      if(seq==9){S.tex[5][30]^=0xffff;S.samp[5][1]^=0xeeee;}
      S.program=(const prog_t *)&programs[which];
      S.ssbo[5][15]=(p_bind_buf){.addr=(uintptr_t)(storage+seq*256),.size=16};
      uint32_t a=(uint32_t)S.tex[5][0];
      if(which<2 || which>=4) {
        unsigned other=which==1,reg=other?6:4,uboreg=other?5:3;
        expected[seq][0]=(a&1)?(uint32_t)S.tex[5][other?10:1]:(uint32_t)S.tex[5][other?12:4];
        expected[seq][1]=sparse_address_oracle(reg,0);expected[seq][2]=sparse_address_oracle(uboreg,1);
        expected[seq][3]=(uint32_t)S.samp[5][other?7:0];
      } else if(which==2) {
        // handles[index].x; index derives from a real driver-table read.
        unsigned offset=(a&31)*16;
        expected[seq][0]=offset>=0x168&&offset<0x568?(uint32_t)S.tex[5][(offset-0x168)/8]:
          offset>=0x120&&offset<0x160?(uint32_t)S.img[5][(offset-0x120)/8]:0;
        expected[seq][1]=sparse_address_oracle(4,0);expected[seq][2]=sparse_address_oracle(3,1);
        expected[seq][3]=(uint32_t)S.samp[5][0];
      } else memset(expected[seq],0,sizeof expected[seq]);
      vk_dispatch(&(p_dispatch){1,1,1});
    }
    CHECK(mtl_wait(mtl_sub_commit(),UINT64_MAX));
    for(unsigned seq=0;seq<12;seq++)for(unsigned lane=0;lane<4;lane++)
      CHECK(((uint32_t *)(storage+seq*256))[lane]==expected[seq][lane]);
    vk_driver_constant_stats(&last,&content,&misses,&reference);
    if(mode<2)mode_misses[mode]=misses-before_misses;
    if(mode)CHECK(last+content>before_last+before_content);
  }
  CHECK(mode_misses[1]<mode_misses[0]);
  for(unsigned n=0;n<6;n++)CALL(nvnProgramFinalize,&programs[n]);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);
  vk_driver_constant_reference(original_reference);vk_descriptor_template_reference(original_template);S=saved;
  LOG("[vulkan sparse driver fixture] opposite branches, disjoint shader schemas, observed/unobserved changes, direct restore, CS address/UBO banks, dynamic fallback and zero-tail; actual GPU results match full reference and plain/template writes");
}
