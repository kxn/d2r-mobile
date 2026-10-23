// Packet-owned mixed schemas. No alias of the original immutable descriptor
// cache and no descriptor-content hash. All calls are recording-owner-only.
#include "nvn_vk_uab.h"
typedef struct {
  uint64_t serial;
  unsigned state,written; // 0 reusable,1 editing,2 sealed,3 pending
  uint8_t *initialized,*touched,*unpublished;
  vk_split_descriptor_value *fixed;
} uab_version;
struct vk_uab_packet {
  vk_uab_schema schema;
  VkDescriptorSetLayout layout;
  VkDescriptorPool pool;
  VkDescriptorSet *sets;
  uint32_t draws,versions;
  uint64_t descriptors;
  uab_version version[VK_UAB_MAX_VERSIONS];
  vk_uab_batch *queued_batch;
};
struct vk_uab_batch {
  unsigned capacity,count,active;
  VkWriteDescriptorSet *writes;
  descriptor_payload *payloads;
  vk_uab_packet **owners;
  uint8_t **publication;
};
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
static uint64_t uab_update_calls,uab_update_writes;
void vk_test_uab_batch_stats(uint64_t *calls,uint64_t *writes) {*calls=uab_update_calls;*writes=uab_update_writes;}
#endif
static void uab_update(unsigned count,const VkWriteDescriptorSet *writes) {
  if(!count)return;
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  uab_update_calls++;uab_update_writes+=count;
#endif
  vkUpdateDescriptorSets(V.device,count,writes,0,NULL);
}
vk_uab_batch *vk_uab_batch_create(unsigned capacity) {
  if(!capacity || capacity>1024)return NULL;
  vk_uab_batch *b=calloc(1,sizeof *b);if(!b)return NULL;
  b->capacity=capacity;b->writes=calloc(capacity,sizeof *b->writes);b->payloads=calloc(capacity,sizeof *b->payloads);
  b->owners=calloc(capacity,sizeof *b->owners);b->publication=calloc(capacity,sizeof *b->publication);
  if(!b->writes || !b->payloads || !b->owners || !b->publication) {
    free(b->publication);free(b->owners);free(b->payloads);free(b->writes);free(b);return NULL;
  }
  return b;
}
int vk_uab_batch_begin(vk_uab_batch *b) {
  if(!b || b->active || b->count)return 0;b->active=1;return 1;
}
void vk_uab_batch_flush(vk_uab_batch *b) {
  if(!b || !b->count)return;
  uab_update(b->count,b->writes);
  for(unsigned n=0;n<b->count;n++) {
    b->owners[n]->queued_batch=NULL;
    if(b->publication[n])*b->publication[n]=0;
  }
  b->count=0;
}
int vk_uab_batch_end(vk_uab_batch *b) {
  if(!b || !b->active)return 0;vk_uab_batch_flush(b);b->active=0;return 1;
}
int vk_uab_batch_destroy(vk_uab_batch *b) {
  if(!b)return 1;if(b->active)return 0;
  vk_uab_batch_flush(b);free(b->publication);free(b->owners);free(b->payloads);free(b->writes);free(b);return 1;
}
static void uab_batch_append(vk_uab_batch *b,vk_uab_packet *p,uint8_t *publication,
    VkWriteDescriptorSet write,const descriptor_payload *payload) {
  if(b->count==b->capacity)vk_uab_batch_flush(b);
  unsigned n=b->count++;b->writes[n]=write;b->payloads[n]=*payload;b->owners[n]=p;b->publication[n]=publication;
  if(publication)*publication=1;
  p->queued_batch=b;
  b->writes[n].pBufferInfo=NULL;b->writes[n].pImageInfo=NULL;b->writes[n].pTexelBufferView=NULL;
  if(write.pBufferInfo)b->writes[n].pBufferInfo=&b->payloads[n].buffer;
  else if(write.pTexelBufferView)b->writes[n].pTexelBufferView=&b->payloads[n].texel;
  else b->writes[n].pImageInfo=&b->payloads[n].image;
}
static uint64_t uab_pool_descriptors;
static int uab_buffer_type(VkDescriptorType t) {
  return t==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || t==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
}
int vk_uab_schema_from_shader(const vk_shader *s,int dynamic,vk_uab_schema *out) {
  if(!s || !out || s->nb>VK_UAB_MAX_BINDINGS || s->set>=6 || dynamic<0 || dynamic>2)return 0;
  vk_uab_schema schema={.set=s->set,.count=s->nb};
  for(unsigned i=0;i<s->nb;i++) {
    // Existing prepared product contains element0 only. Do not manufacture
    // values or enable partially-bound for unknown reflected arrays.
    if(s->bindings[i].count!=1)return 0;
    schema.bindings[i]=(VkDescriptorSetLayoutBinding){.binding=s->bindings[i].binding,
      .descriptorType=descriptor_type(&s->bindings[i]),.descriptorCount=1,.stageFlags=vk_stage(s->stage)};
    if(dynamic)schema.dynamic_ordinal[i]=s->dynamic_uniform_ordinals[dynamic-1][i];
    if(schema.dynamic_ordinal[i] && schema.bindings[i].descriptorType!=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)return 0;
  }
  *out=schema;return 1;
}
int vk_uab_normalize_values(const vk_uab_schema *s,const vk_split_descriptor_value *in,
                           const vk_uniform_offsets *offsets,vk_split_descriptor_value *out) {
  if(!s || !in || !out || s->count>VK_UAB_MAX_BINDINGS)return 0;
  vk_split_descriptor_value values[VK_UAB_MAX_BINDINGS];
  memcpy(values,in,s->count*sizeof *values);
  for(unsigned n=0;n<s->count;n++)if(s->dynamic_ordinal[n]) {
    unsigned o=s->dynamic_ordinal[n]-1;
    if(s->bindings[n].descriptorType!=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || !offsets || o>=offsets->count || o>=VK_UNIFORM_OFFSET_CAPACITY ||
       values[n].offset>UINT64_MAX-offsets->offsets[o])return 0;
    values[n].offset+=offsets->offsets[o];
  }
  memcpy(out,values,s->count*sizeof *out);return 1;
}
static int uab_schema_limits(const vk_uab_schema *s,uint32_t *total) {
  uint32_t counts[11]={0},all=0;
  if(!V.retained_draws || !s || !s->count || s->count>VK_UAB_MAX_BINDINGS || s->set>=6)return 0;
  for(unsigned n=0;n<s->count;n++) {
    const VkDescriptorSetLayoutBinding *b=&s->bindings[n];
    if(b->descriptorCount!=1 || !b->stageFlags || (b->stageFlags&~(VK_SHADER_STAGE_ALL_GRAPHICS|VK_SHADER_STAGE_COMPUTE_BIT)) || b->pImmutableSamplers || (unsigned)b->descriptorType>VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT ||
       b->descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC || b->descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)return 0;
    for(unsigned i=0;i<n;i++)if(s->bindings[i].binding==b->binding)return 0;
    counts[b->descriptorType]++;all++;
  }
  const VkPhysicalDeviceDescriptorIndexingProperties *p=&V.uab_properties;
  uint32_t sampler=counts[VK_DESCRIPTOR_TYPE_SAMPLER]+counts[VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER];
  uint32_t sampled=counts[VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE]+counts[VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER]+counts[VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER];
  uint32_t storage=counts[VK_DESCRIPTOR_TYPE_STORAGE_IMAGE]+counts[VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER];
#define UAB_LIMIT(value,field) if((value)>p->maxPerStageDescriptorUpdateAfterBind##field || (value)>p->maxDescriptorSetUpdateAfterBind##field)return 0
  UAB_LIMIT(sampler,Samplers);UAB_LIMIT(sampled,SampledImages);UAB_LIMIT(storage,StorageImages);
  UAB_LIMIT(counts[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER],UniformBuffers);
  UAB_LIMIT(counts[VK_DESCRIPTOR_TYPE_STORAGE_BUFFER],StorageBuffers);
  UAB_LIMIT(counts[VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT],InputAttachments);
#undef UAB_LIMIT
  if(all>p->maxPerStageUpdateAfterBindResources)return 0;
  *total=all;return 1;
}
VkDescriptorSetLayout vk_uab_packet_layout(const vk_uab_packet *p) {return p?p->layout:VK_NULL_HANDLE;}
vk_uab_packet *vk_uab_packet_create(const vk_uab_schema *s,uint32_t draws,uint32_t versions) {
  uint32_t count;
  if(!draws || draws>VK_UAB_MAX_DRAWS || !versions || versions>VK_UAB_MAX_VERSIONS || !uab_schema_limits(s,&count))return NULL;
  uint64_t descriptors=(uint64_t)count*draws*versions;
  if(descriptors>V.uab_properties.maxUpdateAfterBindDescriptorsInAllPools ||
     uab_pool_descriptors>V.uab_properties.maxUpdateAfterBindDescriptorsInAllPools-descriptors)return NULL;
  vk_uab_packet *p=calloc(1,sizeof *p);if(!p)return NULL;
  p->schema=*s;p->draws=draws;p->versions=versions;p->descriptors=descriptors;
  p->sets=calloc(draws*versions,sizeof *p->sets);if(!p->sets)goto failed;
  for(unsigned v=0;v<versions;v++) {
    p->version[v].initialized=calloc(draws,1);p->version[v].touched=calloc(draws,1);p->version[v].unpublished=calloc(draws,1);
    p->version[v].fixed=calloc((size_t)draws*s->count,sizeof(vk_split_descriptor_value));
    if(!p->version[v].initialized || !p->version[v].touched || !p->version[v].unpublished || !p->version[v].fixed)goto failed;
  }
  VkDescriptorBindingFlags flags[VK_UAB_MAX_BINDINGS];
  for(unsigned n=0;n<s->count;n++)flags[n]=uab_buffer_type(s->bindings[n].descriptorType)?VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT:0;
  p->layout=descriptor_layout_get_flags(s->bindings,s->count,VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,flags);
  if(!p->layout)goto failed;
  VkDescriptorPoolSize sizes[11];unsigned ns=0;
  for(unsigned n=0;n<s->count;n++) {
    unsigned i=0;while(i<ns && sizes[i].type!=s->bindings[n].descriptorType)i++;
    if(i==ns)sizes[ns++]=(VkDescriptorPoolSize){.type=s->bindings[n].descriptorType};
    sizes[i].descriptorCount+=draws*versions;
  }
  VkDescriptorPoolCreateInfo pool={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.flags=VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
    .maxSets=draws*versions,.poolSizeCount=ns,.pPoolSizes=sizes};
  if(vkCreateDescriptorPool(V.device,&pool,NULL,&p->pool)!=VK_SUCCESS)goto failed;
  for(unsigned i=0;i<draws*versions;i++) {
    VkDescriptorSetAllocateInfo allocate={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=p->pool,.descriptorSetCount=1,.pSetLayouts=&p->layout};
    if(vkAllocateDescriptorSets(V.device,&allocate,&p->sets[i])!=VK_SUCCESS)goto failed;
  }
  uab_pool_descriptors+=descriptors;return p;
failed:
  if(p->pool)vkDestroyDescriptorPool(V.device,p->pool,NULL);
  for(unsigned v=0;v<versions;v++){free(p->version[v].fixed);free(p->version[v].initialized);free(p->version[v].touched);free(p->version[v].unpublished);}
  free(p->sets);free(p);return NULL;
}
int vk_uab_packet_begin(vk_uab_packet *p) {
  if(!p)return -1;
  uint64_t complete=vk_completed_serial();
  for(unsigned i=0;i<p->versions;i++) {
    uab_version *v=&p->version[i];
    if(v->state==3 && v->serial<=complete)v->state=0;
    if(!v->state) {v->state=1;v->written=0;memset(v->touched,0,p->draws);return i;}
  }
  return -1;
}
int vk_uab_packet_write_batch(vk_uab_packet *p,unsigned index,unsigned draw,const vk_split_descriptor_value *values,vk_uab_batch *batch) {
  if(batch && !batch->active)return 0;
  if(!p || index>=p->versions || draw>=p->draws || !values)return 0;
  uab_version *v=&p->version[index];if(v->state!=1 || v->touched[draw])return 0;
  vk_split_descriptor_value *fixed=v->fixed+(size_t)draw*p->schema.count;
  // Inspect all values first: an immutable mismatch cannot leave a partially
  // updated set behind, including when a late image entry is the mismatch.
  for(unsigned n=0;n<p->schema.count;n++) {
    VkDescriptorType type=p->schema.bindings[n].descriptorType;
    if(uab_buffer_type(type)) {
      uint64_t alignment=type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER?V.props.limits.minUniformBufferOffsetAlignment:V.props.limits.minStorageBufferOffsetAlignment;
      uint64_t max=type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER?V.props.limits.maxUniformBufferRange:V.props.limits.maxStorageBufferRange;
      if(!values[n].object || !values[n].range || values[n].range>max || values[n].offset%alignment || values[n].offset>UINT64_MAX-values[n].range)return 0;
    } else if(v->initialized[draw] && memcmp(&fixed[n],&values[n],sizeof *values))return 0;
  }
  if(p->queued_batch && p->queued_batch!=batch)vk_uab_batch_flush(p->queued_batch);
  VkWriteDescriptorSet writes[VK_UAB_MAX_BINDINGS];descriptor_payload payload[VK_UAB_MAX_BINDINGS];unsigned nw=0;
  for(unsigned n=0;n<p->schema.count;n++) {
    VkDescriptorType type=p->schema.bindings[n].descriptorType;
    if(v->initialized[draw] && (!uab_buffer_type(type) || !memcmp(&fixed[n],&values[n],sizeof *values)))continue;
    writes[nw]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=p->sets[index*p->draws+draw],
      .dstBinding=p->schema.bindings[n].binding,.descriptorType=type,.descriptorCount=1};
    if(uab_buffer_type(type)) {
      payload[nw].buffer=(VkDescriptorBufferInfo){(VkBuffer)(uintptr_t)values[n].object,values[n].offset,values[n].range};writes[nw].pBufferInfo=&payload[nw].buffer;
    } else if(type==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER || type==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER) {
      payload[nw].texel=(VkBufferView)(uintptr_t)values[n].object;writes[nw].pTexelBufferView=&payload[nw].texel;
    } else {
      payload[nw].image=(VkDescriptorImageInfo){(VkSampler)(uintptr_t)values[n].offset,(VkImageView)(uintptr_t)values[n].object,(VkImageLayout)values[n].range};writes[nw].pImageInfo=&payload[nw].image;
    }
    fixed[n]=values[n];nw++;
  }
  if(batch)for(unsigned n=0;n<nw;n++)uab_batch_append(batch,p,v->initialized[draw]?NULL:&v->unpublished[draw],writes[n],&payload[n]);
  else uab_update(nw,writes);
  v->initialized[draw]=1;if(!v->touched[draw])v->touched[draw]=1,v->written++;
  return 1;
}
int vk_uab_packet_write(vk_uab_packet *p,unsigned version,unsigned draw,const vk_split_descriptor_value *values) {
  return vk_uab_packet_write_batch(p,version,draw,values,NULL);
}
int vk_uab_packet_seal(vk_uab_packet *p,unsigned i) {
  if(!p || i>=p->versions || p->version[i].state!=1 || !p->version[i].written)return 0;
  p->version[i].state=2;return 1;
}
VkDescriptorSet vk_uab_packet_set(const vk_uab_packet *p,unsigned i,unsigned draw) {
  if(!p || i>=p->versions || draw>=p->draws || (p->version[i].state!=1 && p->version[i].state!=2) ||
     !p->version[i].touched[draw] || !p->version[i].initialized[draw])return VK_NULL_HANDLE;
  if(p->version[i].unpublished[draw])vk_uab_batch_flush(p->queued_batch);
  return p->sets[i*p->draws+draw];
}
int vk_uab_packet_pending(vk_uab_packet *p,unsigned i,uint64_t serial) {
  if(!p || i>=p->versions || p->version[i].state!=2 || !serial || serial!=vk_recording_serial())return 0;
  vk_uab_batch_flush(p->queued_batch);
  p->version[i].serial=serial;p->version[i].state=3;return 1;
}
int vk_uab_packet_abandon(vk_uab_packet *p,unsigned i) {
  if(!p || i>=p->versions || p->version[i].state==3 || !p->version[i].state)return 0;
  vk_uab_batch_flush(p->queued_batch);
  p->version[i].state=0;return 1;
}
int vk_uab_packet_destroy(vk_uab_packet *p) {
  if(!p)return 1;
  uint64_t complete=vk_completed_serial();
  for(unsigned i=0;i<p->versions;i++)if(p->version[i].state==3 && p->version[i].serial>complete)return 0;
  vk_uab_batch_flush(p->queued_batch);
  vkDestroyDescriptorPool(V.device,p->pool,NULL);
  if(uab_pool_descriptors<p->descriptors)die("UAB pool budget underflow");
  uab_pool_descriptors-=p->descriptors;
  for(unsigned i=0;i<p->versions;i++){free(p->version[i].fixed);free(p->version[i].initialized);free(p->version[i].touched);free(p->version[i].unpublished);}
  free(p->sets);free(p);return 1;
}

