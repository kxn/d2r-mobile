#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
// Independent GPU interpretation of the original driver ABI: read all words,
// not just the fields used to serialize the CPU cache key.
static uint64_t driver_fixture_address(unsigned stage,unsigned n,unsigned variant,unsigned ubo) {
  return UINT64_C(0x9182736400000000)+(uint64_t)stage*0x1000000+n*0x1000+variant*0x100+ubo*0x40;
}
static uint64_t driver_fixture_size(unsigned n,unsigned variant,unsigned ubo) {
  return (UINT64_C(0x76543210)<<32)+97+n*13+variant*7+ubo*19;
}
static uint32_t driver_fixture_handle(unsigned n,unsigned variant,unsigned kind) {
  return 0x51020304u+n*0x103u+variant*0x1000000u+kind*0x10000000u;
}
static void driver_fixture_bind_buffer(unsigned stage,unsigned index,unsigned ubo,uint64_t addr,uint64_t size) {
  p_bind_buf p={stage,index,addr,size};vk_state_op(ubo?OP_BIND_UBO:OP_BIND_SSBO,&p);
}
static void driver_fixture_bind_handle(unsigned stage,unsigned kind,unsigned index,uint64_t handle) {
  p_bind_handles p={.stage=stage,.first=index,.n=1,.h={handle}};
  vk_state_op(kind==0?OP_BIND_TEX:kind==1?OP_BIND_SAMP:OP_BIND_IMG,&p);
}
static void driver_fixture_seed(unsigned stage,unsigned variant,uint8_t *output) {
  for(unsigned n=0;n<16;n++) {
    driver_fixture_bind_buffer(stage,n,0,driver_fixture_address(stage,n,variant,0),driver_fixture_size(n,variant,0));
    driver_fixture_bind_buffer(stage,n,1,driver_fixture_address(stage,n,variant,1),driver_fixture_size(n,variant,1));
  }
  driver_fixture_bind_buffer(stage,15,0,(uintptr_t)output,1<<20);
  for(unsigned n=0;n<128;n++) {
    driver_fixture_bind_handle(stage,0,n,(UINT64_C(0xfedcba98)<<32)|driver_fixture_handle(n,variant,0));
    driver_fixture_bind_handle(stage,1,n,(UINT64_C(0x98765432)<<32)|driver_fixture_handle(n,variant,1));
  }
  for(unsigned n=0;n<32;n++)driver_fixture_bind_handle(stage,2,n,(UINT64_C(0xabcdef12)<<32)|driver_fixture_handle(n,variant,2));
}
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
extern void vk_test_driver_bloom_begin(unsigned,uint64_t);
#endif

