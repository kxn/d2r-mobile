// The original NVN image upload must retain its captured source generation,
// even if a real paired kernel write starts immediately after CPU phase end.
#include <fcntl.h>
static PFN_vkCmdCopyBufferToImage observe_copy_original;
static uint8_t *observe_kernel_destination;
static size_t observe_kernel_bytes;
static int observe_kernel_fd;
static void VKAPI_PTR observe_copy_after_cpu(VkCommandBuffer command,VkBuffer source,VkImage image,
    VkImageLayout layout,uint32_t count,const VkBufferImageCopy *copies) {
  if(observe_kernel_destination) {
    uint8_t *destination=observe_kernel_destination;observe_kernel_destination=NULL;
    // This runs at the FIRST actual GPU command, not during deswizzle/decode.
    // If the CPU-read gate were still held it would reject owner self-wait.
    dm_guest_write_lease lease=dm_guest_write_begin(destination,observe_kernel_bytes);CHECK(lease.cookie);
    CHECK(pread(observe_kernel_fd,destination,observe_kernel_bytes,0)==(ssize_t)observe_kernel_bytes);
    dm_guest_write_end(&lease);
  }
  observe_copy_original(command,source,image,layout,count,copies);
}
static void shared_image_cpu_observation_test(void) {
  int legacy=vk_legacy_astc_copy_mode() && !vk_pool_shared_active();
  int actual_heap=!legacy && getenv("DM_SHARED_GUEST_HEAP_FIXTURE")!=NULL;
  size_t size=(4u<<20)+65536;uint8_t *raw=NULL,*data=bytes;
  if(actual_heap) {
    extern void *dm_guest_shared_heap_import_pointer(uintptr_t,size_t) __attribute__((weak));
    CHECK(dm_guest_shared_heap_import_pointer);
    CHECK(dm_guest_shared_heap_import_pointer((uintptr_t)data,65536)!=data);
    CHECK(dm_guest_shared_heap_import_pointer((uintptr_t)data,65536)!=NULL);
  } else {
    raw=mmap(NULL,size,PROT_READ|PROT_WRITE,(legacy?MAP_PRIVATE:MAP_SHARED)|MAP_ANONYMOUS,-1,0);
    CHECK(raw!=MAP_FAILED);data=(void *)(((uintptr_t)raw+65535)&~(uintptr_t)65535);
    if(!legacy)vk_test_shared_image_backing(data,4u<<20);
  }
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,data,65536);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtextureBuilder b;NVNtexture texture;CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
  CALL(nvnTextureBuilderSetWidth,&b,16);CALL(nvnTextureBuilderSetHeight,&b,16);CALL(nvnTextureBuilderSetFormat,&b,NVN_FORMAT_RGBA8);
  NVNpackagedTextureLayout packaged={{0}};CALL(nvnTextureBuilderSetPackagedTextureLayout,&b,&packaged);
  CALL(nvnTextureBuilderSetStorage,&b,&pool,0);CHECK(CALL(nvnTextureInitialize,&texture,&b));
  tex_t *t=(tex_t *)&texture;CHECK(t->L.size<=4096 && !(t->L.size&3));
  const uint8_t red[4]={255,0,0,255},green[4]={0,255,0,255};
  uint8_t next[4096];for(size_t n=0;n<t->L.size;n+=4){memcpy(data+n,red,4);memcpy(next+n,green,4);}
  char name[4096];const char *dir=getenv("DM_CAPTURE_TEST_DIR");if(!dir)dir="/tmp";
  CHECK(snprintf(name,sizeof name,"%s/image-observe-XXXXXX",dir)>0);
  observe_kernel_fd=mkstemp(name);CHECK(observe_kernel_fd>=0);CHECK(!unlink(name));
  CHECK(write(observe_kernel_fd,next,t->L.size)==(ssize_t)t->L.size);
  observe_kernel_bytes=t->L.size;observe_kernel_destination=data;
  observe_copy_original=vkCmdCopyBufferToImage;vkCmdCopyBufferToImage=observe_copy_after_cpu;
  shared_image_pixels(&texture,red); // real original GPU command consumes OLD staging
  vkCmdCopyBufferToImage=observe_copy_original;CHECK(!observe_kernel_destination);
  int ready=0,shadow=0;uint32_t properties=0;
  if(!legacy) {
    CHECK(vk_test_fixed_shared_pool((uintptr_t)data,&ready,&shadow,&properties));
    CHECK(!ready && !shadow); // CPU conversion and VkImage use need no guest VkBuffer
  }
  CHECK(!memcmp(data,next,t->L.size)); // a newer source is already present
  CHECK(!close(observe_kernel_fd));
  shared_image_pixels(&texture,green); // old capture must not swallow the FS write
  uint64_t a,before,c,d;mtl_pkg_stats(&a,&before);
  shared_image_pixels(&texture,green);mtl_pkg_stats(&c,&d);CHECK(a==c && before==d);
  if(!legacy) {
    CHECK(vk_test_fixed_shared_pool((uintptr_t)data,&ready,&shadow,&properties));
    CHECK(!ready && !shadow);
    // The first actual linear GPU consumer must still import the same bytes,
    // without invalidating the existing image's CPU-write/snapshot proof.
    mtl_sub_begin();vk_buffer *source;uint64_t offset;
    CHECK(vk_pool_address_stage((uintptr_t)data,t->L.size,(void **)&source,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
    vk_buffer_use use={(uintptr_t)data,t->L.size,0};vk_transfer_buffer_dependencies(&use,1);
    vk_buffer *result=vk_temp_buffer(t->L.size);VkBufferCopy copy={offset,0,t->L.size};
    vkCmdCopyBuffer(V.cmd,source->handle,result->handle,1,&copy);
    vk_buffer_transfer_written();vk_internal_wait(vk_internal_commit());
    CHECK(!memcmp(result->map,next,t->L.size));
    CHECK(vk_test_fixed_shared_pool((uintptr_t)data,&ready,&shadow,&properties));
    CHECK(ready && !shadow);shared_image_pixels(&texture,green);
  }
  CALL(nvnTextureFinalize,&texture);
  // Exercise the same protected CPU phase's BC fallback without lying about
  // device capabilities: the existing owned force-decode selector creates a
  // real RGBA physical image, through the original nvn_bc_decode implementation.
  CALL(nvnTextureBuilderSetFormat,&b,NVN_FORMAT_RGB_DXT1);CALL(nvnTextureBuilderSetStorage,&b,&pool,4096);
  CHECK(CALL(nvnTextureInitialize,&texture,&b));t=(tex_t *)&texture;
  vk_texture_test_force_bc_decode(1);
  uint8_t blocks[128];
  for(unsigned generation=0;generation<2;generation++) {
    const uint8_t block[8]={generation?0xe0:0,generation?0x07:0xf8,0,0,0,0,0,0};
    for(unsigned n=0;n<sizeof blocks;n+=8)memcpy(blocks+n,block,8);
    dm_guest_write_prepare(t->mem,t->L.size);CHECK(!nvn_swizzle(&t->d,0,0,blocks,32,t->mem));
    shared_image_pixels(&texture,generation?green:red);
    CHECK(vk_texture_test_bc_decoded(t)==1);
  }
  vk_texture_test_force_bc_decode(0);CALL(nvnTextureFinalize,&texture);CALL(nvnMemoryPoolFinalize,&pool);
  if(!legacy && !actual_heap)vk_test_shared_image_backing(NULL,0);
  if(raw) {CHECK(!munmap(raw,size));}
  CHECK(!vk_validation_error_count());
  printf("{\"test_details\":\"%s\",\"paired_pread_after_CPU_capture\":true,\"first_GPU_snapshot_old\":true,\"next_GPU_upload_new\":true,\"clean_sample_no_upload\":true,\"original_BC_CPU_decode_protected\":true}\n",legacy?"legacy-image-CPU-observation":"shared-image-CPU-observation");
}

// Real outer draw/dispatch admission with defer=0 and private legacy storage.
// No explicit texture preflight is issued between the queued CPU effect and
// the operation. The first copy callback proves CPU publication preceded the
// actual CPU conversion; GPU image bytes prove the original shader executed.
static PFN_vkCmdCopyBufferToImage legacy_outer_copy_original;
static volatile uint32_t *legacy_outer_effect_word;
static uint32_t legacy_outer_effect_value;
static unsigned legacy_outer_copies;
static void VKAPI_PTR legacy_outer_upload_copy(VkCommandBuffer command,VkBuffer source,VkImage image,
    VkImageLayout layout,uint32_t count,const VkBufferImageCopy *copies) {
  if(legacy_outer_effect_word) {CHECK(*legacy_outer_effect_word==legacy_outer_effect_value);legacy_outer_copies++;}
  legacy_outer_copy_original(command,source,image,layout,count,copies);
}
static void legacy_image_outer_preflight_test(void) {
  CHECK(vk_legacy_astc_copy_mode() && !vk_pool_shared_active() && !V.options.pool_defer_publication);
  vk_state saved=S;uint8_t *data=mmap(NULL,65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
  CHECK(data!=MAP_FAILED);memset(data,0,65536);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,data,65536);CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  const float vertices[]={-1,-1,3,-1,-1,3};memcpy(data+49152,vertices,sizeof vertices);
  NVNtextureBuilder tb;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetWidth,&tb,16);CALL(nvnTextureBuilderSetHeight,&tb,16);CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);
  NVNtexture target;CALL(nvnTextureBuilderSetStorage,&tb,&pool,32768);CHECK(CALL(nvnTextureInitialize,&target,&tb));
  const uint8_t red[4]={255,0,0,255};uint8_t control[3][0x880];memset(control[0],0,sizeof control[0]);memset(control[1],7,sizeof control[1]);memset(control[2],8,sizeof control[2]);
  unsigned start_copies=legacy_outer_copies;
  for(unsigned graphics=0;graphics<2;graphics++) {
    uint8_t *source=data+16384*graphics;dirty_range_fill(source,4096,red);
    NVNtexture texture;CALL(nvnTextureBuilderSetFlags,&tb,NVN_TEXTURE_FLAGS_IMAGE);
    NVNpackagedTextureLayout packaged={{0}};CALL(nvnTextureBuilderSetPackagedTextureLayout,&tb,&packaged);
    CALL(nvnTextureBuilderSetStorage,&tb,&pool,source-data);CHECK(CALL(nvnTextureInitialize,&texture,&tb));
    CHECK(((const tex_t *)&texture)->L.size<=4096);
    mtl_set_tex_pool_n(32);mtl_reg_tex(17,(const tex_t *)&texture,NULL);
    NVNprogram program;NVNshaderData stages[]={{.control=control[0]},{.control=control[graphics?2:1]}};
    CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,graphics?2:1,graphics?stages:stages+1));
    S=(vk_state){.program=(const prog_t *)&program,.depth_range={0,1},.chmask={~0u}};
    S.img[graphics?1:5][0]=17;S.targets=(p_set_rt){.n=1,.color={(const tex_t *)&target}};
    NVNpolygonState ps;CALL(nvnPolygonStateSetDefaults,&ps);CALL(nvnPolygonStateSetCullFace,&ps,NVN_FACE_NONE);S.poly=*(const poly_st *)&ps;
    NVNcolorState cs;CALL(nvnColorStateSetDefaults,&cs);S.color=*(const color_st *)&cs;
    NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);S.ds=*(const ds_st *)&ds;
    NVNvertexAttribState attr;CALL(nvnVertexAttribStateSetDefaults,&attr);CALL(nvnVertexAttribStateSetFormat,&attr,NVN_FORMAT_RG32F,0);S.attr[0]=*(const va_st *)&attr;
    NVNvertexStreamState stream;CALL(nvnVertexStreamStateSetDefaults,&stream);CALL(nvnVertexStreamStateSetStride,&stream,8);S.stream[0]=*(const vs_st *)&stream;
    S.vertex[0]=(NVNbufferRange){(uintptr_t)data+49152,sizeof vertices};S.has_viewport=S.has_scissor=1;
    S.viewport[0][2]=S.viewport[0][3]=16;S.scissor=(p_scissor){0,0,16,16};vk_pipeline_state_changed();
    vk_shader *shader_list[6]={0};vk_program_shaders(S.program,shader_list);
    // Registration identity is not a prediction: replace the SAME live id
    // through non-packaged/null/empty/out-of-limit descriptors, then restore
    // its packaged source. Only the current eligible registration may publish
    // this queued CPU effect. The actual draw/dispatch below must still upload
    // and execute against the restored descriptor.
    mtl_sub_begin();volatile uint32_t *filtered=(volatile uint32_t *)(source+4092);
    dm_guest_write_prepare((void *)filtered,4);*filtered=0;
    mtl_sub_event(filtered,NVN_EVENT_SIGNAL_MODE_WRITE,0x1379acdeu);
    mtl_reg_tex(17,(const tex_t *)&target,NULL);vk_shader_cpu_sources(shader_list,&S,!graphics);
    CHECK(*filtered==0 && vk_pool_pending_writes((uintptr_t)filtered,4));
    tex_t absent=*(const tex_t *)&texture;absent.mem=NULL;
    mtl_reg_tex(17,&absent,NULL);vk_shader_cpu_sources(shader_list,&S,!graphics);
    CHECK(*filtered==0 && vk_pool_pending_writes((uintptr_t)filtered,4));
    absent=*(const tex_t *)&texture;absent.L.size=0;
    mtl_reg_tex(17,&absent,NULL);vk_shader_cpu_sources(shader_list,&S,!graphics);
    CHECK(*filtered==0 && vk_pool_pending_writes((uintptr_t)filtered,4));
    mtl_reg_tex(17,(const tex_t *)&texture,NULL);mtl_set_tex_pool_n(17);
    vk_shader_cpu_sources(shader_list,&S,!graphics);
    CHECK(*filtered==0 && vk_pool_pending_writes((uintptr_t)filtered,4));
    mtl_set_tex_pool_n(32);vk_shader_cpu_sources(shader_list,&S,!graphics);
    CHECK(*filtered==0x1379acdeu && !vk_pool_pending_writes((uintptr_t)filtered,4));
    vk_internal_wait(vk_internal_commit());
    dirty_range_fill(source,4096,red);
    CHECK(!vk_shader_cpu_sources_ready(shader_list,&S,!graphics));
    if(graphics) {p_draw draw={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};CHECK(!vk_ghost_cpu_draw_ready(shader_list,&S,&draw,NULL));}
    for(unsigned clean=0;clean<2;clean++) {
      // Before the second operation the image really is initialized/clean;
      // pending CPU effects have no dirty generation until completion.
      if(clean) {dirty_range_fill(source,4096,red);shared_image_pixels(&texture,red);}
      mtl_sub_begin();legacy_outer_effect_word=(volatile uint32_t *)(source+(clean?0:4092));
      legacy_outer_effect_value=0x12345678u+graphics*4+clean;
      mtl_sub_event(legacy_outer_effect_word,NVN_EVENT_SIGNAL_MODE_WRITE,legacy_outer_effect_value);
      CHECK(vk_pool_pending_writes((uintptr_t)source,4096));CHECK(*legacy_outer_effect_word!=legacy_outer_effect_value);
      unsigned before=legacy_outer_copies;legacy_outer_copy_original=vkCmdCopyBufferToImage;vkCmdCopyBufferToImage=legacy_outer_upload_copy;
      if(graphics) {p_draw draw={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};vk_draw(&draw,NULL);}
      else {p_dispatch dispatch={4,4,1};vk_dispatch(&dispatch);}
      vkCmdCopyBufferToImage=legacy_outer_copy_original;CHECK(legacy_outer_copies==before+1);
      CHECK(*legacy_outer_effect_word==legacy_outer_effect_value);legacy_outer_effect_word=NULL;
      vk_internal_wait(vk_internal_commit());
      shared_image_pixels(&texture,graphics?(const uint8_t[]){0,64,255,255}:(const uint8_t[]){255,0,128,255});
    }
    // A disjoint host page's CPU effect remains queued. It must not cause
    // another source upload or be synchronously completed by this operation.
    mtl_sub_begin();volatile uint32_t *other=(volatile uint32_t *)(source+8192);*other=0;
    mtl_sub_event(other,NVN_EVENT_SIGNAL_MODE_WRITE,0x98765432u);unsigned before=legacy_outer_copies;
    if(graphics) {p_draw draw={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};vk_draw(&draw,NULL);}
    else {p_dispatch dispatch={4,4,1};vk_dispatch(&dispatch);}
    CHECK(legacy_outer_copies==before && vk_pool_pending_writes((uintptr_t)other,4));CHECK(*other==0);
    vk_internal_wait(vk_internal_commit());CHECK(*other==0x98765432u);
    S=(vk_state){0};CALL(nvnProgramFinalize,&program);CALL(nvnTextureFinalize,&texture);
  }
  CALL(nvnTextureFinalize,&target);CALL(nvnMemoryPoolFinalize,&pool);CHECK(!munmap(data,65536));S=saved;vk_pipeline_state_changed();
  CHECK(legacy_outer_copies==start_copies+4 && !vk_validation_error_count());
  printf("{\"test_details\":\"legacy-image-outer-preflight\",\"defer_disabled\":true,\"fresh_draw_and_dispatch\":true,"
         "\"initialized_clean_pending_CPU_effect\":true,\"hostpage_neighbor_effect\":true,\"disjoint_effect_not_waited\":true,"
         "\"optional_packaged_ghost_declined\":true,\"real_GPU_pixels\":true,\"result\":\"passed\"}\n");
  printf("{\"test_details\":\"legacy-CPU-source-prefilter\",\"current_registry_registration\":true,"
         "\"nonpackaged_null_empty_not_waited\":true,\"descriptor_limit_respected\":true,"
         "\"packaged_effect_still_published\":true,\"restored_draw_dispatch_GPU_pixels\":true,\"result\":\"passed\"}\n");
}
