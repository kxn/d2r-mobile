// Real production DSP3 loader/state/picker. Owned fixture SPIR-V, no game data.
static int uniform_range_contract_test(void) {
  mtl_init();vk_shader *shader=vk_shader_load_key("000000000000000b");
  CHECK(shader->specialization_module && shader->range_module && shader->range_slot==2 && shader->range_min_bytes==192);
  CHECK(shader->range_module!=shader->specialization_module && shader->predicate);
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,65536));memset(storage,0,65536);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,65536);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  vk_state saved=S;uint32_t raw=UINT32_C(0x3e800000);memcpy(storage,&raw,4);
  p_bind_buf selector={1,(int)shader->specialization_slot,(uintptr_t)storage,256};vk_state_op(OP_BIND_UBO,&selector);
  vk_uniform_specialization_enabled(1);uint64_t selected0,fallback0,selected,fallback;
  vk_uniform_range_stats(&selected0,&fallback0);mtl_sub_begin();
  const uint64_t sizes[]={176,192,188,192,0,65540,192};
  for(unsigned i=0;i<sizeof sizes/sizeof *sizes;i++) {
    p_bind_buf guarded={1,(int)shader->range_slot,(uintptr_t)storage,sizes[i]};vk_state_op(OP_BIND_UBO,&guarded);
    uint32_t value=0;VkShaderModule expected=sizes[i]>=192?shader->range_module:shader->specialization_module;
    CHECK(vk_uniform_specialization_pick(shader,&value)==expected && value==raw);
    if(!vk_uniform_predicate_observation_enabled())vk_uniform_predicate_enabled(1);
    uint32_t pair[2];CHECK(vk_uniform_hybrid_pick(shader,pair)==expected && pair[0]==raw && !pair[1]);
    if(!vk_uniform_predicate_observation_enabled())vk_uniform_predicate_enabled(0);
  }
  vk_state large=S,small=S;small.ubo[1][shader->range_slot].size=176;
  vk_state_restore(&small);uint32_t value;CHECK(vk_uniform_specialization_pick(shader,&value)==shader->specialization_module);
  vk_state_restore(&large);CHECK(vk_uniform_specialization_pick(shader,&value)==shader->range_module);
  vk_prewarm_stage recipe;CHECK(vk_shader_prewarm_stage(shader,shader->range_module,&recipe) && recipe.module==4);
  VkShaderModule restored;CHECK(vk_shader_prewarm_load(&recipe,&restored)==shader && restored==shader->range_module);
  vk_state_restore(&small);CHECK(vk_uniform_specialization_pick(shader,&value)==shader->specialization_module);
  recipe.fingerprint[0]^=1;CHECK(!vk_shader_prewarm_load(&recipe,&restored));
  shader->range_disabled=1;vk_state_restore(&large);
  CHECK(vk_uniform_specialization_pick(shader,&value)==shader->specialization_module && !shader->specialization_disabled);
  shader->range_disabled=0;CHECK(vk_uniform_specialization_pick(shader,&value)==shader->range_module);
  vk_internal_wait(vk_internal_commit());vk_uniform_range_stats(&selected,&fallback);
  CHECK(selected>selected0 && fallback>fallback0);vk_state_restore(&saved);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);
  uint8_t vertex[0x880]={0};uniform_specialization_test(vertex,1);
  CHECK(vk_validation_error_count()==0);
  printf("{\"test\":\"DSP3-range-contract\",\"checks\":%u,\"result\":\"passed\",\"game_modules_executed\":false}\n",checks);
  return 0;
}
