// 原 NVN 字节范围拷贝/录制时常量快照，经 GPU 连续传送与整池护栏回读验。
// Original NVN byte-range copies and recorded constant snapshots, verified by
// dependent GPU transfers and whole-pool guard readback.
static void buffer_byte_test(void) {
  enum { span=128*1024 };
  uint8_t *data[2], *expected[2]; NVNmemoryPool pools[2];
  for (unsigned i=0;i<2;i++) {
    CHECK(!posix_memalign((void **)&data[i],65536,span));
    expected[i]=malloc(span); CHECK(expected[i]!=NULL);
    memset(data[i],i?0x5a:0xa5,span);
    memcpy(expected[i],data[i],span);
    NVNmemoryPoolBuilder builder={0};
    CALL(nvnMemoryPoolBuilderSetDefaults,&builder);
    CALL(nvnMemoryPoolBuilderSetDevice,&builder,&device);
    CALL(nvnMemoryPoolBuilderSetStorage,&builder,data[i],span);
    CALL(nvnMemoryPoolBuilderSetFlags,&builder,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
    CHECK(CALL(nvnMemoryPoolInitialize,&pools[i],&builder));
  }
  CALL(nvnCommandBufferBeginRecording,&cmd);
  CALL(nvnCommandBufferCopyBufferToBuffer,&cmd,(uintptr_t)(data[0]+1),
       (uintptr_t)(data[1]+3),1,0);
  expected[1][3]=0xa5;
  const unsigned lengths[]={1,2,3,5,7,31,63,257};
  unsigned test=0;
  for (unsigned src=0;src<4;src++) for (unsigned dst=0;dst<4;dst++)
    for (unsigned length=0;length<sizeof lengths/sizeof lengths[0];length++,test++) {
      unsigned n=lengths[length], target=0x1000+test*512;
      uint8_t seed[257];
      for (unsigned i=0;i<n;i++) seed[i]=(uint8_t)(test*37+i*13+7);
      CALL(nvnCommandBufferUpdateUniformBuffer,&cmd,(uintptr_t)(data[0]+0x100),512,src,n,seed);
      memcpy(expected[0]+0x100+src,seed,n);
      CALL(nvnCommandBufferCopyBufferToBuffer,&cmd,(uintptr_t)(data[0]+0x100+src),
           (uintptr_t)(data[0]+target+dst),n,0);
      CALL(nvnCommandBufferCopyBufferToBuffer,&cmd,(uintptr_t)(data[0]+target+dst),
           (uintptr_t)(data[1]+target+3-dst),n,0);
      memcpy(expected[0]+target+dst,seed,n);
      memcpy(expected[1]+target+3-dst,seed,n);
      // 调用方录制后改源数据，不得影响已录制的更新。
      // Caller changes after recording must not change the captured update.
      memset(seed,0,sizeof seed);
    }
  CHECK(test==128);
  CALL(nvnCommandBufferCopyBufferToBuffer,&cmd,0,0,0,0);
  CALL(nvnCommandBufferUpdateUniformBuffer,&cmd,(uintptr_t)data[0],512,0,0,data[0]);
  NVNcommandHandle commands=end();
  CHECK(data[0][0x100]==0xa5 && data[0][0x1000]==0xa5 && data[1][3]==0x5a);
  submit(commands);
  for (unsigned i=0;i<2;i++) CHECK(!memcmp(data[i],expected[i],span));
  // GPU 写回后 CPU 再改一个字节，下一提交须看见新值；含池末尾的一字节拷贝。
  // A CPU byte edit after GPU writeback must reach the next submit. Include a
  // one-byte copy at the exact end of both pools.
  data[0][0x1001]=expected[0][0x1001]=0xc3;
  memcpy(expected[1]+0x400,expected[0]+0x1000,7);
  expected[1][span-1]=expected[0][span-1];
  CALL(nvnCommandBufferBeginRecording,&cmd);
  CALL(nvnCommandBufferCopyBufferToBuffer,&cmd,(uintptr_t)(data[0]+0x1000),
       (uintptr_t)(data[1]+0x400),7,0);
  CALL(nvnCommandBufferCopyBufferToBuffer,&cmd,(uintptr_t)(data[0]+span-1),
       (uintptr_t)(data[1]+span-1),1,0);
  submit(end());
  for (unsigned i=0;i<2;i++) CHECK(!memcmp(data[i],expected[i],span));
  for (unsigned i=0;i<2;i++) {
    CALL(nvnMemoryPoolFinalize,&pools[i]); free(data[i]); free(expected[i]);
  }
}

// 原 CPU 池读取：无关写入不提交，相交写入等真实 GPU/事件，保留 begin 后 CPU 修改。
// CPU-pool reads: unrelated writes stay queued; overlapping writes complete on
// the real GPU before events/readback. CPU edits after begin remain observable.
static void buffer_cpu_read_test(void) {
  enum { span=65536 };uint8_t *data[2],*expected[2],out[32];NVNmemoryPool pools[2];
  for (unsigned i=0;i<2;i++) {
    CHECK(!posix_memalign((void **)&data[i],65536,span));expected[i]=malloc(span);CHECK(expected[i]!=NULL);
    memset(data[i],0x7c,span);memcpy(expected[i],data[i],span);
    NVNmemoryPoolBuilder b={0};CALL(nvnMemoryPoolBuilderSetDefaults,&b);
    CALL(nvnMemoryPoolBuilderSetDevice,&b,&device);CALL(nvnMemoryPoolBuilderSetStorage,&b,data[i],span);
    CALL(nvnMemoryPoolBuilderSetFlags,&b,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
    CHECK(CALL(nvnMemoryPoolInitialize,&pools[i],&b));
  }
  CHECK(vk_pool_read((uintptr_t)data[0],1,out)==-1);
  for (unsigned i=0;i<17;i++) data[0][0x100+i]=expected[0][0x100+i]=(uint8_t)(0x31+i);
  uint64_t fence=mtl_last_fence();volatile uint32_t event=0;
  mtl_sub_begin();
  mtl_sub_copy((uintptr_t)(data[0]+0x100),(uintptr_t)(data[1]+0x400),17);
  mtl_sub_event(&event,0,6);
  data[0][0x100]=expected[0][0x100]=0xe1;
  CHECK(vk_pool_read((uintptr_t)(data[0]+0x100),17,out)==0);
  CHECK(!memcmp(out,expected[0]+0x100,17)&&event==0);
  CHECK(vk_pool_read((uintptr_t)(data[1]+0x3e0),32,out)==0);
  CHECK(!memcmp(out,expected[1]+0x3e0,32)&&event==0);
  CHECK(vk_pool_read((uintptr_t)(data[1]+0x411),32,out)==0);
  CHECK(!memcmp(out,expected[1]+0x411,32)&&event==0);
  CHECK(vk_pool_read(UINT64_MAX-8,32,out)==-1&&event==0);
  CHECK(vk_pool_read((uintptr_t)data[0]+span,1,out)==-1&&event==0);
  CHECK(vk_pool_read((uintptr_t)data[0],1,NULL)==-1&&event==0);
  CHECK(vk_pool_read((uintptr_t)data[0],0,out)==-1&&event==0);
  uint64_t proof_serial=vk_recording_serial();
  memset(out,0x19,sizeof out);
  CHECK(vk_pool_try_read((uintptr_t)(data[1]+0x410),2,out)==0);
  CHECK(event==0&&vk_recording_serial()==proof_serial);
  for(unsigned i=0;i<sizeof out;i++) CHECK(out[i]==0x19);
  CHECK(vk_pool_try_read((uintptr_t)(data[0]+0x100),17,out)==1);
  CHECK(!memcmp(out,expected[0]+0x100,17)&&event==0);
  CHECK(vk_pool_try_read(UINT64_MAX-8,32,out)==-1);
  CHECK(vk_pool_try_read((uintptr_t)data[0],0,out)==-1);
  CHECK(vk_pool_try_read((uintptr_t)data[0],1,NULL)==-1);
  memcpy(expected[1]+0x400,expected[0]+0x100,17);
  CHECK(vk_pool_read((uintptr_t)(data[1]+0x410),2,out)==1);
  CHECK(!memcmp(out,expected[1]+0x410,2)&&event==6);
  CHECK(mtl_last_fence()==fence);
  data[1][0x410]=expected[1][0x410]=0x2a;
  CHECK(vk_pool_read((uintptr_t)(data[1]+0x410),2,out)==0);
  CHECK(!memcmp(out,expected[1]+0x410,2));
  mtl_sub_copy((uintptr_t)(data[1]+0x400),(uintptr_t)(data[1]+0x800),17);
  mtl_sub_event(&event,0,9);
  CHECK(vk_pool_read((uintptr_t)(data[1]+0x400),17,out)==0&&event==6);
  CHECK(!memcmp(out,expected[1]+0x400,17));
  memcpy(expected[1]+0x800,expected[1]+0x400,17);
  CHECK(vk_pool_read((uintptr_t)(data[1]+0x7ff),2,out)==1);
  CHECK(!memcmp(out,expected[1]+0x7ff,2)&&event==9);
  CHECK(mtl_last_fence()==fence);
  data[0][span-1]=expected[0][span-1]=0x33;
  CHECK(vk_pool_read((uintptr_t)(data[0]+span-1),1,out)==0&&out[0]==0x33);
  mtl_sub_event((void *)(data[0]+0x2000),0,0x89abcdef);
  CHECK(vk_pool_read((uintptr_t)(data[0]+0x2000),4,out)==1);
  uint32_t word;memcpy(&word,out,4);CHECK(word==0x89abcdef);
  memcpy(expected[0]+0x2000,out,4);CHECK(mtl_last_fence()==fence);
  mtl_sub_counter((uintptr_t)(data[0]+0x2200),NVN_COUNTER_TYPE_TIMESTAMP);
  mtl_sub_event(&event,0,11);
  CHECK(vk_pool_read((uintptr_t)(data[0]+0x21e0),32,out)==0&&event==9);
  CHECK(vk_pool_read((uintptr_t)(data[0]+0x2208),8,out)==1&&event==11);
  uint64_t ticks;memcpy(&ticks,out,8);CHECK(ticks>0&&ticks!=UINT64_C(0x7c7c7c7c7c7c7c7c));
  CHECK(vk_pool_read((uintptr_t)(data[0]+0x2200),16,out)==0);
  uint64_t query[2];memcpy(query,out,16);CHECK(query[0]==query[1]&&query[0]==ticks);
  memcpy(expected[0]+0x2200,out,16);CHECK(mtl_last_fence()==fence);
  uint64_t submitted=mtl_sub_commit();CHECK(submitted==fence+1);CHECK(mtl_wait(submitted,UINT64_MAX));
  for (unsigned i=0;i<2;i++) {
    CHECK(!memcmp(data[i],expected[i],span));CALL(nvnMemoryPoolFinalize,&pools[i]);free(data[i]);free(expected[i]);
  }
}

// Repeated multi-MiB GPU writes must invalidate reused CPU-cached readbacks.
// Compare every byte, including CPU source changes after command recording.
static void large_readback_reuse_test(void) {
  enum { span=2<<20 };
  uint8_t *data[2];NVNmemoryPool pools[2];
  for(unsigned i=0;i<2;i++) {
    CHECK(!posix_memalign((void **)&data[i],65536,span));
    memset(data[i],0,span);
    NVNmemoryPoolBuilder b={0};
    CALL(nvnMemoryPoolBuilderSetDefaults,&b);CALL(nvnMemoryPoolBuilderSetDevice,&b,&device);
    CALL(nvnMemoryPoolBuilderSetStorage,&b,data[i],span);
    CALL(nvnMemoryPoolBuilderSetFlags,&b,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
    CHECK(CALL(nvnMemoryPoolInitialize,&pools[i],&b));
  }
  for(unsigned pass=0;pass<4;pass++) {
    CALL(nvnCommandBufferBeginRecording,&cmd);
    CALL(nvnCommandBufferCopyBufferToBuffer,&cmd,(uintptr_t)data[0],(uintptr_t)data[1],span,0);
    for(unsigned i=0;i<span;i++) data[0][i]=(uint8_t)(i*13+(i>>9)*7+pass*53);
    submit(end());
    CHECK(!memcmp(data[0],data[1],span));
  }
  for(unsigned i=0;i<2;i++) { CALL(nvnMemoryPoolFinalize,&pools[i]);free(data[i]); }
  LOG("[vulkan large readback fixture] 4 repeated 2MiB GPU copies match every byte");
}

// A partial upload must not consume dirty generations in untouched pages.
// Include image-only internal work and transitions to/from the full policy.
static void deferred_pool_upload_test(void) {
  enum { span=65536 };uint8_t *data;CHECK(!posix_memalign((void **)&data,65536,span));
  memset(data,0x11,span);NVNmemoryPoolBuilder b={0};NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&b);CALL(nvnMemoryPoolBuilderSetDevice,&b,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&b,data,span);
  CALL(nvnMemoryPoolBuilderSetFlags,&b,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&b));
  CHECK(nvn_dirty_tracked(data,span));
  uint64_t full0,used0,protected0;int original;
  vk_pool_sync_policy_stats(&full0,&used0,&protected0,&original);
  vk_pool_sync_reference(0);
  mtl_sub_begin();vk_buffer *mirror;uint64_t offset;
  CHECK(mtl_addr((uintptr_t)data,4,(void **)&mirror,&offset));
  mtl_sub_copy((uintptr_t)data,(uintptr_t)(data+0x8000),64);
  mtl_sub_copy((uintptr_t)(data+0x2000),(uintptr_t)(data+0x9000),64);
  CHECK(mtl_wait(mtl_sub_commit(),UINT64_MAX));
  for(unsigned round=0;round<2;round++) {
    uint8_t a=0x22+round*0x22,c=0x33+round*0x22;
    memset(data,a,64);memset(data+0x2000,c,64);
    uint64_t full_before,used_before,protect_before;int reference;
    vk_pool_sync_policy_stats(&full_before,&used_before,&protect_before,&reference);
    mtl_sub_begin();vk_internal_wait(vk_internal_commit());
    uint64_t full_after,used_after,protect_after;
    vk_pool_sync_policy_stats(&full_after,&used_after,&protect_after,&reference);
    CHECK(!reference);
    CHECK(full_before==full_after&&used_before==used_after&&protect_before==protect_after);
    CHECK(mirror->map[offset]!=a&&mirror->map[offset+0x2000]!=c);
    mtl_sub_begin();mtl_sub_copy((uintptr_t)data,(uintptr_t)(data+0x8000),64);
    CHECK(mtl_wait(mtl_sub_commit(),UINT64_MAX));
    for(unsigned i=0;i<64;i++) CHECK(data[0x8000+i]==a);
    CHECK(mirror->map[offset+0x2000]!=c);
    mtl_sub_begin();mtl_sub_copy((uintptr_t)(data+0x2000),(uintptr_t)(data+0x9000),64);
    CHECK(mtl_wait(mtl_sub_commit(),UINT64_MAX));
    for(unsigned i=0;i<64;i++) CHECK(data[0x9000+i]==c);
    if(!round) {
      vk_pool_sync_reference(1);memset(data+0x2000,0x8f,64);
      mtl_sub_begin();mtl_sub_copy((uintptr_t)(data+0x2000),(uintptr_t)(data+0x9000),64);
      CHECK(mtl_wait(mtl_sub_commit(),UINT64_MAX));
      for(unsigned i=0;i<64;i++) CHECK(data[0x9000+i]==0x8f);
      vk_pool_sync_reference(0);
    }
  }
  vk_pool_sync_reference(original);
  CALL(nvnMemoryPoolFinalize,&pool);free(data);
  LOG("[vulkan deferred upload fixture] untouched dirty pages survive partial uploads, image-only submits do no pool work, full/partial transitions preserve bytes");
}

// 原 Metal 按地址绑定常量/存储；Vulkan 对齐副本不能丢尾字节或污染相邻字节。
// Original Metal binds buffer addresses; Vulkan alignment copies must preserve
// trailing bytes and neighbours, including dependent shader writes and updates.
static void buffer_binding_byte_test(const void *vertex, const void *uniform,
                                    const void *readonly, const void *compute) {
  enum { span=65536 };
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,1<<20));
  memset(storage,0,1<<20);uint8_t *data=storage+0x10000,*expected=malloc(span);CHECK(expected!=NULL);
  NVNmemoryPoolBuilder mb={0};NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtextureBuilder tb={0};NVNtexture target;
  CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&tb,16);
  CALL(nvnTextureBuilderSetHeight,&tb,16);CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);
  CALL(nvnTextureBuilderSetStorage,&tb,&pool,0);CHECK(CALL(nvnTextureInitialize,&target,&tb));
  NVNprogram programs[3];NVNshaderData shaders[]={{.control=vertex},{.control=uniform}};
  for (unsigned i=0;i<3;i++) {
    CHECK(CALL(nvnProgramInitialize,&programs[i],&device));
    if(i==1) shaders[1].control=readonly;
    if(i==2) shaders[0].control=compute;
    CHECK(CALL(nvnProgramSetShaders,&programs[i],i==2?1:2,shaders));
  }
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);
  CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);
  CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);
  CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);
  NVNchannelMaskState channels;CALL(nvnChannelMaskStateSetDefaults,&channels);
  const NVNtexture *colors[]={&target};NVNcopyRegion region={0,0,0,16,16,1};
  const float viewport[]={0,0,16,16},red[]={1,0,0,1},blue[]={0,0,1,1};uint8_t pixels[16*16*4];
  const unsigned offsets[]={0,1,2,3,4,8,12,16,32,48};
  for(unsigned kind=0;kind<2;kind++) for(unsigned oi=0;oi<sizeof offsets/sizeof offsets[0];oi++) for(unsigned tail=1;tail<=3;tail+=2) {
    unsigned off=offsets[oi];
    unsigned pos=0x4000+off,size=16+tail;
    memset(data,0xa5,span);memcpy(data+pos,red,16);memcpy(expected,data,span);
    CALL(nvnCommandBufferBeginRecording,&cmd);CALL(nvnCommandBufferSetRenderTargets,&cmd,1,colors,NULL,NULL,NULL);
    CALL(nvnCommandBufferSetViewports,&cmd,0,1,viewport);
    CALL(nvnCommandBufferBindVertexAttribState,&cmd,0,NULL);CALL(nvnCommandBufferBindVertexStreamState,&cmd,0,NULL);
    CALL(nvnCommandBufferBindPolygonState,&cmd,&polygon);CALL(nvnCommandBufferBindDepthStencilState,&cmd,&ds);
    CALL(nvnCommandBufferBindColorState,&cmd,&color);CALL(nvnCommandBufferBindChannelMaskState,&cmd,&channels);
    CALL(nvnCommandBufferBindProgram,&cmd,&programs[kind],NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    if(kind) CALL(nvnCommandBufferBindStorageBuffer,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,(uintptr_t)(data+pos),size);
    else CALL(nvnCommandBufferBindUniformBuffer,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,(uintptr_t)(data+pos),size);
    for(unsigned part=0;part<3;part++) {
      if(part==1) CALL(nvnCommandBufferUpdateUniformBuffer,&cmd,(uintptr_t)(data+pos),size,0,16,blue);
      CALL(nvnCommandBufferSetScissor,&cmd,part*5,0,part==2?6:5,16);
      CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    }
    submit(end());memcpy(expected+pos,blue,16);CHECK(!memcmp(data,expected,span));
    CALL(nvnTextureReadTexels,&target,NULL,&region,pixels);
    for(unsigned y=0;y<16;y++) for(unsigned x=0;x<16;x++) {
      uint8_t pixel[]={x<5?255:0,0,x<5?0:255,255};
      CHECK(!memcmp(pixels+(y*16+x)*4,pixel,4));
    }
  }
  for(unsigned si=0;si<sizeof offsets/sizeof offsets[0];si++) for(unsigned di=0;di<sizeof offsets/sizeof offsets[0];di++)
    for(unsigned tail=1;tail<=3;tail+=2) {
      unsigned src_off=offsets[si],dst_off=offsets[di];
      unsigned src=0x100+src_off,dst=0x1000+dst_off,mid=0x2000+offsets[(di+3)%(sizeof offsets/sizeof offsets[0])],out=0x3000+src_off,size=256+tail;
      memset(data,0xa5,span);
      for(unsigned i=0;i<64;i++) { uint32_t v=i*17+src_off*31+dst_off;memcpy(data+src+i*4,&v,4); }
      memcpy(expected,data,span);uint32_t changed=0x12345678;memcpy(expected+src,&changed,4);
      CALL(nvnCommandBufferBeginRecording,&cmd);
      CALL(nvnCommandBufferUpdateUniformBuffer,&cmd,(uintptr_t)(data+src),size,0,4,&changed);
      CALL(nvnCommandBufferBindProgram,&cmd,&programs[2],NVN_SHADER_STAGE_COMPUTE_BIT);
      CALL(nvnCommandBufferBindStorageBuffer,&cmd,NVN_SHADER_STAGE_COMPUTE,0,(uintptr_t)(data+src),size);
      CALL(nvnCommandBufferBindStorageBuffer,&cmd,NVN_SHADER_STAGE_COMPUTE,1,(uintptr_t)(data+dst),size);
      CALL(nvnCommandBufferDispatchCompute,&cmd,1,1,1);
      CALL(nvnCommandBufferBindStorageBuffer,&cmd,NVN_SHADER_STAGE_COMPUTE,0,(uintptr_t)(data+dst),size);
      CALL(nvnCommandBufferBindStorageBuffer,&cmd,NVN_SHADER_STAGE_COMPUTE,1,(uintptr_t)(data+mid),size);
      CALL(nvnCommandBufferDispatchCompute,&cmd,1,1,1);
      CALL(nvnCommandBufferCopyBufferToBuffer,&cmd,(uintptr_t)(data+mid),(uintptr_t)(data+out),size,0);
      for(unsigned i=0;i<64;i++) {
        uint32_t v;memcpy(&v,expected+src+i*4,4);v=v*3+7;memcpy(expected+dst+i*4,&v,4);
        v=v*3+7;memcpy(expected+mid+i*4,&v,4);
      }
      memcpy(expected+out,expected+mid,size);submit(end());CHECK(!memcmp(data,expected,span));
    }
  // A readonly storage texel descriptor is a fragment consumer too. GPU
  // updates between two draws must reach it without relying on a writable
  // descriptor fallback or a blanket pass-start memory barrier.
  uint8_t texel_control[0x880];memset(texel_control,31,sizeof texel_control);
  NVNprogram texel_program;NVNtexture texel_texture;NVNtexturePool texture_pool;
  NVNshaderData texel_shaders[]={{.control=vertex},{.control=texel_control}};
  CHECK(CALL(nvnProgramInitialize,&texel_program,&device));
  CHECK(CALL(nvnProgramSetShaders,&texel_program,2,texel_shaders));
  CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_BUFFER);
  CALL(nvnTextureBuilderSetWidth,&tb,4);CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_R32UI);
  CALL(nvnTextureBuilderSetStorage,&tb,&pool,0x14000);
  CHECK(CALL(nvnTextureInitialize,&texel_texture,&tb));
  CHECK(CALL(nvnTexturePoolInitialize,&texture_pool,&pool,786432,4));
  CALL(nvnTexturePoolRegisterTexture,&texture_pool,1,&texel_texture,NULL);
  NVNimageHandle texel_handle=1;
  CALL(nvnCommandBufferBeginRecording,&cmd);
  CALL(nvnCommandBufferSetTexturePool,&cmd,&texture_pool);
  CALL(nvnCommandBufferBindProgram,&cmd,&texel_program,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
  CALL(nvnCommandBufferBindImages,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&texel_handle);
  for(unsigned part=0;part<2;part++) {
    CALL(nvnCommandBufferUpdateUniformBuffer,&cmd,(uintptr_t)(data+0x4000),16,0,16,part?blue:red);
    CALL(nvnCommandBufferSetScissor,&cmd,part*8,0,8,16);
    CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
  }
  submit(end());CALL(nvnTextureReadTexels,&target,NULL,&region,pixels);
  for(unsigned y=0;y<16;y++) for(unsigned x=0;x<16;x++) {
    uint8_t pixel[]={x<8?255:0,0,x<8?0:255,255};
    CHECK(!memcmp(pixels+(y*16+x)*4,pixel,4));
  }
  CALL(nvnTextureFinalize,&texel_texture);CALL(nvnTexturePoolFinalize,&texture_pool);
  CALL(nvnProgramFinalize,&texel_program);
  LOG("[vulkan fragment texel dependency fixture] readonly storage image-buffer sees ordered GPU updates in both shaded halves");
  for(unsigned i=0;i<3;i++) CALL(nvnProgramFinalize,&programs[i]);
  CALL(nvnTextureFinalize,&target);CALL(nvnMemoryPoolFinalize,&pool);free(expected);free(storage);
}

