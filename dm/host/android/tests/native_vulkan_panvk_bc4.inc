// One actual oldcapture BC4 single2D -> arraylayer250 pair; no game/driver change.
typedef struct {uint32_t mip,w,h,offset,span,pitch,valid,block_rows,buffer,upload_api,copy_api,reserved;} panvk_bc4_row;
_Static_assert(sizeof(panvk_bc4_row)==48,"fixed BC4 row");
extern int vk_texture_test_bc_decoded(const tex_t *);
static void panvk_bc4_layered_test(const char *path) {
  CHECK(!mtl_pool_shared_mode() && vk_async_submissions());
  const char *negative=getenv("DM_PANVK_BC4_NEGATIVE");CHECK(!negative || !strcmp(negative,"wrong-mip"));
  FILE *f=fopen(path,"rb");CHECK(f);uint32_t h[16];panvk_bc4_row rows[7];
  CHECK(fread(h,1,sizeof h,f)==sizeof h);
  CHECK(h[0]==0x34434250 && h[1]==1 && h[2]==7 && h[3]==NVN_FORMAT_RGTC1_UNORM && h[4]==128 && h[5]==8 && h[6]==128 && h[7]==7 && h[8]==252 && h[9]==250 && h[10]==94354 && h[11]==95779 && h[12]==11848 && h[13]==10928 && h[14]==512 && h[15]==128);
  CHECK(fread(rows,1,sizeof rows,f)==sizeof rows);uint8_t *payload=malloc(h[12]);CHECK(payload);
  CHECK(fread(payload,1,h[12],f)==h[12] && fgetc(f)==EOF && !ferror(f));CHECK(!fclose(f));
  uint32_t tight_offsets[7],tight_size=0,atlas_x[7],right=0,end=0;
  const uint32_t upload_api[]={89521,89522,89523,89530,89532,89534,89536};
  for(unsigned n=0;n<7;n++) {
    const panvk_bc4_row *r=&rows[n];uint32_t w=128>>n;
    CHECK(r->mip==n && r->w==w && r->h==w && r->pitch==(n<3?((w+3)/4)*8:256) && r->valid==((w+3)/4)*8 && r->block_rows==(w+3)/4);
    CHECK(r->span==(r->block_rows-1)*r->pitch+r->valid && r->offset==((end+15)&~15u) && r->offset<=h[12] && r->span<=h[12]-r->offset);
    CHECK(r->upload_api==upload_api[n] && r->copy_api==94773+2*n && !r->reserved);end=r->offset+r->span;
    tight_offsets[n]=tight_size;tight_size+=r->valid*r->block_rows;atlas_x[n]=right;right+=w;
  }
  CHECK(end==h[12] && tight_size==h[13] && right==254);
  uint8_t *tight=malloc(tight_size),*reference=malloc(128*128);CHECK(tight && reference);
  for(unsigned n=0;n<7;n++)for(unsigned y=0;y<rows[n].block_rows;y++)memcpy(tight+tight_offsets[n]+y*rows[n].valid,payload+rows[n].offset+y*rows[n].pitch,rows[n].valid);
  enum {pool_size=32u<<20,upload_at=1u<<20,source_at=2u<<20,raw_at=3u<<20,array_at=4u<<20,output_at=24u<<20,vertex_at=31u<<20};
  uint8_t *storage;CHECK(!posix_memalign((void **)&storage,65536,pool_size));memset(storage,0,pool_size);memcpy(storage+upload_at,payload,h[12]);
  NVNmemoryPoolBuilder mb;NVNmemoryPool pool;CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,pool_size);CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNcommandBuffer commands[5];for(unsigned n=0;n<5;n++){CHECK(CALL(nvnCommandBufferInitialize,&commands[n],&device));CALL(nvnCommandBufferAddCommandMemory,&commands[n],&pool,n*32768u,32768u);}
  NVNtexture source,array,output;
  for(unsigned n=0;n<3;n++) {
    NVNtextureBuilder b;CALL(nvnTextureBuilderSetDefaults,&b);CALL(nvnTextureBuilderSetDevice,&b,&device);CALL(nvnTextureBuilderSetTarget,&b,n==1?NVN_TEXTURE_TARGET_2D_ARRAY:NVN_TEXTURE_TARGET_2D);
    CALL(nvnTextureBuilderSetWidth,&b,n==2?512:128);CALL(nvnTextureBuilderSetHeight,&b,128);CALL(nvnTextureBuilderSetDepth,&b,n==1?252:1);CALL(nvnTextureBuilderSetLevels,&b,n==0?8:n==1?7:1);CALL(nvnTextureBuilderSetFormat,&b,n==2?NVN_FORMAT_RGBA8:NVN_FORMAT_RGTC1_UNORM);
    CHECK(CALL(nvnTextureBuilderGetStorageSize,&b)<=(n==1?16u<<20:1u<<20));CALL(nvnTextureBuilderSetStorage,&b,&pool,n==0?source_at:n==1?array_at:output_at);CHECK(CALL(nvnTextureInitialize,n==0?&source:n==1?&array:&output,&b));
  }
  NVNtexturePool tp;NVNsamplerPool sp;CHECK(CALL(nvnTexturePoolInitialize,&tp,&pool,vertex_at+65536,64));CHECK(CALL(nvnSamplerPoolInitialize,&sp,&pool,vertex_at+69632,64));
  NVNsamplerBuilder sb;NVNsampler sampler;CALL(nvnSamplerBuilderSetDefaults,&sb);CALL(nvnSamplerBuilderSetDevice,&sb,&device);CALL(nvnSamplerBuilderSetMinMagFilter,&sb,NVN_MIN_FILTER_NEAREST,NVN_MAG_FILTER_NEAREST);CALL(nvnSamplerBuilderSetWrapMode,&sb,NVN_WRAP_MODE_CLAMP_TO_EDGE,NVN_WRAP_MODE_CLAMP_TO_EDGE,NVN_WRAP_MODE_CLAMP_TO_EDGE);CHECK(CALL(nvnSamplerInitialize,&sampler,&sb));CALL(nvnSamplerPoolRegisterSampler,&sp,1,&sampler);
  NVNtextureView views[2][7];NVNseparateTextureHandle handles[2][7];
  for(unsigned variant=0;variant<2;variant++)for(unsigned n=0;n<7;n++) {
    NVNtextureView *v=&views[variant][n];CALL(nvnTextureViewSetDefaults,v);CALL(nvnTextureViewSetLevels,v,n,1);
    if(variant){CALL(nvnTextureViewSetLayers,v,250,1);CALL(nvnTextureViewSetTarget,v,NVN_TEXTURE_TARGET_2D);}
    CALL(nvnTexturePoolRegisterTexture,&tp,1+variant*7+n,variant?&array:&source,v);handles[variant][n]=CALL(nvnDeviceGetSeparateTextureHandle,&device,1+variant*7+n);
  }
  NVNseparateSamplerHandle sh=CALL(nvnDeviceGetSeparateSamplerHandle,&device,1);
  uint8_t vc[0x880],fc[0x880];memset(vc,3,sizeof vc);memset(fc,4,sizeof fc);NVNshaderData shaders[2]={{.control=vc},{.control=fc}};NVNprogram graphics;CHECK(CALL(nvnProgramInitialize,&graphics,&device));CHECK(CALL(nvnProgramSetShaders,&graphics,2,shaders));
  const float vertices[12]={-1,1,0,0,3,1,2,0,-1,-3,0,2};memcpy(storage+vertex_at,vertices,sizeof vertices);NVNbufferRange vr={(uintptr_t)storage+vertex_at,sizeof vertices};
  NVNvertexAttribState attrs[2];for(unsigned n=0;n<2;n++){CALL(nvnVertexAttribStateSetDefaults,&attrs[n]);CALL(nvnVertexAttribStateSetFormat,&attrs[n],NVN_FORMAT_RG32F,n*8);}NVNvertexStreamState stream;CALL(nvnVertexStreamStateSetDefaults,&stream);CALL(nvnVertexStreamStateSetStride,&stream,16);
  NVNcolorState color;NVNpolygonState polygon;NVNdepthStencilState depth;CALL(nvnColorStateSetDefaults,&color);CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);CALL(nvnDepthStencilStateSetDefaults,&depth);CALL(nvnDepthStencilStateSetDepthTestEnable,&depth,0);CALL(nvnDepthStencilStateSetDepthWriteEnable,&depth,0);
  for(unsigned phase=0;phase<5;phase++) {
    NVNcommandBuffer *c=&commands[phase];CALL(nvnCommandBufferBeginRecording,c);
    if(phase==0)for(unsigned n=0;n<7;n++) {
      panvk_bc4_row *r=&rows[n];NVNcopyRegion region={0,0,0,r->w,r->h,1};CALL(nvnCommandBufferSetCopyRowStride,c,n<3?0:r->pitch);CALL(nvnCommandBufferCopyBufferToTexture,c,(uintptr_t)storage+upload_at+r->offset,&source,&views[0][n],&region,0);
    }
    if(phase==2)for(unsigned n=0;n<7;n++) {
      panvk_bc4_row *r=&rows[n];NVNcopyRegion region={0,0,0,r->w,r->h,1};CALL(nvnCommandBufferCopyTextureToTexture,c,&source,&views[0][n],&region,&array,&views[1][n],&region,0);
    }
    if(phase==1 || phase==3) {
      unsigned variant=(phase==3);CALL(nvnCommandBufferBarrier,c,NVN_BARRIER_ORDER_PRIMITIVES_BIT|NVN_BARRIER_INVALIDATE_TEXTURE_BIT);CALL(nvnCommandBufferSetTexturePool,c,&tp);CALL(nvnCommandBufferSetSamplerPool,c,&sp);const NVNtexture *targets[]={&output};CALL(nvnCommandBufferSetRenderTargets,c,1,targets,NULL,NULL,NULL);CALL(nvnCommandBufferBindProgram,c,&graphics,NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);CALL(nvnCommandBufferBindColorState,c,&color);CALL(nvnCommandBufferBindPolygonState,c,&polygon);CALL(nvnCommandBufferBindDepthStencilState,c,&depth);CALL(nvnCommandBufferBindVertexAttribState,c,2,attrs);CALL(nvnCommandBufferBindVertexStreamState,c,1,&stream);CALL(nvnCommandBufferBindVertexBuffers,c,0,1,&vr);CALL(nvnCommandBufferBindSeparateSamplers,c,NVN_SHADER_STAGE_FRAGMENT,0,1,&sh);
      for(unsigned n=0;n<7;n++) {panvk_bc4_row *r=&rows[n];float vp[]={variant*256+atlas_x[n],0,r->w,r->h};CALL(nvnCommandBufferSetViewports,c,0,1,vp);CALL(nvnCommandBufferSetScissor,c,variant*256+atlas_x[n],0,r->w,r->h);const NVNseparateTextureHandle *selected=negative?&handles[0][6]:&handles[variant][n];CALL(nvnCommandBufferBindSeparateTextures,c,NVN_SHADER_STAGE_FRAGMENT,0,1,selected);CALL(nvnCommandBufferDrawArrays,c,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);}
    }
    if(phase==4)for(unsigned variant=0;variant<2;variant++)for(unsigned n=0;n<7;n++) {panvk_bc4_row *r=&rows[n];NVNcopyRegion region={0,0,0,r->w,r->h,1};CALL(nvnCommandBufferSetCopyRowStride,c,0);CALL(nvnCommandBufferCopyTextureToBuffer,c,variant?&array:&source,&views[variant][n],&region,(uintptr_t)storage+raw_at+variant*tight_size+tight_offsets[n],0);}
    NVNcommandHandle handle=CALL(nvnCommandBufferEndRecording,c);CALL(nvnQueueSubmitCommands,&queue,1,&handle);
  }
  NVNsync sync;CHECK(CALL(nvnSyncInitialize,&sync,&device));CALL(nvnQueueFenceSync,&queue,&sync,NVN_SYNC_CONDITION_ALL_GPU_COMMANDS_COMPLETE,1);NVNsyncWaitResult waited=CALL(nvnSyncWait,&sync,UINT64_MAX);CHECK(waited==NVN_SYNC_WAIT_RESULT_ALREADY_SIGNALED || waited==NVN_SYNC_WAIT_RESULT_CONDITION_SATISFIED);CALL(nvnSyncFinalize,&sync);
  CHECK(!vk_texture_test_bc_decoded((const tex_t *)&source) && !vk_texture_test_bc_decoded((const tex_t *)&array));
  uint8_t *atlas=malloc(512*128*4);CHECK(atlas);NVNcopyRegion all={0,0,0,512,128,1};CALL(nvnTextureReadTexels,&output,NULL,&all,atlas);unsigned pixels=0;
  for(unsigned n=0;n<7;n++) {panvk_bc4_row *r=&rows[n];CHECK(!nvn_bc_decode(NVN_FORMAT_RGTC1_UNORM,payload+r->offset,r->pitch,r->w,r->h,reference,r->w));
    for(unsigned variant=0;variant<2;variant++)for(unsigned y=0;y<r->h;y++)for(unsigned x=0;x<r->w;x++){uint8_t *p=atlas+4*(y*512+variant*256+atlas_x[n]+x);unsigned want=reference[y*r->w+x];if(abs((int)p[0]-(int)want)>2)LOG("[BC4 layered mismatch] variant=%u mip=%u xy=%u,%u got=%u expected=%u",variant,n,x,y,p[0],want);CHECK(abs((int)p[0]-(int)want)<=2);CHECK(p[1]==0 && p[2]==0 && p[3]==255);pixels++;}
  }
  CHECK(pixels==43688);for(unsigned variant=0;variant<2;variant++)CHECK(!memcmp(storage+raw_at+variant*tight_size,tight,tight_size));
  CALL(nvnTextureFinalize,&source);CALL(nvnTextureFinalize,&array);CALL(nvnTextureFinalize,&output);CALL(nvnProgramFinalize,&graphics);CALL(nvnTexturePoolFinalize,&tp);CALL(nvnSamplerPoolFinalize,&sp);CALL(nvnSamplerFinalize,&sampler);for(unsigned n=0;n<5;n++)CALL(nvnCommandBufferFinalize,&commands[n]);CALL(nvnMemoryPoolFinalize,&pool);free(atlas);free(tight);free(reference);free(payload);free(storage);CHECK(vk_validation_error_count()==0);
  printf("{\"test\":\"original022-BC4-layered-T2T\",\"result\":\"passed\",\"BC_format\":139,\"mips_copied\":7,\"destination_layers\":252,\"only_layer_read\":250,\"source_levels\":8,\"destination_levels\":7,\"NVN_submissions\":5,\"independent_command_objects\":5,\"explicit_middle_drains\":0,\"final_fence_waits\":1,\"sampled_pixels\":%u,\"raw_BC_bytes_compared\":%u,\"backend_CPU_BC_decode\":false,\"game_modules_executed\":false,\"checks\":%u}\n",pixels,2*tight_size,checks);
}
