// Pack independent mip/layer uploads into bounded original staging buffers.
// Prepare ALL storage before shared CPU observation; never allocate/record
// Vulkan work while the observation is held. Native images never reach here.
enum { IMAGE_UPLOAD_REGIONS=64 };
#define IMAGE_UPLOAD_BYTES (8ull<<20)
typedef struct {
  vk_buffer *source;
  VkBufferImageCopy copy;
  size_t pitch,rows,raw_bytes;
} image_cpu_piece;
typedef struct {
  image_cpu_piece *pieces;
  size_t count;
  uint8_t *scratch;
} image_upload_plan;
static image_upload_plan image_upload_plan_create(image *i) {
  const tex_t *t=&i->tex;
  if(!i->levels || !i->layers || i->layers>SIZE_MAX/i->levels)
    die("Vulkan upload staging plan capacity");
  image_upload_plan p={.count=(size_t)i->layers*i->levels};
  if(p.count>SIZE_MAX/sizeof *p.pieces)die("Vulkan upload staging plan capacity");
  p.pieces=calloc(p.count,sizeof *p.pieces);
  if(!p.pieces)die("Vulkan upload staging plan allocation");
  int bw,bh;uint32_t bpb=nvn_fmt_bpb(t->d.format,&bw,&bh);
  if(!bpb || bw<=0 || bh<=0 || (i->decoded && !i->bpp))die("Vulkan upload block geometry");
  // Physical output, not the logical compressed format, governs offsets.
  uint64_t unit=i->decoded?i->bpp:bpb,alignment=unit;
  while(alignment%4)alignment+=unit;
  size_t maximum_raw=0;
  for(size_t first=0;first<p.count;) {
    unsigned n=0;uint64_t total=0;
    while(first+n<p.count && n<IMAGE_UPLOAD_REGIONS) {
      image_cpu_piece *piece=&p.pieces[first+n];
      uint32_t level=(first+n)%i->levels,layer=(first+n)/i->levels;
      uint32_t w=mip(i->extent.width,level),h=mip(i->extent.height,level),z=mip(i->extent.depth,level);
      uint64_t pitch=((uint64_t)w+bw-1)/bw*bpb,rows=((uint64_t)h+bh-1)/bh;
      if(!pitch || !rows || pitch>SIZE_MAX || rows>SIZE_MAX/pitch || z>SIZE_MAX/(pitch*rows))
        die("Vulkan upload staging size overflow");
      uint64_t raw=pitch*rows*z,bytes=raw;
      if(i->decoded) {
        if(!w || !h || !z || (uint64_t)w*h>SIZE_MAX/z || (uint64_t)w*h*z>SIZE_MAX/i->bpp)
          die("Vulkan decoded upload size overflow");
        bytes=(uint64_t)w*h*z*i->bpp;
      }
      if(!bytes || total>SIZE_MAX-(alignment-1))die("Vulkan upload offset overflow");
      uint64_t offset=(total+alignment-1)/alignment*alignment;
      if(bytes>SIZE_MAX-offset)die("Vulkan upload size overflow");
      // One oversized piece keeps its original single-buffer path/size.
      if(n && offset+bytes>IMAGE_UPLOAD_BYTES)break;
      piece->pitch=pitch;piece->rows=rows;piece->raw_bytes=raw;
      piece->copy=(VkBufferImageCopy){.bufferOffset=offset,
        .imageSubresource={i->aspect,level,layer,1},.imageExtent={w,h,z}};
      if(raw>maximum_raw)maximum_raw=raw;
      total=offset+bytes;n++;
    }
    vk_buffer *source=vk_temp_buffer(total);
    for(unsigned k=0;k<n;k++)p.pieces[first+k].source=source;
    first+=n;
  }
  p.scratch=i->decoded?malloc(maximum_raw):NULL;
  if(i->decoded && !p.scratch)die("Vulkan BC scratch allocation");
  return p;
}
static int image_upload_plan_convert(image *i,const image_upload_plan *p,int ldr) {
  const tex_t *t=&i->tex;
  for(size_t n=0;n<p->count;n++) {
    const image_cpu_piece *piece=&p->pieces[n];
    uint32_t level=piece->copy.imageSubresource.mipLevel,layer=piece->copy.imageSubresource.baseArrayLayer,
      w=piece->copy.imageExtent.width,h=piece->copy.imageExtent.height,z=piece->copy.imageExtent.depth;
    uint8_t *output=piece->source->map+piece->copy.bufferOffset,*raw=i->decoded?p->scratch:output;
    if(t->d.flags&NVN_TEXTURE_FLAGS_LINEAR) {
      if(level || layer || z!=1)die("Vulkan linear layered/mipped upload unsupported");
      size_t stride=t->d.stride?t->d.stride:(piece->pitch+31)&~(size_t)31;
      for(size_t y=0;y<piece->rows;y++)memcpy(raw+y*piece->pitch,t->mem+y*stride,piece->pitch);
    } else if(nvn_deswizzle(&t->d,level,layer,t->mem,raw,piece->pitch))die("Vulkan deswizzle failed");
    int bw,bh;nvn_fmt_bpb(t->d.format,&bw,&bh);
    if(ldr && !nvn_astc_mip_is_ldr(raw,piece->raw_bytes,bw,bh))ldr=0;
    if(i->decoded)for(uint32_t slice=0;slice<z;slice++)
      if(nvn_bc_decode(t->d.format,raw+slice*piece->pitch*piece->rows,piece->pitch,w,h,
          output+(uint64_t)slice*w*h*i->bpp,(size_t)w*i->bpp))die("Vulkan BC decode failed");
  }
  return ldr;
}
static void image_upload_plan_record(image *i,const image_upload_plan *p) {
  for(size_t first=0;first<p->count;) {
    VkBufferImageCopy copies[IMAGE_UPLOAD_REGIONS];unsigned n=0;
    vk_buffer *source=p->pieces[first].source;
    while(first+n<p->count && n<IMAGE_UPLOAD_REGIONS && p->pieces[first+n].source==source) {
      copies[n]=p->pieces[first+n].copy;
      // Preserve every subresource's actual hazard and GPU-use ledger.
      image_transfer(i,image_layer_range(copies[n].imageSubresource),1);n++;
    }
    vkCmdCopyBufferToImage(V.cmd,source->handle,i->handle,VK_IMAGE_LAYOUT_GENERAL,n,copies);
    first+=n;
  }
}
static void image_upload_plan_free(image_upload_plan *p) {free(p->scratch);free(p->pieces);}
