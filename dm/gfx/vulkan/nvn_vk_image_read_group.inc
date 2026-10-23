// Same-pass ordinary FS subset. This object is recording-thread owned.
// A live batch borrows one immutable member vector. Every mutation which can
// invalidate that vector first restores ordinary requests, before callbacks.
static int split_enabled=-1;
static unsigned split_depth;
static const void *split_admitted;
static struct {
  const void *shader;
  uint64_t revision,serial,pass,lifetime,descriptors,samplers;
  uint64_t mask[2],textures[2],sampler_slots[2];
  image_request requests[128];
  unsigned count,nb;
  int valid,building,failed,hot,consumed,capture,capture_slot,capture_sampler;
  vk_split_descriptor_value values[128];
} split_group;
static _Atomic uint64_t split_published[6];
static _Atomic int split_published_enabled;
static uint64_t split_counts[6]; // builds, hits, skipped items, expansions, expanded items, invalidations
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
static void (*split_test_callback)(void *);
static void *split_test_context;
static int split_test_dependency_phase;
void vk_test_image_split_stats(uint64_t out[6]) {memcpy(out,split_counts,sizeof split_counts);}
void vk_test_image_split_callback(void (*callback)(void *),void *context) {
  split_test_callback=callback;split_test_context=context;
  split_test_dependency_phase=getenv("DM_IMAGE_BUFFER_SPLIT_DEPS_FIXTURE")!=NULL;
}
VkImage vk_test_image_split_first_image(void) {
  return split_group.count?split_group.requests[0].owner->handle:VK_NULL_HANDLE;
}
static void image_split_dependency_fixture(void) {
  if(!split_test_callback || !split_test_dependency_phase || !image_binding_active ||
      !image_batch_current.split_read_group)return;
  // Force materialization to grow the actual request allocation. The caller
  // has already captured its old pointer/count, as with a native callback.
  image_request q=split_group.requests[0];
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
static void split_expand(void) {
  if(!split_on())return;
  unsigned active=image_binding_active;
  for(unsigned b=0;b<active;b++)if(image_batches[b].split_read_group) {
    image_batches[b].split_read_group=0;
    image_binding_active=b+1;
    for(unsigned k=0;k<split_group.count;k++) {
      const image_request *q=&split_group.requests[k];
      image_request_add_normalized(q->owner,q->range,q->stages,q->access);
    }
    split_counts[3]++;split_counts[4]+=split_group.count;
  }
  image_binding_active=active;
}
void vk_image_split_break(void) {
  if(!split_on())return;
  split_expand();
  if(split_group.valid || split_group.building)split_counts[5]++;
  split_group.valid=0;split_group.failed=1;
}
static void image_split_changed(image *i) {
  if(split_on() && i->split_group_revision && i->split_group_revision==split_group.revision)
    vk_image_split_break();
}
void vk_image_split_draw_begin(void) {
  if(split_enabled<0) {
    const char *v=getenv("DM_VULKAN_IMAGE_BUFFER_SPLIT");
    if(v && strcmp(v,"0") && strcmp(v,"1"))die("DM_VULKAN_IMAGE_BUFFER_SPLIT expects 0 or 1");
    split_enabled=!v || !strcmp(v,"1");split_group.capture=-1;
    atomic_store_explicit(&split_published_enabled,split_enabled,memory_order_relaxed);
  }
  if(!split_on())return;
  if(split_depth)vk_image_split_break();
  if(!split_depth) {split_admitted=NULL;split_group.consumed=0;split_group.hot=0;}
  split_depth++;
}
void vk_image_split_draw_end(void) {
  if(!split_on())return;
  if(!split_depth)die("Image split draw scope underflow");
  if(!--split_depth) {
    split_admitted=NULL;split_group.capture=-1;
    if(split_group.building) {split_group.building=0;split_group.valid=0;}
  }
}
void vk_image_split_admit(const void *shader,int eligible) {
  if(split_on() && split_depth==1)split_admitted=eligible?shader:NULL;
}
static int split_ready(const void *shader) {
  return split_on() && split_depth==1 && split_admitted==shader && shader &&
    split_group.shader==shader && split_group.valid &&
    split_group.serial==vk_recording_serial() && split_group.pass==V.passes &&
    vk_render_active() && !vk_mip_clear_pending() &&
    split_group.lifetime==image_recipe_generation &&
    split_group.descriptors==atomic_load(&descriptor_epoch) &&
    split_group.samplers==atomic_load(&sampler_registry_epoch);
}
int vk_image_split_prepare(const void *shader,uint64_t skip[2]) {
  if(!split_ready(shader))return 0;
  skip[0]=split_group.mask[0];skip[1]=split_group.mask[1];return 1;
}
static void split_build(const void *shader,unsigned nb) {
  split_expand(); // no active borrower can see the member vector overwritten
  if(!++split_group.revision)die("Image split revision overflow");
  split_group.shader=shader;split_group.nb=nb;
  split_group.mask[0]=split_group.mask[1]=0;
  split_group.textures[0]=split_group.textures[1]=0;
  split_group.sampler_slots[0]=split_group.sampler_slots[1]=0;
  split_group.count=0;split_group.valid=0;split_group.building=1;
  split_group.failed=0;split_group.hot=0;split_group.capture=-1;
  split_group.serial=vk_recording_serial();split_group.pass=V.passes;
  split_group.lifetime=image_recipe_generation;
  split_group.descriptors=atomic_load(&descriptor_epoch);
  split_group.samplers=atomic_load(&sampler_registry_epoch);
  split_counts[0]++;
}
vk_split_descriptor_value *vk_image_split_values(const void *shader,unsigned nb,uint64_t skip[2]) {
  if(!split_on() || split_depth!=1 || !shader || split_admitted!=shader ||
      !image_binding_active || nb>128 || !vk_render_active())return NULL;
  if(split_ready(shader)) {
    skip[0]=split_group.mask[0];skip[1]=split_group.mask[1];
    split_group.hot=1;split_group.consumed=1;
    image_batch_current.split_read_group=1;
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
    if(split_test_callback && !split_test_dependency_phase) {
      void (*callback)(void *)=split_test_callback;void *context=split_test_context;
      split_test_callback=NULL;split_test_context=NULL;callback(context);
    }
#endif
  } else split_build(shader,nb);
  return split_group.values;
}
int vk_image_split_retry(uint64_t retry[2]) {
  if(!split_on() || split_depth!=1 || !split_group.hot)return 0;
  split_group.hot=0;
  if(split_ready(split_group.shader))return 0;
  // A callback happened inside the mixed descriptor consumer, before set
  // creation. Restore only skipped image values through their old consumers.
  // The original request batch was materialized BEFORE that callback; keep
  // those conservative histories, including retired physical owners.
  split_expand();
  retry[0]=split_group.mask[0];retry[1]=split_group.mask[1];
  split_group.capture=-1;split_group.building=0;split_group.valid=0;
  return retry[0] || retry[1];
}
void vk_image_split_capture_begin(unsigned binding,unsigned slot,int sampler_slot) {
  if(!split_on() || split_depth!=1 || !split_group.building || split_group.failed)return;
  split_group.capture=binding<128?(int)binding:-1;
  split_group.capture_slot=slot;split_group.capture_sampler=sampler_slot;
}
void vk_image_split_capture_end(void) {if(split_on())split_group.capture=-1;}
static void image_split_capture(image *i,VkImageSubresourceRange r,VkPipelineStageFlags stages,
                                int storage,const view_t *view) {
  if(!split_on() || split_depth!=1 || !split_group.building || split_group.failed || split_group.capture<0)return;
  unsigned n=split_group.capture,slot=split_group.capture_slot;
  int sampler=split_group.capture_sampler;
  split_group.capture=-1;
  if(slot>=128 || i->tex.has_layout || i->external_access || storage ||
      stages!=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT ||
      (view && view->set_format && view->format!=i->tex.d.format) ||
      sampled_target_alias(&i->tex,view))return;
#ifdef DM_VULKAN_NATIVE_RESOURCES
  if(i->native_entry)return;
#endif
  // shared_alias_linked is a logical NVN export/conflict registration, not a
  // physical VkImage import. It is intentionally NOT an exclusion here.
  if(split_group.count==128)die("Image split group capacity");
  split_group.requests[split_group.count++]=(image_request){i,r,stages,VK_ACCESS_SHADER_READ_BIT,0};
  split_group.mask[n/64]|=UINT64_C(1)<<(n%64);
  split_group.textures[slot/64]|=UINT64_C(1)<<(slot%64);
  if(sampler>=0 && sampler<128)split_group.sampler_slots[sampler/64]|=UINT64_C(1)<<(sampler%64);
  i->split_group_revision=split_group.revision;
}
static void image_split_record_done(void) {
  if(!split_on() || split_depth!=1)return;
  // Count only successful draws that retained their group through all late
  // callbacks. Selection followed by fallback is not a saved-work hit.
  if(split_group.consumed && image_batch_current.split_read_group && split_ready(split_group.shader)) {
    split_counts[1]++;split_counts[2]+=split_group.count;
  }
  if(!split_group.building)return;
  split_group.building=0;
  // Publish only after actual draw access recording. The first read stamps
  // use_clock/mip clocks after this physical pass's begin cutoff; repeated
  // reads in this same pass preserve every >begin classification. Pass/serial
  // changes invalidate rather than extending this proof across a boundary.
  split_group.valid=!split_group.failed && split_group.count &&
    split_group.serial==vk_recording_serial() && split_group.pass==V.passes &&
    vk_render_active() && !vk_mip_clear_pending() &&
    split_group.lifetime==image_recipe_generation &&
    split_group.descriptors==atomic_load(&descriptor_epoch) &&
    split_group.samplers==atomic_load(&sampler_registry_epoch);
}
void vk_image_split_handles(uint32_t op,unsigned stage,unsigned first,unsigned count,const uint64_t *values) {
  if(!split_on() || stage!=1 || !split_group.shader ||
      (op!=OP_BIND_TEX && op!=OP_BIND_SAMP))return;
  const uint64_t *mask=op==OP_BIND_TEX?split_group.textures:split_group.sampler_slots;
  const uint64_t *old=op==OP_BIND_TEX?S.tex[1]:S.samp[1];
  for(unsigned k=0;k<count;k++) {
    unsigned slot=first+k;
    if((mask[slot/64]&(UINT64_C(1)<<(slot%64))) && old[slot]!=values[k]) {vk_image_split_break();break;}
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
