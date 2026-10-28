// Optional paired descriptor representation. Base metadata remains suitable for
// future-command planners. Selection is explicit at the actual draw, and its
// local snapshot is consumed by descriptor binding even if a registry slot is
// retargeted later. Content/format is never a compile-time game assumption.
static atomic_int texel_storage_requested=-1;
static uint64_t texel_storage_selected_recorded,texel_storage_fallback_recorded;
static atomic_uint_fast64_t texel_storage_selected,texel_storage_fallback;
static void texel_storage_publish(void) {
  atomic_store(&texel_storage_selected,texel_storage_selected_recorded);
  atomic_store(&texel_storage_fallback,texel_storage_fallback_recorded);
}
void vk_texel_storage_enabled(int enabled) { atomic_store(&texel_storage_requested,!!enabled); }
void vk_texel_storage_stats(uint64_t *selected,uint64_t *fallback) {
  *selected=atomic_load(&texel_storage_selected);*fallback=atomic_load(&texel_storage_fallback);
}
static int texel_storage_mode(void) {
  int requested=atomic_load(&texel_storage_requested);if(requested>=0)return requested;
  static int initial=-1;
  if(initial<0) {
    const char *v=getenv("DM_VULKAN_TEXEL_STORAGE");
    if(v && strcmp(v,"0") && strcmp(v,"1"))die("DM_VULKAN_TEXEL_STORAGE expects 0 or 1");
    initial=v && !strcmp(v,"1");
  }
  return initial;
}
static void texel_storage_load(vk_shader *base,const uint32_t *original,size_t original_bytes) {
  // Fragment-only first experiment. Compute/vertex keep their original typed
  // texel semantics; the emitter itself does not identify games or stages.
  if(base->stage!=1 || !texel_storage_mode())return;
  base->texel_storage_checked=1;
  const char *root=getenv("DM_VULKAN_SHADERS");char path[2048];
  if(!root || snprintf(path,sizeof path,"%s/%s.tssbo",root,base->key)>=(int)sizeof path)
    die("Vulkan texel alternative path");
  FILE *file=fopen(path,"rb");if(!file) {
    if(errno!=ENOENT)die("Vulkan texel alternative open failed: %s",base->key);
    return;
  }
  fclose(file);size_t bytes;uint32_t *data=read_cache(base->key,"tssbo",&bytes);
  if(bytes<108 || (bytes&3) || data[0]!=UINT32_C(0x31535444) || data[1]!=1 || data[2]!=base->set ||
      !data[3] || data[3]>VK_TEXEL_STORAGE_BINDINGS || data[4]<5 || data[4]>(32u<<18) || data[5]>(32u<<18) ||
      bytes!=88+(size_t)data[3]*4+(size_t)data[4]*4+(size_t)data[5]*4 ||
      (!!data[5])!=!!base->specialization_module)
    die("Vulkan texel alternative recipe invalid: %s",base->key);
  uint8_t digest[32];CC_SHA256(original,original_bytes,digest);
  if(memcmp(digest,data+6,32))die("Vulkan texel alternative base mismatch: %s",base->key);
  if(base->specialization_module) {
    size_t spec_bytes;void *spec=read_cache(base->key,"spec",&spec_bytes);
    CC_SHA256(spec,spec_bytes,digest);free(spec);
  } else memset(digest,0,sizeof digest);
  if(memcmp(digest,data+14,32))die("Vulkan texel alternative specialization mismatch: %s",base->key);
  vk_shader *alt=malloc(sizeof *alt);if(!alt)die("Vulkan texel alternative allocation");
  *alt=*base;alt->next=NULL;alt->texel_storage_alternative=NULL;
  alt->canonical_base=base->canonical_base;
  // Independent cache identity, even when the proved read-set union is the
  // same. Handles/layout and every dynamic resource check remain separate.
  alt->driver_schema[0]=alt->driver_schema[1]=NULL;
  // DPR1 is paired with the original module/schema, not this representation.
  // Do not inherit its module, immutable recipe or admitted tuple state.
  alt->predicate=NULL;
  // This paired schema has its own modules, not the DSP3 range candidate.
  alt->range_module=VK_NULL_HANDLE;alt->range_min_bytes=alt->range_slot=0;alt->range_disabled=1;
  alt->bindings=malloc(base->nb*sizeof *alt->bindings);if(!alt->bindings)die("Vulkan texel alternative reflection");
  memcpy(alt->bindings,base->bindings,base->nb*sizeof *alt->bindings);
  unsigned converted=0,storage=0;uint8_t selected[128]={0};
  for(unsigned i=0;i<data[3];i++) {
    unsigned binding=data[22+i];if(binding>=128 || selected[binding])die("Vulkan texel alternative binding");
    selected[binding]=1;
  }
  for(unsigned i=0;i<alt->nb;i++) {
    vk_binding *b=&alt->bindings[i];
    if(selected[b->binding]) {
      if(b->kind!=7 || b->count!=1 || b->reg<0 || b->reg>=128)
        die("Vulkan texel alternative reflection mismatch: %s",base->key);
      b->kind=VK_BINDING_TEXEL_STORAGE;converted++;
    }
    if(descriptor_type(b)==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)storage+=b->count;
  }
  if(converted!=data[3])die("Vulkan texel alternative absent binding: %s",base->key);
  if(storage>V.props.limits.maxPerStageDescriptorStorageBuffers || storage>V.props.limits.maxDescriptorSetStorageBuffers) {
    free(alt->bindings);free(alt);free(data);return;
  }
  driver_schema_clone(alt,base);
  const uint32_t *spirv=data+22+data[3];
  if(spirv[0]!=0x07230203 || (data[5] && spirv[data[4]]!=0x07230203))die("Vulkan texel alternative SPIR-V");
  VkShaderModuleCreateInfo ci={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=(size_t)data[4]*4,.pCode=spirv};
  VK_CHECK(vkCreateShaderModule(V.device,&ci,NULL,&alt->module));alt->specialization_module=VK_NULL_HANDLE;
  if(data[5]) {
    ci.codeSize=(size_t)data[5]*4;ci.pCode=spirv+data[4];
    VK_CHECK(vkCreateShaderModule(V.device,&ci,NULL,&alt->specialization_module));
  }
  alt->image_read_count=0;alt->image_read_slots=NULL;
  memset(alt->dynamic_uniform_ordinals,0,sizeof alt->dynamic_uniform_ordinals);
  memset(alt->dynamic_uniform_count,0,sizeof alt->dynamic_uniform_count);
  memset(alt->specialization_values,0,sizeof alt->specialization_values);alt->specialization_count=0;
  alt->compute=VK_NULL_HANDLE;alt->compute_layout=VK_NULL_HANDLE;
  shader_descriptor_layouts(alt);base->texel_storage_alternative=alt;free(data);
}
vk_shader *vk_shader_texel_pick(vk_shader *base,uint32_t extra_storage_budget,vk_texel_storage_selection *selection) {
  selection->shader=base;selection->count=0;
  if(!base || !texel_storage_mode() || !base->texel_storage_alternative)return base;
  vk_shader *alt=base->texel_storage_alternative;
  for(unsigned n=0;n<alt->nb;n++) {
    const vk_binding *b=&alt->bindings[n];if(b->kind!=VK_BINDING_TEXEL_STORAGE)continue;
    tex_t texture;view_t view;int has_view;uint64_t offset;
    unsigned tid=S.tex[alt->stage][b->reg]&0xfffff;
    if(!vk_texture_descriptor_view(tid,&texture,&view,&has_view) || texture.d.target!=NVN_TEXTURE_TARGET_BUFFER ||
        (has_view&&view.set_target&&view.target!=NVN_TEXTURE_TARGET_BUFFER) ||
        (has_view&&view.set_format?view.format:texture.d.format)!=NVN_FORMAT_RGBA32F ||
        !texture.mem || !texture.L.size || (texture.L.size&15) || texture.L.size>V.props.limits.maxStorageBufferRange ||
        !vk_pool_range_offset((uintptr_t)texture.mem,texture.L.size,&offset) ||
        offset%V.props.limits.minStorageBufferOffsetAlignment)goto fallback;
    if(selection->count==VK_TEXEL_STORAGE_BINDINGS)die("Vulkan texel selection capacity");
    selection->reads[selection->count].binding=b->binding;
    selection->reads[selection->count].address=(uintptr_t)texture.mem;
    selection->reads[selection->count].size=texture.L.size;
    selection->count++;
  }
  if(selection->count>extra_storage_budget)goto fallback;
  selection->shader=alt;texel_storage_selected_recorded++;return alt;
fallback:
  selection->count=0;texel_storage_fallback_recorded++;return base;
}
static VkDescriptorBufferInfo texel_storage_descriptor(vk_shader *s,const vk_binding *binding,
    const vk_texel_storage_selection *selection) {
  if(!selection || selection->shader!=s)die("Vulkan texel alternative lacks draw-local proof");
  for(unsigned n=0;n<selection->count;n++)if(selection->reads[n].binding==(unsigned)binding->binding) {
    uint64_t address=selection->reads[n].address,size=selection->reads[n].size,offset;vk_buffer *buffer;
    VkPipelineStageFlags stage=producer_stage(s->stage);
    // Same dependency/upload order and exact source range as vk_texel_buffer.
    // Readonly is not immutable; GPU COPY/compute/SSBO aliases remain visible.
    vk_buffer_shader_read(address,size,stage);
    if(!vk_pool_address_stage(address,size,(void **)&buffer,&offset,stage) ||
        offset%V.props.limits.minStorageBufferOffsetAlignment)
      die("Vulkan texel alternative pool changed during draw");
    return (VkDescriptorBufferInfo){.buffer=buffer->handle,.offset=offset,.range=size};
  }
  die("Vulkan texel alternative missing binding");
}
