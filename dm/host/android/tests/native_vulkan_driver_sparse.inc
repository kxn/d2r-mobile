// Independent GPU math/oracle; the shader reads both arms and two c0 banks.
// Same-key driver hits must not bypass the output buffer's changing offset.
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
extern vk_buffer *vk_test_driver_span_constant(vk_shader *,int,uint64_t *,uint64_t *);
#endif
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
#include "../../../host/crypto.h"
// Real load_key regression: valid empty DPL is a pipeline input, while the
// independent DDK/DSP proof still covers exactly the original five artifacts.
static void driver_proof_identity_test(const vk_shader *s) {
  CHECK(vk_pixel_local_enabled());
  char path[2048];uint32_t local[48];
  CHECK(snprintf(path,sizeof path,"%s/%s.plocal",V.options.shader_root,s->key)<(int)sizeof path);
  FILE *file=fopen(path,"rb");CHECK(file!=NULL);
  CHECK(fread(local,1,sizeof local,file)==sizeof local);CHECK(fgetc(file)==EOF);CHECK(!fclose(file));
  CHECK(local[0]==UINT32_C(0x314c5044) && (local[1]==1 || local[1]==2));
  CHECK(local[2]==192 && local[3]==s->stage && local[4]==s->set && !local[5] && local[6]==192 && !local[7]);
  uint8_t hashes[5][32],proof[32],combined[64],pipeline[32],changed[32];
  memcpy(hashes[0],(uint8_t *)local+64,32); // meta, spv, spec, pred, tssbo
  memcpy(hashes[1],(uint8_t *)local+32,32);
  memcpy(hashes[2],(uint8_t *)local+96,96);
  CC_SHA256(hashes,sizeof hashes,proof);CHECK(!memcmp(proof,s->proof_fingerprint,32));
  memcpy(combined,proof,32);CC_SHA256(local,sizeof local,combined+32);
  CC_SHA256(combined,sizeof combined,pipeline);
  CHECK(!memcmp(pipeline,s->fingerprint,32));CHECK(memcmp(proof,pipeline,32));
  CHECK(vk_shader_fingerprint(s->key,changed));CHECK(!memcmp(changed,pipeline,32));
  // Both versions are valid empty recipes with the same exact five hashes.
  // Recompute via the unchanged public entry, then restore owned fixture bytes.
  uint32_t version=local[1];local[1]=3-version;
  file=fopen(path,"wb");CHECK(file!=NULL);CHECK(fwrite(local,1,sizeof local,file)==sizeof local);CHECK(!fclose(file));
  CHECK(vk_shader_fingerprint(s->key,changed));CHECK(memcmp(changed,pipeline,32));
  CC_SHA256(local,sizeof local,combined+32);CC_SHA256(combined,sizeof combined,proof);
  CHECK(!memcmp(changed,proof,32));
  local[1]=version;file=fopen(path,"wb");CHECK(file!=NULL);
  CHECK(fwrite(local,1,sizeof local,file)==sizeof local);CHECK(!fclose(file));
  CHECK(vk_shader_fingerprint(s->key,changed));CHECK(!memcmp(changed,pipeline,32));
}
#endif
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
#ifndef DM_ANDROID_APP
    if(getenv("DM_DRIVER_PROOF_IDENTITY_FIXTURE"))driver_proof_identity_test(s);
    if(getenv("DM_DRIVER_SPAN_SHADER_FIXTURE")) {
      // Use the original compiled shaders and production metadata loader.
      // Dynamic c2 and the known-zero read at 65520 must retain 64 KiB;
      // missing/damaged sparse keys do not invalidate an independent span.
      CHECK(s->driver_span[0]==(n==3?0:4096));
      CHECK(s->driver_span[1]==(n==2||n==3?0:4096));
      CHECK(s->driver_span_module==s->module);
    }
