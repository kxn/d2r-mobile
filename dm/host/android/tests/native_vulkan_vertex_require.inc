// Owned GPU oracle for draw-local VBO requirement reuse. GNU ld wraps the real
// dependency function in the Linux fixture only; Android/production has no
// observer, test hook, counter, reference option or timer.
#if defined(DM_VULKAN_TEST_EMBED) && defined(DM_VULKAN_VERTEX_REQUIRE_WRAP)
void __real_vk_buffer_vertex_require(uint64_t address,uint64_t size);
static const prog_t *vertex_req_observed_program;
static unsigned vertex_req_calls;
static uint64_t vertex_req_addresses[16],vertex_req_sizes[16];
void __wrap_vk_buffer_vertex_require(uint64_t address,uint64_t size) {
  if(vertex_req_observed_program && S.program==vertex_req_observed_program) {
    CHECK(vertex_req_calls<16);vertex_req_addresses[vertex_req_calls]=address;
    vertex_req_sizes[vertex_req_calls++]=size;
  }
  __real_vk_buffer_vertex_require(address,size);
}
static PFN_vkCmdEndRenderPass vertex_req_end_original;
static vk_buffer *vertex_req_late_buffer;
static uint64_t vertex_req_late_offset,vertex_req_late_address;
static const float *vertex_req_late_data;
static unsigned vertex_req_late_writes;
static VKAPI_ATTR void VKAPI_CALL vertex_req_end(VkCommandBuffer command) {
  vertex_req_end_original(command);
  if(vertex_req_late_writes)return;
  vertex_req_late_writes++;
  // A real late TRANSFER writer follows the actual pass (and original ghost)
  // execution. B lives in another physical pool, so its named barrier cannot
  // accidentally make A visible. Never recurse into engine pass-end here.
  VkMemoryBarrier dependency={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT};
  vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
      0,1,&dependency,0,NULL,0,NULL);
  vkCmdUpdateBuffer(command,vertex_req_late_buffer->handle,vertex_req_late_offset,120,vertex_req_late_data);
  vk_buffer_transfer_written_range(vertex_req_late_address,120);
  // A may already have a COMPUTE writeback queued; the later transfer is
  // another actual producer of that snapshot, as in ordinary copy/update.
  vk_buffer_writeback_stage(vertex_req_late_address,120,VK_PIPELINE_STAGE_TRANSFER_BIT);
}
static void vertex_req_update(uint64_t address,const float data[30]) {
  vk_buffer *buffer;uint64_t offset;
  CHECK(vk_pool_address_overwrite(address,120,(void **)&buffer,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
  const vk_buffer_use use={address,120,1};vk_transfer_buffer_dependencies(&use,1);
  vkCmdUpdateBuffer(V.cmd,buffer->handle,offset,120,data);vk_pool_overwritten(address,120);
  vk_buffer_transfer_written_range(address,120);
}
static void vertex_require_dedup_test(void) {
  vk_state saved=S;mtl_finish();
  uint8_t *memory[2];NVNmemoryPool pool[2];
  float a[30],b[30],late[30];const float positions[6]={-1,-1,3,-1,-1,3};
  for(unsigned v=0;v<3;v++) {
    memcpy(a+v*10,positions+v*2,8);memcpy(a+v*10+2,(float[]){.25f,0,0,1,0,0,.25f,0},32);
    memcpy(b+v*10,positions+v*2,8);memcpy(b+v*10+2,(float[]){0,.25f,0,0,0,0,0,0},32);
  }
  memcpy(late,a,sizeof a);for(unsigned v=0;v<3;v++)late[v*10+2]=.5f;
  for(unsigned n=0;n<2;n++) {
    CHECK(!posix_memalign((void **)&memory[n],65536,1<<20));memset(memory[n],0,1<<20);
    memcpy(memory[n]+0x10000,n?b:a,120);memcpy(memory[n]+0x20000,a,120);
    memcpy(memory[n]+0x40000,n?b:late,120);
    NVNmemoryPoolBuilder p;CALL(nvnMemoryPoolBuilderSetDefaults,&p);CALL(nvnMemoryPoolBuilderSetDevice,&p,&device);
    CALL(nvnMemoryPoolBuilderSetStorage,&p,memory[n],1<<20);
    CALL(nvnMemoryPoolBuilderSetFlags,&p,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
    CHECK(CALL(nvnMemoryPoolInitialize,&pool[n],&p));
  }
  const float quad[]={-.75f,-.75f,0,0,0,-.25f,-.75f,0,1,0,-.75f,-.25f,0,0,1,-.25f,-.25f,0,1,1};
  memcpy(memory[0]+0x30000,quad,sizeof quad);float *matrix=(float *)(memory[0]+0x50000);
  matrix[0]=matrix[5]=matrix[10]=matrix[15]=1;
  uint8_t control[5][0x880];const unsigned markers[]={65,66,12,4,26};
  for(unsigned n=0;n<5;n++)memset(control[n],markers[n],0x880);
  NVNprogram program,ui,compute;NVNshaderData shaders[2]={{.control=control[0]},{.control=control[1]}};
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  shaders[0].control=control[2];shaders[1].control=control[3];
  CHECK(CALL(nvnProgramInitialize,&ui,&device));CHECK(CALL(nvnProgramSetShaders,&ui,2,shaders));
  shaders[0].control=control[4];CHECK(CALL(nvnProgramInitialize,&compute,&device));CHECK(CALL(nvnProgramSetShaders,&compute,1,shaders));
  NVNtexture textures[2];for(unsigned n=0;n<2;n++) {
    NVNtextureBuilder t;CALL(nvnTextureBuilderSetDefaults,&t);CALL(nvnTextureBuilderSetDevice,&t,&device);
    CALL(nvnTextureBuilderSetTarget,&t,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&t,n?4:16);
    CALL(nvnTextureBuilderSetHeight,&t,n?4:16);CALL(nvnTextureBuilderSetFormat,&t,NVN_FORMAT_RGBA8);
    CALL(nvnTextureBuilderSetStorage,&t,&pool[0],0x60000+n*65536);CHECK(CALL(nvnTextureInitialize,&textures[n],&t));
  }
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState depth;CALL(nvnDepthStencilStateSetDefaults,&depth);CALL(nvnDepthStencilStateSetDepthTestEnable,&depth,0);
  CALL(nvnDepthStencilStateSetDepthWriteEnable,&depth,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&depth,0);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);
  NVNvertexAttribState attr[4];for(unsigned n=0;n<4;n++) {
    CALL(nvnVertexAttribStateSetDefaults,&attr[n]);CALL(nvnVertexAttribStateSetStreamIndex,&attr[n],n==2?1:0);
    CALL(nvnVertexAttribStateSetFormat,&attr[n],n?NVN_FORMAT_RGBA32F:NVN_FORMAT_RG32F,n==3?24:n?8:0);
  }
  NVNvertexStreamState stream;CALL(nvnVertexStreamStateSetDefaults,&stream);CALL(nvnVertexStreamStateSetStride,&stream,40);
  mtl_set_tex_pool_n(256);mtl_reg_tex(77,(const tex_t *)&textures[1],NULL);
  mtl_sub_begin();p_clear_tex clear={.tex=(const tex_t *)&textures[1],.r={0,0,0,4,4,1},.mask=15,.c.f={0,1,.5f,1}};
  vk_texture_clear(&clear);clear.tex=(const tex_t *)&textures[0];clear.r.width=clear.r.height=16;vk_texture_clear(&clear);
  vk_texture_prepare_attachment((const tex_t *)&textures[0],NULL);
  vk_internal_wait(vk_internal_commit());
  const char *reference=getenv("DM_TEST_VERTEX_REQUIRE_REFERENCE");int old=reference && !strcmp(reference,"1");
  int named_reference=vk_buffer_memory_reference_state();
  for(unsigned test=0;test<10;test++) {
    uint64_t address=(uintptr_t)memory[0]+(test==3?0x20000:0x10000),other=(uintptr_t)memory[1]+0x10000;
    mtl_sub_begin();vertex_req_update(address,a);vertex_req_update(other,b);
    if(test==4 || test==9)vertex_req_update(address,late);
    if(test>=5 && test<=7) {
      S.program=(const prog_t *)&compute;S.ssbo[5][0]=(p_bind_buf){5,0,(uintptr_t)memory[test==5?0:1]+0x40000,120};
      S.ssbo[5][1]=(p_bind_buf){5,1,test==5?address:other,120};vk_dispatch(&(p_dispatch){1,1,1});
      if(test==7) {S.ssbo[5][0].addr=(uintptr_t)memory[0]+0x20000;S.ssbo[5][1].addr=address;vk_dispatch(&(p_dispatch){1,1,1});}
    }
    if(test==8) {
      vk_buffer *buffer;uint64_t offset;CHECK(vk_pool_address_overwrite((uintptr_t)memory[1]+0x80000,8192,(void **)&buffer,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
      vk_barrier();for(unsigned n=0;n<257;n++) {vkCmdFillBuffer(V.cmd,buffer->handle,offset+n*16,4,0x81000000+n);vk_buffer_transfer_written_range((uintptr_t)memory[1]+0x80000+n*16,4);}
      CHECK(vk_buffer_vertex_pending(address,120));
    }
    S=(vk_state){.program=(const prog_t *)&program,.depth_range={0,1},.chmask={~0u}};
    S.targets=(p_set_rt){.n=1,.color={(const tex_t *)&textures[0]}};
    S.poly=*(const poly_st *)&polygon;S.ds=*(const ds_st *)&depth;S.color=*(const color_st *)&color;
    for(unsigned n=0;n<4;n++)S.attr[n]=*(const va_st *)&attr[n];
    S.stream[0]=S.stream[1]=*(const vs_st *)&stream;
    S.vertex[0]=(NVNbufferRange){address,120};S.vertex[1]=(NVNbufferRange){other,120};
    if(test==1)S.vertex[1]=S.vertex[0];
    if(test==2) {S.vertex[1]=(NVNbufferRange){address+8,112};S.attr[2].offset=0;}
    S.has_viewport=S.has_scissor=1;S.viewport[0][2]=S.viewport[0][3]=16;S.scissor=(p_scissor){0,0,16,16};vk_pipeline_state_changed();
    vk_state outer=S;uint64_t before_ghost=0;
    if(test==6 || test==7) {
      // First A is already visible only in case6; B remains pending in a
      // separate VkBuffer. Case7 makes first A close/replay a real ghost.
      if(test==6)__real_vk_buffer_vertex_require(address,120);
      CHECK(vk_buffer_vertex_pending(other,120));
      S.program=(const prog_t *)&ui;S.attr[0]=(va_st){0};S.attr[1]=(va_st){0};
      NVNvertexAttribState u[2];for(unsigned n=0;n<2;n++) {CALL(nvnVertexAttribStateSetDefaults,&u[n]);CALL(nvnVertexAttribStateSetFormat,&u[n],n?NVN_FORMAT_RG32F:NVN_FORMAT_RGB32F,n?12:0);S.attr[n]=*(const va_st *)&u[n];}
      NVNvertexStreamState us;CALL(nvnVertexStreamStateSetDefaults,&us);CALL(nvnVertexStreamStateSetStride,&us,20);S.stream[0]=*(const vs_st *)&us;
      S.vertex[0]=(NVNbufferRange){(uintptr_t)memory[0]+0x30000,80};S.ubo[0][0]=(p_bind_buf){0,0,(uintptr_t)matrix,64};S.tex[1][0]=77;vk_pipeline_state_changed();
      const float grid[]={.125f,.125f,.25f,.25f};mtl_ui_ghost(1,.25f,.25f,grid,.75f,.75f);
      vk_draw(&(p_draw){.prim=NVN_DRAW_PRIMITIVE_TRIANGLE_STRIP,.count=4},NULL);CHECK(vk_render_active());
      before_ghost=V.draws;S=outer;vk_pipeline_state_changed();
      CHECK(vk_pool_address_stage(address,120,(void **)&vertex_req_late_buffer,&vertex_req_late_offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
      vertex_req_late_address=address;vertex_req_late_data=late;vertex_req_late_writes=0;
      vertex_req_end_original=vkCmdEndRenderPass;vkCmdEndRenderPass=vertex_req_end;
    }
    if(test==9)vk_buffer_memory_reference(1);
    vertex_req_observed_program=(const prog_t *)&program;vertex_req_calls=0;
    vk_draw(&(p_draw){.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3},NULL);
    vertex_req_observed_program=NULL;
    if(test==9)vk_buffer_memory_reference(named_reference);
    if(test==6 || test==7) {
      vkCmdEndRenderPass=vertex_req_end_original;CHECK(vertex_req_late_writes==1);CHECK(V.draws==before_ghost+2);
      float rect[4];CHECK(mtl_ui_ghost_rect(rect));mtl_ui_ghost(0,0,0,NULL,0,0);
    }
    unsigned expected=old?4:test==1?1:test==7?4:3;
    LOG("[vulkan vertex require fixture] case=%u reference=%d actual_require_calls=%u expected=%u",test,old,vertex_req_calls,expected);
    CHECK(vertex_req_calls==expected);CHECK(!vk_buffer_vertex_pending(address,120));
    for(unsigned n=0;n<vertex_req_calls;n++)CHECK(vertex_req_sizes[n]>0);
    vk_internal_wait(vk_internal_commit());
    NVNcopyRegion region={0,0,0,16,16,1};uint8_t pixels[1024];CALL(nvnTextureReadTexels,&textures[0],NULL,&region,pixels);
    const unsigned expected_rgb[3]={test==1 || test==2 || (test>=4 && test<=7) || test==9?128:64,test==1 || test==2?0:64,64};
    for(unsigned p=0;p<256;p++)for(unsigned c=0;c<4;c++)CHECK(abs((int)pixels[p*4+c]-(int)(c==3?255:expected_rgb[c]))<=1);
  }
  S=saved;vk_pipeline_state_changed();mtl_set_tex_pool_n(4);
  CALL(nvnProgramFinalize,&program);CALL(nvnProgramFinalize,&ui);CALL(nvnProgramFinalize,&compute);
  for(unsigned n=0;n<2;n++)CALL(nvnTextureFinalize,&textures[n]);
  for(unsigned n=0;n<2;n++){CALL(nvnMemoryPoolFinalize,&pool[n]);free(memory[n]);}
  CHECK(!vk_shader_operation_active());CHECK(!vk_texture_shader_collecting());
  LOG("[vulkan vertex require fixture] actual VBO attributes, reused/aliased streams, per-draw retarget, transfer/compute GPU writers, original late ghost plus newer writer, overflow and global reference pixels passed");
}
#else
static void vertex_require_dedup_test(void) {}
#endif
