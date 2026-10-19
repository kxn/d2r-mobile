#ifdef DM_VULKAN_TEST_EMBED
static PFN_vkCreateDescriptorPool capacity_create;
static PFN_vkAllocateDescriptorSets capacity_allocate;
static int capacity_fault,capacity_batch_fault,capacity_single_fault;
static VKAPI_ATTR VkResult VKAPI_CALL capacity_allocate_observe(VkDevice device,
    const VkDescriptorSetAllocateInfo *ai,VkDescriptorSet *sets) {
  // Explicit all-or-nothing API fault injection, never a driver timing claim.
  if(capacity_fault && ai->descriptorSetCount>1 && !capacity_batch_fault) {
    capacity_batch_fault=1;return VK_ERROR_OUT_OF_POOL_MEMORY;
  }
  if(capacity_fault && capacity_batch_fault && !capacity_single_fault && ai->descriptorSetCount==1) {
    capacity_single_fault=1;return VK_ERROR_FRAGMENTED_POOL;
  }
  return capacity_allocate(device,ai,sets);
}
static struct {VkDescriptorPool pool;uint32_t sets,counts[11];} capacity_pools[64];
static unsigned capacity_pool_count;
static VKAPI_ATTR VkResult VKAPI_CALL capacity_observe(VkDevice device,const VkDescriptorPoolCreateInfo *ci,
    const VkAllocationCallbacks *a,VkDescriptorPool *pool) {
  VkResult result=capacity_create(device,ci,a,pool);
  if(result==VK_SUCCESS) {
    CHECK(capacity_pool_count<64);
    typeof(*capacity_pools) *p=&capacity_pools[capacity_pool_count++];
    *p=(typeof(*p)){.pool=*pool,.sets=ci->maxSets};
    for(unsigned i=0;i<ci->poolSizeCount;i++) {
      CHECK((unsigned)ci->pPoolSizes[i].type<11);
      p->counts[ci->pPoolSizes[i].type]=ci->pPoolSizes[i].descriptorCount;
    }
    uint64_t total=0;for(unsigned i=0;i<11;i++)total+=p->counts[i];
    LOG("[descriptor capacity API] pool=%u maxSets=%u slots=%llu sampler=%u combined=%u sampled=%u storageImage=%u texel=%u storageTexel=%u uniform=%u storage=%u uniformDynamic=%u storageDynamic=%u input=%u",
      capacity_pool_count,p->sets,(unsigned long long)total,p->counts[0],p->counts[1],p->counts[2],p->counts[3],p->counts[4],p->counts[5],p->counts[6],p->counts[7],p->counts[8],p->counts[9],p->counts[10]);
  }
  return result;
}
static void descriptor_capacity_test(void) {
  vk_state saved=S;mtl_init();CHECK(V.options.descriptor_sets==256);
  int adaptive=V.options.descriptor_pool_adaptive;
  if(getenv("DM_DESCRIPTOR_CAPACITY_REUSE_REFERENCE"))vk_descriptor_reuse_reference(1);
  mtl_finish();CHECK(mtl_trim_idle_resources());
  uint8_t *data;CHECK(!posix_memalign((void **)&data,65536,1<<18));memset(data,0,1<<18);
  NVNmemoryPoolBuilder builder;NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&builder);CALL(nvnMemoryPoolBuilderSetDevice,&builder,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&builder,data,1<<18);
  CALL(nvnMemoryPoolBuilderSetFlags,&builder,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&builder));
  uint8_t control[0x880];memset(control,2,sizeof control);NVNprogram program;
  NVNshaderData shader={.control=control};CHECK(CALL(nvnProgramInitialize,&program,&device));
  CHECK(CALL(nvnProgramSetShaders,&program,1,&shader));
  vk_shader *cs=vk_shader_get(control);CHECK(vk_compute_prewarm(cs));
  for(unsigned i=0;i<64;i++)((uint32_t *)(data+4096))[i]=i*17;
  capacity_create=vkCreateDescriptorPool;capacity_pool_count=0;vkCreateDescriptorPool=capacity_observe;
  capacity_allocate=vkAllocateDescriptorSets;capacity_fault=getenv("DM_DESCRIPTOR_CAPACITY_FAULT")!=NULL;
  capacity_batch_fault=capacity_single_fault=0;vkAllocateDescriptorSets=capacity_allocate_observe;
  mtl_sub_begin();S=(vk_state){.program=(const prog_t *)&program};
  S.ssbo[5][0]=(p_bind_buf){.stage=5,.index=0,.addr=(uintptr_t)data+4096,.size=256};
  for(unsigned draw=0;draw<130;draw++) {
    S.ssbo[5][1]=(p_bind_buf){.stage=5,.index=1,.addr=(uintptr_t)data+16384+draw*512,.size=256};
    p_dispatch dispatch={1,1,1,0};vk_dispatch(&dispatch);
  }
  unsigned storage_pools=capacity_pool_count;CHECK(storage_pools>0);
  if(capacity_fault)CHECK(capacity_batch_fault && capacity_single_fault);
  if(adaptive) {
    CHECK(storage_pools==2);CHECK(capacity_pools[0].sets==128&&capacity_pools[1].sets==256);
    for(unsigned i=0;i<2;i++)for(unsigned type=0;type<11;type++)
      CHECK(capacity_pools[i].counts[type]==(type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER?capacity_pools[i].sets*2:0));
  } else {CHECK(storage_pools==(capacity_fault?2:1));CHECK(capacity_pools[0].sets==256);}
  // A real newly loaded FS schema introduces uniform/dynamic-uniform types
  // absent from the already-created SSBO-only pools. Actual driver allocation
  // must succeed with a refreshed census, without a stale-pool retry loop.
  memset(control,1,sizeof control);vk_shader *fs=vk_shader_get(control);CHECK(fs->stage==1);
  CHECK(vk_descriptor_allocate(fs->descriptors)!=VK_NULL_HANDLE);
  if(adaptive) {
    CHECK(capacity_pool_count==storage_pools+1);
    CHECK(capacity_pools[storage_pools].counts[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER]>=256);
  }
  // This actual manual layout deliberately bypasses cold registration. The
  // unchanged all-type budget is mandatory; no guessed schema or SPV parsing.
  VkDescriptorSetLayoutBinding bindings[2]={
    {1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL},
    {2,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL}};
  VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .bindingCount=2,.pBindings=bindings};VkDescriptorSetLayout unknown;
  VK_CHECK(vkCreateDescriptorSetLayout(V.device,&li,NULL,&unknown));
  VkDescriptorSet manual=vk_descriptor_allocate(unknown);CHECK(manual!=VK_NULL_HANDLE);
  if(adaptive) {
    CHECK(capacity_pool_count==storage_pools+2);
    CHECK(capacity_pools[capacity_pool_count-1].sets==256);
    CHECK(capacity_pools[capacity_pool_count-1].counts[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER]==256*8);
    CHECK(capacity_pools[capacity_pool_count-1].counts[VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE]==256*16);
  }
  vk_buffer in,out;vk_buffer_create(&in,256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  vk_buffer_create(&out,256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  for(unsigned i=0;i<64;i++)((uint32_t *)in.map)[i]=i*23+11;
  VkDescriptorBufferInfo info[2]={{in.handle,0,256},{out.handle,0,256}};VkWriteDescriptorSet writes[2];
  for(unsigned i=0;i<2;i++)writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet=manual,.dstBinding=i+1,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&info[i]};
  vkUpdateDescriptorSets(V.device,2,writes,0,NULL);
  VkMemoryBarrier pre={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&pre,0,NULL,0,NULL);
  vkCmdBindPipeline(V.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,cs->compute);
  vkCmdBindDescriptorSets(V.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,cs->compute_layout,cs->set,1,&manual,0,NULL);
  vkCmdDispatch(V.cmd,1,1,1);
  VkMemoryBarrier post={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&post,0,NULL,0,NULL);
  vk_internal_wait(vk_internal_commit());mtl_finish();
  for(unsigned draw=0;draw<130;draw++)for(unsigned i=0;i<64;i++)
    CHECK(((uint32_t *)(data+16384+draw*512))[i]==i*17*3+7);
  for(unsigned i=0;i<64;i++)CHECK(((uint32_t *)out.map)[i]==(i*23+11)*3+7);
  // Same descriptor keys, changed CPU contents, completed context reuse: warm
  // retained handles must avoid new pools, while byte uploads still execute.
  unsigned cold_pools=capacity_pool_count;
  dm_guest_write_prepare(data+4096,256);
  for(unsigned i=0;i<64;i++)((uint32_t *)(data+4096))[i]=i*29+5;
  mtl_sub_begin();
  for(unsigned draw=0;draw<130;draw++) {
    S.ssbo[5][1]=(p_bind_buf){.stage=5,.index=1,.addr=(uintptr_t)data+16384+draw*512,.size=256};
    p_dispatch dispatch={1,1,1,0};vk_dispatch(&dispatch);
  }
  vk_internal_wait(vk_internal_commit());mtl_finish();CHECK(capacity_pool_count==cold_pools);
  for(unsigned draw=0;draw<130;draw++)for(unsigned i=0;i<64;i++)
    CHECK(((uint32_t *)(data+16384+draw*512))[i]==(i*29+5)*3+7);
  // Actual idle destruction then original compute regeneration remains covered
  // by idle_cache_trim_test; here verify no live command borrows the layouts.
  CHECK(mtl_trim_idle_resources());
  vkDestroyDescriptorSetLayout(V.device,unknown,NULL);vk_buffer_destroy(&in);vk_buffer_destroy(&out);
  vkCreateDescriptorPool=capacity_create;vkAllocateDescriptorSets=capacity_allocate;CALL(nvnProgramFinalize,&program);CALL(nvnMemoryPoolFinalize,&pool);free(data);
  vk_descriptor_reuse_reference(0);S=saved;vk_pipeline_state_changed();
  LOG("[descriptor capacity fixture] adaptive=%d storage_pools=%u total_pools=%u logical-capacity only",adaptive,storage_pools,capacity_pool_count);
}
#else
static void descriptor_capacity_test(void) {}
#endif
