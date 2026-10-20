static image *native_acquired_images;
// Actual physical ownership, not a contents cache. Only the translation owner
// creates/removes entries; image retirement already excludes completion users.
// Page envelopes include cache/mprotect neighbours of the native bytes.
typedef struct {uintptr_t first,end,prefix_end;image *owner;} native_address_range;
static native_address_range *native_ranges;
static size_t native_range_count,native_range_capacity;
static void native_range_prefix(size_t first) {
  uintptr_t previous=first?native_ranges[first-1].prefix_end:0;
  for(size_t n=first;n<native_range_count;n++) {
    previous=previous>native_ranges[n].end?previous:native_ranges[n].end;
    native_ranges[n].prefix_end=previous;
  }
}
static void native_range_add(image *i) {
  uintptr_t page=(uintptr_t)getpagesize(),a=(uintptr_t)i->tex.mem;
  if(!page || (page&(page-1)) || i->tex.L.size>UINTPTR_MAX-a || a+i->tex.L.size>UINTPTR_MAX-(page-1))
    die("Native image physical page bounds");
  native_address_range range={a&~(page-1),(a+i->tex.L.size+page-1)&~(page-1),0,i};
  if(native_range_count==native_range_capacity) {
    size_t capacity=native_range_capacity?native_range_capacity*2:16;
    if(capacity<native_range_capacity || capacity>SIZE_MAX/sizeof *native_ranges)die("Native range index size");
    void *memory=realloc(native_ranges,capacity*sizeof *native_ranges);if(!memory)die("Native range index allocation");
    native_ranges=memory;native_range_capacity=capacity;
  }
  size_t lo=0,hi=native_range_count;
  while(lo<hi) {size_t mid=lo+(hi-lo)/2;if(native_ranges[mid].first<=range.first)lo=mid+1;else hi=mid;}
  memmove(native_ranges+lo+1,native_ranges+lo,(native_range_count-lo)*sizeof *native_ranges);
  native_ranges[lo]=range;native_range_count++;native_range_prefix(lo);
}
static void native_range_remove(image *i) {
  size_t n=0;while(n<native_range_count && native_ranges[n].owner!=i)n++;
  if(n==native_range_count)die("Native range physical retirement mismatch");
  memmove(native_ranges+n,native_ranges+n+1,(native_range_count-n-1)*sizeof *native_ranges);
  native_range_count--;native_range_prefix(n);
  if(!native_range_count) {free(native_ranges);native_ranges=NULL;native_range_capacity=0;}
}
static void native_image_write_scope(image *i,unsigned n) {
  if(!vk_pool_native_image_write_ready(i->native_pool))
    die("Native image NC writer requires the shared prewrite commit gate");
  if(!i->native_write_scopes[n]) {
    const nvn_native_mip *m=&i->native_entry->recipe.mips[n];
    i->native_write_scopes[n]=nvn_dirty_scope_acquire(i->tex.mem+m->offset,m->size);
    if(!i->native_write_scopes[n])die("Native image writer scope admission failed");
  }
}
static size_t native_range_first(uintptr_t address) {
  size_t lo=0,hi=native_range_count;
  while(lo<hi) {size_t mid=lo+(hi-lo)/2;if(native_ranges[mid].prefix_end<=address)lo=mid+1;else hi=mid;}
  return lo;
}
#ifdef DM_VULKAN_TEST_EMBED
unsigned vk_test_native_address_index(void) {
  if(native_range_count)die("Native index owned test requires empty registry");
  size_t page=(size_t)getpagesize();uint8_t *mapping=mmap(NULL,8*page,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
  if(mapping==MAP_FAILED)die("Native index owned mapping");
  image owners[4]={0};const unsigned starts[]={0,2,0,6},sizes[]={1,2,5,1};
  for(unsigned n=0;n<4;n++) {owners[n].tex.mem=mapping+starts[n]*page;owners[n].tex.L.size=sizes[n]*page;native_range_add(&owners[n]);}
  unsigned checks=0;
  // Fixed semantic endpoints: the long enclosing interval must survive a
  // short same-base entry and a disjoint middle interval's prefix max.
  const unsigned pages[]={0,1,2,3,4,5,6,7},masks[]={5,4,6,6,4,0,8,0};
  for(unsigned q=0;q<8;q++) {uintptr_t a=(uintptr_t)mapping+pages[q]*page;unsigned mask=0;
    for(size_t n=native_range_first(a);n<native_range_count && native_ranges[n].first<a+1;n++)
      if(native_ranges[n].end>a)mask|=1u<<(unsigned)(native_ranges[n].owner-owners);
    if(mask!=masks[q])die("Native physical index endpoint mismatch");checks++;
  }
  native_range_remove(&owners[2]);native_range_remove(&owners[0]);native_range_remove(&owners[3]);native_range_remove(&owners[1]);
  if(native_range_count || native_ranges || native_range_capacity)die("Native physical index final retirement");checks++;
  native_range_add(&owners[1]);native_range_remove(&owners[1]);checks++;
  if(munmap(mapping,8*page))die("Native physical index own unmap");
  return checks;
}
#endif
static void native_image_linear_written(uint64_t address,uint64_t bytes) {
  if(!native_range_count || !bytes || bytes>UINTPTR_MAX-address)return;
  uintptr_t end=address+bytes,page=(uintptr_t)getpagesize();
  for(size_t n=native_range_first(address);n<native_range_count && native_ranges[n].first<end;n++) {
    native_address_range *r=&native_ranges[n];if(r->end<=address)continue;
    image *i=r->owner;
    for(unsigned level=0;level<i->levels;level++) {
      const nvn_native_mip *m=&i->native_entry->recipe.mips[level];
      uintptr_t a=(uintptr_t)i->tex.mem+m->offset,b=(a+m->size+page-1)&~(page-1);a&=~(page-1);
      if(a<end && b>address)native_image_write_scope(i,level);
    }
  }
}
static void native_image_alias_read(image *i,VkPipelineStageFlags stages) {
  uint64_t previous=(stages&~i->native_alias_visible_stages)?0:i->native_alias_generation;
  uint64_t now=vk_buffer_native_image_read((uintptr_t)i->tex.mem,i->tex.L.size,stages,previous);
  if(now!=i->native_alias_generation)i->native_alias_visible_stages=0;
  i->native_alias_generation=now;i->native_alias_visible_stages|=stages;
}
// The table is a layout/provenance declaration, never read-only authority.
// Imported images own a finite import of original Pool bytes (no payload
// allocation/copy). Submission refs retain accesses; physical image retirement
// releases its immutable native memory token before Pool/guest storage closes.
static const nvn_native_entry *native_image_entry(const tex_t *t) {
  if(!t || !t->has_layout || !nvn_native_layout_is_marker(t->layout))return NULL;
  const nvn_native_entry *entry=nvn_native_layout_resolve(t->layout,&t->d);
  if(!entry)die("Native image layout/description is not in the startup table");
  return entry;
}
// Pure table/descriptor metadata, before any image, view or buffer borrow.
int vk_texture_native_pool_collect(const tex_t *t,VkPipelineStageFlags stages,int writable,
                                  vk_pool_requirement_sink sink,void *context) {
  if(!t || !t->has_layout || !nvn_native_layout_is_marker(t->layout))return 1;
  const nvn_native_entry *entry=native_image_entry(t);
  if(!entry || !sink || !t->mem || t->L.size!=entry->recipe.image_size)return 0;
  vk_pool_requirement requirement={(uintptr_t)t->mem,entry->recipe.image_size,0,stages,writable,
    VK_POOL_REQUIRE_NATIVE_IMAGE};
  return sink(context,&requirement);
}
static int native_pool_operation(const tex_t *t,VkPipelineStageFlags stages,int writable) {
  if(!V.options.pool_window_bytes || !t || !t->has_layout || !nvn_native_layout_is_marker(t->layout))return 0;
  const nvn_native_entry *entry=native_image_entry(t);
  vk_pool_requirement requirement={(uintptr_t)t->mem,entry->recipe.image_size,0,stages,writable,
    VK_POOL_REQUIRE_NATIVE_IMAGE};
  return vk_pool_operation_begin_ranges(&requirement,1);
}
static int native_acquire_alias_writer(const alias_range_node *node,int unused) {
  (void)node;(void)unused;return 1;
}
static VkCommandBuffer native_acquire_pending_primary(image *i,VkPipelineStageFlags stages,VkAccessFlags access,
                                                     uint64_t *alias_generation) {
  VkCommandBuffer command=vk_render_pending_primary();
  if(!command || stages!=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT || access!=VK_ACCESS_SHADER_READ_BIT ||
      i->use_clock>image_pass_begin_clock || vk_mip_clear_pending())return VK_NULL_HANDLE;
  uint64_t address,bytes;
  if(!vk_pool_native_image_acquire_range(i->native_pool,(uintptr_t)i->tex.mem,i->tex.L.size,
      &address,&bytes))return VK_NULL_HANDLE;
  if(!vk_pool_native_acquire_independent(address,bytes))return VK_NULL_HANDLE;
  uintptr_t page=(uintptr_t)getpagesize(),end=address+bytes;
  if(!page || (page&(page-1)) || end>UINTPTR_MAX-(page-1))return VK_NULL_HANDLE;
  address&=~(page-1);end=(end+page-1)&~(page-1);
  // The Pool history alone does not describe private optimal-image writers.
  // Their original alias index remains authoritative. Reject all overlapping
  // writers (including old ones), rather than assume an upload made them inert.
  if(alias_range_overlap(shared_gpu_alias_ranges,address,end,
      native_acquire_alias_writer,1))return VK_NULL_HANDLE;
  for(size_t n=native_range_first(address);n<native_range_count && native_ranges[n].first<end;n++) {
    const native_address_range *range=&native_ranges[n];
    if(range->end>address && range->owner!=i && range->owner->use_clock>image_pass_begin_clock)
      return VK_NULL_HANDLE;
  }
  // Only independent first use moves. Current-recording Buffer alias accesses, CPU
  // publication, unknown/order barriers or image writers keep the original
  // end-pass path. Ownership still returns to FOREIGN at every chunk end;
  // this is not a new immutable-resource or CPU-writing contract.
  // Known retained writers come only from older chunks: the complete current
  // recording page ledger was negative above. A VkBuffer visibility bit does
  // not order this different object. Reuse the existing global alias-memory
  // dependency NOW, on the same pending primary, before FOREIGN acquire and
  // before execution of any secondary draw. Do not defer it to post-Draw
  // history recording and do not clear or manufacture producer history.
  *alias_generation=vk_buffer_native_image_read(address,end-address,stages,0);
  return command;
}
static void native_image_use_consumer(image *i,VkPipelineStageFlags stages,VkAccessFlags access) {
  if(!i->native_entry)return;
  if(!i->native_acquired) {
    uint64_t alias_generation=0;
    VkCommandBuffer command=native_acquire_pending_primary(i,stages,access,&alias_generation);
    if(!command) {
      vk_render_end();command=V.cmd;
      // A failed negative proof can include another stage's Buffer alias in
      // this same logical draw. Retain the original complete acquire scope;
      // only the proven-independent pending-pass path is fragment-only.
      stages=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
      access=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    }
    // GENERAL is the real external producer state, not an UNDEFINED discard.
    VkImageMemoryBarrier acquire={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .oldLayout=VK_IMAGE_LAYOUT_GENERAL,.newLayout=VK_IMAGE_LAYOUT_GENERAL,
      .srcQueueFamilyIndex=VK_QUEUE_FAMILY_FOREIGN_EXT,.dstQueueFamilyIndex=V.queue_family,
      .image=i->handle,.subresourceRange={i->aspect,0,i->levels,0,1},
      .dstAccessMask=access};
    // Keep the existing external handoff source here. Although the ownership
    // acquire source is normatively ignored, this exact provider consumes it;
    // removing its wait also changes the previous chunk's release/acquire
    // implementation ordering. This candidate narrows only the known actual
    // destination and moves the proven-independent barrier before the pass.
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,stages,
      0,0,NULL,0,NULL,1,&acquire);
    i->layout=VK_IMAGE_LAYOUT_GENERAL;i->layout_scope=stages;
    // A new external publication invalidates all previous content ordering
    // proofs even when the CPU wrote identical bytes. The acquire orders it.
    memset(i->access,0,(size_t)i->levels*3*sizeof *i->access);i->access_generation++;
    for(unsigned n=0;n<i->levels;n++) {
      image_access *cell=image_cell(i,0,n,0);
      cell->mip_layout=VK_IMAGE_LAYOUT_GENERAL;
      // Acquire covers every mip even when its first consumer selects one.
      // A later first transition of an unused mip must chain from THIS real
      // destination scope, never TOP_OF_PIPE. This is a directed ordering
      // source, not a claim that any other consumer is already visible.
      cell->mip_scope=stages;
    }
    i->native_alias_generation=alias_generation;
    i->native_alias_visible_stages=alias_generation?stages:0;
    i->native_acquired=1;i->native_next=native_acquired_images;native_acquired_images=i;
  }

}
static void native_image_use(image *i) {
  native_image_use_consumer(i,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
    VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT);
}
#ifdef DM_VULKAN_TEST_EMBED
// A real imported fresh RGBA image exercises the same acquire/alias planner
// without weakening the production ASTC table/producer validator. The pending
// NVN pass is real; a subsequent standard transfer checks the source pixels.
// This test is not an ASTC producer or a fragment-sampling oracle.
int vk_test_native_pending_acquire(VkImage handle,void *allocation,uint64_t address,uint64_t bytes,
                                  VkBuffer output) {
  if(native_acquired_images)die("Pending-acquire fixture requires no other acquired native images");
  nvn_native_entry entry={0};image_access cells[3]={0};
  image own={.handle=handle,.native_entry=&entry,.native_pool=allocation,.aspect=VK_IMAGE_ASPECT_COLOR_BIT,
    .levels=1,.layers=1,.layout=VK_IMAGE_LAYOUT_GENERAL,.access=cells,
    .tex={.mem=(void *)(uintptr_t)address,.L={.size=bytes}}};
  native_range_add(&own);
  native_image_use_consumer(&own,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
  int preserved=vk_render_active();vk_render_end();
  // The real consumer below is a transfer. Preserve an explicit chain from
  // the acquire destination rather than pretend a fragment sample was issued.
  VkMemoryBarrier ready={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_MEMORY_READ_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT};
  vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
    0,1,&ready,0,NULL,0,NULL);
  vk_pool_native_image_use(allocation,address,bytes,VK_PIPELINE_STAGE_TRANSFER_BIT);
  VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={32,32,1}};
  vkCmdCopyImageToBuffer(V.cmd,handle,VK_IMAGE_LAYOUT_GENERAL,output,1,&copy);
  vk_native_images_release();native_range_remove(&own);
  return preserved;
}
#endif
static void native_image_create(image *i,const nvn_native_entry *entry) {
  const nvn_native_recipe *r=&entry->recipe;
  VkExternalMemoryImageCreateInfo external={.sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
    .handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
  i->description=(VkImageCreateInfo){.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .imageType=r->image_type,.format=r->format,.extent={r->width,r->height,r->depth},
    .mipLevels=r->levels,.arrayLayers=r->layers,.samples=r->samples,.tiling=r->tiling,
    .usage=r->usage,.flags=r->flags,.sharingMode=r->sharing,.initialLayout=r->initial_layout};
  VkPhysicalDeviceExternalImageFormatInfo import={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
    .handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
  VkPhysicalDeviceImageFormatInfo2 query={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
    .pNext=&import,.format=r->format,.type=r->image_type,.tiling=r->tiling,.usage=r->usage,.flags=r->flags};
  VkExternalImageFormatProperties features={.sType=VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
  VkImageFormatProperties2 properties={.sType=VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,.pNext=&features};
  VK_CHECK(vkGetPhysicalDeviceImageFormatProperties2(V.physical,&query,&properties));
  VkExternalMemoryFeatureFlags bits=features.externalMemoryProperties.externalMemoryFeatures;
  const VkImageFormatProperties *limit=&properties.imageFormatProperties;
  if(!(bits&VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) || (bits&VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT) ||
      limit->maxExtent.width<r->width || limit->maxExtent.height<r->height || limit->maxMipLevels<r->levels ||
      !limit->maxArrayLayers || !(limit->sampleCounts&1))die("Native image exact recipe not importable");
  // A stack pNext lives only for this API call, never in cached descriptions.
  VkImageCreateInfo create=i->description;create.pNext=&external;
  VK_CHECK(vkCreateImage(V.device,&create,NULL,&i->handle));
  VkMemoryDedicatedRequirements dedicated={.sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
  VkMemoryRequirements2 needs={.sType=VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,.pNext=&dedicated};
  VkImageMemoryRequirementsInfo2 info={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,.image=i->handle};
  vkGetImageMemoryRequirements2(V.device,&info,&needs);
  if(dedicated.requiresDedicatedAllocation || needs.memoryRequirements.size!=r->image_size ||
     needs.memoryRequirements.alignment!=r->image_alignment)die("Native image storage recipe changed");
  for(unsigned n=0;n<i->levels;n++) {
    VkImageSubresource2EXT sub={.sType=VK_STRUCTURE_TYPE_IMAGE_SUBRESOURCE_2_EXT,
      .imageSubresource={i->aspect,n,0}};
    VkSubresourceHostMemcpySizeEXT size={.sType=VK_STRUCTURE_TYPE_SUBRESOURCE_HOST_MEMCPY_SIZE_EXT};
    VkSubresourceLayout2EXT layout={.sType=VK_STRUCTURE_TYPE_SUBRESOURCE_LAYOUT_2_EXT,.pNext=&size};
    vkGetImageSubresourceLayout2EXT(V.device,i->handle,&sub,&layout);
    nvn_native_mip actual={layout.subresourceLayout.offset,size.size,layout.subresourceLayout.rowPitch,
      layout.subresourceLayout.arrayPitch,layout.subresourceLayout.depthPitch};
    if(memcmp(&actual,&r->mips[n],sizeof actual))die("Native image actual mip layout changed");
  }
  i->native_pool=vk_pool_native_image_bind((uintptr_t)i->tex.mem,r->image_size,i->handle,
    &needs.memoryRequirements,&i->memory);
  i->native_entry=entry;i->initialized=1;i->physical_traits=image_physical_traits(i);
  i->access=calloc((size_t)i->levels*3,sizeof *i->access);
  if(!i->access)die("Native image access allocation");
  i->access_generation++;native_range_add(i);native_image_use(i);
}
void vk_native_images_release(void) {
  // Called at the original end of every command chunk, after render/query
  // closure. CPU production still requires the original NVN fence/Flush rules;
  // releasing is not a CPU wait and does not authorize racing host writes.
  while(native_acquired_images) {
    image *i=native_acquired_images;native_acquired_images=i->native_next;i->native_next=NULL;
    image_layout(i,VK_IMAGE_LAYOUT_GENERAL);
    VkImageMemoryBarrier release={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,
      .oldLayout=VK_IMAGE_LAYOUT_GENERAL,.newLayout=VK_IMAGE_LAYOUT_GENERAL,
      .srcQueueFamilyIndex=V.queue_family,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_FOREIGN_EXT,
      .image=i->handle,.subresourceRange={i->aspect,0,i->levels,0,1}};
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
      0,0,NULL,0,NULL,1,&release);i->native_acquired=0;
  }
}

static void native_image_record(image *i,VkImageSubresourceRange range,VkPipelineStageFlags stages,VkAccessFlags access) {
  native_image_use(i);
  if(!stages || !access)return; // metadata-only normalization is not a GPU access
  if(i->native_use_serial!=vk_recording_serial()) {
    i->native_use_serial=vk_recording_serial();i->native_recorded_stages=0;
  }
  // Reuse the existing pool dependency engine for linear/image aliases. The
  // same canonical bytes and actual shader/transfer stages participate in
  // WAR/RAW/WAW ordering; image-only histories would miss VkBuffer writers.
  const nvn_native_recipe *recipe=&i->native_entry->recipe;
  const nvn_native_mip *first=&recipe->mips[range.baseMipLevel];
  const nvn_native_mip *last=&recipe->mips[range.baseMipLevel+range.levelCount-1];
  uint64_t address=(uintptr_t)i->tex.mem+first->offset;
  uint64_t bytes=last->offset+last->size-first->offset;
  if(image_writes(access)) {
    vk_buffer_use use={address,bytes,1};vk_transfer_buffer_dependencies(&use,1);
  } else native_image_alias_read(i,stages);
  if(stages&~i->native_recorded_stages) {
    vk_pool_native_image_use(i->native_pool,(uintptr_t)i->tex.mem,i->tex.L.size,stages);
    i->native_recorded_stages|=stages;
  }
  if(access&VK_ACCESS_TRANSFER_READ_BIT) {
    if(!(i->description.usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT))die("Native image missing transfer source recipe");
  }
  if(image_writes(access)) {
    if((access&~VK_ACCESS_TRANSFER_WRITE_BIT) || !(i->description.usage&VK_IMAGE_USAGE_TRANSFER_DST_BIT))
      die("Native image writer not in baked transfer recipe");
    for(unsigned n=range.baseMipLevel;n<range.baseMipLevel+range.levelCount;n++) {
      const nvn_native_mip *m=&i->native_entry->recipe.mips[n];
      uint64_t address=(uintptr_t)i->tex.mem+m->offset;
      // Both finite native allocation and logical operation Pool are pinned. Acquire
      // only a mip that is actually written, never every sampled-only asset.
      // The independent NC commit gate protects these pages through fence +
      // Invalidate. A missing gate is a hard experimental capability failure.
      native_image_write_scope(i,n);
      vk_buffer_transfer_written_range(address,m->size);
      vk_pool_native_image_writeback(i->native_pool,address,m->size,stages);
    }
  }
}
