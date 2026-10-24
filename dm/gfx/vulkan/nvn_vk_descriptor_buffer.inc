#include "nvn_vk_descriptor_buffer.h"
static size_t descriptor_buffer_size(VkDescriptorType type) {
  const VkPhysicalDeviceDescriptorBufferPropertiesEXT *p=&V.descriptor_buffer_properties;
  switch(type) {
  case VK_DESCRIPTOR_TYPE_SAMPLER:return p->samplerDescriptorSize;
  case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:return p->combinedImageSamplerDescriptorSize;
  case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:return p->sampledImageDescriptorSize;
  case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:return p->storageImageDescriptorSize;
  case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:return p->inputAttachmentDescriptorSize;
  case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:return V.features.robustBufferAccess?p->robustUniformTexelBufferDescriptorSize:p->uniformTexelBufferDescriptorSize;
  case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:return V.features.robustBufferAccess?p->robustStorageTexelBufferDescriptorSize:p->storageTexelBufferDescriptorSize;
  case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:return V.features.robustBufferAccess?p->robustUniformBufferDescriptorSize:p->uniformBufferDescriptorSize;
  case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:return V.features.robustBufferAccess?p->robustStorageBufferDescriptorSize:p->storageBufferDescriptorSize;
  default:return 0;
  }
}
const vk_descriptor_buffer_schema *vk_descriptor_buffer_schema_get(vk_shader *s) {
  if(!V.descriptor_buffers || !s)return NULL;
  vk_descriptor_buffer_schema *q=s->descriptor_buffer_schema;
  if(q && q->owner==s)return q->layout?q:NULL;
  q=calloc(1,sizeof *q);if(!q)return NULL;q->owner=s;
  if(!vk_uab_schema_from_shader(s,0,&q->schema))goto reject;
  q->layout=descriptor_layout_get_flags(q->schema.bindings,q->schema.count,VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,NULL);
  if(!q->layout){free(q);return NULL;}
  vkGetDescriptorSetLayoutSizeEXT(V.device,q->layout,&q->bytes);
  VkDeviceSize align=V.descriptor_buffer_properties.descriptorBufferOffsetAlignment;
  if(q->bytes>UINT64_MAX-(align-1))goto reject;
  q->stride=(q->bytes+align-1)/align*align;
  if(q->stride>V.descriptor_buffer_properties.maxResourceDescriptorBufferRange ||
      q->stride>V.descriptor_buffer_properties.maxSamplerDescriptorBufferRange)goto reject;
  for(unsigned n=0;n<q->schema.count;n++) {
    q->sizes[n]=descriptor_buffer_size(q->schema.bindings[n].descriptorType);
    vkGetDescriptorSetLayoutBindingOffsetEXT(V.device,q->layout,q->schema.bindings[n].binding,&q->offsets[n]);
    if(!q->sizes[n] || q->offsets[n]>q->bytes || q->sizes[n]>q->bytes-q->offsets[n])goto reject;
  }
  s->descriptor_buffer_schema=q;return q;
reject:
  q->layout=VK_NULL_HANDLE;s->descriptor_buffer_schema=q;return NULL;
}
VkPipelineLayout vk_program_descriptor_buffer_layout(vk_shader *const shaders[6]) {
  if(!V.descriptor_buffers || !shaders)return VK_NULL_HANDLE;
  static VkDescriptorSetLayout empty;
  if(!empty)empty=descriptor_layout_get_flags(NULL,0,VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,NULL);
  VkDescriptorSetLayout layouts[6];for(unsigned n=0;n<6;n++)layouts[n]=empty;
  unsigned seen=0,count=0;int biased=0;
  for(unsigned n=0;n<6;n++)if(shaders[n]) {
    vk_shader *s=shaders[n];if(s->set>=6 || (seen&(1u<<s->set)))return VK_NULL_HANDLE;
    seen|=1u<<s->set;
    const vk_descriptor_buffer_schema *q=vk_descriptor_buffer_schema_get(s);if(!q)return VK_NULL_HANDLE;
    layouts[s->set]=q->layout;if(count<=s->set)count=s->set+1;biased|=!!s->storage_bias_mask;
  }
  return count?pipeline_layout_materialize(pipeline_layout_entry_get(layouts,count,biased)):VK_NULL_HANDLE;
}
