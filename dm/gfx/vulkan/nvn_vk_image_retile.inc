// Shared native copy publication. This is a GPU -> original NVN-layout write,
// not a CPU snapshot, a dirty-page exemption, or an assertion that ASTC is
// immutable. Unsupported copies keep the existing GPU-authority guard.
#ifdef DM_VULKAN_SHARED_POOLS
#include "nvn_vk_image_retile_spv.h"
typedef struct { VkBuffer handle;uint64_t offset,size; } shared_copy_backing;
static VkDescriptorSetLayout image_retile_descriptors;
static VkPipelineLayout image_retile_layout;
static VkPipeline image_retile_pipeline;
static void image_retile_create(void) {
  if(image_retile_pipeline)return;
  VkDescriptorSetLayoutBinding b[2]={
    {0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL},
    {1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL}};
  VkDescriptorSetLayoutCreateInfo di={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=2,.pBindings=b};
  VK_CHECK(vkCreateDescriptorSetLayout(V.device,&di,NULL,&image_retile_descriptors));
  VkPushConstantRange push={VK_SHADER_STAGE_COMPUTE_BIT,0,44};
  VkPipelineLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount=1,.pSetLayouts=&image_retile_descriptors,.pushConstantRangeCount=1,.pPushConstantRanges=&push};
  VK_CHECK(vkCreatePipelineLayout(V.device,&li,NULL,&image_retile_layout));
  VkShaderModuleCreateInfo mi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=sizeof dm_image_retile_spv,.pCode=dm_image_retile_spv};
  VkShaderModule module;VK_CHECK(vkCreateShaderModule(V.device,&mi,NULL,&module));
  VkComputePipelineCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"},.layout=image_retile_layout};
  VK_CHECK(vkCreateComputePipelines(V.device,V.pipeline_cache,1,&pi,NULL,&image_retile_pipeline));
  vkDestroyShaderModule(V.device,module,NULL);
}
// Before borrowing any physical image/temporary/descriptor. Whole-pool shared
// storage cannot relocate; this exact original lookup retains its lifetime and
// records ordinary buffer dependencies, including prior raw readers.
static shared_copy_backing image_retile_prepare(const tex_t *t) {
  shared_copy_backing p={0};
  if(!vk_pool_shared_active() || !t || !t->has_layout || nvn_native_layout_is_marker(t->layout) || !t->mem || !t->L.size ||
      t->d.samples>1 || (t->d.flags&NVN_TEXTURE_FLAGS_LINEAR) ||
      (t->d.target!=NVN_TEXTURE_TARGET_2D && t->d.target!=NVN_TEXTURE_TARGET_2D_ARRAY &&
       t->d.target!=NVN_TEXTURE_TARGET_CUBEMAP && t->d.target!=NVN_TEXTURE_TARGET_CUBEMAP_ARRAY))return p;
  int bw,bh,bpb=nvn_fmt_bpb(t->d.format,&bw,&bh);
  if(bpb<=0 || bpb%4 || aspect_for(t->d.format)!=VK_IMAGE_ASPECT_COLOR_BIT)return p;
  for(image *i=image_buckets[t->serial%8192];i;i=i->hash_next)if(i->tex.serial==t->serial)
    if(i->external_access || (i->initialized&&!i->cpu_rebuildable))return p;
  if(vk_texture_shared_linear_conflict((uintptr_t)t->mem,t->L.size))return p;
  vk_buffer *b;uint64_t offset;
  if(!vk_pool_address_stage((uintptr_t)t->mem,t->L.size,(void **)&b,&offset,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT))
    die("Shared image retile target outside live Pool");
  uint64_t align=V.props.limits.minStorageBufferOffsetAlignment;
  if(!align || (align&(align-1)) || (offset&3))return p;
  uint64_t base=offset&~(align-1),range=offset-base+t->L.size;
  if(range>UINT32_MAX || range>V.props.limits.maxStorageBufferRange || range>b->size-base)return p;
  return (shared_copy_backing){b->handle,offset,b->size};
}
static int image_retile_eligible(image *i,shared_copy_backing p,const VkBufferImageCopy *c) {
  if(!p.handle || nvn_native_layout_is_marker(i->tex.layout) || !i->cpu_rebuildable || i->external_access || i->decoded ||
      i->format!=vk_nvn_format(i->tex.d.format) || c->imageOffset.z || c->imageExtent.depth!=1)return 0;
  int bw,bh,bpb=nvn_fmt_bpb(i->tex.d.format,&bw,&bh);
  uint32_t level=c->imageSubresource.mipLevel;
  if(bpb<=0 || bpb%4 || !c->imageExtent.width || !c->imageExtent.height ||
      c->imageOffset.x<0 || c->imageOffset.y<0 || c->imageOffset.x%bw || c->imageOffset.y%bh ||
      level>=i->levels || (uint64_t)c->imageOffset.x+c->imageExtent.width>mip(i->extent.width,level) ||
      (uint64_t)c->imageOffset.y+c->imageExtent.height>mip(i->extent.height,level) ||
      !c->imageSubresource.layerCount || c->imageSubresource.baseArrayLayer>=i->layers ||
      c->imageSubresource.layerCount>i->layers-c->imageSubresource.baseArrayLayer)return 0;
  uint64_t pitch=((uint64_t)(c->bufferRowLength?c->bufferRowLength:c->imageExtent.width)+bw-1)/bw*bpb;
  uint64_t rows=((uint64_t)c->imageExtent.height+bh-1)/bh;
  uint64_t image_rows=((uint64_t)(c->bufferImageHeight?c->bufferImageHeight:c->imageExtent.height)+bh-1)/bh;
  uint64_t cap=V.props.limits.maxStorageBufferRange,align=V.props.limits.minStorageBufferOffsetAlignment;
  if(!pitch || !align || align>cap || pitch>cap || image_rows>cap/pitch)return 0;
  uint64_t slice=pitch*image_rows;
  if(c->imageSubresource.layerCount>cap/slice)return 0;
  uint64_t footprint=(c->imageSubresource.layerCount-1)*slice+(rows-1)*pitch+
      ((uint64_t)c->imageExtent.width+bw-1)/bw*bpb;
  if(footprint>cap-(align-1))return 0;
  return 1;
}
static void image_retile_written(uint64_t address,uint64_t bytes,nvn_dirty_scope_publish *token) {
  nvn_dirty_scope_gpu_write_counted((void *)(uintptr_t)address,bytes,token);
  vk_buffer_shader_written_pretracked(address,bytes,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  vk_buffer_writeback_stage(address,bytes,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}
// Caller retains the original image copy. Source contains its exact GPU blocks
// at that command position, including GPU-produced sources. A private gather
// prevents source/destination aliases from changing the bytes being retiled.
static void image_retile_publish(image *i,shared_copy_backing target,vk_buffer *source,
    uint64_t source_offset,uint64_t pitch,uint64_t slice,const VkBufferImageCopy *c) {
  const tex_t *t=&i->tex;int bw,bh,bpb=nvn_fmt_bpb(t->d.format,&bw,&bh);
  uint32_t words=(uint32_t)(((uint64_t)c->imageExtent.width+bw-1)/bw*bpb/4);
  uint32_t rows=(c->imageExtent.height+bh-1)/bh;
  uint64_t bytes=(uint64_t)words*4,align=V.props.limits.minStorageBufferOffsetAlignment;
  if(pitch%4 || pitch/4>UINT32_MAX || slice%4 || source_offset%4 || source_offset>source->size ||
      (c->imageSubresource.layerCount>1 && slice>(source->size-source_offset)/(c->imageSubresource.layerCount-1)) ||
      (uint64_t)words*rows>UINT32_MAX ||
      ((uint64_t)words*rows+63)/64>V.props.limits.maxComputeWorkGroupCount[0])die("Shared image retile dispatch bounds");
  // The original B2T still reads its original buffer. Its raw range may
  // alias this destination: finish that transfer read before retile writes.
  // The private gather protects the retile's source bytes independently.
  VkBufferMemoryBarrier prior_read={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT,.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
    .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
    .buffer=target.handle,.offset=target.offset,.size=t->L.size};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      0,0,NULL,1,&prior_read,0,NULL);
  image_retile_create();
  nvn_dirty_scope_publish publication;
  // No render/alias callback remains. Retain the old stamp: begin rejects any
  // CPU change, NONE/BUSY page or lifecycle change before our own markers.
  nvn_dirty_scope_publish_begin(i->shared_dirty_scope,t->mem,t->L.size,i->stamp,&publication);
  for(uint32_t z=0;z<c->imageSubresource.layerCount;z++) {
    uint32_t rb,rs,slices,block;uint64_t origin;unsigned th,td;
    if(nvn_deswizzle_geom(&t->d,c->imageSubresource.mipLevel,c->imageSubresource.baseArrayLayer+z,
        &rb,&rs,&slices,&origin,&th,&td,&block) || slices!=1 || th>5 || td>5)
      die("Shared image retile geometry");
    uint64_t so=source_offset+z*slice,sb=so&~(align-1),sr=so-sb+(rows-1)*pitch+bytes;
    uint64_t db=target.offset&~(align-1),dr=target.offset-db+t->L.size;
    if(so>source->size || sr>source->size-sb || sr>V.props.limits.maxStorageBufferRange ||
        sr>UINT32_MAX || origin>t->L.size || (origin&3))die("Shared image retile source range");
    VkDescriptorSet set=vk_descriptor_allocate(image_retile_descriptors);
    VkDescriptorBufferInfo buffers[2]={{source->handle,sb,sr},{target.handle,db,dr}};
    VkWriteDescriptorSet writes[2];
    for(unsigned k=0;k<2;k++)writes[k]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet=set,.dstBinding=k,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&buffers[k]};
    vkUpdateDescriptorSets(V.device,2,writes,0,NULL);
    uint32_t push[]={(uint32_t)((so-sb)/4),(uint32_t)((target.offset-db+origin)/4),words,rows,(uint32_t)(pitch/4),
      rb,rs,(uint32_t)c->imageOffset.x/bw*bpb,(uint32_t)c->imageOffset.y/bh,th,td};
    vkCmdBindPipeline(V.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,image_retile_pipeline);
    vkCmdBindDescriptorSets(V.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,image_retile_layout,0,1,&set,0,NULL);
    vkCmdPushConstants(V.cmd,image_retile_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof push,push);
    vkCmdDispatch(V.cmd,(words*rows+63)/64,1,1);
    // Publish only the words actually written. Whole-image Invalidate could
    // discard a legitimate CPU edit in an untouched mip/ROI. The original
    // cache API rounds these exact intervals only to nonCoherentAtomSize.
    uint32_t x0=(uint32_t)c->imageOffset.x/bw*bpb,y0=(uint32_t)c->imageOffset.y/bh;
    uint32_t xend=x0+words*4,yend=y0+rows,bh_rows=8u<<th;
    uint64_t run_begin=0,run_end=0;
    // Visit GOBs/16B lanes in physical order. Coalesce only contiguous ACTUAL
    // words; unlike a whole mip envelope this never invalidates a hole/other
    // mip. No temporary interval array, sorting or persistent cache is needed.
    for(uint32_t by=y0/bh_rows;by<=(yend-1)/bh_rows;by++)
      for(uint32_t bx=x0/64;bx<=(xend-1)/64;bx++)
        for(uint32_t gy=0;gy<(1u<<th);gy++) {
          uint32_t gob_y=by*bh_rows+gy*8;
          if(gob_y>=yend || gob_y+8<=y0)continue;
          for(uint32_t tid=0;tid<32;tid++) {
            uint32_t y=gob_y+((tid>>2)&3)*2+(tid&1);
            uint32_t x=bx*64+(((tid>>4)<<1)|((tid>>1)&1))*16;
            if(y<y0 || y>=yend || x>=xend || x+16<=x0)continue;
            uint32_t lo=x<x0?x0:x,hi=x+16>xend?xend:x+16;
            uint64_t at=origin+nvn_bl_offset(lo,y,0,rb,rs,th,td),n=hi-lo;
            if(at>t->L.size || n>t->L.size-at)die("Shared image retile write bounds");
            if(run_end && at!=run_end) {
              image_retile_written((uintptr_t)t->mem+run_begin,run_end-run_begin,&publication);run_end=0;
            }
            if(!run_end)run_begin=at;
            run_end=at+n;
          }
        }
    if(run_end)image_retile_written((uintptr_t)t->mem+run_begin,run_end-run_begin,&publication);
  }
  // Keep the original ordered dirty publication, CPU fence/Invalidate, and
  // later raw GPU dependencies. Only a token containing exclusively our own
  // publications may acknowledge its captured generation. Any CPU/alias write
  // keeps the old stamp, so original preflight waits/publication/reupload run.
  uint64_t own_stamp;
  if(nvn_dirty_scope_publish_finish(&publication,&own_stamp))i->stamp=own_stamp;
  i->cpu_rebuildable=1;image_shared_alias_remove(i);
}
#endif
