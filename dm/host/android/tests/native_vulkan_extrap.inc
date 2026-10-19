// 外推核的独立逐像素参考：十二点标量卷积，不复用 GLSL 五抽头写法。
// Independent per-pixel reference: scalar twelve-point convolution, rather
// than the shader's five bilinear taps. This fixture executes no game modules.
static double rx_weight(int tap,double f) {
  if (tap==-1) return -.5*f+f*f-.5*f*f*f;
  if (tap==0) return 1-2.5*f*f+1.5*f*f*f;
  if (tap==1) return .5*f+2*f*f-1.5*f*f*f;
  return -.5*f*f+.5*f*f*f;
}
static int rx_clamp(int x,int hi) { return x<0?0:x>hi?hi:x; }
static void extrap_kernel_test(int depth_format) {
  enum { width=12,height=8,count=width*height };
  if (getenv("DM_RT_TEST_NOPSO")) {
    CHECK(!vk_extrap_setup(VK_FORMAT_R16G16B16A16_SFLOAT,vk_nvn_format(depth_format)));return;
  }
  CHECK(vk_extrap_setup(VK_FORMAT_R16G16B16A16_SFLOAT,vk_nvn_format(depth_format)));
  void *storage;CHECK(!posix_memalign(&storage,65536,1<<20));memset(storage,0,1<<20);
  NVNmemoryPoolBuilder mb={0};NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtexture textures[4];
  for (int i=0;i<4;i++) {
    NVNtextureBuilder tb={0};CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
    CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);
    CALL(nvnTextureBuilderSetWidth,&tb,width);CALL(nvnTextureBuilderSetHeight,&tb,height);
    CALL(nvnTextureBuilderSetFormat,&tb,i&1?depth_format:NVN_FORMAT_RGBA16F);
    CALL(nvnTextureBuilderSetStorage,&tb,&pool,i*65536);
    CHECK(CALL(nvnTextureInitialize,&textures[i],&tb));
  }
  _Float16 *input=(void *)((uint8_t *)storage+0x50000);
  for (int y=0;y<height;y++) for (int x=0;x<width;x++) {
    input[(y*width+x)*4]=.125+x/32.;input[(y*width+x)*4+1]=.125+y/32.;
    input[(y*width+x)*4+2]=.125+(x+y)/64.;input[(y*width+x)*4+3]=.5;
  }
  NVNcopyRegion region={0,0,0,width,height,1};
  vk_rt_image *phdr=NULL,*pdepth=NULL;
  const double shifts[][2]={{0,0},{1,-2},{.25,-.5},{-.375,.25},{0,0},{.75,-.25}};
  for (unsigned test=0;test<sizeof shifts/sizeof shifts[0];test++) {
    unsigned hole=test==2?0x10:test==3?2:test==4?0x10:0;
    uint8_t source_stencil[count];memset(source_stencil,test==4?hole:0,sizeof source_stencil);
    if (hole&&test!=4) for (int y=3;y<5;y++) for (int x=4;x<6;x++) source_stencil[y*width+x]=hole;
    CALL(nvnCommandBufferBeginRecording,&cmd);CALL(nvnCommandBufferSetCopyRowStride,&cmd,0);
    CALL(nvnCommandBufferCopyBufferToTexture,&cmd,(uintptr_t)input,&textures[0],NULL,&region,0);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,0,NULL,NULL,&textures[1],NULL);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,width,height);
    CALL(nvnCommandBufferClearDepthStencil,&cmd,.25,1,test==4?hole:0,255);
    if (hole&&test!=4) {
      CALL(nvnCommandBufferSetScissor,&cmd,4,3,2,2);CALL(nvnCommandBufferClearDepthStencil,&cmd,.25,0,hole,255);
    }
    const float sentinel[]={.875,.125,.25,.5};
    CALL(nvnCommandBufferClearTexture,&cmd,&textures[2],NULL,&region,sentinel,15);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,0,NULL,NULL,&textures[3],NULL);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,width,height);
    CALL(nvnCommandBufferClearDepthStencil,&cmd,.875,1,0x20,255);
    if (test==3) {
      CALL(nvnCommandBufferSetScissor,&cmd,0,0,3,2);CALL(nvnCommandBufferClearDepthStencil,&cmd,.875,0,0x22,255);
    }
    submit(end());
    float prev[16]={0},cur[16]={0};for (int i=0;i<4;i++) prev[i*5]=cur[i*5]=1;
    if (test==5) { prev[0]=cur[0]=2;prev[5]=cur[5]=3;prev[12]=cur[12]=10000;prev[13]=cur[13]=-10000; }
    cur[12]+=(float)(2*shifts[test][0]/width);cur[13]-=(float)(2*shifts[test][1]/height);
    cur[14]=.125;
    double dx=(double)(cur[12]-prev[12])*width/2,dy=-(double)(cur[13]-prev[13])*height/2;
    mtl_sub_begin();vk_rt_image src,ds,dst,dds;
    CHECK(vk_rt_texture((tex_t *)&textures[0],&src)&&vk_rt_texture((tex_t *)&textures[1],&ds)&&
          vk_rt_texture((tex_t *)&textures[2],&dst)&&vk_rt_texture((tex_t *)&textures[3],&dds));
    phdr=vk_rt_copy(&src,phdr);pdepth=vk_rt_copy(&ds,pdepth);
    CHECK(vk_extrap_encode(&dst,&dds,phdr,pdepth,prev,cur,0x10,2));
    vk_buffer *color=vk_temp_buffer(count*8),*depth=vk_temp_buffer(count*4),*stencil=vk_temp_buffer(count);
    VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={width,height,1}};
    vk_barrier();vkCmdCopyImageToBuffer(V.cmd,dst.image,VK_IMAGE_LAYOUT_GENERAL,color->handle,1,&copy);
    copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;
    vkCmdCopyImageToBuffer(V.cmd,dds.image,VK_IMAGE_LAYOUT_GENERAL,depth->handle,1,&copy);
    copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_STENCIL_BIT;
    vkCmdCopyImageToBuffer(V.cmd,dds.image,VK_IMAGE_LAYOUT_GENERAL,stencil->handle,1,&copy);
    vk_internal_wait(vk_internal_commit());
    for (int y=0;y<height;y++) for (int x=0;x<width;x++) {
      double p=x-dx,q=y-dy;int oob=p<-.5||q<-.5||p>width-.5||q>height-.5;
      p=fmax(0,fmin(width-1,p));q=fmax(0,fmin(height-1,q));
      int bx=rx_clamp((int)floor(p),width-2),by=rx_clamp((int)floor(q),height-2);
      int cx=rx_clamp((int)nearbyint(p),width-1),cy=rx_clamp((int)nearbyint(q),height-1);
      int has_hole=source_stencil[by*width+bx]|source_stencil[by*width+bx+1]|
          source_stencil[(by+1)*width+bx]|source_stencil[(by+1)*width+bx+1];
      int blocked=test==3&&x<3&&y<2;
      unsigned expected_stencil=blocked?0x22:0x20|((has_hole||oob)?0x80:0);
      if (stencil->map[y*width+x]!=expected_stencil)
        fprintf(stderr,"extrap stencil test=%u pixel=%d,%d got=%u expected=%u source_hole=%d oob=%d\n",
                test,x,y,stencil->map[y*width+x],expected_stencil,has_hole,oob);
      CHECK(stencil->map[y*width+x]==expected_stencil);
      float got_depth;
      if (depth_format==NVN_FORMAT_DEPTH24_STENCIL8) {
        uint32_t packed;memcpy(&packed,depth->map+(y*width+x)*4,4);
        got_depth=(packed&0xffffff)/16777215.f;
      } else memcpy(&got_depth,depth->map+(y*width+x)*4,4);
      CHECK(fabs(got_depth-(blocked?.875:.375))<1e-5);
      double fx=-dx-floor(-dx),fy=-dy-floor(-dy),amount=fx*(1-fx)+fy*(1-fy);
      for (int c=0;c<4;c++) {
        double expected=1;
        if (blocked) expected=sentinel[c];
        else if (c<3&&has_hole) {
          static const int directions[8][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,1},{1,-1},{-1,-1}};
          double sum=0,weight=0;
          for (int d=0;d<8;d++) for (int r=1;r<=24;r++) {
            int sx=cx+directions[d][0]*4*r,sy=cy+directions[d][1]*4*r;
            if (sx<0||sy<0||sx>=width||sy>=height) break;
            if (source_stencil[sy*width+sx]) continue;
            double w=1./(r*r);sum+=(double)input[(sy*width+sx)*4+c]*w;weight+=w;break;
          }
          expected=weight?sum/weight:input[(cy*width+cx)*4+c];
        } else if (c<3) {
          double sum=0,weight=0,min=1e10,max=-1e10,avg=0;
          int ix=(int)floor(p),iy=(int)floor(q);
          for (int iy2=-1;iy2<=2;iy2++) for (int ix2=-1;ix2<=2;ix2++) {
            if ((ix2==-1||ix2==2)&&(iy2==-1||iy2==2)) continue;
            double w=rx_weight(ix2,p-ix)*rx_weight(iy2,q-iy);
            sum+=(double)input[(rx_clamp(iy+iy2,height-1)*width+rx_clamp(ix+ix2,width-1))*4+c]*w;weight+=w;
          }
          for (int j=0;j<2;j++) for (int i=0;i<2;i++) {
            double v=input[((by+j)*width+bx+i)*4+c];min=fmin(min,v);max=fmax(max,v);avg+=v*.25;
          }
          double cr=fmax(min,fmin(max,sum/weight));expected=fmax(0,cr+amount*(cr-avg));
        }
        _Float16 got;memcpy(&got,color->map+((y*width+x)*4+c)*2,2);
        if (fabs((double)got-expected)>.003)
          fprintf(stderr,"extrap test=%u pixel=%d,%d c=%d got=%g expected=%g\n",test,x,y,c,(double)got,expected);
        CHECK(fabs((double)got-expected)<.003);
      }
    }
  }
  mtl_sub_begin();vk_rt_release(phdr);vk_rt_release(pdepth);vk_internal_wait(vk_internal_commit());
  for (int i=0;i<4;i++) CALL(nvnTextureFinalize,&textures[i]);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);
}

