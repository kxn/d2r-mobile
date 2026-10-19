// Submission-thread-owned immutable layouts. Equality includes every binding,
// its stage visibility and every push range; no module or descriptor contents.
// Like shaders/PSOs these objects live for the backend device lifetime.
typedef struct { uint32_t binding,type,count,stages; } descriptor_layout_binding;
typedef struct descriptor_layout_entry {
  struct descriptor_layout_entry *next;
  uint32_t count;
  VkDescriptorSetLayout layout;
  descriptor_layout_binding bindings[];
} descriptor_layout_entry;
typedef struct pipeline_layout_entry {
  struct pipeline_layout_entry *next;
  VkDescriptorSetLayout sets[6];
  uint32_t count,biased;
  VkPipelineLayout layout;
} pipeline_layout_entry;
static descriptor_layout_entry *descriptor_layout_cache[256];
static pipeline_layout_entry *pipeline_layout_cache[256];
static uint64_t layout_hash(const void *data,size_t size) {
  const uint8_t *p=data;uint64_t h=UINT64_C(14695981039346656037);
  while(size--) { h^=*p++;h*=UINT64_C(1099511628211); }return h;
}
static VkDescriptorSetLayout descriptor_layout_get(const VkDescriptorSetLayoutBinding *bindings,uint32_t count) {
  descriptor_layout_binding key[128];
  if(count>128)die("Vulkan descriptor layout binding capacity");
  for(uint32_t i=0;i<count;i++) {
    if(bindings[i].pImmutableSamplers)die("Vulkan immutable sampler layout pending");
    descriptor_layout_binding value={bindings[i].binding,bindings[i].descriptorType,bindings[i].descriptorCount,bindings[i].stageFlags};
    uint32_t j=i;while(j && key[j-1].binding>value.binding) { key[j]=key[j-1];j--; }key[j]=value;
  }
  uint32_t bucket=(layout_hash(key,count*sizeof *key)^count)%256;
  for(descriptor_layout_entry *e=descriptor_layout_cache[bucket];e;e=e->next)
    if(e->count==count && !memcmp(e->bindings,key,count*sizeof *key))return e->layout;
  descriptor_layout_entry *e=calloc(1,sizeof *e+count*sizeof *key);
  if(!e)die("Vulkan descriptor layout cache allocation");
  VkDescriptorSetLayoutBinding ordered[128];
  for(uint32_t i=0;i<count;i++)ordered[i]=(VkDescriptorSetLayoutBinding){
    .binding=key[i].binding,.descriptorType=key[i].type,.descriptorCount=key[i].count,.stageFlags=key[i].stages};
  VkDescriptorSetLayoutCreateInfo ci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .bindingCount=count,.pBindings=ordered};
  VK_CHECK(vkCreateDescriptorSetLayout(V.device,&ci,NULL,&e->layout));
  vk_descriptor_schema_register(e->layout,ordered,count);
  e->count=count;memcpy(e->bindings,key,count*sizeof *key);e->next=descriptor_layout_cache[bucket];descriptor_layout_cache[bucket]=e;return e->layout;
}
static VkPipelineLayout pipeline_layout_get(const VkDescriptorSetLayout sets[6],uint32_t count,int biased) {
  uint32_t bucket=(layout_hash(sets,count*sizeof *sets)^count^!!biased)%256;
  for(pipeline_layout_entry *e=pipeline_layout_cache[bucket];e;e=e->next)
    if(e->count==count && e->biased==!!biased && !memcmp(e->sets,sets,count*sizeof *sets))return e->layout;
  pipeline_layout_entry *e=calloc(1,sizeof *e);if(!e)die("Vulkan pipeline layout cache allocation");
  VkPushConstantRange push={.stageFlags=VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_COMPUTE_BIT,.offset=0,.size=128};
  VkPipelineLayoutCreateInfo ci={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount=count,.pSetLayouts=sets,.pushConstantRangeCount=biased?1:0,.pPushConstantRanges=biased?&push:NULL};
  VK_CHECK(vkCreatePipelineLayout(V.device,&ci,NULL,&e->layout));
  e->count=count;e->biased=!!biased;memcpy(e->sets,sets,count*sizeof *sets);
  e->next=pipeline_layout_cache[bucket];pipeline_layout_cache[bucket]=e;return e->layout;
}
static atomic_int dynamic_uniform_reference;
static atomic_int dynamic_uniform_narrow_reference;
void vk_dynamic_uniform_reference(int reference) { atomic_store(&dynamic_uniform_reference,!!reference); }
int vk_dynamic_uniform_reference_mode(void) { return atomic_load(&dynamic_uniform_reference); }
void vk_dynamic_uniform_narrow_reference(int reference) { atomic_store(&dynamic_uniform_narrow_reference,!!reference); }
int vk_dynamic_uniform_narrow_reference_mode(void) { return atomic_load(&dynamic_uniform_narrow_reference); }
int vk_program_dynamic_uniforms(vk_shader *const list[6]) {
  if(atomic_load(&dynamic_uniform_reference) || V.props.limits.minUniformBufferOffsetAlignment>UINT32_MAX)return 0;
  // Select the complete program's budget, not independent per-stage limits.
  // Device/layout limits retain the original narrow path, then static UBOs.
  for(int mode=atomic_load(&dynamic_uniform_narrow_reference)?1:2;mode;mode--) {
    uint32_t total=0,narrow=0;
    for(unsigned i=0;i<5;i++)if(list[i]) {
      total+=list[i]->dynamic_uniform_count[mode-1];
      narrow+=list[i]->dynamic_uniform_count[0];
    }
    if(total && total<=V.props.limits.maxDescriptorSetUniformBuffersDynamic)
      return mode==2 && total==narrow?1:mode;
  }
  return 0;
}
