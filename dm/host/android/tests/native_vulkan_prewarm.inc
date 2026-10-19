// The runner supplies one cache directory across independent cold/warm/corrupt
// processes. GPU pixels are always checked; expected cache behavior is opt-in
// so ordinary mobile/full-suite execution does not depend on disk history.
#include "../../../gfx/vulkan/nvn_vk_prewarm.h"
static int prewarm_fixture_setting(const char *name) {
  const char *s=getenv(name);if(!s)return -1;
  CHECK(!strcmp(s,"0")||!strcmp(s,"1"));return !strcmp(s,"1");
}
static void pipeline_prewarm_test(const void *vertex,const void *fragment) {
  vk_state saved=S;
  int expect=prewarm_fixture_setting("DM_TEST_PREWARM_EXPECT_WARM");
  int expect_reject=prewarm_fixture_setting("DM_TEST_PREWARM_EXPECT_REJECT");
  int old_dynamic=vk_dynamic_uniform_reference_mode();
  int old_narrow=vk_dynamic_uniform_narrow_reference_mode();
  uint64_t special[4];int old_special;
  vk_uniform_specialization_stats(special,special+1,special+2,special+3,&old_special);
  vk_uniform_specialization_enabled(0);
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,393216));memset(storage,0,393216);
  memcpy(storage,(float[]){.5f,.25f,.75f,1.f},16);
  memcpy(storage+65536,(float[]){.25f,0,0,0},16);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,393216);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtextureBuilder tb;NVNtexture texture;
  CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);
  CALL(nvnTextureBuilderSetWidth,&tb,8);CALL(nvnTextureBuilderSetHeight,&tb,8);
  CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);CALL(nvnTextureBuilderSetStorage,&tb,&pool,262144);
  CHECK(CALL(nvnTextureInitialize,&texture,&tb));
  uint8_t extended_control[0x880];memset(extended_control,47,sizeof extended_control);
  NVNprogram programs[2];
  for(unsigned n=0;n<2;n++) {
    NVNshaderData shaders[]={{.control=vertex},{.control=n?extended_control:fragment}};
    CHECK(CALL(nvnProgramInitialize,&programs[n],&device));
    CHECK(CALL(nvnProgramSetShaders,&programs[n],2,shaders));
  }
  NVNpolygonState poly;CALL(nvnPolygonStateSetDefaults,&poly);CALL(nvnPolygonStateSetCullFace,&poly,NVN_FACE_NONE);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);
  CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);
  CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);CALL(nvnColorStateSetBlendEnable,&color,0,1);
  NVNblendState blend;CALL(nvnBlendStateSetDefaults,&blend);CALL(nvnBlendStateSetBlendTarget,&blend,0);
  CALL(nvnBlendStateSetBlendFunc,&blend,NVN_BLEND_FUNC_CONSTANT_COLOR,NVN_BLEND_FUNC_ZERO,NVN_BLEND_FUNC_ONE,NVN_BLEND_FUNC_ZERO);
  uint64_t recorded0,warmed0,rejected0,hits0;
  vk_prewarm_stats(&recorded0,&warmed0,&rejected0,&hits0);
  uint32_t pipeline0=vk_graphics_pipeline_count();
  // Static, narrow dynamic, and extended dynamic (two distinct UBO bindings),
  // then repeat the exact static PSO. Distinct blend constants give these owned
  // states an identity independent of the ordinary color fixture.
  unsigned cases=expect==1?5:4;
  for(unsigned test=0;test<cases;test++) {
    int dynamic=test==1?1:test==2?2:0;
    vk_dynamic_uniform_reference(dynamic==0);vk_dynamic_uniform_narrow_reference(dynamic!=2);
    mtl_sub_begin();
    S=(vk_state){.program=(const prog_t *)&programs[test==2],.targets={.n=1,.color={(const tex_t *)&texture}},
      .depth_range={0,1},.chmask={15},.has_viewport=1,.has_scissor=1};
    S.poly=*(const poly_st *)&poly;S.ds=*(const ds_st *)&ds;S.color=*(const color_st *)&color;
    S.blend[0]=*(const blend_st *)&blend;
    S.viewport[0][2]=S.viewport[0][3]=8;S.scissor=(p_scissor){0,0,8,8};
    memcpy(S.blend_color,(float[]){.25f,.5f,.75f,1.f},16);
    if(test==4) {
      // Not executed in the cold generation process. Even with a warmed
      // module/layout, a new immutable blend constant must create a new PSO.
      S.blend_color[0]=.5625f;S.blend_color[1]=.1875f;S.blend_color[2]=.6875f;
    }
    S.ubo[1][0]=(p_bind_buf){.stage=1,.index=0,.addr=(uintptr_t)storage,.size=65536};
    S.ubo[1][5]=(p_bind_buf){.stage=1,.index=5,.addr=(uintptr_t)storage+65536,.size=65536};
    vk_pipeline_state_changed();
    p_clear_color clear={.mask=15};memcpy(clear.rgba,(float[]){0,0,1,1},16);
    vk_target_clear_color(&S.targets,NULL,&clear);
    uint32_t before=vk_graphics_pipeline_count();uint64_t r,w,j,h;
    vk_prewarm_stats(&r,&w,&j,&h);
    p_draw draw={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};vk_draw(&draw,NULL);
    vk_shader *resolved[6];vk_program_shaders(S.program,resolved);
    CHECK(vk_program_dynamic_uniforms(resolved)==dynamic);
    vk_internal_wait(vk_internal_commit());
    if(test==3)CHECK(vk_graphics_pipeline_count()==before);
    if(expect==1 && test<4)CHECK(vk_graphics_pipeline_count()==before);
    if(test==4)CHECK(vk_graphics_pipeline_count()==before+1);
    uint8_t pixels[256];NVNcopyRegion region={0,0,0,8,8,1};
    CALL(nvnTextureReadTexels,&texture,NULL,&region,pixels);
    const float source[]={.5f,.25f,test==2?.5f:.75f,1.f};
    for(unsigned n=0;n<64;n++)for(unsigned c=0;c<4;c++) {
      float factor=c==3?1.f:S.blend_color[c];int value=(int)lrintf(source[c]*factor*255.f);
      if(abs((int)pixels[n*4+c]-value)>1)
        LOG("[vulkan prewarm fixture mismatch] case=%u pixel=%u channel=%u actual=%u expected=%d",test,n,c,pixels[n*4+c],value);
      CHECK(abs((int)pixels[n*4+c]-value)<=1);
    }
    uint64_t r1,w1,j1,h1;vk_prewarm_stats(&r1,&w1,&j1,&h1);
    if(expect==1 && test<3)CHECK(h1>h);
    LOG("[vulkan prewarm fixture] case=%u dynamic=%d PSO=%u->%u warm_hits=%llu->%llu pixels passed",test,dynamic,before,vk_graphics_pipeline_count(),(unsigned long long)h,(unsigned long long)h1);
  }
  uint64_t recorded,warmed,rejected,hits;vk_prewarm_stats(&recorded,&warmed,&rejected,&hits);
  if(expect==1) { CHECK(warmed>0);CHECK(hits>hits0);CHECK(vk_graphics_pipeline_count()==pipeline0+1); }
  if(expect==0)CHECK(warmed==0);
  if(expect_reject==1)CHECK(rejected>0);
  vk_pipeline_cache_checkpoint();
  for(unsigned n=0;n<2;n++)CALL(nvnProgramFinalize,&programs[n]);
  CALL(nvnTextureFinalize,&texture);CALL(nvnMemoryPoolFinalize,&pool);free(storage);
  S=saved;vk_pipeline_state_changed();vk_uniform_specialization_enabled(old_special);
  vk_dynamic_uniform_reference(old_dynamic);vk_dynamic_uniform_narrow_reference(old_narrow);
  LOG("[vulkan prewarm fixture] real static/narrow/extended/blend pixels, repeated PSO, expected_warm=%d expected_reject=%d recorded=%llu warmed=%llu rejected=%llu hits=%llu passed",expect,expect_reject,(unsigned long long)recorded,(unsigned long long)warmed,(unsigned long long)rejected,(unsigned long long)hits);
}
