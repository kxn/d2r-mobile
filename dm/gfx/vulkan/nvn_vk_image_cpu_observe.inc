#ifdef DM_VULKAN_SHARED_POOLS
// Shared sources are not snapshotted into an extra whole-Pool mirror. Prepare
// exactly the original upload staging, then keep the short CPU observation
// phase across its deswizzle/decode. No renderer/driver calls while it is held.
typedef struct {
  vk_buffer *source;
  VkBufferImageCopy copy;
  size_t pitch,rows,raw_bytes;
} image_cpu_piece;
static void image_upload_observed_contents(image *i) {
  const tex_t *t=&i->tex;
  if(t->d.samples>1 || i->aspect!=VK_IMAGE_ASPECT_COLOR_BIT)
    die("Vulkan packaged depth/MS image unsupported");
  i->checked_dirty_epoch=0;i->checked_dirty_range=(nvn_dirty_range_proof){0};
  // Render/pass replay is finished before the CPU phase. Later GPU commands
  // use the original physical image and staging lifetimes after phase end.
  image_general_transfer(i);
  size_t count=(size_t)i->layers*i->levels;
  if(!count || count>SIZE_MAX/sizeof(image_cpu_piece))die("Shared upload staging plan capacity");
  image_cpu_piece *pieces=calloc(count,sizeof *pieces);
  if(!pieces)die("Shared upload staging plan allocation");
  size_t maximum_raw=0;
  for(uint32_t layer=0;layer<i->layers;layer++)for(uint32_t level=0;level<i->levels;level++) {
    image_cpu_piece *piece=&pieces[(size_t)layer*i->levels+level];
    uint32_t w=mip(i->extent.width,level),h=mip(i->extent.height,level),z=mip(i->extent.depth,level);
    int bw,bh;uint32_t bpb=nvn_fmt_bpb(t->d.format,&bw,&bh);
    if(!bpb || bw<=0 || bh<=0)die("Shared upload block geometry");
    piece->pitch=((size_t)w+bw-1)/bw*bpb;piece->rows=((size_t)h+bh-1)/bh;
    if(piece->rows>SIZE_MAX/piece->pitch || z>SIZE_MAX/(piece->pitch*piece->rows))
      die("Shared upload staging size overflow");
    piece->raw_bytes=piece->pitch*piece->rows*z;
    if(piece->raw_bytes>maximum_raw)maximum_raw=piece->raw_bytes;
    uint64_t output=i->decoded?(uint64_t)w*h*z*i->bpp:piece->raw_bytes;
    piece->source=vk_temp_buffer(output);
    piece->copy=(VkBufferImageCopy){.imageSubresource={i->aspect,level,layer,1},.imageExtent={w,h,z}};
  }
  // Reuse one largest compressed CPU scratch for the BC fallback; native
  // compressed uploads read directly into their original GPU staging buffers.
  uint8_t *scratch=i->decoded?malloc(maximum_raw):NULL;
  if(i->decoded && !scratch)die("Shared BC scratch allocation");
  int ldr=V.astc_decode_mode && !i->external_access && !i->decoded && astc_unorm_format(i->format) &&
    i->description.imageType==VK_IMAGE_TYPE_2D && i->layers==1 && i->extent.depth==1;
  image_astc_proof(i,0);
  vk_shared_cpu_observation observation;
  if(!vk_pool_cpu_observe_begin((uintptr_t)t->mem,t->L.size,&observation))
    die("Shared CPU conversion missing page-level ordered preflight");
  uint64_t stamp=observation.stamp;
  for(size_t n=0;n<count;n++) {
    image_cpu_piece *piece=&pieces[n];uint32_t level=piece->copy.imageSubresource.mipLevel,
      layer=piece->copy.imageSubresource.baseArrayLayer,w=piece->copy.imageExtent.width,
      h=piece->copy.imageExtent.height,z=piece->copy.imageExtent.depth;
    uint8_t *raw=i->decoded?scratch:piece->source->map;
    if(t->d.flags&NVN_TEXTURE_FLAGS_LINEAR) {
      if(level || layer || z!=1)die("Vulkan linear layered/mipped upload unsupported");
      size_t stride=t->d.stride?t->d.stride:(piece->pitch+31)&~(size_t)31;
      for(size_t y=0;y<piece->rows;y++)memcpy(raw+y*piece->pitch,t->mem+y*stride,piece->pitch);
    } else if(nvn_deswizzle(&t->d,level,layer,t->mem,raw,piece->pitch))die("Vulkan deswizzle failed");
    int bw,bh;nvn_fmt_bpb(t->d.format,&bw,&bh);
    if(ldr && !nvn_astc_mip_is_ldr(raw,piece->raw_bytes,bw,bh))ldr=0;
    if(i->decoded)for(uint32_t slice=0;slice<z;slice++)
      if(nvn_bc_decode(t->d.format,raw+slice*piece->pitch*piece->rows,piece->pitch,w,h,
          piece->source->map+(uint64_t)slice*w*h*i->bpp,w*i->bpp))die("Vulkan BC decode failed");
  }
  // Pages are still RO. End releases waiting kernel/user writes; all of them
  // receive a generation AFTER captured stamp. Never protect again afterwards
  // or assign a later global-now stamp which could swallow those new writes.
  vk_pool_cpu_observe_end(&observation);free(scratch);
  for(size_t n=0;n<count;n++) {
    image_cpu_piece *piece=&pieces[n];
    image_transfer(i,image_layer_range(piece->copy.imageSubresource),1);
    vkCmdCopyBufferToImage(V.cmd,piece->source->handle,i->handle,VK_IMAGE_LAYOUT_GENERAL,1,&piece->copy);
  }
  free(pieces);
  if(i->initialized)reuploads++;else uploads++;
  i->stamp=stamp;i->initialized=1;image_rebuildable_set(i,!i->external_access);
  if(i->cpu_rebuildable)image_shared_alias_remove(i);
  image_astc_proof(i,ldr);
}
#endif
