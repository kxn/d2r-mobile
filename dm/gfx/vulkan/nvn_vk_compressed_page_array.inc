// Optional private BC3 page-array representation. Unlike legacy propagation,
// every supported consumer can first restore the original decoded RGBA image.
// Logical descriptors/Pool bytes never change. No compressed-BC re-encoding.
static int image_page_source_format(VkFormat f) {
  if(f==VK_FORMAT_ASTC_4x4_UNORM_BLOCK || f==VK_FORMAT_ASTC_4x4_SRGB_BLOCK)return 4;
  if(f==VK_FORMAT_ASTC_6x6_UNORM_BLOCK || f==VK_FORMAT_ASTC_6x6_SRGB_BLOCK)return 6;
  return 0;
}
static int image_page_matching_format(const image *d,VkFormat f) {
  return (d->tex.d.format==0x44 && (f==VK_FORMAT_ASTC_4x4_UNORM_BLOCK || f==VK_FORMAT_ASTC_6x6_UNORM_BLOCK)) ||
      (d->tex.d.format==0x48 && (f==VK_FORMAT_ASTC_4x4_SRGB_BLOCK || f==VK_FORMAT_ASTC_6x6_SRGB_BLOCK));
}
static int image_page_format_supported(const image *i,VkFormat f,VkFormatFeatureFlags features) {
  VkFormatProperties props;vk_format_properties(f,&props);
  if((props.optimalTilingFeatures&features)!=features)return 0;
  VkImageFormatProperties supported;
  return vkGetPhysicalDeviceImageFormatProperties(V.physical,f,i->description.imageType,
      i->description.tiling,i->description.usage,i->description.flags,&supported)==VK_SUCCESS &&
      i->extent.width<=supported.maxExtent.width && i->extent.height<=supported.maxExtent.height &&
      i->extent.depth<=supported.maxExtent.depth && i->levels<=supported.maxMipLevels &&
      i->layers<=supported.maxArrayLayers && (supported.sampleCounts&i->description.samples);
}
static int image_page_fresh(const image *d) {
  if(!compressed_page_arrays || d->compressed_page_array || !d->decoded ||
      d->tex.d.target!=NVN_TEXTURE_TARGET_2D_ARRAY || d->layers<2 ||
      (!image_page_matching_format(d,VK_FORMAT_ASTC_4x4_UNORM_BLOCK) &&
       !image_page_matching_format(d,VK_FORMAT_ASTC_4x4_SRGB_BLOCK)))return 0;
#ifdef DM_VULKAN_SHARED_POOLS
  if(vk_pool_shared_active() || vk_legacy_astc_copy_mode())return 0;
#endif
#ifdef DM_VULKAN_NATIVE_RESOURCES
  if(d->native_entry)return 0;
#endif
  // Unknown CPU versions never acquire a negative dirty proof. This observes
  // metadata, not guest contents, and is admission-only (not per sampled draw).
  return nvn_dirty_tracked(d->tex.mem,d->tex.L.size) && astc_copy_fresh(d);
}
static int image_page_region(const image *s,const image *d,const p_copy_t2t *p) {
  unsigned block=image_page_source_format(s->format);
  if(!block || s->description.samples!=VK_SAMPLE_COUNT_1_BIT || p->flags&~NVN_COPY_FLAGS_ENGINE_2D_BIT ||
      (p->has_sv && p->sv.set_format && p->sv.format!=s->tex.d.format) ||
      (p->has_dv && p->dv.set_format && p->dv.format!=d->tex.d.format))return 0;
  if((p->has_sv && p->sv.set_target && p->sv.target!=NVN_TEXTURE_TARGET_2D && p->sv.target!=NVN_TEXTURE_TARGET_2D_ARRAY) ||
      (p->has_dv && p->dv.set_target && p->dv.target!=NVN_TEXTURE_TARGET_2D && p->dv.target!=NVN_TEXTURE_TARGET_2D_ARRAY))return 0;
  unsigned level[2]={p->has_sv&&p->sv.set_levels?p->sv.base_level:0,p->has_dv&&p->dv.set_levels?p->dv.base_level:0};
  unsigned layer[2]={p->has_sv&&p->sv.set_layers?p->sv.min_layer:0,p->has_dv&&p->dv.set_layers?p->dv.min_layer:0};
  const image *im[2]={s,d};const NVNcopyRegion *r[2]={&p->sr,&p->dr};
  if(p->sr.width!=p->dr.width || p->sr.height!=p->dr.height || p->sr.depth!=p->dr.depth)return 0;
  for(unsigned n=0;n<2;n++) {
    if(level[n]>=im[n]->levels || im[n]->description.imageType!=VK_IMAGE_TYPE_2D ||
        r[n]->xoffset<0 || r[n]->yoffset<0 || r[n]->zoffset<0 ||
        r[n]->width<=0 || r[n]->height<=0 || r[n]->depth<=0)return 0;
    uint32_t w=mip(im[n]->extent.width,level[n]),h=mip(im[n]->extent.height,level[n]);
    if((uint64_t)r[n]->xoffset+r[n]->width>w || (uint64_t)r[n]->yoffset+r[n]->height>h ||
        (uint64_t)layer[n]+r[n]->zoffset+r[n]->depth>im[n]->layers ||
        r[n]->xoffset%block || r[n]->yoffset%block ||
        (r[n]->width%block && (uint64_t)r[n]->xoffset+r[n]->width!=w) ||
        (r[n]->height%block && (uint64_t)r[n]->yoffset+r[n]->height!=h))return 0;
  }
  return 1;
}
// replacement already owns a fresh allocation; old storage/requests/views are
// retained until real completion. Logical list/hash/alias nodes stay in i.
static image *image_page_replace(image *i,image *replacement,image *old) {
  *old=*i;image_replacement_detach(i,old);
#ifdef DM_VULKAN_SHARED_POOLS
  old->shared_alias_linked=0;old->shared_alias_next=NULL;old->shared_alias_range=(alias_range_node){0};
#endif
  image_recipes_changed();image_binding_replaced(i,old);
  // Keep list topology/identity, but take all newly allocated physical state.
  *i=*replacement;i->allocation_bytes=replacement->allocation_bytes;
  return old;
}
static void image_page_demote(image *i) {
  if(i->compressed_page_array!=1)return;
  i->last_use=vk_recording_serial();
  vk_mip_clear_drain();vk_render_end();
  if(i->compressed_page_array!=1)return; // callback already restored it
  image_general_transfer(i);vk_transfer_barrier();
  image *old=malloc(sizeof *old);if(!old)die("Page-array retirement allocation");
  image replacement=*i;
  replacement.views=NULL;replacement.access=NULL;replacement.allocation=NULL;
  replacement.allocation_bytes=0;replacement.accounted_rebuildable=0;
  replacement.cpu_rebuildable=0;replacement.zero_mips=0;replacement.astc_ldr_proven=0;
  replacement.format=decoded_format(i->tex.d.format);replacement.description.format=replacement.format;
  replacement.decoded=1;replacement.bpp=4;replacement.compressed_page_array=2;
  image_allocate(&replacement);
  image_page_replace(i,&replacement,old);
  for(unsigned level=0;level+1<i->levels;level++) {
    VkImageSubresourceLayers sub={i->aspect,level,0,i->layers};
    int width=mip(i->extent.width,level),height=mip(i->extent.height,level);
    VkImageBlit blit={.srcSubresource=sub,.dstSubresource=sub,
      .srcOffsets={{0,0,0},{width,height,1}},.dstOffsets={{0,0,0},{width,height,1}}};
    image_transfer_pair(old,sub,i,sub);
    vkCmdBlitImage(V.cmd,old->handle,VK_IMAGE_LAYOUT_GENERAL,i->handle,VK_IMAGE_LAYOUT_GENERAL,1,&blit,VK_FILTER_NEAREST);
  }
  // Do not fabricate CPU rebuildability: only a complete original CPU upload
  // can grant it. Undefined old cells stay undefined, including on restoration.
  i->initialized=old->initialized;
  vk_completion(image_storage_destroy,old);
}
static void image_page_cpu_check(image *i) {
  if(i->compressed_page_array!=1)return;
  uint64_t epoch=nvn_dirty_change_epoch();
  if(epoch && i->checked_dirty_epoch==epoch && i->checked_dirty_stamp==i->compressed_page_stamp)return;
  if(nvn_dirty_since(i->tex.mem,i->tex.L.size,i->compressed_page_stamp)) {image_page_demote(i);return;}
  i->checked_dirty_epoch=epoch;i->checked_dirty_stamp=i->compressed_page_stamp;
}
static void image_page_copy_prepare(image *s,image *d,const p_copy_t2t *p) {
  if(!compressed_page_arrays)return;
  int compatible=s!=d && image_page_matching_format(d,s->format) && image_page_region(s,d,p) &&
      (s->astc_ldr_proven || s->compressed_page_array==1);
  if(d->compressed_page_array==1) {
    if(!compatible || s->format!=d->format)image_page_demote(d);
    return;
  }
  if(!compatible || !s->astc_ldr_proven || !image_page_fresh(d))return;
  // A reversible representation is admitted only with both compression and
  // exact full-array restoration supported for these real formats/usages.
  VkFormatFeatureFlags common=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|
      VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
  if(!image_page_format_supported(d,s->format,common|VK_FORMAT_FEATURE_BLIT_SRC_BIT) ||
      !image_page_format_supported(d,d->format,common|VK_FORMAT_FEATURE_BLIT_DST_BIT))return;
  d->last_use=vk_recording_serial();s->last_use=vk_recording_serial();
  vk_mip_clear_drain();vk_render_end();
  if(!s->astc_ldr_proven || !image_page_fresh(d) || !image_page_matching_format(d,s->format) || !image_page_region(s,d,p))return;
  image *old=malloc(sizeof *old);if(!old)return;
  image replacement=*d;
  replacement.views=NULL;replacement.access=NULL;replacement.allocation=NULL;
  replacement.allocation_bytes=0;replacement.accounted_rebuildable=0;replacement.cpu_rebuildable=0;
  replacement.format=s->format;replacement.description.format=s->format;replacement.decoded=0;replacement.bpp=16;
  replacement.compressed_page_array=1;replacement.astc_ldr_proven=0;replacement.zero_mips=0;
  if(image_try_allocate(&replacement)!=VK_SUCCESS) {free(old);return;}
  replacement.compressed_page_stamp=nvn_dirty_now();
  // Protection is the existing page mechanism; subsequent changes only revoke
  // the representation. No logical content or upload stamp is advanced.
  nvn_dirty_protect(d->tex.mem,d->tex.L.size);
  replacement.checked_dirty_epoch=replacement.checked_dirty_stamp=0;
  replacement.checked_dirty_range=(nvn_dirty_range_proof){0};
  image_page_replace(d,&replacement,old);vk_completion(image_storage_destroy,old);
}
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
// CPU-host oracle exercises restoration/lifetime with actual BC3 hardware
// storage. It does NOT grant ASTC capability or test compressed admission.
void vk_test_page_array_bc_restoration(const tex_t *t) {
  image *i=astc_copy_find(t);
  if(!i || i->decoded || i->format!=vk_nvn_format(t->d.format) ||
      !i->initialized || !V.features.textureCompressionBC ||
      !image_page_format_supported(i,i->format,VK_FORMAT_FEATURE_BLIT_SRC_BIT))
    die("Owned BC restoration setup");
  i->compressed_page_array=1;i->compressed_page_stamp=nvn_dirty_now();
  nvn_dirty_protect(t->mem,t->L.size);
  i->checked_dirty_epoch=i->checked_dirty_stamp=0;
}
int vk_test_page_array_state(const tex_t *t) {
  image *i=astc_copy_find(t);return i?i->compressed_page_array:-1;
}
void vk_test_page_array_restore(const tex_t *t) {
  image *i=astc_copy_find(t);if(!i)die("Owned page restoration owner");image_page_demote(i);
}
#endif

