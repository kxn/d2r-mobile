// Optional DPL1 loader, owned by the recording thread. Artifact closure is
// checked once; a missing/invalid optional recipe preserves the normal path.
typedef struct {
  uint32_t kind,words;
  const uint32_t *code;
  vk_pixel_local_proof proof;
} pixel_local_record;
typedef struct {
  void *blob;
  unsigned count;
  pixel_local_record records[];
} pixel_local_cache;
static _Atomic int pixel_local_initial=-1;
static pthread_once_t pixel_local_once=PTHREAD_ONCE_INIT;
static void pixel_local_initialize(void) {
  const char *value=getenv("DM_VULKAN_PIXEL_LOCAL");
  if(value && strcmp(value,"0") && strcmp(value,"1"))
    LOG("[vulkan] invalid DM_VULKAN_PIXEL_LOCAL; keeping disabled");
  atomic_store_explicit(&pixel_local_initial,value && !strcmp(value,"1"),memory_order_relaxed);
}
int vk_pixel_local_enabled(void) {
  // This immutable scalar publishes no other data. Steady-state readers need
  // one relaxed load; simultaneous first readers sample the environment once.
  int initial=atomic_load_explicit(&pixel_local_initial,memory_order_relaxed);
  if(initial<0) {
    pthread_once(&pixel_local_once,pixel_local_initialize);
    initial=atomic_load_explicit(&pixel_local_initial,memory_order_relaxed);
  }
  return initial;
}
static uint32_t pixel_local_word(const void *memory) {
  const uint8_t *p=memory;
  return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void *pixel_local_read(const char *key,const char *suffix,size_t *bytes) {
  *bytes=0;char path[2048];
  if(!V.options.shader_root || snprintf(path,sizeof path,"%s/%s.%s",V.options.shader_root,key,suffix)>=(int)sizeof path)return NULL;
  FILE *file=fopen(path,"rb");if(!file)return NULL;
  struct stat st;
  if(fstat(fileno(file),&st)||st.st_size<=0||st.st_size>(32<<20)){fclose(file);return NULL;}
  void *data=malloc(st.st_size);
  if(!data || fread(data,1,st.st_size,file)!=(size_t)st.st_size){free(data);fclose(file);return NULL;}
  fclose(file);*bytes=st.st_size;return data;
}
static int pixel_local_source(unsigned kind,void *blobs[5],const size_t bytes[5],const void **source,size_t *size) {
  if(kind==1){*source=blobs[0];*size=bytes[0];return !!*source;}
  unsigned index=kind==2?2:kind==3?3:4;
  const uint8_t *data=blobs[index];size_t length=bytes[index],offset=0,words=0;
  if(!data)return 0;
  if(kind==2){if(length<80)return 0;offset=60;words=(length-60)/4;}
  else if(kind==3){if(length<184)return 0;offset=164;words=pixel_local_word(data+32);}
  else {
    if(length<108)return 0;
    uint32_t count=pixel_local_word(data+12),base_words=pixel_local_word(data+16);
    if(count>16)return 0;
    offset=88+(size_t)count*4;words=base_words;
    if(kind==5){offset+=(size_t)base_words*4;words=pixel_local_word(data+20);}
  }
  if(words<5 || offset>length || words>(length-offset)/4)return 0;
  *source=data+offset;*size=words*4;return pixel_local_word(*source)==UINT32_C(0x07230203);
}
static int pixel_local_ref_valid(const vk_shader *shader,const vk_pixel_local_uniform *ref) {
  if(ref->set!=shader->set || ref->binding>=128 || ref->offset>65532 || (ref->offset&3))return 0;
  for(unsigned k=0;k<shader->nb;k++) {
    const vk_binding *b=&shader->bindings[k];
    if(b->binding==(int)ref->binding && b->kind==1 && b->count==1)return 1;
  }
  return 0;
}
static pixel_local_cache *pixel_local_load(vk_shader *base) {
  size_t bytes;uint8_t *data=pixel_local_read(base->key,"plocal",&bytes);if(!data)return NULL;
  pixel_local_cache *cache=NULL;void *blobs[5]={0};size_t sizes[5]={0};
  if(bytes<192 || (bytes&3) || pixel_local_word(data)!=UINT32_C(0x314c5044) ||
     pixel_local_word(data+4)!=1 || pixel_local_word(data+8)!=bytes ||
     pixel_local_word(data+12)!=base->stage || pixel_local_word(data+16)!=base->set ||
     pixel_local_word(data+20)>5 || pixel_local_word(data+24)!=192 || pixel_local_word(data+28))goto bad;
  const char *suffixes[]={"spv","meta","spec","pred","tssbo"};
  for(unsigned i=0;i<5;i++) {
    blobs[i]=pixel_local_read(base->key,suffixes[i],&sizes[i]);uint8_t digest[32]={0};
    if(blobs[i])CC_SHA256(blobs[i],sizes[i],digest);
    if((i<2 && !blobs[i]) || memcmp(digest,data+32+i*32,32))goto bad;
  }
  unsigned count=pixel_local_word(data+20);
  if(!count) {
    for(unsigned i=0;i<5;i++)free(blobs[i]);
    free(data);return NULL;
  }
  cache=calloc(1,sizeof *cache+(size_t)count*sizeof *cache->records);if(!cache)goto bad;
  cache->count=count;size_t offset=192;unsigned kinds=0;
  for(unsigned n=0;n<cache->count;n++) {
    if(offset>bytes || bytes-offset<96)goto bad;
    const uint8_t *record=data+offset;pixel_local_record *out=&cache->records[n];
    unsigned kind=pixel_local_word(record),length=pixel_local_word(record+4);
    unsigned count=pixel_local_word(record+8),uv_count=pixel_local_word(record+12),words=pixel_local_word(record+24);
    if(kind<1 || kind>5 || (kinds&(1u<<kind)) || length<96 || length>bytes-offset ||
       count>8 || uv_count>16 || pixel_local_word(record+28) ||
       length!=96+(size_t)count*16+(size_t)uv_count*64+(size_t)words*4)goto bad;
    kinds|=1u<<kind;
    const void *source;size_t source_bytes;uint8_t digest[32];
    if(!pixel_local_source(kind,blobs,sizes,&source,&source_bytes))goto bad;
    CC_SHA256(source,source_bytes,digest);if(memcmp(digest,record+32,32))goto bad;
    vk_pixel_local_proof *proof=&out->proof;proof->set=base->set;proof->count=count;
    proof->uv_count=uv_count;proof->uv_location=pixel_local_word(record+16);
    proof->position_attribute=pixel_local_word(record+20);
    memcpy(proof->source_sha256,record+32,32);memcpy(proof->variant_sha256,record+64,32);
    if(base->stage==0) {
      if(kind!=1 || count || !uv_count || words || proof->position_attribute>=16 || proof->uv_location)goto bad;
      uint8_t zero[32]={0};if(memcmp(proof->variant_sha256,zero,32))goto bad;
    } else if(base->stage==1) {
      if(!count || uv_count || words<5 || proof->uv_location>=16 || proof->position_attribute)goto bad;
    } else goto bad;
    const uint8_t *p=record+96;unsigned seen[128]={0};
    for(unsigned i=0;i<count;i++,p+=16) {
      vk_pixel_local_binding *b=&proof->bindings[i];unsigned set=pixel_local_word(p);
      b->binding=pixel_local_word(p+4);b->input_index=pixel_local_word(p+8);b->lod_mask=pixel_local_word(p+12);
      if(set!=base->set || b->binding>=128 || seen[b->binding]++ || b->input_index!=i || !b->lod_mask || (b->lod_mask&~3u))goto bad;
      unsigned found=0;
      // For paired modules unchanged combined-image bindings have the same
      // metadata; converted texel-buffer bindings are never eligible here.
      for(unsigned j=0;j<base->nb;j++)if(base->bindings[j].binding==(int)b->binding && base->bindings[j].kind==3 && base->bindings[j].count==1)found++;
      if(found!=1)goto bad;
    }
    unsigned uv_seen=0;
    for(unsigned i=0;i<uv_count;i++,p+=64) {
      vk_pixel_local_uv *uv=&proof->uv[i];uv->location=pixel_local_word(p);uv->attribute=pixel_local_word(p+4);uv->mode=pixel_local_word(p+8);
      if(uv->location>=16 || uv->attribute>=16 || uv->mode>1 || pixel_local_word(p+12) || (uv_seen&(1u<<uv->location)))goto bad;
      uv_seen|=1u<<uv->location;
      vk_pixel_local_uniform *refs[]={&uv->scale[0],&uv->scale[1],&uv->bias[0],&uv->bias[1]};
      for(unsigned j=0;j<4;j++) {
        refs[j]->set=pixel_local_word(p+16+j*12);refs[j]->binding=pixel_local_word(p+20+j*12);refs[j]->offset=pixel_local_word(p+24+j*12);
        if(uv->mode?!pixel_local_ref_valid(base,refs[j]):(refs[j]->set||refs[j]->binding||refs[j]->offset))goto bad;
      }
    }
    out->kind=kind;out->words=words;out->code=(const uint32_t *)p;
    if(words) {
      if(pixel_local_word(p)!=UINT32_C(0x07230203))goto bad;
      CC_SHA256(p,(size_t)words*4,digest);if(memcmp(digest,proof->variant_sha256,32))goto bad;
    }
    offset+=length;
  }
  if(offset!=bytes)goto bad;
  for(unsigned i=0;i<5;i++)free(blobs[i]);
  cache->blob=data;return cache;
bad:
  for(unsigned i=0;i<5;i++)free(blobs[i]);
  free(cache);free(data);
  LOG("[vulkan] pixel-local artifact rejected: %s",base->key);return NULL;
}
const vk_pixel_local_proof *vk_shader_pixel_local(vk_shader *shader,VkShaderModule actual) {
  if(!shader || !actual || !vk_pixel_local_enabled())return NULL;
  // Lifetime-owned construction association: no global shader-list search on
  // the candidate-draw path. Alternative representations retain this owner.
  vk_shader *base=shader->canonical_base;
  if(!base || base->canonical_base!=base)return NULL;
  unsigned kind=shader==base?(actual==base->module?1:actual==base->specialization_module?2:base->predicate&&actual==base->predicate->module?3:0):
    shader==base->texel_storage_alternative?(actual==shader->module?4:actual==shader->specialization_module?5:0):0;
  if(!kind)return NULL;
  if(!base->pixel_local_checked){base->pixel_local_checked=1;base->pixel_local_data=pixel_local_load(base);}
  pixel_local_cache *cache=base->pixel_local_data;if(!cache)return NULL;
  for(unsigned i=0;i<cache->count;i++) {
    pixel_local_record *r=&cache->records[i];if(r->kind!=kind)continue;
    vk_pixel_local_proof *proof=&r->proof;
    if(proof->shader)return proof;
    if(base->stage==0){proof->source_shader=proof->shader=shader;proof->module=actual;return proof;}
    if(proof->count>V.props.limits.maxPerStageDescriptorInputAttachments || proof->count>V.props.limits.maxDescriptorSetInputAttachments)return NULL;
    vk_shader *alt=calloc(1,sizeof *alt);if(!alt)return NULL;*alt=*shader;
    alt->next=NULL;alt->texel_storage_alternative=NULL;alt->predicate=NULL;
    alt->canonical_base=base;
    alt->pixel_local_data=NULL;alt->pixel_local_checked=1;
    alt->bindings=malloc(shader->nb*sizeof *alt->bindings);if(!alt->bindings){free(alt);return NULL;}
    memcpy(alt->bindings,shader->bindings,shader->nb*sizeof *alt->bindings);
    unsigned converted=0;
    for(unsigned n=0;n<alt->nb;n++)for(unsigned j=0;j<proof->count;j++)if(alt->bindings[n].binding==(int)proof->bindings[j].binding) {
      if(alt->bindings[n].kind!=3 || alt->bindings[n].count!=1){free(alt->bindings);free(alt);return NULL;}
      alt->bindings[n].kind=VK_BINDING_PIXEL_LOCAL;alt->bindings[n].sampler=proof->bindings[j].input_index;converted++;
    }
    if(converted!=proof->count){free(alt->bindings);free(alt);return NULL;}
    VkShaderModuleCreateInfo ci={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=(size_t)r->words*4,.pCode=r->code};
    VkResult result=vkCreateShaderModule(V.device,&ci,NULL,&alt->module);
    if(result!=VK_SUCCESS){free(alt->bindings);free(alt);return NULL;}
    alt->specialization_module=VK_NULL_HANDLE;alt->specialization_count=0;alt->specialization_disabled=1;
    alt->image_read_count=0;alt->image_read_slots=NULL;alt->c1_buffer=(vk_buffer){0};
    memset(alt->dynamic_uniform_ordinals,0,sizeof alt->dynamic_uniform_ordinals);
    memset(alt->dynamic_uniform_count,0,sizeof alt->dynamic_uniform_count);
    alt->compute=VK_NULL_HANDLE;alt->compute_layout=VK_NULL_HANDLE;
    uint8_t identity[64];memcpy(identity,shader->fingerprint,32);memcpy(identity+32,proof->variant_sha256,32);CC_SHA256(identity,sizeof identity,alt->fingerprint);
    shader_descriptor_layouts(alt);proof->source_shader=shader;proof->shader=alt;proof->module=alt->module;return proof;
  }
  return NULL;
}
