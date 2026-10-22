// Sole recording-owner state, shared by guest and every internal compute
// recorder. This is only the last emitted Vulkan command, not a resource cache.
// An explicit successful begin starts a new recording even if its handle was
// reused. Execute-secondary makes primary command state undefined.
enum { COMPUTE_BIND_SETS=8,COMPUTE_BIND_OFFSETS=16 };
static struct {
  VkCommandBuffer command;
  VkPipeline pipeline;
  VkPipelineLayout layout;
  VkDescriptorSet sets[COMPUTE_BIND_SETS];
  uint32_t offsets[COMPUTE_BIND_OFFSETS];
  uint32_t first,count,offset_count;
} compute_bindings;
static void compute_bind_reset(void) {
  compute_bindings.command=VK_NULL_HANDLE;
  compute_bindings.pipeline=VK_NULL_HANDLE;
  compute_bindings.layout=VK_NULL_HANDLE;
}
VkResult vk_command_begin(VkCommandBuffer command,const VkCommandBufferBeginInfo *info) {
  VkResult result=vkBeginCommandBuffer(command,info);
  if(result==VK_SUCCESS)compute_bind_reset();
  return result;
}
void vk_command_execute(uint32_t count,const VkCommandBuffer *commands) {
  vkCmdExecuteCommands(V.cmd,count,commands);
  compute_bind_reset();
}
static void compute_bind_command(void) {
  if(compute_bindings.command!=V.cmd) {
    compute_bind_reset();compute_bindings.command=V.cmd;
  }
}
void vk_compute_bind_pipeline(VkPipeline pipeline) {
  compute_bind_command();
  if(!V.cmd || !pipeline || compute_bindings.pipeline!=pipeline) {
    vkCmdBindPipeline(V.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
    compute_bindings.pipeline=pipeline;
  }
}
void vk_compute_bind_descriptors(VkPipelineLayout layout,uint32_t first,uint32_t count,
    const VkDescriptorSet *sets,uint32_t offset_count,const uint32_t *offsets) {
  compute_bind_command();
  // Unknown/long representations remain legal Vulkan calls; they cannot
  // publish a truncated proof. Do not allocate or change caller semantics.
  if(!V.cmd || !layout || count>COMPUTE_BIND_SETS || offset_count>COMPUTE_BIND_OFFSETS ||
      (count&&!sets) || (offset_count&&!offsets)) {
    vkCmdBindDescriptorSets(V.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,first,count,sets,offset_count,offsets);
    compute_bindings.layout=VK_NULL_HANDLE;return;
  }
  if(compute_bindings.layout!=layout || compute_bindings.first!=first ||
      compute_bindings.count!=count || compute_bindings.offset_count!=offset_count ||
      (count&&memcmp(compute_bindings.sets,sets,count*sizeof *sets)) ||
      (offset_count&&memcmp(compute_bindings.offsets,offsets,offset_count*sizeof *offsets))) {
    vkCmdBindDescriptorSets(V.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,first,count,sets,offset_count,offsets);
    compute_bindings.layout=layout;compute_bindings.first=first;
    compute_bindings.count=count;compute_bindings.offset_count=offset_count;
    if(count)memcpy(compute_bindings.sets,sets,count*sizeof *sets);
    if(offset_count)memcpy(compute_bindings.offsets,offsets,offset_count*sizeof *offsets);
  }
}
