// Finite owned MAP_SHARED backing; never identifies arbitrary guest addresses.
#include <sys/mman.h>
extern void vk_test_shared_image_backing(const void *,size_t);
extern void nvn_test_readback_observer(void (*)(int));
static _Atomic unsigned shared_readback_calls,shared_readback_wrong_owner;
static void shared_readback_owner(int dedicated) {
  atomic_fetch_add(&shared_readback_calls,1);
  if(!dedicated)atomic_store(&shared_readback_wrong_owner,1);
}
static void shared_image_pixels(NVNtexture *t,const uint8_t rgba[4]) {
  mtl_sub_begin();VkImageLayout layout;
  VkImage image=vk_texture_present_source((const tex_t *)t,&layout);
  vk_buffer *result=vk_temp_buffer(16*16*4);
  VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={16,16,1}};
  vkCmdCopyImageToBuffer(V.cmd,image,layout,result->handle,1,&copy);
  vk_internal_wait(vk_internal_commit());
  for(unsigned n=0;n<256;n++)CHECK(!memcmp(result->map+4*n,rgba,4));
}
static void shared_image_copy_test(NVNmemoryPool *pool,uint8_t *data) {
  const int formats[]={NVN_FORMAT_RGBA8,NVN_FORMAT_RGBA_DXT5,NVN_FORMAT_RGBA_ASTC_4x4};
  unsigned format_count=V.features.textureCompressionASTC_LDR?3:2;
  for(unsigned f=0;f<format_count;f++) {
    NVNtexture t[2];NVNtextureBuilder b;
    CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_2D_ARRAY);
    CALL(nvnTextureBuilderSetWidth,&b,16);CALL(nvnTextureBuilderSetHeight,&b,16);
    CALL(nvnTextureBuilderSetDepth,&b,2);CALL(nvnTextureBuilderSetLevels,&b,3);
    CALL(nvnTextureBuilderSetFormat,&b,formats[f]);
    NVNpackagedTextureLayout layout={{0}};CALL(nvnTextureBuilderSetPackagedTextureLayout,&b,&layout);
    VkImage physical[2];
    for(unsigned n=0;n<2;n++) {
      CALL(nvnTextureBuilderSetStorage,&b,pool,(1u<<20)+n*(256u<<10)+f*(64u<<10));
      CHECK(CALL(nvnTextureInitialize,&t[n],&b));
      tex_t *tex=(tex_t *)&t[n];CHECK(tex->L.size<65536);
      dm_guest_write_prepare(tex->mem,tex->L.size);memset(tex->mem,0,tex->L.size);
      mtl_sub_begin();VkImageLayout unused;physical[n]=vk_texture_present_source(tex,&unused);
      vk_internal_wait(vk_internal_commit());
    }
    tex_t *a=(tex_t *)&t[0],*d=(tex_t *)&t[1];
    int bw,bh,bpb=nvn_fmt_bpb(formats[f],&bw,&bh);
    size_t pitch=(8u+bw-1)/bw*bpb,rows=(8u+bh-1)/bh,nbytes=pitch*rows;
    uint8_t *linear=data+393216;memset(linear,0x11,nbytes);
    // Source really is GPU-produced; the CPU source is intentionally old.
    mtl_sub_begin();vk_buffer *buffer;uint64_t offset;
    CHECK(vk_pool_address_overwrite((uintptr_t)linear,nbytes,(void **)&buffer,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
    vkCmdFillBuffer(V.cmd,buffer->handle,offset,nbytes,0x27272727);
    vk_buffer_transfer_written_range((uintptr_t)linear,nbytes);
    vk_buffer_writeback_stage((uintptr_t)linear,nbytes,VK_PIPELINE_STAGE_TRANSFER_BIT);
    CHECK(linear[0]==0x11);
    p_copy_b2t copy={.tex=a,.src=(uintptr_t)linear,.has_view=1,
      .view={.set_levels=1,.base_level=1,.set_layers=1,.min_layer=1},
      .r={.width=8,.height=8,.depth=1}};
    vk_texture_copy_b2t(&copy,0,0);
    vk_buffer *gpu=vk_temp_buffer(nbytes);
    VkMemoryBarrier dependency={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT};
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&dependency,0,NULL,0,NULL);
    VkBufferImageCopy read={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,1,1,1},.imageExtent={8,8,1}};
    vkCmdCopyImageToBuffer(V.cmd,physical[0],VK_IMAGE_LAYOUT_GENERAL,gpu->handle,1,&read);
    vk_internal_wait(vk_internal_commit());
    uint8_t raw[1024];CHECK(nbytes<=sizeof raw);CHECK(!nvn_deswizzle(&a->d,1,1,a->mem,raw,pitch));
    for(size_t n=0;n<nbytes;n++){CHECK(gpu->map[n]==0x27);CHECK(raw[n]==0x27);}
    CHECK(vk_test_texture_rebuildable(a->serial)==1);
    // The next operation reads this formerly GPU-owned texture as its source,
    // exactly the failed game call chain. Other mips/layers remain canonical.
    mtl_sub_begin();p_copy_t2t pair={.src=a,.dst=d,.has_sv=1,.has_dv=1,
      .sv=copy.view,.dv=copy.view,.sr={.width=8,.height=8,.depth=1},.dr={.width=8,.height=8,.depth=1}};
    vk_texture_copy_t2t(&pair);
    gpu=vk_temp_buffer(nbytes);
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&dependency,0,NULL,0,NULL);
    vkCmdCopyImageToBuffer(V.cmd,physical[1],VK_IMAGE_LAYOUT_GENERAL,gpu->handle,1,&read);
    vk_internal_wait(vk_internal_commit());
    CHECK(!nvn_deswizzle(&d->d,1,1,d->mem,raw,pitch));
    for(size_t n=0;n<nbytes;n++){CHECK(gpu->map[n]==0x27);CHECK(raw[n]==0x27);}
    CHECK(vk_test_texture_rebuildable(d->serial)==1);
    // Nonzero compressed-block offsets into a different mip: preserve every
    // untouched block, and a CPU edit in another mip while this GPU ROI runs.
    mtl_sub_begin();pair.sr=(NVNcopyRegion){.xoffset=4,.yoffset=4,.width=4,.height=4,.depth=1};
    pair.dv.base_level=0;pair.dr=pair.sr;
    vk_texture_copy_t2t(&pair);
    size_t big_pitch=(16u+bw-1)/bw*bpb,big_rows=(16u+bh-1)/bh,big_size=big_pitch*big_rows;
    gpu=vk_temp_buffer(big_size);
    read.imageSubresource.mipLevel=0;read.imageExtent=(VkExtent3D){16,16,1};
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&dependency,0,NULL,0,NULL);
    vkCmdCopyImageToBuffer(V.cmd,physical[1],VK_IMAGE_LAYOUT_GENERAL,gpu->handle,1,&read);
    uint64_t own_serial=vk_internal_commit();
    uint8_t tail[64];memset(tail,0x38,sizeof tail);
    uint32_t tr,ty,tz,tb;uint64_t to;unsigned th,td;
    CHECK(!nvn_deswizzle_geom(&d->d,2,0,&tr,&ty,&tz,&to,&th,&td,&tb));
    dm_guest_write_prepare(d->mem+to,tr*ty);CHECK(!nvn_swizzle(&d->d,2,0,tail,tr,d->mem));
    vk_internal_wait(own_serial);
    uint8_t partial[1024];CHECK(big_size<=sizeof partial);
    CHECK(!nvn_deswizzle(&d->d,0,1,d->mem,partial,big_pitch));
    for(size_t y=0;y<big_rows;y++)for(size_t x=0;x<big_pitch;x++) {
      uint8_t expected=y>=4u/bh && y<8u/bh && x>=4u/bw*bpb && x<8u/bw*bpb?0x27:0;
      CHECK(partial[y*big_pitch+x]==expected);CHECK(gpu->map[y*big_pitch+x]==expected);
    }
    CHECK(!nvn_deswizzle(&d->d,2,0,d->mem,tail,tr));
    for(size_t n=0;n<tr*ty;n++)CHECK(tail[n]==0x38);
    read.imageSubresource.mipLevel=1;read.imageExtent=(VkExtent3D){8,8,1};
    uint8_t other[4096];size_t other_pitch=(16u+bw-1)/bw*bpb,other_rows=(16u+bh-1)/bh;
    CHECK(!nvn_deswizzle(&d->d,0,0,d->mem,other,other_pitch));
    for(size_t n=0;n<other_pitch*other_rows;n++)CHECK(other[n]==0);
    // Canonical bytes now contain GPU 0x27. A later CPU write of the original
    // zero value is a real edit and must survive the next source conversion.
    uint8_t changed[1024]={0};dm_guest_write_prepare(d->mem,d->L.size);
    CHECK(!nvn_swizzle(&d->d,1,1,changed,pitch,d->mem));
    mtl_sub_begin();vk_texture_cpu_preflight(d);VkImageLayout current;
    VkImage image=vk_texture_present_source(d,&current);gpu=vk_temp_buffer(nbytes);
    vkCmdCopyImageToBuffer(V.cmd,image,current,gpu->handle,1,&read);
    vk_internal_wait(vk_internal_commit());
    for(size_t n=0;n<nbytes;n++)CHECK(gpu->map[n]==0);
    if(!f) {
      // Source is a genuinely noncanonical GPU image, not a CPU-known proof.
      mtl_sub_begin();VkImage escaped=vk_texture_native(a);
      VkClearColorValue color={.float32={128.f/255,128.f/255,128.f/255,128.f/255}};
      VkImageSubresourceRange sub={VK_IMAGE_ASPECT_COLOR_BIT,1,1,1,1};
      vkCmdClearColorImage(V.cmd,escaped,VK_IMAGE_LAYOUT_GENERAL,&color,1,&sub);
      vk_internal_wait(vk_internal_commit());
      CHECK(!nvn_deswizzle(&a->d,1,1,a->mem,raw,pitch));CHECK(raw[0]==0x27);
      mtl_sub_begin();pair.sr=(NVNcopyRegion){.width=8,.height=8,.depth=1};pair.dr=pair.sr;pair.dv.base_level=1;
      vk_texture_copy_t2t(&pair);gpu=vk_temp_buffer(nbytes);
      vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&dependency,0,NULL,0,NULL);
      vkCmdCopyImageToBuffer(V.cmd,physical[1],VK_IMAGE_LAYOUT_GENERAL,gpu->handle,1,&read);
      vk_internal_wait(vk_internal_commit());CHECK(!nvn_deswizzle(&d->d,1,1,d->mem,raw,pitch));
      for(size_t n=0;n<nbytes;n++){CHECK(gpu->map[n]==128);CHECK(raw[n]==128);}
    }
    CALL(nvnTextureFinalize,&t[0]);CALL(nvnTextureFinalize,&t[1]);
  }
  printf("{\"test_details\":\"shared-native-copy-retile-formats\",\"native_ASTC_available\":%s}\n",V.features.textureCompressionASTC_LDR?"true":"false");
  printf("{\"test_details\":\"shared-native-copy-retile\",\"GPU_produced_B2T_source\":true,\"T2T_source_chain\":true,\"two_layers_three_mips\":true,\"same_value_CPU_edit\":true}\n");
}
static void shared_image_dirty_test(void) {
  CHECK(vk_pool_shared_mode());
  size_t allocation=(4u<<20)+65536;
  int actual_heap=getenv("DM_SHARED_GUEST_HEAP_FIXTURE")!=NULL;
  uint8_t *raw=NULL,*data=bytes;
  const char *negative=getenv("DM_SHARED_IMAGE_NEGATIVE");
  if(!actual_heap) {
    raw=mmap(NULL,allocation,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
    CHECK(raw!=MAP_FAILED);data=(void *)(((uintptr_t)raw+65535)&~(uintptr_t)65535);
    if(!negative || strcmp(negative,"provenance"))vk_test_shared_image_backing(data,4u<<20);
  }
  memset(data,0,4u<<20);
  NVNmemoryPoolBuilder b;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&b);CALL(nvnMemoryPoolBuilderSetDevice,&b,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&b,data,2u<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&b,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&b));CHECK(!nvn_dirty_tracked(data,2u<<20));
  if(getenv("DM_SHARED_IMAGE_COPY_TEST"))shared_image_copy_test(&pool,data);
  NVNtexture textures[2];NVNtextureBuilder tb;
  for(unsigned n=0;n<2;n++) {
    CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
    CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);
    CALL(nvnTextureBuilderSetWidth,&tb,16);CALL(nvnTextureBuilderSetHeight,&tb,16);
    CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);
    NVNpackagedTextureLayout layout={{0}};CALL(nvnTextureBuilderSetPackagedTextureLayout,&tb,&layout);
    CALL(nvnTextureBuilderSetStorage,&tb,&pool,0);CHECK(CALL(nvnTextureInitialize,&textures[n],&tb));
    CHECK(((tex_t *)&textures[n])->L.size<=4096);
  }
  uint8_t red[]={255,0,0,255},green[]={0,255,0,255},blue[]={0,0,255,255};
  // A packaged render target may be first defined entirely by the GPU. No CPU
  // upload ever ran, but later sampling must retain that definition and track
  // subsequent source writes conservatively (including same-value stores).
  NVNtexture first_clear;
  CALL(nvnTextureBuilderSetStorage,&tb,&pool,65536);
  CHECK(CALL(nvnTextureInitialize,&first_clear,&tb));
  CHECK(!nvn_dirty_tracked(data+65536,4096));
  dirty_range_fill(data+65536,4096,red);
  uint64_t cu,cr,cu2,cr2;mtl_pkg_stats(&cu,&cr);
  mtl_sub_begin();
  p_set_rt target={.n=1,.color={ (const tex_t *)&first_clear }};
  p_clear_color clear={.index=0,.mask=15,.rgba={0,1,0,1}};
  vk_target_clear_color(&target,NULL,&clear);
  vk_internal_wait(vk_internal_commit());
  shared_image_pixels(&first_clear,green);
  CHECK(nvn_dirty_tracked(data+65536,((tex_t *)&first_clear)->L.size));
  CHECK(vk_test_texture_rebuildable(((tex_t *)&first_clear)->serial)==0);
  mtl_pkg_stats(&cu2,&cr2);CHECK(cu2==cu && cr2==cr);
  // An unrelated tracked page changes the global epoch, not this image's
  // source generation. The original negative proof must still preserve GPU.
  dirty_range_fill(data,4096,blue);
  shared_image_pixels(&textures[0],blue); // real separate image establishes scope
  data[0]=blue[0]; // fault even for an unchanged byte; other-page publication
  shared_image_pixels(&first_clear,green);
  if(negative && !strcmp(negative,"first-clear-cpu")) {
    data[65536]=255; // same value as before GPU green; must NOT be swallowed
    shared_image_pixels(&first_clear,green);
    die("Same-value CPU store after first GPU clear was incorrectly ignored");
  }
  if(negative && !strcmp(negative,"first-clear-neighbor")) {
    CHECK(((tex_t *)&first_clear)->L.size<4096);
    data[65536+((tex_t *)&first_clear)->L.size]=0x42; // same OS page, outside image
    shared_image_pixels(&first_clear,green);
    die("Ambiguous same-page neighbor write was incorrectly accepted");
  }
  if(negative && !strcmp(negative,"first-clear-change")) {
    data[65536]=127; // real CPU value change; partial retile is still unsupported
    shared_image_pixels(&first_clear,green);
    die("CPU change after first GPU clear was incorrectly accepted");
  }
  // Both ordinary outer operations must prepare a never-imported image Pool
  // even though successful shared startup deliberately makes defer=0.
  CHECK(!V.options.pool_defer_publication);
  uint8_t control[3][0x880];memset(control[0],0,sizeof control[0]);
  memset(control[1],7,sizeof control[1]);memset(control[2],8,sizeof control[2]);
  const float vertices[]={-1,-1,3,-1,-1,3};memcpy(data+(512u<<10),vertices,sizeof vertices);
  for(unsigned graphics=0;graphics<2;graphics++) {
    NVNmemoryPoolBuilder fresh_builder;NVNmemoryPool fresh_pool;
    CALL(nvnMemoryPoolBuilderSetDefaults,&fresh_builder);CALL(nvnMemoryPoolBuilderSetDevice,&fresh_builder,&device);
    uint8_t *source=data+((2+graphics)<<20);dirty_range_fill(source,4096,red);
    CALL(nvnMemoryPoolBuilderSetStorage,&fresh_builder,source,1u<<20);
    CALL(nvnMemoryPoolBuilderSetFlags,&fresh_builder,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
    CHECK(CALL(nvnMemoryPoolInitialize,&fresh_pool,&fresh_builder));
    NVNtexture image;CALL(nvnTextureBuilderSetStorage,&tb,&fresh_pool,0);
    CALL(nvnTextureBuilderSetFlags,&tb,NVN_TEXTURE_FLAGS_IMAGE);CHECK(CALL(nvnTextureInitialize,&image,&tb));
    int ready,shadow;uint32_t flags;extern int vk_test_fixed_shared_pool(uint64_t,int *,int *,uint32_t *);
    CHECK(vk_test_fixed_shared_pool((uintptr_t)source,&ready,&shadow,&flags));CHECK(!ready && !shadow);
    mtl_set_tex_pool_n(32);mtl_reg_tex(17,(const tex_t *)&image,NULL);
    NVNprogram program;NVNshaderData stages[]={{.control=control[0]},{.control=control[graphics?2:1]}};
    CHECK(CALL(nvnProgramInitialize,&program,&device));
    CHECK(CALL(nvnProgramSetShaders,&program,graphics?2:1,graphics?stages:stages+1));
    S=(vk_state){.program=(const prog_t *)&program,.depth_range={0,1},.chmask={~0u}};
    S.img[graphics?1:5][0]=17;S.targets=(p_set_rt){.n=1,.color={(const tex_t *)&first_clear}};
    NVNpolygonState ps;CALL(nvnPolygonStateSetDefaults,&ps);CALL(nvnPolygonStateSetCullFace,&ps,NVN_FACE_NONE);S.poly=*(const poly_st *)&ps;
    NVNcolorState cs;CALL(nvnColorStateSetDefaults,&cs);S.color=*(const color_st *)&cs;
    NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);S.ds=*(const ds_st *)&ds;
    NVNvertexAttribState attr;CALL(nvnVertexAttribStateSetDefaults,&attr);CALL(nvnVertexAttribStateSetFormat,&attr,NVN_FORMAT_RG32F,0);S.attr[0]=*(const va_st *)&attr;
    NVNvertexStreamState stream;CALL(nvnVertexStreamStateSetDefaults,&stream);CALL(nvnVertexStreamStateSetStride,&stream,8);S.stream[0]=*(const vs_st *)&stream;
    S.vertex[0]=(NVNbufferRange){(uintptr_t)data+(512u<<10),sizeof vertices};
    S.has_viewport=S.has_scissor=1;S.viewport[0][2]=S.viewport[0][3]=16;S.scissor=(p_scissor){0,0,16,16};
    vk_pipeline_state_changed();mtl_sub_begin();
    if(graphics) {p_draw draw={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};vk_draw(&draw,NULL);}
    else {p_dispatch dispatch={4,4,1};vk_dispatch(&dispatch);}
    vk_internal_wait(vk_internal_commit());
    CHECK(vk_test_fixed_shared_pool((uintptr_t)source,&ready,&shadow,&flags));CHECK(ready && !shadow);
    shared_image_pixels(&image,graphics?(const uint8_t[]){0,64,255,255}:(const uint8_t[]){255,0,128,255});
    S=(vk_state){0};CALL(nvnProgramFinalize,&program);CALL(nvnTextureFinalize,&image);CALL(nvnMemoryPoolFinalize,&fresh_pool);
  }
  CALL(nvnTextureFinalize,&first_clear);
  dirty_range_fill(data,4096,red);
  shared_image_pixels(&textures[0],red);shared_image_pixels(&textures[1],red);
  CHECK(nvn_dirty_tracked(data,4096));// The first-clear scope remains alive until original image retirement.
  uint64_t uploads,reuploads,u2,r2;mtl_pkg_stats(&uploads,&reuploads);
  for(unsigned n=0;n<4;n++)shared_image_pixels(&textures[n&1],red);
  mtl_pkg_stats(&u2,&r2);CHECK(u2==uploads && r2==reuploads); // no everydraw reupload
  dirty_range_fill(data,4096,green); // real original dirty fault
  shared_image_pixels(&textures[0],green);shared_image_pixels(&textures[1],green);
  // Known GPU writes to packaged bytes change CPU-visible source only through
  // original WB completion, and invalidate both converted images' dirty proof.
  mtl_sub_begin();vk_buffer *buffer;uint64_t offset;
  CHECK(vk_pool_address_overwrite((uintptr_t)data,4096,(void **)&buffer,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
  vkCmdFillBuffer(V.cmd,buffer->handle,offset,4096,0xffff0000u);
  vk_buffer_transfer_written_range((uintptr_t)data,4096);
  vk_buffer_writeback_stage((uintptr_t)data,4096,VK_PIPELINE_STAGE_TRANSFER_BIT);
  vk_texture_cpu_preflight((const tex_t *)&textures[0]); // original ordered CPU observation commits/waits
  vk_internal_wait(vk_internal_commit());
  shared_image_pixels(&textures[0],blue);shared_image_pixels(&textures[1],blue);
  // Attachment preparation promotes storage with a real complete GPU copy,
  // but no draw follows. The source is still fully CPU rebuildable; its own
  // temporary transfer-write record must not leave a false GPU alias behind.
  mtl_sub_begin();vk_texture_cpu_preflight((const tex_t *)&textures[0]);
  vk_texture_prepare_attachment((const tex_t *)&textures[0],NULL);
  vk_internal_wait(vk_internal_commit());
  CHECK(vk_test_texture_rebuildable(((const tex_t *)&textures[0])->serial)==1);
  CHECK(!vk_texture_shared_linear_conflict((uintptr_t)data,4096));
  dirty_range_fill(data,4096,red);shared_image_pixels(&textures[0],red);shared_image_pixels(&textures[1],red);
  dirty_range_fill(data,4096,blue);shared_image_pixels(&textures[0],blue);shared_image_pixels(&textures[1],blue);
  if(negative && !strcmp(negative,"cpu-image-alias")) {
    mtl_sub_begin();vk_texture_native((const tex_t *)&textures[0]);vk_internal_wait(vk_internal_commit());
    dirty_range_fill(data,4096,red);shared_image_pixels(&textures[0],red);
    die("CPU dirty alias of GPU-owned image was incorrectly accepted");
  }
  if(negative && !strcmp(negative,"image-alias")) {
    mtl_sub_begin();vk_texture_native((const tex_t *)&textures[0]);
    CHECK(vk_pool_address_stage((uintptr_t)data,4,(void **)&buffer,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
    die("GPU-owned image alias was incorrectly accepted");
  }
  if(negative && !strcmp(negative,"cross-identity-upload")) {
    mtl_sub_begin();VkImage native=vk_texture_native((const tex_t *)&textures[0]);
    VkClearColorValue value={.float32={0,1,0,1}};
    VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCmdClearColorImage(V.cmd,native,VK_IMAGE_LAYOUT_GENERAL,&value,1,&range);
    vk_internal_wait(vk_internal_commit());CHECK(!memcmp(data,blue,4));
    NVNtexture fresh_alias;CALL(nvnTextureBuilderSetStorage,&tb,&pool,0);
    CHECK(CALL(nvnTextureInitialize,&fresh_alias,&tb));
    shared_image_pixels(&fresh_alias,green); // guest is stale blue: never upload it
    die("New texture identity uploaded stale CPU bytes from GPU-owned image span");
  }
  if(getenv("DM_SHARED_IMAGE_CPU_READ_TEST") || getenv("DM_SHARED_IMAGE_READBACK_TEST") ||
      (negative && !strcmp(negative,"required-cpu-read-alias"))) {
    // Real GPU content differs from its old packaged backing. A CPU proof
    // must not return those stale blue bytes as evidence about this image.
    mtl_sub_begin();VkImage native=vk_texture_native((const tex_t *)&textures[0]);
    VkClearColorValue value={.float32={0,1,0,1}};
    VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCmdClearColorImage(V.cmd,native,VK_IMAGE_LAYOUT_GENERAL,&value,1,&range);
    vk_internal_wait(vk_internal_commit());
    uint8_t observed[4]={0x71,0x72,0x73,0x74},untouched[4];
    memcpy(untouched,observed,4);CHECK(!memcmp(data,blue,4));
    mtl_sub_begin();
    CHECK(vk_pool_read((uintptr_t)data,4,NULL)==-1);
    CHECK(vk_pool_try_read((uintptr_t)data,4,NULL)==-1);
    CHECK(vk_pool_try_read((uintptr_t)data,4,observed)==0);
    CHECK(!memcmp(observed,untouched,4));
    for(unsigned reference=0;reference<2;reference++) {
      vk_cover_wait_reference(reference);
      CHECK(!vk_cover_read((uintptr_t)data,4,observed));
      CHECK(!memcmp(observed,untouched,4));
    }
    vk_cover_wait_reference(0);
    // A different linear range in the SAME Pool remains readable as before.
    memcpy(data+131072,red,4);
    CHECK(vk_pool_try_read((uintptr_t)data+131072,4,observed)==1);
    CHECK(!memcmp(observed,red,4));
    CHECK(vk_pool_read((uintptr_t)data+131072,4,observed)==0);
    CHECK(!memcmp(observed,red,4));
    if(negative && !strcmp(negative,"required-cpu-read-alias")) {
      (void)vk_pool_read((uintptr_t)data,4,observed);
      die("Required CPU read accepted stale GPU-owned image backing");
    }
    vk_internal_wait(vk_internal_commit());
    shared_image_pixels(&textures[0],green); // original GPU image path still works
    printf("{\"test_details\":\"shared-image-CPU-read-guard\",\"optional_stale_proof_declined\":true,\"same_pool_nonalias_linear_read\":true,\"GPU_image_preserved\":true}\n");
    if(getenv("DM_SHARED_IMAGE_READBACK_TEST")) {
      CHECK(mtl_pool_shared_mode());CHECK(!mtl_pool_defer_publication_mode());
      uint8_t *tight=data+196608,*strided=data+262144;
      memset(tight,0xcd,1024);memset(strided,0xcd,1280);
      NVNcopyRegion region={.width=16,.height=16,.depth=1};
      atomic_store(&shared_readback_calls,0);atomic_store(&shared_readback_wrong_owner,0);
      nvn_test_readback_observer(shared_readback_owner);
      CALL(nvnTextureReadTexels,&textures[0],NULL,&region,tight);
      CALL(nvnTextureReadTexelsStrided,&textures[0],NULL,&region,strided,80,1280);
      nvn_test_readback_observer(NULL); // both original synchronous jobs returned
      CHECK(atomic_load(&shared_readback_calls)==2);
      CHECK(!atomic_load(&shared_readback_wrong_owner));
      for(unsigned y=0;y<16;y++) {
        for(unsigned x=0;x<16;x++) {
          CHECK(!memcmp(tight+64*y+4*x,green,4));
          CHECK(!memcmp(strided+80*y+4*x,green,4));
        }
        for(unsigned p=64;p<80;p++)CHECK(strided[80*y+p]==0xcd);
      }
      // A subsequent ordinary GPU Buffer consumer sees the CPU publication.
      mtl_sub_begin();vk_buffer *source;uint64_t start;
      CHECK(vk_pool_address_stage((uintptr_t)tight,1024,(void **)&source,&start,VK_PIPELINE_STAGE_TRANSFER_BIT));
      vk_buffer_use use={(uintptr_t)tight,1024,0};vk_transfer_buffer_dependencies(&use,1);
      vk_buffer *result=vk_temp_buffer(1024);VkBufferCopy copy={start,0,1024};
      vkCmdCopyBuffer(V.cmd,source->handle,result->handle,1,&copy);
      vk_buffer_transfer_written();vk_internal_wait(vk_internal_commit());
      for(unsigned n=0;n<256;n++)CHECK(!memcmp(result->map+4*n,green,4));
      printf("{\"test_details\":\"shared-image-NVN-readback-owner\",\"dedicated_owner_jobs\":2,\"tight_and_strided_exact\":true,\"synchronous_CPU_publication\":true,\"later_GPU_consumer_exact\":true}\n");
    }
  }
  int ready,shadow;uint32_t flags;extern int vk_test_fixed_shared_pool(uint64_t,int *,int *,uint32_t *);
  CHECK(vk_test_fixed_shared_pool((uintptr_t)data,&ready,&shadow,&flags));CHECK(ready && !shadow);
  CALL(nvnTextureFinalize,&textures[0]);CALL(nvnTextureFinalize,&textures[1]);CALL(nvnMemoryPoolFinalize,&pool);
  CHECK(!nvn_dirty_tracked(data,4096));memset(data,0x81,4096); // scopes restored RW, original mapping still valid
  vk_test_shared_image_backing(NULL,0);if(raw)CHECK(!munmap(raw,allocation));
  CHECK(!vk_validation_error_count());
  printf("{\"test_configuration\":\"shared-image-provenance\",\"actual_early_heap\":%s,\"finite_test_certificate\":%s}\n",actual_heap?"true":"false",actual_heap?"false":"true");
  printf("{\"test\":\"shared-image-only-dirty\",\"checks\":%u,\"result\":\"passed\",\"actual_memory_flags\":%u,\"game_modules_executed\":false}\n",checks,flags);
}
