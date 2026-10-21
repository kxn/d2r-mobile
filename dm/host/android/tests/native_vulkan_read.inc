#include "native_vulkan_mip_stamp.inc"

// CPU ReadTexels strides describe destination rows, not Vulkan buffer layout.
// Guard every byte outside those rows, including odd pitches and slice gaps.
static void strided_read_texels_test(void) {
  uint8_t *storage;
  CHECK(!posix_memalign((void **)&storage,65536,1<<20));
  memset(storage,0,1<<20);
  NVNmemoryPoolBuilder mb; NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);
  CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  for(unsigned volume=0;volume<2;volume++) {
    NVNtextureBuilder b; NVNtexture texture;
    CALL(nvnTextureBuilderSetDefaults,&b);
    CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,volume?NVN_TEXTURE_TARGET_3D:NVN_TEXTURE_TARGET_2D_ARRAY);
    CALL(nvnTextureBuilderSetWidth,&b,8); CALL(nvnTextureBuilderSetHeight,&b,8);
    CALL(nvnTextureBuilderSetDepth,&b,4); CALL(nvnTextureBuilderSetLevels,&b,2);
    CALL(nvnTextureBuilderSetFormat,&b,NVN_FORMAT_RGBA8);
    CALL(nvnTextureBuilderSetStorage,&b,&pool,volume*65536);
    CHECK(CALL(nvnTextureInitialize,&texture,&b));
    NVNtextureView view;
    CALL(nvnTextureViewSetDefaults,&view);
    CALL(nvnTextureViewSetLevels,&view,1,1);
    if(!volume) CALL(nvnTextureViewSetLayers,&view,1,2);
    CALL(nvnCommandBufferBeginRecording,&cmd);
    for(unsigned z=0;z<(volume?2:4);z++) {
      NVNcopyRegion plane={0,0,z,4,4,1};
      const float color[4]={!(z&1),z&1,0,1};
      CALL(nvnCommandBufferClearTexture,&cmd,&texture,&view,&plane,color,15);
      // Array clears address the selected view's first layer plus zoffset.
      if(!volume && z==1) break;
    }
    submit(end());
    NVNcopyRegion region={1,1,0,3,2,2};
    const size_t rows[]={16,17,12},images[]={48,43,24};
    for(unsigned pitch=0;pitch<3;pitch++) {
      region.depth = pitch==2 ? 0 : 2;
      uint8_t result[128],expected[128];
      memset(result,0xd7,sizeof result); memset(expected,0xd7,sizeof expected);
      for(unsigned z=0;z<(pitch==2?1:2);z++) for(unsigned y=0;y<2;y++) for(unsigned x=0;x<3;x++) {
        uint8_t color[4]={z?0:255,z?255:0,0,255};
        memcpy(expected+z*images[pitch]+y*rows[pitch]+x*4,color,4);
      }
      CALL(nvnTextureReadTexelsStrided,&texture,&view,&region,result,rows[pitch],images[pitch]);
      for(unsigned byte=0;byte<sizeof result;byte++) {
        if(result[byte]!=expected[byte])
          LOG("strided read mismatch: volume=%u row=%zu image=%zu byte=%u got=%u expected=%u",
              volume,rows[pitch],images[pitch],byte,result[byte],expected[byte]);
        CHECK(result[byte]==expected[byte]);
      }
    }
    CALL(nvnTextureFinalize,&texture);
  }
  CALL(nvnMemoryPoolFinalize,&pool); free(storage);
  LOG("[vulkan CPU texture read fixture] mip/array/3D slices, odd strides and untouched row/slice padding passed");
}

