// Generic exact Uniform selector specialization. Optional offline recipe;
// unavailable CPU data and bounded variant overflow retain the base module.
// This never waits for a producer, changes a guest buffer or freezes a frame.
static atomic_int uniform_spec_enabled=1;
static atomic_uint_fast64_t uniform_spec_hits,uniform_spec_pending,uniform_spec_limits,uniform_spec_variants;
void vk_uniform_specialization_enabled(int enabled) { atomic_store(&uniform_spec_enabled,!!enabled); }
void vk_uniform_specialization_stats(uint64_t *hits,uint64_t *pending,uint64_t *limits,uint64_t *variants,int *enabled) {
  *hits=atomic_load(&uniform_spec_hits);*pending=atomic_load(&uniform_spec_pending);
  *limits=atomic_load(&uniform_spec_limits);*variants=atomic_load(&uniform_spec_variants);*enabled=atomic_load(&uniform_spec_enabled);
}
static atomic_int uniform_predicate_enabled;
static struct {
  uint64_t hits,pending,limits,variants,rejected,masks[16];
} uniform_predicate_recorded;
static atomic_uint_fast64_t uniform_predicate_hits,uniform_predicate_pending,
  uniform_predicate_limits,uniform_predicate_variants,uniform_predicate_rejects,
  uniform_predicate_masks[16];