#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
unsigned vk_test_page_array_policy(const tex_t *t) {
  unsigned checks=0;
#define PAGE_POLICY(x) do {checks++;if(!(x))die("Page-array policy: %s",#x);}while(0)
  image_access cells[27]={0};
  image d={.tex=*t,.format=decoded_format(t->d.format),.decoded=1,.access=cells,
    .extent={16,16,1},.levels=3,.layers=3,.aspect=VK_IMAGE_ASPECT_COLOR_BIT,
    .description={.imageType=VK_IMAGE_TYPE_2D,.samples=VK_SAMPLE_COUNT_1_BIT,
      .usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT}};
  PAGE_POLICY(image_page_fresh(&d));
  d.initialized=1;PAGE_POLICY(!image_page_fresh(&d));d.initialized=0;
  d.access[26].writes=VK_ACCESS_TRANSFER_WRITE_BIT;PAGE_POLICY(!image_page_fresh(&d));d.access[26].writes=0;
  d.access[1].write_clock=1;PAGE_POLICY(!image_page_fresh(&d));d.access[1].write_clock=0;
  d.compressed_page_array=2;PAGE_POLICY(!image_page_fresh(&d));d.compressed_page_array=0;
  d.tex.d.format=0x49;PAGE_POLICY(!image_page_fresh(&d));d.tex.d.format=t->d.format;
  PAGE_POLICY(image_page_matching_format(&d,t->d.format==0x48?VK_FORMAT_ASTC_6x6_SRGB_BLOCK:VK_FORMAT_ASTC_6x6_UNORM_BLOCK));
  PAGE_POLICY(!image_page_matching_format(&d,t->d.format==0x48?VK_FORMAT_ASTC_6x6_UNORM_BLOCK:VK_FORMAT_ASTC_6x6_SRGB_BLOCK));
  image s=d;s.format=VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
  p_copy_t2t p={.sr={4,4,0,8,8,1},.dr={4,4,1,8,8,1}};
  PAGE_POLICY(image_page_region(&s,&d,&p));
  p.dr.xoffset=5;PAGE_POLICY(!image_page_region(&s,&d,&p));p.dr.xoffset=4;
  p.flags=NVN_COPY_FLAGS_LINEAR_FILTER_BIT;PAGE_POLICY(!image_page_region(&s,&d,&p));p.flags=0;
  p.dr.width=4;PAGE_POLICY(!image_page_region(&s,&d,&p));p.dr.width=8;
  p.dr.zoffset=3;PAGE_POLICY(!image_page_region(&s,&d,&p));p.dr.zoffset=1;
  p.has_dv=1;p.dv.set_levels=1;p.dv.base_level=3;PAGE_POLICY(!image_page_region(&s,&d,&p));p.has_dv=0;
  s.format=VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
  p.sr=(NVNcopyRegion){6,6,0,10,10,1};p.dr=(NVNcopyRegion){6,6,1,10,10,1};
  PAGE_POLICY(image_page_region(&s,&d,&p)); // legal non-multiple block edge
  p.sr.xoffset=p.dr.xoffset=4;PAGE_POLICY(!image_page_region(&s,&d,&p));
  p.sr=(NVNcopyRegion){0,0,0,12,12,1};p.dr=(NVNcopyRegion){0,0,1,12,12,1};PAGE_POLICY(image_page_region(&s,&d,&p));
  p.sr.width=p.dr.width=10;PAGE_POLICY(!image_page_region(&s,&d,&p)); // not a mip edge
#undef PAGE_POLICY
  return checks;
}
#endif
