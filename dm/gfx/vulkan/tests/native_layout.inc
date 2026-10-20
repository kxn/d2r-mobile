// Metadata and the real NVN entrypoints, no native image/GPU execution claim.
// Input is a declared table produced from actual resource bake artifacts.
#include "mbedtls/sha256.h"
static int native_layout_checks;
static vk_pool_requirement native_layout_requirement;
static unsigned native_layout_requirement_count;
static int native_layout_sink(void *context,const vk_pool_requirement *r) {
  (void)context;native_layout_requirement=*r;native_layout_requirement_count++;return 1;
}

#define NATIVE_CHECK(x) do {if(!(x))die("Native layout fixture: %s",#x);native_layout_checks++;}while(0)
static void native_table_rehash(nvn_native_table *table) {
  memset(table->digest,0,32);
  mbedtls_sha256((const void *)table,table->file_bytes,table->digest,0);
}
static int native_layout_test(const char *path) {
  extern unsigned vk_test_native_address_index(void);
  NATIVE_CHECK(vk_test_native_address_index()==10);
  FILE *file=fopen(path,"rb");NATIVE_CHECK(file!=NULL);
  NATIVE_CHECK(!fseek(file,0,SEEK_END));long count=ftell(file);NATIVE_CHECK(count>0);
  NATIVE_CHECK(!fseek(file,0,SEEK_SET));nvn_native_table *table=malloc((size_t)count);
  NATIVE_CHECK(table && fread(table,1,(size_t)count,file)==(size_t)count);NATIVE_CHECK(!fclose(file));
  NATIVE_CHECK(table->file_bytes==(uint64_t)count && table->count==1);
  nvn_native_entry *entry=(void *)(table+1);nvn_native_identity id={0};
  memcpy(id.layout,entry->recipe.layout_uuid,16);memcpy(id.device,entry->recipe.device_uuid,16);
  memcpy(id.driver,entry->recipe.driver_uuid,16);memcpy(id.driver_sha256,table->driver_sha256,32);
  memcpy(id.producer_sha256,table->producer_sha256,32);
  // Each rejected installation must leave selection untouched.
  id.layout[0]^=1;NATIVE_CHECK(!nvn_native_table_install(table,count,table->pack_sha256,&id));id.layout[0]^=1;
  id.driver_sha256[0]^=1;NATIVE_CHECK(!nvn_native_table_install(table,count,table->pack_sha256,&id));id.driver_sha256[0]^=1;
  id.producer_sha256[0]^=1;NATIVE_CHECK(!nvn_native_table_install(table,count,table->pack_sha256,&id));id.producer_sha256[0]^=1;
  nvn_native_table *bad=malloc(count);NATIVE_CHECK(bad!=NULL);memcpy(bad,table,count);
  ((nvn_native_entry *)(bad+1))->recipe.mips[8].offset=UINT64_MAX;
  nvn_native_recipe *recipe=&((nvn_native_entry *)(bad+1))->recipe;
  memset(recipe->digest,0,32);mbedtls_sha256((const void *)recipe,sizeof *recipe,recipe->digest,0);
  native_table_rehash(bad);NATIVE_CHECK(!nvn_native_table_install(bad,count,table->pack_sha256,&id));free(bad);
  if(table->version==2) {
    bad=malloc(count);NATIVE_CHECK(bad!=NULL);memcpy(bad,table,count);
    nvn_native_family *f=(void *)((nvn_native_entry *)(bad+1)+bad->count);
    f->thumbnail_mip=entry->description.levels;native_table_rehash(bad);
    NATIVE_CHECK(!nvn_native_table_install(bad,count,table->pack_sha256,&id));
    memcpy(bad,table,count);f->source_layout[0]=6;native_table_rehash(bad);
    NATIVE_CHECK(!nvn_native_table_install(bad,count,table->pack_sha256,&id));
    memcpy(bad,table,count);memcpy(f->source_layout,"DMN",3);native_table_rehash(bad);
    NATIVE_CHECK(!nvn_native_table_install(bad,count,table->pack_sha256,&id));free(bad);
  }
  NATIVE_CHECK(!nvn_native_table_install(table,count-1,table->pack_sha256,&id));
  NATIVE_CHECK(nvn_native_table_install(table,count,table->pack_sha256,&id));
  NATIVE_CHECK(!nvn_native_table_install(table,count,table->pack_sha256,&id));
  NVNtextureBuilder builder={0};h_nvnTextureBuilderSetDefaults(&builder);
  h_nvnTextureBuilderSetWidth(&builder,entry->description.width);
  h_nvnTextureBuilderSetHeight(&builder,entry->description.height);
  h_nvnTextureBuilderSetLevels(&builder,entry->description.levels);
  h_nvnTextureBuilderSetFormat(&builder,entry->description.format);
  NVNpackagedTextureLayout layout={{'D','M','N',0,1,0,0,0}};layout.layout[3]=table->version;
  h_nvnTextureBuilderSetPackagedTextureLayout(&builder,&layout);
  NATIVE_CHECK(TB(&builder)->d.pkg_block_h==0);
  NATIVE_CHECK(h_nvnTextureBuilderGetStorageSize(&builder)==entry->recipe.image_size);
  NATIVE_CHECK(h_nvnTextureBuilderGetStorageAlignment(&builder)==entry->recipe.image_alignment);
  uint8_t *owned=NULL;size_t owned_size=entry->recipe.image_size+65536;
  NATIVE_CHECK(!posix_memalign((void **)&owned,65536,owned_size));
  pool_t pool={.mem=owned,.size=owned_size};
  h_nvnTextureBuilderSetStorage(&builder,(NVNmemoryPool *)&pool,entry->recipe.image_alignment);
  NVNtexture texture={0};NATIVE_CHECK(h_nvnTextureInitialize(&texture,&builder));
  NATIVE_CHECK(vk_texture_native_pool_collect(CTX(&texture),VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,native_layout_sink,NULL));
  NATIVE_CHECK(native_layout_requirement_count==1 && native_layout_requirement.address==(uintptr_t)CTX(&texture)->mem &&
      native_layout_requirement.size==entry->recipe.image_size && native_layout_requirement.stages==VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  tex_t ordinary_for_plan=*CTX(&texture);ordinary_for_plan.has_layout=0;
  NATIVE_CHECK(vk_texture_native_pool_collect(&ordinary_for_plan,VK_PIPELINE_STAGE_TRANSFER_BIT,1,native_layout_sink,NULL));
  NATIVE_CHECK(native_layout_requirement_count==1);
  NVNtextureView view={0};h_nvnTextureViewSetDefaults(&view);
  for(unsigned n=0;n<entry->recipe.levels;n++) {
    h_nvnTextureViewSetLevels(&view,n,1);
    NATIVE_CHECK((uint64_t)h_nvnTextureGetViewOffset(&texture,&view)==entry->recipe.mips[n].offset);
  }
  if(table->version==2) {
    const nvn_native_family *family=(const void *)(entry+table->count);unsigned first=family->thumbnail_mip;
    NATIVE_CHECK(first>0 && first<entry->description.levels);
    nvn_texdesc tail=entry->description,source;tail.samples=TB(&builder)->d.samples;tail.width>>=first;tail.height>>=first;tail.levels-=first;
    if(!tail.width)tail.width=1;if(!tail.height)tail.height=1;uint8_t original[8];
    NATIVE_CHECK(nvn_native_layout_classify(layout.layout,&tail,&source,original)==2);
    NATIVE_CHECK(!nvn_native_layout_resolve(layout.layout,&tail));
    NATIVE_CHECK(!memcmp(original,family->source_layout,8) && source.pkg_block_h==family->source_layout[0]+1);
    nvn_texlayout storage;nvn_tex_layout(&source,&storage);
    h_nvnTextureBuilderSetWidth(&builder,tail.width);h_nvnTextureBuilderSetHeight(&builder,tail.height);
    h_nvnTextureBuilderSetLevels(&builder,tail.levels);
    NATIVE_CHECK(h_nvnTextureBuilderGetStorageSize(&builder)==storage.size);
    NATIVE_CHECK(h_nvnTextureBuilderGetStorageAlignment(&builder)==storage.align);
    NVNtexture thumbnail={0};NATIVE_CHECK(h_nvnTextureInitialize(&thumbnail,&builder));
    NATIVE_CHECK(!nvn_native_layout_is_marker(CTX(&thumbnail)->layout));
    NATIVE_CHECK(!memcmp(CTX(&thumbnail)->layout,original,8) && !memcmp(&CTX(&thumbnail)->d,&source,sizeof source));
    for(unsigned n=0;n<tail.levels;n++) {h_nvnTextureViewSetLevels(&view,n,1);
      NATIVE_CHECK(h_nvnTextureGetViewOffset(&thumbnail,&view)==nvn_tex_level_offset(&source,n));}
    h_nvnTextureFinalize(&thumbnail);
    tail.flags=NVN_TEXTURE_FLAGS_IMAGE;NATIVE_CHECK(!nvn_native_layout_classify(layout.layout,&tail,NULL,NULL));
    tail.flags=0;tail.width++;NATIVE_CHECK(!nvn_native_layout_classify(layout.layout,&tail,NULL,NULL));
    h_nvnTextureBuilderSetWidth(&builder,entry->description.width);h_nvnTextureBuilderSetHeight(&builder,entry->description.height);
    h_nvnTextureBuilderSetLevels(&builder,entry->description.levels);
  }
  nvn_texdesc mismatch=entry->description;mismatch.width++;
  NATIVE_CHECK(!nvn_native_layout_resolve(layout.layout,&mismatch));
  tex_t ordinary=*CTX(&texture);ordinary.has_layout=0;
  NATIVE_CHECK(!h_nvnTextureCompare(&texture,(const NVNtexture *)&ordinary));
  NATIVE_CHECK(!h_nvnTextureCompare((const NVNtexture *)&ordinary,&texture));
  layout.layout[3]=0xff;NATIVE_CHECK(nvn_native_layout_is_marker(layout.layout));
  NATIVE_CHECK(!nvn_native_layout_resolve(layout.layout,&entry->description));layout.layout[3]=table->version;
  layout.layout[4]=2;NATIVE_CHECK(!nvn_native_layout_resolve(layout.layout,&entry->description));
  // Installation owns its table; destroying the caller copy cannot change it.
  memset(table,0,count);free(table);
  NATIVE_CHECK(h_nvnTextureBuilderGetStorageSize(&builder)==CTX(&texture)->L.size);
  h_nvnTextureFinalize(&texture);free(owned);
  printf("{\"test\":\"native-layout-NVN-metadata\",\"checks\":%d,\"native_GPU_executed\":false,\"result\":\"passed\"}\n",native_layout_checks);
  return 0;
}
#undef NATIVE_CHECK