// A partial GPU copy must preserve packaged CPU data in every other mip and
// slice, both on first use and after the CPU changes an already uploaded image.
static void texture_copy_destination_test(void) {
  uint8_t *storage;
  CHECK(!posix_memalign((void **)&storage,65536,1<<20));
  memset(storage,0,1<<20);
  NVNmemoryPoolBuilder mb; NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);
  CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  for(unsigned volume=0;volume<2;volume++) {
    for(unsigned scaled=0;scaled<2;scaled++) {
      NVNtextureBuilder b; NVNtexture source,destination;
      CALL(nvnTextureBuilderSetDefaults,&b);
      CALL(nvnTextureBuilderSetDevice,&b,&device);
      CALL(nvnTextureBuilderSetWidth,&b,8); CALL(nvnTextureBuilderSetHeight,&b,8);
      CALL(nvnTextureBuilderSetFormat,&b,NVN_FORMAT_RGBA8);
      if(volume) {
        CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_3D);
        CALL(nvnTextureBuilderSetDepth,&b,4);
      }
      CALL(nvnTextureBuilderSetStorage,&b,&pool,0);
      CHECK(CALL(nvnTextureInitialize,&source,&b));
      CALL(nvnTextureBuilderSetTarget,&b,volume?NVN_TEXTURE_TARGET_3D:NVN_TEXTURE_TARGET_2D_ARRAY);
      CALL(nvnTextureBuilderSetDepth,&b,4); CALL(nvnTextureBuilderSetLevels,&b,2);
      CALL(nvnTextureBuilderSetStorage,&b,&pool,65536);
      NVNpackagedTextureLayout packaged={{0}};
      CALL(nvnTextureBuilderSetPackagedTextureLayout,&b,&packaged);
      CHECK(CALL(nvnTextureInitialize,&destination,&b));
      NVNtextureView view;
      CALL(nvnTextureViewSetDefaults,&view); CALL(nvnTextureViewSetLevels,&view,1,1);
      if(!volume) CALL(nvnTextureViewSetLayers,&view,1,3);
      const float green[4]={0,1,0,1}; NVNcopyRegion whole={0,0,0,8,8,volume?4:1};
      CALL(nvnCommandBufferBeginRecording,&cmd);
      CALL(nvnCommandBufferClearTexture,&cmd,&source,NULL,&whole,green,15);
      submit(end());
      for(unsigned cpu_edit=0;cpu_edit<2;cpu_edit++) {
        uint8_t initial[4]={31+cpu_edit,61,91,255};
        for(unsigned byte=65536;byte<131072;byte+=4) memcpy(storage+byte,initial,4);
        NVNcopyRegion sr={0,0,0,scaled?4:2,scaled?4:2,1},dr={1,1,1,2,2,1};
        CALL(nvnCommandBufferBeginRecording,&cmd);
        CALL(nvnCommandBufferCopyTextureToTexture,&cmd,&source,NULL,&sr,&destination,&view,&dr,
             scaled?NVN_COPY_FLAGS_ENGINE_2D_BIT:0);
        submit(end());
        for(unsigned level=0;level<2;level++) {
          unsigned size=8>>level,slices=volume?4>>level:4;
          for(unsigned z=0;z<slices;z++) {
            NVNtextureView read_view;
            CALL(nvnTextureViewSetDefaults,&read_view);
            CALL(nvnTextureViewSetLevels,&read_view,level,1);
            NVNcopyRegion read={0,0,volume?z:0,size,size,1};
            if(!volume) CALL(nvnTextureViewSetLayers,&read_view,z,1);
            uint8_t pixels[8*8*4];
            CALL(nvnTextureReadTexels,&destination,&read_view,&read,pixels);
            for(unsigned y=0;y<size;y++) for(unsigned x=0;x<size;x++) {
              const uint8_t copied[4]={0,255,0,255};
              int inside=level==1 && z==(volume?1:2) && x>=1 && x<3 && y>=1 && y<3;
              const uint8_t *expected=inside?copied:initial;
              if(memcmp(pixels+(y*size+x)*4,expected,4))
                LOG("copy destination mismatch: volume=%u scaled=%u CPUedit=%u mip=%u slice=%u xy=%u,%u got=%u,%u,%u,%u",
                    volume,scaled,cpu_edit,level,z,x,y,pixels[(y*size+x)*4],pixels[(y*size+x)*4+1],pixels[(y*size+x)*4+2],pixels[(y*size+x)*4+3]);
              CHECK(!memcmp(pixels+(y*size+x)*4,expected,4));
            }
          }
        }
      }
      CALL(nvnTextureFinalize,&destination); CALL(nvnTextureFinalize,&source);
    }
  }
  CALL(nvnMemoryPoolFinalize,&pool); free(storage);
  LOG("[vulkan texture copy fixture] packaged partial copy/blit preserves other mips/slices and CPU edits passed");
}

