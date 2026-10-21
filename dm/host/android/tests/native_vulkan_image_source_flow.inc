#if defined(DM_VULKAN_TEST_EMBED) && defined(DM_VULKAN_IMAGE_SOURCE_WRAP) && !defined(DM_ANDROID_APP)
extern void vk_test_image_source_flow(int);
extern int vk_test_image_source_origin(const tex_t *,uint64_t *);
extern unsigned vk_test_image_source_live(void);
extern uint64_t vk_test_image_source_bytes(void);
void __real_vk_texture_cpu_preflight(const tex_t *);
int __real_vk_texture_native_pool_collect(const tex_t *,VkPipelineStageFlags,int,vk_pool_requirement_sink,void *);
static int source_flow_observe;
static unsigned source_flow_cpu_calls,source_flow_native_calls;
static const tex_t *source_flow_last_cpu,*source_flow_last_native;
static uint64_t source_flow_last_native_serial;
static struct {const tex_t *pointer;uint64_t generation;} source_flow_origins[8*640];
static unsigned source_flow_origin_count;
void __wrap_vk_texture_cpu_preflight(const tex_t *t) {
  uint64_t generation;int id=source_flow_observe?vk_test_image_source_origin(t,&generation):0;
  if(id) {
    unsigned n=0;for(;n<source_flow_origin_count;n++)if(source_flow_origins[n].pointer==t)break;
    CHECK(n<8*640);if(n==source_flow_origin_count)source_flow_origin_count++;
    source_flow_origins[n].pointer=t;source_flow_origins[n].generation=generation;source_flow_cpu_calls++;source_flow_last_cpu=t;
  }
  __real_vk_texture_cpu_preflight(t);
}
int __wrap_vk_texture_native_pool_collect(const tex_t *t,VkPipelineStageFlags stages,int writable,
    vk_pool_requirement_sink sink,void *context) {
  uint64_t generation;int id=source_flow_observe?vk_test_image_source_origin(t,&generation):0;
  if(id) {
    unsigned n=0;for(;n<source_flow_origin_count;n++)if(source_flow_origins[n].pointer==t)break;
    CHECK(n<source_flow_origin_count && source_flow_origins[n].generation==generation);
    tex_t current;CHECK(vk_texture_descriptor(id-1,&current));CHECK(!memcmp(&current,t,sizeof current));
    CHECK(!writable);source_flow_native_calls++;source_flow_last_native=t;source_flow_last_native_serial=t->serial;
  }
  return __real_vk_texture_native_pool_collect(t,stages,writable,sink,context);
}
extern void vk_test_shader_image_binding_mask_build(vk_shader *);
static int image_source_owned_sink(void *context,const vk_pool_requirement *range) {(void)context;(void)range;return 1;}
static void image_source_value_boundaries(void) {
  vk_state saved=S;S.targets=(p_set_rt){0};
  vk_binding binding={.kind=6,.count=1,.reg=0};
  vk_shader shader={.stage=1,.nb=1,.bindings=&binding},*list[6]={NULL,&shader};
  vk_test_shader_image_binding_mask_build(&shader);CHECK(!shader.sampled_source_schema);
  shader.pool_binding_mask_ready=1;vk_test_shader_image_binding_mask_build(&shader);CHECK(shader.sampled_source_schema==2);
  tex_t a={.serial=UINT64_C(0x1000000000000100)},b={.serial=UINT64_C(0x1000000000000200)};
  mtl_set_tex_pool_n(64);mtl_reg_tex(37,&a,NULL);mtl_reg_tex(38,&b,NULL);S.tex[1][0]=37;
  int parent=vk_shader_image_sources_begin();CHECK(parent);vk_shader_cpu_sources(list,&S,0);
  CHECK(vk_test_image_source_live()==1);const tex_t *parent_value=source_flow_last_cpu;
  mtl_reg_tex(37,&b,NULL); // Epoch changes after actual CPU consumer, before admission.
  CHECK(vk_shader_pool_ranges_state(list,&S,0,image_source_owned_sink,NULL));
  CHECK(source_flow_last_native==parent_value && source_flow_last_native_serial==b.serial);
  mtl_reg_tex(37,&a,NULL);vk_shader_cpu_sources(list,&S,0);parent_value=source_flow_last_cpu;
  S.tex[1][0]=38; // Handle changes without needing another registry publication.
  CHECK(vk_shader_pool_ranges_state(list,&S,0,image_source_owned_sink,NULL));
  CHECK(source_flow_last_native==parent_value && source_flow_last_native_serial==b.serial);
  S.tex[1][0]=37;vk_shader_cpu_sources(list,&S,0);parent_value=source_flow_last_cpu;
  int child=vk_shader_image_sources_begin();S.tex[1][0]=38;vk_shader_cpu_sources(list,&S,0);
  CHECK(source_flow_last_cpu!=parent_value);CHECK(vk_shader_pool_ranges_state(list,&S,0,image_source_owned_sink,NULL));
  vk_shader_image_sources_end(child);S.tex[1][0]=37;
  unsigned before=source_flow_native_calls;CHECK(vk_shader_pool_ranges_state(list,&S,0,image_source_owned_sink,NULL));
  CHECK(source_flow_native_calls==before+1 && source_flow_last_native==parent_value && source_flow_last_native_serial==a.serial);
  mtl_set_tex_pool_n(0);CHECK(!vk_shader_pool_ranges_state(list,&S,0,image_source_owned_sink,NULL));
  mtl_set_tex_pool_n(64);CHECK(vk_shader_pool_ranges_state(list,&S,0,image_source_owned_sink,NULL));
  int nested[8];for(unsigned n=0;n<8;n++)nested[n]=vk_shader_image_sources_begin();
  vk_shader_cpu_sources(list,&S,0);CHECK(!vk_test_image_source_live()); // bounded overflow is original path
  for(unsigned n=8;n;n--)vk_shader_image_sources_end(nested[n-1]);
  CHECK(vk_shader_pool_ranges_state(list,&S,0,image_source_owned_sink,NULL));
  vk_shader_image_sources_end(parent);CHECK(!vk_test_image_source_live());
  uint64_t bytes=vk_test_image_source_bytes();vk_test_image_source_flow(0);
  CHECK(!vk_shader_image_sources_begin());vk_shader_cpu_sources(list,&S,0);
  CHECK(!vk_test_image_source_live() && vk_test_image_source_bytes()==bytes);vk_shader_image_sources_end(0);
  vk_test_image_source_flow(1);S=saved;mtl_set_tex_pool_n(4);
  LOG("[image source value oracle] epoch/handle changes, limit rejection, nested and overflow isolation, operation end and native-OFF no-build passed");
}
static unsigned source_flow_native_requirements;
static vk_pool_requirement source_flow_native_requirement;
static int image_source_native_sink(void *context,const vk_pool_requirement *range) {
  (void)context;source_flow_native_requirements++;source_flow_native_requirement=*range;return 1;
}
static void image_source_native_table_metadata(void) {
  const char *path=getenv("DM_IMAGE_SOURCE_FLOW_NATIVE_TABLE"),*payload=getenv("DM_IMAGE_SOURCE_FLOW_NATIVE_PAYLOAD");
  CHECK(path && payload);FILE *file=fopen(path,"rb");CHECK(file);
  CHECK(!fseek(file,0,SEEK_END));long size=ftell(file);CHECK(size>0 && size<1048576);rewind(file);
  nvn_native_table *table=malloc(size);CHECK(table && fread(table,1,size,file)==(size_t)size);CHECK(!fclose(file));
  CHECK(table->file_bytes==(uint64_t)size && table->count==1);
  const nvn_native_entry *entry=(const void *)(table+1);nvn_native_identity identity={0};
  memcpy(identity.layout,entry->recipe.layout_uuid,16);memcpy(identity.device,entry->recipe.device_uuid,16);
  memcpy(identity.driver,entry->recipe.driver_uuid,16);memcpy(identity.driver_sha256,table->driver_sha256,32);
  memcpy(identity.producer_sha256,table->producer_sha256,32);
  // Original table validator; this is a metadata test using an already baked
  // f830 resource, not an assertion that Lavapipe is the baked driver.
  CHECK(nvn_native_table_install(table,size,table->pack_sha256,&identity));
  uint64_t bytes_count=(entry->recipe.image_size+65535)&~UINT64_C(65535);uint8_t *data;
  CHECK(entry->recipe.image_alignment<=65536 && !posix_memalign((void **)&data,65536,bytes_count));
  file=fopen(payload,"rb");CHECK(file && fread(data,1,entry->recipe.image_size,file)==entry->recipe.image_size);
  CHECK(fgetc(file)==EOF);CHECK(!fclose(file));
  NVNmemoryPoolBuilder pb;NVNmemoryPool pool;CALL(nvnMemoryPoolBuilderSetDefaults,&pb);
  CALL(nvnMemoryPoolBuilderSetDevice,&pb,&device);CALL(nvnMemoryPoolBuilderSetStorage,&pb,data,bytes_count);
  CALL(nvnMemoryPoolBuilderSetFlags,&pb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&pb));
  NVNtextureBuilder tb;NVNtexture texture;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
  CALL(nvnTextureBuilderSetWidth,&tb,entry->description.width);CALL(nvnTextureBuilderSetHeight,&tb,entry->description.height);
  CALL(nvnTextureBuilderSetLevels,&tb,entry->description.levels);CALL(nvnTextureBuilderSetFormat,&tb,entry->description.format);
  NVNpackagedTextureLayout layout={{'D','M','N',0,1,0,0,0}};layout.layout[3]=table->version;
  CALL(nvnTextureBuilderSetPackagedTextureLayout,&tb,&layout);CALL(nvnTextureBuilderSetStorage,&tb,&pool,0);
  CHECK(CALL(nvnTextureBuilderGetStorageSize,&tb)==entry->recipe.image_size);
  CHECK(CALL(nvnTextureInitialize,&texture,&tb));
  vk_state saved=S;S.targets=(p_set_rt){0};S.tex[1][0]=39;mtl_set_tex_pool_n(64);mtl_reg_tex(39,(const tex_t *)&texture,NULL);
  vk_binding binding={.kind=6,.count=1,.reg=0};vk_shader shader={.stage=1,.nb=1,.bindings=&binding,.pool_binding_mask_ready=1};
  vk_shader *list[6]={NULL,&shader};vk_test_shader_image_binding_mask_build(&shader);
  int token=vk_shader_image_sources_begin();vk_shader_cpu_sources(list,&S,0);const tex_t *source=source_flow_last_cpu;
  unsigned before=source_flow_native_calls;CHECK(vk_shader_pool_ranges_state(list,&S,0,image_source_native_sink,NULL));
  CHECK(source_flow_native_calls==before+1 && source_flow_last_native==source && source_flow_native_requirements==1);
  CHECK(source_flow_native_requirement.address==(uintptr_t)data && source_flow_native_requirement.size==entry->recipe.image_size);
  CHECK(source_flow_native_requirement.binding_kind==VK_POOL_REQUIRE_NATIVE_IMAGE && !source_flow_native_requirement.writable);
  CHECK(source_flow_native_requirement.stages==VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  vk_shader_image_sources_end(token);S=saved;mtl_set_tex_pool_n(4);
  CALL(nvnTextureFinalize,&texture);CALL(nvnMemoryPoolFinalize,&pool);free(data);free(table);
  LOG("[image source native metadata] original baked table/opaque bytes -> original Builder -> CPU source -> original collector -> nonempty native range passed; no ASTC GPU execution");
}
static void image_source_flow_check(void) {
  CHECK(source_flow_cpu_calls>0 && source_flow_native_calls>0);
  CHECK(!vk_test_image_source_live());
  uint64_t bytes=vk_test_image_source_bytes();CHECK(bytes<=8*640*168+2048);
  LOG("[image source flow oracle] shared source argument in CPU/pre-native collector: cpu=%u native=%u bytes=%llu",source_flow_cpu_calls,source_flow_native_calls,(unsigned long long)bytes);
}
static void image_source_flow_test(void) {
  mtl_init();CHECK(V.options.pool_window_bytes && (vk_pool_shared_mode() || V.options.pool_defer_publication));
  uint8_t controls[13][0x880];for(unsigned n=0;n<13;n++)memset(controls[n],n,0x880);
  vk_test_image_source_flow(1);source_flow_observe=1;image_source_value_boundaries();
  logical_preflight_test(controls[3],controls[4]);
  recipe_reentrant_test(controls[12],controls[4]);
  image_source_native_table_metadata();image_source_flow_check();
  source_flow_observe=0;vk_test_image_source_flow(0);
}
#else
static void image_source_flow_test(void) {}
#endif