// Lengths, independent stage prefixes, and compute-produced vertex input must
// remain unchanged when descriptors include bytes before the logical binding.
static void storage_offset_dependency_test(const void *fragment) {
  enum { span=1<<20 };
  // This expanded producer/consumer matrix owns its command arena. Do not
  // exhaust or overwrite handles recorded by the surrounding fixture.
  NVNcommandBuffer saved_cmd=cmd;
  uint8_t *command_bytes;CHECK(!posix_memalign((void **)&command_bytes,65536,span));
  NVNmemoryPoolBuilder command_builder;NVNmemoryPool command_pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&command_builder);
  CALL(nvnMemoryPoolBuilderSetDevice,&command_builder,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&command_builder,command_bytes,span);
  CALL(nvnMemoryPoolBuilderSetFlags,&command_builder,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&command_pool,&command_builder));
  CHECK(CALL(nvnCommandBufferInitialize,&cmd,&device));
  CALL(nvnCommandBufferAddCommandMemory,&cmd,&command_pool,0,span);
  uint8_t *data,*expected=malloc(span);CHECK(expected!=NULL);
  CHECK(!posix_memalign((void **)&data,65536,span));memset(data,0xa5,span);
  NVNmemoryPoolBuilder mb={0};NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,data,span);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  uint8_t controls[6][0x880];NVNprogram programs[6];
  for(unsigned i=0;i<6;i++) {
    memset(controls[i],24+i,sizeof controls[i]);
    CHECK(CALL(nvnProgramInitialize,&programs[i],&device));
    NVNshaderData sd[2]={{.control=controls[i]},{.control=fragment}};
    CHECK(CALL(nvnProgramSetShaders,&programs[i],i==0||i>=4?2:1,sd));
  }
  const unsigned offsets[]={0,4,8,12,16,32,48};
  for(unsigned kind=0;kind<2;kind++) for(unsigned i=0;i<7;i++) for(unsigned tail=0;tail<4;tail++) {
    unsigned src=0x10000+offsets[i],dst=0x20000+offsets[(i+2)%7];
    unsigned source_bytes=20+tail,dest_bytes=44+tail;
    memset(data,0xa5,span);
    for(unsigned w=0;w<5;w++) { uint32_t value=0x45123400+w;memcpy(data+src+4*w,&value,4); }
    memcpy(expected,data,span);
    uint32_t words[]={5,11,0x45123400,0x45123404};
    memcpy(expected+dst,words,sizeof words);uint32_t last=0x91abcdef;
    memcpy(expected+dst+40,&last,4);
    CALL(nvnCommandBufferBeginRecording,&cmd);
    CALL(nvnCommandBufferBindProgram,&cmd,&programs[kind?3:1],NVN_SHADER_STAGE_COMPUTE_BIT);
    if(kind) CALL(nvnCommandBufferBindUniformBuffer,&cmd,NVN_SHADER_STAGE_COMPUTE,0,(uintptr_t)(data+src),source_bytes);
    else CALL(nvnCommandBufferBindStorageBuffer,&cmd,NVN_SHADER_STAGE_COMPUTE,0,(uintptr_t)(data+src),source_bytes);
    CALL(nvnCommandBufferBindStorageBuffer,&cmd,NVN_SHADER_STAGE_COMPUTE,1,(uintptr_t)(data+dst),dest_bytes);
    CALL(nvnCommandBufferDispatchCompute,&cmd,1,1,1);submit(end());
    CHECK(!memcmp(data,expected,span));
  }
  NVNtextureBuilder tb={0};NVNtexture target;
  CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&tb,16);
  CALL(nvnTextureBuilderSetHeight,&tb,16);CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);
  CALL(nvnTextureBuilderSetStorage,&tb,&pool,0);CHECK(CALL(nvnTextureInitialize,&target,&tb));
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);
  CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);
  CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);
  NVNchannelMaskState channels;CALL(nvnChannelMaskStateSetDefaults,&channels);
  const NVNtexture *colors[]={&target};NVNcopyRegion region={0,0,0,16,16,1};
  const float viewport[]={0,0,16,16},positions[]={-1,-1,3,-1,-1,3},red[]={1,0,0,1},blue[]={0,0,1,1};
  uint8_t pixels[16*16*4];
  NVNtexturePool textures;
  CHECK(CALL(nvnTexturePoolInitialize,&textures,&pool,0x80000,4));
  for(unsigned vertex_kind=0;vertex_kind<3;vertex_kind++)
  for(unsigned producer=0;producer<3;producer++)
  for(unsigned indexed=0;indexed<2;indexed++) for(unsigned i=0;i<7;i++) {
    unsigned src=0x10000+offsets[i],dst=0x20000+offsets[(i+2)%7],shade=0x30000+offsets[(i+4)%7];
    if(vertex_kind) dst=0x20000+i*V.props.limits.minTexelBufferOffsetAlignment;
    const uint32_t indices[]={0,1,2};
    const void *produced=indexed?(const void *)indices:(const void *)positions;
    unsigned produced_bytes=indexed?sizeof indices:sizeof positions;
    memcpy(data+src,produced,produced_bytes);memcpy(data+shade,red,sizeof red);
    memcpy(data+0x40000,positions,sizeof positions);
    memset(data+dst,0xa5,produced_bytes);
    NVNtexture position_texture;
    if(vertex_kind) {
      CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_BUFFER);
      CALL(nvnTextureBuilderSetWidth,&tb,6);CALL(nvnTextureBuilderSetHeight,&tb,1);
      CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_R32UI);
      CALL(nvnTextureBuilderSetStorage,&tb,&pool,indexed?0x40000:dst);
      CHECK(CALL(nvnTextureInitialize,&position_texture,&tb));
      CALL(nvnTexturePoolRegisterTexture,&textures,1,&position_texture,NULL);
    }
    CALL(nvnCommandBufferBeginRecording,&cmd);
    if(producer==0) {
      CALL(nvnCommandBufferBindProgram,&cmd,&programs[2],NVN_SHADER_STAGE_COMPUTE_BIT);
      CALL(nvnCommandBufferBindStorageBuffer,&cmd,NVN_SHADER_STAGE_COMPUTE,0,(uintptr_t)(data+src),produced_bytes);
      CALL(nvnCommandBufferBindStorageBuffer,&cmd,NVN_SHADER_STAGE_COMPUTE,1,(uintptr_t)(data+dst),produced_bytes);
      CALL(nvnCommandBufferDispatchCompute,&cmd,1,1,1);
    } else if(producer==1)
      CALL(nvnCommandBufferCopyBufferToBuffer,&cmd,(uintptr_t)(data+src),(uintptr_t)(data+dst),produced_bytes,0);
    else
      CALL(nvnCommandBufferUpdateUniformBuffer,&cmd,(uintptr_t)(data+dst),produced_bytes,0,produced_bytes,produced);
    if(i==0) {
      // Submit the producer without QueueFinish. Its hazard must survive the
      // next batch even if GPU completion/CPU publication happens quickly.
      NVNcommandHandle producer=end();
      CALL(nvnQueueSubmitCommands,&queue,1,&producer);
      CALL(nvnCommandBufferBeginRecording,&cmd);
    }
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,colors,NULL,NULL,NULL);
    CALL(nvnCommandBufferSetViewports,&cmd,0,1,viewport);
    CALL(nvnCommandBufferBindVertexAttribState,&cmd,0,NULL);CALL(nvnCommandBufferBindVertexStreamState,&cmd,0,NULL);
    CALL(nvnCommandBufferBindPolygonState,&cmd,&polygon);CALL(nvnCommandBufferBindDepthStencilState,&cmd,&ds);
    CALL(nvnCommandBufferBindColorState,&cmd,&color);CALL(nvnCommandBufferBindChannelMaskState,&cmd,&channels);
    CALL(nvnCommandBufferBindProgram,&cmd,&programs[vertex_kind?3+vertex_kind:0],NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    if(vertex_kind) {
      CALL(nvnCommandBufferSetTexturePool,&cmd,&textures);
      if(vertex_kind==1) {
        NVNseparateTextureHandle h={.value=1};
        CALL(nvnCommandBufferBindSeparateTextures,&cmd,NVN_SHADER_STAGE_VERTEX,0,1,&h);
      } else {
        NVNimageHandle h=1;
        CALL(nvnCommandBufferBindImages,&cmd,NVN_SHADER_STAGE_VERTEX,0,1,&h);
      }
    }
    CALL(nvnCommandBufferBindStorageBuffer,&cmd,NVN_SHADER_STAGE_VERTEX,0,
         (uintptr_t)(data+(indexed?0x40000:dst)),sizeof positions);
    CALL(nvnCommandBufferBindStorageBuffer,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,(uintptr_t)(data+shade),sizeof red);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,8,16);
    if(indexed) CALL(nvnCommandBufferDrawElementsInstanced,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,
        NVN_INDEX_TYPE_UNSIGNED_INT,3,(uintptr_t)(data+dst),0,0,1);
    else CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    CALL(nvnCommandBufferUpdateUniformBuffer,&cmd,(uintptr_t)(data+shade),sizeof blue,0,sizeof blue,blue);
    CALL(nvnCommandBufferSetScissor,&cmd,8,0,8,16);
    if(indexed) CALL(nvnCommandBufferDrawElementsInstanced,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,
        NVN_INDEX_TYPE_UNSIGNED_INT,3,(uintptr_t)(data+dst),0,0,1);
    else CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    submit(end());CHECK(!memcmp(data+dst,produced,produced_bytes));CHECK(!memcmp(data+shade,blue,sizeof blue));
    CALL(nvnTextureReadTexels,&target,NULL,&region,pixels);
    for(unsigned y=0;y<16;y++) for(unsigned x=0;x<16;x++) {
      uint8_t pixel[]={x<8?255:0,0,x<8?0:255,255};CHECK(!memcmp(pixels+(y*16+x)*4,pixel,4));
    }
    if(vertex_kind) CALL(nvnTextureFinalize,&position_texture);
  }
  CALL(nvnTexturePoolFinalize,&textures);
  CALL(nvnTextureFinalize,&target);
  for(unsigned i=0;i<6;i++) CALL(nvnProgramFinalize,&programs[i]);
  CALL(nvnMemoryPoolFinalize,&pool);free(expected);free(data);
  CALL(nvnCommandBufferFinalize,&cmd);cmd=saved_cmd;
  CALL(nvnMemoryPoolFinalize,&command_pool);free(command_bytes);
  LOG("[vulkan storage offset fixture] runtime lengths/trailing bytes/whole-pool guards, compute UBO pointers, compute to VS and independent VS/FS prefixes passed");
}

