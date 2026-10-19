// Owned metadata admission fixtures; no game CPU/module or shader mutation.
// The GPU cases below verify requirement propagation through actual draw/PSO
// paths, not the compiler's derivative reconstruction algorithm.
static int quad_transfer_contract_test(void) {
  crash_thread_init();mtl_init();
  const char *bad=getenv("DM_QUAD_TRANSFER_BAD_KEY");
  if(bad) {vk_shader_load_key(bad);die("quad contract invalid metadata accepted");}
  uint8_t controls[2][0x880];
  for(unsigned i=0;i<2;i++)memset(controls[i],i,sizeof controls[i]);
  vk_shader *native=vk_shader_get(controls[0]),*quad=vk_shader_get(controls[1]);
  CHECK(native->stage==0 && !native->quad_transfer);
  CHECK(quad->stage==1 && quad->quad_transfer==DM_VK_META_QUAD_TRANSFER);
  CHECK(!vk_shader_quad_transfer_error(NULL,0,0,0));
  CHECK(!vk_shader_quad_transfer_error(native,0,0,0));
  const VkPrimitiveTopology good[]={VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN};
  for(unsigned i=0;i<3;i++)CHECK(!vk_shader_quad_transfer_error(quad,VK_SAMPLE_COUNT_1_BIT,good[i],VK_POLYGON_MODE_FILL));
  const VkSampleCountFlagBits samples[]={VK_SAMPLE_COUNT_2_BIT,VK_SAMPLE_COUNT_4_BIT,
    VK_SAMPLE_COUNT_8_BIT,VK_SAMPLE_COUNT_16_BIT,VK_SAMPLE_COUNT_32_BIT,VK_SAMPLE_COUNT_64_BIT};
  for(unsigned i=0;i<6;i++)CHECK(vk_shader_quad_transfer_error(quad,samples[i],good[0],VK_POLYGON_MODE_FILL));
  for(unsigned i=0;i<=VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;i++) {
    if(i==good[0] || i==good[1] || i==good[2])continue;
    CHECK(vk_shader_quad_transfer_error(quad,VK_SAMPLE_COUNT_1_BIT,i,VK_POLYGON_MODE_FILL));
  }
  CHECK(vk_shader_quad_transfer_error(quad,VK_SAMPLE_COUNT_1_BIT,good[0],VK_POLYGON_MODE_LINE));
  CHECK(vk_shader_quad_transfer_error(quad,VK_SAMPLE_COUNT_1_BIT,good[0],VK_POLYGON_MODE_POINT));
  VkPhysicalDeviceLimits saved=V.props.limits;
  V.props.limits.maxFramebufferWidth=(1u<<23)-1;
  V.props.limits.maxFramebufferHeight=(1u<<23)-1;
  CHECK(!vk_shader_quad_transfer_error(quad,VK_SAMPLE_COUNT_1_BIT,good[0],VK_POLYGON_MODE_FILL));
  V.props.limits.maxFramebufferWidth=1u<<23;
  CHECK(vk_shader_quad_transfer_error(quad,VK_SAMPLE_COUNT_1_BIT,good[0],VK_POLYGON_MODE_FILL));
  V.props.limits.maxFramebufferWidth=saved.maxFramebufferWidth;V.props.limits.maxFramebufferHeight=1u<<23;
  CHECK(vk_shader_quad_transfer_error(quad,VK_SAMPLE_COUNT_1_BIT,good[0],VK_POLYGON_MODE_FILL));
  V.props.limits=saved;
  for(const hle_force_ent *e=(const void *)__start_swforce;e<(const hle_force_ent *)(const void *)__stop_swforce;e++)
    if(!strcmp(e->name,"nvnBootstrapLoader"))bootstrap=e->fn;
  CHECK(bootstrap);
  NVNdeviceBuilder db;CALL(nvnDeviceBuilderSetDefaults,&db);CHECK(CALL(nvnDeviceInitialize,&device,&db));
  NVNqueueBuilder qb;CALL(nvnQueueBuilderSetDefaults,&qb);CALL(nvnQueueBuilderSetDevice,&qb,&device);
  CHECK(CALL(nvnQueueInitialize,&queue,&qb));
  // Existing pixel oracle issues repeated CALL/JUMP draws, reuses PSOs, and
  // independently checks every output pixel with the actual flagged module.
  unknown_stage_reuse_test(controls[0],controls[1]);
  CHECK(vk_validation_error_count()==0);
  printf("{\"test\":\"quad-transfer-runtime-contract\",\"checks\":%u,\"result\":\"passed\"}\n",checks);
  return 0;
}
