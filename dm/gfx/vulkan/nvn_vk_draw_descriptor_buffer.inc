// Completed graphics-context storage. CPU writes never reach an in-flight
// version; original owner/pins and retained-object retirement stay authoritative.
static int draw_descriptor_block_create(draw_parameters *p,draw_parameter_schema *s,draw_parameter_block *b) {
  const vk_descriptor_buffer_schema *q=vk_descriptor_buffer_schema_get(s->shader);
  if(!q || !q->stride || q->stride>UINT64_MAX/DRAW_PARAMETER_BLOCK_SLOTS)return 0;
  uint64_t bytes=q->stride*DRAW_PARAMETER_BLOCK_SLOTS;
  if(bytes>16u*1024*1024-p->descriptor_bytes ||
      bytes>V.descriptor_buffer_properties.maxResourceDescriptorBufferRange ||
      bytes>V.descriptor_buffer_properties.maxSamplerDescriptorBufferRange)return 0;
  b->db_values=calloc((size_t)DRAW_PARAMETER_BLOCK_SLOTS*q->schema.count,sizeof *b->db_values);
  if(!b->db_values)return 0;
  if(vk_buffer_create_optional(&b->db_buffer,bytes,VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT|VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT)!=VK_SUCCESS) {
    free(b->db_values);b->db_values=NULL;return 0;
  }
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  db_test_stats[0]++;
#endif
  VkBufferDeviceAddressInfo info={.sType=VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,.buffer=b->db_buffer.handle};
  b->db_address=vkGetBufferDeviceAddressKHR(V.device,&info);
  if(!b->db_address || b->db_address%V.descriptor_buffer_properties.descriptorBufferOffsetAlignment ||
      !(b->db_buffer.properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
    vk_buffer_destroy(&b->db_buffer);free(b->db_values);b->db_values=NULL;b->db_address=0;
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
    db_test_stats[1]++;
#endif
    return 0;
  }
  b->db_schema=q;p->descriptor_bytes+=bytes;return 1;
}
static int draw_descriptor_block_write(draw_parameter_block *b,unsigned slot,const vk_shader_prepared_values *v,
                                        vk_descriptor_buffer_binding *out) {
  const vk_descriptor_buffer_schema *q=b->db_schema;
  if(!q || q->schema.count!=v->count || q->owner!=v->shader || slot>=DRAW_PARAMETER_BLOCK_SLOTS)return 0;
  vk_split_descriptor_value *old=b->db_values+(size_t)slot*v->count;
  int initialized=b->db_initialized&(UINT32_C(1)<<slot);
  VkDescriptorAddressInfoEXT addresses[128];
  // Validate the complete prepared product before changing any descriptor.
  // All descriptor types are memory contents, not recorded set objects. The
  // caller owns this completed context and allocates each current draw a unique
  // slot. Keep the schema/epoch/pins contract; changed image/sampler/texel bytes
  // do not invalidate a CB which records only this arena address and offset.
  for(unsigned n=0;n<v->count;n++) {
    VkDescriptorType t=q->schema.bindings[n].descriptorType;
    const vk_split_descriptor_value *x=&v->values[n];
    int buffer=t==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || t==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    if(buffer) {
      if(!x->object || !x->range || x->range==VK_WHOLE_SIZE || x->offset>UINT64_MAX-x->range)return 0;
      uint64_t alignment=t==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER?V.props.limits.minUniformBufferOffsetAlignment:V.props.limits.minStorageBufferOffsetAlignment;
      uint64_t maximum=t==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER?V.props.limits.maxUniformBufferRange:V.props.limits.maxStorageBufferRange;
      if(x->offset%alignment || x->range>maximum)return 0;
      if(initialized && !memcmp(old+n,x,sizeof *x))continue;
      // Standard host API over the already consumed/pinned VkBuffer. Never
      // re-resolve guest pools or recover a Buffer from a view handle.
      VkBufferDeviceAddressInfo info={.sType=VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,.buffer=(VkBuffer)x->object};
      VkDeviceAddress base=vkGetBufferDeviceAddressKHR(V.device,&info);
      if(!base || base>UINT64_MAX-x->offset || base+x->offset>UINT64_MAX-x->range)return 0;
      addresses[n]=(VkDescriptorAddressInfoEXT){.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT,.address=base+x->offset,.range=x->range,.format=VK_FORMAT_UNDEFINED};
    } else if(t==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER || t==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER) {
      if(!x->object || v->texel_addresses[n].sType!=VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT ||
          !v->texel_addresses[n].address || !v->texel_addresses[n].range ||
          v->texel_addresses[n].range==VK_WHOLE_SIZE || v->texel_addresses[n].format==VK_FORMAT_UNDEFINED)return 0;
      addresses[n]=v->texel_addresses[n];
    } else if(t==VK_DESCRIPTOR_TYPE_SAMPLER) {if(!x->offset)return 0;}
    else if(!x->object || (t==VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER && !x->offset))return 0;
  }
  uint8_t *destination=b->db_buffer.map+slot*q->stride;
  for(unsigned n=0;n<v->count;n++) {
    const vk_split_descriptor_value *x=&v->values[n];
    if(initialized && !memcmp(old+n,x,sizeof *x))continue;
    VkDescriptorGetInfoEXT get={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,.type=q->schema.bindings[n].descriptorType};
    VkDescriptorImageInfo image={.sampler=(VkSampler)x->offset,.imageView=(VkImageView)x->object,.imageLayout=(VkImageLayout)x->range};
    VkSampler sampler=(VkSampler)x->offset;
    switch(get.type) {
    case VK_DESCRIPTOR_TYPE_SAMPLER:get.data.pSampler=&sampler;break;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:get.data.pCombinedImageSampler=&image;break;
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:get.data.pSampledImage=&image;break;
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:get.data.pStorageImage=&image;break;
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:get.data.pInputAttachmentImage=&image;break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:get.data.pUniformTexelBuffer=&addresses[n];break;
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:get.data.pStorageTexelBuffer=&addresses[n];break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:get.data.pUniformBuffer=&addresses[n];break;
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:get.data.pStorageBuffer=&addresses[n];break;
    default:return 0;
    }
    vkGetDescriptorEXT(V.device,&get,q->sizes[n],destination+q->offsets[n]);old[n]=*x;
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
    db_test_stats[3]++;
    if(b->db_reset_pending&(UINT32_C(1)<<slot))db_test_stats[4]++;
    if(get.type==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER || get.type==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)db_test_stats[7]++;
    if(get.type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || get.type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)db_test_stats[8]++;
#endif
  }
  b->db_initialized|=UINT32_C(1)<<slot;b->db_reset_pending&=~(UINT32_C(1)<<slot);
  *out=(vk_descriptor_buffer_binding){b->db_address,slot*q->stride};return 1;
}
