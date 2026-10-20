// Focused original022 NVN functional subset from an official GFXR conversion.
// No game, capture/replay engine, driver modification, measurement or sharing.
#include "../../../gfx/tex/nvn_bcdec.h"
typedef struct {
  uint32_t image,format,base_width,base_height,levels,mip,width,height;
  uint32_t offset,size,x,y,source_buffer,capture_index,reserved[2];
} panvk_bc3_row;
_Static_assert(sizeof(panvk_bc3_row)==64,"fixed little-endian fixture row");
static void panvk_bc3_focused_test(const char *path) {
  CHECK(!mtl_pool_shared_mode());
  const char *negative=getenv("DM_PANVK_BC3_CAPTURE_NEGATIVE");
  CHECK(!negative || !strcmp(negative,"wrong-texture"));
  FILE *f=fopen(path,"rb");CHECK(f);uint32_t h[8];
  CHECK(fread(h,1,sizeof h,f)==sizeof h);
  CHECK(h[0]==0x33434250 && h[1]==1 && h[2]==52 && h[3]==7 &&
        h[4]==1024 && h[5]==640 && h[6]==371920 && h[7]==16);
  panvk_bc3_row rows[52];CHECK(fread(rows,1,sizeof rows,f)==sizeof rows);
  uint8_t *payload=malloc(h[6]);CHECK(payload);
  CHECK(fread(payload,1,h[6],f)==h[6] && fgetc(f)==EOF && !ferror(f));CHECK(!fclose(f));
  uint32_t image_seen=0,seen_mips[7]={0},payload_end=0;
  for(unsigned n=0;n<52;n++) {
    panvk_bc3_row *r=&rows[n];
    CHECK(r->image<7 && (r->format==NVN_FORMAT_RGBA_DXT5 || r->format==NVN_FORMAT_RGBA_DXT5_SRGB));
    CHECK((r->base_width==4 || r->base_width==16 || r->base_width==128 || r->base_width==256) && r->base_width==r->base_height && r->levels<=9 && r->mip<r->levels);
    CHECK(r->width==((r->base_width>>r->mip)?(r->base_width>>r->mip):1) && r->height==((r->base_height>>r->mip)?(r->base_height>>r->mip):1));
    CHECK(r->size==((r->width+3)/4)*((r->height+3)/4)*16 && r->offset==payload_end && r->size<=h[6]-r->offset);
    CHECK(r->x+r->width<=h[4] && r->y+r->height<=h[5] && !r->reserved[0] && !r->reserved[1]);
    CHECK(!(seen_mips[r->image]&(1u<<r->mip)));seen_mips[r->image]|=1u<<r->mip;
    image_seen|=1u<<r->image;payload_end+=r->size;
    for(unsigned p=0;p<n;p++) {
      panvk_bc3_row *a=&rows[p];
      CHECK(r->x+r->width<=a->x || a->x+a->width<=r->x || r->y+r->height<=a->y || a->y+a->height<=r->y);
      if(a->image==r->image)CHECK(a->format==r->format && a->base_width==r->base_width && a->base_height==r->base_height && a->levels==r->levels);
    }
  }
  CHECK(image_seen==127 && payload_end==h[6]);
  // A separate bounded pool leaves the original fixture's 4MiB setup alone.
  enum { pool_size=8u<<20,upload_at=1u<<20,raw_at=(1u<<20)+(1u<<19),
         texture_at=2u<<20,texture_stride=1u<<17,output_at=3u<<20,vertex_at=7u<<20 };
  CHECK(h[6]<(1u<<19));uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,pool_size));memset(storage,0,pool_size);
  memcpy(storage+upload_at,payload,h[6]);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,pool_size);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  CHECK(CALL(nvnCommandBufferInitialize,&cmd,&device));CALL(nvnCommandBufferAddCommandMemory,&cmd,&pool,0,1u<<20);
  NVNtexture textures[7],output;
  for(unsigned i=0;i<8;i++) {
    const panvk_bc3_row *r=NULL;
    if(i<7) {for(unsigned n=0;n<52;n++)if(rows[n].image==i){r=&rows[n];break;}CHECK(r && seen_mips[i]==((1u<<r->levels)-1));}
    NVNtextureBuilder b;CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);
    CALL(nvnTextureBuilderSetTarget,&b,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&b,r?r->base_width:h[4]);
    CALL(nvnTextureBuilderSetHeight,&b,r?r->base_height:h[5]);CALL(nvnTextureBuilderSetLevels,&b,r?r->levels:1);
    CALL(nvnTextureBuilderSetFormat,&b,r?r->format:NVN_FORMAT_RGBA8);
    CHECK(CALL(nvnTextureBuilderGetStorageSize,&b)<=(r?texture_stride:4u<<20));
    CALL(nvnTextureBuilderSetStorage,&b,&pool,r?texture_at+i*texture_stride:output_at);
    CHECK(CALL(nvnTextureInitialize,r?&textures[i]:&output,&b));
  }
  NVNtexturePool tp;NVNsamplerPool sp;CHECK(CALL(nvnTexturePoolInitialize,&tp,&pool,vertex_at+65536,64));
  CHECK(CALL(nvnSamplerPoolInitialize,&sp,&pool,vertex_at+69632,64));
  NVNsamplerBuilder sb;NVNsampler sampler;CALL(nvnSamplerBuilderSetDefaults,&sb);CALL(nvnSamplerBuilderSetDevice,&sb,&device);
  CALL(nvnSamplerBuilderSetMinMagFilter,&sb,NVN_MIN_FILTER_NEAREST,NVN_MAG_FILTER_NEAREST);
  CALL(nvnSamplerBuilderSetWrapMode,&sb,NVN_WRAP_MODE_CLAMP_TO_EDGE,NVN_WRAP_MODE_CLAMP_TO_EDGE,NVN_WRAP_MODE_CLAMP_TO_EDGE);
  CHECK(CALL(nvnSamplerInitialize,&sampler,&sb));CALL(nvnSamplerPoolRegisterSampler,&sp,1,&sampler);
  NVNtextureView views[52];NVNseparateTextureHandle handles[52];
  for(unsigned n=0;n<52;n++) {
    CALL(nvnTextureViewSetDefaults,&views[n]);CALL(nvnTextureViewSetLevels,&views[n],rows[n].mip,1);
    CALL(nvnTexturePoolRegisterTexture,&tp,n+1,&textures[rows[n].image],&views[n]);
    handles[n]=CALL(nvnDeviceGetSeparateTextureHandle,&device,n+1);
  }
  NVNseparateSamplerHandle sh=CALL(nvnDeviceGetSeparateSamplerHandle,&device,1);
  uint8_t vertex_control[0x880],fragment_control[0x880];memset(vertex_control,3,sizeof vertex_control);memset(fragment_control,4,sizeof fragment_control);
  NVNshaderData shaders[2]={{.control=vertex_control},{.control=fragment_control}};NVNprogram graphics;
  CHECK(CALL(nvnProgramInitialize,&graphics,&device));CHECK(CALL(nvnProgramSetShaders,&graphics,2,shaders));
  const float vertices[12]={-1,1,0,0,3,1,2,0,-1,-3,0,2};memcpy(storage+vertex_at,vertices,sizeof vertices);
  NVNvertexAttribState attrs[2];for(unsigned n=0;n<2;n++){CALL(nvnVertexAttribStateSetDefaults,&attrs[n]);CALL(nvnVertexAttribStateSetFormat,&attrs[n],NVN_FORMAT_RG32F,n*8);}
  NVNvertexStreamState stream;CALL(nvnVertexStreamStateSetDefaults,&stream);CALL(nvnVertexStreamStateSetStride,&stream,16);
  NVNbufferRange vr={(uintptr_t)storage+vertex_at,sizeof vertices};
  NVNcolorState color;NVNpolygonState polygon;NVNdepthStencilState depth;
  CALL(nvnColorStateSetDefaults,&color);CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  CALL(nvnDepthStencilStateSetDefaults,&depth);CALL(nvnDepthStencilStateSetDepthTestEnable,&depth,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&depth,0);
  CALL(nvnCommandBufferBeginRecording,&cmd);
  CALL(nvnCommandBufferSetTexturePool,&cmd,&tp);CALL(nvnCommandBufferSetSamplerPool,&cmd,&sp);
  // All52 captured full-mip uploads remain in one real command recording.
  for(unsigned n=0;n<52;n++) {
    panvk_bc3_row *r=&rows[n];NVNcopyRegion region={0,0,0,r->width,r->height,1};
    CALL(nvnCommandBufferSetCopyRowStride,&cmd,0);
    CALL(nvnCommandBufferCopyBufferToTexture,&cmd,(uintptr_t)storage+upload_at+r->offset,&textures[r->image],&views[n],&region,0);
  }
  CALL(nvnCommandBufferBarrier,&cmd,NVN_BARRIER_ORDER_PRIMITIVES_BIT|NVN_BARRIER_INVALIDATE_TEXTURE_BIT);
  const NVNtexture *targets[]={&output};CALL(nvnCommandBufferSetRenderTargets,&cmd,1,targets,NULL,NULL,NULL);
  CALL(nvnCommandBufferBindProgram,&cmd,&graphics,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
  CALL(nvnCommandBufferBindColorState,&cmd,&color);CALL(nvnCommandBufferBindPolygonState,&cmd,&polygon);CALL(nvnCommandBufferBindDepthStencilState,&cmd,&depth);
  CALL(nvnCommandBufferBindVertexAttribState,&cmd,2,attrs);CALL(nvnCommandBufferBindVertexStreamState,&cmd,1,&stream);CALL(nvnCommandBufferBindVertexBuffers,&cmd,0,1,&vr);
  CALL(nvnCommandBufferBindSeparateSamplers,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,&sh);
  for(unsigned n=0;n<52;n++) {
    panvk_bc3_row *r=&rows[n];float viewport[]={r->x,r->y,r->width,r->height};
    CALL(nvnCommandBufferSetViewports,&cmd,0,1,viewport);CALL(nvnCommandBufferSetScissor,&cmd,r->x,r->y,r->width,r->height);
    const NVNseparateTextureHandle *selected=negative?&handles[0]:&handles[n];
    CALL(nvnCommandBufferBindSeparateTextures,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,1,selected);
    CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
  }
  submit(end());
  extern int vk_texture_test_bc_decoded(const tex_t *);
  for(unsigned n=0;n<7;n++)CHECK(!vk_texture_test_bc_decoded((const tex_t *)&textures[n]));
  size_t atlas_bytes=(size_t)h[4]*h[5]*4;uint8_t *atlas=malloc(atlas_bytes),*reference=malloc(256*256*4);CHECK(atlas && reference);
  NVNcopyRegion all={0,0,0,h[4],h[5],1};CALL(nvnTextureReadTexels,&output,NULL,&all,atlas);
  unsigned sampled_pixels=0;
  for(unsigned n=0;n<52;n++) {
    panvk_bc3_row *r=&rows[n];CHECK(!nvn_bc_decode(r->format,payload+r->offset,((r->width+3)/4)*16,r->width,r->height,reference,r->width*4));
    for(unsigned y=0;y<r->height;y++)for(unsigned x=0;x<r->width;x++)for(unsigned k=0;k<4;k++) {
      unsigned want=reference[4*(y*r->width+x)+k];
      if(r->format==NVN_FORMAT_RGBA_DXT5_SRGB && k<3){double v=want/255.;want=lround(255*(v<=.04045?v/12.92:pow((v+.055)/1.055,2.4)));}
      unsigned got=atlas[4*((r->y+y)*h[4]+r->x+x)+k];
      if(abs((int)got-(int)want)>2)LOG("[BC3 focused mismatch] row=%u mip=%u xy=%u,%u channel=%u got=%u expected=%u",n,r->mip,x,y,k,got,want);
      CHECK(abs((int)got-(int)want)<=2);
    }
    sampled_pixels+=r->width*r->height;
  }
  // Separate real GPU T2B batch validates all raw BC bytes without recompression.
  CALL(nvnCommandBufferBeginRecording,&cmd);
  for(unsigned n=0;n<52;n++) {
    panvk_bc3_row *r=&rows[n];NVNcopyRegion region={0,0,0,r->width,r->height,1};
    CALL(nvnCommandBufferSetCopyRowStride,&cmd,0);
    CALL(nvnCommandBufferCopyTextureToBuffer,&cmd,&textures[r->image],&views[n],&region,(uintptr_t)storage+raw_at+r->offset,0);
  }
  submit(end());CHECK(!memcmp(storage+raw_at,payload,h[6]));
  for(unsigned n=0;n<7;n++)CALL(nvnTextureFinalize,&textures[n]);
  CALL(nvnTextureFinalize,&output);
  CALL(nvnProgramFinalize,&graphics);CALL(nvnTexturePoolFinalize,&tp);CALL(nvnSamplerPoolFinalize,&sp);CALL(nvnSamplerFinalize,&sampler);
  CALL(nvnCommandBufferFinalize,&cmd);CALL(nvnMemoryPoolFinalize,&pool);
  free(atlas);free(reference);free(payload);free(storage);CHECK(vk_validation_error_count()==0);
  printf("{\"test\":\"original022-BC3-capture-focused\",\"result\":\"passed\",\"regions\":52,\"images\":7,\"sampled_pixels\":%u,\"raw_BC_bytes\":%u,\"backend_CPU_BC_decode\":false,\"game_modules_executed\":false,\"checks\":%u}\n",sampled_pixels,h[6],checks);
}
