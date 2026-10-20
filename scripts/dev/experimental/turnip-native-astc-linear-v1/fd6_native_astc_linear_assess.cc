/* SPDX-License-Identifier: MIT
 * Source-only assessment using the unchanged existing explicit fully-linear
 * FDL path. This is not a Vulkan policy change or a hardware validation.
 */
#include "freedreno_layout.h"
#include "fd6_hw.h"
#include <cstdio>
#include <cassert>
int main()
{
 const unsigned shapes[][3]={{256,256,9},{128,128,8},{64,64,7},{256,128,9},
    {32,32,6},{128,64,8},{128,256,9},{64,128,8},{16,16,5}};
 const struct fd_dev_id id={.chip_id=0x43030b00};
 for(auto &s:shapes){
  struct fdl_image_params p={};p.format=PIPE_FORMAT_ASTC_4x4;p.nr_samples=1;
  p.width0=s[0];p.height0=s[1];p.depth0=1;p.mip_levels=s[2];p.array_size=1;p.tile_mode=TILE6_LINEAR;
  struct fdl_explicit_layout e={0,align(util_format_get_nblocksx(p.format,p.width0)*16,256)};
  struct fdl_layout l;assert(fdl6_layout_image(&l,fd_dev_info_raw(&id),&p,&e));
  assert(l.tile_mode==TILE6_LINEAR&&l.pitchalign==8&&!l.ubwc);
  const struct fdl_layout *layouts[3]={&l,nullptr,nullptr};
  unsigned checked=0;
  for(unsigned base=0;base<p.mip_levels;++base){
   struct fdl_view_args a={};a.iova=0x100000000ull;a.base_miplevel=base;a.level_count=p.mip_levels-base;
   a.layer_count=1;a.min_lod_clamp=base;a.format=p.format;a.type=FDL_VIEW_TYPE_2D;
   a.swiz[0]=PIPE_SWIZZLE_X;a.swiz[1]=PIPE_SWIZZLE_Y;a.swiz[2]=PIPE_SWIZZLE_Z;a.swiz[3]=PIPE_SWIZZLE_W;
   struct fdl6_view v;fdl6_view_init<A7XX>(&v,layouts,&a,false);
   assert((v.descriptor[2]&A6XX_TEX_MEMOBJ_2_PITCHALIGN__MASK)==2);
   struct fdl_image_params sub=p;sub.width0=u_minify(p.width0,base);sub.height0=u_minify(p.height0,base);sub.mip_levels-=base;
   struct fdl_explicit_layout sube={0,v.pitch};struct fdl_layout subl;
   assert(fdl6_layout_image(&subl,fd_dev_info_raw(&id),&sub,&sube));
   for(unsigned m=base;m<p.mip_levels;++m){
    assert(fdl_pitch(&l,m)==fdl_pitch(&subl,m-base));
    assert(l.slices[m].offset-l.slices[base].offset==subl.slices[m-base].offset);++checked;
   }
  }
  printf("{\"width\":%u,\"height\":%u,\"levels\":%u,\"physical_host_alignment4096_bytes\":%llu,\"FDL_raw_size\":%llu,\"baseMip_submip_pitch_and_offset_checks\":%u,\"mips\":[",s[0],s[1],s[2],(unsigned long long)align64(l.size,4096),(unsigned long long)l.size,checked);
  for(unsigned m=0;m<p.mip_levels;++m)printf("%s{\"mip\":%u,\"pitch\":%u,\"offset\":%u,\"size\":%u}",m?",":"",m,fdl_pitch(&l,m),l.slices[m].offset,l.slices[m].size0);
  puts("]}");
 }
}