static int uab_program_limits(const vk_uab_schema *const sets[6]) {
  uint32_t counts[11]={0},stage_counts[6][11]={{0}},size=0;
  for(unsigned slot=0;slot<6;slot++)if(sets[slot]) {
    const vk_uab_schema *s=sets[slot];if(s->set!=slot)return 0;
    size=slot+1;
    for(unsigned n=0;n<s->count;n++) {
      const VkDescriptorSetLayoutBinding *b=&s->bindings[n];counts[b->descriptorType]++;
      if(b->stageFlags&~(VK_SHADER_STAGE_ALL_GRAPHICS|VK_SHADER_STAGE_COMPUTE_BIT))return 0;
      for(unsigned stage=0;stage<6;stage++)if(b->stageFlags&(1u<<stage))stage_counts[stage][b->descriptorType]++;
    }
  }
  if(!size || size>V.props.limits.maxBoundDescriptorSets)return 0;
  const VkPhysicalDeviceDescriptorIndexingProperties *p=&V.uab_properties;
#define UAB_AGG(c,prefix) \
  if((c)[0]+(c)[1]>p->prefix##Samplers || (c)[1]+(c)[2]+(c)[4]>p->prefix##SampledImages || \
     (c)[3]+(c)[5]>p->prefix##StorageImages || (c)[6]>p->prefix##UniformBuffers || \
     (c)[7]>p->prefix##StorageBuffers || (c)[10]>p->prefix##InputAttachments)return 0
  UAB_AGG(counts,maxDescriptorSetUpdateAfterBind);
  for(unsigned stage=0;stage<6;stage++) {
    UAB_AGG(stage_counts[stage],maxPerStageDescriptorUpdateAfterBind);
    uint64_t all=0;for(unsigned t=0;t<11;t++)all+=stage_counts[stage][t];
    if(all>p->maxPerStageUpdateAfterBindResources)return 0;
  }
#undef UAB_AGG
  return 1;
}
int vk_uab_pipeline_layout_create(vk_uab_packet *const sets[6],
    const VkPushConstantRange *push,uint32_t push_count,VkPipelineLayout *out) {
  if(!sets || !out || push_count>1 || (push_count && !push))return 0;
  if(push_count && (!push->stageFlags || (push->stageFlags&~(VK_SHADER_STAGE_ALL_GRAPHICS|VK_SHADER_STAGE_COMPUTE_BIT)) ||
      !push->size || push->size%4 || push->offset%4 || push->offset>V.props.limits.maxPushConstantsSize ||
      push->size>V.props.limits.maxPushConstantsSize-push->offset))return 0;
  const vk_uab_schema *schemas[6]={0};VkDescriptorSetLayout layouts[6]={0},empty=VK_NULL_HANDLE;unsigned size=0;
  for(unsigned i=0;i<6;i++)if(sets[i]){schemas[i]=&sets[i]->schema;layouts[i]=sets[i]->layout;size=i+1;}
  if(!uab_program_limits(schemas))return 0;
  // Empty layouts have no descriptor backing and no cache identity to reuse.
  for(unsigned i=0;i<size;i++)if(!layouts[i]) {
    if(!empty) {VkDescriptorSetLayoutCreateInfo ci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
      if(vkCreateDescriptorSetLayout(V.device,&ci,NULL,&empty)!=VK_SUCCESS)return 0;}
    layouts[i]=empty;
  }
  VkPipelineLayoutCreateInfo ci={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=size,.pSetLayouts=layouts,.pushConstantRangeCount=push_count,.pPushConstantRanges=push};
  VkResult result=vkCreatePipelineLayout(V.device,&ci,NULL,out);
  if(empty)vkDestroyDescriptorSetLayout(V.device,empty,NULL);
  return result==VK_SUCCESS;
}

VkDescriptorSetLayout vk_shader_uab_layout(vk_shader *shader) {
  if(!shader || !V.retained_draws)return VK_NULL_HANDLE;
  if(shader->uab_descriptors)return shader->uab_descriptors;
  if(!shader->nb)return shader->uab_descriptors=descriptor_layout_get(NULL,0);
  vk_uab_schema schema;uint32_t count;
  if(!vk_uab_schema_from_shader(shader,0,&schema) || !uab_schema_limits(&schema,&count))return VK_NULL_HANDLE;
  VkDescriptorBindingFlags flags[VK_UAB_MAX_BINDINGS];
  for(unsigned n=0;n<schema.count;n++)flags[n]=uab_buffer_type(schema.bindings[n].descriptorType)?VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT:0;
  return shader->uab_descriptors=descriptor_layout_get_flags(schema.bindings,schema.count,
      VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,flags);
}
VkPipelineLayout vk_program_uab_layout(vk_shader *const shaders[6]) {
  if(!shaders || !V.retained_draws)return VK_NULL_HANDLE;
  vk_uab_schema schemas[6];const vk_uab_schema *by_set[6]={0};VkDescriptorSetLayout layouts[6];unsigned count=0;int biased=0;
  if(!empty_layout)empty_layout=descriptor_layout_get(NULL,0);
  for(unsigned i=0;i<6;i++)layouts[i]=empty_layout;
  for(unsigned i=0;i<6;i++)if(shaders[i]) {
    vk_shader *s=shaders[i];
    if(s->set>=6 || by_set[s->set] || !vk_uab_schema_from_shader(s,0,&schemas[i]))return VK_NULL_HANDLE;
    by_set[s->set]=&schemas[i];layouts[s->set]=vk_shader_uab_layout(s);
    if(!layouts[s->set])return VK_NULL_HANDLE;
    if(count<=s->set)count=s->set+1;biased|=!!s->storage_bias_mask;
  }
  if(!uab_program_limits(by_set))return VK_NULL_HANDLE;
  return pipeline_layout_get(layouts,count,biased);
}

#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
VkDescriptorSetLayout vk_test_uab_plain_layout(const vk_uab_schema *s) {return descriptor_layout_get(s->bindings,s->count);}
#endif
