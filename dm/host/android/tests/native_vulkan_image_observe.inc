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
  size_t size=(4u<<20)+65536;uint8_t *raw=mmap(NULL,size,PROT_READ|PROT_WRITE,(legacy?MAP_PRIVATE:MAP_SHARED)|MAP_ANONYMOUS,-1,0);
  CHECK(raw!=MAP_FAILED);uint8_t *data=(void *)(((uintptr_t)raw+65535)&~(uintptr_t)65535);
  if(!legacy)vk_test_shared_image_backing(data,4u<<20);
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
  CHECK(!memcmp(data,next,t->L.size)); // a newer source is already present
  CHECK(!close(observe_kernel_fd));
  shared_image_pixels(&texture,green); // old capture must not swallow the FS write
  uint64_t a,before,c,d;mtl_pkg_stats(&a,&before);
  shared_image_pixels(&texture,green);mtl_pkg_stats(&c,&d);CHECK(a==c && before==d);
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
  if(!legacy)vk_test_shared_image_backing(NULL,0);
  CHECK(!munmap(raw,size));CHECK(!vk_validation_error_count());
  printf("{\"test_details\":\"%s\",\"paired_pread_after_CPU_capture\":true,\"first_GPU_snapshot_old\":true,\"next_GPU_upload_new\":true,\"clean_sample_no_upload\":true,\"original_BC_CPU_decode_protected\":true}\n",legacy?"legacy-image-CPU-observation":"shared-image-CPU-observation");
}