#endif
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
    driver_fixture_bind_handle(5,0,0,2);driver_fixture_bind_handle(5,0,1,0x10203040);driver_fixture_bind_handle(5,0,4,0x50607080);driver_fixture_bind_handle(5,1,0,0xabcdef);
    vk_state initial=S;
    for(unsigned seq=0;seq<12;seq++) {
      unsigned which=seq>=10?seq-6:seq==3?1:seq==6||seq==7?2:seq>=8?3:0;
      if(seq==1) {
        // Same shader/read-set, every other binding can legitimately change.
        driver_fixture_bind_handle(5,0,87,S.tex[5][87]^0x1223);driver_fixture_bind_handle(5,1,37,S.samp[5][37]^0x4567);driver_fixture_bind_handle(5,2,6,S.img[5][6]^0x8899);
        driver_fixture_bind_buffer(5,2,0,S.ssbo[5][2].addr^(UINT64_C(1)<<40),S.ssbo[5][2].size);driver_fixture_bind_buffer(5,8,1,S.ubo[5][8].addr,S.ubo[5][8].size^0x7788);
      }
      if(seq==2){driver_fixture_bind_handle(5,0,0,S.tex[5][0]^1);driver_fixture_bind_handle(5,0,1,S.tex[5][1]^0x31415);driver_fixture_bind_buffer(5,3,1,S.ubo[5][3].addr,S.ubo[5][3].size^0x9292);}
      if(seq==4)vk_state_restore(&initial); // Direct upstream-style restore: no dirty epoch.
      if(seq==5){driver_fixture_bind_handle(5,0,0,S.tex[5][0]^(UINT64_C(1)<<60));driver_fixture_bind_buffer(5,4,0,S.ssbo[5][4].addr,S.ssbo[5][4].size^(UINT64_C(1)<<48));}
      if(seq==6)driver_fixture_bind_handle(5,0,0,18);
      if(seq==7){driver_fixture_bind_handle(5,0,0,23);driver_fixture_bind_handle(5,0,87,S.tex[5][87]^0x5a5a);} // Unknown indexing keeps full-key path.
      if(seq==9){driver_fixture_bind_handle(5,0,30,S.tex[5][30]^0xffff);driver_fixture_bind_handle(5,1,1,S.samp[5][1]^0xeeee);}
      S.program=(const prog_t *)&programs[which];
      driver_fixture_bind_buffer(5,15,0,(uintptr_t)(storage+seq*256),16);
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
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
      if(getenv("DM_DRIVER_SPAN_SHADER_FIXTURE")) {
        // Query the actual production constant result; do not change the
        // original draw, GPU numerical oracle or last/content/miss assertions.
        vk_shader *actual=vk_shader_get(controls[which]);
        for(unsigned slot=0;slot<=2;slot+=2) {
          uint64_t offset,range;
          CHECK(vk_test_driver_span_constant(actual,slot,&offset,&range)!=NULL);
          uint64_t expected_range=mode==0 || vk_driver_span_reference_mode() ||
              !actual->driver_span[slot/2]?65536:4096;
          CHECK(range==expected_range);
          if(getenv("DM_DRIVER_PROOF_IDENTITY_FIXTURE") && mode==1 && seq==0) {
            vk_shader changed=*actual;changed.module=VK_NULL_HANDLE;
            CHECK(changed.driver_span_module!=changed.module);
            CHECK(vk_test_driver_span_constant(&changed,slot,&offset,&range)!=NULL);
            CHECK(range==65536); // unproved DPL/future module cannot inherit span
          }
        }
      }
#endif
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
  vk_driver_constant_reference(original_reference);vk_descriptor_template_reference(original_template);vk_state_restore(&saved);
  LOG("[vulkan sparse driver fixture] opposite branches, disjoint shader schemas, observed/unobserved changes, direct restore, CS address/UBO banks, dynamic fallback and zero-tail; actual GPU results match full reference and plain/template writes");
}