// Sample mip1 then first-bind mip2 while rendering mip0. A storage promotion
// afterwards must normalize mixed layouts and retain every mip and old view.
static void separate_mip_layout_test(NVNprogram *sample,NVNprogram *fill,NVNtexturePool *descriptors,
                                     NVNtexture *restore_target) {
  NVNtextureBuilder b;NVNtexture texture;NVNtextureView views[3];
  CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
  CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_2D);
  CALL(nvnTextureBuilderSetWidth,&b,16);CALL(nvnTextureBuilderSetHeight,&b,16);
  CALL(nvnTextureBuilderSetLevels,&b,3);CALL(nvnTextureBuilderSetFormat,&b,NVN_FORMAT_RGBA8);
  CALL(nvnTextureBuilderSetFlags,&b,NVN_TEXTURE_FLAGS_IMAGE);
  CALL(nvnTextureBuilderSetStorage,&b,&memory,(3<<20)+720896);
  CHECK(CALL(nvnTextureInitialize,&texture,&b));
  const float initial[3][4]={{0,0,0,1},{0,1,0,1},{0,0,1,1}};
  CALL(nvnCommandBufferBeginRecording,&cmd);
  for(unsigned level=0;level<3;level++) {
    CALL(nvnTextureViewSetDefaults,&views[level]);CALL(nvnTextureViewSetLevels,&views[level],level,1);
    NVNcopyRegion r={0,0,0,16>>level,16>>level,1};
    CALL(nvnCommandBufferClearTexture,&cmd,&texture,&views[level],&r,initial[level],15);
  }
  submit(end());
  for(unsigned level=1;level<3;level++) CALL(nvnTexturePoolRegisterTexture,descriptors,level+1,&texture,&views[level]);
  const NVNtexture *targets[]={&texture};const NVNtextureView *target_views[]={&views[0]};
  NVNseparateTextureHandle handles[2]={CALL(nvnDeviceGetSeparateTextureHandle,&device,2),CALL(nvnDeviceGetSeparateTextureHandle,&device,3)};
  NVNimageHandle storage=CALL(nvnDeviceGetImageHandle,&device,3);
  for(unsigned iteration=0;iteration<2;iteration++) {
    uint64_t passes,draws,after_passes,after_draws;mtl_stats(&passes,&draws);
    CALL(nvnCommandBufferBeginRecording,&cmd);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,targets,target_views,NULL,NULL);
    CALL(nvnCommandBufferBindProgram,&cmd,sample,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    for(unsigned half=0;half<2;half++) {
      CALL(nvnCommandBufferBindSeparateTextures,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&handles[half]);
      CALL(nvnCommandBufferSetScissor,&cmd,half*8,0,8,16);
      CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    }
    if(!iteration) {
      CALL(nvnCommandBufferBindProgram,&cmd,fill,NVN_SHADER_STAGE_COMPUTE_BIT);
      CALL(nvnCommandBufferBindImages,&cmd,NVN_SHADER_STAGE_COMPUTE,0,1,&storage);
      CALL(nvnCommandBufferDispatchCompute,&cmd,1,1,1);
    }
    submit(end());mtl_stats(&after_passes,&after_draws);
    CHECK(after_passes-passes==1);
    for(unsigned level=0;level<3;level++) {
      uint8_t pixels[16*16*4];NVNcopyRegion r={0,0,0,16>>level,16>>level,1};
      CALL(nvnTextureReadTexels,&texture,&views[level],&r,pixels);
      for(unsigned y=0;y<(unsigned)r.height;y++) for(unsigned x=0;x<(unsigned)r.width;x++) {
        const uint8_t *expected=level==1 || (!level&&x<8)?(const uint8_t[]){0,255,0,255}:
            level==2 || iteration?(const uint8_t[]){255,0,128,255}:(const uint8_t[]){0,0,255,255};
        CHECK(!memcmp(pixels+(y*r.width+x)*4,expected,4));
      }
    }
  }
  CALL(nvnCommandBufferBeginRecording,&cmd);
  const NVNtexture *restore[]={restore_target};
  NVNseparateTextureHandle handle=CALL(nvnDeviceGetSeparateTextureHandle,&device,1);
  CALL(nvnCommandBufferSetRenderTargets,&cmd,1,restore,NULL,NULL,NULL);
  CALL(nvnCommandBufferBindProgram,&cmd,sample,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
  CALL(nvnCommandBufferBindSeparateTextures,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&handle);
  CALL(nvnCommandBufferSetScissor,&cmd,0,0,16,16);submit(end());
  CALL(nvnTextureFinalize,&texture);
  LOG("[vulkan separate mip fixture] mip0 attachment and late mip1/mip2 samples stay in one pass; mixed-layout storage promotion and repeated submissions preserve all pixels");
}