static void driver_fixture_expected(unsigned stage,unsigned variant,uint8_t *output,uint32_t expected[32768]) {
  memset(expected,0,131072);
  // Addresses[17/81/49] are VS/FS/CS respectively; CS UBO starts at [33].
  unsigned vec=stage==0?17:stage==1?81:49;
  for(unsigned n=0;n<16;n++) {
    uint64_t addr=n==15?(uintptr_t)output:driver_fixture_address(stage,n,variant,0);
    expected[(vec+n)*4]=(uint32_t)addr;expected[(vec+n)*4+1]=addr>>32;
    expected[(vec+n)*4+2]=n==15?1<<20:(uint32_t)driver_fixture_size(n,variant,0);
    if(stage==5) {
      addr=driver_fixture_address(stage,n,variant,1);
      expected[(33+n)*4]=(uint32_t)addr;expected[(33+n)*4+1]=addr>>32;
      expected[(33+n)*4+2]=(uint32_t)driver_fixture_size(n,variant,1);
    }
  }
  // Handles[22].z/Handles[86].z and Handles[18] are the independent
  // uvec4 ABI coordinates, with zero upper words in every eight-byte cell.
  for(unsigned n=0;n<128;n++) {
    expected[16384+22*4+2+n*2]=driver_fixture_handle(n,variant,0);
    expected[16384+86*4+2+n*2]=driver_fixture_handle(n,variant,1);
  }
  for(unsigned n=0;n<8;n++)expected[16384+18*4+n*2]=driver_fixture_handle(n,variant,2);
}
// Failure-only owned-data diagnostics. Preserve the exact assertion below;
// dumps contain only this fixture's generated tables and integer pixels.
static const char *driver_fixture_diagnostic_prefix="";
static void driver_fixture_mismatch(unsigned mode,unsigned stage,unsigned seq,unsigned first,
                                    const uint32_t *actual,const uint32_t *expected) {
  unsigned count=0,sentinel=0;
  for(unsigned word=0;word<32768;word++) {
    if(actual[word]>=7654321 && actual[word]<=7654324)sentinel++;
    if(actual[word]!=expected[word]) {
      if(count<16)LOG("[vulkan compact driver mismatch] mode=%u reference=%u stage=%u seq=%u word=%u pixel=%u,%u lane=%u actual=%#010x expected=%#010x",
                     mode,mode==0||mode==3,stage,seq,word,(word/4)%128,(word/4)/128,word%4,actual[word],expected[word]);
      count++;
    }
  }
  LOG("[vulkan compact driver mismatch] first=%u mismatched_words=%u sentinel_words=%u format=RGBA32UI point_size=1 viewport=128x64",
      first,count,sentinel);
  const char *base=getenv("DM_CAPTURE_TEST_DIR");if(!base)base=".";
  const uint32_t *arrays[]={actual,expected};
  for(unsigned i=0;i<2;i++) {
    char path[4096];snprintf(path,sizeof path,"%s/%scompact-driver-mode%u-stage%u-seq%u-%s.bin",base,driver_fixture_diagnostic_prefix,mode,stage,seq,i?"expected":"actual");
    FILE *file=fopen(path,"wb");
    if(file) {size_t bytes=fwrite(arrays[i],1,131072,file);int closed=fclose(file);LOG("[vulkan compact driver mismatch] dump=%s bytes=%zu close=%d",path,bytes,closed);}
    else LOG("[vulkan compact driver mismatch] cannot write dump=%s",path);
  }
}
static void driver_compact_point_test(unsigned vertex_marker,unsigned fragment_marker,const char *label) {
  vk_state saved=S;uint64_t last0,content0,misses0;int original;
  vk_driver_constant_stats(&last0,&content0,&misses0,&original);
  VkFormatProperties properties;vk_format_properties(VK_FORMAT_R32G32B32A32_UINT,&properties);
  CHECK(properties.optimalTilingFeatures&VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,8<<20));memset(storage,0,8<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,8<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  uint8_t controls[3][0x880];const unsigned markers[]={vertex_marker,fragment_marker,50};
  for(unsigned n=0;n<3;n++)memset(controls[n],markers[n],sizeof controls[n]);
  NVNprogram graphics,compute;NVNshaderData shaders[2]={{.control=controls[0]},{.control=controls[1]}};
  CHECK(CALL(nvnProgramInitialize,&graphics,&device));CHECK(CALL(nvnProgramSetShaders,&graphics,2,shaders));
  CHECK(CALL(nvnProgramInitialize,&compute,&device));shaders[0].control=controls[2];CHECK(CALL(nvnProgramSetShaders,&compute,1,shaders));
  NVNtexture textures[8][2];
  for(unsigned seq=0;seq<8;seq++)for(unsigned k=0;k<2;k++) {
    NVNtextureBuilder tb;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
    CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&tb,128);CALL(nvnTextureBuilderSetHeight,&tb,64);
    CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA32UI);
    CHECK(CALL(nvnTextureBuilderGetStorageSize,&tb)<=262144);
    CALL(nvnTextureBuilderSetStorage,&tb,&pool,(3<<20)+(seq*2+k)*262144);
    CHECK(CALL(nvnTextureInitialize,&textures[seq][k],&tb));
  }
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);
  CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);
  uint32_t (*expected)[32768]=malloc(2*131072);CHECK(expected!=NULL);
  uint32_t *pixels=malloc(131072);CHECK(pixels!=NULL);
  for(unsigned mode=0;mode<4;mode++) {
    vk_driver_constant_reference(mode==0||mode==3);
    LOG("[vulkan compact driver fixture] begin mode=%u reference=%u dynamic_reference=%d narrow_reference=%d template_reference=%d markers=%u/%u label=%s VS/FS=integer attachments CS=storage",
      mode,mode==0||mode==3,vk_dynamic_uniform_reference_mode(),vk_dynamic_uniform_narrow_reference_mode(),vk_descriptor_template_reference_mode(),vertex_marker,fragment_marker,label);
    dm_guest_write_prepare(storage,3<<20);memset(storage,0,3<<20);
    mtl_sub_begin();S=(vk_state){.program=(const prog_t *)&graphics,.depth_range={0,1},.chmask={15},.has_scissor=1,.has_viewport=1};
    S.chmask.mask=255;S.viewport[0][2]=128;S.viewport[0][3]=64;S.scissor=(p_scissor){0,0,128,64};
    S.poly=*(const poly_st *)&polygon;S.ds=*(const ds_st *)&ds;S.color=*(const color_st *)&color;
    // Sentinel values make an uncovered zero-tail pixel fail as well.
    for(unsigned seq=0;seq<8;seq++)for(unsigned k=0;k<2;k++) {
      p_clear_tex clear={.tex=(const tex_t *)&textures[seq][k],.r={0,0,0,128,64,1},.mask=15,
                         .c.f={7654321,7654322,7654323,7654324}};
      vk_texture_clear(&clear);
    }
    vk_state a;
    for(unsigned seq=0;seq<8;seq++) {
      unsigned variant=seq==2||seq==3;
      if(seq==7){} // Consume the actual bq_after_draw restore, without rebinding.
      else if(seq==4)vk_state_restore(&a); // Direct upstream-style state restore, without binding APIs.
      else {driver_fixture_seed(0,variant,storage);driver_fixture_seed(1,variant,storage+(1<<20));}
      if(seq==6)vk_test_driver_bloom_begin(0,driver_fixture_handle(0,1,0));
      if(!seq)a=S;
      if(seq>=4)for(unsigned stage=0;stage<2;stage++) {
        // These bits are deliberately absent from the ABI. They must not
        // change its normalized key or leak into the GPU table.
        for(unsigned n=0;n<15;n++)driver_fixture_bind_buffer(stage,n,0,S.ssbo[stage][n].addr,S.ssbo[stage][n].size^(UINT64_C(1)<<48));
        for(unsigned n=0;n<128;n++) {driver_fixture_bind_handle(stage,0,n,S.tex[stage][n]^(UINT64_C(1)<<60));driver_fixture_bind_handle(stage,1,n,S.samp[stage][n]^(UINT64_C(1)<<59));}
        driver_fixture_bind_handle(stage,2,31,S.img[stage][31]^0x1234); // c2 exposes only image slots 0..7.
      }
      S.targets=(p_set_rt){.n=2,.color={(const tex_t *)&textures[seq][0],(const tex_t *)&textures[seq][1]}};
      vk_pipeline_state_changed();vk_draw(&(p_draw){.prim=NVN_DRAW_PRIMITIVE_POINTS,.count=8192},NULL);
    }
    vk_render_end();S.program=(const prog_t *)&compute;
    vk_state ca;
    for(unsigned seq=0;seq<6;seq++) {
      if(seq==4)vk_state_restore(&ca);else driver_fixture_seed(5,seq==2||seq==3,storage+(2<<20));
      if(!seq)ca=S;
      if(seq>=4)for(unsigned n=0;n<16;n++) {
        driver_fixture_bind_buffer(5,n,1,S.ubo[5][n].addr,S.ubo[5][n].size^(UINT64_C(1)<<48));
        if(n!=15)driver_fixture_bind_buffer(5,n,0,S.ssbo[5][n].addr,S.ssbo[5][n].size^(UINT64_C(1)<<49));
      }
      vk_dispatch(&(p_dispatch){1,1,1});
    }
    CHECK(mtl_wait(mtl_sub_commit(),UINT64_MAX));
    // The graphics path uses only integer color outputs, so it remains
    // executable when vertex/fragment storage atomics are unavailable.
    for(unsigned k=0;k<2;k++)for(unsigned seq=0;seq<8;seq++) {
      unsigned variant=seq==2||seq==3;
      driver_fixture_expected(k,variant,storage+(k<<20),expected[variant]);
      if(k==1 && seq==6)expected[variant][16384+22*4+2]=driver_fixture_handle(0,1,0);
      NVNcopyRegion region={0,0,0,128,64,1};
      CALL(nvnTextureReadTexels,&textures[seq][k],NULL,&region,pixels);
      for(unsigned word=0;word<32768;word++) {
        if(pixels[word]!=expected[variant][word]) {
          driver_fixture_mismatch(mode,k,seq,word,pixels,expected[variant]);
          // Compare the same pixels from the other attachment too: this
          // separates VS flat-varying transport from FS's own Uniform reads.
          uint32_t *peer=malloc(131072),*peer_expected=malloc(131072);
          if(peer && peer_expected) {
            CALL(nvnTextureReadTexels,&textures[seq][1-k],NULL,&region,peer);
            driver_fixture_expected(1-k,variant,storage+((1-k)<<20),peer_expected);
            unsigned first=0;while(first<32768 && peer[first]==peer_expected[first])first++;
            driver_fixture_mismatch(mode,1-k,seq,first,peer,peer_expected);
          }
          free(peer);free(peer_expected);
        }
        CHECK(pixels[word]==expected[variant][word]);
      }
    }
    // Compute never depends on either graphics storage-atomic feature.
    {
      uint32_t *out=(uint32_t *)(storage+(2<<20));CHECK(out[0]==6);
      for(unsigned variant=0;variant<2;variant++)driver_fixture_expected(5,variant,(uint8_t *)out,expected[variant]);
      unsigned counts[2]={0};unsigned base=49;
      for(unsigned seq=0;seq<6;seq++) {
        const uint32_t *actual=out+4+seq*32768;
        unsigned variant=actual[base*4+2]==(uint32_t)driver_fixture_size(0,1,0);counts[variant]++;
        for(unsigned word=0;word<32768;word++) {
          if(actual[word]!=expected[variant][word])driver_fixture_mismatch(mode,5,seq,word,actual,expected[variant]);
          CHECK(actual[word]==expected[variant][word]);
        }
      }
      CHECK(counts[0]==4&&counts[1]==2);
    }
  }
  uint64_t last,content,misses;int reference;vk_driver_constant_stats(&last,&content,&misses,&reference);
  CHECK(last>last0&&content>content0&&misses>misses0&&reference==1);
  vk_driver_constant_reference(original);vk_state_restore(&saved);vk_pipeline_state_changed();free(expected);free(pixels);
  for(unsigned seq=0;seq<8;seq++)for(unsigned k=0;k<2;k++)CALL(nvnTextureFinalize,&textures[seq][k]);
  CALL(nvnProgramFinalize,&graphics);CALL(nvnProgramFinalize,&compute);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);
  LOG("[vulkan compact driver fixture] markers=%u/%u label=%s non-atomic VS/FS integer attachments + independent CS complete c0+c2 GPU bytes, all slots/gaps/64KiB tails, 64-bit addresses/low32 sizes+handles, direct restore, immutable A/B/A and repeated reference/candidate passed",vertex_marker,fragment_marker,label);
}

