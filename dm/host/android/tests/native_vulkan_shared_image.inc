// Finite owned MAP_SHARED backing; never identifies arbitrary guest addresses.
#include <sys/mman.h>
extern void vk_test_shared_image_backing(const void *,size_t);
static void shared_image_pixels(NVNtexture *t,const uint8_t rgba[4]) {
  mtl_sub_begin();VkImageLayout layout;
  VkImage image=vk_texture_present_source((const tex_t *)t,&layout);
  vk_buffer *result=vk_temp_buffer(16*16*4);
  VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={16,16,1}};
  vkCmdCopyImageToBuffer(V.cmd,image,layout,result->handle,1,&copy);
  vk_internal_wait(vk_internal_commit());
  for(unsigned n=0;n<256;n++)CHECK(!memcmp(result->map+4*n,rgba,4));
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
  CALL(nvnMemoryPoolBuilderSetStorage,&b,data,4u<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&b,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&b));CHECK(!nvn_dirty_tracked(data,4u<<20));
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
  dirty_range_fill(data,4096,red);
  shared_image_pixels(&textures[0],red);shared_image_pixels(&textures[1],red);
  CHECK(nvn_dirty_tracked(data,4096));CHECK(!nvn_dirty_tracked(data+65536,4096));
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
  if(getenv("DM_SHARED_IMAGE_CPU_READ_TEST") ||
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
    CHECK(!vk_cover_read((uintptr_t)data,4,observed));
    CHECK(!memcmp(observed,untouched,4));
    // A different linear range in the SAME Pool remains readable as before.
    memcpy(data+65536,red,4);
    CHECK(vk_pool_try_read((uintptr_t)data+65536,4,observed)==1);
    CHECK(!memcmp(observed,red,4));
    CHECK(vk_pool_read((uintptr_t)data+65536,4,observed)==0);
    CHECK(!memcmp(observed,red,4));
    if(negative && !strcmp(negative,"required-cpu-read-alias")) {
      (void)vk_pool_read((uintptr_t)data,4,observed);
      die("Required CPU read accepted stale GPU-owned image backing");
    }
    vk_internal_wait(vk_internal_commit());
    shared_image_pixels(&textures[0],green); // original GPU image path still works
    printf("{\"test_details\":\"shared-image-CPU-read-guard\",\"optional_stale_proof_declined\":true,\"same_pool_nonalias_linear_read\":true,\"GPU_image_preserved\":true}\n");
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