// Actual BufferUsageFlags.Write and TextureUsageFlags.ImageStore are distinct.
// Three queued guest submits verify compute reader -> texel writer -> reader,
// and CPU publication of the true texel write, without intermediate CPU waits.
static void compute_texel_dependencies_test(const void *copy_control) {
  uint8_t *data;CHECK(!posix_memalign((void **)&data,65536,1<<20));memset(data,0,1<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;NVNtexturePool textures;NVNtexture image;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,data,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtextureBuilder tb;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_BUFFER);CALL(nvnTextureBuilderSetWidth,&tb,64);
  CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_R32UI);CALL(nvnTextureBuilderSetStorage,&tb,&pool,0);
  CHECK(CALL(nvnTextureInitialize,&image,&tb));
  CHECK(CALL(nvnTexturePoolInitialize,&textures,&pool,786432,4));
  CALL(nvnTexturePoolRegisterTexture,&textures,1,&image,NULL);
  uint8_t controls[2][0x880];NVNprogram programs[3];
  for(unsigned i=0;i<2;i++) memset(controls[i],32+i,sizeof controls[i]);
  const void *code[]={copy_control,controls[0],controls[1]};
  for(unsigned i=0;i<3;i++) {
    NVNshaderData shader={.control=code[i]};CHECK(CALL(nvnProgramInitialize,&programs[i],&device));
    CHECK(CALL(nvnProgramSetShaders,&programs[i],1,&shader));
  }
  uint32_t *source=(void *)data,*first=(void *)(data+8192),*second=(void *)(data+16384),*third=(void *)(data+24576);
  for(unsigned i=0;i<64;i++) source[i]=i+3;
  NVNimageHandle h=1;
  for(unsigned phase=0;phase<3;phase++) {
    CALL(nvnCommandBufferBeginRecording,&cmd);
    CALL(nvnCommandBufferSetTexturePool,&cmd,&textures);
    CALL(nvnCommandBufferBindProgram,&cmd,&programs[phase==1?1:0],NVN_SHADER_STAGE_COMPUTE_BIT);
    if(phase==1) {
      CALL(nvnCommandBufferBindImages,&cmd,NVN_SHADER_STAGE_COMPUTE,0,1,&h);
      CALL(nvnCommandBufferDispatchCompute,&cmd,64,1,1);
    } else {
      CALL(nvnCommandBufferBindStorageBuffer,&cmd,NVN_SHADER_STAGE_COMPUTE,0,(uintptr_t)source,256);
      CALL(nvnCommandBufferBindStorageBuffer,&cmd,NVN_SHADER_STAGE_COMPUTE,1,(uintptr_t)(phase?second:first),256);
      CALL(nvnCommandBufferDispatchCompute,&cmd,1,1,1);
    }
    NVNcommandHandle handle=end();CALL(nvnQueueSubmitCommands,&queue,1,&handle);
  }
  CALL(nvnQueueFinish,&queue);
  for(unsigned i=0;i<64;i++) {
    CHECK(first[i]==(i+3)*3+7);CHECK(source[i]==256+i*17);CHECK(second[i]==source[i]*3+7);
  }
  vk_buffer_sync_stats before,after;vk_buffer_sync_snapshot(&before);
  CALL(nvnCommandBufferBeginRecording,&cmd);
  CALL(nvnCommandBufferBindProgram,&cmd,&programs[2],NVN_SHADER_STAGE_COMPUTE_BIT);
  CALL(nvnCommandBufferBindImages,&cmd,NVN_SHADER_STAGE_COMPUTE,0,1,&h);
  CALL(nvnCommandBufferBindStorageBuffer,&cmd,NVN_SHADER_STAGE_COMPUTE,0,(uintptr_t)third,256);
  CALL(nvnCommandBufferDispatchCompute,&cmd,64,1,1);submit(end());
  vk_buffer_sync_snapshot(&after);
  CHECK(after.readback_bytes-before.readback_bytes==256);
  for(unsigned i=0;i<64;i++) CHECK(third[i]==source[i]*3+7);
  // A stream of overlapping GPU writes must retain its real dependency
  // without making an untouched gap look written when the history fills.
  enum { copies=512, stride=128, width=256, offset=32768,
         bytes=(copies-1)*stride+width };
  uint8_t *expected=calloc(1,bytes);CHECK(expected!=NULL);
  mtl_sub_begin();
  vk_buffer *buffer;uint64_t base;
  CHECK(vk_pool_address_stage((uintptr_t)data,1<<20,(void **)&buffer,&base,VK_PIPELINE_STAGE_TRANSFER_BIT));
  vk_barrier();
  for(unsigned i=0;i<copies;i++) {
    vk_transfer_barrier();
    VkBufferCopy copy={base,base+offset+i*stride,width};
    vkCmdCopyBuffer(V.cmd,buffer->handle,buffer->handle,1,&copy);
    vk_buffer_transfer_written_range((uintptr_t)data+offset+i*stride,width);
    memcpy(expected+i*stride,source,width);
  }
  const unsigned separate[]={196608,131072,262144};
  for(unsigned i=0;i<3;i++) {
    vk_transfer_barrier();
    VkBufferCopy copy={base,base+separate[i],width};
    vkCmdCopyBuffer(V.cmd,buffer->handle,buffer->handle,1,&copy);
    vk_buffer_transfer_written_range((uintptr_t)data+separate[i],width);
    vk_buffer_writeback_stage((uintptr_t)data+separate[i],width,VK_PIPELINE_STAGE_TRANSFER_BIT);
  }
  if(vk_submission_barrier_mode()==0) {
    CHECK(vk_buffer_vertex_pending((uintptr_t)data+offset+16,4));
    CHECK(!vk_buffer_vertex_pending((uintptr_t)data+offset+bytes+32,4));
    for(unsigned i=0;i<3;i++) {
      CHECK(vk_buffer_vertex_pending((uintptr_t)data+separate[i],4));
      CHECK(!vk_buffer_vertex_pending((uintptr_t)data+separate[i]+width,4));
    }
  }
  vk_buffer_writeback_stage((uintptr_t)data+offset,bytes,VK_PIPELINE_STAGE_TRANSFER_BIT);
  vk_internal_wait(vk_internal_commit());
  CHECK(!memcmp(data+offset,expected,bytes));
  CHECK(data[offset-1]==0 && data[offset+bytes]==0);free(expected);
  for(unsigned i=0;i<3;i++) CHECK(!memcmp(data+separate[i],source,width));
  for(unsigned i=0;i<3;i++) CALL(nvnProgramFinalize,&programs[i]);
  CALL(nvnTextureFinalize,&image);CALL(nvnTexturePoolFinalize,&textures);CALL(nvnMemoryPoolFinalize,&pool);free(data);
  LOG("[vulkan compute texel dependency fixture] queued reader/writer/reader values and true write publication; readonly scaling flags do not cause source readback");
}
