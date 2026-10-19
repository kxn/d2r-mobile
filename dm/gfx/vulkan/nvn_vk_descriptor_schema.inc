// Owner-thread-only immutable layout census, separate from descriptor values.
// Registers only layouts that live with the backend device. No hot resources,
// speculative reflection or individual layout pools are introduced here.
struct descriptor_schema {
  struct descriptor_schema *next;
  VkDescriptorSetLayout layout;
  uint32_t counts[11];
};
static struct descriptor_schema *descriptor_schemas[256];
static uint32_t descriptor_census[11];
static struct descriptor_schema *descriptor_schema_find(VkDescriptorSetLayout layout) {
  unsigned slot=((uintptr_t)layout>>4)%256;
  for(struct descriptor_schema *s=descriptor_schemas[slot];s;s=s->next)
    if(s->layout==layout)return s;
  return NULL;
}
void vk_descriptor_schema_register(VkDescriptorSetLayout layout,const VkDescriptorSetLayoutBinding *bindings,uint32_t count) {
  if(!V.options.descriptor_pool_adaptive)return;
  uint32_t counts[11]={0};
  if(!layout || count>128 || (count&&!bindings))return;
  for(uint32_t i=0;i<count;i++) {
    const VkDescriptorSetLayoutBinding *b=&bindings[i];
    // Storage-dynamic and extension descriptor kinds are not used by the
    // backend; leave them on the unchanged legacy budget/failure path.
    if((unsigned)b->descriptorType>10 || b->descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC ||
       !b->descriptorCount || b->pImmutableSamplers)return;
    for(uint32_t j=0;j<i;j++)if(bindings[j].binding==b->binding)return;
    uint64_t total=(uint64_t)counts[b->descriptorType]+b->descriptorCount;
    if(total>UINT32_MAX/2048u)return;
    counts[b->descriptorType]=(uint32_t)total;
  }
  struct descriptor_schema *old=descriptor_schema_find(layout);
  if(old) {
    if(memcmp(old->counts,counts,sizeof counts))die("Vulkan immutable descriptor schema changed");
    return;
  }
  struct descriptor_schema *s=calloc(1,sizeof *s);
  if(!s)die("Vulkan descriptor schema census allocation");
  s->layout=layout;memcpy(s->counts,counts,sizeof counts);
  unsigned slot=((uintptr_t)layout>>4)%256;s->next=descriptor_schemas[slot];descriptor_schemas[slot]=s;
  for(unsigned i=0;i<11;i++)if(counts[i]>descriptor_census[i])descriptor_census[i]=counts[i];
}
