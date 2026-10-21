// DDK1: optional, all-path offline proof. GPU module bytes are unchanged.
// Loader failures keep the old exact full key. Shader objects (and recipes)
// currently have device/process lifetime, like their VkShaderModule objects.
static void driver_schema_clone(vk_shader *dst,const vk_shader *src) {
  for(unsigned n=0;n<2;n++) {
    dst->driver_schema[n]=NULL;
    if(src->driver_schema[n]) {
      size_t bytes=sizeof(vk_driver_schema)+src->driver_schema[n]->count*sizeof(uint16_t);
      dst->driver_schema[n]=malloc(bytes);
      if(!dst->driver_schema[n])die("Vulkan driver schema allocation");
      memcpy(dst->driver_schema[n],src->driver_schema[n],bytes);
    }
  }
}
// DSP1 is independent of the optional sparse equality key. Its all-path
// bound includes zero-tail loads and every fingerprinted module variant.
// Missing/old/invalid sidecars retain the original 64 KiB descriptor contract.
static void driver_span_load(vk_shader *s) {
  char path[2048];
  if(!V.options.shader_root || snprintf(path,sizeof path,"%s/%s.dspan",V.options.shader_root,s->key)>=(int)sizeof path)return;
  FILE *file=fopen(path,"rb");if(!file)return;
  uint32_t data[25];size_t bytes=fread(data,1,sizeof data,file);fclose(file);
  if(bytes!=96 || data[0]!=UINT32_C(0x31505344) || data[1]!=1 ||
      data[2]!=s->stage || data[3]!=s->set || memcmp(data+4,s->fingerprint,32))return;
  uint8_t digest[32];CC_SHA256(data,64,digest);if(memcmp(digest,data+16,32))return;
  for(unsigned n=0;n<2;n++) {
    uint32_t binding=data[12+n*2],end=data[13+n*2];unsigned slot=n*2,found=0;
    if(end>65536 || (end&3) || binding>=128 || (slot==0 && s->stage!=0 && s->stage!=1 && s->stage!=5))continue;
    for(unsigned k=0;k<s->nb;k++) {
      const vk_binding *b=&s->bindings[k];
      if(b->kind==1 && b->reg==(int)slot && b->binding==(int)binding && b->count==1)found++;
    }
    if(found==1 && end<=4096)s->driver_span[n]=4096;
  }
  s->driver_span_module=s->module;
  // The paired texel-storage modules are part of the same DSP fingerprint.
  // Other future/shallow module alternatives retain their old module token
  // and cannot consume this authorization after installing a new module.
  if(s->texel_storage_alternative) {
    vk_shader *alt=s->texel_storage_alternative;
    memcpy(alt->driver_span,s->driver_span,sizeof s->driver_span);
    alt->driver_span_module=alt->module;
  }
}
static void driver_schema_load(vk_shader *s) {
  char path[2048];
  if(!V.options.shader_root || snprintf(path,sizeof path,"%s/%s.dkey",V.options.shader_root,s->key)>=(int)sizeof path)return;
  FILE *file=fopen(path,"rb");if(!file)return;
  uint32_t data[96];size_t bytes=fread(data,1,sizeof data,file);
  int complete=feof(file);fclose(file);
  if(!complete || bytes<96 || (bytes&3) || data[0]!=UINT32_C(0x314b4444) || data[1]!=1 ||
      data[2]!=s->stage || data[3]!=s->set || memcmp(data+4,s->fingerprint,32))return;
  uint8_t digest[32];CC_SHA256(data,bytes-32,digest);
  if(memcmp(digest,(uint8_t *)data+bytes-32,32))return;
  struct { uint32_t count;const uint16_t *words; } schemas[2]={{0}};
  uint32_t valid=0;size_t at=48,end=bytes-32;
  for(unsigned n=0;n<2;n++) {
    if(end-at<8)return;
    uint32_t binding,count;memcpy(&binding,(uint8_t *)data+at,4);memcpy(&count,(uint8_t *)data+at+4,4);at+=8;
    if(count==UINT32_MAX)continue;
    unsigned slot=n*2,full=slot==2?264:s->stage==5?96:48;
    if((slot==0 && s->stage!=0 && s->stage!=1 && s->stage!=5) || count>32 || count*2>=full ||
        end-at<((count+1)&~1u)*2)return;
    unsigned found=0;
    for(unsigned k=0;k<s->nb;k++) {
      const vk_binding *b=&s->bindings[k];
      if(b->kind==1 && b->reg==(int)slot && b->binding==(int)binding && b->count==1)found++;
    }
    if(found!=1)return;
    const uint16_t *words=(const void *)((uint8_t *)data+at);
    for(unsigned k=0;k<count;k++)if(words[k]>=full || (k && words[k]<=words[k-1]))return;
    if((count&1) && words[count])return;
    schemas[n].count=count;schemas[n].words=words;valid|=1u<<n;
    at+=((count+1)&~1u)*2;
  }
  if(at!=end)return;
  for(unsigned n=0;n<2;n++)if(valid&(1u<<n)) {
    size_t size=sizeof(vk_driver_schema)+schemas[n].count*2;
    vk_driver_schema *schema=malloc(size);if(!schema)die("Vulkan driver schema allocation");
    schema->count=schemas[n].count;memcpy(schema->words,schemas[n].words,schema->count*2);
    s->driver_schema[n]=schema;
  }
  if(s->texel_storage_alternative)driver_schema_clone(s->texel_storage_alternative,s);
}
static size_t driver_sparse_key_build(const vk_driver_schema *schema,int slot,unsigned stage,uint8_t *key) {
  for(unsigned k=0;k<schema->count;k++) {
    unsigned word=schema->words[k];uint32_t value;
    if(slot==2) {
      value=word<128?S.tex[stage][word]:word<256?S.samp[stage][word-128]:S.img[stage][word-256];
    } else {
      const p_bind_buf *b=word>=48?&S.ubo[stage][(word-48)/3]:&S.ssbo[stage][word/3];
      value=word%3==2?(uint32_t)b->size:(uint32_t)(b->addr>>((word%3)*32));
    }
    memcpy(key+k*4,&value,4);
  }
  // The existing word hash reads u64 chunks. Pad the last half, never read
  // uninitialized scratch and never fold schema identity out of the key.
  size_t bytes=schema->count*4;if(bytes&7){memset(key+bytes,0,4);bytes+=4;}
  return bytes;
}
#ifdef DM_VULKAN_TEST_EMBED
unsigned vk_driver_schema_words(const vk_shader *s,unsigned slot) {
  if(!s || (slot!=0 && slot!=2) || !s->driver_schema[slot/2])return UINT32_MAX;
  return s->driver_schema[slot/2]->count;
}
#endif