// Actual shader RAW/WAW, copy WAR, aliases within one dispatch and independent
// mip/layer contents. No explicit NVN barrier hides the backend dependencies.
static void image_dependency_test(NVNprogram *fill,int image_hint,int srgb) {
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,1<<20));
  memset(storage,0,1<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;NVNtexturePool descriptors;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  CHECK(CALL(nvnTexturePoolInitialize,&descriptors,&pool,786432,256));
  NVNtexture textures[2];NVNtextureView views[3][2];
  for(unsigned t=0;t<2;t++) {
    NVNtextureBuilder b;CALL(nvnTextureBuilderSetDefaults,&b);
    CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_2D_ARRAY);
    CALL(nvnTextureBuilderSetFlags,&b,image_hint?NVN_TEXTURE_FLAGS_IMAGE:0);
    CALL(nvnTextureBuilderSetWidth,&b,8);CALL(nvnTextureBuilderSetHeight,&b,8);
    CALL(nvnTextureBuilderSetDepth,&b,2);CALL(nvnTextureBuilderSetLevels,&b,3);
    CALL(nvnTextureBuilderSetFormat,&b,srgb?NVN_FORMAT_RGBA8_SRGB:NVN_FORMAT_RGBA8);
    CALL(nvnTextureBuilderSetStorage,&b,&pool,t*65536);
    CHECK(CALL(nvnTextureInitialize,&textures[t],&b));
  }
  uint8_t control[0x880];memset(control,30,sizeof control);
  NVNprogram increment;CHECK(CALL(nvnProgramInitialize,&increment,&device));
  NVNshaderData shader={.control=control};CHECK(CALL(nvnProgramSetShaders,&increment,1,&shader));
  for(unsigned level=0;level<3;level++) for(unsigned layer=0;layer<2;layer++) {
    CALL(nvnTextureViewSetDefaults,&views[level][layer]);
    CALL(nvnTextureViewSetLevels,&views[level][layer],level,1);
    CALL(nvnTextureViewSetLayers,&views[level][layer],layer,1);
  }
  NVNtextureView image_view=views[1][1];CALL(nvnTextureViewSetTarget,&image_view,NVN_TEXTURE_TARGET_2D);
  if(srgb) CALL(nvnTextureViewSetFormat,&image_view,NVN_FORMAT_RGBA8);
  CALL(nvnTexturePoolRegisterTexture,&descriptors,1,&textures[0],&image_view);
  NVNimageHandle handles[2]={1,1};
  const float black[4]={0,0,0,1},green[4]={0,1,0,1};
  NVNcopyRegion selected={0,0,0,4,4,1};
  CALL(nvnCommandBufferBeginRecording,&cmd);
  CALL(nvnCommandBufferSetTexturePool,&cmd,&descriptors);
  for(unsigned t=0;t<2;t++) for(unsigned level=0;level<3;level++) for(unsigned layer=0;layer<2;layer++) {
    NVNcopyRegion r={0,0,0,8>>level,8>>level,1};
    if(!t) {
      // Populate attachment views and cached framebuffers before lazy storage
      // promotion, in the same submission as the first compute writer.
      const NVNtexture *targets[]={&textures[t]};
      const NVNtextureView *target_views[]={&views[level][layer]};
      CALL(nvnCommandBufferSetRenderTargets,&cmd,1,targets,target_views,NULL,NULL);
      CALL(nvnCommandBufferClearColor,&cmd,0,black,15);
    } else CALL(nvnCommandBufferClearTexture,&cmd,&textures[t],&views[level][layer],&r,black,15);
  }
  CALL(nvnCommandBufferBindImages,&cmd,NVN_SHADER_STAGE_COMPUTE,0,2,handles);
  CALL(nvnCommandBufferBindProgram,&cmd,fill,NVN_SHADER_STAGE_COMPUTE_BIT);
  CALL(nvnCommandBufferDispatchCompute,&cmd,1,1,1);
  CALL(nvnCommandBufferBindProgram,&cmd,&increment,NVN_SHADER_STAGE_COMPUTE_BIT);
  CALL(nvnCommandBufferDispatchCompute,&cmd,4,4,1);
  CALL(nvnCommandBufferDispatchCompute,&cmd,4,4,1);
  CALL(nvnCommandBufferCopyTextureToTexture,&cmd,&textures[0],&views[1][1],&selected,
      &textures[1],&views[1][0],&selected,0);
  CALL(nvnCommandBufferClearTexture,&cmd,&textures[0],&views[1][1],&selected,green,15);
  submit(end());
  // Exercise the promoted image again after completion callbacks retire the
  // original views; storage descriptors and attachments must use new storage.
  CALL(nvnCommandBufferBeginRecording,&cmd);
  CALL(nvnCommandBufferBindProgram,&cmd,fill,NVN_SHADER_STAGE_COMPUTE_BIT);
  CALL(nvnCommandBufferDispatchCompute,&cmd,1,1,1);
  const NVNtexture *targets[]={&textures[0]};
  const NVNtextureView *target_views[]={&views[1][1]};
  CALL(nvnCommandBufferSetRenderTargets,&cmd,1,targets,target_views,NULL,NULL);
  CALL(nvnCommandBufferClearColor,&cmd,0,green,15);
  CALL(nvnCommandBufferSetRenderTargets,&cmd,0,NULL,NULL,NULL,NULL);
  submit(end());
  for(unsigned t=0;t<2;t++) for(unsigned level=0;level<3;level++) for(unsigned layer=0;layer<2;layer++) {
    NVNcopyRegion r={0,0,0,8>>level,8>>level,1};uint8_t pixels[8*8*4];
    CALL(nvnTextureReadTexels,&textures[t],&views[level][layer],&r,pixels);
    const uint8_t *expected=level==1 && layer==(t?0:1)?
        (t?(const uint8_t[]){255,128,128,255}:(const uint8_t[]){0,255,0,255}):
        (const uint8_t[]){0,0,0,255};
    for(unsigned p=0;p<(unsigned)(r.width*r.height);p++) {
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
      if(memcmp(pixels+p*4,expected,4))LOG("[image dependency mismatch] hint=%d srgb=%d texture=%u level=%u layer=%u pixel=%u got=%u/%u/%u/%u expected=%u/%u/%u/%u",image_hint,srgb,t,level,layer,p,pixels[p*4],pixels[p*4+1],pixels[p*4+2],pixels[p*4+3],expected[0],expected[1],expected[2],expected[3]);
#endif
      CHECK(!memcmp(pixels+p*4,expected,4));
    }
  }
  CALL(nvnProgramFinalize,&increment);
  for(unsigned t=0;t<2;t++) CALL(nvnTextureFinalize,&textures[t]);
  CALL(nvnTexturePoolFinalize,&descriptors);CALL(nvnMemoryPoolFinalize,&pool);free(storage);
  LOG("[vulkan image dependency fixture] IMAGE hint=%d sRGB=%d, attachment-to-storage promotion, retained mip/layers, retired framebuffer views and repeated storage bindings passed",image_hint,srgb);
}


