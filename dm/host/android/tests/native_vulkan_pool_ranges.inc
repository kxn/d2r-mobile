// Range enumeration must remain logical: even large, unaligned bindings do
// not materialize a GPU mirror or borrow a physical resource during planning.
typedef struct {
  vk_pool_requirement ranges[32];
  unsigned count,capacity;
} pool_range_fixture;
static int pool_range_collect(void *context,const vk_pool_requirement *range) {
  pool_range_fixture *out=context;
  if(out->count==out->capacity)return 0;
  out->ranges[out->count++]=*range;
  return 1;
}
static void shader_pool_ranges_test(void) {
  vk_state saved=S;memset(&S,0,sizeof S);
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,65536));
  memset(storage,0,65536);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,65536);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtexture texture;NVNtextureBuilder tb;
  CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_BUFFER);
  CALL(nvnTextureBuilderSetWidth,&tb,64);CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_R32UI);
  CALL(nvnTextureBuilderSetStorage,&tb,&pool,4096);
  CHECK(CALL(nvnTextureInitialize,&texture,&tb));
  tex_t descriptor=*(const tex_t *)&texture;
  enum {slot=61};mtl_set_tex_pool_n(256);mtl_reg_tex(slot,&descriptor,NULL);
  uint64_t registered,backed,count,materialized;int reference;
  vk_pool_mirror_stats(&registered,&backed,&count,&materialized,&reference);
  vk_binding vb[]={
    {.kind=1,.reg=3,.count=1},
    {.kind=2,.reg=1,.flags=1,.count=1},
    {.kind=1,.reg=0,.count=1},
    {.kind=1,.reg=1,.count=1},
    {.kind=1,.reg=2,.count=1},
    {.kind=3,.reg=0,.count=1},
  };
  vk_binding fb[]={
    {.kind=8,.reg=2,.flags=1,.count=1},
    {.kind=7,.reg=127,.flags=1,.count=1},
    {.kind=9,.reg=31,.flags=4,.count=1},
  };
  vk_shader vertex={.stage=0,.nb=sizeof vb/sizeof *vb,.bindings=vb,.storage_bias_mask=1u<<1};
  vk_shader fragment={.stage=1,.nb=sizeof fb/sizeof *fb,.bindings=fb,.storage_bias_mask=1u<<18};
  vk_shader *list[6]={&vertex,&fragment};
  S.ubo[0][0]=(p_bind_buf){.addr=0x100040,.size=131072};
  S.ssbo[0][1]=(p_bind_buf){.addr=0x200003,.size=19570000};
  S.ubo[1][2]=(p_bind_buf){.addr=0x300003,.size=72};
  S.tex[1][127]=slot;S.img[1][31]=slot;
  pool_range_fixture out={.capacity=32};
  CHECK(vk_shader_pool_ranges(list,0,pool_range_collect,&out));CHECK(out.count==5);
  CHECK(out.ranges[0].address==0x100040&&out.ranges[0].size==131072);
  CHECK(out.ranges[0].binding_kind==1&&!out.ranges[0].writable&&!out.ranges[0].prefix_alignment);
  CHECK(out.ranges[0].stages==VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);
  CHECK(out.ranges[1].address==0x200003&&out.ranges[1].size==19570000&&out.ranges[1].writable);
  CHECK(out.ranges[1].prefix_alignment==V.props.limits.minStorageBufferOffsetAlignment);
  CHECK(out.ranges[2].address==0x300003&&out.ranges[2].size==72&&out.ranges[2].binding_kind==8);
  CHECK(out.ranges[2].writable&&out.ranges[2].prefix_alignment==V.props.limits.minStorageBufferOffsetAlignment);
  for(unsigned i=3;i<5;i++) {
    CHECK(out.ranges[i].address==(uintptr_t)descriptor.mem&&out.ranges[i].size==descriptor.L.size);
    CHECK(out.ranges[i].stages==VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    CHECK(out.ranges[i].writable==(i==4));
  }
#ifdef DM_VULKAN_TEST_EMBED
  // Mocks begin uninitialized and must retain the original scan. Once the
  // actual cold builder runs, the same literal range oracle still applies.
  void vk_test_shader_pool_binding_mask_build(vk_shader *);
  CHECK(!vertex.pool_binding_mask_ready && !fragment.pool_binding_mask_ready);
  vk_test_shader_pool_binding_mask_build(&vertex);
  vk_test_shader_pool_binding_mask_build(&fragment);
  CHECK(vertex.pool_binding_mask_ready && fragment.pool_binding_mask_ready);
  pool_range_fixture masked={.capacity=32};
  CHECK(vk_shader_pool_ranges(list,0,pool_range_collect,&masked));CHECK(masked.count==5);
  for(unsigned k=0;k<5;k++) {
    CHECK(masked.ranges[k].address==out.ranges[k].address && masked.ranges[k].size==out.ranges[k].size);
    CHECK(masked.ranges[k].prefix_alignment==out.ranges[k].prefix_alignment);
    CHECK(masked.ranges[k].stages==out.ranges[k].stages && masked.ranges[k].writable==out.ranges[k].writable);
    CHECK(masked.ranges[k].binding_kind==out.ranges[k].binding_kind);
  }
  // Real Vulkan device limits need not allow128 simultaneous descriptors.
  // Exercise immutable index boundaries through the same cold builder, then
  // validate its observable logical ranges, not the mask's implementation bits.
  vk_binding wide[128];for(unsigned k=0;k<128;k++)wide[k]=(vk_binding){.count=1};
  wide[0]=vb[0];wide[63]=vb[1];wide[64]=fb[0];wide[127]=fb[1];
  vk_shader boundary={.stage=1,.nb=128,.bindings=wide,.storage_bias_mask=1u<<18};
  vk_shader *wide_list[6]={NULL,&boundary};
  S.ubo[1][0]=(p_bind_buf){.addr=0x100040,.size=131072};
  S.ssbo[1][1]=(p_bind_buf){.addr=0x200003,.size=19570000};
  vk_test_shader_pool_binding_mask_build(&boundary);CHECK(boundary.pool_binding_mask_ready);
  masked=(pool_range_fixture){.capacity=32};
  CHECK(vk_shader_pool_ranges(wide_list,0,pool_range_collect,&masked));CHECK(masked.count==4);
  CHECK(masked.ranges[0].address==0x100040 && masked.ranges[0].size==131072);
  CHECK(masked.ranges[1].address==0x200003 && masked.ranges[1].size==19570000 && masked.ranges[1].writable);
  CHECK(masked.ranges[2].address==0x300003 && masked.ranges[2].size==72 && masked.ranges[2].writable);
  CHECK(masked.ranges[3].address==(uintptr_t)descriptor.mem && masked.ranges[3].binding_kind==7);
  // Unsupported metadata cannot silently discard an earlier valid range or
  // use a copied old mask. Exact old partial-sink/error behavior is retained.
  wide[64].count=2;vk_test_shader_pool_binding_mask_build(&boundary);
  CHECK(!boundary.pool_binding_mask_ready);masked.count=0;
  CHECK(!vk_shader_pool_ranges(wide_list,0,pool_range_collect,&masked));CHECK(masked.count==2);
  wide[64]=(vk_binding){.kind=10,.reg=0,.count=1};vk_test_shader_pool_binding_mask_build(&boundary);
  CHECK(!boundary.pool_binding_mask_ready);masked.count=0;
  CHECK(!vk_shader_pool_ranges(wide_list,0,pool_range_collect,&masked));CHECK(masked.count==2);
  wide[64]=(vk_binding){.count=1};wide[0]=wide[63]=wide[127]=wide[64];
  vk_test_shader_pool_binding_mask_build(&boundary);CHECK(boundary.pool_binding_mask_ready);
  masked.count=0;CHECK(vk_shader_pool_ranges(wide_list,0,pool_range_collect,&masked));CHECK(!masked.count);
#endif
  vertex.input_mask=15;
  vk_state captured=S;
  captured.attr[0]=(va_st){.fmt=NVN_FORMAT_RGBA32F,.stream=5};
  captured.attr[1]=(va_st){.fmt=NVN_FORMAT_RGBA32F,.stream=5};
  captured.attr[2]=(va_st){.fmt=NVN_FORMAT_RGBA32F,.stream=6};
  // Active shader input with no declared attribute format follows the same
  // omission rule as pipeline/vertex binding creation.
  captured.attr[3]=(va_st){.stream=7};
  captured.vertex[5]=(NVNbufferRange){.address=0x700003,.size=65545};
  captured.vertex[6]=(NVNbufferRange){.address=0x800000,.size=17};
  p_draw_idx indexed={.count=7,.itype=NVN_INDEX_TYPE_UNSIGNED_INT,.ibuf=0x900004};
  out=(pool_range_fixture){.capacity=32};
  CHECK(vk_graphics_pool_ranges(list,&captured,&indexed,pool_range_collect,&out));
  CHECK(out.count==8);
  CHECK(out.ranges[5].binding_kind==VK_POOL_REQUIRE_VERTEX&&out.ranges[5].address==0x700003&&out.ranges[5].size==65545);
  CHECK(out.ranges[6].binding_kind==VK_POOL_REQUIRE_VERTEX&&out.ranges[6].address==0x800000&&out.ranges[6].size==17);
  CHECK(out.ranges[7].binding_kind==VK_POOL_REQUIRE_INDEX&&out.ranges[7].address==0x900004&&out.ranges[7].size==28);
  for(unsigned i=5;i<8;i++)CHECK(out.ranges[i].stages==VK_PIPELINE_STAGE_VERTEX_INPUT_BIT&&!out.ranges[i].writable);
  // Explicit saved state is sufficient: no temporary global S installation
  // or hidden physical lookup is allowed during deferred-draw planning.
  CHECK(!S.attr[0].fmt&&!S.vertex[5].address);
  indexed.itype=NVN_INDEX_TYPE_UNSIGNED_SHORT;out.count=0;
  CHECK(vk_graphics_pool_ranges(list,&captured,&indexed,pool_range_collect,&out));
  CHECK(out.ranges[7].size==14);
  indexed.count=0;out.count=0;
  CHECK(!vk_graphics_pool_ranges(list,&captured,&indexed,pool_range_collect,&out));
  indexed.count=-1;CHECK(!vk_graphics_pool_ranges(list,&captured,&indexed,pool_range_collect,&out));
  indexed.count=7;indexed.itype=NVN_INDEX_TYPE_UNSIGNED_BYTE;out.count=0;
  CHECK(!vk_graphics_pool_ranges(list,&captured,&indexed,pool_range_collect,&out));
  indexed.itype=NVN_INDEX_TYPE_UNSIGNED_INT;indexed.ibuf=UINT64_MAX-7;out.count=0;
  CHECK(!vk_graphics_pool_ranges(list,&captured,&indexed,pool_range_collect,&out));
  captured.attr[2].stream=16;out.count=0;
  CHECK(!vk_graphics_pool_ranges(list,&captured,NULL,pool_range_collect,&out));
  captured.attr[2].stream=6;captured.vertex[6].address=0;out.count=0;
  CHECK(!vk_graphics_pool_ranges(list,&captured,NULL,pool_range_collect,&out));
  vertex.input_mask=0;
  // Every graphics stage uses its own logical binding bank.
  const VkPipelineStageFlags stages[]={VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT,
    VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT,VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT};
  vk_binding one={.kind=2,.reg=15,.flags=4,.count=1};
  for(unsigned stage=0;stage<6;stage++) {
    vk_shader shader={.stage=stage,.nb=1,.bindings=&one};
    memset(list,0,sizeof list);list[stage]=&shader;
    S.ssbo[stage][15]=(p_bind_buf){.addr=0x500000+stage*4096,.size=32+stage};
    out=(pool_range_fixture){.capacity=32};
    CHECK(vk_shader_pool_ranges(list,stage==5,pool_range_collect,&out));CHECK(out.count==1);
    CHECK(out.ranges[0].address==0x500000+stage*4096&&out.ranges[0].size==32+stage);
    CHECK(out.ranges[0].stages==stages[stage]&&!out.ranges[0].writable);
    one.flags=1;out.count=0;
    CHECK(vk_shader_pool_ranges(list,stage==5,pool_range_collect,&out));CHECK(out.ranges[0].writable);
    one.flags=4;
  }
  memset(list,0,sizeof list);list[0]=&vertex;list[1]=&fragment;
  out=(pool_range_fixture){.capacity=1};
  CHECK(!vk_shader_pool_ranges(list,0,pool_range_collect,&out));CHECK(out.count==1);
  CHECK(!vk_shader_pool_ranges(list,0,NULL,&out));
  fragment.stage=5;out=(pool_range_fixture){.capacity=32};
  CHECK(!vk_shader_pool_ranges(list,0,pool_range_collect,&out));fragment.stage=1;
  const int invalid_kinds[]={1,2,7,8,9,10,99};
  const int invalid_regs[]={19,16,128,16,32,0,0};
  vk_binding invalid={.count=1};vk_shader invalid_shader={.stage=5,.nb=1,.bindings=&invalid};
  memset(list,0,sizeof list);list[5]=&invalid_shader;
  for(unsigned i=0;i<sizeof invalid_kinds/sizeof *invalid_kinds;i++) {
    invalid.kind=invalid_kinds[i];invalid.reg=invalid_regs[i];out.count=0;
    CHECK(!vk_shader_pool_ranges(list,1,pool_range_collect,&out));CHECK(!out.count);
  }
  invalid=(vk_binding){.kind=2,.reg=-1,.count=1};
  CHECK(!vk_shader_pool_ranges(list,1,pool_range_collect,&out));
  invalid.reg=0;invalid.count=2;CHECK(!vk_shader_pool_ranges(list,1,pool_range_collect,&out));
  invalid.count=1;S.ssbo[5][0]=(p_bind_buf){.addr=UINT64_MAX-7,.size=16};
  CHECK(!vk_shader_pool_ranges(list,1,pool_range_collect,&out));
  S.ssbo[5][0]=(p_bind_buf){0};out.count=0;
  CHECK(vk_shader_pool_ranges(list,1,pool_range_collect,&out));CHECK(!out.count);
  uint64_t r,b,c,m;int current_reference;vk_pool_mirror_stats(&r,&b,&c,&m,&current_reference);
  CHECK(r==registered&&b==backed&&c==count&&m==materialized);
  CHECK(reference==current_reference);
  CALL(nvnTextureFinalize,&texture);mtl_set_tex_pool_n(4);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);S=saved;vk_pipeline_state_changed();
  LOG("[vulkan logical pool ranges fixture] full UBO/SSBO extents, stage banks, alignment prefixes, texel writable enums, saved-state vertex/index gather, conservative rejection and zero physical allocations passed");
}
