// Real NVN entrypoints and command recording, finite owned MAP_SHARED backing.
// No game modules. The reference is the verified original compact block input,
// not another host-native export/import operation or a guessed vendor layout.
#include "../../../gfx/nvn/nvn_native_layout.h"
#include "../../../gfx/tex/nvn_bcdec.h"
#include "mbedtls/sha256.h"
#include "dm_guest_heap.h"
#include <sys/mman.h>
#ifdef DM_ANDROID_APP
#include "../../guest_heap_startup.h"
// The host whole-AS fixture already supplies this adapter in its SVC stubs.
// Android links the real SVC, so use the same existing cold selector directly;
// this publishes no custom allocator or arbitrary-pointer import certificate.
int dm_guest_heap_startup_for_fixture(int requested) {
  return dm_guest_heap_startup_freeze(requested,vk_pool_shared_mode);
}
#endif
typedef struct {uint32_t width,height;uint64_t offset,size;uint8_t sha256[32];} nr_input_mip;
typedef struct {
  uint8_t magic[8];uint32_t version,header_bytes;uint64_t file_bytes;
  uint32_t width,height,levels,vk_format,block,game_format,nvn_format,reserved;
  uint8_t texture_sha256[32],descriptor_sha256[32],declaration_sha256[32];
  nr_input_mip mips[16];uint8_t digest[32];
} nr_input;
static int nr_expected_resident,nr_expected_lazy;
// Test-owned working bytes only. A constant BC3 image cannot be sensitized by
// swapping identical blocks. Reuse the existing decoder to choose a different
// alpha, then a valid constant-alpha block; source/pack bytes stay untouched.
static void nr_mutated_blocks(uint32_t format,const uint8_t original[32],uint8_t changed[32]) {
  memcpy(changed,original+16,16);memcpy(changed+16,original,16);
  if(!memcmp(changed,original,32)) {
    CHECK(format==0x44 || format==0x48);
    uint8_t rgba[4*4*4];CHECK(!nvn_bc_decode(format,original,16,4,4,rgba,16));
    changed[0]=changed[1]=rgba[3]^255;memset(changed+2,0,6);
  }
  CHECK(memcmp(changed,original,32));
}
_Static_assert(sizeof(nr_input)==1080,"original resource preparation ABI");
extern void vk_test_shared_image_backing(const void *,size_t);
static uint8_t *nr_read(const char *directory,const char *name,size_t *bytes) {
  char path[4096];CHECK(snprintf(path,sizeof path,"%s/%s",directory,name)<sizeof path);
  FILE *file=fopen(path,"rb");CHECK(file && !fseek(file,0,SEEK_END));
  long size=ftell(file);CHECK(size>0 && size<(16<<20));CHECK(!fseek(file,0,SEEK_SET));
  uint8_t *data=malloc((size_t)size);CHECK(data && fread(data,1,size,file)==(size_t)size);
  CHECK(!fclose(file));*bytes=size;return data;
}
static NVNtextureView nr_view(unsigned level) {
  NVNtextureView v;CALL(nvnTextureViewSetDefaults,&v);CALL(nvnTextureViewSetLevels,&v,level,1);
  CALL(nvnTextureViewSetLayers,&v,0,1);return v;
}
static void nr_blocks(NVNtexture *texture,const nr_input *input,const uint8_t *linear) {
  for(unsigned level=0;level<input->levels;level++) {
    const nr_input_mip *m=&input->mips[level];uint8_t *actual=malloc(m->size);CHECK(actual);
    NVNtextureView v=nr_view(level);NVNcopyRegion region={0,0,0,m->width,m->height,1};
    CALL(nvnTextureReadTexels,texture,&v,&region,actual);
    CHECK(!memcmp(actual,linear+m->offset,m->size));free(actual);
  }
}
static void nr_draw(NVNtexture *source,unsigned level,NVNtexture *output,uint8_t pixels[1024]) {
  NVNtextureView v=nr_view(level);mtl_reg_tex(71,(const tex_t *)source,(const view_t *)&v);
  S.targets=(p_set_rt){.n=1,.color={(const tex_t *)output}};vk_pipeline_state_changed();
  mtl_sub_begin();p_draw draw={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};
  vk_draw(&draw,NULL);vk_internal_wait(vk_internal_commit());
  NVNcopyRegion region={0,0,0,16,16,1};CALL(nvnTextureReadTexels,output,NULL,&region,pixels);
}
static void nr_samples(NVNtexture *native,NVNtexture *reference,NVNtexture *output,unsigned levels) {
  extern int vk_test_native_image_owner(uint64_t);
  uint8_t expected[1024],actual[1024];
  for(unsigned level=0;level<levels;level++) {
    nr_draw(reference,level,output,expected);nr_draw(native,level,output,actual);
    CHECK(!memcmp(expected,actual,sizeof actual));
    if(nr_expected_resident)CHECK(vk_test_native_image_owner(((const tex_t *)native)->serial)==2);
  }
}
static void nr_tail_compare(NVNtexture *tail,unsigned first,NVNtexture *native,NVNtexture *output,
                            const nr_input *input,const uint8_t *linear) {
  uint8_t expected[1024],actual[1024];
  for(unsigned level=first;level<input->levels;level++) {
    const nr_input_mip *m=&input->mips[level];uint8_t *blocks=malloc(m->size);CHECK(blocks);
    NVNtextureView v=nr_view(level-first);NVNcopyRegion region={0,0,0,m->width,m->height,1};
    CALL(nvnTextureReadTexels,tail,&v,&region,blocks);CHECK(!memcmp(blocks,linear+m->offset,m->size));free(blocks);
    nr_draw(native,level,output,expected);nr_draw(tail,level-first,output,actual);CHECK(!memcmp(actual,expected,sizeof actual));
  }
}
static int native_resource_gpu_test(const char *directory) {
  const char *resident=getenv("DM_NATIVE_RESOURCE_EXPECT_RESIDENT");
  CHECK(!resident || !strcmp(resident,"1"));nr_expected_resident=resident!=NULL;
  const char *lazy=getenv("DM_NATIVE_RESOURCE_EXPECT_LAZY");
  CHECK(!lazy || !strcmp(lazy,"1"));nr_expected_lazy=lazy!=NULL;
  enum {pool_bytes=4<<20,native_offset=(1<<20)+8192,reference_offset=2<<20,
    linear_offset=3<<20,vertex_offset=(3<<20)+(256<<10),output_offset=(3<<20)+(320<<10),
    alias_offset=(3<<20)+(384<<10)};
  CHECK(vk_pool_shared_mode());
  size_t table_size,payload_size,linear_size;
  // The table was already authenticated and installed by real driver startup.
  const char *table_path=getenv("DM_VULKAN_NATIVE_RESOURCE_TABLE");CHECK(table_path);
  FILE *table_file=fopen(table_path,"rb");CHECK(table_file);
  nvn_native_table header;nvn_native_entry entry;
  CHECK(fread(&header,1,sizeof header,table_file)==sizeof header && header.count==1);
  CHECK(fread(&entry,1,sizeof entry,table_file)==sizeof entry);
  // Gate the actual selected compressed format, not an unrelated ASTC feature.
  // This fixture still compares original linear blocks and ordinary VkImages;
  // accepting BC3 here grants no native-resident/coherent driver shortcut.
  int bc3=entry.recipe.format==VK_FORMAT_BC3_UNORM_BLOCK || entry.recipe.format==VK_FORMAT_BC3_SRGB_BLOCK;
  int astc=entry.recipe.format==VK_FORMAT_ASTC_4x4_UNORM_BLOCK || entry.recipe.format==VK_FORMAT_ASTC_4x4_SRGB_BLOCK ||
    entry.recipe.format==VK_FORMAT_ASTC_6x6_UNORM_BLOCK || entry.recipe.format==VK_FORMAT_ASTC_6x6_SRGB_BLOCK;
  CHECK((bc3 && V.features.textureCompressionBC) || (astc && V.features.textureCompressionASTC_LDR));
  VkFormatProperties supported;vkGetPhysicalDeviceFormatProperties(V.physical,entry.recipe.format,&supported);
  VkFormatFeatureFlags required=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
  CHECK((supported.optimalTilingFeatures&required)==required);
  nvn_native_family family={0};
  if(header.version==2)CHECK(fread(&family,1,sizeof family,table_file)==sizeof family);
  CHECK(fgetc(table_file)==EOF && !fclose(table_file));
  table_size=sizeof header+sizeof entry+(header.version==2?sizeof family:0);CHECK(header.file_bytes==table_size);
  uint8_t marker[8]={'D','M','N',0,1,0,0,0};marker[3]=header.version;
  const nvn_native_entry *selected=nvn_native_layout_resolve(marker,&entry.description);
  CHECK(selected && !memcmp(selected,&entry,sizeof entry));
  uint8_t *payload=nr_read(directory,"native-resource.bin",&payload_size);
  uint8_t *linear=nr_read(directory,"resource-input.bin",&linear_size),sum[32];
  CHECK(payload_size==entry.recipe.image_size && payload_size<(512<<10));
  mbedtls_sha256(payload,payload_size,sum,0);CHECK(!memcmp(sum,entry.recipe.payload_sha256,32));
  mbedtls_sha256(linear,linear_size,sum,0);CHECK(!memcmp(sum,entry.recipe.input_sha256,32));
  CHECK(linear_size>=sizeof(nr_input) && linear_size<(256<<10));nr_input *input=(void *)linear;
  CHECK(!memcmp(input->magic,"DMRSRC1",8) && input->version==1 && input->header_bytes==sizeof *input &&
    input->file_bytes==linear_size && input->width==entry.recipe.width && input->height==entry.recipe.height &&
    input->levels==entry.recipe.levels && input->levels==9 && input->width==256 && input->height==256 &&
    input->nvn_format==entry.description.format && input->vk_format==entry.recipe.format);
  for(unsigned n=0;n<input->levels;n++) {
    const nr_input_mip *m=&input->mips[n];CHECK(m->offset<=linear_size && m->size<=linear_size-m->offset);
    CHECK(m->width==(input->width>>n) && m->height==(input->height>>n));
    CHECK(m->size==(uint64_t)((m->width+input->block-1)/input->block)*((m->height+input->block-1)/input->block)*16);
  }
  int window_fixture=V.options.pool_window_bytes!=0;
  int lazy_active;uint64_t lazy_unique,lazy_mapped,lazy_nodes,lazy_covers;
  vk_pool_lazy_window_stats(&lazy_active,&lazy_unique,&lazy_mapped,&lazy_nodes,&lazy_covers);
  if(nr_expected_lazy)CHECK(window_fixture && lazy_active);
  int real_heap_fixture=getenv("DM_SHARED_GUEST_HEAP_FIXTURE")!=NULL;
  uint8_t *raw=NULL,*storage=NULL;
  if(window_fixture || real_heap_fixture) {
    CHECK((!window_fixture || V.options.pool_window_bytes==65536) &&
      dm_guest_shared_heap_contains((uintptr_t)bytes,pool_bytes));
    storage=bytes;dm_guest_write_prepare(storage,pool_bytes);
  } else {
    raw=mmap(NULL,pool_bytes+65536,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);CHECK(raw!=MAP_FAILED);
    storage=(void *)(((uintptr_t)raw+65535)&~(uintptr_t)65535);
    vk_test_shared_image_backing(storage,pool_bytes);
  }
  memset(storage,0xa5,pool_bytes);memcpy(storage+native_offset,payload,payload_size);
  memcpy(storage+linear_offset,linear,linear_size);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;CALL(nvnMemoryPoolBuilderSetDefaults,&mb);
  CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,pool_bytes);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));CHECK(!nvn_dirty_tracked(storage+native_offset,payload_size));
  NVNtexture native,reference,output;
  for(unsigned n=0;n<3;n++) {
    NVNtextureBuilder tb;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
    CALL(nvnTextureBuilderSetWidth,&tb,n==2?16:input->width);CALL(nvnTextureBuilderSetHeight,&tb,n==2?16:input->height);
    CALL(nvnTextureBuilderSetLevels,&tb,n==2?1:input->levels);CALL(nvnTextureBuilderSetFormat,&tb,n==2?NVN_FORMAT_RGBA8:input->nvn_format);
    if(!n) {NVNpackagedTextureLayout layout;memcpy(layout.layout,marker,8);CALL(nvnTextureBuilderSetPackagedTextureLayout,&tb,&layout);
      CHECK(CALL(nvnTextureBuilderGetStorageAlignment,&tb)==entry.recipe.image_alignment);
      CHECK(CALL(nvnTextureBuilderGetStorageSize,&tb)==entry.recipe.image_size);}
    CALL(nvnTextureBuilderSetStorage,&tb,&pool,n==0?native_offset:n==1?reference_offset:output_offset);
    CHECK(CALL(nvnTextureInitialize,n==0?&native:n==1?&reference:&output,&tb));
  }
  CHECK(CALL(nvnCommandBufferInitialize,&cmd,&device));CALL(nvnCommandBufferAddCommandMemory,&cmd,&pool,0,1<<20);
  CALL(nvnMemoryPoolFlushMappedRange,&pool,native_offset,payload_size);
  CALL(nvnMemoryPoolFlushMappedRange,&pool,linear_offset,linear_size);
  // Standard linear compressed upload into an ordinary reference image.
  CALL(nvnCommandBufferBeginRecording,&cmd);
  for(unsigned level=0;level<input->levels;level++) {
    NVNtextureView view=nr_view(level);const nr_input_mip *m=&input->mips[level];NVNcopyRegion region={0,0,0,m->width,m->height,1};
    CALL(nvnCommandBufferCopyBufferToTexture,&cmd,(uintptr_t)storage+linear_offset+m->offset,&reference,&view,&region,0);
  }
  submit(end());nr_blocks(&native,input,linear);nr_blocks(&reference,input,linear);
  extern int vk_test_fixed_shared_pool(uint64_t,int *,int *,VkMemoryPropertyFlags *);
  int imported=0,shadow=1;VkMemoryPropertyFlags properties=0;
  CHECK(vk_test_fixed_shared_pool((uintptr_t)storage,&imported,&shadow,&properties));
  CHECK(imported && !shadow && (properties&VK_MEMORY_PROPERTY_HOST_CACHED_BIT));
  CHECK((properties&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) || vk_pool_shared_gpu_gate_mode());
  CHECK(!nvn_dirty_tracked(storage+native_offset,payload_size)); // sample/read-only has no scopes
  uint8_t vertex[0x880],fragment[0x880];memset(vertex,3,sizeof vertex);memset(fragment,4,sizeof fragment);
  NVNprogram program;NVNshaderData shaders[]={{.control=vertex},{.control=fragment}};
  CHECK(CALL(nvnProgramInitialize,&program,&device));CHECK(CALL(nvnProgramSetShaders,&program,2,shaders));
  NVNsamplerBuilder sb;NVNsampler sampler;CALL(nvnSamplerBuilderSetDefaults,&sb);CALL(nvnSamplerBuilderSetDevice,&sb,&device);
  CALL(nvnSamplerBuilderSetMinMagFilter,&sb,NVN_MIN_FILTER_NEAREST,NVN_MAG_FILTER_NEAREST);
  CALL(nvnSamplerBuilderSetWrapMode,&sb,NVN_WRAP_MODE_CLAMP_TO_EDGE,NVN_WRAP_MODE_CLAMP_TO_EDGE,NVN_WRAP_MODE_CLAMP_TO_EDGE);
  CHECK(CALL(nvnSamplerInitialize,&sampler,&sb));mtl_set_tex_pool_n(256);mtl_reg_samp(72,&((const samp_t *)&sampler)->p);
  const float vertices[]={-1,-1,0,0,3,-1,2,0,-1,3,0,2};memcpy(storage+vertex_offset,vertices,sizeof vertices);
  CALL(nvnMemoryPoolFlushMappedRange,&pool,vertex_offset,sizeof vertices);
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);NVNdepthStencilState ds;CALL(nvnDepthStencilStateSetDefaults,&ds);
  CALL(nvnDepthStencilStateSetDepthTestEnable,&ds,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&ds,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&ds,0);
  S=(vk_state){.program=(const prog_t *)&program,.depth_range={0,1},.chmask={~0u}};
  S.poly=*(const poly_st *)&polygon;S.ds=*(const ds_st *)&ds;S.color=*(const color_st *)&color;
  S.tex[1][0]=71;S.samp[1][0]=(uint64_t)72<<20;S.has_viewport=S.has_scissor=1;
  S.viewport[0][2]=S.viewport[0][3]=16;S.scissor=(p_scissor){0,0,16,16};
  for(unsigned n=0;n<2;n++) {NVNvertexAttribState attr;CALL(nvnVertexAttribStateSetDefaults,&attr);
    CALL(nvnVertexAttribStateSetFormat,&attr,NVN_FORMAT_RG32F,n*8);S.attr[n]=*(const va_st *)&attr;}
  NVNvertexStreamState stream;CALL(nvnVertexStreamStateSetDefaults,&stream);CALL(nvnVertexStreamStateSetStride,&stream,16);
  S.stream[0]=*(const vs_st *)&stream;S.vertex[0]=(NVNbufferRange){(uintptr_t)storage+vertex_offset,sizeof vertices};
  nr_samples(&native,&reference,&output,input->levels);
  // Destructive union is a separate backing policy. Lazy immutable aliases
  // retain the same native allocation; the alias writer/readback, CPU Flush,
  // B2T/T2T and release checks below still execute through that real policy.
  if(window_fixture && !lazy_active) {
    extern void vk_test_texture_native_binding(uint64_t,VkDeviceMemory *,uint64_t *);
    VkDeviceMemory original,current;uint64_t original_offset,current_offset;
    uint64_t serial=((const tex_t *)&native)->serial;
    vk_test_texture_native_binding(serial,&original,&original_offset);
    vk_pool_span_info before,after;
    CHECK(vk_pool_span_query((uintptr_t)storage+native_offset,payload_size,&before)==VK_POOL_SPAN_SINGLE);
    for(unsigned pass=0;pass<2;pass++) {
      mtl_sub_begin();vk_pool_requirement cross={(uintptr_t)storage+native_offset-(pass?98304:32768),
        payload_size+(pass?196608:65536),0,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0};
      int operation=vk_pool_operation_begin_ranges(&cross,1);
      CHECK(vk_pool_span_query((uintptr_t)storage+native_offset,payload_size,&after)==VK_POOL_SPAN_SINGLE);
      CHECK(after.backing_generation>before.backing_generation);before=after;
      vk_test_texture_native_binding(serial,&current,&current_offset);
      CHECK(current==original && current_offset==original_offset);
      vk_pool_operation_end(operation);vk_internal_wait(vk_internal_commit());
      nr_blocks(&native,input,linear);nr_samples(&native,&reference,&output,input->levels);
    }
  }
  CHECK(!nvn_dirty_tracked(storage+native_offset,payload_size));
  NVNtexture tail;int has_tail=header.version==2 && family.thumbnail_mip;
  if(has_tail) {
    unsigned first=family.thumbnail_mip;CHECK(first<input->levels);
    NVNtextureBuilder tb;CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
    CALL(nvnTextureBuilderSetWidth,&tb,input->mips[first].width);CALL(nvnTextureBuilderSetHeight,&tb,input->mips[first].height);
    CALL(nvnTextureBuilderSetLevels,&tb,input->levels-first);CALL(nvnTextureBuilderSetFormat,&tb,input->nvn_format);
    NVNpackagedTextureLayout layout;memcpy(layout.layout,marker,8);CALL(nvnTextureBuilderSetPackagedTextureLayout,&tb,&layout);
    CALL(nvnTextureBuilderSetStorage,&tb,&pool,reference_offset+(256<<10));CHECK(CALL(nvnTextureInitialize,&tail,&tb));
    tex_t *source=(void *)&tail;CHECK(!nvn_native_layout_is_marker(source->layout));
    CHECK(!memcmp(source->layout,family.source_layout,8) && source->L.size<(64<<10));
    dm_guest_write_prepare(source->mem,source->L.size);memset(source->mem,0,source->L.size);
    // First exercise the original packaged CPU encoding, using its existing
    // NVN swizzler rather than treating family metadata as a native payload.
    for(unsigned level=first;level<input->levels;level++) {
      const nr_input_mip *m=&input->mips[level];size_t pitch=((m->width+input->block-1)/input->block)*16;
      CHECK(!nvn_swizzle(&source->d,level-first,0,linear+m->offset,pitch,source->mem));
    }
    CALL(nvnMemoryPoolFlushMappedRange,&pool,source->off,source->L.size);
    nr_tail_compare(&tail,first,&native,&output,input,linear);
  }
  // First native GPU writer is deliberately a linear alias, before any image
  // B2T writer can establish scopes. Copy this exact mip's opaque producer
  // bytes, never infer a vendor block permutation. Scope checks catch a lost
  // writer hook even though the finite transfer preserves these pixel values.
  const nvn_native_mip *alias_mip=&entry.recipe.mips[2];CHECK(alias_mip->size<(128<<10));
  memcpy(storage+alias_offset,payload+alias_mip->offset,alias_mip->size);
  CALL(nvnMemoryPoolFlushMappedRange,&pool,alias_offset,alias_mip->size);
  CALL(nvnCommandBufferBeginRecording,&cmd);CALL(nvnCommandBufferCopyBufferToBuffer,&cmd,
    (uintptr_t)storage+alias_offset,(uintptr_t)storage+native_offset+alias_mip->offset,alias_mip->size,0);
  submit(end());CHECK(nvn_dirty_tracked(storage+native_offset+alias_mip->offset,alias_mip->size));
  nr_blocks(&native,input,linear);nr_samples(&native,&reference,&output,input->levels);
  // Thumbnail-shaped publication: swap valid compressed blocks in mip4 and upload
  // only levels4..8. This proves this specific full-Builder/B2T contract, not
  // the still-unobserved game's choice of a separate reduced Builder.
  const nr_input_mip *top=&input->mips[4];CHECK(top->size>=32);
  uint8_t before[1024],after[1024];nr_draw(&reference,4,&output,before);
  uint8_t original_blocks[32],changed_blocks[32];memcpy(original_blocks,linear+top->offset,32);
  nr_mutated_blocks(input->nvn_format,original_blocks,changed_blocks);
  memcpy(linear+top->offset,changed_blocks,32);
  memcpy(storage+linear_offset,linear,linear_size);CALL(nvnMemoryPoolFlushMappedRange,&pool,linear_offset,linear_size);
  CALL(nvnCommandBufferBeginRecording,&cmd);
  for(unsigned level=4;level<input->levels;level++) {
    const nr_input_mip *m=&input->mips[level];NVNtextureView v=nr_view(level);NVNcopyRegion region={0,0,0,m->width,m->height,1};
    CALL(nvnCommandBufferCopyBufferToTexture,&cmd,(uintptr_t)storage+linear_offset+m->offset,&native,&v,&region,0);
    CALL(nvnCommandBufferCopyBufferToTexture,&cmd,(uintptr_t)storage+linear_offset+m->offset,&reference,&v,&region,0);
  }
  submit(end());nr_blocks(&native,input,linear);nr_samples(&native,&reference,&output,input->levels);
  if(has_tail) {
    // The real descriptor's compact thumbnail blocks were independently
    // checked equal to this verified input tail. Preserve linear B2T semantics.
    unsigned first=family.thumbnail_mip;CALL(nvnCommandBufferBeginRecording,&cmd);
    for(unsigned level=first;level<input->levels;level++) {
      const nr_input_mip *m=&input->mips[level];NVNtextureView v=nr_view(level-first);NVNcopyRegion region={0,0,0,m->width,m->height,1};
      CALL(nvnCommandBufferCopyBufferToTexture,&cmd,(uintptr_t)storage+linear_offset+m->offset,&tail,&v,&region,0);
    }
    submit(end());nr_tail_compare(&tail,first,&native,&output,input,linear);
  }
  CHECK(nvn_dirty_tracked(storage+native_offset+entry.recipe.mips[4].offset,entry.recipe.mips[4].size));
  CHECK(!nvn_dirty_tracked(storage+native_offset,4096)); // mip0 stayed sample-only
  nr_draw(&reference,4,&output,after);CHECK(memcmp(before,after,sizeof after));
  // Legal CPU native production after QueueFinish: exact raw opaque bytes,
  // then explicit NVN Flush. No runtime scan or second permanent image copy.
  dm_guest_write_prepare(storage+native_offset,payload_size);
  memcpy(storage+native_offset,payload,payload_size);
  CALL(nvnMemoryPoolFlushMappedRange,&pool,native_offset,payload_size);
  memcpy(linear+top->offset,original_blocks,32);
  nr_blocks(&native,input,linear);
  // Native -> ordinary T2T restores the altered reference thumbnail mips.
  CALL(nvnCommandBufferBeginRecording,&cmd);
  for(unsigned level=4;level<input->levels;level++) {
    const nr_input_mip *m=&input->mips[level];NVNtextureView v=nr_view(level);NVNcopyRegion region={0,0,0,m->width,m->height,1};
    CALL(nvnCommandBufferCopyTextureToTexture,&cmd,&native,&v,&region,&reference,&v,&region,0);
  }
  submit(end());nr_blocks(&reference,input,linear);nr_samples(&native,&reference,&output,input->levels);
  // Standard B2T -> native T2T in one recorded stream exercises the actual
  // cross-image transfer dependency rather than only waiting between copies.
  memcpy(linear+top->offset,changed_blocks,32);
  memcpy(storage+linear_offset,linear,linear_size);CALL(nvnMemoryPoolFlushMappedRange,&pool,linear_offset,linear_size);
  NVNtextureView thumbnail=nr_view(4);NVNcopyRegion thumbnail_region={0,0,0,top->width,top->height,1};
  CALL(nvnCommandBufferBeginRecording,&cmd);
  CALL(nvnCommandBufferCopyBufferToTexture,&cmd,(uintptr_t)storage+linear_offset+top->offset,&reference,&thumbnail,&thumbnail_region,0);
  CALL(nvnCommandBufferCopyTextureToTexture,&cmd,&reference,&thumbnail,&thumbnail_region,&native,&thumbnail,&thumbnail_region,0);
  submit(end());nr_blocks(&native,input,linear);nr_samples(&native,&reference,&output,input->levels);
  // Read original canonical native bytes through a linear NVN alias.
  CALL(nvnCommandBufferBeginRecording,&cmd);CALL(nvnCommandBufferCopyBufferToBuffer,&cmd,
    (uintptr_t)storage+native_offset,(uintptr_t)storage+alias_offset,4096,0);submit(end());
  CHECK(!memcmp(storage+alias_offset,storage+native_offset,4096));
  CHECK(!memcmp(storage+native_offset-64,(uint8_t[64]){[0 ... 63]=0xa5},64));
  CHECK(!memcmp(storage+native_offset+payload_size,(uint8_t[64]){[0 ... 63]=0xa5},64));
  uint64_t native_import_count,native_import_bytes,native_metadata_bytes;
  vk_native_allocation_stats(&native_import_count,&native_import_bytes,&native_metadata_bytes);
  CHECK(native_import_count==1 && native_import_bytes>=payload_size && native_import_bytes<=payload_size+65536);
  S=(vk_state){0};vk_pipeline_state_changed();CALL(nvnSamplerFinalize,&sampler);CALL(nvnProgramFinalize,&program);
  CALL(nvnTextureFinalize,&native);CALL(nvnTextureFinalize,&reference);CALL(nvnTextureFinalize,&output);
  if(has_tail)CALL(nvnTextureFinalize,&tail);
  CALL(nvnCommandBufferFinalize,&cmd);CALL(nvnMemoryPoolFinalize,&pool);
  CHECK(!nvn_dirty_tracked(storage+native_offset,payload_size));if(raw)vk_test_shared_image_backing(NULL,0);
  storage[native_offset]=0x42;CHECK(storage[native_offset]==0x42);if(raw)CHECK(!munmap(raw,pool_bytes+65536));
  uint64_t count_after,bytes_after,metadata_after;vk_native_allocation_stats(&count_after,&bytes_after,&metadata_after);
  CHECK(!count_after && !bytes_after && !metadata_after);
  printf("{\"native_allocation_lane\":true,\"imports\":%llu,\"import_bytes\":%llu,\"metadata_bytes\":%llu,\"same_physical_pages_as_Buffer_imports\":true,\"no_payload_copy\":true,\"window_two_unions\":%s,\"last_release_collected\":true}\n",
    (unsigned long long)native_import_count,(unsigned long long)native_import_bytes,(unsigned long long)native_metadata_bytes,window_fixture && !lazy_active?"true":"false");
  free(payload);free(linear);CHECK(vk_validation_error_count()==0);
  printf("{\"native_resource_owner_check\":true,\"resident_expected\":%s,\"all_sampled_mips_checked\":%s}\n",
    nr_expected_resident?"true":"false",nr_expected_resident?"true":"false");
  printf("{\"native_resource_lazy_check\":true,\"expected\":%s,\"active\":%s,\"window_bytes\":%llu}\n",
    nr_expected_lazy?"true":"false",lazy_active?"true":"false",(unsigned long long)V.options.pool_window_bytes);
  printf("{\"test\":\"native-resource-original-NVN\",\"checks\":%u,\"format\":%u,\"mips\":%u,\"family_version\":%u,\"tail_levels\":%u,\"memory_flags\":%u,\"atom\":%llu,\"native_offset\":%u,\"pool_bytes\":%u,\"no_pool_shadow\":true,\"full_all_mips\":true,\"tail_packaged_and_B2T\":%s,\"shared_gpu_gate\":%s,\"default_enabled\":false,\"game_modules_executed\":false,\"result\":\"passed\"}\n",
    checks,entry.recipe.format,entry.recipe.levels,header.version,has_tail?entry.recipe.levels-family.thumbnail_mip:0,
    properties,(unsigned long long)V.props.limits.nonCoherentAtomSize,native_offset,pool_bytes,
    has_tail?"true":"false",vk_pool_shared_gpu_gate_mode()?"true":"false");return 0;
}
