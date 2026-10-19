// Production entry-point integration, not an isolated reservation API test.
// Reuse the original pixel/byte oracles unchanged under the window policy.
#ifdef DM_VULKAN_TEST_EMBED
extern void vk_test_pool_window_bytes(uint64_t);
static void pool_window_ghost_promotion_test(const void *vertex,const void *fragment) {
  void mtl_ui_ghost(int,float,float,const float[4],float,float);
  int mtl_ui_ghost_rect(float[4]);vk_state saved=S;
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,1<<20));memset(storage,0,1<<20);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;CALL(nvnMemoryPoolBuilderSetDefaults,&mb);
  CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtexture textures[2];for(unsigned i=0;i<2;i++) {
    NVNtextureBuilder tb;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
    CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&tb,i?4:16);
    CALL(nvnTextureBuilderSetHeight,&tb,i?4:16);CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);
    CALL(nvnTextureBuilderSetStorage,&tb,&pool,i*65536);CHECK(CALL(nvnTextureInitialize,&textures[i],&tb));
  }
  const float quad[]={-.75f,-.75f,0,0,0,-.25f,-.75f,0,1,0,-.75f,-.25f,0,0,1,-.25f,-.25f,0,1,1};
  memcpy(storage+0x30000,quad,sizeof quad);memcpy(storage+(4*65536-16),quad,sizeof quad);
  float *other=(float *)(storage+(4*65536-16));for(unsigned n=0;n<4;n++)other[n*5]+=1;
  float *matrix=(float *)(storage+0x50000);matrix[0]=matrix[5]=matrix[10]=matrix[15]=1;
  NVNprogram program;NVNshaderData shaders[]={{.control=vertex},{.control=fragment}};
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  NVNvertexAttribState attr[2];for(unsigned n=0;n<2;n++) {
    CALL(nvnVertexAttribStateSetDefaults,&attr[n]);CALL(nvnVertexAttribStateSetStreamIndex,&attr[n],0);
    CALL(nvnVertexAttribStateSetFormat,&attr[n],n?NVN_FORMAT_RG32F:NVN_FORMAT_RGB32F,n?12:0);
  }
  NVNvertexStreamState stream;CALL(nvnVertexStreamStateSetDefaults,&stream);CALL(nvnVertexStreamStateSetStride,&stream,20);
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);
  CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);
  enum { texture_id=77 };mtl_set_tex_pool_n(256);mtl_reg_tex(texture_id,(const tex_t *)&textures[1],NULL);
  mtl_sub_begin();S=(vk_state){.program=(const prog_t *)&program,.depth_range={0,1},.chmask={~0u}};
  S.targets=(p_set_rt){.n=1,.color={(const tex_t *)&textures[0]}};
  S.poly=*(const poly_st *)&polygon;S.ds=*(const ds_st *)&ds;S.color=*(const color_st *)&color;
  S.attr[0]=*(const va_st *)&attr[0];S.attr[1]=*(const va_st *)&attr[1];S.stream[0]=*(const vs_st *)&stream;
  S.vertex[0]=(NVNbufferRange){(uintptr_t)storage+0x30000,80};
  S.ubo[0][0]=(p_bind_buf){.stage=0,.index=0,.addr=(uintptr_t)matrix,.size=64};S.tex[1][0]=texture_id;
  S.has_viewport=S.has_scissor=1;S.viewport[0][2]=S.viewport[0][3]=16;S.scissor=(p_scissor){0,0,16,16};
  vk_pipeline_state_changed();
  p_clear_tex clear={.tex=(const tex_t *)&textures[0],.r={0,0,0,16,16,1},.mask=15,.c.f={0,0,0,1}};
  vk_texture_clear(&clear);clear.tex=(const tex_t *)&textures[1];clear.r.width=clear.r.height=4;
  memcpy(clear.c.f,(float[]){0,1,.5f,1},16);vk_texture_clear(&clear);
  const float grid[]={.125f,.125f,.25f,.25f};mtl_ui_ghost(1,.25f,.25f,grid,.75f,.75f);
  p_draw draw={.prim=NVN_DRAW_PRIMITIVE_TRIANGLE_STRIP,.count=4};vk_draw(&draw,NULL);
  CHECK(vk_render_active());uint64_t pass=V.passes,draws=V.draws;
  // Outer state uses another physical offset; the nested original ghost must
  // bind its saved A, restore S, then the outer draw must bind B in this same
  // secondary. The three disjoint icons make stale cache state visible.
  S.vertex[0].address=(uintptr_t)storage+(4*65536-16);vk_pipeline_state_changed();
  // The outer VBO crosses an existing window boundary. End the original
  // physical pass for maintenance without losing the pending ghost, migrate,
  // replay that original ghost, then record this outer draw in the new pass.
  vk_draw(&draw,NULL);
  // With no existing full-area job, the shifted ghost first creates its
  // constrained-area pass; restoring the outer viewport needs a second pass.
  // Both still preserve the original attachment contents and command order.
  CHECK(V.draws==draws+2 && V.passes==pass+2 && vk_render_active());
  vk_pool_span_info span;
  CHECK(vk_pool_span_query(S.vertex[0].address,80,&span)==VK_POOL_SPAN_SINGLE);
  CHECK(span.logical_begin==0 && span.logical_end==(1<<20));
  float rect[4];CHECK(mtl_ui_ghost_rect(rect));mtl_ui_ghost(0,0,0,NULL,0,0);
  // Original empty draws still bind descriptors/vertex/index state. Preserve
  // that execution, but do not trigger whole-REGISTRY fallback merely for an
  // address-only VBO or an index buffer with no actual index fetch.
  uint64_t fallbacks=vk_pool_window_fallbacks();S.vertex[0].size=0;
  p_draw empty=draw;empty.count=0;vk_draw(&empty,NULL);
  p_draw_idx empty_indices={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.itype=NVN_INDEX_TYPE_UNSIGNED_INT,
    .count=0,.instances=1,.ibuf=(uintptr_t)storage+6*65536};
  vk_draw(NULL,&empty_indices);CHECK(vk_pool_window_fallbacks()==fallbacks);
  vk_internal_wait(vk_internal_commit());
  uint8_t pixels[1024];NVNcopyRegion region={0,0,0,16,16,1};CALL(nvnTextureReadTexels,&textures[0],NULL,&region,pixels);
  for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++) {
    int visible=(x>=2&&x<6&&y>=2&&y<6)||(x>=10&&x<14&&y>=2&&y<6)||(x>=10&&x<14&&y>=10&&y<14);
    CHECK(!memcmp(pixels+(y*16+x)*4,visible?(uint8_t[]){0,255,128,255}:(uint8_t[]){0,0,0,255},4));
  }
  S=saved;vk_pipeline_state_changed();mtl_set_tex_pool_n(4);
  CALL(nvnProgramFinalize,&program);for(unsigned n=0;n<2;n++)CALL(nvnTextureFinalize,&textures[n]);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);
  LOG("[vulkan production pool windows] pending original ghost survives explicit pass/submission/whole migration before restored outer draw");
}

