// Real native-size RGBA8 byte patterns, the reference linear blit, and a
// subsequent NVN read of the original texture. No game shader is required.
#include "../../../gfx/vulkan/nvn_vk_present_shader.h"
static void shader_present_pixels(const uint8_t *actual,const uint8_t *expected,size_t size,const char *label) {
  if(memcmp(actual,expected,size)) {
    for(size_t n=0;n<size;n++)if(actual[n]!=expected[n]) {
      LOG("[vulkan shader present mismatch] %s byte=%zu actual=%u expected=%u",label,n,actual[n],expected[n]);break;
    }
    CHECK(0);
  }
  CHECK(1);
}
static void shader_present_size(unsigned width,unsigned height) {
  vk_state saved=S;size_t size=(size_t)width*height*4;
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,64<<20));memset(storage,0,64<<20);
  uint8_t *pattern=storage+(16<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,64<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtextureBuilder tb;NVNtexture source;
  CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);
  CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);
  CALL(nvnTextureBuilderSetWidth,&tb,width);CALL(nvnTextureBuilderSetHeight,&tb,height);
  CALL(nvnTextureBuilderSetLevels,&tb,1);CALL(nvnTextureBuilderSetStorage,&tb,&pool,0);
  CHECK(CALL(nvnTextureBuilderGetStorageSize,&tb)<=16<<20);
  CHECK(CALL(nvnTextureInitialize,&source,&tb));
  NVNcommandBuffer local;CHECK(CALL(nvnCommandBufferInitialize,&local,&device));
  CALL(nvnCommandBufferAddCommandMemory,&local,&pool,48<<20,1<<20);
  VkExtent2D extent={width,height};VkImage images[2];VkDeviceMemory memory[2];
  for(unsigned n=0;n<2;n++) {
    VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
      .format=VK_FORMAT_R8G8B8A8_UNORM,.extent={width,height,1},.mipLevels=1,.arrayLayers=1,
      .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
      .usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    VK_CHECK(vkCreateImage(V.device,&ci,NULL,&images[n]));VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(V.device,images[n],&mr);
    VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=mr.size,
      .memoryTypeIndex=vk_memory_type(mr.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    VK_CHECK(vkAllocateMemory(V.device,&ai,NULL,&memory[n]));VK_CHECK(vkBindImageMemory(V.device,images[n],memory[n],0));
  }
  vk_present_shader_target target={0};vk_present_shader_target_create(&target,images[0],extent);
  VkImageView cached=VK_NULL_HANDLE;
  for(unsigned phase=0;phase<2;phase++) {
    dm_guest_write_prepare(pattern,size);
    // All byte values, alpha, row boundaries, and a changing asymmetric image.
    for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++) {
      size_t p=((size_t)y*width+x)*4;
      pattern[p]=x+phase*71;pattern[p+1]=y*13+x*3+phase*29;
      pattern[p+2]=(x^y)+phase*47;pattern[p+3]=x*7+y*11+phase*101;
    }
    NVNcopyRegion region={0,0,0,width,height,1};
    CALL(nvnCommandBufferBeginRecording,&local);
    // Zero selects tightly packed rows and leaves the fixture's normal copy
    // state intact; a native-width explicit stride would leak into later tests.
    CALL(nvnCommandBufferSetCopyRowStride,&local,0);
    CALL(nvnCommandBufferCopyBufferToTexture,&local,(uintptr_t)pattern,&source,NULL,&region,0);
    submit(CALL(nvnCommandBufferEndRecording,&local));
    mtl_sub_begin();vk_present_sample sampled;
    CHECK(vk_texture_present_sample(&source,VK_FORMAT_R8G8B8A8_UNORM,extent,&sampled));
    CHECK(sampled.format==VK_FORMAT_R8G8B8A8_UNORM);
    CHECK(sampled.layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL || sampled.layout==VK_IMAGE_LAYOUT_GENERAL);
    if(cached)CHECK(sampled.view==cached);else cached=sampled.view;
    VkImageMemoryBarrier before[2];
    for(unsigned n=0;n<2;n++)before[n]=(VkImageMemoryBarrier){.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .oldLayout=phase?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED,
      .newLayout=n?VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .srcAccessMask=phase?VK_ACCESS_TRANSFER_READ_BIT:0,
      .dstAccessMask=n?VK_ACCESS_TRANSFER_WRITE_BIT:VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
      .image=images[n],.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    vkCmdPipelineBarrier(V.cmd,phase?VK_PIPELINE_STAGE_TRANSFER_BIT:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,NULL,0,NULL,2,before);
    vk_present_shader_render(&sampled,&target);
    VkImageLayout source_layout;VkImage raw=vk_texture_present_source((const tex_t *)&source,&source_layout);
    VkImageBlit blit={.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},
      .srcOffsets={{0,0,0},{width,height,1}},.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},
      .dstOffsets={{0,0,0},{width,height,1}}};
    vkCmdBlitImage(V.cmd,raw,source_layout,images[1],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_LINEAR);
    for(unsigned n=0;n<2;n++) {
      before[n].oldLayout=before[n].newLayout;before[n].newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      before[n].srcAccessMask=before[n].dstAccessMask;before[n].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    }
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,2,before);
    vk_buffer *output=vk_temp_buffer(size*2);
    for(unsigned n=0;n<2;n++) {
      VkBufferImageCopy copy={.bufferOffset=n*size,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},
        .imageExtent={width,height,1}};
      vkCmdCopyImageToBuffer(V.cmd,images[n],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,output->handle,1,&copy);
    }
    vk_barrier();vk_internal_wait(vk_internal_commit());
    shader_present_pixels(output->map,pattern,size,"shader");
    shader_present_pixels(output->map+size,pattern,size,"linear-blit reference");
    shader_present_pixels(output->map,output->map+size,size,"shader versus reference");
    uint8_t *retained=malloc(size);CHECK(retained!=NULL);
    CALL(nvnTextureReadTexels,&source,NULL,&region,retained);
    shader_present_pixels(retained,pattern,size,"retained NVN source");free(retained);
  }
  vk_present_shader_target_destroy(&target);
  for(unsigned n=0;n<2;n++) { vkDestroyImage(V.device,images[n],NULL);vkFreeMemory(V.device,memory[n],NULL); }
  CALL(nvnCommandBufferFinalize,&local);CALL(nvnTextureFinalize,&source);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);S=saved;vk_pipeline_state_changed();
}
static void shader_present_test(void) {
  VkExtent3D source={2772,1240,1};VkExtent2D target={2772,1240};
  CHECK(vk_present_shader_eligible(VK_FORMAT_R8G8B8A8_UNORM,VK_SAMPLE_COUNT_1_BIT,source,
    VK_IMAGE_USAGE_SAMPLED_BIT,VK_FORMAT_R8G8B8A8_UNORM,target,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,1));
  for(unsigned failure=0;failure<8;failure++) {
    VkExtent3D a=source;VkExtent2D b=target;
    VkFormat sf=VK_FORMAT_R8G8B8A8_UNORM,df=sf;
    VkImageUsageFlags su=VK_IMAGE_USAGE_SAMPLED_BIT,du=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkSampleCountFlagBits samples=VK_SAMPLE_COUNT_1_BIT;int full=1;
    if(failure==0)full=0;
    if(failure==1)b.width--;
    if(failure==2)df=VK_FORMAT_B8G8R8A8_UNORM;
    if(failure==3)sf=df=VK_FORMAT_R8G8B8A8_SRGB;
    if(failure==4)su=VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if(failure==5)du=VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if(failure==6)a.depth=2;
    if(failure==7)samples=VK_SAMPLE_COUNT_4_BIT;
    CHECK(!vk_present_shader_eligible(sf,samples,a,su,df,b,du,full));
  }
  shader_present_size(2772,1240);shader_present_size(2750,1260);
  LOG("[vulkan shader present fixture] two native sizes, all RGBA8 byte values/alpha/rows, real GPU producers and CPU updates, reused view/target, exact linear-blit reference, retained NVN source, format/resize/crop/MSAA/usage fallback passed");
}
