// Owned SPIR-V/metadata fixtures only: no game CPU code or game state.
static int pixel_local_loader_test(void) {
  const char *root=getenv("DM_PIXEL_LOCAL_LOADER_FIXTURE");if(!root)return 0;
  V.options.shader_root=root;setenv("DM_VULKAN_SHADERS",root,1);
  vk_texel_storage_enabled(1);
  const char *real_keys=getenv("DM_PIXEL_LOCAL_REAL_KEYS");
  if(real_keys) {
    char *keys=strdup(real_keys),*state=NULL;if(!keys)die("pixel-local fixture key allocation");
    for(char *key=strtok_r(keys,",",&state);key;key=strtok_r(NULL,",",&state)) {
      vk_shader *shader=vk_shader_load_key(key);
      const vk_pixel_local_proof *p=vk_shader_pixel_local(shader,shader->module);
      if(!p || (shader->stage==1?!p->count:!p->uv_count))die("pixel-local actual artifact proof: %s",key);
      LOG("[pixel-local actual loader fixture] key=%s stage=%u inputs=%u uv=%u",key,shader->stage,p->count,p->uv_count);
    }
    free(keys);return 0;
  }
  vk_shader *fs=vk_shader_load_key("0000000000000001");
  const vk_pixel_local_proof *proof=vk_shader_pixel_local(fs,fs->module);
  if(!vk_pixel_local_enabled()) {
    if(proof)die("pixel-local default must be off");
    LOG("[pixel-local loader fixture] default off, original module retained");return 0;
  }
  if(!proof || fs->canonical_base!=fs || proof->shader->canonical_base!=fs ||
     proof->count!=4 || proof->uv_location!=1 || proof->shader==fs || proof->source_shader!=fs || proof->module==fs->module)
    die("pixel-local four-input module/schema fixture");
  for(unsigned i=0;i<4;i++)if(proof->bindings[i].binding!=i+1 || proof->bindings[i].input_index!=i)
    die("pixel-local full-group fixed mapping");
  for(unsigned repeat=0;repeat<32;repeat++)
    if(vk_shader_pixel_local(fs,fs->module)!=proof)die("pixel-local immutable proof identity");
  for(unsigned omit=0;omit<4;omit++) {
    unsigned mask=15u&~(1u<<omit);
    const vk_pixel_local_proof *subset=vk_shader_pixel_local_subset(fs,fs->module,mask);
    if(!subset || subset->count!=3 || subset->group_count!=4 || subset->selection_mask!=mask ||
       subset->source_shader!=fs || subset->shader==proof->shader || subset->module==proof->module)
      die("pixel-local exact subset immutable identity");
    for(unsigned i=0;i<fs->nb;i++)if(fs->bindings[i].binding==(int)omit+1 &&
        subset->shader->bindings[i].kind!=3)die("pixel-local omitted sampler was converted");
  }
  if(vk_shader_pixel_local_subset(fs,fs->module,3) || vk_shader_pixel_local_subset(fs,fs->module,16))
    die("pixel-local unsupported subset must fallback");
  vk_shader orphan=*fs;orphan.canonical_base=NULL;
  if(vk_shader_pixel_local(&orphan,fs->module))die("pixel-local missing owner accepted");
  vk_shader invalid_owner=*fs;invalid_owner.canonical_base=fs;orphan.canonical_base=&invalid_owner;
  if(vk_shader_pixel_local(&orphan,fs->module))die("pixel-local invalid owning base accepted");
  vk_shader *vs=vk_shader_load_key("0000000000000002");
  const vk_pixel_local_proof *vertex=vk_shader_pixel_local(vs,vs->module);
  if(!vertex || vertex->count || vertex->uv_count!=1 || vertex->position_attribute || vertex->uv[0].mode!=1 ||
     vertex->uv[0].attribute!=3 || vertex->uv[0].scale[0].offset!=48 || vertex->uv[0].bias[1].offset!=44 || vertex->module!=vs->module)
    die("pixel-local exact vertex FMA proof");
  if(vk_shader_pixel_local(fs,vs->module))die("pixel-local actual module identity rejection");
  for(unsigned i=3;i<=17;i++) {
    if(i==11)continue;
    char key[17];snprintf(key,sizeof key,"%016x",i);vk_shader *bad=vk_shader_load_key(key);
    if(vk_shader_pixel_local(bad,bad->module))die("pixel-local corrupted proof accepted: %s",key);
  }
  vk_shader *legacy=vk_shader_load_key("0000000000000012");
  const vk_pixel_local_proof *legacy_full=vk_shader_pixel_local(legacy,legacy->module);
  if(!legacy_full || legacy_full->count!=4 || vk_shader_pixel_local_subset(legacy,legacy->module,7))
    die("pixel-local DPL1 full proof/fallback compatibility");
  vk_shader *all=vk_shader_load_key("000000000000000b");
  if(!all->specialization_module || !all->predicate || !all->texel_storage_alternative || !all->texel_storage_alternative->specialization_module)
    die("pixel-local five source modules were not loaded");
  vk_shader *sources[]={all,all,all,all->texel_storage_alternative,all->texel_storage_alternative};
  VkShaderModule modules[]={all->module,all->specialization_module,all->predicate->module,
    all->texel_storage_alternative->module,all->texel_storage_alternative->specialization_module};
  const vk_pixel_local_proof *records[5];
  for(unsigned i=0;i<5;i++) {
    records[i]=vk_shader_pixel_local(sources[i],modules[i]);
    if(!records[i] || sources[i]->canonical_base!=all || records[i]->shader->canonical_base!=all ||
       records[i]->count!=1 || records[i]->bindings[0].binding!=3 || records[i]->source_shader!=sources[i])
      die("pixel-local source module independent proof %u",i);
    for(unsigned j=0;j<i;j++)if(records[i]==records[j] || records[i]->module==records[j]->module || records[i]->shader==records[j]->shader)
      die("pixel-local variants inherited another module proof");
    if(records[i]->shader->nb!=sources[i]->nb ||
       records[i]->shader->storage_descriptor_count!=sources[i]->storage_descriptor_count ||
       records[i]->shader->has_writable_buffers!=sources[i]->has_writable_buffers ||
       records[i]->shader->image_read_count+1!=sources[i]->image_read_count)
      die("pixel-local unconverted resource classification changed");
    for(unsigned k=0;k<sources[i]->nb;k++) {
      const vk_binding *before=&sources[i]->bindings[k],*after=&records[i]->shader->bindings[k];
      if(before->binding!=3 && memcmp(before,after,sizeof *before))
        die("pixel-local changed an unconverted descriptor");
      if(before->binding==3 && (after->kind!=VK_BINDING_PIXEL_LOCAL || after->reg!=before->reg ||
          after->binding!=before->binding || after->count!=before->count || after->flags!=before->flags))
        die("pixel-local changed the original input resource identity");
    }
  }
  LOG("[pixel-local loader fixture] 4 inputs+VS FMA+5 independent base/spec/pred/tssbo/tssbo_spec modules; 14 corruption/closure controls rejected; DPL2 exact masks and DPL1 compatibility");
  return 0;
}
