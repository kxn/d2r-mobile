#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
// Actual original upload/copy/readback, with fixture-only API observations.
extern void vk_texture_test_astc_copy_upload(const tex_t *);
#ifdef DM_VULKAN_SHARED_POOLS
extern void vk_test_shared_image_backing(const void *,size_t);
#endif
static PFN_vkCmdCopyBufferToImage upload_regions_original;
static unsigned upload_regions_active,upload_regions_calls,upload_regions_count,upload_regions_max;
static uint64_t upload_regions_largest;
static VkBuffer upload_regions_first_source;
static uint64_t upload_regions_first_bytes;
static VKAPI_ATTR void VKAPI_CALL upload_regions_copy(VkCommandBuffer command,VkBuffer source,
    VkImage image,VkImageLayout layout,uint32_t count,const VkBufferImageCopy *regions) {
  if(upload_regions_active) {
    CHECK(count && count<=64);uint64_t end=0;
    for(unsigned n=0;n<count;n++) {
      CHECK(!(regions[n].bufferOffset%4) && regions[n].bufferOffset>=end);
      end=regions[n].bufferOffset+(uint64_t)regions[n].imageExtent.width*
          regions[n].imageExtent.height*regions[n].imageExtent.depth*4;
    }
    CHECK(count==1 || end<=(8ull<<20));
    if(!upload_regions_calls){upload_regions_first_source=source;upload_regions_first_bytes=end;}
    upload_regions_calls++;upload_regions_count+=count;
    if(count>upload_regions_max)upload_regions_max=count;
    if(end>upload_regions_largest)upload_regions_largest=end;
  }
  upload_regions_original(command,source,image,layout,count,regions);
}
static void upload_regions_pixels(uint8_t *p,unsigned w,unsigned h,unsigned z,unsigned level,unsigned layer) {
  for(unsigned slice=0;slice<z;slice++)for(size_t n=0;n<(size_t)w*h;n++) {
    uint8_t *v=p+((size_t)slice*w*h+n)*4;
    v[0]=17+level;v[1]=31+layer;v[2]=49+slice;v[3]=255;
  }
}
static void upload_regions_case(unsigned w,unsigned h,unsigned depth,unsigned levels,int volume,
    unsigned expected_calls,unsigned expected_max,uint64_t expected_largest) {
  NVNtextureBuilder b;CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
  CALL(nvnTextureBuilderSetTarget,&b,volume?NVN_TEXTURE_TARGET_3D:NVN_TEXTURE_TARGET_2D_ARRAY);
  CALL(nvnTextureBuilderSetWidth,&b,w);CALL(nvnTextureBuilderSetHeight,&b,h);CALL(nvnTextureBuilderSetDepth,&b,depth);
  CALL(nvnTextureBuilderSetLevels,&b,levels);CALL(nvnTextureBuilderSetFormat,&b,NVN_FORMAT_RGBA8);
  NVNpackagedTextureLayout layout={{0}};CALL(nvnTextureBuilderSetPackagedTextureLayout,&b,&layout);
  uint64_t bytes=CALL(nvnTextureBuilderGetStorageSize,&b);bytes=(bytes+65535)&~65535ull;
  uint8_t *storage;void *mapped=NULL;CHECK(bytes);
#ifdef DM_VULKAN_SHARED_POOLS
  if(vk_pool_shared_active()) {
    mapped=mmap(NULL,bytes+65536,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
    CHECK(mapped!=MAP_FAILED);storage=(void *)(((uintptr_t)mapped+65535)&~(uintptr_t)65535);
    vk_test_shared_image_backing(storage,bytes); // finite owned fixture certificate
  } else
#endif
  CHECK(!posix_memalign((void **)&storage,65536,bytes));
  memset(storage,0,bytes);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,bytes);CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));CALL(nvnTextureBuilderSetStorage,&b,&pool,0);
  NVNtexture texture;CHECK(CALL(nvnTextureInitialize,&texture,&b));const tex_t *t=(const tex_t *)&texture;
  uint8_t *pixels=malloc((size_t)w*h*(volume?depth:1)*4);CHECK(pixels);
  for(unsigned layer=0;layer<(volume?1:depth);layer++)for(unsigned level=0;level<levels;level++) {
    unsigned mw=w>>level,mh=h>>level,mz=volume?depth>>level:1;
    if(!mw)mw=1;if(!mh)mh=1;if(!mz)mz=1;
    upload_regions_pixels(pixels,mw,mh,mz,level,layer);
    CHECK(!nvn_swizzle(&t->d,level,layer,pixels,mw*4,t->mem));
  }
  VkSemaphore gate=vk_async_submissions()&&V.timeline_semaphore?block_gpu_queue():VK_NULL_HANDLE;
  mtl_sub_begin();vk_texture_cpu_preflight(t);
  upload_regions_calls=upload_regions_count=upload_regions_max=0;upload_regions_largest=0;
  upload_regions_active=1;vk_texture_test_astc_copy_upload(t);upload_regions_active=0;
  CHECK(upload_regions_calls==expected_calls && upload_regions_max==expected_max);
  CHECK(upload_regions_count==levels*(volume?1:depth));
  if(expected_largest)CHECK(upload_regions_largest==expected_largest);
  uint64_t ticket=vk_internal_commit();
  if(gate) {
    CHECK(vk_completed_serial()<ticket);
    mtl_sub_begin();vk_buffer *fresh=vk_temp_buffer(upload_regions_first_bytes);
    CHECK(fresh->handle!=upload_regions_first_source); // old batch still owned by true fence
    memset(fresh->map,0x93,upload_regions_first_bytes);
    uint64_t next=vk_internal_commit();
    VkSemaphoreSignalInfoKHR signal={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO_KHR,.semaphore=gate,.value=1};
    VK_CHECK(V.signal_timeline(V.device,&signal));vk_internal_wait(next);
    vkDestroySemaphore(V.device,gate,NULL);
  } else vk_internal_wait(ticket);
  for(unsigned layer=0;layer<(volume?1:depth);layer++)for(unsigned level=0;level<levels;level++) {
    unsigned mw=w>>level,mh=h>>level,mz=volume?depth>>level:1;
    if(!mw)mw=1;if(!mh)mh=1;if(!mz)mz=1;
    NVNcopyRegion region={0,0,0,mw,mh,mz};memset(pixels,0,(size_t)mw*mh*mz*4);
    mtl_read_texels(t,level,layer,&region,pixels,0,0);
    for(unsigned z=0;z<mz;z++)for(size_t n=0;n<(size_t)mw*mh;n++) {
      uint8_t *v=pixels+((size_t)z*mw*mh+n)*4;
      CHECK(v[0]==17+level && v[1]==31+layer && v[2]==49+z && v[3]==255);
    }
  }
  CALL(nvnTextureFinalize,&texture);CALL(nvnMemoryPoolFinalize,&pool);free(pixels);