static void pool_window_production_test(const void *vertex,const void *uniform,
    const void *readonly,const void *compute) {
  enum {window=65536,bytes=4<<20};
  uint64_t old_window=V.options.pool_window_bytes,full,used,protected;
  int reference;vk_pool_sync_policy_stats(&full,&used,&protected,&reference);
  vk_pool_sync_reference(0);mtl_finish();
  CHECK(!V.cmd);vk_test_pool_window_bytes(window);
  uint8_t *source,*dest;
  CHECK(!posix_memalign((void **)&source,window,bytes));
  CHECK(!posix_memalign((void **)&dest,window,bytes));
  dm_guest_write_prepare(source,bytes);dm_guest_write_prepare(dest,bytes);
  for(unsigned i=0;i<bytes;i++) {source[i]=(uint8_t)(i*19+(i/4096)*3+11);dest[i]=0xa7;}
  mtl_pool_add(source,bytes);mtl_pool_add(dest,bytes);
  uint64_t fence=mtl_last_fence();mtl_sub_begin();vk_stream_begin();
  mtl_sub_copy((uintptr_t)source+2*window+8,(uintptr_t)dest+10*window+256,256);
  vk_pool_span_info a,b;
  CHECK(vk_pool_span_query((uintptr_t)source+2*window+8,256,&a)==VK_POOL_SPAN_SINGLE);
  CHECK(vk_pool_span_query((uintptr_t)dest+10*window+256,256,&b)==VK_POOL_SPAN_SINGLE);
  CHECK(a.logical_begin==2*window && a.logical_end==3*window && a.local_offset==8);
  CHECK(b.logical_begin==10*window && b.logical_end==11*window && b.local_offset==256);
  CHECK(memcmp(source+2*window+8,dest+10*window+256,256)!=0); // GPU copy has not published
  uint8_t expected[32];memcpy(expected,source+2*window-4,sizeof expected);
  uint32_t gpu=0x98ab76cd;for(unsigned k=12;k<28;k++)expected[k]=((uint8_t *)&gpu)[(k-12)%4];
  pool_window_fill(source+2*window+8,16,gpu); // no guest/shadow publication
  // A new CPU write in an unbacked page and an older GPU-only writer must
  // both survive migration. Later uploads must not execute before its copy.
  dm_guest_write_prepare(source+2*window-4,4);memcpy(source+2*window-4,(uint8_t[]){1,3,5,7},4);
  memcpy(expected,source+2*window-4,4);
  mtl_sub_copy((uintptr_t)source+2*window-4,(uintptr_t)dest+10*window+1024,sizeof expected);
  CHECK(mtl_last_fence()==fence);
  CHECK(vk_pool_span_query((uintptr_t)source+2*window-4,sizeof expected,&a)==VK_POOL_SPAN_SINGLE);
  CHECK(a.logical_begin==0&&a.logical_end==bytes&&a.handle!=b.handle);
  vk_stream_end();vk_internal_wait(vk_internal_commit());
  CHECK(!memcmp(expected,dest+10*window+1024,sizeof expected));
  // Full reference must migrate the unrelated partial destination BEFORE
  // begin_submission's eager sync, not discover its holes during a draw.
  vk_pool_sync_reference(1);mtl_sub_begin();
  CHECK(vk_pool_span_query((uintptr_t)dest+10*window+256,256,&b)==VK_POOL_SPAN_SINGLE);
  CHECK(b.logical_begin==0&&b.logical_end==bytes);
  vk_internal_wait(vk_internal_commit());vk_pool_sync_reference(0);
  mtl_pool_del(source);mtl_pool_del(dest);free(source);free(dest);

  // Image CPU staging must not materialize its texture-only window. Only the
  // actual B2T/T2B buffer intervals acquire canonical pooled buffers.
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,window,bytes));
  dm_guest_write_prepare(storage,bytes);memset(storage,0,bytes);
  for(unsigned k=0;k<64;k++)storage[3*window+32+k]=(uint8_t)(k*3+7);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;NVNtextureBuilder tb;NVNtexture texture;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,bytes);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&tb,4);
  CALL(nvnTextureBuilderSetHeight,&tb,4);CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);
  CALL(nvnTextureBuilderSetStorage,&tb,&pool,0);CHECK(CALL(nvnTextureInitialize,&texture,&tb));
  p_copy_b2t copy={.src=(uintptr_t)storage+3*window+32,.tex=(const tex_t *)&texture,.r={0,0,0,4,4,1}};
  mtl_sub_begin();vk_texture_copy_b2t(&copy,0,0);
  copy.src=(uintptr_t)storage+7*window+64;vk_texture_copy_t2b(&copy,0,0);
  CHECK(vk_pool_span_query((uintptr_t)storage,64,&a)!=VK_POOL_SPAN_SINGLE);
  CHECK(vk_pool_span_query((uintptr_t)storage+3*window+32,64,&a)==VK_POOL_SPAN_SINGLE&&a.local_offset==32);
  CHECK(vk_pool_span_query((uintptr_t)storage+7*window+64,64,&b)==VK_POOL_SPAN_SINGLE&&b.local_offset==64);
  vk_internal_wait(vk_internal_commit());CHECK(!memcmp(storage+3*window+32,storage+7*window+64,64));
  CALL(nvnTextureFinalize,&texture);CALL(nvnMemoryPoolFinalize,&pool);free(storage);
  LOG("[vulkan production pool windows] transfer promotion preserves GPU-only bytes and new uploads; stream/fence/full-reference/image-copy contracts passed");
  // These are the existing strict full-pixel/byte assertions, not altered
  // oracle expectations. Includes UPDATE_UB, compute, writable alignment
  // copyback, texel aliases, VBO/index and original nested ghost replay.
  // The legacy oracle's early loops use the global command recorder. Supply
  // independent command storage, then restore it, rather than exhaust the
  // original full-suite arena by running the same strict oracle twice.
  NVNcommandBuffer saved_cmd=cmd;NVNmemoryPool command_pool;uint8_t *commands;
  CHECK(!posix_memalign((void **)&commands,65536,8<<20));
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,commands,8<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&command_pool,&mb));
  CHECK(CALL(nvnCommandBufferInitialize,&cmd,&device));
  CALL(nvnCommandBufferAddCommandMemory,&cmd,&command_pool,0,8<<20);
  buffer_binding_byte_test(vertex,uniform,readonly,compute);
  CALL(nvnCommandBufferFinalize,&cmd);cmd=saved_cmd;
  CALL(nvnMemoryPoolFinalize,&command_pool);free(commands);
  uint8_t ui_vertex[0x880],ui_fragment[0x880];memset(ui_vertex,12,sizeof ui_vertex);memset(ui_fragment,4,sizeof ui_fragment);
  vertex_index_binding_test(ui_vertex,ui_fragment);
  pool_window_ghost_promotion_test(ui_vertex,ui_fragment);
  CHECK(!V.cmd);mtl_finish();
  vk_test_pool_window_bytes(old_window);vk_pool_sync_reference(reference);
  LOG("[vulkan production pool windows] original graphics/compute/UBO/SSBO/typed-texel/vertex/index/ghost strict GPU oracles passed; policy restored");
}
#else
static void pool_window_production_test(const void *v,const void *u,const void *r,const void *c) {
  (void)v;(void)u;(void)r;(void)c;
  LOG("[vulkan production pool windows] runtime fixture selector unavailable in standalone target");
}
#endif
