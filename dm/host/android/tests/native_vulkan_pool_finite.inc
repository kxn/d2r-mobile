// Exact GPU byte oracles for finite owner maintenance. No game CPU module,
// RSS/performance measurement, shader-visible range narrowing or draw logger.
#ifdef DM_VULKAN_TEST_EMBED
#include <sched.h>
extern int vk_test_pool_consolidate(uint64_t,uint64_t,const vk_pool_requirement *,uint64_t);
extern void vk_test_pool_migration_publish_hook(void (*)(void *),void *);
extern void vk_test_pool_migration_copy_hook(void (*)(void *),void *);
static void pool_finite_same(vk_pool_span_info a,uint8_t *address) {
  vk_pool_span_info b;CHECK(vk_pool_span_query((uintptr_t)address,4,&b)==VK_POOL_SPAN_SINGLE);
  CHECK(a.pool_identity==b.pool_identity&&a.handle==b.handle&&a.backing_generation==b.backing_generation);
  CHECK(a.logical_begin==b.logical_begin&&a.logical_end==b.logical_end);
}
typedef struct {vk_pool_requirement first,extra;unsigned calls;} pool_finite_recollect;
static int pool_finite_collect(void *context,vk_pool_requirement_sink sink,void *out) {
  pool_finite_recollect *c=context;c->calls++;
  if(!sink(out,&c->first))return 0;
  return c->calls<2 || sink(out,&c->extra);
}
typedef struct {uint8_t *storage;unsigned size;pthread_t deleter;uint64_t replacement_identity;} pool_finite_reuse;
static void *pool_finite_delete(void *context) {mtl_pool_del(((pool_finite_reuse *)context)->storage);return NULL;}
static void pool_finite_recreate(void *context) {
  pool_finite_reuse *c=context;CHECK(!pthread_create(&c->deleter,NULL,pool_finite_delete,c));
  vk_pool_span_info q;unsigned tries;
  for(tries=0;tries<100000;tries++) {
    if(vk_pool_span_query((uintptr_t)c->storage,4,&q)==VK_POOL_SPAN_NONE)break;
    sched_yield();
  }
  CHECK(tries<100000);mtl_pool_add(c->storage,c->size);
  CHECK(vk_pool_span_query((uintptr_t)c->storage,4,&q)==VK_POOL_SPAN_UNBACKED);
  c->replacement_identity=q.pool_identity;
}
// Actual graphics descriptor/vertex fetch path, not a synthetic range kind:
// one existing outside node, then a full >window readonly VS SSBO descriptor.
static void pool_finite_vertex_ssbo_test(void) {
  enum {W=65536,SIZE=8*W};vk_state saved=S;uint8_t *storage;
  CHECK(!posix_memalign((void **)&storage,W,SIZE));dm_guest_write_prepare(storage,SIZE);memset(storage,0x69,SIZE);
  const float positions[]={-1,-1,3,-1,-1,3},red[]={1,0,0,1};
  uint8_t *positions_at=storage+2*W-16,*color_at=storage+7*W+64;
  memcpy(positions_at,positions,sizeof positions);memcpy(color_at,red,sizeof red);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;CALL(nvnMemoryPoolBuilderSetDefaults,&mb);
  CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,SIZE);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  vk_pool_requirement seed=pool_window_need(storage+64,4,2,VK_PIPELINE_STAGE_TRANSFER_BIT,0);vk_pool_reserve_result result;
  CHECK(vk_pool_reserve_ranges(&seed,1,W,&result)==VK_POOL_RESERVE_OK);
  vk_pool_span_info outside,q;CHECK(vk_pool_span_query(seed.address,4,&outside)==VK_POOL_SPAN_SINGLE);
  mtl_sub_begin();uint32_t gpu=0x89674523;pool_window_fill(storage+64,4,gpu);vk_internal_wait(vk_internal_commit());
  uint8_t control[2][0x880];memset(control[0],24,0x880);memset(control[1],5,0x880);
  NVNprogram program;NVNshaderData sd[]={{.control=control[0]},{.control=control[1]}};
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,sd));
  NVNtextureBuilder tb;NVNtexture target;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&tb,16);
  CALL(nvnTextureBuilderSetHeight,&tb,16);CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);
  CALL(nvnTextureBuilderSetStorage,&tb,&pool,6*W);CHECK(CALL(nvnTextureInitialize,&target,&tb));
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);
  CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);
  S=(vk_state){.program=(const prog_t *)&program,.targets={.n=1,.color={(const tex_t *)&target}},.depth_range={0,1},.chmask={~0u}};
  S.poly=*(const poly_st *)&polygon;S.ds=*(const ds_st *)&ds;S.color=*(const color_st *)&color;
  S.has_viewport=S.has_scissor=1;S.viewport[0][2]=S.viewport[0][3]=16;S.scissor=(p_scissor){0,0,16,16};
  S.ssbo[0][0]=(p_bind_buf){0,0,(uintptr_t)positions_at,2*W+32};S.ssbo[1][0]=(p_bind_buf){1,0,(uintptr_t)color_at,16};vk_pipeline_state_changed();
  mtl_sub_begin();vk_draw(&(p_draw){.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3},NULL);
  CHECK(S.ssbo[0][0].size==2*W+32);CHECK(vk_pool_span_query((uintptr_t)positions_at,2*W+32,&q)==VK_POOL_SPAN_SINGLE);
  CHECK(q.logical_begin==W&&q.logical_end==5*W&&q.local_offset==W-16);pool_finite_same(outside,storage+64);
  uint8_t output[4];pool_window_read(storage+64,4,output);vk_internal_wait(vk_internal_commit());CHECK(!memcmp(output,&gpu,4));
  uint8_t pixels[1024];NVNcopyRegion region={0,0,0,16,16,1};CALL(nvnTextureReadTexels,&target,NULL,&region,pixels);
  for(unsigned k=0;k<256;k++)CHECK(!memcmp(pixels+4*k,(uint8_t[]){255,0,0,255},4));
  S=saved;vk_pipeline_state_changed();CALL(nvnTextureFinalize,&target);CALL(nvnProgramFinalize,&program);CALL(nvnMemoryPoolFinalize,&pool);free(storage);
}
static void pool_finite_append(void *context) {
  uint8_t *storage=context;vk_pool_requirement r=pool_window_need(storage+5*65536+64,4,2,VK_PIPELINE_STAGE_TRANSFER_BIT,0);
  vk_pool_reserve_result result;CHECK(vk_pool_reserve_ranges(&r,1,65536,&result)==VK_POOL_RESERVE_OK&&result.added_spans==1);
  mtl_sub_begin();pool_window_fill(storage+5*65536+64,4,0xa1b2c3d4);vk_internal_wait(vk_internal_commit());
}
static void pool_finite_append_test(void) {
  enum {W=65536,SIZE=10*W};uint8_t *storage;CHECK(!posix_memalign((void **)&storage,W,SIZE));
  dm_guest_write_prepare(storage,SIZE);memset(storage,0x39,SIZE);mtl_pool_add(storage,SIZE);
  vk_pool_requirement needs[]={pool_window_need(storage+W+64,4,2,VK_PIPELINE_STAGE_TRANSFER_BIT,0),pool_window_need(storage+8*W+64,4,2,VK_PIPELINE_STAGE_TRANSFER_BIT,0)};
  vk_pool_reserve_result result;CHECK(vk_pool_reserve_ranges(needs,2,W,&result)==VK_POOL_RESERVE_OK);
  vk_pool_span_info old,q;CHECK(vk_pool_span_query(needs[0].address,4,&old)==VK_POOL_SPAN_SINGLE);
  mtl_sub_begin();pool_window_fill(storage+W+64,4,0x1234abcd);vk_internal_wait(vk_internal_commit());
  // A disjoint published node appears between the selected first/after
  // pointers, outside the destination. Its real GPU-only page must never be
  // copied using that destination's local offset or lost on failed publish.
  vk_test_pool_migration_copy_hook(pool_finite_append,storage);
  vk_pool_requirement cross=pool_window_need(storage+2*W-16,32,2,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,64);
  CHECK(!vk_test_pool_consolidate((uintptr_t)storage,old.pool_identity,&cross,W));vk_test_pool_migration_copy_hook(NULL,NULL);
  pool_finite_same(old,storage+W+64);CHECK(vk_pool_span_query((uintptr_t)storage+5*W+64,4,&q)==VK_POOL_SPAN_SINGLE);
  uint8_t output[8];mtl_sub_begin();pool_window_read(storage+W+64,4,output);pool_window_read(storage+5*W+64,4,output+4);vk_internal_wait(vk_internal_commit());
  CHECK(!memcmp(output,(uint32_t[]){0x1234abcd,0xa1b2c3d4},8));
  CHECK(vk_pool_span_query(cross.address,cross.size,&q)==VK_POOL_SPAN_CROSSING);mtl_pool_del(storage);free(storage);
}
static void pool_finite_consolidation_test(void) {
  mtl_init();
  if(V.options.pool_mirror_reference)return;
  enum {W=65536,SIZE=16*W+193};
  uint64_t old_window=V.options.pool_window_bytes,full,used,protected;int reference;
  vk_pool_sync_policy_stats(&full,&used,&protected,&reference);vk_pool_sync_reference(0);mtl_finish();
  vk_test_pool_window_bytes(W);
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,W,SIZE));dm_guest_write_prepare(storage,SIZE);
  for(unsigned k=0;k<SIZE;k++)storage[k]=(uint8_t)(k*7+(k/4096)*11+0x31);
  mtl_pool_add(storage,SIZE);
  const unsigned indices[]={0,1,2,4,5,8,12,16};
  vk_pool_requirement needs[8];vk_pool_span_info spans[8],q;
  for(unsigned k=0;k<8;k++)needs[k]=pool_window_need(storage+indices[k]*W+64,4,2,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,64);
  vk_pool_reserve_result result;CHECK(vk_pool_reserve_ranges(needs,8,W,&result)==VK_POOL_RESERVE_OK);
  CHECK(result.added_spans==8&&result.added_buffer_bytes==7*W+193);
  for(unsigned k=0;k<8;k++)CHECK(vk_pool_span_query(needs[k].address,4,&spans[k])==VK_POOL_SPAN_SINGLE);
  // GPU-only writes on all old nodes, including adjacent outside neighbors.
  // A selected and an outside CPU edit remain deferred until actual consume.
  uint32_t words[8];uint8_t baseline[8],scratch[128];mtl_sub_begin();
  for(unsigned k=0;k<8;k++) {words[k]=0xabcd1000+k;pool_window_fill(storage+indices[k]*W+64,4,words[k]);}
  memcpy(baseline,storage+2*W+4096+64,4);memcpy(baseline+4,storage+5*W+4096+64,4);
  pool_window_read(storage+2*W+4096+64,4,scratch);pool_window_read(storage+5*W+4096+64,4,scratch+4);
  // Retain a real old selected BufferView until ordinary submission recycle.
  vk_buffer *old;uint64_t offset;CHECK(vk_pool_address_stage((uintptr_t)storage+2*W,256,(void **)&old,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
  VkBufferViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,.buffer=old->handle,.format=VK_FORMAT_R32_UINT,.offset=offset,.range=256};VkBufferView view;
  VK_CHECK(vkCreateBufferView(V.device,&vi,NULL,&view));vk_texture_own_buffer_view(view);
  vk_internal_wait(vk_internal_commit());CHECK(!memcmp(scratch,baseline,8));
  for(unsigned k=0;k<8;k++)CHECK(memcmp(storage+indices[k]*W+64,&words[k],4));
  dm_guest_write_prepare(storage+2*W+4096+64,4);memset(storage+2*W+4096+64,0xb8,4);
  dm_guest_write_prepare(storage+5*W+4096+64,4);memset(storage+5*W+4096+64,0xc9,4);
  dm_guest_write_prepare(storage+3*W+32,4);memset(storage+3*W+32,0xda,4); // cold hole
  vk_pool_requirement crossing=pool_window_need(storage+2*W-16,2*W+32,2,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,64);
  CHECK(vk_pool_reserve_ranges(&crossing,1,W,&result)==VK_POOL_RESERVE_NEED_WHOLE);
  CHECK(result.trigger.address==crossing.address&&result.trigger.size==crossing.size&&result.trigger.prefix_alignment==64);
  uint64_t identity=spans[0].pool_identity,initialized=vk_pool_initialized_bytes();
  // Invalid/old identity/OOM cannot publish or consume the old dirty upload.
  CHECK(!vk_test_pool_consolidate((uintptr_t)storage,identity+1,&crossing,W));
  vk_pool_requirement invalid=crossing;invalid.prefix_alignment=3;
  CHECK(!vk_test_pool_consolidate((uintptr_t)storage,identity,&invalid,W));
  invalid=crossing;invalid.size=SIZE;
  CHECK(!vk_test_pool_consolidate((uintptr_t)storage,identity,&invalid,W));
  vk_shader_operation_begin();CHECK(!vk_test_pool_consolidate((uintptr_t)storage,identity,&crossing,W));vk_shader_writebacks();
  vk_texture_binding_scope_begin();CHECK(!vk_test_pool_consolidate((uintptr_t)storage,identity,&crossing,W));vk_texture_binding_scope_end();
  uint32_t blocks0,allocs0,blocks1,allocs1;uint64_t bytes0,bytes1;
  vk_memory_stats(&blocks0,&allocs0,&bytes0);vk_test_pool_reserve_fail_after(0);
  CHECK(!vk_test_pool_consolidate((uintptr_t)storage,identity,&crossing,W));vk_test_pool_reserve_fail_after(-1);
  vk_memory_stats(&blocks1,&allocs1,&bytes1);CHECK(allocs0==allocs1&&bytes0==bytes1);
  for(unsigned k=0;k<8;k++)pool_finite_same(spans[k],storage+indices[k]*W+64);
  // Real production admission ends the current recording, performs isolated
  // initialized-page relocation, reopens, and recollects before returning token.
  mtl_sub_begin();uint64_t fence=mtl_last_fence();int token=vk_pool_operation_begin_ranges(&crossing,1);CHECK(token);
  CHECK(mtl_last_fence()==fence&&vk_pool_initialized_bytes()==initialized);
  CHECK(vk_test_pool_migration_copy_bytes()==4*4096);
  CHECK(vk_pool_span_query(crossing.address,crossing.size,&q)==VK_POOL_SPAN_SINGLE);
  CHECK(q.logical_begin==W&&q.logical_end==5*W&&q.local_offset==W-16&&q.pool_identity==identity);
  for(unsigned k=0;k<8;k++)if(k!=1&&k!=2&&k!=3)pool_finite_same(spans[k],storage+indices[k]*W+64);
  CHECK(q.handle!=spans[1].handle&&q.handle!=spans[2].handle&&q.handle!=spans[3].handle);
  vk_pool_operation_end(token);
  CHECK(vk_pool_address_stage((uintptr_t)storage+2*W+4096+64,4,(void **)&old,&offset,VK_PIPELINE_STAGE_TRANSFER_BIT));
  CHECK(offset==W+4096+64&&!memcmp(old->map+offset,baseline,4)); // prior bytes, before queued upload
  uint8_t *output=malloc(2*W+32),*expected=malloc(2*W+32);CHECK(output&&expected);
  memcpy(expected,(void *)(uintptr_t)crossing.address,crossing.size);memcpy(expected+16+64,&words[2],4);
  pool_window_read((uint8_t *)(uintptr_t)crossing.address,crossing.size,output);
  for(unsigned k=0;k<8;k++)pool_window_read(storage+indices[k]*W+64,4,scratch+4*k);
  pool_window_read(storage+5*W+4096+64,4,scratch+32);vk_internal_wait(vk_internal_commit());
  CHECK(!memcmp(output,expected,crossing.size));for(unsigned k=0;k<8;k++)CHECK(!memcmp(scratch+4*k,&words[k],4));
  CHECK(!memcmp(scratch+32,(uint8_t[]){0xc9,0xc9,0xc9,0xc9},4));free(expected);free(output);
  // Exact real-world shape: a disjoint multiwindow request in a materialized
  // pool adds a finite node and copies zero old bytes. Recollection adds an
  // independently required range, never an old cached partial plan.
  vk_pool_span_info retained[8];for(unsigned k=0;k<8;k++)CHECK(vk_pool_span_query(needs[k].address,4,&retained[k])==VK_POOL_SPAN_SINGLE);
  pool_finite_recollect collect={.first=pool_window_need(storage+9*W+W/2,W+64,2,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,64),.extra=needs[6]};
  initialized=vk_pool_initialized_bytes();mtl_sub_begin();token=vk_pool_operation_begin(pool_finite_collect,&collect);
  CHECK(token&&collect.calls>=2&&vk_test_pool_migration_copy_bytes()==0&&vk_pool_initialized_bytes()==initialized);
  CHECK(vk_pool_span_query(collect.first.address,collect.first.size,&q)==VK_POOL_SPAN_SINGLE);
  CHECK(q.logical_begin==9*W&&q.logical_end==11*W&&q.local_offset==W/2);
  for(unsigned k=0;k<8;k++)pool_finite_same(retained[k],storage+indices[k]*W+64);
  vk_pool_operation_end(token);pool_window_read(storage+10*W+32,4,scratch);vk_internal_wait(vk_internal_commit());CHECK(!memcmp(scratch,storage+10*W+32,4));
  // Closure includes the whole earlier long node, but mere adjacency at9W
  // preserves the new disjoint node. Outside exact handles/generations survive.
  vk_pool_span_info disjoint=q;crossing=pool_window_need(storage+4*W+128,4*W,2,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,64);
  mtl_sub_begin();token=vk_pool_operation_begin_ranges(&crossing,1);CHECK(token);
  CHECK(vk_pool_span_query(crossing.address,crossing.size,&q)==VK_POOL_SPAN_SINGLE);
  CHECK(q.logical_begin==W&&q.logical_end==9*W);
  pool_finite_same(disjoint,storage+9*W+W/2);pool_finite_same(spans[0],storage+64);pool_finite_same(spans[6],storage+12*W+64);
  vk_pool_operation_end(token);for(unsigned k=0;k<8;k++)pool_window_read(storage+indices[k]*W+64,4,scratch+4*k);
  vk_internal_wait(vk_internal_commit());for(unsigned k=0;k<8;k++)CHECK(!memcmp(scratch+4*k,&words[k],4));
  // Clipped tail carries193 initialized bytes and an exact nonzero origin.
  crossing=pool_window_need(storage+14*W+193,2*W,2,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,64);
  initialized=vk_pool_initialized_bytes();mtl_sub_begin();token=vk_pool_operation_begin_ranges(&crossing,1);CHECK(token);
  CHECK(vk_pool_initialized_bytes()==initialized&&vk_test_pool_migration_copy_bytes()==193);
  CHECK(vk_pool_span_query(crossing.address,crossing.size,&q)==VK_POOL_SPAN_SINGLE);
  CHECK(q.logical_begin==14*W&&q.logical_end==SIZE&&q.local_offset==193);vk_pool_operation_end(token);
  pool_window_read(storage+16*W+64,4,scratch);vk_internal_wait(vk_internal_commit());CHECK(!memcmp(scratch,&words[7],4));
  // Full logical requests/reference semantics still converge to the whole pool.
  crossing=pool_window_need(storage,SIZE,VK_POOL_REQUIRE_VERTEX,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,0);
  mtl_sub_begin();token=vk_pool_operation_begin_ranges(&crossing,1);CHECK(token);
  CHECK(vk_pool_span_query((uintptr_t)storage,SIZE,&q)==VK_POOL_SPAN_SINGLE&&q.logical_begin==0&&q.logical_end==SIZE);
  vk_pool_operation_end(token);pool_window_read(storage+W+64,4,scratch);vk_internal_wait(vk_internal_commit());CHECK(!memcmp(scratch,&words[1],4));
  mtl_pool_del(storage);
  // Original pool is unlinked/recreated while isolated GPU copy owns its pin.
  // The old callback must reject publication against the new same-address pool.
  mtl_pool_add(storage,SIZE);CHECK(vk_pool_reserve_ranges(needs,8,W,&result)==VK_POOL_RESERVE_OK);
  CHECK(vk_pool_span_query(needs[0].address,4,&q)==VK_POOL_SPAN_SINGLE);identity=q.pool_identity;
  pool_finite_reuse reuse={.storage=storage,.size=SIZE};vk_test_pool_migration_publish_hook(pool_finite_recreate,&reuse);
  crossing=pool_window_need(storage+2*W-16,2*W+32,2,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,64);
  CHECK(!vk_test_pool_consolidate((uintptr_t)storage,identity,&crossing,W));vk_test_pool_migration_publish_hook(NULL,NULL);
  CHECK(!pthread_join(reuse.deleter,NULL));CHECK(reuse.replacement_identity!=identity);
  CHECK(vk_pool_span_query((uintptr_t)storage,4,&q)==VK_POOL_SPAN_UNBACKED&&q.pool_identity==reuse.replacement_identity);
  CHECK(!vk_test_pool_consolidate((uintptr_t)storage,identity,&crossing,W));
  mtl_sub_begin();token=vk_pool_operation_begin_ranges(needs,1);CHECK(token);vk_pool_operation_end(token);
  pool_window_read(storage+64,4,scratch);vk_internal_wait(vk_internal_commit());CHECK(!memcmp(scratch,storage+64,4));
  mtl_pool_del(storage);free(storage);mtl_finish();
  pool_finite_vertex_ssbo_test();pool_finite_append_test();
  vk_test_pool_window_bytes(old_window);vk_pool_sync_reference(reference);
  LOG("[vulkan finite pool fixture] full binding/prefix, nonzero origin, old GPU-only bytes, pending CPU edits/cold holes, unchanged outside handles, recollection, finite new component, old long-span closure, clipped tail, whole fallback, invalid/OOM rollback and real same-address replacement passed");
}
#else
// Actual graphics descriptor/vertex fetch path, not a synthetic range kind:
// one existing outside node, then a full >window readonly VS SSBO descriptor.
static void pool_finite_vertex_ssbo_test(void) {
  enum {W=65536,SIZE=8*W};vk_state saved=S;uint8_t *storage;
  CHECK(!posix_memalign((void **)&storage,W,SIZE));dm_guest_write_prepare(storage,SIZE);memset(storage,0x69,SIZE);
  const float positions[]={-1,-1,3,-1,-1,3},red[]={1,0,0,1};
  uint8_t *positions_at=storage+2*W-16,*color_at=storage+7*W+64;
  memcpy(positions_at,positions,sizeof positions);memcpy(color_at,red,sizeof red);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;CALL(nvnMemoryPoolBuilderSetDefaults,&mb);
  CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,SIZE);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  vk_pool_requirement seed=pool_window_need(storage+64,4,2,VK_PIPELINE_STAGE_TRANSFER_BIT,0);vk_pool_reserve_result result;
  CHECK(vk_pool_reserve_ranges(&seed,1,W,&result)==VK_POOL_RESERVE_OK);
  vk_pool_span_info outside,q;CHECK(vk_pool_span_query(seed.address,4,&outside)==VK_POOL_SPAN_SINGLE);
  mtl_sub_begin();uint32_t gpu=0x89674523;pool_window_fill(storage+64,4,gpu);vk_internal_wait(vk_internal_commit());
  uint8_t control[2][0x880];memset(control[0],24,0x880);memset(control[1],5,0x880);
  NVNprogram program;NVNshaderData sd[]={{.control=control[0]},{.control=control[1]}};
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,sd));
  NVNtextureBuilder tb;NVNtexture target;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&tb,16);
  CALL(nvnTextureBuilderSetHeight,&tb,16);CALL(nvnTextureBuilderSetFormat,&tb,NVN_FORMAT_RGBA8);
  CALL(nvnTextureBuilderSetStorage,&tb,&pool,6*W);CHECK(CALL(nvnTextureInitialize,&target,&tb));
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);
  CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);
  S=(vk_state){.program=(const prog_t *)&program,.targets={.n=1,.color={(const tex_t *)&target}},.depth_range={0,1},.chmask={~0u}};
  S.poly=*(const poly_st *)&polygon;S.ds=*(const ds_st *)&ds;S.color=*(const color_st *)&color;
  S.has_viewport=S.has_scissor=1;S.viewport[0][2]=S.viewport[0][3]=16;S.scissor=(p_scissor){0,0,16,16};
  S.ssbo[0][0]=(p_bind_buf){0,0,(uintptr_t)positions_at,2*W+32};S.ssbo[1][0]=(p_bind_buf){1,0,(uintptr_t)color_at,16};vk_pipeline_state_changed();
  mtl_sub_begin();vk_draw(&(p_draw){.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3},NULL);
  CHECK(S.ssbo[0][0].size==2*W+32);CHECK(vk_pool_span_query((uintptr_t)positions_at,2*W+32,&q)==VK_POOL_SPAN_SINGLE);
  CHECK(q.logical_begin==W&&q.logical_end==5*W&&q.local_offset==W-16);pool_finite_same(outside,storage+64);
  uint8_t output[4];pool_window_read(storage+64,4,output);vk_internal_wait(vk_internal_commit());CHECK(!memcmp(output,&gpu,4));
  uint8_t pixels[1024];NVNcopyRegion region={0,0,0,16,16,1};CALL(nvnTextureReadTexels,&target,NULL,&region,pixels);
  for(unsigned k=0;k<256;k++)CHECK(!memcmp(pixels+4*k,(uint8_t[]){255,0,0,255},4));
  S=saved;vk_pipeline_state_changed();CALL(nvnTextureFinalize,&target);CALL(nvnProgramFinalize,&program);CALL(nvnMemoryPoolFinalize,&pool);free(storage);
}
static void pool_finite_append(void *context) {
  uint8_t *storage=context;vk_pool_requirement r=pool_window_need(storage+5*65536+64,4,2,VK_PIPELINE_STAGE_TRANSFER_BIT,0);
  vk_pool_reserve_result result;CHECK(vk_pool_reserve_ranges(&r,1,65536,&result)==VK_POOL_RESERVE_OK&&result.added_spans==1);
  mtl_sub_begin();pool_window_fill(storage+5*65536+64,4,0xa1b2c3d4);vk_internal_wait(vk_internal_commit());
}
static void pool_finite_append_test(void) {
  enum {W=65536,SIZE=10*W};uint8_t *storage;CHECK(!posix_memalign((void **)&storage,W,SIZE));
  dm_guest_write_prepare(storage,SIZE);memset(storage,0x39,SIZE);mtl_pool_add(storage,SIZE);
  vk_pool_requirement needs[]={pool_window_need(storage+W+64,4,2,VK_PIPELINE_STAGE_TRANSFER_BIT,0),pool_window_need(storage+8*W+64,4,2,VK_PIPELINE_STAGE_TRANSFER_BIT,0)};
  vk_pool_reserve_result result;CHECK(vk_pool_reserve_ranges(needs,2,W,&result)==VK_POOL_RESERVE_OK);
  vk_pool_span_info old,q;CHECK(vk_pool_span_query(needs[0].address,4,&old)==VK_POOL_SPAN_SINGLE);
  mtl_sub_begin();pool_window_fill(storage+W+64,4,0x1234abcd);vk_internal_wait(vk_internal_commit());
  // A disjoint published node appears between the selected first/after
  // pointers, outside the destination. Its real GPU-only page must never be
  // copied using that destination's local offset or lost on failed publish.
  vk_test_pool_migration_copy_hook(pool_finite_append,storage);
  vk_pool_requirement cross=pool_window_need(storage+2*W-16,32,2,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,64);
  CHECK(!vk_test_pool_consolidate((uintptr_t)storage,old.pool_identity,&cross,W));vk_test_pool_migration_copy_hook(NULL,NULL);
  pool_finite_same(old,storage+W+64);CHECK(vk_pool_span_query((uintptr_t)storage+5*W+64,4,&q)==VK_POOL_SPAN_SINGLE);
  uint8_t output[8];mtl_sub_begin();pool_window_read(storage+W+64,4,output);pool_window_read(storage+5*W+64,4,output+4);vk_internal_wait(vk_internal_commit());
  CHECK(!memcmp(output,(uint32_t[]){0x1234abcd,0xa1b2c3d4},8));
  CHECK(vk_pool_span_query(cross.address,cross.size,&q)==VK_POOL_SPAN_CROSSING);mtl_pool_del(storage);free(storage);
}
static void pool_finite_consolidation_test(void) {}
#endif