#ifdef DM_VULKAN_SHARED_POOLS
  if(mapped) {vk_test_shared_image_backing(NULL,0);CHECK(!munmap(mapped,bytes+65536));}
  else
#endif
  free(storage);
  LOG("[upload regions GPU] %ux%ux%u levels=%u volume=%d calls=%u regions=%u max=%u largest=%llu",w,h,depth,levels,volume,
      upload_regions_calls,upload_regions_count,upload_regions_max,(unsigned long long)upload_regions_largest);
}
static void upload_regions_gpu_test(void) {
  mtl_init();upload_regions_original=vkCmdCopyBufferToImage;vkCmdCopyBufferToImage=upload_regions_copy;
  CHECK(vk_pool_shared_active()==!!(getenv("DM_VULKAN_SHARED_POOLS") && !strcmp(getenv("DM_VULKAN_SHARED_POOLS"),"1")));
  upload_regions_case(4,4,23,3,0,2,64,0); // region capacity including1x1 tail mips
  upload_regions_case(16,8,4,4,1,1,4,0); // real3D mip depth/slices
  // The original finite owned shared certificate is capped at4MiB. Keep it;
  // the same generic planner's large byte/fallback cases use legacy sources.
  if(!vk_pool_shared_active()) {
    upload_regions_case(1024,1024,3,1,0,2,2,8ull<<20);
    upload_regions_case(2048,2048,1,2,0,2,1,16ull<<20);
  }
  vkCmdCopyBufferToImage=upload_regions_original;CHECK(vk_validation_error_count()==0);
}
#endif
