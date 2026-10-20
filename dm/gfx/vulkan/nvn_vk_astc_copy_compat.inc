// Compatibility for the upstream offline ASTC resource pack. Its packaged
// pages are ASTC, while freshly allocated GPU page arrays remain logical BC3.
// This is NOT a generic NVN compressed-format conversion or reinterpretation.
// Keep the original logical descriptor and Pool bytes; only wholly undefined
// physical storage may adopt the source's actual encoding. Defined mixed BC3
// targets instead preserve all previous pixels in the decoded RGBA8 family.
typedef struct {
#ifdef DM_VULKAN_SHARED_POOLS
  nvn_dirty_scope *scope;
#endif
  uint64_t stamp;
  int legacy;
} astc_copy_observation;
static VkFormat astc_copy_format(int logical) {
  if(logical==0x44)return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
  if(logical==0x48)return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
  return VK_FORMAT_UNDEFINED;
}
static int astc_copy_target(const tex_t *t) {
  return t && (t->d.target==NVN_TEXTURE_TARGET_2D || t->d.target==NVN_TEXTURE_TARGET_2D_ARRAY) &&
      t->d.samples<=1 && !(t->d.flags&NVN_TEXTURE_FLAGS_IMAGE);
}
static image *astc_copy_find(const tex_t *t) {
  for(image *i=image_buckets[t->serial%8192];i;i=i->hash_next)
    if(i->tex.serial==t->serial)return i;
  return NULL;
}
static int image_astc_copy_unchanged(image *i) {
  if(!i->astc_copy_encoding && !i->bc_copy_decoded)return 1;
  uint64_t epoch=nvn_dirty_change_epoch();
  if(epoch && i->checked_dirty_epoch==epoch && i->checked_dirty_stamp==i->astc_copy_stamp)return 1;
  nvn_dirty_range_proof proof={0};
  if(epoch && nvn_dirty_range_refresh(&i->checked_dirty_range,&proof) &&
      i->checked_dirty_stamp==i->astc_copy_stamp && proof.changes==i->checked_dirty_range.changes) {
    i->checked_dirty_epoch=epoch;return 1;
  }
  nvn_dirty_range_observe(i->tex.mem,i->tex.L.size,&proof);
  if(nvn_dirty_since(i->tex.mem,i->tex.L.size,i->astc_copy_stamp))return 0;
  i->checked_dirty_epoch=epoch;i->checked_dirty_stamp=i->astc_copy_stamp;
  i->checked_dirty_range=proof;return 1;
}
static void image_astc_copy_check(image *i) {
  if(!image_astc_copy_unchanged(i))
    die("ASTC propagated image CPU/unknown Pool encoding changed: serial=%llu logical=%#x physical=%u",
        (unsigned long long)i->tex.serial,i->tex.d.format,i->format);
}
static VkFormat image_astc_copy_view_format(image *i,int nvn_format) {
  (void)i;
  VkFormat format=astc_copy_format(nvn_format);
  if(!format)die("ASTC propagated image cannot reinterpret logical format %#x",nvn_format);
  return format;
}
static void astc_copy_observation_release(astc_copy_observation *o) {
#ifdef DM_VULKAN_SHARED_POOLS
  if(o->scope)nvn_dirty_scope_release(o->scope);
#endif
  memset(o,0,sizeof *o);
}
static int astc_copy_legacy_envelope(const tex_t *t) {
  uint64_t page=(uint64_t)getpagesize();
  return t && t->mem && t->L.size && !((uintptr_t)t->mem&(page-1)) &&
      !(t->L.size&(page-1)) && nvn_dirty_tracked(t->mem,t->L.size);
}
// Preserve the old already-imported provenance requirement. In window mode
// an adjacent/crossing imported region must first become ONE exact span, not
// be treated as proof that every byte of this observation is already covered.
// A defined BC3 target cannot adopt ASTC block encoding. The only new
// representation here is the existing decoded RGBA8 family, preserving every
// physical mip/layer before the incoming copy. Raw BC byte escape stays refused.
static int astc_copy_mixed_target(const image *d) {
#ifdef DM_VULKAN_SHARED_POOLS
  return d && vk_pool_shared_active() && d->initialized && d->shared_alias_linked &&
      !d->tex.has_layout && !d->external_access && !d->cpu_rebuildable &&
      !d->astc_copy_encoding && !d->bc_copy_decoded && !d->decoded &&
      !d->shared_dirty_scope && astc_copy_target(&d->tex) &&
      d->format==vk_nvn_format(d->tex.d.format) && astc_copy_format(d->tex.d.format) &&
      d->aspect==VK_IMAGE_ASPECT_COLOR_BIT && d->description.samples==VK_SAMPLE_COUNT_1_BIT &&
      !(d->description.usage&~(VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT));
#else
  (void)d;return 0;
#endif
}
static int image_astc_copy_needs_span(const p_copy_t2t *p) {
#ifdef DM_VULKAN_SHARED_POOLS
  if(!vk_pool_shared_active() || !astc_copy_target(p->src) || !astc_copy_target(p->dst) ||
      !astc_copy_format(p->dst->d.format) || p->dst->has_layout || !p->dst->mem || !p->dst->L.size)return 0;
  image *s=astc_copy_find(p->src),*d=astc_copy_find(p->dst);
  VkFormat source=s?s->format:vk_nvn_format(p->src->d.format);
  if(source!=astc_copy_format(p->dst->d.format) || (d && (d->astc_copy_encoding || (d->initialized && !astc_copy_mixed_target(d)))))return 0;
  vk_pool_span_info span;
  int status=vk_pool_span_query((uintptr_t)p->dst->mem,p->dst->L.size,&span);
  return status==VK_POOL_SPAN_SINGLE || status==VK_POOL_SPAN_CROSSING;
#else
  (void)p;return 0;
#endif
}
static astc_copy_observation image_astc_copy_preflight(const p_copy_t2t *p) {
  astc_copy_observation o={0};
#ifdef DM_VULKAN_SHARED_POOLS
  if((!vk_pool_shared_active() && !vk_legacy_astc_copy_mode()) || !astc_copy_target(p->src) || !astc_copy_target(p->dst) ||
      !astc_copy_format(p->dst->d.format) || p->dst->has_layout || !p->dst->mem || !p->dst->L.size)
    return o;
  image *s=astc_copy_find(p->src),*d=astc_copy_find(p->dst);
  VkFormat source=s?s->format:vk_nvn_format(p->src->d.format);
  if(source!=astc_copy_format(p->dst->d.format) || (d && (d->astc_copy_encoding || (d->initialized && !astc_copy_mixed_target(d)))))return o;
  if(!vk_pool_shared_active()) {
    // A page-level dirty event must describe this entire logical destination,
    // never an unrelated neighbor. Do not invent CPU_NO_ACCESS provenance.
    if(!astc_copy_legacy_envelope(p->dst) ||
        !vk_pool_cpu_observe_prepare((uintptr_t)p->dst->mem,p->dst->L.size))return o;
    vk_shared_cpu_observation phase;
    if(!vk_pool_cpu_observe_begin((uintptr_t)p->dst->mem,p->dst->L.size,&phase))return o;
    o.stamp=phase.stamp;o.legacy=1;vk_pool_cpu_observe_end(&phase);return o;
  }
  // No new Pool materialization/pin is justified merely by this observation.
  // The existing startup provenance and already-imported identity are required.
  if(!shared_image_backing((uintptr_t)p->dst->mem,p->dst->L.size) ||
      !vk_pool_shared_materialized((uintptr_t)p->dst->mem,p->dst->L.size))return o;
  if(!vk_pool_shared_image_source((uintptr_t)p->dst->mem,p->dst->L.size,1))return o;
  o.scope=nvn_dirty_scope_acquire(p->dst->mem,p->dst->L.size);
  if(!o.scope)return o;
  if(!vk_pool_shared_observe_protect((uintptr_t)p->dst->mem,p->dst->L.size,&o.stamp))
    astc_copy_observation_release(&o);
#else
  (void)p;
#endif
  return o;
}
static int astc_copy_region(const image *s,const image *d,const p_copy_t2t *p) {
  if(p->flags&~NVN_COPY_FLAGS_ENGINE_2D_BIT ||
      (p->has_sv && p->sv.set_format && (s->astc_copy_encoding?astc_copy_format(p->sv.format):vk_nvn_format(p->sv.format))!=s->format) ||
      (p->has_dv && p->dv.set_format && p->dv.format!=d->tex.d.format))return 0;
  if((p->has_sv && p->sv.set_target && p->sv.target!=NVN_TEXTURE_TARGET_2D && p->sv.target!=NVN_TEXTURE_TARGET_2D_ARRAY) ||
      (p->has_dv && p->dv.set_target && p->dv.target!=NVN_TEXTURE_TARGET_2D && p->dv.target!=NVN_TEXTURE_TARGET_2D_ARRAY))return 0;
  // Pure bounds: range() also creates/promotes format views and is deliberately
  // not called during this pre-replacement eligibility proof.
  VkImageSubresourceRange sr={.baseMipLevel=p->has_sv&&p->sv.set_levels?p->sv.base_level:0,
    .baseArrayLayer=p->has_sv&&p->sv.set_layers?p->sv.min_layer:0},
    dr={.baseMipLevel=p->has_dv&&p->dv.set_levels?p->dv.base_level:0,
    .baseArrayLayer=p->has_dv&&p->dv.set_layers?p->dv.min_layer:0};
  if(sr.baseMipLevel>=s->levels || dr.baseMipLevel>=d->levels ||
      p->sr.width!=p->dr.width || p->sr.height!=p->dr.height || p->sr.depth!=p->dr.depth ||
      p->sr.width<=0 || p->sr.height<=0 || p->sr.depth<=0 ||
      p->sr.xoffset<0 || p->sr.yoffset<0 || p->sr.zoffset<0 ||
      p->dr.xoffset<0 || p->dr.yoffset<0 || p->dr.zoffset<0)return 0;
  const NVNcopyRegion *r[2]={&p->sr,&p->dr};const image *im[2]={s,d};
  const VkImageSubresourceRange *sub[2]={&sr,&dr};
  for(unsigned n=0;n<2;n++) {
    uint32_t w=mip(im[n]->extent.width,sub[n]->baseMipLevel),h=mip(im[n]->extent.height,sub[n]->baseMipLevel);
    if((uint64_t)r[n]->xoffset+r[n]->width>w || (uint64_t)r[n]->yoffset+r[n]->height>h ||
        (uint64_t)sub[n]->baseArrayLayer+r[n]->zoffset+r[n]->depth>im[n]->layers ||
        (r[n]->xoffset&3) || (r[n]->yoffset&3) ||
        ((r[n]->width&3) && (uint64_t)r[n]->xoffset+r[n]->width!=w) ||
        ((r[n]->height&3) && (uint64_t)r[n]->yoffset+r[n]->height!=h))return 0;
  }
  return 1;
}
static int astc_copy_fresh(const image *d) {
  if(d->initialized || d->tex.has_layout || d->external_access || d->astc_copy_encoding ||
      !astc_copy_target(&d->tex) || d->aspect!=VK_IMAGE_ASPECT_COLOR_BIT ||
      d->description.samples!=VK_SAMPLE_COUNT_1_BIT ||
      d->description.usage&~(VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT))return 0;
#ifdef DM_VULKAN_SHARED_POOLS
  if(d->shared_dirty_scope)return 0;
#endif
  for(size_t n=0;n<(size_t)d->levels*d->layers*3;n++)
    if(d->access[n].writers || d->access[n].write_clock || d->access[n].writes)return 0;
  // A new logical identity must not overwrite an already-defined alias. This
  // admission-only walk does not add a registry scan to ordinary sampled draws.
  uint64_t base=(uintptr_t)d->tex.mem,size=d->tex.L.size;
  if(!size || size>UINT64_MAX-base)return 0;
  for(const image *a=images;a;a=a->next)if(a!=d && (a->initialized || a->external_access
#ifdef DM_VULKAN_SHARED_POOLS
      || a->shared_alias_linked
#endif
      ) && a->tex.mem && a->tex.L.size) {
    uint64_t other=(uintptr_t)a->tex.mem;
    if(other<=base?base-other<a->tex.L.size:other-base<size)return 0;
  }
  return 1;
}
static int astc_copy_no_defined_alias(const image *d) {
  uint64_t base=(uintptr_t)d->tex.mem,size=d->tex.L.size;
  if(!base || !size || size>UINT64_MAX-base)return 0;
  for(const image *a=images;a;a=a->next)if(a!=d && a->tex.mem && a->tex.L.size &&
      (a->initialized || a->external_access
#ifdef DM_VULKAN_SHARED_POOLS
       || a->shared_alias_linked
#endif
      )) {
    uint64_t other=(uintptr_t)a->tex.mem;
    if(other<=base?base-other<a->tex.L.size:other-base<size)return 0;
  }
  return 1;
}
static void image_bc_copy_decode(image *d,VkFormat incoming,astc_copy_observation *o) {
#ifdef DM_VULKAN_SHARED_POOLS
  if(!o->scope || !o->stamp || !astc_copy_mixed_target(d) || !astc_copy_no_defined_alias(d))
    die("Mixed compressed copy requires observed BC3 storage with no defined alias");
  VkFormat decoded=decoded_format(d->tex.d.format);
  VkFormatProperties old_properties,incoming_properties,out_properties;
  vk_format_properties(d->format,&old_properties);vk_format_properties(incoming,&incoming_properties);
  vk_format_properties(decoded,&out_properties);
  if(!(old_properties.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_SRC_BIT) ||
      !(incoming_properties.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_SRC_BIT) ||
      !(out_properties.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_DST_BIT) ||
      !(out_properties.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
    die("Mixed compressed copy decode blit unsupported");
  VkImageFormatProperties supported;
  if(vkGetPhysicalDeviceImageFormatProperties(V.physical,decoded,d->description.imageType,
      d->description.tiling,d->description.usage,d->description.flags,&supported)!=VK_SUCCESS ||
      d->extent.width>supported.maxExtent.width || d->extent.height>supported.maxExtent.height ||
      d->layers>supported.maxArrayLayers || d->levels>supported.maxMipLevels ||
      !(supported.sampleCounts&VK_SAMPLE_COUNT_1_BIT))
    die("Mixed compressed copy RGBA physical image unsupported");
  // Callbacks may define/replace resources. Take no old handle/view before
  // ending them, then recheck the logical representation and captured CPU era.
  vk_mip_clear_drain();vk_render_end();
  if(d->bc_copy_decoded) {image_astc_copy_check(d);astc_copy_observation_release(o);return;}
  if(!astc_copy_mixed_target(d) || !astc_copy_no_defined_alias(d) ||
      nvn_dirty_since(d->tex.mem,d->tex.L.size,o->stamp))
    die("Mixed compressed copy changed during physical replacement");
  image_general_transfer(d);vk_transfer_barrier();
  image *old=malloc(sizeof *old);if(!old)die("Mixed compressed copy retirement allocation");
  *old=*d;old->shared_alias_linked=0;old->shared_alias_next=NULL;
  old->shared_alias_range=(alias_range_node){0};
  image_recipes_changed();image_binding_replaced(d,old);
  d->views=NULL;d->format=decoded;d->description.format=decoded;d->decoded=1;d->bpp=4;
  image_allocate(d);
  // Keep the same logical owner/alias registry node. The old VkImage/access
  // history and descriptor/framebuffer views retire with the real submission.
  d->bc_copy_decoded=1;d->astc_copy_stamp=o->stamp;
  d->shared_dirty_scope=o->scope;o->scope=NULL;d->cpu_rebuildable=0;astc_copy_live++;
  d->checked_dirty_epoch=d->checked_dirty_stamp=0;d->checked_dirty_range=(nvn_dirty_range_proof){0};
  for(unsigned level=0;level<d->levels;level++) {
    VkImageSubresourceLayers sub={d->aspect,level,0,d->layers};
    int width=mip(d->extent.width,level),height=mip(d->extent.height,level);
    VkImageBlit b={.srcSubresource=sub,.dstSubresource=sub,
      .srcOffsets={{0,0,0},{width,height,1}},.dstOffsets={{0,0,0},{width,height,1}}};
    image_transfer_pair(old,sub,d,sub);
    vkCmdBlitImage(V.cmd,old->handle,VK_IMAGE_LAYOUT_GENERAL,d->handle,VK_IMAGE_LAYOUT_GENERAL,
      1,&b,VK_FILTER_NEAREST);
  }
  d->initialized=1;vk_completion(image_storage_destroy,old);
  LOG("[mixed compressed copy] serial=%llu logical=%#x old=%u physical=%u levels=%u layers=%u preserved before incoming ROI",
      (unsigned long long)d->tex.serial,d->tex.d.format,old->format,d->format,d->levels,d->layers);
#else
  (void)d;(void)incoming;(void)o;die("Mixed compressed copy requires scoped shared observation");
#endif
}
static void image_astc_copy_propagate(image *s,image *d,const p_copy_t2t *p,astc_copy_observation *o) {
#ifdef DM_VULKAN_SHARED_POOLS
  int legacy=!vk_pool_shared_active() && vk_legacy_astc_copy_mode();
  if(!vk_pool_shared_active() && !legacy) {astc_copy_observation_release(o);return;}
#else
  // In particular preserve the original legacy ASTC -> decoded RGBA8 blit.
  astc_copy_observation_release(o);return;
#endif
  if(d->bc_copy_decoded) {image_astc_copy_check(d);astc_copy_observation_release(o);return;}
  if(d->astc_copy_encoding) {
    if(!astc_copy_target(&s->tex) || !astc_copy_target(&d->tex) || s->format!=d->format || !astc_copy_region(s,d,p))
      die("ASTC propagated destination requires the same actual compressed encoding and block geometry");
    astc_copy_observation_release(o);return;
  }
  VkFormat format=astc_copy_format(d->tex.d.format);
#ifdef DM_VULKAN_SHARED_POOLS
  if(legacy && (!d->decoded || d->format!=(format==VK_FORMAT_ASTC_4x4_SRGB_BLOCK?
      VK_FORMAT_R8G8B8A8_SRGB:VK_FORMAT_R8G8B8A8_UNORM))) {
    astc_copy_observation_release(o);return;
  }
#endif
  if(!format || s->format!=format || s==d) {astc_copy_observation_release(o);return;}
#ifdef DM_VULKAN_SHARED_POOLS
  if(!(o->scope || (legacy && o->legacy)) || !o->stamp || !astc_copy_fresh(d) || !astc_copy_region(s,d,p)) {
    if(legacy){astc_copy_observation_release(o);return;}
    if(astc_copy_mixed_target(d) && astc_copy_region(s,d,p)) {
      image_bc_copy_decode(d,s->format,o);astc_copy_observation_release(o);return;
    }
    die("ASTC resource-copy destination has no undefined-storage/CPU-version proof");
  }
  // End/replay before the final eligibility check, without holding a view or
  // descriptor across it. A nested draw may have defined the destination.
  vk_mip_clear_drain();vk_render_end();
  if(!astc_copy_fresh(d) || nvn_dirty_since(d->tex.mem,d->tex.L.size,o->stamp)) {
    if(legacy){astc_copy_observation_release(o);return;}
    die("ASTC resource-copy destination changed during physical replacement");
  }
  VkImageFormatProperties supported;
  VkResult valid=vkGetPhysicalDeviceImageFormatProperties(V.physical,format,d->description.imageType,
      d->description.tiling,d->description.usage,d->description.flags,&supported);
  if(valid!=VK_SUCCESS || d->extent.width>supported.maxExtent.width || d->extent.height>supported.maxExtent.height ||
      d->layers>supported.maxArrayLayers || d->levels>supported.maxMipLevels)
    {if(legacy){astc_copy_observation_release(o);return;}die("ASTC resource-copy physical image unsupported");}
  // Register the exact CPU-escape refusal BEFORE changing the physical
  // encoding. Capacity/OOM leaves the original undefined RGBA image intact.
  if(legacy && !vk_pool_legacy_encoding_guard_add((uintptr_t)d->tex.mem,d->tex.L.size)) {
    astc_copy_observation_release(o);return;
  }
  image *old=malloc(sizeof *old);if(!old)die("ASTC resource-copy physical retirement allocation");
  *old=*d;image_recipes_changed();image_binding_replaced(d,old);
  d->views=NULL;d->format=format;d->description.format=format;d->decoded=0;d->bpp=16;
  image_allocate(d);d->astc_copy_encoding=1;d->astc_copy_stamp=o->stamp;
  d->shared_dirty_scope=o->scope;o->scope=NULL;d->cpu_rebuildable=0;astc_copy_live++;
  d->checked_dirty_epoch=d->checked_dirty_stamp=0;d->checked_dirty_range=(nvn_dirty_range_proof){0};
  vk_completion(image_storage_destroy,old);
  LOG("[ASTC resource copy] serial=%llu logical=%#x physical=%u levels=%u layers=%u CPU-observed=%llu bytes=%llu",
      (unsigned long long)d->tex.serial,d->tex.d.format,format,d->levels,d->layers,
      (unsigned long long)d->astc_copy_stamp,(unsigned long long)d->tex.L.size);
#else
  die("ASTC resource-copy physical propagation requires scoped CPU observation");
#endif
  astc_copy_observation_release(o);
}
#ifdef DM_VULKAN_TEST_EMBED
// Owned functional API: no fake GPU capability and no renderer hot counters.
// The raw handle is used only by this byte oracle, with original image hazards.
int vk_texture_test_astc_copy_info(const tex_t *t,unsigned state[5],VkImage *raw) {
  image *i=astc_copy_find(t);if(!i)return 0;
  image_astc_copy_check(i);
  state[0]=i->astc_copy_encoding;state[1]=i->format;state[2]=i->tex.d.format;
  state[3]=i->cpu_rebuildable;state[4]=i->decoded;
  if(raw) {image_use(i,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    image_record(i,(VkImageSubresourceRange){i->aspect,0,i->levels,0,i->layers},
      VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT,0);*raw=i->handle;}
  return 1;
}
unsigned vk_texture_test_astc_copy_policy(void *owned,size_t bytes) {
  if(bytes<8192)die("ASTC copy policy owned extent");
  unsigned checks=0;const char *negative=getenv("DM_ASTC_COPY_POLICY_NEGATIVE");
#define ASTC_POLICY(x) do{checks++;if(!(x))die("ASTC copy policy: %s",#x);}while(0)
  image s={.tex={.d={.format=0x79,.target=NVN_TEXTURE_TARGET_2D,.width=128,.height=128,.depth=1,.levels=8}},
    .format=VK_FORMAT_ASTC_4x4_UNORM_BLOCK,.extent={128,128,1},.levels=8,.layers=1};
  image d={.tex={.d={.format=0x44,.target=NVN_TEXTURE_TARGET_2D_ARRAY,.width=128,.height=128,.depth=42,.levels=7},
      .mem=owned,.L={.size=4096}},.format=VK_FORMAT_BC3_UNORM_BLOCK,.extent={128,128,1},.levels=7,.layers=42,
    .aspect=VK_IMAGE_ASPECT_COLOR_BIT,.description={.samples=VK_SAMPLE_COUNT_1_BIT,.usage=7}};
  d.access=calloc(7*42*3,sizeof *d.access);if(!d.access)die("ASTC policy access metadata");
  p_copy_t2t p={.src=&s.tex,.dst=&d.tex,.sr={0,0,0,128,128,1},.dr={0,0,41,128,128,1}};
  ASTC_POLICY(astc_copy_fresh(&d));ASTC_POLICY(astc_copy_region(&s,&d,&p));
  d.initialized=1;ASTC_POLICY(!astc_copy_fresh(&d));d.initialized=0;
  d.tex.has_layout=1;ASTC_POLICY(!astc_copy_fresh(&d));d.tex.has_layout=0;
  d.external_access=1;ASTC_POLICY(!astc_copy_fresh(&d));d.external_access=0;
  image alias=d;alias.initialized=1;alias.next=images;image *saved_images=images;images=&alias;
  ASTC_POLICY(!astc_copy_fresh(&d));images=saved_images;
  d.description.usage|=VK_IMAGE_USAGE_STORAGE_BIT;ASTC_POLICY(!astc_copy_fresh(&d));d.description.usage=7;
  d.access[1].writes=VK_ACCESS_TRANSFER_WRITE_BIT;ASTC_POLICY(!astc_copy_fresh(&d));d.access[1].writes=0;
  p.flags=NVN_COPY_FLAGS_LINEAR_FILTER_BIT;ASTC_POLICY(!astc_copy_region(&s,&d,&p));p.flags=0;
  p.dr.width=64;ASTC_POLICY(!astc_copy_region(&s,&d,&p));p.dr.width=128;
  p.dr.zoffset=42;ASTC_POLICY(!astc_copy_region(&s,&d,&p));p.dr.zoffset=41;
  p.sr.xoffset=1;ASTC_POLICY(!astc_copy_region(&s,&d,&p));p.sr.xoffset=0;
  p.has_dv=1;p.dv.set_format=1;p.dv.format=0x41;ASTC_POLICY(!astc_copy_region(&s,&d,&p));p.has_dv=0;
  ASTC_POLICY(image_astc_copy_view_format(&d,0x44)==VK_FORMAT_ASTC_4x4_UNORM_BLOCK);
  ASTC_POLICY(image_astc_copy_view_format(&d,0x48)==VK_FORMAT_ASTC_4x4_SRGB_BLOCK);
  if(negative && !strcmp(negative,"view"))image_astc_copy_view_format(&d,0x41);
  free(d.access);
  // The real original tracker must reject any CPU publication, including an
  // unchanged byte. No memcmp/hash can turn equal bytes into an authority proof.
  int legacy=vk_legacy_astc_copy_mode() && !vk_pool_shared_active();
  nvn_dirty_scope *scope=NULL,*other=NULL;
  if(legacy)nvn_dirty_add_range(owned,8192);
  else {scope=nvn_dirty_scope_acquire(owned,4096);other=nvn_dirty_scope_acquire((uint8_t *)owned+4096,4096);}
  ASTC_POLICY(legacy?nvn_dirty_tracked(owned,8192):scope!=NULL);
  ASTC_POLICY(legacy?nvn_dirty_tracked((uint8_t *)owned+4096,4096):other!=NULL);
  nvn_dirty_protect(owned,8192);d.astc_copy_encoding=1;d.astc_copy_stamp=nvn_dirty_now();
  ASTC_POLICY(image_astc_copy_unchanged(&d));
  dm_guest_write_prepare((uint8_t *)owned+4096,1);((volatile uint8_t *)owned)[4096]=0x71;
  ASTC_POLICY(image_astc_copy_unchanged(&d)); // unrelated page invalidates epoch, not the source
  uint8_t before=*(volatile uint8_t *)owned;dm_guest_write_prepare(owned,1);*(volatile uint8_t *)owned=before;
  ASTC_POLICY(!image_astc_copy_unchanged(&d));
  if(negative && !strcmp(negative,"prepare"))image_astc_copy_check(&d);
  nvn_dirty_protect(owned,4096);d.astc_copy_stamp=nvn_dirty_now();d.checked_dirty_epoch=0;
  ASTC_POLICY(image_astc_copy_unchanged(&d));
  *(volatile uint8_t *)owned=before;ASTC_POLICY(!image_astc_copy_unchanged(&d)); // original signal hook
  if(negative && !strcmp(negative,"fault"))image_astc_copy_check(&d);
  if(legacy) {
    // Raw guards must remain independent of shared-only Pool resolution. This
    // is a metadata refusal oracle, not a fake ASTC-capable VkImage workload.
    ASTC_POLICY(astc_copy_legacy_envelope(&d.tex));
    d.tex.mem++;ASTC_POLICY(!astc_copy_legacy_envelope(&d.tex));d.tex.mem--;
    d.tex.L.size--;ASTC_POLICY(!astc_copy_legacy_envelope(&d.tex));d.tex.L.size++;
    image *aliases=shared_gpu_aliases;d.initialized=1;d.shared_alias_next=aliases;shared_gpu_aliases=&d;astc_copy_live++;
    mtl_sub_begin();uint32_t unchanged=0x12345678;
    ASTC_POLICY(vk_pool_try_read((uintptr_t)owned,4,&unchanged)==0 && unchanged==0x12345678);
    for(unsigned ref=0;ref<2;ref++) {vk_cover_wait_reference(ref);ASTC_POLICY(!vk_cover_read((uintptr_t)owned,4,&unchanged)&&unchanged==0x12345678);}
    vk_cover_wait_reference(0);
    if(negative && !strcmp(negative,"required-linear"))vk_pool_read((uintptr_t)owned,4,&unchanged);
    if(negative && !strcmp(negative,"gpu-linear")) {vk_buffer *raw;uint64_t off;vk_pool_address_stage((uintptr_t)owned,4,(void **)&raw,&off,VK_PIPELINE_STAGE_TRANSFER_BIT);}
    if(negative && !strcmp(negative,"gpu-writer"))vk_texture_shared_source_written((uintptr_t)owned,4);
    if(negative && !strcmp(negative,"writer-alias")){image w=d;w.astc_copy_encoding=0;w.shared_alias_linked=0;image_shared_alias_write(&w);}
    shared_gpu_aliases=aliases;astc_copy_live--;vk_internal_wait(vk_internal_commit());
    nvn_dirty_remove_range(owned);
  } else {nvn_dirty_scope_release(scope);nvn_dirty_scope_release(other);}
  ASTC_POLICY(!image_astc_copy_unchanged(&d)); // unknown/lifetime cannot be cached as clean
  if(negative && !strcmp(negative,"lifetime"))image_astc_copy_check(&d);
#undef ASTC_POLICY
  return checks;
}
#endif
#ifdef DM_VULKAN_TEST_EMBED
void vk_texture_test_astc_copy_upload(const tex_t *t) {image *i=image_get(t);image_upload(i);}
#endif

#if defined(DM_VULKAN_TEST_EMBED) && defined(DM_VULKAN_SHARED_POOLS)
void vk_texture_test_bc_copy_decode(const p_copy_t2t *p) {
  vk_texture_cpu_preflight(p->src);vk_texture_cpu_preflight(p->dst);
  image *found=astc_copy_find(p->dst);
  if(!found)die("Owned mixed copy target must already be defined");
  if(found->bc_copy_decoded) {image_astc_copy_check(found);return;}
  astc_copy_observation o={0};
  if(!shared_image_backing((uintptr_t)p->dst->mem,p->dst->L.size) ||
      !vk_pool_shared_materialized((uintptr_t)p->dst->mem,p->dst->L.size) ||
      !vk_pool_shared_image_source((uintptr_t)p->dst->mem,p->dst->L.size,1))
    die("Owned mixed copy observation unavailable");
  found=astc_copy_find(p->dst);if(!found)die("Owned mixed copy target changed after CPU preflight");
  o.scope=nvn_dirty_scope_acquire(p->dst->mem,p->dst->L.size);
  if(!o.scope || !vk_pool_shared_observe_protect((uintptr_t)p->dst->mem,p->dst->L.size,&o.stamp))
    die("Owned mixed copy observation refused");
  image *source=astc_copy_find(p->src);if(!source)die("Owned mixed copy source missing");
  image_bc_copy_decode(found,source->format,&o);astc_copy_observation_release(&o);
}
#endif

#if defined(DM_VULKAN_TEST_EMBED) && defined(DM_VULKAN_SHARED_POOLS)
VkImageLayout vk_texture_test_bc_copy_layout(const tex_t *t) {
  image *i=astc_copy_find(t);if(!i)die("Owned mixed copy image missing");return i->layout;
}
#endif