// Original Legacy Device Pack uploads ASTC and copies it into arrays declared
// BC3 by the game. On a non-BC device this is ASTC -> decoded RGBA8, including
// sRGB encoding. Verify real GPU conversion and untouched mips/array layers.
static void astc_legacy_copy_test(void) {
  if(!V.features.textureCompressionASTC_LDR || V.features.textureCompressionBC) return;
  uint8_t *storage; CHECK(!posix_memalign((void **)&storage,65536,1<<20));
  memset(storage,0,1<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  // Arm astcenc 5.2.0, 4x4 -thorough, constant RGBA(31,63,127,255).
  const uint8_t block[16]={0xfc,0xfd,0xff,0xff,0xff,0xff,0xff,0xff,0x1f,0x1f,0x3f,0x3f,0x7f,0x7f,0xff,0xff};
  memcpy(storage+196608,block,sizeof block);
  for(unsigned srgb=0;srgb<2;srgb++) {
    NVNtextureBuilder b;NVNtexture source,array,output;
    CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetWidth,&b,4);CALL(nvnTextureBuilderSetHeight,&b,4);
    CALL(nvnTextureBuilderSetFormat,&b,srgb?NVN_FORMAT_RGBA_ASTC_4x4_SRGB:NVN_FORMAT_RGBA_ASTC_4x4);
    CALL(nvnTextureBuilderSetStorage,&b,&pool,0);CHECK(CALL(nvnTextureInitialize,&source,&b));
    CALL(nvnTextureBuilderSetWidth,&b,8);CALL(nvnTextureBuilderSetHeight,&b,8);
    CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_2D_ARRAY);CALL(nvnTextureBuilderSetDepth,&b,3);
    CALL(nvnTextureBuilderSetLevels,&b,2);
    CALL(nvnTextureBuilderSetFormat,&b,srgb?NVN_FORMAT_RGBA_DXT5_SRGB:NVN_FORMAT_RGBA_DXT5);
    NVNpackagedTextureLayout packaged={{0}};
    CALL(nvnTextureBuilderSetPackagedTextureLayout,&b,&packaged);
    CALL(nvnTextureBuilderSetStorage,&b,&pool,65536);CHECK(CALL(nvnTextureInitialize,&array,&b));
    CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetDepth,&b,1);
    CALL(nvnTextureBuilderSetLevels,&b,1);CALL(nvnTextureBuilderSetWidth,&b,4);CALL(nvnTextureBuilderSetHeight,&b,4);
    CALL(nvnTextureBuilderSetFormat,&b,srgb?NVN_FORMAT_RGBA8_SRGB:NVN_FORMAT_RGBA8);
    CALL(nvnTextureBuilderSetStorage,&b,&pool,131072);CHECK(CALL(nvnTextureInitialize,&output,&b));
    NVNcopyRegion four={0,0,0,4,4,1},dst={0,0,1,4,4,1};NVNtextureView view;
    CALL(nvnTextureViewSetDefaults,&view);CALL(nvnTextureViewSetLevels,&view,1,1);CALL(nvnTextureViewSetLayers,&view,1,2);
    CALL(nvnCommandBufferBeginRecording,&cmd);CALL(nvnCommandBufferSetCopyRowStride,&cmd,16);
    CALL(nvnCommandBufferCopyBufferToTexture,&cmd,(uintptr_t)(storage+196608),&source,NULL,&four,0);
    CALL(nvnCommandBufferCopyTextureToTexture,&cmd,&source,NULL,&four,&array,&view,&dst,0);submit(end());
    for(unsigned level=0;level<2;level++) for(unsigned layer=0;layer<3;layer++) {
      CALL(nvnTextureViewSetDefaults,&view);CALL(nvnTextureViewSetLevels,&view,level,1);
      CALL(nvnTextureViewSetLayers,&view,layer,1);
      NVNcopyRegion src={0,0,0,8>>level,8>>level,1};
      CALL(nvnCommandBufferBeginRecording,&cmd);
      CALL(nvnCommandBufferCopyTextureToTexture,&cmd,&array,&view,&src,&output,NULL,&four,0);submit(end());
      uint8_t pixels[64],expected[4]={0,0,0,0};
      if(level==1&&layer==2) memcpy(expected,(uint8_t[]){31,63,127,255},4);
      CALL(nvnTextureReadTexels,&output,NULL,&four,pixels);
      for(unsigned byte=0;byte<sizeof pixels;byte++) {
        int delta=(int)pixels[byte]-expected[byte%4];
        if(delta < -1 || delta > 1) LOG("ASTC legacy mismatch srgb=%u mip=%u layer=%u byte=%u got=%u expected=%u",srgb,level,layer,byte,pixels[byte],expected[byte%4]);
        CHECK(delta>=-1&&delta<=1);
      }
    }
    CALL(nvnTextureFinalize,&output);CALL(nvnTextureFinalize,&array);CALL(nvnTextureFinalize,&source);
  }
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);
  LOG("[vulkan ASTC legacy fixture] native ASTC upload, linear/sRGB decode into BC3 fallback array mip/layer and untouched subresources passed");
}
