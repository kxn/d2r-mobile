// Actual compute/transfer writers and fragment consumers, including a writer
// introduced after render-end. The observer is host-only, with no runtime cost.
#ifdef DM_VULKAN_TEST_EMBED
extern int vk_test_fragment_buffer_pending(uint64_t,uint64_t);
#ifdef DM_VULKAN_FS_QUERY_WRAP
void __real_vk_render_end_at(const char *,unsigned);
static void (*fs_query_after_end)(void);
void __wrap_vk_render_end_at(const char *file,unsigned line) {
  __real_vk_render_end_at(file,line);
  if(fs_query_after_end) {
    void (*callback)(void)=fs_query_after_end;fs_query_after_end=NULL;callback();
  }
}
static vk_buffer *fs_query_late_buffer;
static uint64_t fs_query_late_offset,fs_query_late_address;
static unsigned fs_query_late_calls;
static void fs_query_late_write(void) {
  const float blue[]={0,0,1,1};
  const vk_buffer_use use={fs_query_late_address,16,1};vk_transfer_buffer_dependencies(&use,1);
  vkCmdUpdateBuffer(V.cmd,fs_query_late_buffer->handle,fs_query_late_offset,16,blue);
  vk_pool_overwritten(fs_query_late_address,16);
  vk_buffer_transfer_written_range(fs_query_late_address,16);
  vk_buffer_writeback_stage(fs_query_late_address,16,VK_PIPELINE_STAGE_TRANSFER_BIT);fs_query_late_calls++;
}
#endif
static void fragment_buffer_query_test(void) {
  vk_state saved=S;mtl_finish();
  uint8_t *memory;CHECK(!posix_memalign((void **)&memory,65536,1<<20));memset(memory,0,1<<20);
  const float positions[]={-1,-1,3,-1,-1,3},red[]={1,0,0,1};
  memcpy(memory+0x10000,positions,sizeof positions);memcpy(memory+0x20000,red,16);
  NVNmemoryPoolBuilder pb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&pb);CALL(nvnMemoryPoolBuilderSetDevice,&pb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&pb,memory,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&pb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&pb));
  uint8_t controls[3][0x880];unsigned markers[]={24,5,26};
  for(unsigned n=0;n<3;n++)memset(controls[n],markers[n],0x880);
  NVNprogram graphics,compute;NVNshaderData sd[2]={{.control=controls[0]},{.control=controls[1]}};
  CHECK(CALL(nvnProgramInitialize,&graphics,&device));CHECK(CALL(nvnProgramSetShaders,&graphics,2,sd));
  sd[0].control=controls[2];CHECK(CALL(nvnProgramInitialize,&compute,&device));CHECK(CALL(nvnProgramSetShaders,&compute,1,sd));
  NVNtextureBuilder tb;NVNtexture target;
  CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&tb,16);
  CALL(nvnTextureBuilderSetHeight,&tb,16);CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);
  CALL(nvnTextureBuilderSetStorage,&tb,&pool,0x60000);CHECK(CALL(nvnTextureInitialize,&target,&tb));
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState depth;CALL(nvnDepthStencilStateSetDefaults,&depth);
  CALL(nvnDepthStencilStateSetDepthTestEnable,&depth,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&depth,0);
  CALL(nvnDepthStencilStateSetStencilTestEnable,&depth,0);NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);
  int reference=vk_buffer_memory_reference_state();
  uint64_t address=(uintptr_t)memory+0x30000;
  for(unsigned test=0;test<6;test++) {
#ifndef DM_VULKAN_FS_QUERY_WRAP
    if(test==2)continue;
#endif
    mtl_sub_begin();vk_barrier();vk_buffer_memory_reference(test==5);
    S=(vk_state){.program=(const prog_t *)&compute};
    S.ssbo[5][0]=(p_bind_buf){5,0,(uintptr_t)memory+0x20000,16};
    S.ssbo[5][1]=(p_bind_buf){5,1,address,16};vk_dispatch(&(p_dispatch){1,1,1});
    vk_buffer *buffer;uint64_t offset;
    CHECK(vk_pool_address_stage(address,16,(void **)&buffer,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
    if(test==0 || test==3) {
      // Separate TRANSFER history overlaps a COMPUTE write, in one actual pool.
      const vk_buffer_use use={address+12,4,1};vk_transfer_buffer_dependencies(&use,1);
      vkCmdUpdateBuffer(V.cmd,buffer->handle,offset+12,4,red+3);
      vk_pool_overwritten(address+12,4);vk_buffer_transfer_written_range(address+12,4);
      vk_buffer_writeback_stage(address+12,4,VK_PIPELINE_STAGE_TRANSFER_BIT);
    }
    if(test==4)vk_buffer_shader_read(address,16,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    S=(vk_state){.program=(const prog_t *)&graphics,.depth_range={0,1},.chmask={~0u}};
    S.poly=*(const poly_st *)&polygon;S.ds=*(const ds_st *)&depth;S.color=*(const color_st *)&color;
    S.targets=(p_set_rt){.n=1,.color={(const tex_t *)&target}};
    S.ssbo[0][0]=(p_bind_buf){0,0,(uintptr_t)memory+0x10000,sizeof positions};
    S.ssbo[1][0]=(p_bind_buf){1,0,address,16};S.has_viewport=S.has_scissor=1;
    S.viewport[0][2]=S.viewport[0][3]=16;S.scissor=(p_scissor){0,0,16,16};vk_pipeline_state_changed();
    if(test!=2) {
      p_clear_color clear={.mask=15};memcpy(clear.rgba,(float[]){0,0,0,1},16);
      vk_target_clear_color(&S.targets,&S.scissor,&clear);
      CHECK(vk_render_pending_primary()!=VK_NULL_HANDLE);
    }
#ifdef DM_VULKAN_FS_QUERY_WRAP
    if(test==2) {
      fs_query_late_buffer=buffer;fs_query_late_offset=offset;fs_query_late_address=address;
      fs_query_late_calls=0;fs_query_after_end=fs_query_late_write;
    }
#endif
    vk_buffer_shader_read(address,16,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    CHECK(!vk_test_fragment_buffer_pending(address,16));
#ifdef DM_VULKAN_FS_QUERY_WRAP
    if(test==2)CHECK(fs_query_late_calls==1 && !fs_query_after_end);
#endif
    vk_draw(&(p_draw){.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3},NULL);vk_internal_wait(vk_internal_commit());
    uint8_t pixels[16*16*4];NVNcopyRegion region={0,0,0,16,16,1};CALL(nvnTextureReadTexels,&target,NULL,&region,pixels);
    for(unsigned n=0;n<256;n++) {
      const uint8_t expected[]={test==2?0:255,0,test==2?255:0,255};CHECK(!memcmp(pixels+4*n,expected,4));
    }
  }
  vk_buffer_memory_reference(reference);mtl_finish();CALL(nvnTextureFinalize,&target);
  CALL(nvnProgramFinalize,&graphics);CALL(nvnProgramFinalize,&compute);CALL(nvnMemoryPoolFinalize,&pool);
  free(memory);S=saved;vk_pipeline_state_changed();
  LOG("[fragment buffer query] real mixed writers, active primary, no pending, late pass-end transfer and reference pixels passed");
}
#else
static void fragment_buffer_query_test(void) {}
#endif
