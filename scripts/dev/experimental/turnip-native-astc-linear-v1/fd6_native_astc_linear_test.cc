/* Copyright © 2026. SPDX-License-Identifier: MIT
 * Pure FDL/descriptor/CPU copy checks. No Vulkan instance or GPU access.
 */
#include "freedreno_layout.h"
#include "fd6_hw.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static unsigned checks, shapes, views, copied_mips;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); abort(); } } while (0)

static std::vector<char>
blocks(unsigned w, unsigned h, unsigned level, unsigned layer)
{
   std::vector<char> b(w * h * 16);
   for (unsigned i=0; i<b.size(); ++i)
      b[i] = (char)((i * 37 + level * 71 + layer * 97) & 255);
   return b;
}

static void
check_image(enum pipe_format format, unsigned width, unsigned height,
            unsigned layers, bool tiled)
{
   const struct fd_dev_id id = {.chip_id=0x43030b00};
   const struct fd_dev_info *info = fd_dev_info_raw(&id);
   CHECK(info && info->props.supports_linear_mipmap_threshold_in_blocks);
   struct fdl_image_params p = {};
   p.format=format; p.width0=width; p.height0=height; p.depth0=1;
   p.nr_samples=1; p.array_size=layers; p.tile_mode=tiled?TILE6_3:TILE6_LINEAR;
   p.mip_levels=util_logbase2(MAX2(width,height))+1;
   struct fdl_layout l;
   struct fdl_explicit_layout explicit_layout={0,align(util_format_get_nblocksx(format,width)*16,256)};
   CHECK(fdl6_layout_image(&l,info,&p,&explicit_layout)); ++shapes;
   CHECK(l.pitchalign==8 && !l.ubwc && l.layer_first);
   CHECK(l.layer_size % 4096==0 && align64(l.size,4096)==l.layer_size && layers==1);
   CHECK(l.tile_mode==TILE6_LINEAR && l.pitch0==explicit_layout.pitch);

   const struct fdl_layout *layouts[3]={&l,nullptr,nullptr};
   for (unsigned base=0;base<p.mip_levels;++base) {
      for (unsigned layer=0;layer<layers;++layer) {
         struct fdl_view_args a = {};
         a.iova=0x100000000ull; a.base_miplevel=base;
         a.level_count=p.mip_levels-base; a.base_array_layer=layer;
         a.layer_count=layers-layer; a.min_lod_clamp=base;
         a.format=format; a.type=FDL_VIEW_TYPE_2D;
         a.swiz[0]=PIPE_SWIZZLE_X; a.swiz[1]=PIPE_SWIZZLE_Y;
         a.swiz[2]=PIPE_SWIZZLE_Z; a.swiz[3]=PIPE_SWIZZLE_W;
         struct fdl6_view v;
         fdl6_view_init<A7XX>(&v,layouts,&a,false); ++views;
         CHECK((v.descriptor[2]&A6XX_TEX_MEMOBJ_2_PITCHALIGN__MASK)==2);
         CHECK(((v.descriptor[2]&A6XX_TEX_MEMOBJ_2_PITCH__MASK)>>
                  A6XX_TEX_MEMOBJ_2_PITCH__SHIFT)==fdl_pitch(&l,base));
         CHECK(v.pitch==fdl_pitch(&l,base));
         CHECK(v.base_addr==a.iova+fdl_surface_offset(&l,base,layer));
         CHECK((v.descriptor[1]&A6XX_TEX_MEMOBJ_1_WIDTH__MASK)==u_minify(width,base));
         CHECK(((v.descriptor[1]&A6XX_TEX_MEMOBJ_1_HEIGHT__MASK)>>A6XX_TEX_MEMOBJ_1_HEIGHT__SHIFT)==u_minify(height,base));
         CHECK(((v.descriptor[3]&A6XX_TEX_MEMOBJ_3_ARRAY_PITCH__MASK)<<12)==l.layer_size);
         struct fdl_image_params sub=p;
         sub.width0=u_minify(width,base); sub.height0=u_minify(height,base);
         sub.mip_levels=p.mip_levels-base;
         struct fdl_explicit_layout sub_explicit={0,v.pitch};
         struct fdl_layout sublayout;
         CHECK(fdl6_layout_image(&sublayout,info,&sub,&sub_explicit));
         for(unsigned m=base;m<p.mip_levels;++m) {
            CHECK(fdl_pitch(&l,m)==fdl_pitch(&sublayout,m-base));
            CHECK(l.slices[m].offset-l.slices[base].offset==sublayout.slices[m-base].offset);
         }
      }
   }

   /* The same FDL helpers used by host-image-copy provide pitch, mip/layer
    * offsets and tiled swizzle. Fill all mips/layers before reading any back,
    * so writes into adjacent slices cannot hide behind symmetric copies.
    */
   for (unsigned mode=0;mode<2;++mode) {
      struct fdl_ubwc_config c = {16,0,(enum fdl_macrotile_mode)mode};
      std::vector<char> image(l.size+128, (char)0x5a);
      char *mem=image.data()+64;
      for (unsigned layer=0;layer<layers;++layer) {
         for (unsigned m=0;m<p.mip_levels;++m) {
            unsigned bw=util_format_get_nblocksx(format,u_minify(width,m));
            unsigned bh=util_format_get_nblocksy(format,u_minify(height,m));
            unsigned pitch=fdl_pitch(&l,m), off=fdl_surface_offset(&l,m,layer);
            if (fdl_tile_mode(&l,m)) {
               CHECK(pitch%1024==0);
               CHECK(l.slices[m].size0==pitch*align(bh,16));
            } else {
               CHECK(pitch==align(u_minify(l.pitch0,m),256));
               CHECK(l.slices[m].size0==pitch*(m==p.mip_levels-1?align(bh,4):bh));
            }
            CHECK(off+l.slices[m].size0<=l.size);
            auto src=blocks(bw,bh,m,layer);
            if (fdl_tile_mode(&l,m))
               fdl6_memcpy_linear_to_tiled(0,0,bw,bh,mem+off,src.data(),&l,m,bw*16,&c);
            else for (unsigned y=0;y<bh;++y)
               memcpy(mem+off+y*pitch,src.data()+y*bw*16,bw*16);
         }
      }
      for (unsigned layer=0;layer<layers;++layer) {
         for (unsigned m=0;m<p.mip_levels;++m) {
            unsigned bw=util_format_get_nblocksx(format,u_minify(width,m));
            unsigned bh=util_format_get_nblocksy(format,u_minify(height,m));
            unsigned pitch=fdl_pitch(&l,m), off=fdl_surface_offset(&l,m,layer);
            auto expected=blocks(bw,bh,m,layer);
            std::vector<char> result(expected.size(),0);
            if (fdl_tile_mode(&l,m))
               fdl6_memcpy_tiled_to_linear(0,0,bw,bh,result.data(),mem+off,&l,m,bw*16,&c);
            else for (unsigned y=0;y<bh;++y)
               memcpy(result.data()+y*bw*16,mem+off+y*pitch,bw*16);
            CHECK(result==expected); ++copied_mips;
         }
      }
      CHECK(std::all_of(image.begin(),image.begin()+64,[](char v){return v==(char)0x5a;}));
      CHECK(std::all_of(image.end()-64,image.end(),[](char v){return v==(char)0x5a;}));
   }
   printf("{\"format\":%u,\"width\":%u,\"height\":%u,\"levels\":%u,\"layers\":%u,\"requested_tiled\":%s,\"size\":%llu,\"layer_size\":%llu,\"mips\":[",format,width,height,p.mip_levels,layers,tiled?"true":"false",(unsigned long long)l.size,(unsigned long long)l.layer_size);
   for(unsigned m=0;m<p.mip_levels;++m)
      printf("%s{\"level\":%u,\"offset\":%u,\"pitch\":%u,\"size\":%u,\"tiled\":%s}",m?",":"",m,l.slices[m].offset,fdl_pitch(&l,m),l.slices[m].size0,fdl_tile_mode(&l,m)?"true":"false");
   puts("]}");
}

int main()
{
   const enum pipe_format formats[]={PIPE_FORMAT_ASTC_4x4,PIPE_FORMAT_ASTC_4x4_SRGB,
                                    PIPE_FORMAT_ASTC_6x6,PIPE_FORMAT_ASTC_6x6_SRGB};
   const unsigned dims[][2]={{1,1},{5,7},{15,17},{63,65},{64,64},{65,67},
      {95,97},{96,96},{97,101},{128,128},{256,256},{257,129},
      {512,256},{512,512},{1024,512},{1024,1024}};
   for(auto f:formats)for(auto &d:dims)check_image(f,d[0],d[1],1,false);
   fprintf(stderr,"PASS %u shapes, %u baseMip/layer views, %u CPU block-copy mip roundtrips, %u checks; no GPU\n",shapes,views,copied_mips,checks);
}
