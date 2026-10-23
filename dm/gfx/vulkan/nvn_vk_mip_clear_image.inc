// Read-only admission: never allocate/upload/promote while another pass is
// being kept open. Packaged CPU-backed images are deliberately out of scope.
int vk_texture_mip_clear_admit(const tex_t *t,const view_t *v,VkImage *handle) {
  vk_texture_raster_break();
  if(!t || !v || !v->set_levels || v->nlevels!=1 || t->has_layout ||
      t->d.target!=NVN_TEXTURE_TARGET_2D || t->d.samples>1 || t->L.layers!=1 ||
      t->d.levels<2 || t->d.levels>16 || v->base_level<0 || v->base_level>=t->d.levels ||
      (v->set_target && v->target!=NVN_TEXTURE_TARGET_2D) ||
      (v->set_format && v->format!=t->d.format) ||
      (v->set_layers && (v->min_layer || v->nlayers!=1)))return 0;
  for(image *i=image_buckets[t->serial%8192];i;i=i->hash_next)if(i->tex.serial==t->serial) {
    if(!i->initialized || i->tex.has_layout || !i->handle || i->tex.mem!=t->mem ||
        memcmp(&i->tex.d,&t->d,sizeof t->d) || memcmp(&i->tex.L,&t->L,sizeof t->L) ||
        i->description.imageType!=VK_IMAGE_TYPE_2D || i->layers!=1 ||
        i->aspect!=VK_IMAGE_ASPECT_COLOR_BIT || !image_mip_tracked(i) || !image_safe_layouts(i) ||
        !(i->description.usage&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ||
        (i->description.usage&VK_IMAGE_USAGE_STORAGE_BIT) ||
        i->general_submit==vk_recording_serial() ||
        image_cell(i,0,v->base_level,0)->use_clock>image_pass_begin_clock)return 0;
    // A different VkImage may alias the guest storage. Its use in this same
    // physical pass cannot be inferred from this image's own mip cells.
    for(image *other=images;other;other=other->next)if(other!=i &&
        other->use_clock>image_pass_begin_clock && other->tex.mem && other->tex.L.size) {
      uint64_t a=(uintptr_t)t->mem,b=(uintptr_t)other->tex.mem;
      uint64_t an=t->L.size,bn=other->tex.L.size;
      if(an>UINT64_MAX-a || bn>UINT64_MAX-b || (a<b?b-a<an:a-b<bn))return 0;
    }
    // Existing active attachment ownership retains this actual storage until
    // this recording serial's true GPU completion, including retirement.
    i->last_use=vk_recording_serial();*handle=i->handle;return 1;
  }
  return 0;
}
// A synchronous front-proof batch, never a cache across replay. Keep the
// one-view entry above unchanged for every actual clear revalidation.
int vk_texture_mip_clear_admit_views(const tex_t *t,const view_t *const *views,
                                     unsigned count,VkImage *handle) {
  if(!t || !views || !count || count>4)return 0;
  for(unsigned n=1;n<count;n++) {
    const view_t *v=views[n];
    if(!v || !v->set_levels || v->nlevels!=1 || v->base_level<0 || v->base_level>=t->d.levels ||
        (v->set_target && v->target!=NVN_TEXTURE_TARGET_2D) ||
        (v->set_format && v->format!=t->d.format) ||
        (v->set_layers && (v->min_layer || v->nlayers!=1)))return 0;
  }
  // This proves the first view, exact physical identity and whole-allocation
  // alias safety once. It only updates last_use; no callback, resource change
  // or use-clock mutation can intervene before checking the remaining cells.
  if(!vk_texture_mip_clear_admit(t,views[0],handle))return 0;
  for(image *i=image_buckets[t->serial%8192];i;i=i->hash_next)if(i->tex.serial==t->serial) {
    for(unsigned n=1;n<count;n++)
      if(image_cell(i,0,views[n]->base_level,0)->use_clock>image_pass_begin_clock)return 0;
    return 1;
  }
  return 0;
}
void vk_texture_mip_clear_mark(const tex_t *t,const view_t *v) {
  vk_texture_raster_break();
  for(image *i=image_buckets[t->serial%8192];i;i=i->hash_next)if(i->tex.serial==t->serial) {
    // Logical content is now pending, not known zero and not yet a physical
    // GPU writer. Consumer hooks drain before using layout/visibility facts.
    i->zero_mips&=~zero_mip_mask(v->base_level,1);return;
  }
  die("Vulkan admitted mip clear image disappeared");
}