// Historical loop controls run independently before the point diagnostics.
static void driver_fulltable_loop_test(void) {
  vk_state saved=S;uint64_t last0,content0,misses0;int original;
  vk_driver_constant_stats(&last0,&content0,&misses0,&original);
  if(!V.features.vertexPipelineStoresAndAtomics || !V.features.fragmentStoresAndAtomics) {
    LOG("[vulkan compact driver fixture] unavailable vertex/fragment storage atomics; existing FS driver pixel fixture remains active");return;
  }
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,4<<20));memset(storage,0,4<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,4<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  uint8_t controls[3][0x880];for(unsigned n=0;n<3;n++)memset(controls[n],48+n,sizeof controls[n]);
  NVNprogram graphics,compute;NVNshaderData shaders[2]={{.control=controls[0]},{.control=controls[1]}};
  CHECK(CALL(nvnProgramInitialize,&graphics,&device));CHECK(CALL(nvnProgramSetShaders,&graphics,2,shaders));
  CHECK(CALL(nvnProgramInitialize,&compute,&device));shaders[0].control=controls[2];CHECK(CALL(nvnProgramSetShaders,&compute,1,shaders));
  NVNtextureBuilder tb;NVNtexture texture;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&tb,1);CALL(nvnTextureBuilderSetHeight,&tb,1);
  CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);CALL(nvnTextureBuilderSetStorage,&tb,&pool,3<<20);CHECK(CALL(nvnTextureInitialize,&texture,&tb));
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);
  CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);
  uint32_t (*expected)[32768]=malloc(2*131072);CHECK(expected!=NULL);
  for(unsigned mode=0;mode<4;mode++) {
    vk_driver_constant_reference(mode==0||mode==3);
    LOG("[vulkan driver loop control] begin mode=%u reference=%u dynamic_reference=%d narrow_reference=%d template_reference=%d markers=48/49",mode,mode==0||mode==3,vk_dynamic_uniform_reference_mode(),vk_dynamic_uniform_narrow_reference_mode(),vk_descriptor_template_reference_mode());
    dm_guest_write_prepare(storage,3<<20);memset(storage,0,3<<20);
    mtl_sub_begin();S=(vk_state){.program=(const prog_t *)&graphics,.depth_range={0,1},.chmask={15},.has_scissor=1,.has_viewport=1};
    S.targets=(p_set_rt){.n=1,.color={(const tex_t *)&texture}};S.viewport[0][2]=S.viewport[0][3]=1;S.scissor=(p_scissor){0,0,1,1};
    S.poly=*(const poly_st *)&polygon;S.ds=*(const ds_st *)&ds;S.color=*(const color_st *)&color;
    vk_state a;
    for(unsigned seq=0;seq<6;seq++) {
      unsigned variant=seq==2||seq==3;
      if(seq==4)vk_state_restore(&a); // Direct upstream-style state restore, without binding APIs.
      else {driver_fixture_seed(0,variant,storage);driver_fixture_seed(1,variant,storage+(1<<20));}
      if(!seq)a=S;
      if(seq>=4)for(unsigned stage=0;stage<2;stage++) {
        // These bits are deliberately absent from the ABI. They must not
        // change its normalized key or leak into the GPU table.
        for(unsigned n=0;n<15;n++)driver_fixture_bind_buffer(stage,n,0,S.ssbo[stage][n].addr,S.ssbo[stage][n].size^(UINT64_C(1)<<48));
        for(unsigned n=0;n<128;n++) {driver_fixture_bind_handle(stage,0,n,S.tex[stage][n]^(UINT64_C(1)<<60));driver_fixture_bind_handle(stage,1,n,S.samp[stage][n]^(UINT64_C(1)<<59));}
        driver_fixture_bind_handle(stage,2,31,S.img[stage][31]^0x1234); // c2 exposes only image slots 0..7.
      }
      vk_pipeline_state_changed();vk_draw(&(p_draw){.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3},NULL);
    }
    vk_render_end();S.program=(const prog_t *)&compute;
    vk_state ca;
    for(unsigned seq=0;seq<6;seq++) {
      if(seq==4)vk_state_restore(&ca);else driver_fixture_seed(5,seq==2||seq==3,storage+(2<<20));
      if(!seq)ca=S;
      if(seq>=4)for(unsigned n=0;n<16;n++) {
        driver_fixture_bind_buffer(5,n,1,S.ubo[5][n].addr,S.ubo[5][n].size^(UINT64_C(1)<<48));
        if(n!=15)driver_fixture_bind_buffer(5,n,0,S.ssbo[5][n].addr,S.ssbo[5][n].size^(UINT64_C(1)<<49));
      }
      vk_dispatch(&(p_dispatch){1,1,1});
    }
    CHECK(mtl_wait(mtl_sub_commit(),UINT64_MAX));
    const unsigned stages[]={0,1,5};
    for(unsigned k=0;k<3;k++) {
      uint32_t *out=(uint32_t *)(storage+(k<<20));CHECK(out[0]==6);
      for(unsigned variant=0;variant<2;variant++)driver_fixture_expected(stages[k],variant,(uint8_t *)out,expected[variant]);
      unsigned counts[2]={0};unsigned base=stages[k]==0?17:stages[k]==1?81:49;
      for(unsigned seq=0;seq<6;seq++) {
        const uint32_t *actual=out+4+seq*32768;
        unsigned variant=actual[base*4+2]==(uint32_t)driver_fixture_size(0,1,0);counts[variant]++;
        for(unsigned word=0;word<32768;word++) {
          if(actual[word]!=expected[variant][word])driver_fixture_mismatch(mode,stages[k],seq,word,actual,expected[variant]);
          CHECK(actual[word]==expected[variant][word]);
        }
      }
      CHECK(counts[0]==4&&counts[1]==2);
    }
    NVNcopyRegion region={0,0,0,1,1,1};uint8_t pixel[4];CALL(nvnTextureReadTexels,&texture,NULL,&region,pixel);
    CHECK(!memcmp(pixel,(uint8_t[]){255,255,255,255},4));
  }
  uint64_t last,content,misses;int reference;vk_driver_constant_stats(&last,&content,&misses,&reference);
  CHECK(last>last0&&content>content0&&misses>misses0&&reference==1);
  vk_driver_constant_reference(original);vk_state_restore(&saved);vk_pipeline_state_changed();free(expected);
  CALL(nvnTextureFinalize,&texture);CALL(nvnProgramFinalize,&graphics);CALL(nvnProgramFinalize,&compute);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);
  LOG("[vulkan driver loop control] markers=48/49/50 VS/FS/CS complete c0+c2 GPU bytes, all slots/gaps/64KiB tails, 64-bit addresses/low32 sizes+handles, direct restore, immutable A/B/A and repeated reference/candidate passed");
}

#include "native_vulkan_driver_sparse.inc"
static void driver_compact_key_test(void) {
  driver_sparse_key_test();
  driver_fixture_diagnostic_prefix="loop-";
  driver_fulltable_loop_test();
  driver_fixture_diagnostic_prefix="clamped-";
  driver_compact_point_test(57,58,"clamped");
  driver_fixture_diagnostic_prefix="";
  driver_compact_point_test(51,52,"original");
}
#endif