void vk_uniform_predicate_enabled(int enabled) { atomic_store(&uniform_predicate_enabled,!!enabled); }
void vk_uniform_predicate_stats(uint64_t *hits,uint64_t *pending,uint64_t *limits,
                               uint64_t *variants,uint64_t *rejected,uint64_t masks[16],int *enabled) {
  *hits=atomic_load(&uniform_predicate_hits);*pending=atomic_load(&uniform_predicate_pending);
  *limits=atomic_load(&uniform_predicate_limits);*variants=atomic_load(&uniform_predicate_variants);
  *rejected=atomic_load(&uniform_predicate_rejects);*enabled=atomic_load(&uniform_predicate_enabled);
  for(unsigned n=0;n<16;n++) masks[n]=atomic_load(&uniform_predicate_masks[n]);
}
static void uniform_predicate_publish(void) {
  atomic_store_explicit(&uniform_predicate_hits,uniform_predicate_recorded.hits,memory_order_relaxed);
  atomic_store_explicit(&uniform_predicate_pending,uniform_predicate_recorded.pending,memory_order_relaxed);
  atomic_store_explicit(&uniform_predicate_limits,uniform_predicate_recorded.limits,memory_order_relaxed);
  atomic_store_explicit(&uniform_predicate_variants,uniform_predicate_recorded.variants,memory_order_relaxed);
  atomic_store_explicit(&uniform_predicate_rejects,uniform_predicate_recorded.rejected,memory_order_relaxed);
  for(unsigned n=0;n<16;n++) atomic_store_explicit(&uniform_predicate_masks[n],uniform_predicate_recorded.masks[n],memory_order_relaxed);
}
static void uniform_predicate_load(vk_shader *s,const uint32_t *base,size_t base_bytes,
                                   const void *old_recipe,size_t old_bytes) {
  const char *root=getenv("DM_VULKAN_SHADERS");char path[2048];
  if(!root || snprintf(path,sizeof path,"%s/%s.pred",root,s->key)>=(int)sizeof path)
    die("Vulkan Uniform predicate path");
  FILE *file=fopen(path,"rb");if(!file) {
    if(errno!=ENOENT)die("Vulkan Uniform predicate open failed: %s",s->key);
    return;
  }
  fclose(file);size_t bytes;uint32_t *data=read_cache(s->key,"pred",&bytes);
  // DPR1: nine header words, base SHA256, complete old .spec SHA256,
  // four fixed predicate slots, then SPIR-V. No unversioned DSP extension.
  if(bytes<184 || (bytes&3) || data[0]!=UINT32_C(0x31525044) || data[1]!=1 || data[2] ||
      data[3]>60000 || data[3]==s->specialization_id || data[4]>=128 || data[5]<3 || data[5]>=19 ||
      data[6]>65532 || (data[6]&3) || !data[7] || data[7]>4 || data[8]<5 ||
      data[8]>(32u<<18) || bytes!=164+(size_t)data[8]*4 || data[41]!=UINT32_C(0x07230203))
    die("Vulkan Uniform predicate recipe invalid: %s",s->key);
  uint8_t digest[32];CC_SHA256(base,base_bytes,digest);
  if(memcmp(digest,data+9,32))die("Vulkan Uniform predicate base mismatch: %s",s->key);
  CC_SHA256(old_recipe,old_bytes,digest);
  if(memcmp(digest,data+17,32))die("Vulkan Uniform predicate specialization mismatch: %s",s->key);
  unsigned found=0,mask=0;
  for(unsigned n=0;n<s->nb;n++) {
    const vk_binding *b=&s->bindings[n];
    if(b->binding==(int)data[4] && b->reg==(int)data[5] && b->kind==1 && b->count==1)found++;
  }
  if(found!=1)die("Vulkan Uniform predicate binding mismatch: %s",s->key);
  for(unsigned n=0;n<4;n++) {
    const uint32_t *p=data+25+n*4;
    if(n>=data[7]) {
      if(p[0]||p[1]||p[2]||p[3])die("Vulkan Uniform predicate unused slot: %s",s->key);
    } else {
      if((p[0]!=1 && p[0]!=2) || p[3]>=4 || (mask&(1u<<p[3])))
        die("Vulkan Uniform predicate operation invalid: %s",s->key);
      mask|=1u<<p[3];
    }
  }
  unsigned old_id=0,new_id=0;uint32_t spec_target[2]={0},spec_type[2]={0};
  const uint32_t *module=data+41;
  for(size_t n=5;n<data[8];) {
    uint32_t words=module[n]>>16,op=module[n]&65535;
    if(!words || words>data[8]-n)die("Vulkan Uniform predicate SPIR-V malformed: %s",s->key);
    if(op==71 && words>=3 && module[n+2]==1) {
      if(words!=4)die("Vulkan Uniform predicate SpecId malformed: %s",s->key);
      if(module[n+3]==s->specialization_id)old_id++,spec_target[0]=module[n+1];
      else if(module[n+3]==data[3])new_id++,spec_target[1]=module[n+1];
      else die("Vulkan Uniform predicate unexpected SpecId: %s",s->key);
    }
    n+=words;
  }
  if(old_id!=1 || new_id!=1 || !spec_target[0] || !spec_target[1] || spec_target[0]==spec_target[1] ||
      spec_target[0]>=module[3] || spec_target[1]>=module[3])
    die("Vulkan Uniform predicate SpecId mismatch: %s",s->key);
  unsigned constants[2]={0},types[2]={0};
  for(size_t n=5;n<data[8];n+=module[n]>>16) {
    uint32_t words=module[n]>>16,op=module[n]&65535;
    if(op==50 && words==4)for(unsigned k=0;k<2;k++)if(module[n+2]==spec_target[k]) {
      constants[k]++;spec_type[k]=module[n+1];
    }
  }
  for(size_t n=5;n<data[8];n+=module[n]>>16) {
    uint32_t words=module[n]>>16,op=module[n]&65535;
    if(op==21 && words==4 && module[n+2]==32 && !module[n+3])
      for(unsigned k=0;k<2;k++)if(module[n+1]==spec_type[k])types[k]++;
  }
  if(constants[0]!=1 || constants[1]!=1 || types[0]!=1 || types[1]!=1)
    die("Vulkan Uniform predicate uint32 specialization type mismatch: %s",s->key);
  // Like base/spec shader modules, this object is owned for device/process
  // lifetime. Ordinary queue finish/submission recycling must not free it;
  // a future global shader teardown must destroy module then free the object.
  vk_uniform_predicate *p=calloc(1,sizeof *p);if(!p)die("Vulkan Uniform predicate allocation");
  p->slot=data[5]-3;p->offset=data[6];p->id=data[3];p->count=data[7];
  memcpy(p->predicates,data+25,sizeof p->predicates);
  VkShaderModuleCreateInfo ci={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize=(size_t)data[8]*4,.pCode=data+41};
  VK_CHECK(vkCreateShaderModule(V.device,&ci,NULL,&p->module));s->predicate=p;free(data);
}
static void uniform_specialization_load(vk_shader *s,const uint32_t *base,size_t base_bytes) {
  if(s->stage!=1) return;
  const char *root=getenv("DM_VULKAN_SHADERS");char path[2048];
  if(!root || snprintf(path,sizeof path,"%s/%s.spec",root,s->key)>=(int)sizeof path)
    die("Vulkan Uniform specialization path");
  FILE *file=fopen(path,"rb");if(!file) {
    if(errno!=ENOENT)die("Vulkan Uniform specialization open failed: %s",s->key);
    return;
  }
  fclose(file);size_t bytes;uint32_t *data=read_cache(s->key,"spec",&bytes);
  if(bytes<80 || (data[0]!=UINT32_C(0x31505344)&&data[0]!=UINT32_C(0x32505344)) ||
     (data[1]!=1 && !(data[0]==UINT32_C(0x32505344)&&data[1]==257)) ||
     data[2]>=128 || data[3]<3 || data[3]>=19 || data[4]>65532 || (data[4]&3) ||
     data[5]>60000 || data[6]>(32u<<18) || bytes!=60+(size_t)data[6]*4 || data[15]!=0x07230203)
    die("Vulkan Uniform specialization recipe invalid: %s",s->key);
  uint8_t digest[32];CC_SHA256(base,base_bytes,digest);
  if(memcmp(digest,data+7,32))die("Vulkan Uniform specialization base mismatch: %s",s->key);
  unsigned found=0;
  for(unsigned n=0;n<s->nb;n++) {
    const vk_binding *b=&s->bindings[n];
    if(b->binding==(int)data[2]&&b->reg==(int)data[3]&&b->kind==1&&b->count==1)found++;
  }
  if(found!=1)die("Vulkan Uniform specialization binding mismatch: %s",s->key);
  VkShaderModuleCreateInfo ci={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize=(size_t)data[6]*4,.pCode=data+15};
  VK_CHECK(vkCreateShaderModule(V.device,&ci,NULL,&s->specialization_module));
  s->specialization_integer_only=(data[1]&256)!=0;
  s->specialization_slot=data[3]-3;s->specialization_offset=data[4];s->specialization_id=data[5];
  uniform_predicate_load(s,base,base_bytes,data,bytes);free(data);
}
static int uniform_specialization_read(vk_shader *s,uint32_t *bits,uint64_t *address) {
  p_bind_buf u=S.ubo[1][s->specialization_slot];
  if(u.size<s->specialization_offset+4 || u.addr>UINT64_MAX-s->specialization_offset ||
      vk_pool_try_read(u.addr+s->specialization_offset,4,bits)!=1) {
    atomic_fetch_add(&uniform_spec_pending,1);return 0;
  }
  if(address)*address=u.addr+s->specialization_offset;
  // Raw-word replacement keeps the existing floating exceptional fallback.
  uint32_t exponent=*bits&UINT32_C(0x7f800000);
  return s->specialization_integer_only ||
    (exponent!=UINT32_C(0x7f800000) && (exponent || !(*bits&UINT32_C(0x007fffff))));
}
static VkShaderModule uniform_specialization_admit(vk_shader *s,uint32_t bits,uint32_t *value) {
  unsigned n=0;for(;n<s->specialization_count;n++)if(s->specialization_values[n]==bits)break;
  if(n==s->specialization_count) {
    if(n==16){atomic_fetch_add(&uniform_spec_limits,1);return s->module;}
    s->specialization_values[s->specialization_count++]=bits;atomic_fetch_add(&uniform_spec_variants,1);
  }
  *value=bits;atomic_fetch_add(&uniform_spec_hits,1);return s->specialization_module;
}
VkShaderModule vk_uniform_specialization_pick(vk_shader *s,uint32_t *value) {
  *value=0;
  if(!s || !s->specialization_module || s->specialization_disabled || !atomic_load(&uniform_spec_enabled))return s?s->module:VK_NULL_HANDLE;
  uint32_t bits;if(!uniform_specialization_read(s,&bits,NULL))return s->module;
  return uniform_specialization_admit(s,bits,value);
}
VkShaderModule vk_uniform_hybrid_pick(vk_shader *s,uint32_t values[2]) {
  values[0]=values[1]=0;
  // No extra source read or variant lookup in the original default path.
  if(!s || !s->predicate || s->predicate->disabled || !atomic_load(&uniform_predicate_enabled))
    return vk_uniform_specialization_pick(s,values);
  if(!s->specialization_module || s->specialization_disabled || !atomic_load(&uniform_spec_enabled))return s->module;
  uint32_t raw;uint64_t raw_address;
  if(!uniform_specialization_read(s,&raw,&raw_address))return s->module;
  vk_uniform_predicate *p=s->predicate;p_bind_buf u=S.ubo[1][p->slot];uint32_t bits;
  int available=u.size>=p->offset+4 && u.addr<=UINT64_MAX-p->offset;
  if(available) {
    if(u.addr+p->offset==raw_address)bits=raw;
    else available=vk_pool_try_read(u.addr+p->offset,4,&bits)==1;
  }
  if(!available) {
    uniform_predicate_recorded.pending++;
    return uniform_specialization_admit(s,raw,values);
  }
  uint32_t mask=0;
  for(unsigned n=0;n<p->count;n++) {
    uint32_t equal=(bits&p->predicates[n].and_mask)==p->predicates[n].compare;
    mask|=(equal^(p->predicates[n].op==2))<<p->predicates[n].bit;
  }
  unsigned n=0;
  for(;n<p->variants;n++)if(p->values[n][0]==raw && p->values[n][1]==mask)break;
  if(n==p->variants) {
    if(n==16) {
      uniform_predicate_recorded.limits++;
      return uniform_specialization_admit(s,raw,values);
    }
    p->values[n][0]=raw;p->values[n][1]=mask;p->variants++;
    uniform_predicate_recorded.variants++;
  }
  values[0]=raw;values[1]=mask;uniform_predicate_recorded.hits++;
  uniform_predicate_recorded.masks[mask]++;return p->module;
}
VkShaderModule vk_uniform_predicate_rejected(vk_shader *s,uint32_t values[2]) {
  s->predicate->disabled=1;uniform_predicate_recorded.rejected++;
  uint32_t raw=values[0];values[0]=values[1]=0;
  return uniform_specialization_admit(s,raw,values);
}
