// Owned wire metadata fixture against production parser/storage/layout code.
// No driver, image allocation, coherency or GPU execution qualification.
#include "nvn_native_layout.h"
#include "mbedtls/sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
_Static_assert(offsetof(nvn_native_recipe,producer_source_sha256)==288,"native producer SHA wire offset");
static unsigned checks;
#define CHECK(x) do {if(!(x)){fprintf(stderr,"metadata check failed line%d: %s\n",__LINE__,#x);return 1;}checks++;}while(0)
static void recipe_seal(nvn_native_recipe *r) {
  memset(r->digest,0,32);mbedtls_sha256((const void *)r,sizeof *r,r->digest,0);
}
static void table_seal(nvn_native_table *t,size_t bytes) {
  memset(t->digest,0,32);mbedtls_sha256((const void *)t,bytes,t->digest,0);
}
int main(int argc,char **argv) {
  CHECK(argc==2);FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long length=ftell(f);CHECK(length>0);rewind(f);
  size_t bytes=(size_t)length;nvn_native_table *t=malloc(bytes),*bad=malloc(bytes);CHECK(t&&bad);CHECK(fread(t,1,bytes,f)==bytes);CHECK(!fclose(f));
  CHECK(t->count==6 && t->version==2 && bytes==160+6*1056);
  nvn_native_entry *e=(void *)(t+1);nvn_native_family *family=(void *)(e+t->count);nvn_native_identity id={0};
  memcpy(id.layout,e->recipe.layout_uuid,16);memcpy(id.device,e->recipe.device_uuid,16);memcpy(id.driver,e->recipe.driver_uuid,16);memcpy(id.driver_sha256,t->driver_sha256,32);memcpy(id.producer_sha256,t->producer_sha256,32);
#define REJECT(change) do {memcpy(bad,t,bytes);nvn_native_entry *b=(void *)(bad+1);nvn_native_family *bf=(void *)(b+t->count);(void)bf;change;for(unsigned k=0;k<t->count;k++)recipe_seal(&b[k].recipe);table_seal(bad,bytes);CHECK(!nvn_native_table_install(bad,bytes,t->pack_sha256,&id));}while(0)
  CHECK(!nvn_native_table_install(t,159,t->pack_sha256,&id));
  CHECK(!nvn_native_table_install(t,bytes-1,t->pack_sha256,&id));
  CHECK(!nvn_native_table_install(t,sizeof *t+(size_t)NVN_NATIVE_MAX_ENTRIES*(sizeof *e+sizeof *family)+1,t->pack_sha256,&id));
  nvn_native_identity wrong=id;wrong.driver_sha256[0]^=1;CHECK(!nvn_native_table_install(t,bytes,t->pack_sha256,&wrong));
  wrong=id;wrong.producer_sha256[0]^=1;CHECK(!nvn_native_table_install(t,bytes,t->pack_sha256,&wrong));
  memcpy(bad,t,bytes);bad->digest[0]^=1;CHECK(!nvn_native_table_install(bad,bytes,t->pack_sha256,&id));
  REJECT(bad->count=NVN_NATIVE_MAX_ENTRIES+1);
  REJECT(bad->entry_bytes++);
  REJECT(b[0].description.format=0x43); // BC2 is outside this incremental contract.
  REJECT(b[0].recipe.format=138);     // BC3 UNORM cannot become sRGB by relabeling.
  REJECT(b[1].recipe.format=137);
  REJECT(b[0].recipe.format=131);     // BC1 not implicitly admitted.
  REJECT(b[0].recipe.width++);
  REJECT(b[0].recipe.image_size=(16u<<20)+1);
  REJECT(b[0].recipe.image_alignment=2048);
  REJECT(b[0].recipe.image_alignment=12288);
  REJECT(b[0].recipe.mips[1].offset=UINT64_MAX);
  REJECT(b[0].recipe.mips[1].offset=0);
  REJECT(b[0].recipe.mips[15].size=1);
  REJECT(b[0].recipe.usage|=8);       // storage usage not added by BC3 support.
  REJECT(b[0].description.flags=1);
  REJECT(bf[0].thumbnail_mip=b[0].description.levels);
  REJECT(bf[0].source_layout[0]=6);
  REJECT(memcpy(bf[0].source_layout,"DMN",3));
  // All failures above must leave the startup selection empty.
  CHECK(nvn_native_table_install(t,bytes,t->pack_sha256,&id));
  CHECK(!nvn_native_table_install(t,bytes,t->pack_sha256,&id));
  const unsigned nvn[]={0x44,0x48,0x79,0x87,0x7d,0x8b},vk[]={137,138,157,158,165,166};
  for(unsigned k=0;k<6;k++) {
    CHECK(e[k].description.format==(int)nvn[k] && e[k].recipe.format==vk[k]);
    uint8_t marker[8]={'D','M','N',2,(uint8_t)(k+1),0,0,0};nvn_texdesc d=e[k].description;
    CHECK(nvn_native_layout_classify(marker,&d,NULL,NULL)==1);
    const nvn_native_entry *selected=nvn_native_layout_resolve(marker,&d);CHECK(selected && selected->recipe.format==vk[k]);
    nvn_texlayout storage;CHECK(nvn_native_layout_storage(marker,&d,&storage));CHECK(storage.size==16384 && storage.align==4096 && storage.bpb==16 && storage.layers==1);
    int bw=0,bh=0;CHECK(nvn_fmt_bpb(d.format,&bw,&bh)==16);CHECK(bw==(k>=4?6:4) && bh==(k>=4?6:4));
    d.samples=0;CHECK(nvn_native_layout_resolve(marker,&d)); // original default samples is1-compatible.
    d.samples=1;d.width++;CHECK(!nvn_native_layout_resolve(marker,&d));d=e[k].description;
    unsigned first=family[k].thumbnail_mip;d.width>>=first;d.height>>=first;d.levels-=first;nvn_texdesc source;uint8_t original[8];
    CHECK(nvn_native_layout_classify(marker,&d,&source,original)==2);CHECK(!nvn_native_layout_resolve(marker,&d));
    CHECK(source.format==(int)nvn[k] && source.pkg_block_h==1 && !memcmp(original,family[k].source_layout,8));
    nvn_tex_layout(&source,&storage);CHECK(storage.bpb==16 && storage.size>0 && storage.align>0);
    for(unsigned level=0;level<source.levels;level++)CHECK(nvn_tex_level_offset(&source,level)<storage.size);
    marker[3]=0xff;CHECK(!nvn_native_layout_resolve(marker,&e[k].description));
  }
  // Process-lifetime selection owns its bytes; caller reuse cannot relabel BC3.
  memset(t,0,bytes);free(t);uint8_t marker[8]={'D','M','N',2,1,0,0,0};nvn_texdesc d={.target=1,.format=0x44,.samples=1,.width=8,.height=8,.depth=1,.levels=4};
  CHECK(nvn_native_layout_resolve(marker,&d)->recipe.format==137);
  free(bad);printf("{\"test\":\"native-BC3-wire-metadata\",\"checks\":%u,\"GPU_executed\":false,\"game_modules_executed\":false,\"result\":\"passed\"}\n",checks);return 0;
}
