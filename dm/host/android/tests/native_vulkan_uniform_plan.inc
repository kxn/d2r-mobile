// Shared-lazy UBO consumption contract. Original NVN shaders/draw and GPU
// pixels, with real setters and all three dynamic-offset representations.
#if defined(DM_VULKAN_SHARED_IMPORT_WINDOWS) && defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
extern void vk_test_ubo_consumption(uint64_t [3],int *);
extern void vk_test_ubo_before_require(void (*)(void *),void *);
extern unsigned vk_test_pool_uniform_borrowed(void);
static void uniform_plan_commit_callback(void *context) {
  unsigned *calls=context;(*calls)++;
  CHECK(vk_test_pool_uniform_borrowed()>0);
  // Only the UBO group is borrowed here, before original VBO resolution.
  // Commit really flushes an armed original ghost and waits for GPU effects.
  uint64_t serial=vk_recording_serial(),draws=V.draws;
  vk_internal_wait(vk_internal_commit());CHECK(V.draws==draws+1);
  size_t owned,borrowed;vk_test_pool_operation_pin_counts(&owned,&borrowed);
  CHECK(owned>=1 && !borrowed && !vk_test_pool_uniform_borrowed());
  mtl_sub_begin();CHECK(vk_recording_serial()!=serial);
}
static void uniform_plan_gpu_test(void) {
  enum {W=65536};
  CHECK(vk_pool_shared_mode());vk_state saved=S;
  NVNmemoryPool pool,uniform_pool;NVNmemoryPoolBuilder pb;
  CALL(nvnMemoryPoolBuilderSetDefaults,&pb);CALL(nvnMemoryPoolBuilderSetDevice,&pb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&pb,bytes,2*W);
  CALL(nvnMemoryPoolBuilderSetFlags,&pb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&pb));
  CALL(nvnMemoryPoolBuilderSetStorage,&pb,bytes+2*W,(4u<<20)-2*W);
  CHECK(CALL(nvnMemoryPoolInitialize,&uniform_pool,&pb));
  NVNtexture target;NVNtextureBuilder tb;CALL(nvnTextureBuilderSetDefaults,&tb);
  CALL(nvnTextureBuilderSetDevice,&tb,&device);CALL(nvnTextureBuilderSetWidth,&tb,16);
  CALL(nvnTextureBuilderSetHeight,&tb,16);CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);
  CALL(nvnTextureBuilderSetStorage,&tb,&pool,0);CHECK(CALL(nvnTextureInitialize,&target,&tb));
  uint8_t controls[3][0x880];unsigned markers[]={12,47,5};NVNprogram programs[2];
  for(unsigned n=0;n<3;n++)memset(controls[n],markers[n],sizeof controls[n]);
  for(unsigned n=0;n<2;n++) {
    NVNshaderData code[]={{.control=controls[0]},{.control=controls[n+1]}};
    CHECK(CALL(nvnProgramInitialize,&programs[n],&device));CHECK(CALL(nvnProgramSetShaders,&programs[n],2,code));
  }
  const float vertices[]={-1,1,0,0,0,3,1,0,0,0,-1,-3,0,0,0};
  const float identity[]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
  dm_guest_write_prepare(bytes+W,5*W);
  memcpy(bytes+W,vertices,sizeof vertices);memcpy(bytes+2*W,identity,sizeof identity);
  memcpy(bytes+3*W,(float[]){.25f,0,.75f,1},16);
  memcpy(bytes+4*W,(float[]){.75f,0,0,1},16);memcpy(bytes+5*W,(float[]){.5f,0,0,1},16);
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState depth;CALL(nvnDepthStencilStateSetDefaults,&depth);
  CALL(nvnDepthStencilStateSetDepthTestEnable,&depth,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&depth,0);
  CALL(nvnDepthStencilStateSetStencilTestEnable,&depth,0);NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);
  NVNvertexAttribState attributes[2];for(unsigned n=0;n<2;n++) {
    CALL(nvnVertexAttribStateSetDefaults,&attributes[n]);CALL(nvnVertexAttribStateSetStreamIndex,&attributes[n],0);
    CALL(nvnVertexAttribStateSetFormat,&attributes[n],n?NVN_FORMAT_RG32F:NVN_FORMAT_RGB32F,n?12:0);
  }
  NVNvertexStreamState stream;CALL(nvnVertexStreamStateSetDefaults,&stream);CALL(nvnVertexStreamStateSetStride,&stream,20);
  int old_dynamic=vk_dynamic_uniform_reference_mode(),old_narrow=vk_dynamic_uniform_narrow_reference_mode();
  for(unsigned mode=0;mode<3;mode++) {
    vk_dynamic_uniform_reference(mode==0);vk_dynamic_uniform_narrow_reference(mode==1);
    mtl_sub_begin();vk_state state={.depth_range={0,1},.chmask={~0u}};
    state.targets=(p_set_rt){.n=1,.color={(const tex_t *)&target}};
    state.poly=*(const poly_st *)&polygon;state.ds=*(const ds_st *)&depth;state.color=*(const color_st *)&color;
    for(unsigned n=0;n<2;n++)state.attr[n]=*(const va_st *)&attributes[n];
    state.stream[0]=*(const vs_st *)&stream;state.vertex[0]=(NVNbufferRange){(uintptr_t)bytes+W,sizeof vertices};
    state.has_viewport=state.has_scissor=1;state.viewport[0][2]=state.viewport[0][3]=16;
    state.scissor=(p_scissor){0,0,16,16};state.program=(const prog_t *)&programs[0];
    vk_state_restore(&state);vk_pipeline_state_changed();
    p_bind_buf vs={.stage=0,.index=0,.addr=(uintptr_t)bytes+2*W,.size=64};vk_state_op(OP_BIND_UBO,&vs);
    p_bind_buf fs={.stage=1,.index=0,.addr=(uintptr_t)bytes+3*W,.size=16};vk_state_op(OP_BIND_UBO,&fs);
    p_bind_buf storage={.stage=1,.index=0,.addr=(uintptr_t)bytes+3*W+4,.size=16};vk_state_op(OP_BIND_SSBO,&storage);
    p_bind_buf moving={.stage=1,.index=5,.addr=(uintptr_t)bytes+4*W,.size=16};vk_state_op(OP_BIND_UBO,&moving);
    p_draw draw={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.first=0,.count=3};
    uint64_t before[3],after[3];int enabled;vk_test_ubo_consumption(before,&enabled);
    for(unsigned n=0;n<8;n++) {
      // Slot 0 same-value setters must keep both VS/FS stable while slot 5
      // changes every draw. No whole-stage invalidation or offset truncation.
      vk_state_op(OP_BIND_UBO,&vs);vk_state_op(OP_BIND_UBO,&fs);
      moving.addr=(uintptr_t)bytes+(n&1?5:4)*W;vk_state_op(OP_BIND_UBO,&moving);
      if(mode==0 && n==5 && getenv("DM_UNIFORM_PLAN_ALIAS_FATAL")) {
        // A real nonnative optimal-image writer acquires an alias of the
        // formerly proved UBO. The following read must re-enter the existing
        // unsupported-alias error, never reuse its old negative proof.
        NVNtexture alias;NVNtextureBuilder ab;CALL(nvnTextureBuilderSetDefaults,&ab);
        CALL(nvnTextureBuilderSetDevice,&ab,&device);CALL(nvnTextureBuilderSetWidth,&ab,4);
        CALL(nvnTextureBuilderSetHeight,&ab,4);CALL(nvnTextureBuilderSetFormat,&ab,NVN_FORMAT_RGBA8);
        CALL(nvnTextureBuilderSetStorage,&ab,&uniform_pool,W);CHECK(CALL(nvnTextureInitialize,&alias,&ab));
        p_clear_tex ac={.tex=(const tex_t *)&alias,.r={0,0,0,4,4,1},.mask=15,.c.f={1,0,0,1}};
        vk_texture_clear(&ac);CHECK(vk_texture_shared_linear_conflict(fs.addr,fs.size));
      }
      S.scissor=(p_scissor){n*2,0,2,16};vk_draw(&draw,NULL);
    }
    vk_test_ubo_consumption(after,&enabled);
    if(enabled)CHECK(after[0]>before[0] && after[1]>before[1] && after[2]>before[2]);
    else CHECK(!memcmp(before,after,sizeof before));
    vk_internal_wait(vk_internal_commit());
    uint8_t pixels[16*16*4];NVNcopyRegion region={0,0,0,16,16,1};CALL(nvnTextureReadTexels,&target,NULL,&region,pixels);
    for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++) {
      uint8_t *p=pixels+(y*16+x)*4;CHECK(abs((int)p[0]-64)<=1);
      CHECK(abs((int)p[1]-((x/2)&1?128:191))<=1 && abs((int)p[2]-191)<=1 && p[3]==255);
    }
    // Same shader and payload after an actual commit cannot reuse old ledger
    // or owner references. First draw is cold; a later stable draw can qualify.
    mtl_sub_begin();vk_test_ubo_consumption(before,&enabled);vk_draw(&draw,NULL);
    vk_test_ubo_consumption(after,&enabled);CHECK(after[0]==before[0]);
    vk_draw(&draw,NULL);vk_draw(&draw,NULL);vk_test_ubo_consumption(after,&enabled);
    if(enabled)CHECK(after[0]>before[0]);
    // A real GPU transfer changes the same UBO without a setter. Its original
    // dependency/publication chain must run again; check fresh GPU pixels.
    vk_render_end();uint64_t written_offset;
    vk_buffer written=lazy_alias_resolve(bytes+3*W,16,&written_offset);
    lazy_alias_fill(written,written_offset,bytes+3*W,16,0x3f000000u);
    S.scissor=(p_scissor){0,0,16,16};vk_test_ubo_consumption(before,&enabled);vk_draw(&draw,NULL);
    vk_test_ubo_consumption(after,&enabled);CHECK(after[0]==before[0]);
    vk_internal_wait(vk_internal_commit());CALL(nvnTextureReadTexels,&target,NULL,&region,pixels);
    for(unsigned n=0;n<256;n++)CHECK(abs((int)pixels[n*4]-128)<=1 && abs((int)pixels[n*4+1]-128)<=1 &&
        abs((int)pixels[n*4+2]-128)<=1 && pixels[n*4+3]==255);
    dm_guest_write_prepare(bytes+3*W,16);memcpy(bytes+3*W,(float[]){.25f,0,.75f,1},16);
    mtl_sub_begin();vk_draw(&draw,NULL);vk_draw(&draw,NULL);vk_draw(&draw,NULL);
    // A/B/A includes a storage-bias consumer excluded by the candidate. Its
    // push bias and alignment copy stay original; the reused stage array must
    // not leave either UBO ordinal from the old shader ready.
    p_bind_program bp={.prog=(const prog_t *)&programs[1]};vk_state_op(OP_BIND_PROGRAM,&bp);vk_draw(&draw,NULL);
    bp.prog=(const prog_t *)&programs[0];vk_state_op(OP_BIND_PROGRAM,&bp);
    vk_test_ubo_consumption(before,&enabled);vk_draw(&draw,NULL);vk_test_ubo_consumption(after,&enabled);
    // VS can stay ready; FS must fill both schema entries again.
    CHECK(after[0]-before[0]<3);
    vk_internal_wait(vk_internal_commit());
    if(enabled) {
      // This is a real original drag-follow nested draw, not a fabricated
      // revision bump. Keep UBOs in a DIFFERENT Pool from the VBO/attachment,
      // so the operation's ordinary pins cannot accidentally cover the group.
      const float quad[]={-.75f,-.75f,0,0,0,-.25f,-.75f,0,1,0,-.75f,-.25f,0,0,1,-.25f,-.25f,0,1,1};
      dm_guest_write_prepare(bytes+W,sizeof quad);memcpy(bytes+W,quad,sizeof quad);
      mtl_sub_begin();S.vertex[0].size=80;S.scissor=(p_scissor){0,0,16,16};
      p_clear_tex clear={.tex=(const tex_t *)&target,.r={0,0,0,16,16,1},.mask=15,.c.f={0,0,0,1}};
      vk_texture_clear(&clear);
      const float grid[]={.125f,.125f,.25f,.25f};
      mtl_ui_ghost(1,.25f,.25f,grid,.75f,.75f);
      p_draw icon={.prim=NVN_DRAW_PRIMITIVE_TRIANGLE_STRIP,.count=4};
      vk_draw(&icon,NULL);vk_draw(&icon,NULL);vk_draw(&icon,NULL);
      unsigned callbacks=0;uint64_t actual=V.draws;
      vk_test_ubo_before_require(uniform_plan_commit_callback,&callbacks);vk_draw(&icon,NULL);
      CHECK(callbacks==1 && V.draws==actual+2);float ghost_rect[4];CHECK(mtl_ui_ghost_rect(ghost_rect));
      mtl_ui_ghost(0,0,0,NULL,0,0);vk_internal_wait(vk_internal_commit());
      CALL(nvnTextureReadTexels,&target,NULL,&region,pixels);
      for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++) {
        int icon_pixel=(x>=2&&x<6&&y>=2&&y<6)||(x>=10&&x<14&&y>=10&&y<14);
        const uint8_t *p=pixels+(y*16+x)*4;
        if(icon_pixel)CHECK(abs((int)p[0]-64)<=1 && abs((int)p[1]-128)<=1 && abs((int)p[2]-191)<=1 && p[3]==255);
        else CHECK(!memcmp(p,(uint8_t[]){0,0,0,255},4));
      }
      dm_guest_write_prepare(bytes+W,sizeof vertices);memcpy(bytes+W,vertices,sizeof vertices);
    }
    printf("{\"test\":\"uniform-consumption-shared\",\"mode\":%u,\"enabled\":%d,\"result\":\"passed\"}\n",mode,enabled);
  }
  vk_dynamic_uniform_reference(old_dynamic);vk_dynamic_uniform_narrow_reference(old_narrow);
  vk_state_restore(&saved);vk_pipeline_state_changed();
  CALL(nvnTextureFinalize,&target);for(unsigned n=0;n<2;n++)CALL(nvnProgramFinalize,&programs[n]);
  CALL(nvnMemoryPoolFinalize,&uniform_pool);CALL(nvnMemoryPoolFinalize,&pool);CHECK(!vk_validation_error_count());
}
#endif
