// Recording-thread-owned same-pass FS subsets. Ordinary and resident native
// members borrow disjoint immutable request vectors and descriptor ordinals.
// A native-only mutation must never invalidate the ordinary subset.
static int split_enabled=-1,native_front_enabled;
static int native_front_logged;
static unsigned split_depth;
static const void *split_admitted;
static int split_capture=-1;
static unsigned split_capture_slot;
static int split_capture_sampler;
typedef struct {
  const void *shader;
  uint64_t revision,serial,pass,lifetime,descriptors,samplers,empty_sources;
  uint64_t mask[2],textures[2],sampler_slots[2];
  image_request requests[128];
  unsigned count,pool_count;
  uint64_t native_writers;
  void *pool_tokens[128],*pool_owners[128];
  int valid,building,failed,hot,consumed;
  vk_split_descriptor_value *payload;
} image_read_group;
static image_read_group split_groups[2]; // bit0 ordinary, bit1 resident native
static vk_split_descriptor_value split_values_storage[128];
static _Atomic uint64_t split_published[6];
static _Atomic int split_published_enabled;
// Counts are SUBGROUP builds/hits/expansions/invalidations; items remain actual
// descriptor/request entries. Disabled native contributes no subgroup events.
static uint64_t split_counts[6];
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
static uint64_t native_front_test_counts[4]; // captures, hot items, Pool borrows, writer invalidations
void vk_test_native_read_group_counts(uint64_t out[4]) {memcpy(out,native_front_test_counts,sizeof native_front_test_counts);}
static void (*split_test_callback)(void *);
static void *split_test_context;
static int split_test_dependency_phase;
void vk_test_image_split_stats(uint64_t out[6]) {memcpy(out,split_counts,sizeof split_counts);}
void vk_test_image_split_callback(void (*callback)(void *),void *context) {
  split_test_callback=callback;split_test_context=context;
  split_test_dependency_phase=getenv("DM_IMAGE_BUFFER_SPLIT_DEPS_FIXTURE")!=NULL;
}
VkImage vk_test_image_split_first_image(void) {
  for(unsigned n=0;n<2;n++)if(split_groups[n].count)return split_groups[n].requests[0].owner->handle;
  return VK_NULL_HANDLE;
}
static void image_split_dependency_fixture(void) {
  if(!split_test_callback || !split_test_dependency_phase || !image_binding_active ||
      !image_batch_current.split_read_group)return;
  unsigned n=(image_batch_current.split_read_group&1)?0:1;
  image_request q=split_groups[n].requests[0];
  while(image_binding_count<image_binding_capacity)
    image_request_add_normalized(q.owner,q.range,q.stages,q.access);
  void (*callback)(void *)=split_test_callback;void *context=split_test_context;
  split_test_callback=NULL;split_test_context=NULL;callback(context);
}
unsigned vk_test_image_split_requests(VkImage handle) {
  unsigned count=0;
  for(unsigned b=0;b<image_binding_active;b++)for(size_t k=0;k<image_batches[b].count;k++)
    count+=image_batches[b].requests[k].owner->handle==handle;
  return count;
}
#endif
static int split_on(void) {return split_enabled==1;}
static unsigned split_active_groups(void) {return native_front_enabled?2:1;}
static void split_expand_mask(unsigned mask) {
  if(!split_on())return;
  unsigned active=image_binding_active;
  for(unsigned b=0;b<active;b++) {
    unsigned selected=image_batches[b].split_read_group&mask;
    // Clear ALL selected borrower bits before appending. Raster materialization
    // can recurse through image_split_changed; it must not expand us twice.
    image_batches[b].split_read_group&=~selected;
    image_binding_active=b+1;
    for(unsigned n=0;n<2;n++)if(selected&(1u<<n)) {
      image_read_group *g=&split_groups[n];
      for(unsigned k=0;k<g->count;k++) {
        const image_request *q=&g->requests[k];
        image_request_add_normalized(q->owner,q->range,q->stages,q->access);
      }
      split_counts[3]++;split_counts[4]+=g->count;
    }
  }
  image_binding_active=active;
}
static void split_break_mask(unsigned mask) {
  if(!split_on())return;
  split_expand_mask(mask);
  for(unsigned n=0;n<split_active_groups();n++)if(mask&(1u<<n)) {
    image_read_group *g=&split_groups[n];
    if(g->valid || g->building)split_counts[5]++;
    g->valid=0;g->failed=1;
  }
}
void vk_image_split_native_writer_changed(void) {
  image_read_group *g=&split_groups[1];
  if(native_front_enabled && g->count) {
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
    if(g->valid || g->building)native_front_test_counts[3]++;
#endif
    split_break_mask(2);
  }
}
void vk_image_split_break(void) {split_break_mask(3);}
static void image_split_changed(image *i) {
  raster_observe(i);
  unsigned n=0;
#ifdef DM_VULKAN_NATIVE_RESOURCES
  n=!!i->native_entry;
#endif
  image_read_group *g=&split_groups[n];
  if(split_on() && i->split_group_revision && i->split_group_revision==g->revision)
    split_break_mask(1u<<n);
}
void vk_image_split_draw_begin(void) {
  if(split_enabled<0) {
    const char *v=getenv("DM_VULKAN_IMAGE_BUFFER_SPLIT");
    if(v && strcmp(v,"0") && strcmp(v,"1"))die("DM_VULKAN_IMAGE_BUFFER_SPLIT expects 0 or 1");
    split_enabled=!v || !strcmp(v,"1");
    const char *native=getenv("DM_VULKAN_NATIVE_READ_GROUP_FRONT");
    if(native && strcmp(native,"0") && strcmp(native,"1"))die("DM_VULKAN_NATIVE_READ_GROUP_FRONT expects 0 or 1");
#ifdef DM_VULKAN_NATIVE_RESOURCES
    native_front_enabled=split_enabled && native && !strcmp(native,"1");
#endif
    LOG("[vulkan native read group front] enabled=%d partitioned=1 same-pass/serial resident coherent FS only",native_front_enabled);
    atomic_store_explicit(&split_published_enabled,split_enabled,memory_order_relaxed);
  }
  if(!split_on())return;
  if(split_depth)vk_image_split_break();
  if(!split_depth) {
    split_admitted=NULL;
    for(unsigned n=0;n<2;n++)split_groups[n].consumed=split_groups[n].hot=0;
  }
  split_depth++;
}
void vk_image_split_draw_end(void) {
  if(!split_on())return;
  if(!split_depth)die("Image split draw scope underflow");
  if(!--split_depth) {
    split_admitted=NULL;split_capture=-1;
    for(unsigned n=0;n<2;n++)if(split_groups[n].building) {
      split_groups[n].building=0;split_groups[n].valid=0;
    }
  }
}
void vk_image_split_admit(const void *shader,int eligible) {
  if(split_on() && split_depth==1)split_admitted=eligible?shader:NULL;
}
static int split_ready(const image_read_group *g,const void *shader) {
  return split_on() && split_depth==1 && split_admitted==shader && shader &&
    g->shader==shader && g->valid &&
    (g->count || (g->empty_sources && g->empty_sources==vk_driver_state_versions[1][1])) &&
    (g!=&split_groups[1] || !g->count || (g->native_writers &&
      g->native_writers==vk_buffer_native_writer_generation())) &&
    g->serial==vk_recording_serial() && g->pass==V.passes &&
    vk_render_active() && !vk_mip_clear_pending() &&
    g->lifetime==image_recipe_generation &&
    g->descriptors==atomic_load(&descriptor_epoch) &&
    g->samplers==atomic_load(&sampler_registry_epoch);
}
static void split_native_retain(image_read_group *g) {
#ifdef DM_VULKAN_NATIVE_RESOURCES
  for(unsigned k=0;k<g->pool_count;k++)vk_pool_native_image_operation_retain(g->pool_tokens[k]);
#endif
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  native_front_test_counts[2]+=g->pool_count;
#endif
}
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
unsigned vk_test_native_read_group_retain(void) {
  image_read_group *g=&split_groups[1];
  if(!g->count || !g->valid || g->serial!=vk_recording_serial())return 0;
  split_native_retain(g);return g->pool_count;
}
#endif
int vk_image_split_prepare(const void *shader,uint64_t skip[2]) {
  int found=0;skip[0]=skip[1]=0;
  for(unsigned n=0;n<split_active_groups();n++)if(split_groups[n].count && split_ready(&split_groups[n],shader)) {
    skip[0]|=split_groups[n].mask[0];skip[1]|=split_groups[n].mask[1];found=1;
  }
  return found;
}
static void split_build(unsigned n,const void *shader,vk_split_descriptor_value *payload) {
  split_expand_mask(1u<<n); // never overwrite a borrowed member vector
  image_read_group *g=&split_groups[n];
  if(!++g->revision)die("Image split revision overflow");
  g->shader=shader;g->payload=payload;
  g->mask[0]=g->mask[1]=g->textures[0]=g->textures[1]=0;
  g->sampler_slots[0]=g->sampler_slots[1]=0;
  g->count=g->pool_count=0;g->native_writers=0;g->valid=0;g->building=1;
  g->failed=g->hot=0;split_capture=-1;
  g->serial=vk_recording_serial();g->pass=V.passes;
  g->empty_sources=vk_driver_state_versions[1][1];
  g->lifetime=image_recipe_generation;g->descriptors=atomic_load(&descriptor_epoch);
  g->samplers=atomic_load(&sampler_registry_epoch);split_counts[0]++;
}
vk_split_descriptor_value *vk_image_split_values(const void *shader,unsigned nb,uint64_t skip[2],
                                               vk_split_descriptor_value *prepared_values) {
  if(!split_on() || split_depth!=1 || !shader || split_admitted!=shader ||
      !image_binding_active || nb>128 || !vk_render_active()) {
    for(unsigned n=0;n<split_active_groups();n++)
      if(prepared_values && split_groups[n].payload==prepared_values)split_break_mask(1u<<n);
    return NULL;
  }
  vk_split_descriptor_value *payload=prepared_values?prepared_values:split_values_storage;
  for(unsigned n=0;n<split_active_groups();n++) {
    image_read_group *g=&split_groups[n];
    if(split_ready(g,shader) && g->payload==payload) {
      // An empty admission memo only avoids its own cold rebuild. It owns
      // no descriptor product, requests or Pool borrows, and is never a hit.
      if(!g->count)continue;
      skip[0]|=g->mask[0];skip[1]|=g->mask[1];
      if(n)split_native_retain(g);
      g->hot=1;g->consumed=1;image_batch_current.split_read_group|=1u<<n;
    } else split_build(n,shader,payload);
  }
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
  if(image_batch_current.split_read_group && split_test_callback && !split_test_dependency_phase) {
    void (*callback)(void *)=split_test_callback;void *context=split_test_context;
    split_test_callback=NULL;split_test_context=NULL;callback(context);
  }
#endif
  return payload;
}
int vk_image_split_retry(uint64_t retry[2]) {
  if(!split_on() || split_depth!=1)return 0;
  int changed=0;
  for(unsigned n=0;n<split_active_groups();n++) {
    image_read_group *g=&split_groups[n];
    if(!g->hot)continue;
    // A surviving subset remains pending across another subset's retry:
    // that original consumer can itself invoke a later invalidating callback.
    if(split_ready(g,g->shader))continue;
    g->hot=0;
    // Only this previously skipped subset is retried. Another hot subset's
    // descriptor values and borrowed original requests remain untouched.
    split_expand_mask(1u<<n);
    retry[0]|=g->mask[0];retry[1]|=g->mask[1];
    changed|=!!(g->mask[0]|g->mask[1]);g->building=0;g->valid=0;
  }
  if(changed)split_capture=-1;
  return changed;
}
void vk_image_split_capture_begin(unsigned binding,unsigned slot,int sampler_slot) {
  if(!split_on() || split_depth!=1)return;
  split_capture=binding<128?(int)binding:-1;
  split_capture_slot=slot;split_capture_sampler=sampler_slot;
}
void vk_image_split_capture_end(void) {if(split_on())split_capture=-1;}
static void image_split_capture(image *i,VkImageSubresourceRange r,VkPipelineStageFlags stages,
                                int storage,const view_t *view) {
  if(!split_on() || split_depth!=1 || split_capture<0)return;
  unsigned ordinal=split_capture,slot=split_capture_slot;int sampler=split_capture_sampler;
  split_capture=-1;
  unsigned n=0;
#ifdef DM_VULKAN_NATIVE_RESOURCES
  n=!!i->native_entry;
#endif
  image_read_group *g=&split_groups[n];
  if(!g->building || g->failed || (n && !native_front_enabled))return;
  if(slot>=128 || i->external_access || storage || stages!=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT ||
      (view && view->set_format && view->format!=i->tex.d.format) || sampled_target_alias(&i->tex,view))return;
#ifdef DM_VULKAN_NATIVE_RESOURCES
  if(n) {
    if(i->native_acquired!=2 || i->layout!=VK_IMAGE_LAYOUT_GENERAL ||
        !vk_pool_native_image_queue_resident(i->native_pool))return;
    void *owner=vk_pool_native_image_operation_owner(i->native_pool);
    unsigned k=0;while(k<g->pool_count && g->pool_owners[k]!=owner)k++;
    if(k==g->pool_count) {g->pool_owners[k]=owner;g->pool_tokens[k]=i->native_pool;g->pool_count++;}
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
    native_front_test_counts[0]++;
#endif
  } else
#endif
  if(i->tex.has_layout)return;
  if(g->count==128)die("Image split group capacity");
  g->requests[g->count++]=(image_request){i,r,stages,VK_ACCESS_SHADER_READ_BIT,0};
  g->mask[ordinal/64]|=UINT64_C(1)<<(ordinal%64);
  g->textures[slot/64]|=UINT64_C(1)<<(slot%64);
  if(sampler>=0 && sampler<128)g->sampler_slots[sampler/64]|=UINT64_C(1)<<(sampler%64);
  i->split_group_revision=g->revision;
}
static void image_split_record_done(void) {
  if(!split_on() || split_depth!=1)return;
  for(unsigned n=0;n<split_active_groups();n++) {
    image_read_group *g=&split_groups[n];
    if(g->consumed && (image_batch_current.split_read_group&(1u<<n)) && split_ready(g,g->shader)) {
      split_counts[1]++;split_counts[2]+=g->count;
      if(n) {
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
        native_front_test_counts[1]+=g->count;
#endif
        if(!native_front_logged) {
          native_front_logged=1;
          LOG("[vulkan native read group front] actual-hit native=%u unique-Pools=%u",g->count,g->pool_count);
        }
      }
    }
    if(!g->building)continue;
    g->building=0;g->native_writers=n?vk_buffer_native_writer_generation():0;
    // The first actual read has stamped every member after this pass cutoff.
    // Repeated reads preserve those classifications; boundaries never reuse it.
    int empty=native_front_enabled && !g->count && g->empty_sources &&
      g->empty_sources==vk_driver_state_versions[1][1];
    g->valid=!g->failed && (g->count || empty) && g->serial==vk_recording_serial() && g->pass==V.passes &&
      vk_render_active() && !vk_mip_clear_pending() && g->lifetime==image_recipe_generation &&
      g->descriptors==atomic_load(&descriptor_epoch) && g->samplers==atomic_load(&sampler_registry_epoch);
    if(g->valid && empty) {
      // c2's existing source version covers ordinary setters/restores. Watch
      // every texture/sampler handle too: the original bloom helper updates
      // c2 only for low32 changes, while this event sees the complete handle.
      g->textures[0]=g->textures[1]=g->sampler_slots[0]=g->sampler_slots[1]=UINT64_MAX;
    }
  }
}
void vk_image_split_handles(uint32_t op,unsigned stage,unsigned first,unsigned count,const uint64_t *values) {
  if(!split_on() || stage!=1 || (op!=OP_BIND_TEX && op!=OP_BIND_SAMP))return;
  const uint64_t *old=op==OP_BIND_TEX?S.tex[1]:S.samp[1];
  for(unsigned n=0;n<split_active_groups();n++) {
    image_read_group *g=&split_groups[n];if(!g->shader)continue;
    const uint64_t *mask=op==OP_BIND_TEX?g->textures:g->sampler_slots;
    for(unsigned k=0;k<count;k++) {
      unsigned slot=first+k;
      if((mask[slot/64]&(UINT64_C(1)<<(slot%64))) && old[slot]!=values[k]) {split_break_mask(1u<<n);break;}
    }
  }
}
static void image_split_publish(void) {
  if(!split_on())return;
  for(unsigned k=0;k<6;k++)atomic_store_explicit(&split_published[k],split_counts[k],memory_order_relaxed);
}
void vk_image_split_stats(uint64_t out[6]) {
  for(unsigned k=0;k<6;k++)out[k]=atomic_load_explicit(&split_published[k],memory_order_relaxed);
}
int vk_image_split_enabled(void) {return atomic_load_explicit(&split_published_enabled,memory_order_relaxed);}
