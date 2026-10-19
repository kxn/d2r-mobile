// Fresh, explicitly self-owned whole 64KiB readonly UBO allocation. Original
// NVN recorder -> translator -> shader -> real pixels; not a guest/game probe.
static void dirty_range_pixels(NVNtexture *texture,const uint8_t value[4]);
static void owned_nvn_buffer_test(void) {
  mtl_init();
  CHECK(vk_test_pool_owned_supported());
  uint8_t vertex[0x880]={0},fragment[0x880];memset(fragment,39,sizeof fragment);
  NVNshaderData shaders[]={{.control=vertex},{.control=fragment}};
  NVNprogram program;CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  uint8_t *uniform,*storage;CHECK(!posix_memalign((void **)&uniform,65536,65536));
  CHECK(!posix_memalign((void **)&storage,65536,786432));memset(uniform,0,65536);memset(storage,0,786432);
  NVNmemoryPoolBuilder mb;NVNmemoryPool source,other;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,uniform,65536);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&source,&mb));
  uint64_t imports,retirements;vk_test_pool_owned_stats(&imports,&retirements);
  CHECK(vk_test_pool_owned_attach((uintptr_t)uniform,65536));
  CHECK(!vk_test_pool_owned_attach((uintptr_t)uniform,65536)); // one lifetime, no repin
  int properties=vk_test_pool_owned_properties((uintptr_t)uniform);
  CHECK((properties&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT))==
      (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT));
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,786432);CHECK(CALL(nvnMemoryPoolInitialize,&other,&mb));
  NVNtextureBuilder tb;NVNtexture texture;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&tb,16);CALL(nvnTextureBuilderSetHeight,&tb,16);
  CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);CALL(nvnTextureBuilderSetStorage,&tb,&other,524288);CHECK(CALL(nvnTextureInitialize,&texture,&tb));
  NVNcommandBuffer local_cmd;CHECK(CALL(nvnCommandBufferInitialize,&local_cmd,&device));
  CALL(nvnCommandBufferAddCommandMemory,&local_cmd,&other,131072,393216);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);NVNchannelMaskState mask;CALL(nvnChannelMaskStateSetDefaults,&mask);
  const NVNtexture *target[]={&texture};const float viewport[]={0,0,16,16};uint8_t pixels[1024];NVNcopyRegion region={0,0,0,16,16,1};
  vk_state saved=S;uint64_t ignored[4];int enabled;vk_uniform_specialization_stats(ignored,ignored+1,ignored+2,ignored+3,&enabled);vk_uniform_specialization_enabled(0);
  vk_pool_span_info original;CHECK(vk_pool_span_query((uintptr_t)uniform,65536,&original)==VK_POOL_SPAN_SINGLE);
  const float values[]={.25f,.75f,.125f,.875f};
  for(unsigned iteration=0;iteration<4;iteration++) {
    // Completed previous draw/readback before legal CPU modification. Both
    // original prepare and direct CPU store run with exclusion still held.
    nvn_dirty_protect(uniform,65536);
    if(iteration&1)dm_guest_write_prepare(uniform,4);
    float cpu_value=iteration==3?.375f:values[iteration];memcpy(uniform,&cpu_value,4);
    CALL(nvnMemoryPoolFlushMappedRange,&source,0,4);
    CALL(nvnCommandBufferBeginRecording,&local_cmd);
    if(iteration==3) {
      // This ORIGINAL GPU writer must trigger outer operation demotion before
      // any borrowed handle, rebuild shadow, then original copy/writeback.
      CALL(nvnCommandBufferUpdateUniformBuffer,&local_cmd,(uintptr_t)uniform,65536,0,4,&values[iteration]);
    }
    CALL(nvnCommandBufferSetRenderTargets,&local_cmd,1,target,NULL,NULL,NULL);
    CALL(nvnCommandBufferSetViewports,&local_cmd,0,1,viewport);CALL(nvnCommandBufferSetScissor,&local_cmd,0,0,16,16);
    CALL(nvnCommandBufferBindDepthStencilState,&local_cmd,&ds);CALL(nvnCommandBufferBindPolygonState,&local_cmd,&polygon);
    CALL(nvnCommandBufferBindColorState,&local_cmd,&color);CALL(nvnCommandBufferBindChannelMaskState,&local_cmd,&mask);
    CALL(nvnCommandBufferBindVertexAttribState,&local_cmd,0,NULL);CALL(nvnCommandBufferBindVertexStreamState,&local_cmd,0,NULL);
    CALL(nvnCommandBufferBindProgram,&local_cmd,&program,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferBindUniformBuffer,&local_cmd,NVN_SHADER_STAGE_FRAGMENT,0,(uintptr_t)uniform,65536);
    CALL(nvnCommandBufferDrawArrays,&local_cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    submit(CALL(nvnCommandBufferEndRecording,&local_cmd));CALL(nvnTextureReadTexels,&texture,NULL,&region,pixels);
    for(unsigned p=0;p<256;p++) {
      CHECK(abs((int)pixels[p*4]-(int)lroundf(values[iteration]*255))<=1);
      CHECK(pixels[p*4+1]==(values[iteration]<.5f?255:0) && pixels[p*4+2]==(values[iteration]<.5f?0:255) && pixels[p*4+3]==255);
    }
    CHECK(!memcmp(uniform,&values[iteration],4));
    if(iteration==3)CHECK(vk_test_pool_owned_shadow_matches((uintptr_t)uniform,&values[iteration],4));
    vk_pool_span_info current;CHECK(vk_pool_span_query((uintptr_t)uniform,65536,&current)==VK_POOL_SPAN_SINGLE);
    if(iteration<3) {
      CHECK(current.handle==original.handle && current.backing_generation==original.backing_generation);
      CHECK(vk_test_pool_owned_properties((uintptr_t)uniform)==properties);
    } else {CHECK(current.handle!=original.handle);CHECK(!vk_test_pool_owned_properties((uintptr_t)uniform));}
    uint64_t now_imports,now_retirements;vk_test_pool_owned_stats(&now_imports,&now_retirements);
    CHECK(now_imports==imports+1);CHECK(now_retirements==retirements+(iteration==3));
  }
  CHECK(!vk_test_pool_owned_attach((uintptr_t)uniform,65536)); // sticky retirement
  nvn_dirty_protect(uniform,4096);*(volatile float *)uniform=.5f; // original fault restored
  CHECK(!vk_test_pool_owned_retire((uintptr_t)uniform,65536));
  vk_uniform_specialization_enabled(enabled);S=saved;vk_pipeline_state_changed();
  CALL(nvnCommandBufferFinalize,&local_cmd);CALL(nvnTextureFinalize,&texture);CALL(nvnProgramFinalize,&program);
  CALL(nvnMemoryPoolFinalize,&source);CALL(nvnMemoryPoolFinalize,&other);free(uniform);free(storage);
  // A distinct initially protected allocation is rejected before any import;
  // neither the test nor API makes it writable in order to retry GUP.
  uint8_t *protected;CHECK(!posix_memalign((void **)&protected,65536,65536));memset(protected,0,65536);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,protected,65536);CHECK(CALL(nvnMemoryPoolInitialize,&source,&mb));
  vk_test_pool_owned_stats(&imports,&retirements);nvn_dirty_protect(protected,65536);
  CHECK(!vk_test_pool_owned_attach((uintptr_t)protected,65536));
  uint64_t after_imports,after_retirements;vk_test_pool_owned_stats(&after_imports,&after_retirements);
  CHECK(after_imports==imports && after_retirements==retirements);
  *protected=73; // ORIGINAL fault handler; no manual unprotect/retry
  CHECK(*protected==73);CALL(nvnMemoryPoolFinalize,&source);free(protected);
  // Late ORIGINAL packaged image initialization is a CPU exposure. It must
  // retire before first VkImage use; no image may silently coexist with import.
  uint8_t *aliased;CHECK(!posix_memalign((void **)&aliased,65536,65536));memset(aliased,0,65536);
  const uint8_t pixel[4]={13,51,99,255};for(unsigned p=0;p<4096;p+=4)memcpy(aliased+p,pixel,4);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,aliased,65536);CHECK(CALL(nvnMemoryPoolInitialize,&source,&mb));
  CHECK(vk_test_pool_owned_attach((uintptr_t)aliased,65536));
  CHECK(vk_test_pool_owned_properties((uintptr_t)aliased));
  CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&tb,16);CALL(nvnTextureBuilderSetHeight,&tb,16);
  CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);NVNpackagedTextureLayout packaged={{0}};
  CALL(nvnTextureBuilderSetPackagedTextureLayout,&tb,&packaged);CALL(nvnTextureBuilderSetStorage,&tb,&source,0);
  CHECK(CALL(nvnTextureInitialize,&texture,&tb));CHECK(!vk_test_pool_owned_properties((uintptr_t)aliased));
  dirty_range_pixels(&texture,pixel);
  CHECK(!vk_test_pool_owned_attach((uintptr_t)aliased,65536));
  CALL(nvnTextureFinalize,&texture);CALL(nvnMemoryPoolFinalize,&source);free(aliased);
  LOG("[owned NVN fixture] readonly UBO real pixels, same import, legal CPU writes, original GPU writer sticky demotion, restored fault passed; flags=%#x",properties);
}