// 合成原通道顺序，验证隔帧/蒙皮过滤、补洞、镜头跳变和强制完整帧。
// Synthetic original pass sequence exercises alternating/skinned rendering,
// hole fill, camera-jump fallback and force-full. No game modules execute.
static void extrap_runtime_test(const void *static_control,const void *fragment_control,int depth_format) {
  int available=!getenv("DM_RT_TEST_NOPSO");
  enum { w=512,h=16 };void *storage;CHECK(!posix_memalign(&storage,65536,1<<20));memset(storage,0,1<<20);
  NVNmemoryPoolBuilder mb={0};NVNmemoryPool pool;
  CALL(nvnMemoryPoolBuilderSetDefaults,&mb);CALL(nvnMemoryPoolBuilderSetDevice,&mb,&device);
  CALL(nvnMemoryPoolBuilderSetStorage,&mb,storage,1<<20);
  CALL(nvnMemoryPoolBuilderSetFlags,&mb,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
  CHECK(CALL(nvnMemoryPoolInitialize,&pool,&mb));
  NVNtexture texture[5];
  for (int i=0;i<5;i++) {
    NVNtextureBuilder tb={0};CALL(nvnTextureBuilderSetDefaults,&tb);CALL(nvnTextureBuilderSetDevice,&tb,&device);
    CALL(nvnTextureBuilderSetTarget,&tb,NVN_TEXTURE_TARGET_2D);CALL(nvnTextureBuilderSetWidth,&tb,w);CALL(nvnTextureBuilderSetHeight,&tb,h);
    CALL(nvnTextureBuilderSetFormat,&tb,i==1?depth_format:i==4?NVN_FORMAT_RGBA8:NVN_FORMAT_R11G11B10F);
    CALL(nvnTextureBuilderSetStorage,&tb,&pool,i*65536);CHECK(CALL(nvnTextureInitialize,&texture[i],&tb));
  }
  NVNwindowBuilder wb={0};NVNwindow window;NVNtexture *windows[]={&texture[4]};
  CALL(nvnWindowBuilderSetDefaults,&wb);CALL(nvnWindowBuilderSetDevice,&wb,&device);
  CALL(nvnWindowBuilderSetTextures,&wb,1,windows);CHECK(CALL(nvnWindowInitialize,&window,&wb));
  uint8_t skin_control[0x880];memset(skin_control,13,sizeof skin_control);
  NVNprogram programs[4];
  for (unsigned i=0;i<4;i++) {
    NVNshaderData shaders[]={{.control=i&1?skin_control:static_control},{.control=fragment_control}};
    CHECK(CALL(nvnProgramInitialize,&programs[i],&device));
    CHECK(CALL(nvnProgramSetShaders,&programs[i],i<2?2:1,shaders));
    vk_shader *list[6];vk_program_shaders((prog_t *)&programs[i],list);
  }
  // 只把自有测试着色器标为原蒙皮角色，生产键表完全不改；结束后恢复。
  // Tag only our fixture shader with an upstream skin role, restoring it at
  // exit. Production shader classification and game data are unchanged.
  vk_shader *skin_shader=vk_shader_get(skin_control);char key[17];memcpy(key,skin_shader->key,17);
  memcpy(skin_shader->key,"0fa4eb7559f1637a",17);
  NVNdepthStencilState plain,unit;
  CALL(nvnDepthStencilStateSetDefaults,&plain);CALL(nvnDepthStencilStateSetDepthTestEnable,&plain,0);
  CALL(nvnDepthStencilStateSetDepthWriteEnable,&plain,0);CALL(nvnDepthStencilStateSetStencilTestEnable,&plain,0);
  unit=plain;CALL(nvnDepthStencilStateSetStencilTestEnable,&unit,1);
  CALL(nvnDepthStencilStateSetStencilFunc,&unit,NVN_FACE_FRONT_AND_BACK,NVN_STENCIL_FUNC_ALWAYS);
  CALL(nvnDepthStencilStateSetStencilOp,&unit,NVN_FACE_FRONT_AND_BACK,NVN_STENCIL_OP_KEEP,NVN_STENCIL_OP_KEEP,NVN_STENCIL_OP_REPLACE);
  NVNpolygonState polygon;CALL(nvnPolygonStateSetDefaults,&polygon);CALL(nvnPolygonStateSetCullFace,&polygon,NVN_FACE_NONE);
  NVNcolorState color;CALL(nvnColorStateSetDefaults,&color);NVNchannelMaskState channels;CALL(nvnChannelMaskStateSetDefaults,&channels);
  float *camera=(void *)((uint8_t *)storage+0x90000),*uniform=(void *)((uint8_t *)storage+0x91000);
  const NVNtexture *hdr[]={&texture[0]},*ldr[]={&texture[2]},*aux[]={&texture[3]},*screen[]={&texture[4]},*oit[]={&texture[0],&texture[3]};
  const float green[]={0,1,0,1},red[]={1,0,0,1},blue[]={0,0,1,1},viewport[]={0,0,w,h};
  NVNcopyRegion region={0,0,0,w,h,1};uint8_t pixels[w*h*4];
  extern int g_dm_rt_ab;g_dm_rt_ab=1;vk_runtime_force_full(0);mtl_world_frozen(0);
  vk_runtime_stats baseline,stats;vk_runtime_snapshot(&baseline);
  for (unsigned frame=0;frame<9;frame++) {
    if (frame==6) vk_runtime_force_full(2);
    float matrices[132]={0};for (int i=0;i<4;i++) matrices[84+i*5]=matrices[116+i*5]=1;
    if (frame==5) matrices[84+12]=1; // 镜头跳变，先于任何世界过滤。 | Jump before world filtering.
    mtl_set_frame(10000+frame);
    CALL(nvnCommandBufferBeginRecording,&cmd);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,0,NULL,NULL,&texture[1],NULL);
    CALL(nvnCommandBufferSetViewports,&cmd,0,1,viewport);CALL(nvnCommandBufferSetScissor,&cmd,0,0,w,h);
    CALL(nvnCommandBufferClearDepthStencil,&cmd,.25,1,0,255);
    CALL(nvnCommandBufferBindVertexAttribState,&cmd,0,NULL);CALL(nvnCommandBufferBindVertexStreamState,&cmd,0,NULL);
    CALL(nvnCommandBufferBindColorState,&cmd,&color);CALL(nvnCommandBufferBindChannelMaskState,&cmd,&channels);
    CALL(nvnCommandBufferBindPolygonState,&cmd,&polygon);CALL(nvnCommandBufferBindDepthStencilState,&cmd,&plain);
    CALL(nvnCommandBufferBindUniformBuffer,&cmd,NVN_SHADER_STAGE_VERTEX,3,(uintptr_t)camera,sizeof matrices);
    CALL(nvnCommandBufferUpdateUniformBuffer,&cmd,(uintptr_t)camera,sizeof matrices,0,sizeof matrices,matrices);
    CALL(nvnCommandBufferBindUniformBuffer,&cmd,NVN_SHADER_STAGE_FRAGMENT,0,(uintptr_t)uniform,16);
    CALL(nvnCommandBufferBindProgram,&cmd,&programs[2],NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,0,0);CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,0,NULL,NULL,&texture[1],NULL);
    CALL(nvnCommandBufferBindDepthStencilState,&cmd,&unit);
    CALL(nvnCommandBufferSetStencilRef,&cmd,NVN_FACE_FRONT_AND_BACK,0x10);
    CALL(nvnCommandBufferSetStencilMask,&cmd,NVN_FACE_FRONT_AND_BACK,0x10);
    CALL(nvnCommandBufferBindProgram,&cmd,&programs[3],NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferSetScissor,&cmd,10+frame*5,4,4,4);CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,hdr,NULL,&texture[1],NULL);
    CALL(nvnCommandBufferBindDepthStencilState,&cmd,&plain);CALL(nvnCommandBufferSetScissor,&cmd,0,0,w,h);
    CALL(nvnCommandBufferClearColor,&cmd,0,blue,15);
    CALL(nvnCommandBufferBindProgram,&cmd,&programs[0],NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferUpdateUniformBuffer,&cmd,(uintptr_t)uniform,16,0,16,green);
    CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    CALL(nvnCommandBufferBindDepthStencilState,&cmd,&unit);CALL(nvnCommandBufferBindProgram,&cmd,&programs[1],NVN_SHADER_STAGE_ALL_GRAPHICS_BITS);
    CALL(nvnCommandBufferUpdateUniformBuffer,&cmd,(uintptr_t)uniform,16,0,16,red);
    CALL(nvnCommandBufferSetScissor,&cmd,10+frame*5,4,4,4);CALL(nvnCommandBufferDrawArrays,&cmd,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
    // 这里验重建的不透明主通道；后面的合成/色调映射只构造角色边界，不模拟游戏着色器。
    // Read the reconstructed opaque pass. Later passes identify roles; this
    // fixture does not imitate the game's composite/tone-mapping shaders.
    CALL(nvnCommandBufferCopyTextureToTexture,&cmd,&texture[0],NULL,&region,&texture[4],NULL,&region,0);
    CALL(nvnCommandBufferSetScissor,&cmd,0,0,w,h);CALL(nvnCommandBufferSetRenderTargets,&cmd,2,oit,NULL,&texture[1],NULL);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,ldr,NULL,&texture[1],NULL);
    CALL(nvnCommandBufferClearColor,&cmd,0,blue,15);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,hdr,NULL,&texture[1],NULL);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,ldr,NULL,&texture[1],NULL);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,aux,NULL,NULL,NULL);
    CALL(nvnCommandBufferSetRenderTargets,&cmd,1,screen,NULL,NULL,NULL);
    uint64_t fence=mtl_last_fence();submit(end());CHECK(mtl_last_fence()==fence+1);
    CALL(nvnTextureReadTexels,&texture[4],NULL,&region,pixels);
    for (int y=0;y<h;y++) for (int x=0;x<w;x++) {
      int moving=y>=4&&y<8&&x>=10+(int)frame*5&&x<14+(int)frame*5;
      if (memcmp(pixels+(y*w+x)*4,moving?(uint8_t[]){255,0,0,255}:(uint8_t[]){0,255,0,255},4))
        fprintf(stderr,"runtime frame=%u pixel=%d,%d got=%u,%u,%u expected moving=%d\n",frame,x,y,pixels[(y*w+x)*4],pixels[(y*w+x)*4+1],pixels[(y*w+x)*4+2],moving);
      CHECK(!memcmp(pixels+(y*w+x)*4,moving?(uint8_t[]){255,0,0,255}:(uint8_t[]){0,255,0,255},4));
    }
    vk_runtime_snapshot(&stats);
    if (available) {
      if (frame==3||frame==8) CHECK(stats.state==2);
      if (frame==5||frame==6||frame==7) CHECK(stats.state==0);
    }
  }
  if (available) {
    CHECK(stats.encoded>=baseline.encoded+2);CHECK(stats.jumps>=baseline.jumps+1);
    CHECK(stats.camera_reads>=baseline.camera_reads+4);CHECK(stats.camera_waits>=baseline.camera_waits+2);
  }
  else CHECK(stats.disabled);
  g_dm_rt_ab=0;
  CALL(nvnCommandBufferBeginRecording,&cmd);CALL(nvnCommandBufferSetRenderTargets,&cmd,1,screen,NULL,NULL,NULL);submit(end());
  vk_runtime_snapshot(&stats);CHECK(!stats.enabled&&stats.state==0);
  memcpy(skin_shader->key,key,17);
  for (unsigned i=0;i<4;i++) CALL(nvnProgramFinalize,&programs[i]);
  CALL(nvnWindowFinalize,&window);for (unsigned i=0;i<5;i++) CALL(nvnTextureFinalize,&texture[i]);
  CALL(nvnMemoryPoolFinalize,&pool);free(storage);
}
