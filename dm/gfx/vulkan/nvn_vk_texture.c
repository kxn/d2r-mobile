// 原 NVN 贴图快照对应 Vulkan 图像，复用原项目 Tegra 解排布与 BC 解压。
// Map original NVN texture snapshots to Vulkan images, reusing the project's
// Tegra deswizzle and BC decoding.
#include "../nvn/nvn_dirty.h"
#include "../nvn/nvn_native_layout.h"
#include "../tex/nvn_bcdec.h"
#include "../tex/nvn_deswizzle.h"
#include "../tex/nvn_astc_metadata.h"
#include "nvn_vk_shader.h"
#include "nvn_vk_extrap.h"
#include "nvn_vk_present_shader.h"
#include "nvn_vk_pixel_local_runtime.h"
#include <math.h>
#include <stdatomic.h>
#include <unistd.h>
#ifdef DM_VULKAN_SHARED_POOLS
#include "nvn_vk_alias_interval.h"
#endif
#ifdef DM_VULKAN_NATIVE_RESOURCES
#include <sys/mman.h>
#endif
typedef struct image_view {
  struct image_view *next;
  VkImageViewCreateInfo description;
  VkImageView handle;
  VkImageUsageFlags usage;
  int astc_decode8;
} image_view;
typedef struct {
  VkPipelineStageFlags readers, writers, visible;
  VkPipelineStageFlags layout_ordered;
  VkAccessFlags writes,layout_ordered_access;
  uint64_t raster_pass;
  VkPipelineStageFlags operation_writers;
  uint64_t write_clock;
  VkImageLayout mip_layout;
  VkPipelineStageFlags mip_scope;
  uint64_t use_clock;
} image_access;
typedef struct {
  VkImageSubresourceRange range;
  VkPipelineStageFlags readers,visible;
  uint64_t generation;
  // Actual last full-range mip stamp, not a new visibility proof. Only its
  // relation to the current physical pass cutoff permits omitting restamps.
  uint64_t mip_stamp;
} image_read_proof;
typedef struct image {
  struct image *next;
  struct image *hash_next;
  tex_t tex;
  VkImage handle;
  VkDeviceMemory memory;
  void *allocation;
  uint64_t allocation_bytes;
  int accounted_rebuildable;
  struct image *list_prev;
  VkFormat format;
  VkImageCreateInfo description;
  VkExtent3D extent;
  uint32_t levels, layers, bpp;
  VkImageAspectFlags aspect;
  uint64_t stamp;
  uint64_t checked_dirty_epoch,checked_dirty_stamp;
  nvn_dirty_range_proof checked_dirty_range;
#ifdef DM_VULKAN_SHARED_POOLS
  nvn_dirty_scope *shared_dirty_scope;
  struct image *shared_alias_next;
  alias_range_node shared_alias_range;
  int shared_alias_linked;
  uint32_t shared_writer; // 1 generic GPU access, 2 B2T, 3 T2T
  uint64_t shared_writer_source;
  NVNcopyRegion shared_writer_region;
#endif
#ifdef DM_VULKAN_NATIVE_RESOURCES
  const nvn_native_entry *native_entry;
  void *native_pool;
  nvn_dirty_scope *native_write_scopes[NVN_NATIVE_LEVELS];
  struct image *native_next;
  uint64_t native_use_serial,native_alias_generation;
  VkPipelineStageFlags native_alias_visible_stages;
  VkPipelineStageFlags native_recorded_stages;
  // 0 external, 1 transient chunk owner, 2 coherent same-queue resident.
  // Only state 1 enters native_acquired_images; it is not a lifetime reference.
  int native_acquired;
#endif
  uint64_t last_use;
  uint64_t trace_stamp, trace_levels;
  int initialized, decoded;
  int astc_copy_encoding; // upstream ASTC-pack copy: logical BC3, physical ASTC4
  int bc_copy_decoded; // mixed compressed copies: logical BC3, physical RGBA8
  uint64_t astc_copy_stamp; // CPU observer only; never a canonical upload stamp
  // A complete CPU upload can rebuild this physical image. Any later GPU
  // writer revokes the proof, even if its result happens to equal guest bytes.
  int cpu_rebuildable, external_access;
  int astc_ldr_proven;
  uint32_t zero_mips;
  VkImageLayout layout;
  VkPipelineStageFlags layout_scope;
  uint64_t use_clock;
  uint64_t general_submit;
  image_view *views;
  image_access *access;
  uint64_t access_generation;
  image_read_proof read_proofs[2];
  unsigned read_proof_next;
  unsigned physical_traits;
} image;
// Physical VMA allocations only: imported native pages are already the guest
// Pool, not a second private texture allocation. Completion can free promoted
// storage while the recording owner manages the live candidate cursor.
static _Atomic uint64_t private_image_bytes;
static _Atomic uint64_t rebuildable_private_bytes,private_cache_released_bytes;
static image *private_image_cursor;
// Provisional candidate policy, pending original-game footprint review. This
// limits only live CPU-rebuildable private cache allocations, not RTs/native.
static uint64_t private_image_budget=512ull<<20;
static void image_rebuildable_set(image *i,int rebuildable) {
  i->cpu_rebuildable=rebuildable;
  int account=!!rebuildable && !!i->allocation_bytes;
  if(account==i->accounted_rebuildable)return;
  i->accounted_rebuildable=account;
  if(account)atomic_fetch_add(&rebuildable_private_bytes,i->allocation_bytes);
  else {
    uint64_t prior=atomic_fetch_sub(&rebuildable_private_bytes,i->allocation_bytes);
    if(prior<i->allocation_bytes)die("Rebuildable private image accounting underflow");
  }
}
static void image_replacement_detach(image *i,image *old) {
  // old retains the physical bytes until completion, but is no longer a live
  // cache candidate. The replacement will admit its own allocation separately.
  if(old->accounted_rebuildable) {
    uint64_t prior=atomic_fetch_sub(&rebuildable_private_bytes,old->allocation_bytes);
    if(prior<old->allocation_bytes)die("Replaced private image accounting underflow");
    old->accounted_rebuildable=0;
  }
  old->list_prev=NULL;i->allocation_bytes=0;i->accounted_rebuildable=0;
}
enum {
  IMAGE_MIP_TRACKED=1u<<0,
  IMAGE_LAYOUT_COLOR=1u<<1,
  IMAGE_LAYOUT_SAMPLED_ARRAY=1u<<2,
  IMAGE_LAYOUT_DEPTH=1u<<3,
};
// Recording-owner identity generation for logical image destruction and real
// physical replacement. Reject freed owners and handle-reuse ABA before any
// cached pointer dereference. Completion only frees already-invalidated copies.
static uint64_t image_recipe_generation=1;
static void image_recipes_changed(void) {
  if(++image_recipe_generation==0) die("Vulkan image recipe generation overflow");
}
// Content proof belongs to actual physical image bytes, not a filename,
// format hint, current scene, or CPU address. Every physical writer revokes it.
static void image_astc_proof(image *i,int proven) {
  proven=!!proven && !i->external_access;
  if(i->astc_ldr_proven!=proven) {
    i->astc_ldr_proven=proven;
    image_recipes_changed();
  }
}
static int astc_unorm_format(VkFormat f) {
  return f>=VK_FORMAT_ASTC_4x4_UNORM_BLOCK && f<=VK_FORMAT_ASTC_12x12_UNORM_BLOCK &&
      !((f-VK_FORMAT_ASTC_4x4_UNORM_BLOCK)&1);
}
static int image_astc_decode8(const image *i,const VkImageViewCreateInfo *v,VkImageUsageFlags usage) {
  return V.astc_decode_mode && i->astc_ldr_proven && !i->external_access &&
      usage==VK_IMAGE_USAGE_SAMPLED_BIT && v->format==i->format &&
      astc_unorm_format(v->format) && v->viewType==VK_IMAGE_VIEW_TYPE_2D;
}
static uint32_t zero_mip_mask(unsigned first,unsigned count) {
  if(first>=16 || !count || count>16-first) return 0;
  return ((1u<<count)-1)<<first;
}
static unsigned zero_channels(const image *i) {
  if(i->description.imageType!=VK_IMAGE_TYPE_2D || i->layers!=1 ||
      i->extent.depth!=1 || i->levels>16 || i->description.samples!=VK_SAMPLE_COUNT_1_BIT)
    return 0;
  if(i->format==VK_FORMAT_B10G11R11_UFLOAT_PACK32) return 7;
  return i->format==VK_FORMAT_R8G8B8A8_UNORM || i->format==VK_FORMAT_R8G8B8A8_SRGB?15:0;
}
// Keep complex arrays, depth/stencil and multisampling on the existing
// conservative path. Tracked mip layouts remain valid across policy switches.
static int image_mip_tracked(const image *i) {
  return !!(i->physical_traits&IMAGE_MIP_TRACKED);
}
#ifdef DM_VULKAN_SHARED_POOLS
static image *shared_gpu_aliases;
static alias_range_node *shared_gpu_alias_ranges;
#include "nvn_vk_alias_negative.h"
static alias_negative_cache shared_alias_negative={.epoch=1};
static unsigned astc_copy_live;
static void image_shared_alias_remove(image *i) {
  if(!i->shared_alias_linked)return;
  image **at=&shared_gpu_aliases;while(*at && *at!=i)at=&(*at)->shared_alias_next;
  if(!*at)die("Shared GPU image alias list lifetime");
  int removed=0;
  shared_gpu_alias_ranges=alias_range_remove(shared_gpu_alias_ranges,&i->shared_alias_range,&removed);
  if(!removed)die("Shared GPU image alias index lifetime");
  alias_negative_changed(&shared_alias_negative);
  i->shared_alias_range=(alias_range_node){0};
  *at=i->shared_alias_next;i->shared_alias_next=NULL;i->shared_alias_linked=0;
}
static void image_shared_alias_write(image *i) {
#ifdef DM_VULKAN_NATIVE_RESOURCES
  if(i->native_entry)return; // backing is already canonical; no Tegra conversion
#endif
  int shared=vk_pool_shared_active(),legacy=vk_legacy_astc_copy_mode();
  if((!shared && !legacy) || i->shared_alias_linked)return;
  // Cold first-writer admission only. A later physical alias cannot silently
  // give a propagated array a second, incompatible logical BC3 encoding.
  if(astc_copy_live || i->astc_copy_encoding || i->bc_copy_decoded)for(image *a=shared_gpu_aliases;a;a=a->shared_alias_next)
    if(a!=i && (a->astc_copy_encoding || i->astc_copy_encoding || a->bc_copy_decoded || i->bc_copy_decoded) && a->tex.mem && i->tex.mem) {
      uint64_t x=(uintptr_t)i->tex.mem,y=(uintptr_t)a->tex.mem;
      if(y<=x?x-y<a->tex.L.size:y-x<i->tex.L.size)
        die("ASTC propagated image conflicts with another optimal-image writer identity");
    }
  // Legacy raw guards concern only propagated encodings. Do not turn the
  // list into a scan of every ordinary render target on every Buffer binding.
  if(!shared && !i->astc_copy_encoding)return;
  i->shared_alias_range=(alias_range_node){.start=(uintptr_t)i->tex.mem,
      .end=(uintptr_t)i->tex.mem+i->tex.L.size,.serial=i->tex.serial,.owner=i,.height=1};
  i->shared_alias_range.max_end=i->shared_alias_range.end;
  int inserted=0;
  shared_gpu_alias_ranges=alias_range_insert(shared_gpu_alias_ranges,&i->shared_alias_range,&inserted);
  if(!inserted)die("Shared GPU image alias index duplicate");
  alias_negative_changed(&shared_alias_negative);
  i->shared_alias_linked=1;i->shared_alias_next=shared_gpu_aliases;shared_gpu_aliases=i;
}
#endif
#ifdef DM_VULKAN_NATIVE_RESOURCES
static void native_image_prepare(image *,VkImageSubresourceRange,VkPipelineStageFlags,VkAccessFlags);
static void native_image_record(image *,VkImageSubresourceRange,VkPipelineStageFlags,VkAccessFlags);
#endif
#include "nvn_vk_image_dependencies.inc"
static _Atomic int layout_requested=-1,layout_active=0;
static int layout_reference=0;
// This comparison changes only sampled-only color arrays. The recording
// owner snapshots requests at submit begin; bindings never read atomics.
static _Atomic int sampled_array_layout_requested=0,sampled_array_layout_active=0;
static int sampled_array_layout_reference=0;
void vk_sampled_array_layout_reference(int reference) {
  atomic_store(&sampled_array_layout_requested,!!reference);
}
int vk_sampled_array_layout_reference_state(void) {
  return atomic_load(&sampled_array_layout_active);
}
static _Atomic int depth_layout_requested=-1;
static int depth_layout_reference=-1;
static _Atomic int readonly_depth_requested=-1;
static int readonly_depth_reference=-1;
static _Atomic uint64_t readonly_depth_attachments;
void vk_readonly_depth_reference(int reference) { atomic_store(&readonly_depth_requested,!!reference); }
void vk_readonly_depth_stats(uint64_t *uses,int *reference) {
  *uses=atomic_load(&readonly_depth_attachments);*reference=readonly_depth_reference;
}
static _Atomic int compute_layout_requested=-1;
static int compute_layout_reference=-1;
static _Atomic uint64_t compute_optimal_samples;
// Diagnostics only: the single recording owner accumulates totals locally.
// Readers observe the published atomic snapshot from the last commit, never
// the mutable recording state. Resource dependency generations are separate.
static struct {
  uint64_t transitions,safe,fallback,compute,depth_attachment,depth_sampled;
} image_layout_recorded;
void vk_compute_layout_stats(uint64_t *samples,int *reference) {
  *samples=atomic_load(&compute_optimal_samples);*reference=compute_layout_reference;
}
void vk_compute_layout_reference(int reference) { atomic_store(&compute_layout_requested,!!reference); }
static _Atomic uint64_t depth_attachment_uses,depth_sampled_uses;
void vk_depth_layout_stats(uint64_t *attachments,uint64_t *sampled,int *reference) {
  *attachments=atomic_load(&depth_attachment_uses);*sampled=atomic_load(&depth_sampled_uses);
  *reference=depth_layout_reference;
}
void vk_depth_layout_reference(int reference) { atomic_store(&depth_layout_requested,!!reference); }
static void depth_layout_init(void) {
  if(depth_layout_reference>=0) return;
  readonly_depth_reference=atomic_load(&readonly_depth_requested);
  if(readonly_depth_reference<0) {
    const char *v=getenv("DM_VULKAN_READONLY_DEPTH_REFERENCE");
    if(v&&strcmp(v,"0")&&strcmp(v,"1")) die("DM_VULKAN_READONLY_DEPTH_REFERENCE expects 0 or 1");
    readonly_depth_reference=v&&!strcmp(v,"1");
  }
  depth_layout_reference=atomic_load(&depth_layout_requested);
  if(depth_layout_reference<0) {
    const char *v=getenv("DM_VULKAN_DEPTH_LAYOUT_REFERENCE");
    if(v&&strcmp(v,"0")&&strcmp(v,"1")) die("DM_VULKAN_DEPTH_LAYOUT_REFERENCE expects 0 or 1");
    depth_layout_reference=v&&!strcmp(v,"1");
  }
  LOG("[vulkan] depth/stencil layouts: %s (fixed for this process)",
      depth_layout_reference?"GENERAL reference":"attachment/read-only optimal");
}
static void compute_layout_init(void) {
  if(compute_layout_reference>=0) return;
  compute_layout_reference=atomic_load(&compute_layout_requested);
  if(compute_layout_reference<0) {
    const char *v=getenv("DM_VULKAN_COMPUTE_LAYOUT_REFERENCE");
    if(v&&strcmp(v,"0")&&strcmp(v,"1")) die("DM_VULKAN_COMPUTE_LAYOUT_REFERENCE expects 0 or 1");
    compute_layout_reference=v&&!strcmp(v,"1");
  }
  LOG("[vulkan] compute sampled layouts: %s (fixed for this process)",
      compute_layout_reference?"GENERAL reference":"read-only optimal");
}
static _Atomic int storage_usage_requested=-1;
static int storage_usage_reference=-1;
static _Atomic int attachment_usage_requested=-1;
static int attachment_usage_reference=-1;
void vk_attachment_usage_reference(int reference) {
  atomic_store(&attachment_usage_requested,!!reference);
}
static int attachment_usage_mode(void) {
  if(attachment_usage_reference<0) {
    attachment_usage_reference=atomic_load(&attachment_usage_requested);
    if(attachment_usage_reference<0) {
      const char *v=getenv("DM_VULKAN_ATTACHMENT_USAGE_REFERENCE");
      if(v&&strcmp(v,"0")&&strcmp(v,"1")) die("DM_VULKAN_ATTACHMENT_USAGE_REFERENCE expects 0 or 1");
      attachment_usage_reference=v&&!strcmp(v,"1");
    }
    LOG("[vulkan] color attachment usage: %s (fixed for this process)",
        attachment_usage_reference?"format capability reference":"actual attachment only");
  }
  return attachment_usage_reference;
}
void vk_storage_usage_reference(int reference) {
  atomic_store(&storage_usage_requested,!!reference);
}
// Physical usage is immutable. Latch this comparison policy once, rather than
// changing existing images or inspecting environment variables on each bind.
static int storage_usage_mode(void) {
  if(storage_usage_reference<0) {
    storage_usage_reference=atomic_load(&storage_usage_requested);
    if(storage_usage_reference<0) {
      const char *v=getenv("DM_VULKAN_STORAGE_USAGE_REFERENCE");
      if(v&&strcmp(v,"0")&&strcmp(v,"1")) die("DM_VULKAN_STORAGE_USAGE_REFERENCE expects 0 or 1");
      storage_usage_reference=v&&!strcmp(v,"1");
    }
    LOG("[vulkan] image storage usage: %s (fixed for this process)",
        storage_usage_reference?"NVN IMAGE hint reference":"deferred until actual binding");
  }
  return storage_usage_reference;
}
static _Atomic uint64_t layout_transitions,layout_safe_uses,layout_fallbacks;
static _Atomic uint64_t zero_copy_skips;
uint64_t vk_texture_zero_copy_skipped(void) { return atomic_load(&zero_copy_skips); }
void vk_image_layout_reference(int reference) { atomic_store(&layout_requested,!!reference); }
void vk_image_layout_stats(uint64_t *transitions,uint64_t *safe,uint64_t *fallback,int *reference) {
  *transitions=atomic_load(&layout_transitions);*safe=atomic_load(&layout_safe_uses);
  *fallback=atomic_load(&layout_fallbacks);*reference=atomic_load(&layout_active);
}
// Single-layer color keeps bounded mip tracking. Single-mip depth/stencil
// arrays and bounded sampled-only color arrays use a whole-image layout;
// access hazards still track every layer. Actual storage/transfer users and
// attachment feedback keep GENERAL. Other complex images stay conservative.
// This bounded policy only changes the whole-image sampling layout. Arrays
// keep per-cell access hazards and must never enter single-layer mip tracking.
// A later real storage/attachment use promotes physical usage and exits it.
// Physical properties change only at image_allocate, including a promotion.
// Policy switches and this submit's GENERAL requirement stay live below.
static unsigned image_physical_traits(const image *i) {
  unsigned traits=0;
  if(i->description.imageType==VK_IMAGE_TYPE_2D && i->description.samples==VK_SAMPLE_COUNT_1_BIT) {
    if(i->aspect==VK_IMAGE_ASPECT_COLOR_BIT) {
      if(i->levels<=16 && i->layers==1)traits|=IMAGE_LAYOUT_COLOR;
      if(i->levels>1 && i->levels<=16 && i->layers==1)traits|=IMAGE_MIP_TRACKED;
    } else if(i->levels==1 && (i->layers==1 ||
        ((i->aspect&VK_IMAGE_ASPECT_DEPTH_BIT) && i->layers>1 && i->layers<=16)))traits|=IMAGE_LAYOUT_DEPTH;
  }
  const VkImageUsageFlags writers=VK_IMAGE_USAGE_STORAGE_BIT |
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  if(i->tex.d.target==NVN_TEXTURE_TARGET_2D_ARRAY &&
      i->description.imageType==VK_IMAGE_TYPE_2D &&
      i->description.samples==VK_SAMPLE_COUNT_1_BIT &&
      i->aspect==VK_IMAGE_ASPECT_COLOR_BIT && i->levels && i->levels<=16 &&
      i->layers>1 && i->layers<=256 &&
      (i->description.usage&VK_IMAGE_USAGE_SAMPLED_BIT) &&
      !(i->description.usage&writers))traits|=IMAGE_LAYOUT_SAMPLED_ARRAY;
  return traits;
}
static int image_sampled_array_layout(const image *i) {
  return !!(i->physical_traits&IMAGE_LAYOUT_SAMPLED_ARRAY);
}
static int image_safe_layouts(const image *i) {
#ifdef DM_VULKAN_NATIVE_RESOURCES
  // Authenticated native payloads are produced and handed off in GENERAL.
  // Keep their sampled/transfer descriptors in that layout between FOREIGN
  // acquire/release. Access dependencies and ownership handoffs still run;
  // ordinary private images retain the original optimal-layout policy.
  if(i->native_entry)return 0;
#endif
  return !layout_reference &&
      ((i->physical_traits&IMAGE_LAYOUT_COLOR) ||
       (!sampled_array_layout_reference && (i->physical_traits&IMAGE_LAYOUT_SAMPLED_ARRAY)) ||
       (!depth_layout_reference && (i->physical_traits&IMAGE_LAYOUT_DEPTH)));
}
static void image_whole_layout_access(image *i,VkImageLayout next,
    VkPipelineStageFlags dst_stage,VkAccessFlags dst_access) {
  if(i->layout==next) return;
  // A newly sampled image last accessed before this physical pass can have
  // its transition recorded on the pending primary, before BeginRenderPass.
  // Never move a transition across any real use of this image in the pass.
  VkCommandBuffer command=VK_NULL_HANDLE;
  if((next==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL || next==VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) &&
      dst_stage==VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT &&
      i->use_clock<=image_pass_begin_clock)
    command=vk_render_pending_primary();
  if(!command) {
    // Ending secondary recording can add upstream drag-follow replay users.
    vk_render_end();
    if(i->layout==next) return;
    command=V.cmd;
  }
  VkPipelineStageFlags src_stage=i->layout_scope;
  VkAccessFlags src_access=0;
  for(unsigned a=0;a<3;a++) if(i->aspect&(1u<<a))
    for(unsigned level=0;level<i->levels;level++)
      for(unsigned layer=0;layer<i->layers;layer++) {
        const image_access *c=image_cell(i,a,level,layer);
        src_stage |= c->readers|c->writers;
        src_access |= c->writes;
      }
  if(!src_stage) src_stage=VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  // Transitions are synchronization operations, not shader writes. Chain the
  // previous transition's destination scope and retain actual write history.
  VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask=src_access,.dstAccessMask=dst_access,
      .oldLayout=i->layout,.newLayout=next,
      .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
      .image=i->handle,.subresourceRange={i->aspect,0,i->levels,0,i->layers}};
  vkCmdPipelineBarrier(command,src_stage,dst_stage,0,0,NULL,0,NULL,1,&barrier);
  i->layout=next;i->layout_scope=dst_stage;i->access_generation++;
  for(unsigned a=0;a<3;a++) if(i->aspect&(1u<<a))
    for(unsigned level=0;level<i->levels;level++)
      for(unsigned layer=0;layer<i->layers;layer++) {
        image_access *c=image_cell(i,a,level,layer);
        c->readers=0;c->raster_pass=0;
        c->visible=dst_stage;
        // Only the first actual operation can consume this complete ordering
        // proof. Recording any subsequent access clears it for this cell.
        c->layout_ordered=dst_stage;
        c->layout_ordered_access=dst_access;
      }
  image_layout_recorded.transitions++;
}
static void image_range_layout_access_normalized(image *i,VkImageSubresourceRange r,VkImageLayout next,
    VkPipelineStageFlags dst_stage,VkAccessFlags dst_access) {
  if(!image_mip_tracked(i)) { image_whole_layout_access(i,next,dst_stage,dst_access);return; }
  // A concrete whole-image layout summarizes every mip; mixed layouts use
  // MAX_ENUM. Repeated bindings need no per-mip scan when that proof matches.
  // Access dependencies still run separately for every operation.
  if(i->layout==next) return;
  int changed=0,precedes_pass=1;
  for(unsigned level=r.baseMipLevel;level<r.baseMipLevel+r.levelCount;level++) {
    image_access *c=image_cell(i,0,level,0);
    if(c->mip_layout!=next) {
      changed=1;
      if(c->use_clock>image_pass_begin_clock) precedes_pass=0;
    }
  }
  if(!changed) return;
  VkCommandBuffer command=VK_NULL_HANDLE;
  if(precedes_pass && next==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
      dst_stage==VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
    command=vk_render_pending_primary();
  if(!command) { vk_render_end();command=V.cmd; }
  for(unsigned level=r.baseMipLevel;level<r.baseMipLevel+r.levelCount;level++) {
    image_access *c=image_cell(i,0,level,0);
    if(c->mip_layout==next) continue;
    VkPipelineStageFlags src=c->mip_scope|c->readers|c->writers;
    if(!src) src=VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask=c->writes,.dstAccessMask=dst_access,
      .oldLayout=c->mip_layout,.newLayout=next,
      .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
      .image=i->handle,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,level,1,0,1}};
    vkCmdPipelineBarrier(command,src,dst_stage,0,0,NULL,0,NULL,1,&b);
    c->mip_layout=next;c->mip_scope=dst_stage;
    c->readers=0;c->raster_pass=0;c->visible=dst_stage;c->layout_ordered=dst_stage;
    c->layout_ordered_access=dst_access;
    i->access_generation++;
    image_layout_recorded.transitions++;
  }
  // Raw image consumers first normalize the whole image. A mixed-layout image
  // must never silently advertise GENERAL as if all its mips were in it.
  i->layout=image_cell(i,0,0,0)->mip_layout;
  for(unsigned level=1;level<i->levels;level++)
    if(image_cell(i,0,level,0)->mip_layout!=i->layout) { i->layout=VK_IMAGE_LAYOUT_MAX_ENUM;break; }
}
static void image_range_layout_access(image *i,VkImageSubresourceRange r,VkImageLayout next,
    VkPipelineStageFlags stages,VkAccessFlags access) {
  if(image_mip_tracked(i))r=image_access_range(i,r);
  image_range_layout_access_normalized(i,r,next,stages,access);
}
static void image_layout_access(image *i,VkImageLayout next,
    VkPipelineStageFlags stages,VkAccessFlags access) {
  image_range_layout_access(i,(VkImageSubresourceRange){i->aspect,0,i->levels,0,i->layers},
      next,stages,access);
}
static void image_layout(image *i,VkImageLayout next) {
  image_layout_access(i,next,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
      VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT);
}
static void image_general_access(image *i,VkPipelineStageFlags stages,VkAccessFlags access) {
  i->general_submit=vk_recording_serial();
  image_layout_access(i,VK_IMAGE_LAYOUT_GENERAL,stages,access);
}
static void image_general(image *i) {
  image_general_access(i,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
      VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT);
}
static void image_general_transfer(image *i) {
  image_general_access(i,VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT);
}
static VkImageLayout image_use_range_normalized(image *i,VkImageSubresourceRange r,VkImageLayout preferred,
                                    VkPipelineStageFlags shader_stages) {
  int sampled_array=image_sampled_array_layout(i);
  // Transfer-source requests use image_use rather than image_general_transfer.
  // Keep this array's complete layout GENERAL for the rest of that submit too.
  if(sampled_array && preferred!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    i->general_submit=vk_recording_serial();
  int safe=image_safe_layouts(i) && i->general_submit!=vk_recording_serial() &&
      (!sampled_array || preferred==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  int color=preferred==VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  int depth=color && i->aspect!=VK_IMAGE_ASPECT_COLOR_BIT;
  int depth_read=preferred==VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL &&
      shader_stages==(VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
  if(depth) preferred=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  else if(i->aspect!=VK_IMAGE_ASPECT_COLOR_BIT && preferred==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    preferred=VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
  int transfer=preferred==VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  VkPipelineStageFlags stages=transfer?VK_PIPELINE_STAGE_TRANSFER_BIT:
      depth?VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT:
      color?VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT:
      shader_stages?shader_stages:VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  VkAccessFlags access=transfer?VK_ACCESS_TRANSFER_READ_BIT:
      // Read-only depth/stencil tests still end with this backend's STORE
      // operation. Its late-test write must be in the transition destination
      // scope; shader sampling alone below remains a read-only consumer.
      depth_read?VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT:
      depth?VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT:
      color?VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT:
      VK_ACCESS_SHADER_READ_BIT;
  VkImageLayout next=safe?preferred:VK_IMAGE_LAYOUT_GENERAL;
  // Mipped arrays have one concrete layout for every layer/mip. Their cell
  // mip_layout fields are intentionally unused; do not route through the
  // single-layer per-mip representation even when the view selects one cell.
  if(sampled_array) image_whole_layout_access(i,next,stages,access);
  else image_range_layout_access_normalized(i,r,next,stages,access);
  if(safe && shader_stages==VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT && !color && !transfer)
    image_layout_recorded.compute++;
  if(safe && i->aspect!=VK_IMAGE_ASPECT_COLOR_BIT) {
    if(depth || depth_read) image_layout_recorded.depth_attachment++;
    else if(preferred==VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) image_layout_recorded.depth_sampled++;
  }
  if(safe)image_layout_recorded.safe++;else image_layout_recorded.fallback++;
  return next;
}
static VkImageLayout image_use_range(image *i,VkImageSubresourceRange r,VkImageLayout preferred,
    VkPipelineStageFlags stages) {
  if(image_mip_tracked(i))r=image_access_range(i,r);
  return image_use_range_normalized(i,r,preferred,stages);
}
static void image_use(image *i,VkImageLayout preferred) {
  image_use_range(i,(VkImageSubresourceRange){i->aspect,0,i->levels,0,i->layers},preferred,0);
}
static int sampled_target_alias(const tex_t *texture,const view_t *sample_view) {
  for(int k=0;k<=S.targets.n;k++) {
    const tex_t *target=k==S.targets.n?S.targets.depth:S.targets.color[k];
    if(!target) continue;
    if(texture->serial==target->serial) {
      // A sampled view restricted to other mips is not attachment feedback.
      // Only the bounded, single-layer 2D path can exploit this distinction.
      if(texture->d.target==NVN_TEXTURE_TARGET_2D && texture->L.layers==1 &&
          texture->d.samples<=1 && texture->d.levels>1 && texture->d.levels<=16) {
        const view_t *v=k==S.targets.n?(S.targets.has_dview?&S.targets.dview:NULL):
            (S.targets.has_cview[k]?&S.targets.cview[k]:NULL);
        int attached=v&&v->set_levels?v->base_level:0;
        int first=sample_view&&sample_view->set_levels?sample_view->base_level:0;
        int count=sample_view&&sample_view->set_levels&&sample_view->nlevels?
            sample_view->nlevels:texture->d.levels-first;
        if(first>=0 && count>0 && first<=texture->d.levels &&
            count<=texture->d.levels-first && attached>=0 && attached<texture->d.levels &&
            (attached<first || attached-first>=count)) continue;
      }
      return 1;
    }
    if(texture->mem && target->mem && texture->L.size && target->L.size &&
        (uintptr_t)texture->mem<(uintptr_t)target->mem+target->L.size &&
        (uintptr_t)target->mem<(uintptr_t)texture->mem+texture->L.size) return 1;
  }
  return 0;
}
static VkImageView cached_view(image *i,
                               const VkImageViewCreateInfo *description,
                               VkImageUsageFlags usage) {
  int decode8=image_astc_decode8(i,description,usage);
  for (image_view *v = i->views; v; v = v->next)
    if (v->astc_decode8==decode8 && v->usage == usage && v->description.viewType == description->viewType &&
        v->description.format == description->format &&
        !memcmp(&v->description.components, &description->components,
                sizeof description->components) &&
        !memcmp(&v->description.subresourceRange,
                &description->subresourceRange,
                sizeof description->subresourceRange))
      return v->handle;
  image_view *v = calloc(1, sizeof *v);
  if (!v)
    die("Vulkan cached image-view allocation");
  v->description = *description;
  // Never persist a stack pNext in the view/descriptor recipes.
  v->description.pNext=NULL;
  v->usage = usage;
  v->astc_decode8=decode8;
  VkImageViewUsageCreateInfo use = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO,
                                   .usage = usage};
  VkImageViewCreateInfo create = *description;
  VkImageViewASTCDecodeModeEXT decode={
      .sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_ASTC_DECODE_MODE_EXT,
      .pNext=&use,.decodeMode=VK_FORMAT_R8G8B8A8_UNORM};
  create.pNext = decode8?(const void *)&decode:(const void *)&use;
  VK_CHECK(vkCreateImageView(V.device, &create, NULL, &v->handle));
  v->next = i->views;
  i->views = v;
  return v->handle;
}
#include "nvn_vk_astc_contract.inc"
static image *images;
// O(1) physical-cache removal; a budget tick never rescans the full image list
// to unlink one cold entry. Only the recording owner edits logical membership.
static void image_list_remove(image *i) {
  if(i->list_prev)i->list_prev->next=i->next;
  else {if(images!=i)die("Vulkan image list ownership");images=i->next;}
  if(i->next)i->next->list_prev=i->list_prev;
  if(private_image_cursor==i)private_image_cursor=i->next;
  i->next=i->list_prev=NULL;
}
static image *image_buckets[8192];
#if defined(DM_VULKAN_TEST_EMBED)
int vk_texture_test_astc_info(const tex_t *texture,VkImageView view,unsigned state[5]) {
  for(const image *i=image_buckets[texture->serial%8192];i;i=i->hash_next)
    if(i->tex.serial==texture->serial) {
      memset(state,0,5*sizeof *state);state[0]=i->astc_ldr_proven;state[1]=i->external_access;
      for(const image_view *v=i->views;v;v=v->next) {
        state[2]++;if(v->astc_decode8)state[3]++;
        if(v->handle==view)state[4]=v->astc_decode8;
      }
      return 1;
    }
  return 0;
}
// Owned GPU tests inspect logical hazard sequence numbers, not elapsed time.
// Tools cannot expose this private invariant. Lookup only: no image creation,
// upload, state transition, resource registration or production hot counter.
int vk_texture_test_mip_clocks(const tex_t *texture,uint64_t state[20]) {
  if(!texture)return 0;
  for(const image *i=image_buckets[texture->serial%8192];i;i=i->hash_next)
    if(i->tex.serial==texture->serial) {
      if(!image_mip_tracked(i))return 0;
      state[0]=image_write_clock;state[1]=image_pass_begin_clock;
      state[2]=i->use_clock;state[3]=i->access_generation;
      for(unsigned level=0;level<i->levels;level++)
        state[4+level]=i->access[(size_t)level*i->layers].use_clock;
      return i->levels;
    }
  return 0;
}
#endif
static _Atomic uint32_t live_images;
typedef struct retired_image {
  struct retired_image *next;
  uint64_t serial, at;
} retired_image;
static retired_image *retired_images;
static uint64_t uploads, reuploads;
static uint32_t mip(uint32_t n, uint32_t l) {
  n >>= l;
  return n ? n : 1;
}
static VkImageAspectFlags aspect_for(int format) {
  if (format == NVN_FORMAT_STENCIL8)
    return VK_IMAGE_ASPECT_STENCIL_BIT;
  if (format == NVN_FORMAT_DEPTH24_STENCIL8 ||
      format == NVN_FORMAT_DEPTH32F_STENCIL8)
    return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
  if (format >= NVN_FORMAT_DEPTH16 && format <= NVN_FORMAT_DEPTH32F)
    return VK_IMAGE_ASPECT_DEPTH_BIT;
  return VK_IMAGE_ASPECT_COLOR_BIT;
}
#ifdef DM_VULKAN_TEST_EMBED
static int test_force_bc_decode;
void vk_texture_test_force_bc_decode(int force) { test_force_bc_decode=!!force; }
int vk_texture_test_bc_decoded(const tex_t *texture) {
  for(const image *i=images;i;i=i->next)if(i->tex.serial==texture->serial)return i->decoded;
  return -1;
}
#endif
static VkFormat decoded_format(int f) {
  if (f == 0x49)
    return VK_FORMAT_R8_UNORM;
  if (f == 0x4a)
    return VK_FORMAT_R8_SNORM;
  if (f == 0x4b)
    return VK_FORMAT_R8G8_UNORM;
  if (f == 0x4c)
    return VK_FORMAT_R8G8_SNORM;
  if (f == 0x4f || f == 0x50)
    return VK_FORMAT_R16G16B16A16_SFLOAT;
  if ((f >= 0x45 && f <= 0x48) || f == 0x4e)
    return VK_FORMAT_R8G8B8A8_SRGB;
  return VK_FORMAT_R8G8B8A8_UNORM;
}
static void image_allocate(image *i) {
  // These physical properties are immutable until a new allocation. Cache
  // the predicate here, including after storage/mutable-format promotion.
  i->physical_traits=image_physical_traits(i);
  size_t cells=(size_t)i->levels*i->layers*3;
  if(!cells || cells>SIZE_MAX/sizeof *i->access) die("Vulkan image access capacity");
  i->access=calloc(cells,sizeof *i->access);
  if(!i->access) die("Vulkan image access allocation");
  i->access_generation++;
  VK_CHECK(vkCreateImage(V.device, &i->description, NULL, &i->handle));
  VK_CHECK(vk_memory_image(i->handle, &i->memory, &i->allocation));
  i->allocation_bytes=vk_memory_allocation_bytes(i->allocation);
  if(!i->allocation_bytes)die("Private image allocation size");
  uint64_t prior=atomic_fetch_add(&private_image_bytes,i->allocation_bytes);
  if(prior>UINT64_MAX-i->allocation_bytes)die("Private image allocation accounting overflow");
  image_rebuildable_set(i,i->cpu_rebuildable);
  VkImageMemoryBarrier barrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_GENERAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = i->handle,
      .subresourceRange = {i->aspect, 0, i->levels, 0,
                           i->description.imageType == VK_IMAGE_TYPE_3D
                               ? VK_REMAINING_ARRAY_LAYERS : i->layers},
      .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
  vkCmdPipelineBarrier(V.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
  i->layout=VK_IMAGE_LAYOUT_GENERAL;
  i->layout_scope=0;
  if(image_mip_tracked(i)) for(unsigned level=0;level<i->levels;level++)
    image_cell(i,0,level,0)->mip_layout=VK_IMAGE_LAYOUT_GENERAL;
}
#ifdef DM_VULKAN_NATIVE_RESOURCES
#include "nvn_vk_native_image.inc"
#ifdef DM_VULKAN_TEST_EMBED
void vk_test_texture_native_binding(uint64_t serial,VkDeviceMemory *memory,uint64_t *offset) {
  extern void vk_test_native_allocation_binding(void *,VkDeviceMemory *,uint64_t *);
  for(image *i=image_buckets[serial%8192];i;i=i->hash_next)if(i->tex.serial==serial && i->native_entry) {
    vk_test_native_allocation_binding(i->native_pool,memory,offset);return;
  }
  die("Native image binding test owner missing");
}
#endif

#endif
static void image_allocation_free(image *i) {
  image_rebuildable_set(i,0);
  vk_memory_free(i->allocation);i->allocation=NULL;
  if(i->allocation_bytes) {
    uint64_t prior=atomic_fetch_sub(&private_image_bytes,i->allocation_bytes);
    if(prior<i->allocation_bytes)die("Private image allocation accounting underflow");
    i->allocation_bytes=0;
  }
}
static void image_storage_destroy(void *context) {
  image *i = context;
  while (i->views) {
    image_view *view = i->views;
    i->views = view->next;
    vk_render_view_retired(view->handle);
    vkDestroyImageView(V.device, view->handle, NULL);
    free(view);
  }
  vkDestroyImage(V.device, i->handle, NULL);
#ifdef DM_VULKAN_NATIVE_RESOURCES
  if(i->native_entry) {
    native_range_remove(i);
    // Physical storage retirement, after every referencing GPU submission and
    // its cache publication/gate completion; never release on TextureFinalize.
    for(unsigned n=0;n<NVN_NATIVE_LEVELS;n++)nvn_dirty_scope_release(i->native_write_scopes[n]);
    vk_pool_native_image_release(i->native_pool);
  } else
#endif
  image_allocation_free(i);
  free(i->access);
  free(i);
}
// Vulkan 的非压缩颜色兼容类按位数分组；压缩类仍按块编码分开。
// Uncompressed color compatibility classes use bit size; compressed classes
// remain separate by block encoding. Depth/stencil formats require equality.
static unsigned format_class(VkFormat f) {
  if (f == VK_FORMAT_R4G4_UNORM_PACK8 || (f >= VK_FORMAT_R8_UNORM && f <= VK_FORMAT_R8_SRGB)) return 8;
  if ((f >= VK_FORMAT_R4G4B4A4_UNORM_PACK16 && f <= VK_FORMAT_A1R5G5B5_UNORM_PACK16) ||
      (f >= VK_FORMAT_R8G8_UNORM && f <= VK_FORMAT_R8G8_SRGB) ||
      (f >= VK_FORMAT_R16_UNORM && f <= VK_FORMAT_R16_SFLOAT)) return 16;
  if (f >= VK_FORMAT_R8G8B8_UNORM && f <= VK_FORMAT_B8G8R8_SRGB) return 24;
  if ((f >= VK_FORMAT_R8G8B8A8_UNORM && f <= VK_FORMAT_A2B10G10R10_SSCALED_PACK32) ||
      (f >= VK_FORMAT_R16G16_UNORM && f <= VK_FORMAT_R16G16_SFLOAT) ||
      (f >= VK_FORMAT_R32_UINT && f <= VK_FORMAT_R32_SFLOAT) ||
      f == VK_FORMAT_B10G11R11_UFLOAT_PACK32 || f == VK_FORMAT_E5B9G9R9_UFLOAT_PACK32) return 32;
  if (f >= VK_FORMAT_R16G16B16_UNORM && f <= VK_FORMAT_R16G16B16_SFLOAT) return 48;
  if ((f >= VK_FORMAT_R16G16B16A16_UNORM && f <= VK_FORMAT_R16G16B16A16_SFLOAT) ||
      (f >= VK_FORMAT_R32G32_UINT && f <= VK_FORMAT_R32G32_SFLOAT)) return 64;
  if (f >= VK_FORMAT_R32G32B32_UINT && f <= VK_FORMAT_R32G32B32_SFLOAT) return 96;
  if (f >= VK_FORMAT_R32G32B32A32_UINT && f <= VK_FORMAT_R32G32B32A32_SFLOAT) return 128;
  if (f >= VK_FORMAT_BC1_RGB_UNORM_BLOCK && f <= VK_FORMAT_ASTC_12x12_SRGB_BLOCK)
    return 1000 + (f - VK_FORMAT_BC1_RGB_UNORM_BLOCK) / 2;
  return 10000 + f;
}
// Replace physical storage without changing guest texture identity. Keep old
// views/framebuffers alive until the copy and all earlier queue users finish.
static void image_promote(image *i, VkImageCreateFlags flags, VkImageUsageFlags usage) {
#ifdef DM_VULKAN_NATIVE_RESOURCES
  if(i->native_entry && ((flags&~i->description.flags) || (usage&~i->description.usage)))
    die("Native image recipe cannot change physical allocation online");
#endif
  flags |= i->description.flags;
  usage |= i->description.usage;
  if(flags==i->description.flags && usage==i->description.usage) return;
  // A view-format query also calls this helper when no replacement is needed.
  // Drain only real promotion; no-op STORE metadata must not end the pass again.
  vk_mip_clear_drain();
  // Ending a pass may replay nested draws which already promote this image.
  vk_render_end();
  flags |= i->description.flags;
  usage |= i->description.usage;
  if(flags==i->description.flags && usage==i->description.usage) return;
  image_recipes_changed();
  image_general_transfer(i);vk_transfer_barrier();
  image *old=malloc(sizeof *old);
  if(!old) die("Vulkan image promotion allocation");
  *old=*i;
  // Only i remains a logical lookup/candidate. old is exclusively retained by
  // the existing completion callback; its bytes remain counted until then.
  image_replacement_detach(i,old);
#ifdef DM_VULKAN_SHARED_POOLS
  // The deferred physical copy is not a second owner of the same guest bytes.
  // The original image pointer remains the sole live list/index identity.
  old->shared_alias_linked=0;old->shared_alias_next=NULL;
  old->shared_alias_range=(alias_range_node){0};
#endif
  int cpu_rebuildable=i->cpu_rebuildable;
  image_binding_replaced(i,old);
  i->views=NULL;
  i->description.flags=flags;
  i->description.usage=usage;
  image_allocate(i);
  for(unsigned level=0;level<i->levels;level++)
    for(unsigned aspect=1;aspect<=VK_IMAGE_ASPECT_STENCIL_BIT;aspect<<=1)
      if(i->aspect & aspect) {
        VkImageCopy copy={.srcSubresource={aspect,level,0,i->layers},
            .dstSubresource={aspect,level,0,i->layers},
            .extent={mip(i->extent.width,level),mip(i->extent.height,level),
                     mip(i->extent.depth,level)}};
        vkCmdCopyImage(V.cmd,old->handle,VK_IMAGE_LAYOUT_GENERAL,i->handle,
            VK_IMAGE_LAYOUT_GENERAL,1,&copy);
      }
  image_record(old,(VkImageSubresourceRange){old->aspect,0,old->levels,0,old->layers},
      VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT,0);
  image_record(i,(VkImageSubresourceRange){i->aspect,0,i->levels,0,i->layers},
      VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,0);
  // Promotion preserves every mip/layer/aspect; it does not create new data.
  image_rebuildable_set(i,cpu_rebuildable);
#ifdef DM_VULKAN_SHARED_POOLS
  // Copying a CPU-rebuildable image during promotion does not make its
  // packaged backing stale. Match the restored authority in the alias list.
  if(cpu_rebuildable)image_shared_alias_remove(i);
#endif
  vk_barrier();
  vk_completion(image_storage_destroy,old);
  LOG("[vulkan] image promotion serial=%llu flags=%#x->%#x usage=%#x->%#x",
      (unsigned long long)i->tex.serial,old->description.flags,flags,
      old->description.usage,usage);
}
static void image_astc_copy_check(image *i);
static VkFormat image_astc_copy_view_format(image *i,int nvn_format);
static VkFormat image_binding_format(image *i,const view_t *v,int storage) {
  if(i->bc_copy_decoded) {
    image_astc_copy_check(i);
    if(storage || (v && v->set_format && v->format!=0x44 && v->format!=0x48))
      die("Mixed compressed copy cannot acquire incompatible logical BC3 view");
  }
  if(i->astc_copy_encoding) {
    image_astc_copy_check(i);
    if(storage)die("ASTC propagated copy has no logical BC3 storage encoding");
  }
  VkFormat format=i->format;
  if(v && v->set_format && v->format!=i->tex.d.format) {
    format=i->astc_copy_encoding?image_astc_copy_view_format(i,v->format):vk_nvn_format(v->format);
    if(i->decoded && v->format>=0x41 && v->format<=0x50)
      format=decoded_format(v->format);
    if(!format || format_class(format)!=format_class(i->format))
      die("Vulkan incompatible image view formats: %u -> %u",i->format,format);
  }
  VkImageCreateFlags flags=format!=i->format?VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT:0;
  if(storage && (format!=i->format || !(i->description.usage & VK_IMAGE_USAGE_STORAGE_BIT))) {
    VkFormatProperties props;
    vk_format_properties(format,&props);
    if(!(props.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))
      die("Vulkan unsupported storage image view format %u",format);
    if(format!=i->format) {
      vk_format_properties(i->format,&props);
      if(!(props.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))
        flags |= VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;
    }
  }
  image_promote(i,flags,
      storage?VK_IMAGE_USAGE_STORAGE_BIT:0);
  return format;
}
static VkFormat image_view_format(image *i,const view_t *v) {
  return image_binding_format(i,v,0);
}
#ifdef DM_VULKAN_SHARED_POOLS
// The startup allocator owns this certificate. No pool flag, imported pointer
// success or historical address pattern can stand in for backing provenance.
extern int dm_guest_shared_heap_contains(uintptr_t,size_t) __attribute__((weak));
#ifdef DM_VULKAN_TEST_EMBED
static uintptr_t shared_image_test_base;static size_t shared_image_test_size;
void vk_test_shared_image_backing(const void *mem,size_t bytes) {
  // The owned fixture supplies its own finite MAP_SHARED mapping, never guest.
  if(bytes>(4u<<20) || (bytes && (!mem || ((uintptr_t)mem&4095) || (bytes&4095))))
    die("Shared image owned fixture backing extent");
  shared_image_test_base=(uintptr_t)mem;shared_image_test_size=bytes;
}
#endif
static int shared_image_backing(uint64_t address,uint64_t bytes) {
  size_t page=(size_t)getpagesize();
  if(!bytes || bytes>UINTPTR_MAX-address || address+bytes>UINTPTR_MAX-(page-1))return 0;
  uintptr_t first=address&~(uintptr_t)(page-1),end=(address+bytes+page-1)&~(uintptr_t)(page-1);
#ifdef DM_VULKAN_TEST_EMBED
  if(shared_image_test_size && first>=shared_image_test_base &&
      first-shared_image_test_base<=shared_image_test_size &&
      end-first<=shared_image_test_size-(first-shared_image_test_base))return 1;
#endif
  return dm_guest_shared_heap_contains && dm_guest_shared_heap_contains(first,end-first);
}
#endif
// Only logical copies of textures are used across a visibility callback.
// Callers must invoke this before borrowing any image/view/descriptor/temp.
void vk_texture_cpu_preflight(const tex_t *t) {
#ifdef DM_VULKAN_NATIVE_RESOURCES
  if(t && t->has_layout && nvn_native_layout_is_marker(t->layout))return; // no CPU deswizzle/dirty-source observation
#endif
#ifdef DM_VULKAN_SHARED_POOLS
  if((vk_pool_shared_active() || vk_legacy_astc_copy_mode()) && t && t->has_layout && t->mem && t->L.size) {
    if(!vk_pool_shared_active() && !nvn_dirty_tracked(t->mem,t->L.size))return; // original untracked/always-upload fallback
    if(vk_pool_shared_active() && !shared_image_backing((uintptr_t)t->mem,t->L.size))
      die("Shared packaged image requires committed shared-heap backing");
    int read=1;
    for(image *i=image_buckets[t->serial%8192];i;i=i->hash_next)if(i->tex.serial==t->serial) {
      uint64_t epoch=nvn_dirty_change_epoch();
      read=!i->initialized || !(epoch && i->checked_dirty_epoch==epoch &&
        i->checked_dirty_stamp==i->stamp) && nvn_dirty_since(t->mem,t->L.size,i->stamp);break;
    }
    // A queued CPU effect has no dirty generation until completion. Publish
    // it even for an initialized/clean image, before any image/view borrows.
    if((read || vk_pool_texture_cpu_effect_pending((uintptr_t)t->mem,t->L.size)) && !vk_pool_cpu_observe_prepare((uintptr_t)t->mem,t->L.size))
      die("Shared packaged image source outside live shared Pool");
  }
#endif
  if(V.options.pool_defer_publication && t && t->d.target!=NVN_TEXTURE_TARGET_BUFFER &&
      t->mem && t->L.size && vk_pool_cpu_prepare((uintptr_t)t->mem,t->L.size)<0)
    die("Vulkan texture CPU source lifetime changed during preflight");
}
static void image_upload(image *i);
static image *image_get_usage(const tex_t *t,VkImageUsageFlags required) {
  if (!V.cmd)
    die("Vulkan image access outside a submission");
  for (image *i = image_buckets[t->serial % 8192]; i; i = i->hash_next)
    if (i->tex.serial == t->serial) {
      image_astc_copy_check(i);
      if((i->astc_copy_encoding || i->bc_copy_decoded) && required)die("ASTC propagated copy cannot acquire logical BC3 writer usage");
      i->last_use = vk_recording_serial();
      if(required && (required&~i->description.usage)) {
        image_upload(i);
        VkImageCreateFlags flags=i->description.imageType==VK_IMAGE_TYPE_3D?
            VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT:0;
        image_promote(i,flags,required);
      }
      if(layout_reference) image_layout(i,VK_IMAGE_LAYOUT_GENERAL);
      return i;
    }
  image *i = calloc(1, sizeof *i);
  if (!i)
    die("Vulkan image allocation");
  vk_render_end();
  i->tex = *t;
  i->last_use = vk_recording_serial();
  i->format = vk_nvn_format(t->d.format);
  i->aspect = aspect_for(t->d.format);
  i->extent =
      (VkExtent3D){t->d.width,
                   t->d.target == NVN_TEXTURE_TARGET_1D ||
                           t->d.target == NVN_TEXTURE_TARGET_1D_ARRAY
                       ? 1
                       : t->d.height,
                   t->d.target == NVN_TEXTURE_TARGET_3D ? t->d.depth : 1};
  i->levels = t->d.levels ? t->d.levels : 1;
  i->layers = t->L.layers;
  if (!i->format || t->d.target == NVN_TEXTURE_TARGET_BUFFER)
    die("Vulkan unsupported image format=%#x target=%d", t->d.format,
        t->d.target);
  VkFormatProperties props;
  vk_format_properties(i->format, &props);
  int bw, bh;
  i->bpp = nvn_fmt_bpb(t->d.format, &bw, &bh);
  int sampled_format=!!(props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
#ifdef DM_VULKAN_TEST_EMBED
  if(test_force_bc_decode)sampled_format=0;
#endif
  if (!sampled_format && t->d.format >= 0x41 && t->d.format <= 0x50) {
    uint32_t unused;
    int bytes;
    if (!nvn_bc_unpacked(t->d.format, &unused, &bytes))
      die("Vulkan BC decoder missing");
    i->decoded = 1;
    i->bpp = bytes;
    i->format = decoded_format(t->d.format);
    vk_format_properties(i->format, &props);
  }
#ifdef DM_VULKAN_NATIVE_RESOURCES
  const nvn_native_entry *native=native_image_entry(t);
  if(native) {
    if(!shared_image_backing((uintptr_t)t->mem,t->L.size))die("Native image requires committed shared heap backing");
    if(required&~native->recipe.usage)die("Native resource usage not in baked recipe");
    native_image_create(i,native);
    goto image_publish;
  }
#endif
  VkImageUsageFlags usage =
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)
    usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
  // NVN IMAGE permits storage use; it does not prove actual use. Retain guest
  // flags and promote physical storage at the first real storage binding.
  if (storage_usage_mode() && (t->d.flags & NVN_TEXTURE_FLAGS_IMAGE) &&
      (props.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))
    usage |= VK_IMAGE_USAGE_STORAGE_BIT;
  if (i->aspect != VK_IMAGE_ASPECT_COLOR_BIT)
    usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  else if (((required&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) || attachment_usage_mode()) &&
      (props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT))
    usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  usage|=required;
  VkImageCreateInfo ci = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = t->d.target == NVN_TEXTURE_TARGET_3D
                       ? VK_IMAGE_TYPE_3D
                       : (t->d.target == NVN_TEXTURE_TARGET_1D ||
                                  t->d.target == NVN_TEXTURE_TARGET_1D_ARRAY
                              ? VK_IMAGE_TYPE_1D
                              : VK_IMAGE_TYPE_2D),
      .format = i->format,
      .extent = i->extent,
      .mipLevels = i->levels,
      .arrayLayers = i->layers,
      .samples = t->d.samples > 1 ? (VkSampleCountFlagBits)t->d.samples
                                  : VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = usage};
  if (t->d.target == NVN_TEXTURE_TARGET_CUBEMAP ||
      t->d.target == NVN_TEXTURE_TARGET_CUBEMAP_ARRAY)
    ci.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
  // 原 clear_tex 可逐深度面清 3D 纹理；Vulkan 以 2D 附件视图选中同一面。
  // Original clear_tex addresses individual 3D planes through attachments.
  if (ci.imageType == VK_IMAGE_TYPE_3D &&
      (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
    ci.flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
  i->description = ci;
#ifdef DM_VULKAN_SHARED_POOLS
  if(vk_pool_shared_mode() && t->has_layout && t->mem && t->L.size) {
    // Full GPU clears may deliberately skip the initial CPU upload. Observe
    // the CPU source version independently of that upload, before any image
    // writer: untracked is unknown, never evidence of a later CPU edit.
    // Outer preflight already imported/pinned storage before this borrow.
    // The GPU-dependency check cannot submit/wait; the following CPU phase
    // waits only paired kernel leases, never renderer callbacks/replacement.
    if(!shared_image_backing((uintptr_t)t->mem,t->L.size) ||
        !vk_pool_shared_image_source((uintptr_t)t->mem,t->L.size,0))
      die("Shared image initial observation missing ordered source preflight");
    i->shared_dirty_scope=nvn_dirty_scope_acquire(t->mem,t->L.size);
    if(!i->shared_dirty_scope)die("Shared image initial dirty scope admission failed");
    vk_shared_cpu_observation observation;
    if(!vk_pool_shared_cpu_observe_begin((uintptr_t)t->mem,t->L.size,&observation))
      die("Shared image initial observation missing page-level CPU preflight");
    i->stamp=observation.stamp;vk_pool_shared_cpu_observe_end(&observation);
  }
#endif
  image_allocate(i);
#ifdef DM_VULKAN_NATIVE_RESOURCES
image_publish:
#endif
  i->next = images;
  if(images)images->list_prev=i;
  images = i;
  i->hash_next = image_buckets[t->serial % 8192];
  image_buckets[t->serial % 8192] = i;
  atomic_fetch_add(&live_images, 1);
  return i;
}
static image *image_get(const tex_t *t) { return image_get_usage(t,0); }
#if defined(DM_VULKAN_NATIVE_RESOURCES) && defined(DM_VULKAN_TEST_EMBED)
// Owned RGBA dependency oracle only: the production table still rejects RGBA.
// Replace a completed, never-viewed placeholder by a genuinely imported image;
// no real producer recipe or host-native mip permutation is asserted here.
void vk_test_native_rgba_adopt(const tex_t *t,VkImage handle,VkDeviceMemory memory,void *allocation,
    const VkImageCreateInfo *description,const nvn_native_entry *entry) {
  if(!handle) {(void)image_get(t);return;}
  image *i=NULL;for(image *at=image_buckets[t->serial%8192];at;at=at->hash_next)
    if(at->tex.serial==t->serial){i=at;break;}
  if(!i || i->native_entry || i->views || i->last_use>vk_completed_serial() || i->shared_dirty_scope ||
      i->format!=VK_FORMAT_R8G8B8A8_UNORM || description->format!=i->format ||
      description->mipLevels!=i->levels || description->arrayLayers!=1 || !allocation || !entry)
    die("Owned native RGBA adoption requires a completed untouched matching placeholder");
  image_shared_alias_remove(i);vkDestroyImage(V.device,i->handle,NULL);image_allocation_free(i);
  i->allocation=NULL;i->handle=handle;i->memory=memory;i->native_pool=allocation;i->native_entry=entry;
  i->description=*description;i->description.pNext=NULL;i->initialized=1;i->layout=VK_IMAGE_LAYOUT_GENERAL;
  i->layout_scope=0;i->physical_traits=image_physical_traits(i);
  memset(i->access,0,(size_t)i->levels*3*sizeof *i->access);i->access_generation++;
  for(unsigned n=0;n<i->levels;n++)image_cell(i,0,n,0)->mip_layout=VK_IMAGE_LAYOUT_GENERAL;
  native_range_add(i);image_recipes_changed();
}
int vk_test_native_image_owner(uint64_t serial) {
  for(image *i=images;i;i=i->next)if(i->tex.serial==serial)return i->native_acquired;
  return -1;
}
VkPipelineStageFlags vk_test_native_mip_scope(uint64_t serial,unsigned level,uint64_t *use_clock) {
  for(image *i=image_buckets[serial%8192];i;i=i->hash_next)if(i->tex.serial==serial) {
    if(!i->native_entry || level>=i->levels)die("Native scope oracle bounds");
    image_access *cell=image_cell(i,0,level,0);*use_clock=cell->use_clock;return cell->mip_scope;
  }
  die("Native scope oracle image missing");
}
VkPipelineStageFlags vk_test_native_unused_mip_source(uint64_t serial,unsigned level) {
  for(image *i=image_buckets[serial%8192];i;i=i->hash_next)if(i->tex.serial==serial) {
    if(!i->native_entry || level>=i->levels || image_cell(i,0,level,0)->use_clock)
      die("Owned acquire oracle requires an unused native mip");
    return image_cell(i,0,level,0)->mip_scope;
  }
  die("Owned acquire oracle image missing");
}
#endif
static image *image_get_attachment(const tex_t *t) {
  VkImageUsageFlags usage=aspect_for(t->d.format)==VK_IMAGE_ASPECT_COLOR_BIT?
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT:VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  return image_get_usage(t,usage);
}
static uint64_t texture_dirty_hits,texture_dirty_checks;
static _Atomic uint64_t published_texture_dirty_hits,published_texture_dirty_checks;
static _Atomic int texture_dirty_requested=-1,texture_dirty_active;
static int texture_dirty_reference_active;
void vk_texture_dirty_reference(int reference) { atomic_store(&texture_dirty_requested,!!reference); }
int vk_texture_dirty_reference_mode(void) { return atomic_load(&texture_dirty_active); }
void vk_texture_dirty_stats(uint64_t *hits,uint64_t *checks) {
  *hits=atomic_load_explicit(&published_texture_dirty_hits,memory_order_relaxed);
  *checks=atomic_load_explicit(&published_texture_dirty_checks,memory_order_relaxed);
}
static void texture_dirty_mode_begin(void) {
  int reference=atomic_load(&texture_dirty_requested);
  if(reference<0) {
    const char *v=getenv("DM_VULKAN_TEXTURE_DIRTY_PROOF_REFERENCE");
    if(v && strcmp(v,"0") && strcmp(v,"1")) die("DM_VULKAN_TEXTURE_DIRTY_PROOF_REFERENCE expects 0 or 1");
    reference=v && !strcmp(v,"1");
    atomic_store(&texture_dirty_requested,reference);
  }
  texture_dirty_reference_active=reference;atomic_store(&texture_dirty_active,reference);
}
#include "nvn_vk_image_cpu_observe.inc"
// Keep deswizzle/staging/copy state off the high-frequency readonly proof
// path. Only image_upload calls this after the original ordered checks; its
// body, upload/protection stamp and physical dependency ordering are unchanged.
static __attribute__((noinline)) void image_upload_contents(image *i) {
  const tex_t *t=&i->tex;
#ifdef DM_VULKAN_SHARED_POOLS
  if((vk_pool_shared_active() || vk_legacy_astc_copy_mode()) && vk_texture_shared_linear_conflict((uintptr_t)t->mem,t->L.size)) {
    image *owner=NULL;
    for(image *a=shared_gpu_aliases;a;a=a->shared_alias_next)
      if(a->initialized && a->tex.mem && a->tex.L.size &&
          (uintptr_t)t->mem<(uintptr_t)a->tex.mem+a->tex.L.size &&
          (uintptr_t)t->mem+t->L.size>(uintptr_t)a->tex.mem) {owner=a;break;}
    die("Shared CPU image upload aliases GPU-owned optimal source: canonical retile required target_serial=%llu target_format=%#x source=%p bytes=%llu owner_serial=%llu owner_format=%#x owner_source=%p owner_bytes=%llu",
        (unsigned long long)t->serial,t->d.format,(void *)t->mem,(unsigned long long)t->L.size,
        (unsigned long long)(owner?owner->tex.serial:0),owner?owner->tex.d.format:0,
        owner?(void *)owner->tex.mem:NULL,(unsigned long long)(owner?owner->tex.L.size:0));
  }
  if(vk_pool_shared_mode() && !i->shared_dirty_scope) {
    if(!shared_image_backing((uintptr_t)t->mem,t->L.size))die("Shared image provenance changed");
    i->shared_dirty_scope=nvn_dirty_scope_acquire(t->mem,t->L.size);
    if(!i->shared_dirty_scope)die("Shared image dirty scope admission failed");
  }
  if(vk_pool_shared_active() || (vk_legacy_astc_copy_mode() && nvn_dirty_tracked(t->mem,t->L.size))) {
    image_upload_observed_contents(i);return;
  }
#endif
  // Untracked sources are always uploaded; they must not acquire a proof.
  i->checked_dirty_epoch=0;
  i->checked_dirty_range=(nvn_dirty_range_proof){0};
  if (t->d.samples > 1 || i->aspect != VK_IMAGE_ASPECT_COLOR_BIT)
    die("Vulkan packaged depth/MS image unsupported");
  image_general_transfer(i);
  uint64_t stamp = nvn_dirty_now();
  // OFF/unsupported paths never scan. Only whole native 2D, one-layer uploads
  // can establish this certificate; sRGB already has its own 8-bit decode.
  int ldr=V.astc_decode_mode && !i->external_access && !i->decoded &&
      astc_unorm_format(i->format) && i->description.imageType==VK_IMAGE_TYPE_2D &&
      i->layers==1 && i->extent.depth==1;
  image_astc_proof(i,0);
  for (uint32_t layer = 0; layer < i->layers; layer++)
    for (uint32_t level = 0; level < i->levels; level++) {
      uint32_t w = mip(i->extent.width, level),
               h = mip(i->extent.height, level),
               z = mip(i->extent.depth, level);
      int bw, bh;
      uint32_t bpb = nvn_fmt_bpb(t->d.format, &bw, &bh);
      size_t pitch = (w + bw - 1) / bw * bpb, rows = (h + bh - 1) / bh;
      // CPU BC decoding consumes these compressed bytes synchronously. They
      // never enter a GPU command/descriptor/effect, so retaining a mapped
      // VkBuffer through the submission and global staging cache is needless.
      // Native compressed uploads still require their original GPU staging.
      vk_buffer *raw = i->decoded ? NULL : vk_temp_buffer(pitch * rows * z);
      uint8_t *raw_bytes = i->decoded ? malloc(pitch * rows * z) : raw->map;
      if (!raw_bytes) die("Vulkan BC deswizzle CPU scratch allocation");
      if (t->d.flags & NVN_TEXTURE_FLAGS_LINEAR) {
        if (level || layer || z != 1)
          die("Vulkan linear layered/mipped upload unsupported");
        size_t stride = t->d.stride ? t->d.stride : (pitch + 31) & ~31;
        for (size_t y = 0; y < rows; y++)
          memcpy(raw_bytes + y * pitch, t->mem + y * stride, pitch);
      } else if (nvn_deswizzle(&t->d, level, layer, t->mem, raw_bytes, pitch))
        die("Vulkan deswizzle failed");
      // Classify exactly the staging bytes consumed by this upload, never a
      // second read of mutable guest memory. No RGBA scratch or allocation.
      if(ldr && !nvn_astc_mip_is_ldr(raw_bytes,pitch*rows*z,bw,bh)) ldr=0;
      vk_buffer *src = raw;
      if (i->decoded) {
        src = vk_temp_buffer((uint64_t)w * h * z * i->bpp);
        for (uint32_t slice = 0; slice < z; slice++)
          if (nvn_bc_decode(t->d.format, raw_bytes + slice * pitch * rows, pitch,
                            w, h, src->map + (uint64_t)slice * w * h * i->bpp,
                            w * i->bpp))
            die("Vulkan BC decode failed");
        free(raw_bytes);
      }
      VkBufferImageCopy c = {.imageSubresource = {.aspectMask = i->aspect,
                                                  .mipLevel = level,
                                                  .baseArrayLayer = layer,
                                                  .layerCount = 1},
                             .imageExtent = {w, h, z}};
      image_transfer(i,image_layer_range(c.imageSubresource),1);
      vkCmdCopyBufferToImage(V.cmd, src->handle, i->handle,
                             VK_IMAGE_LAYOUT_GENERAL, 1, &c);
    }
  if (i->initialized)
    reuploads++;
  else
    uploads++;
  nvn_dirty_protect(t->mem, t->L.size);
  i->stamp = stamp;
  i->initialized = 1;
  // Raw VkImage users can retain a handle and write later without passing
  // through image_record. A later CPU upload cannot revoke that escape.
  image_rebuildable_set(i,!i->external_access);
#ifdef DM_VULKAN_SHARED_POOLS
  if(i->cpu_rebuildable)image_shared_alias_remove(i);
#endif
  // Publish only after every mip copy was recorded. The existing ordered
  // image dependencies protect content changes; old views retain lifetimes.
  image_astc_proof(i,ldr);
}
static VkImageSubresourceRange range(image *i,const view_t *v);
#include "nvn_vk_astc_copy_compat.inc"
static void image_upload(image *i) {
#ifdef DM_VULKAN_NATIVE_RESOURCES
  if(i->native_entry) {native_image_use(i);return;}
#endif
  image_astc_copy_check(i);
  // HLE texture initialization is an ordered CPU exposure, including late
  // aliases. Its sticky pages keep every later writer on original eager WB.
  // Check this before dirty/stamp earlyreturn; never commit with i borrowed.
  if(V.options.pool_defer_publication && i->tex.mem && i->tex.L.size &&
      !vk_pool_cpu_source_ready((uintptr_t)i->tex.mem,i->tex.L.size))
    die("Vulkan CPU texture source missing ordered initialization exposure");
  const tex_t *t = &i->tex;
  if(!t->has_layout) return;
#ifdef DM_VULKAN_SHARED_POOLS
  // Do not let a late CPU effect hide behind the epoch/range clean fast path.
  // No submit/wait is legal here: the outer logical operation owns admission.
  if(vk_legacy_astc_copy_mode() && !vk_pool_shared_active() && nvn_dirty_tracked(t->mem,t->L.size) &&
      vk_pool_legacy_cpu_observe_pending((uintptr_t)t->mem,t->L.size))
    die("Legacy CPU texture source missing ordered outer preflight");
  if(vk_pool_shared_mode() && !shared_image_backing((uintptr_t)t->mem,t->L.size))
    die("Shared image conversion backing provenance changed");
#endif
  vk_mip_clear_texture_access(t,NULL,0);
  if(i->initialized) {
    // Take the publication sequence BEFORE checking pages. A later edit
    // invalidates this proof on the next use; never advance the upload stamp.
    uint64_t epoch=nvn_dirty_change_epoch();
    if(!texture_dirty_reference_active && epoch && i->checked_dirty_epoch==epoch &&
       i->checked_dirty_stamp==i->stamp) { texture_dirty_hits++;return; }
    nvn_dirty_range_proof proof = {0};
    if (!texture_dirty_reference_active && epoch) {
      // Snapshot BEFORE the original page test. A concurrent/later publication
      // must invalidate this negative result; latest generation is not a key.
      if (nvn_dirty_range_refresh(&i->checked_dirty_range, &proof)) {
        if (i->checked_dirty_stamp == i->stamp &&
            proof.changes == i->checked_dirty_range.changes) {
          i->checked_dirty_epoch = epoch; texture_dirty_hits++; return;
        }
      } else nvn_dirty_range_observe(t->mem, t->L.size, &proof);
    }
    texture_dirty_checks++;
    if(!nvn_dirty_since(t->mem,t->L.size,i->stamp)) {
      i->checked_dirty_epoch=epoch;i->checked_dirty_stamp=i->stamp;
      i->checked_dirty_range=proof;return;
    }
  }
#ifdef DM_VULKAN_SHARED_POOLS
  // A clean physical GPU consumer needs no CPU observation, even while its
  // canonical retile publication is pending. Only actual conversion reads
  // require the outer preflight's completed CPU visibility, before borrowing.
  if(vk_pool_shared_active() &&
      !vk_pool_shared_image_source((uintptr_t)t->mem,t->L.size,0))
    die("Shared image conversion missing ordered CPU preflight");
  if(vk_pool_shared_mode() && i->initialized && !i->cpu_rebuildable)
    die("Shared CPU packaged source aliases GPU-owned image: partial freshness/retile not implemented serial=%llu format=%#x target=%d source=%p bytes=%llu scope=%d stamp=%llu now=%llu epoch=%llu external=%d writer=%u writer_source=%#llx writer_xyz=%d,%d,%d writer_extent=%d,%d,%d",
        (unsigned long long)t->serial,t->d.format,t->d.target,t->mem,
        (unsigned long long)t->L.size,!!i->shared_dirty_scope,
        (unsigned long long)i->stamp,(unsigned long long)nvn_dirty_now(),
        (unsigned long long)nvn_dirty_change_epoch(),i->external_access,
        i->shared_writer,(unsigned long long)i->shared_writer_source,
        i->shared_writer_region.xoffset,i->shared_writer_region.yoffset,i->shared_writer_region.zoffset,
        i->shared_writer_region.width,i->shared_writer_region.height,i->shared_writer_region.depth);
#endif
  image_upload_contents(i);
}

static VkImageSubresourceRange range(image *i, const view_t *v) {
  VkImageSubresourceRange r = {
      .aspectMask = i->aspect, .levelCount = 1, .layerCount = 1};
  if (v) {
    if (v->set_levels)
      r.baseMipLevel = v->base_level;
    if (v->set_layers)
      r.baseArrayLayer = v->min_layer;
    image_view_format(i, v);
  }
  if (r.baseMipLevel >= i->levels || r.baseArrayLayer >= i->layers)
    die("Vulkan image subresource out of range");
  return r;
}
static VkImageSubresourceRange attachment_range(image *i, const view_t *v) {
  if (i->description.imageType != VK_IMAGE_TYPE_3D) return range(i, v);
  uint32_t level = v && v->set_levels ? v->base_level : 0;
  uint32_t plane = v && v->set_layers ? v->min_layer : 0;
  if (!(i->description.flags & VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT) ||
      level >= i->levels || plane >= mip(i->extent.depth, level))
    die("Vulkan 3D attachment plane out of range");
  image_view_format(i, v);
  return (VkImageSubresourceRange){i->aspect, level, 1, plane, 1};
}
#include "nvn_vk_mip_clear_image.inc"
static void clear_image(image *i, const view_t *v, VkImageAspectFlags aspect,
                        const VkClearValue *value, const p_scissor *sc) {
  if(i->description.imageType==VK_IMAGE_TYPE_3D &&
      !(i->description.flags&VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT))
    i=image_get_attachment(&i->tex);
  VkImageSubresourceRange r = attachment_range(i, v);
  r.aspectMask = aspect;
  uint32_t w = mip(i->extent.width, r.baseMipLevel),
           h = mip(i->extent.height, r.baseMipLevel);
  int reinterpreted = image_view_format(i, v) != i->format ||
                      i->description.imageType == VK_IMAGE_TYPE_3D;
  p_scissor full = {0, 0, w, h};
  if (reinterpreted && !sc) sc = &full;
  if (reinterpreted || (sc && (sc->x != 0 || sc->y != 0 || sc->w != (int)w || sc->h != (int)h))) {
    int32_t x = sc->x > 0 ? sc->x : 0, y = sc->y > 0 ? sc->y : 0;
    int64_t right = (int64_t)sc->x + sc->w, bottom = (int64_t)sc->y + sc->h;
    if (right > w)
      right = w;
    if (bottom > h)
      bottom = h;
    if (right <= x || bottom <= y)
      return;
    vk_attachment a = vk_texture_attachment(&i->tex, v);
    VkAttachmentDescription description = {
        .format = a.format,
        .samples = a.samples,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
        .initialLayout = a.layout,
        .finalLayout = a.layout};
    VkAttachmentReference ref = {0, a.layout};
    VkSubpassDescription sub = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = aspect == VK_IMAGE_ASPECT_COLOR_BIT ? 1 : 0,
        .pColorAttachments = aspect == VK_IMAGE_ASPECT_COLOR_BIT ? &ref : NULL,
        .pDepthStencilAttachment =
            aspect == VK_IMAGE_ASPECT_COLOR_BIT ? NULL : &ref};
    VkRenderPassCreateInfo ci = {.sType =
                                     VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                                 .attachmentCount = 1,
                                 .pAttachments = &description,
                                 .subpassCount = 1,
                                 .pSubpasses = &sub};
    VkRenderPass pass;
    VK_CHECK(vkCreateRenderPass(V.device, &ci, NULL, &pass));
    VkFramebufferCreateInfo fi = {.sType =
                                      VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                                  .renderPass = pass,
                                  .attachmentCount = 1,
                                  .pAttachments = &a.view,
                                  .width = w,
                                  .height = h,
                                  .layers = 1};
    VkFramebuffer framebuffer;
    VK_CHECK(vkCreateFramebuffer(V.device, &fi, NULL, &framebuffer));
    if(vk_submission_barrier_mode()==0) vk_draw_barrier(0);
    else vk_barrier();
    VkRenderPassBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = pass,
        .framebuffer = framebuffer,
        // 局部清除只读写裁剪矩形，保留其余附件像素。
        // Load/store only the clipped clear rectangle;
        // the rest of the attachment is preserved.
        .renderArea = {{x, y}, {right - x, bottom - y}}};
    vkCmdBeginRenderPass(V.cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
    VkClearAttachment clear = {
        .aspectMask = aspect, .colorAttachment = 0, .clearValue = *value};
    VkClearRect rectangle = {.rect = {{x, y}, {right - x, bottom - y}},
                             .layerCount = 1};
    vkCmdClearAttachments(V.cmd, 1, &clear, 1, &rectangle);
    vkCmdEndRenderPass(V.cmd);
    vk_texture_attachment_record(&i->tex,v,0);
    vk_defer_pass(pass, framebuffer);
    i->initialized = 1;
    V.passes++;
    return;
  }
  image_general_transfer(i);image_transfer(i,r,1);
  if (aspect == VK_IMAGE_ASPECT_COLOR_BIT)
    vkCmdClearColorImage(V.cmd, i->handle, VK_IMAGE_LAYOUT_GENERAL,
                         &value->color, 1, &r);
  else
    vkCmdClearDepthStencilImage(V.cmd, i->handle, VK_IMAGE_LAYOUT_GENERAL,
                                &value->depthStencil, 1, &r);
  i->initialized = 1;
  V.passes++;
}
void vk_target_clear_color(const p_set_rt *rt, const p_scissor *sc,
                           const p_clear_color *p) {
  if (p->index < 0 || p->index >= rt->n || !rt->color[p->index])
    die("Vulkan clear target invalid");
  if (p->mask != 15)
    die("Vulkan masked color clear pending");
  if(vk_mip_clear_try(rt,sc,p))return;
  vk_texture_cpu_preflight(rt->color[p->index]);
  vk_mip_clear_texture_access(rt->color[p->index],rt->has_cview[p->index]?&rt->cview[p->index]:NULL,0);
  image *i = image_get_attachment(rt->color[p->index]);
  const view_t *view = rt->has_cview[p->index] ? &rt->cview[p->index] : NULL;
  uint32_t level = view && view->set_levels ? view->base_level : 0;
  uint32_t width = mip(i->extent.width, level),
           height = mip(i->extent.height, level);
  // 局部清除要保留 CPU 初始内容及其他子资源；全幅单层清除才可省略上传。
  // Partial clears preserve initial CPU contents and other subresources. Only
  // a full single-subresource clear can discard the initial upload.
  if (i->levels > 1 || i->layers > 1 ||
      (sc && (sc->x > 0 || sc->y > 0 || (int64_t)sc->x + sc->w < width ||
              (int64_t)sc->y + sc->h < height)))
    image_upload(i);
  VkClearValue value;
  memcpy(value.color.float32, p->rgba, 16);
  int cleared = vk_render_target_clear(rt, sc, p->index,
                                       VK_IMAGE_ASPECT_COLOR_BIT, &value);
  if (cleared) {
    if (cleared == 1)
      i->initialized = 1;
  } else {
    clear_image(i, rt->has_cview[p->index] ? &rt->cview[p->index] : NULL,
                VK_IMAGE_ASPECT_COLOR_BIT, &value, sc);
  }
  // A narrow proof, never inferred from CPU bytes or a previous frame. Every
  // physical write invalidates it through image_record. Only complete clears
  // of a selected 2D mip establish it; partial clears do not. Other mips
  // retain independent facts and may contain entirely different data.
  if(cleared!=2 && zero_channels(i) &&
      (!sc || (sc->x<=0 && sc->y<=0 && (int64_t)sc->x+sc->w>=width &&
               (int64_t)sc->y+sc->h>=height)) &&
      p->rgba[0]==0 && p->rgba[1]==0 && p->rgba[2]==0 &&
      (i->format==VK_FORMAT_B10G11R11_UFLOAT_PACK32 || p->rgba[3]==0))
    i->zero_mips|=zero_mip_mask(level,1);
}
void vk_target_clear_depth(const p_set_rt *rt, const p_scissor *sc,
                           const p_clear_ds *p) {
  vk_mip_clear_drain();
  if (!rt->depth)
    die("Vulkan depth clear without depth target");
  vk_texture_cpu_preflight(rt->depth);
  image *i = image_get(rt->depth);
  VkImageAspectFlags a = 0;
  if (p->depth_mask)
    a |= VK_IMAGE_ASPECT_DEPTH_BIT;
  if (p->stencil_mask) {
    if ((p->stencil_mask & 255) != 255)
      die("Vulkan masked stencil clear pending");
    a |= VK_IMAGE_ASPECT_STENCIL_BIT;
  }
  if (a & ~i->aspect)
    die("Vulkan depth clear aspect unavailable");
  VkClearValue value = {.depthStencil = {p->depth, (uint32_t)p->stencil}};
  int cleared = a ? vk_render_target_clear(rt, sc, -1, a, &value) : 0;
  if (cleared) {
    if (cleared == 1)
      i->initialized = 1;
    return;
  }
  if (a)
    clear_image(i, rt->has_dview ? &rt->dview : NULL, a, &value, sc);
}
void vk_texture_clear(const p_clear_tex *p) {
  vk_mip_clear_drain();vk_texture_cpu_preflight(p->tex);
  image *i = image_get(p->tex);
  if(i->astc_copy_encoding || i->bc_copy_decoded)die("ASTC propagated copy cannot accept logical BC3 texture clear");
  int is3d = i->description.imageType == VK_IMAGE_TYPE_3D;
  view_t view = p->has_view ? p->view : (view_t){0};
  if (is3d) view.set_layers = 0; // 原 3D 路径按 zoffset 选深度面，忽略数组层。
                                // Original 3D clears use zoffset, not array layers.
  VkImageSubresourceRange r = range(i, &view);
  int nz = p->r.depth ? p->r.depth : 1;
  uint32_t planes = is3d ? mip(i->extent.depth, r.baseMipLevel) : i->layers;
  int64_t first = (is3d ? 0 : r.baseArrayLayer) + (int64_t)p->r.zoffset;
  if (p->r.xoffset || p->r.yoffset ||
      p->r.width != (int)mip(i->extent.width, r.baseMipLevel) ||
      p->r.height != (int)mip(i->extent.height, r.baseMipLevel) ||
      (p->mask & 15) != 15)
    die("Vulkan partial texture clear pending");
  if (nz < 1 || first < 0 || first + nz > planes)
    die("Vulkan texture clear planes out of range");
  VkClearValue value = {0};
  if (i->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) {
    value.depthStencil.depth = p->c.f[0];
    value.depthStencil.stencil = p->c.u[1];
  } else if (i->aspect == VK_IMAGE_ASPECT_STENCIL_BIT) {
    value.depthStencil.stencil = p->c.u[0];
  } else {
    // ClearTexture/ui 都经原 Metal 的数值转换；不能把 float 位型当整数清除值。
    // Match Metal's numeric clear conversion, rather than reinterpreting floats.
    VkFormat format = image_view_format(i, &view);
    int kind = 0;
    switch (format) {
    case VK_FORMAT_R8_UINT: case VK_FORMAT_R8G8_UINT: case VK_FORMAT_R8G8B8_UINT:
    case VK_FORMAT_R8G8B8A8_UINT: case VK_FORMAT_R16_UINT: case VK_FORMAT_R16G16_UINT:
    case VK_FORMAT_R16G16B16_UINT: case VK_FORMAT_R16G16B16A16_UINT:
    case VK_FORMAT_R32_UINT: case VK_FORMAT_R32G32_UINT: case VK_FORMAT_R32G32B32_UINT:
    case VK_FORMAT_R32G32B32A32_UINT: case VK_FORMAT_A2B10G10R10_UINT_PACK32: kind = 1; break;
    case VK_FORMAT_R8_SINT: case VK_FORMAT_R8G8_SINT: case VK_FORMAT_R8G8B8_SINT:
    case VK_FORMAT_R8G8B8A8_SINT: case VK_FORMAT_R16_SINT: case VK_FORMAT_R16G16_SINT:
    case VK_FORMAT_R16G16B16_SINT: case VK_FORMAT_R16G16B16A16_SINT:
    case VK_FORMAT_R32_SINT: case VK_FORMAT_R32G32_SINT: case VK_FORMAT_R32G32B32_SINT:
    case VK_FORMAT_R32G32B32A32_SINT: kind = -1; break;
    default: break;
    }
    for (unsigned k = 0; k < 4; k++) {
      double v = p->is_uint ? (double)p->c.u[k] : p->c.f[k];
      if (kind > 0) value.color.uint32[k] = (uint32_t)fmin(fmax(v, 0), UINT32_MAX);
      else if (kind < 0) value.color.int32[k] = (int32_t)fmin(fmax(v, INT32_MIN), INT32_MAX);
      else value.color.float32[k] = v;
    }
  }
  // 清一个层/级不能丢掉其余子资源的原 CPU 初值。
  // Preserve original CPU contents in subresources outside the clear.
  if (i->aspect == VK_IMAGE_ASPECT_COLOR_BIT &&
      (i->levels > 1 || (uint32_t)nz != planes)) image_upload(i);
  view.set_layers = 1;
  for (int z = 0; z < nz; z++) {
    view.min_layer = first + z;
    clear_image(i, &view, i->aspect, &value, NULL);
  }
}
static VkBufferImageCopy copy_region(image *i, const p_copy_b2t *p,
                                     uint64_t row, uint64_t img) {
  VkImageSubresourceRange r = range(i, p->has_view ? &p->view : NULL);
  if (i->aspect != VK_IMAGE_ASPECT_COLOR_BIT ||
      (p->flags & ~NVN_COPY_FLAGS_ENGINE_2D_BIT))
    die("Vulkan image-copy aspect/flags pending: format=%#x decoded=%d "
        "flags=%#x",
        i->tex.d.format, i->decoded, p->flags);
  int bw, bh;
  uint32_t bpb = nvn_fmt_bpb(i->tex.d.format, &bw, &bh);
  if ((row && row % bpb) || (img && (!row || img % row)))
    die("Vulkan image copy stride invalid");
  VkBufferImageCopy c = {
      .bufferRowLength = row ? row / bpb * bw : 0,
      .bufferImageHeight = img ? img / row * bh : 0,
      .imageSubresource = {.aspectMask = i->aspect,
                           .mipLevel = r.baseMipLevel,
                           .baseArrayLayer = r.baseArrayLayer,
                           .layerCount = 1},
      .imageOffset = {p->r.xoffset, p->r.yoffset, p->r.zoffset},
      .imageExtent = {p->r.width, p->r.height, p->r.depth}};
  if (i->tex.d.target != NVN_TEXTURE_TARGET_3D) {
    c.imageSubresource.baseArrayLayer += p->r.zoffset;
    c.imageSubresource.layerCount = p->r.depth;
    c.imageOffset.z = 0;
    c.imageExtent.depth = 1;
  }
  return c;
}
static uint64_t copy_bytes(image *i, const p_copy_b2t *p, uint64_t row,
                           uint64_t img) {
  int bw, bh;
  int bpb = nvn_fmt_bpb(i->tex.d.format, &bw, &bh);
  uint64_t pitch = row ? row : (p->r.width + bw - 1) / bw * bpb;
  uint64_t slice = img ? img : pitch * ((p->r.height + bh - 1) / bh);
  return (p->r.depth - 1) * slice + ((p->r.height + bh - 1) / bh - 1) * pitch +
         (p->r.width + bw - 1) / bw * bpb;
}
// Pure logical copy footprint, before image_get/upload or any temporary can
// borrow physical resources. Keep original NVN compressed block geometry.
static uint64_t copy_pool_footprint(const p_copy_b2t *p,uint64_t row,uint64_t img) {
  int bw,bh,bpb=p->tex?nvn_fmt_bpb(p->tex->d.format,&bw,&bh):0;
  if(bpb<=0 || bw<=0 || bh<=0 || p->r.width<=0 || p->r.height<=0 || p->r.depth<=0)
    die("Vulkan invalid logical image-copy extent");
  if((row && row%(unsigned)bpb) || (img && (!row || img%row)))
    die("Vulkan image copy stride invalid");
  uint64_t blocks=((uint64_t)p->r.width+bw-1)/bw,lines=((uint64_t)p->r.height+bh-1)/bh;
  if(blocks>UINT64_MAX/(unsigned)bpb)die("Vulkan image copy row overflow");
  uint64_t last=blocks*bpb,pitch=row?row:last;
  if(lines>UINT64_MAX/pitch)die("Vulkan image copy slice overflow");
  uint64_t slice=img?img:pitch*lines,depth=(uint64_t)p->r.depth;
  if((depth-1) && slice>UINT64_MAX/(depth-1))die("Vulkan image copy depth overflow");
  uint64_t n=(depth-1)*slice,tail=(lines-1)*pitch;
  if(tail>UINT64_MAX-n || last>UINT64_MAX-n-tail)die("Vulkan image copy footprint overflow");
  n+=tail+last;return n;
}
#include "nvn_vk_image_retile.inc"
typedef struct {
  const p_copy_b2t *copy;
  const tex_t *retile;
  const p_copy_t2t *astc;
#ifdef DM_VULKAN_NATIVE_RESOURCES
  const tex_t *native_source,*native_destination;
#endif
  uint64_t row,img;
  int write;
} copy_pool_plan;
static int copy_pool_collect(void *context,vk_pool_requirement_sink sink,void *sink_context) {
  const copy_pool_plan *p=context;
#ifdef DM_VULKAN_NATIVE_RESOURCES
  if(!vk_texture_native_pool_collect(p->native_source,VK_PIPELINE_STAGE_TRANSFER_BIT,0,sink,sink_context) ||
     !vk_texture_native_pool_collect(p->native_destination,VK_PIPELINE_STAGE_TRANSFER_BIT,1,sink,sink_context))return 0;
#endif
  if(p->copy) {
    vk_pool_requirement r={p->copy->src,copy_pool_footprint(p->copy,p->row,p->img),
      0,VK_PIPELINE_STAGE_TRANSFER_BIT,p->write,0};
    if(!sink(sink_context,&r))return 0;
  }
#ifdef DM_VULKAN_SHARED_POOLS
  if(image_retile_logical(p->retile)) {
    vk_pool_requirement r={(uintptr_t)p->retile->mem,p->retile->L.size,
      V.props.limits.minStorageBufferOffsetAlignment,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,1,0};
    if(!sink(sink_context,&r))return 0;
  }
#endif
  if(p->astc && image_astc_copy_needs_span(p->astc)) {
    vk_pool_requirement r={(uintptr_t)p->astc->dst->mem,p->astc->dst->L.size,
      0,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0};
    if(!sink(sink_context,&r))return 0;
  }
  return 1;
}
static int copy_pool_operation(const p_copy_b2t *p,uint64_t row,uint64_t img,int write) {
  if(!V.options.pool_window_bytes)return 0;
  copy_pool_plan plan={.copy=p,.retile=write?NULL:p->tex,.row=row,.img=img,.write=write};
#ifdef DM_VULKAN_NATIVE_RESOURCES
  plan.native_source=write?p->tex:NULL;plan.native_destination=write?NULL:p->tex;
#endif
  return vk_pool_operation_begin(copy_pool_collect,&plan);
}
void vk_texture_copy_b2t(const p_copy_b2t *p, uint64_t row, uint64_t img) {
  // Copy needs an ended pass anyway. End/replay it before the final CPU
  // visibility proof, while no physical image or retile buffer is borrowed.
  vk_mip_clear_drain();vk_render_end();
  int operation=copy_pool_operation(p,row,img,0);
  // Admission may migrate storage and replay a deferred render pass.
  vk_render_end();vk_texture_cpu_preflight(p->tex);
  if(V.options.pool_defer_publication && p->tex && p->tex->d.format>=0x41 && p->tex->d.format<=0x50)
    vk_pool_cpu_prepare(p->src,copy_pool_footprint(p,row,img));
#ifdef DM_VULKAN_SHARED_POOLS
  shared_copy_backing canonical=image_retile_prepare(p->tex);
#endif
  image *i = image_get(p->tex);
  if(i->astc_copy_encoding || i->bc_copy_decoded)die("ASTC propagated copy cannot consume untyped logical BC3 Buffer bytes");
  image_upload(i);
  VkBufferImageCopy c = copy_region(i, p, row, img);
  vk_buffer *src;
  uint64_t off;
  uint64_t n = copy_bytes(i, p, row, img);
  if (!vk_pool_address_stage(p->src,n,(void **)&src,&off,VK_PIPELINE_STAGE_TRANSFER_BIT))
    die("Vulkan B2T outside pool");
  if (i->decoded) {
    // 原项目的 BC CPU 后备路径；源若在本次提交被 GPU
    // 改过，必须另走同步解码，不能读旧镜像。 Original project's BC CPU
    // fallback; if this submit wrote the source on GPU, synchronized decoding
    // is required instead of stale mirror reads.
    for (vk_effect *e = V.effects; e; e = e->next)
      if (e->src && (uintptr_t)e->dst < p->src + n &&
          (uintptr_t)e->dst + e->size > p->src)
        die("Vulkan BC copy source has pending GPU writes");
    int bw, bh;
    uint32_t bpb = nvn_fmt_bpb(i->tex.d.format, &bw, &bh);
    uint64_t pitch = row ? row : (p->r.width + bw - 1) / bw * bpb,
             slice = img ? img : pitch * ((p->r.height + bh - 1) / bh);
    uint64_t decoded_pitch = (uint64_t)p->r.width * i->bpp,
             decoded_slice = decoded_pitch * p->r.height;
    vk_buffer *decoded = vk_temp_buffer(decoded_slice * p->r.depth);
    for (int z = 0; z < p->r.depth; z++)
      if (nvn_bc_decode(i->tex.d.format,
                        (const uint8_t *)(uintptr_t)p->src + z * slice, pitch,
                        p->r.width, p->r.height,
                        decoded->map + z * decoded_slice, decoded_pitch))
        die("Vulkan BC buffer decode failed");
    src = decoded;
    off = 0;
    c.bufferRowLength = c.bufferImageHeight = 0;
  }
  c.bufferOffset = off;
  image_general_transfer(i);vk_transfer_barrier();
#ifdef DM_VULKAN_SHARED_POOLS
  int retile=image_retile_eligible(i,canonical,&c);
  vk_buffer *gather=NULL;
  uint64_t pitch=0,slice=0;
  if(retile) {
    int bw,bh,bpb=nvn_fmt_bpb(i->tex.d.format,&bw,&bh);
    pitch=row?row:((uint64_t)p->r.width+bw-1)/bw*bpb;
    slice=img?img:pitch*((p->r.height+bh-1)/bh);
    // Gather at the original GPU copy position, never from mutable CPU bytes.
    // It also makes a raw source alias of destination storage race-free.
    vk_buffer_use read_source={p->src,n,0},write_backing={(uintptr_t)i->tex.mem,i->tex.L.size,1};
    vk_transfer_buffer_dependencies(&read_source,1);
    vk_compute_buffer_dependencies(&write_backing,1,0);
    gather=vk_temp_buffer(n);
    VkBufferCopy snapshot={.srcOffset=off,.size=n};
    vkCmdCopyBuffer(V.cmd,src->handle,gather->handle,1,&snapshot);
  }
#endif
  image_record(i,image_layer_range(c.imageSubresource),VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,0);
  vkCmdCopyBufferToImage(V.cmd, src->handle, i->handle, VK_IMAGE_LAYOUT_GENERAL,
                         1, &c);
  i->initialized = 1;
#ifdef DM_VULKAN_SHARED_POOLS
  i->shared_writer=2;i->shared_writer_source=p->src;i->shared_writer_region=p->r;
  if(retile) {
    VkBufferMemoryBarrier ready={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT,
      .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
      .buffer=gather->handle,.offset=0,.size=n};
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,NULL,1,&ready,0,NULL);
    image_retile_publish(i,canonical,gather,0,pitch,slice,&c);
  }
#endif
  vk_pool_operation_end(operation);
}
void vk_texture_copy_t2b(const p_copy_b2t *p, uint64_t row, uint64_t img) {
  vk_texture_cpu_preflight(p->tex);
  int operation=copy_pool_operation(p,row,img,1);
  vk_mip_clear_drain();
  image *i = image_get(p->tex);
  if(i->astc_copy_encoding || i->bc_copy_decoded)die("ASTC propagated copy cannot export logical BC3 Buffer bytes");
  if (i->decoded)
    die("Vulkan decoded BC recompression pending");
  image_upload(i);
  VkBufferImageCopy c = copy_region(i, p, row, img);
  uint64_t n = copy_bytes(i, p, row, img);
  vk_buffer *dst;
  uint64_t off;
  if (!vk_pool_address_stage(p->src,n,(void **)&dst,&off,VK_PIPELINE_STAGE_TRANSFER_BIT))
    die("Vulkan T2B outside pool");
  c.bufferOffset = off;
  image_use(i,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);vk_transfer_barrier();
  image_record(i,image_layer_range(c.imageSubresource),VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT,0);
  vkCmdCopyImageToBuffer(V.cmd, i->handle, i->layout, dst->handle,
                         1, &c);
  vk_buffer_transfer_written_range(p->src,n);
  vk_buffer_writeback_stage(p->src, n, VK_PIPELINE_STAGE_TRANSFER_BIT);
  vk_pool_operation_end(operation);
}
static uint32_t full_zero_copy(image *s,VkImageSubresourceRange sr,image *d,
                              VkImageSubresourceRange dr,const p_copy_t2t *p) {
  return s!=d && s->format==d->format && zero_channels(s) && zero_channels(d) &&
      (s->zero_mips&zero_mip_mask(sr.baseMipLevel,1)) &&
      !p->sr.xoffset && !p->sr.yoffset && !p->sr.zoffset &&
      !p->dr.xoffset && !p->dr.yoffset && !p->dr.zoffset &&
      p->sr.depth==1 && p->dr.depth==1 &&
      p->sr.width==p->dr.width && p->sr.height==p->dr.height &&
      p->sr.width==(int)mip(s->extent.width,sr.baseMipLevel) &&
      p->sr.height==(int)mip(s->extent.height,sr.baseMipLevel) &&
      p->dr.width==(int)mip(d->extent.width,dr.baseMipLevel) &&
      p->dr.height==(int)mip(d->extent.height,dr.baseMipLevel)?
      zero_mip_mask(dr.baseMipLevel,1):0;
}
void vk_texture_copy_t2t(const p_copy_t2t *p) {
  vk_texture_cpu_preflight(p->src);vk_texture_cpu_preflight(p->dst);
  int operation=0;astc_copy_observation astc_observation;
#ifdef DM_VULKAN_SHARED_POOLS
  if(vk_pool_shared_active() && V.options.pool_window_bytes) {
    vk_mip_clear_drain();copy_pool_plan plan={.retile=p->dst,.astc=p};
#ifdef DM_VULKAN_NATIVE_RESOURCES
    plan.native_source=p->src;plan.native_destination=p->dst;
#endif
    operation=vk_pool_operation_begin(copy_pool_collect,&plan);
    // Union precedes exact ASTC qualification and every physical borrow.
    astc_observation=image_astc_copy_preflight(p);
  } else
#endif
  {
    // Preserve the original whole/legacy callback and observation order.
    astc_observation=image_astc_copy_preflight(p);vk_mip_clear_drain();
  }
#ifdef DM_VULKAN_SHARED_POOLS
  shared_copy_backing canonical=image_retile_prepare(p->dst);
#endif
  image *s = image_get(p->src), *d = image_get(p->dst);
  image_upload(s);
  // Metal resolves both textures through tex_mtl before copying. Preserve the
  // destination's packaged CPU contents outside the copied region/subresource,
  // and consume its dirty stamp before GPU writes so a later fetch cannot
  // upload older CPU data over this copy.
  image_upload(d);
  image_astc_copy_propagate(s,d,p,&astc_observation);
  if (p->flags &
      ~(NVN_COPY_FLAGS_ENGINE_2D_BIT | NVN_COPY_FLAGS_LINEAR_FILTER_BIT))
    die("Vulkan mirrored texture copy pending: flags=%#x", p->flags);
  if (s->aspect != d->aspect)
    die("Vulkan copy aspect mismatch");
  VkImageSubresourceRange sr = range(s, p->has_sv ? &p->sv : NULL),
                          dr = range(d, p->has_dv ? &p->dv : NULL);
  // Complete, same-format zero bits copied onto already zero bits do not
  // change any pixels. No artificial transfer writer or layout transition is
  // needed; future consumers still synchronize the actual prior clear/copy.
  uint32_t identical_zero=full_zero_copy(s,sr,d,dr,p);
  if(identical_zero && (d->zero_mips&identical_zero)) {
    atomic_fetch_add(&zero_copy_skips,1);vk_pool_operation_end(operation);return;
  }
  if(s==d) image_general_transfer(s);
  else image_use(s,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  image_general_transfer(d);
  if (s->format != d->format || p->sr.width != p->dr.width ||
      p->sr.height != p->dr.height || p->sr.depth != p->dr.depth) {
    VkFormatProperties source, dest;
    vk_format_properties(s->format, &source);
    vk_format_properties(d->format, &dest);
    VkFormatFeatureFlags filter =
        p->flags & NVN_COPY_FLAGS_LINEAR_FILTER_BIT
            ? VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT
            : 0;
    if (!(source.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) ||
        !(dest.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) ||
        (filter && !(source.optimalTilingFeatures & filter)) ||
        s->aspect != VK_IMAGE_ASPECT_COLOR_BIT)
      die("Vulkan format conversion unavailable: %u -> %u flags=%#x", s->format,
          d->format, p->flags);
    VkImageBlit b = {
        .srcSubresource = {s->aspect, sr.baseMipLevel, sr.baseArrayLayer, 1},
        .dstSubresource = {d->aspect, dr.baseMipLevel, dr.baseArrayLayer, 1},
        .srcOffsets = {{p->sr.xoffset, p->sr.yoffset, p->sr.zoffset},
                       {p->sr.xoffset + p->sr.width,
                        p->sr.yoffset + p->sr.height,
                        p->sr.zoffset + p->sr.depth}},
        .dstOffsets = {{p->dr.xoffset, p->dr.yoffset, p->dr.zoffset},
                       {p->dr.xoffset + p->dr.width,
                        p->dr.yoffset + p->dr.height,
                        p->dr.zoffset + p->dr.depth}}};
    if ((s->tex.d.target == NVN_TEXTURE_TARGET_3D) !=
        (d->tex.d.target == NVN_TEXTURE_TARGET_3D))
      die("Vulkan 3D/array blit pending");
    if (s->tex.d.target != NVN_TEXTURE_TARGET_3D) {
      if (p->sr.depth != p->dr.depth)
        die("Vulkan layer count mismatch");
      b.srcSubresource.baseArrayLayer += p->sr.zoffset;
      b.dstSubresource.baseArrayLayer += p->dr.zoffset;
      b.srcSubresource.layerCount = b.dstSubresource.layerCount = p->sr.depth;
      b.srcOffsets[0].z = b.dstOffsets[0].z = 0;
      b.srcOffsets[1].z = b.dstOffsets[1].z = 1;
    }
    image_transfer_pair(s,b.srcSubresource,d,b.dstSubresource);
    vkCmdBlitImage(V.cmd, s->handle, s->layout, d->handle,
                   VK_IMAGE_LAYOUT_GENERAL, 1, &b,
                   filter ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
    d->initialized = 1;
#ifdef DM_VULKAN_SHARED_POOLS
    d->shared_writer=3;d->shared_writer_source=s->tex.serial;d->shared_writer_region=p->dr;
#endif
    vk_pool_operation_end(operation);return;
  }
  VkImageCopy c = {
      .srcSubresource = {s->aspect, sr.baseMipLevel, sr.baseArrayLayer, 1},
      .dstSubresource = {d->aspect, dr.baseMipLevel, dr.baseArrayLayer, 1},
      .srcOffset = {p->sr.xoffset, p->sr.yoffset, p->sr.zoffset},
      .dstOffset = {p->dr.xoffset, p->dr.yoffset, p->dr.zoffset},
      .extent = {p->sr.width, p->sr.height, p->sr.depth}};
  if (s->tex.d.target != NVN_TEXTURE_TARGET_3D) {
    c.srcSubresource.baseArrayLayer += p->sr.zoffset;
    c.dstSubresource.baseArrayLayer += p->dr.zoffset;
    c.srcSubresource.layerCount = c.dstSubresource.layerCount = p->sr.depth;
    c.srcOffset.z = c.dstOffset.z = 0;
    c.extent.depth = 1;
  }
#ifdef DM_VULKAN_SHARED_POOLS
  VkBufferImageCopy publication={.imageSubresource=c.dstSubresource,.imageOffset=c.dstOffset,.imageExtent=c.extent};
  int retile=image_retile_eligible(d,canonical,&publication) && !s->decoded;
  vk_buffer *gather=NULL;uint64_t pitch=0,slice=0;
  if(retile) {
    int bw,bh,bpb=nvn_fmt_bpb(s->tex.d.format,&bw,&bh);
    pitch=((uint64_t)p->sr.width+bw-1)/bw*bpb;slice=pitch*((p->sr.height+bh-1)/bh);
    vk_buffer_use write={(uintptr_t)d->tex.mem,d->tex.L.size,1};
    vk_compute_buffer_dependencies(&write,1,0);
    gather=vk_temp_buffer(slice*c.srcSubresource.layerCount);
    VkBufferImageCopy read={.imageSubresource=c.srcSubresource,.imageOffset=c.srcOffset,.imageExtent=c.extent};
    image_transfer(s,image_layer_range(c.srcSubresource),0);
    vkCmdCopyImageToBuffer(V.cmd,s->handle,s->layout,gather->handle,1,&read);
  }
#endif
  image_transfer_pair(s,c.srcSubresource,d,c.dstSubresource);
  uint32_t copied_zero=full_zero_copy(s,sr,d,dr,p);
  for (unsigned aspect = 1; aspect <= VK_IMAGE_ASPECT_STENCIL_BIT; aspect <<= 1)
    if (s->aspect & aspect) {
      c.srcSubresource.aspectMask = c.dstSubresource.aspectMask = aspect;
      vkCmdCopyImage(V.cmd, s->handle, s->layout, d->handle,
                     VK_IMAGE_LAYOUT_GENERAL, 1, &c);
    }
  d->initialized = 1;
  d->zero_mips|=copied_zero;
#ifdef DM_VULKAN_SHARED_POOLS
  d->shared_writer=3;d->shared_writer_source=s->tex.serial;d->shared_writer_region=p->dr;
  if(retile) {
    VkBufferMemoryBarrier ready={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT,
      .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
      .buffer=gather->handle,.offset=0,.size=gather->size};
    vkCmdPipelineBarrier(V.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,NULL,1,&ready,0,NULL);
    image_retile_publish(d,canonical,gather,0,pitch,slice,&publication);
  }
#endif
  vk_pool_operation_end(operation);
}

typedef struct {
  vk_buffer *buffer;
  uint8_t *dst;
  size_t row, image, width, rows;
  unsigned slices;
} texture_readback;
static void read_texels_done(void *opaque) {
  const texture_readback *r = opaque;
  for (unsigned z = 0; z < r->slices; z++)
    for (size_t y = 0; y < r->rows; y++) {
      uint8_t *dst = r->dst + z * r->image + y * r->row;
      dm_guest_write_prepare(dst, r->width);
      memcpy(dst, r->buffer->map + (z * r->rows + y) * r->width, r->width);
    }
}
void mtl_read_texels(const tex_t *t, unsigned level, unsigned layer,
                     const NVNcopyRegion *r, uint8_t *dst, size_t row,
                     size_t img) {
  if (V.cmd)
    die("Vulkan synchronous read during recording");
  mtl_sub_begin();
  vk_texture_cpu_preflight(t);
#ifdef DM_VULKAN_NATIVE_RESOURCES
  int native_operation=native_pool_operation(t,VK_PIPELINE_STAGE_TRANSFER_BIT,0);
#endif
  image *i = image_get(t);
  if(i->astc_copy_encoding || i->bc_copy_decoded)die("ASTC propagated copy cannot ReadTexels as logical BC3 bytes");
  image_upload(i);
  p_copy_b2t p = {.tex = t,
                  .has_view = 1,
                  .view = {.set_levels = 1,
                           .set_layers = 1,
                           .base_level = level,
                           .min_layer = layer},
                  .r = *r};
  if (!p.r.depth) p.r.depth = 1;
  // Match Metal's tight GPU readback followed by CPU row copies. Destination
  // strides need not obey Vulkan buffer-copy alignment, and padding is not data.
  VkBufferImageCopy c = copy_region(i, &p, 0, 0);
  uint64_t n = copy_bytes(i, &p, 0, 0);
  vk_buffer *b = vk_temp_buffer(n);
  image_use(i,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);image_transfer(i,image_layer_range(c.imageSubresource),0);
  vkCmdCopyImageToBuffer(V.cmd, i->handle, i->layout, b->handle,
                         1, &c);
  vk_buffer_transfer_written();
  int bw, bh;
  unsigned bpb = nvn_fmt_bpb(t->d.format, &bw, &bh);
  size_t width = (p.r.width + bw - 1) / bw * bpb;
  size_t rows = (p.r.height + bh - 1) / bh;
  if (!row) row = width;
  if (!img) img = row * rows;
  texture_readback readback = {b, dst, row, img, width, rows, p.r.depth};
  size_t written = (p.r.depth - 1) * img + (rows - 1) * row + width;
  // Publish while this submit still owns the staging buffer. The synchronous
  // wait also keeps the callback's stack context alive through CPU publication.
  vk_completion_write(read_texels_done, &readback, dst, written);
#ifdef DM_VULKAN_NATIVE_RESOURCES
  vk_pool_operation_end(native_operation);
#endif
  uint64_t fence = vk_internal_commit();
  vk_internal_wait(fence);
}
void mtl_tex_release(tex_t *t) {
  if (!t->serial)
    return;
  retired_image *r = malloc(sizeof *r);
  if (!r)
    die("Vulkan image retirement allocation");
  r->serial = t->serial;
  r->at = nvn_submissions_enqueued();
  pthread_mutex_lock(&V.resources);
  r->next = retired_images;
  retired_images = r;
  pthread_mutex_unlock(&V.resources);
}
uint32_t vk_texture_count(void) { return atomic_load(&live_images); }
static void image_destroy(image *i) {
  image_list_remove(i);
#ifdef DM_VULKAN_SHARED_POOLS
  if(i->astc_copy_encoding || i->bc_copy_decoded) {if(!astc_copy_live)die("ASTC copy live count underflow");astc_copy_live--;}
  image_shared_alias_remove(i);
  nvn_dirty_scope_release(i->shared_dirty_scope);i->shared_dirty_scope=NULL;
#endif
  image_recipes_changed();
  image **entry = &image_buckets[i->tex.serial % 8192];
  while (*entry && *entry != i)
    entry = &(*entry)->hash_next;
  if (*entry)
    *entry = i->hash_next;
  atomic_fetch_sub(&live_images, 1);
  image_storage_destroy(i);
}

#ifdef DM_VULKAN_SHARED_POOLS
void vk_texture_shared_source_written(uint64_t address,uint64_t bytes) {
  if(!bytes || bytes>UINT64_MAX-address)return;
  // The known GPU write publishes page generation/epoch. Every converted
  // image's existing dirty/stamp proof therefore fails before reuse; actual
  // ASTC proof describes old physical bytes until original upload replaces it.
  if(!vk_pool_shared_active()) {
    if(vk_legacy_astc_copy_mode())vk_texture_shared_linear_access(address,bytes);
    return;
  }
#ifdef DM_VULKAN_NATIVE_RESOURCES
  native_image_linear_written(address,bytes);
#endif
  nvn_dirty_scope_gpu_write((void *)(uintptr_t)address,bytes);
}
static int image_shared_alias_eligible(const alias_range_node *node,int shared) {
  const image *i=node->owner;
  // Initialized/encoding can change without membership changes; do not cache
  // these predicates in the tree or make a negative address prediction.
  return i->initialized && (shared || i->astc_copy_encoding) && i->tex.mem && i->tex.L.size;
}
int vk_texture_shared_linear_conflict(uint64_t address,uint64_t bytes) {
  if(!bytes)return 0;
  if(bytes>UINT64_MAX-address)return 1;
  if(!shared_gpu_alias_ranges)return 0;
  // Shared mode is fixed before guest execution, not a per-node property.
  return alias_negative_overlap(&shared_alias_negative,shared_gpu_alias_ranges,
      address,address+bytes,image_shared_alias_eligible,vk_pool_shared_active());
}
#ifdef DM_VULKAN_TEST_EMBED
// Functional proof oracle only: allow the same empty-tree geometric query to
// populate its valid negative entry, so first-ever real writers are tested too.
int vk_test_shared_alias_negative_prime(uint64_t address,uint64_t bytes) {
  if(!bytes || bytes>UINT64_MAX-address)return 0;
  if(alias_negative_overlap(&shared_alias_negative,shared_gpu_alias_ranges,
      address,address+bytes,image_shared_alias_eligible,vk_pool_shared_active()))return 0;
  return alias_negative_matches(&shared_alias_negative,address,address+bytes);
}
#endif
static int image_copy_encoding_eligible(const alias_range_node *node,int ignored) {
  (void)ignored;const image *i=node->owner;
  return i->initialized && (i->astc_copy_encoding || i->bc_copy_decoded);
}
void vk_texture_copy_encoding_access(uint64_t address,uint64_t bytes) {
  if(!bytes)return;
  if(bytes>UINT64_MAX-address || (astc_copy_live && alias_range_overlap(shared_gpu_alias_ranges,
      address,address+bytes,image_copy_encoding_eligible,0)))
    die("Compressed image physical conversion cannot publish logical BC3 cache bytes");
}
void vk_texture_shared_linear_access(uint64_t address,uint64_t bytes) {
  if(vk_texture_shared_linear_conflict(address,bytes))
    die("Shared linear access aliases GPU-owned optimal image: explicit conversion required");
}
void vk_texture_shared_pool_close(uint64_t address,uint64_t bytes) {
  // Original mtl_pool_del called finish first; no physical users remain.
  image **at=&images;
  while(*at) {image *i=*at;
    if((vk_pool_shared_active() || i->astc_copy_encoding) && i->tex.mem && i->tex.L.size && address<(uintptr_t)i->tex.mem+i->tex.L.size &&
        address+bytes>(uintptr_t)i->tex.mem) {image_destroy(i);}else at=&i->next;
  }
}
#endif

void mtl_pkg_stats(uint64_t *u, uint64_t *r) {
  *u = uploads;
  *r = reuploads;
}
static void sampler_shutdown(void);
static void texture_binding_scope_shutdown(void);
static void texture_lookup_scope_shutdown(void);
void vk_texture_shutdown(void) {
  mtl_finish();
  sampler_shutdown();
  texture_binding_scope_shutdown();
  texture_lookup_scope_shutdown();
  while (images) {
    image *i = images;
    image_destroy(i);
  }
  for(unsigned b=0;b<sizeof image_batches/sizeof *image_batches;b++) {
    free(image_batches[b].requests);image_batches[b]=(image_batch){0};
  }
  image_binding_active=0;
  image_write_clock=image_pass_begin_clock=0;
  image_reference_visible_clock=0;
}

// 描述符保留对象快照；Vulkan 视图活到引用它的提交真正完成。
// Descriptors retain object snapshots; Vulkan views live until referencing
// submissions actually complete.
typedef struct {
  tex_t texture;
  view_t view;
  int valid, has_view;
} texture_descriptor;
typedef struct {
  samp_par params;
  int valid;
  VkSampler sampler;
  uint64_t last_use;
} sampler_descriptor;
static texture_descriptor *texture_descriptors;
static sampler_descriptor *sampler_descriptors;
static atomic_uint_fast64_t sampler_registry_epoch;
static atomic_int sampler_lookup_requested,sampler_lookup_active;
static int sampler_lookup_reference;
// Recording-owner cache contains handles, never pointers into the mutable
// registry. The first locked lookup already marks this exact submit's use.
static struct { int id;VkSampler sampler;uint64_t submit,epoch; } sampler_lookups[64];
static uint64_t sampler_lookup_hits,sampler_lookup_misses;
static atomic_uint_fast64_t published_sampler_lookup_hits,published_sampler_lookup_misses;
void vk_sampler_lookup_reference(int reference) { atomic_store(&sampler_lookup_requested,!!reference); }
void vk_sampler_lookup_stats(uint64_t *hits,uint64_t *misses,int *reference) {
  *hits=atomic_load(&published_sampler_lookup_hits);*misses=atomic_load(&published_sampler_lookup_misses);
  *reference=atomic_load(&sampler_lookup_active);
}
static void sampler_registry_changed(void) {
  if(atomic_fetch_add_explicit(&sampler_registry_epoch,1,memory_order_release)==UINT64_MAX)
    die("Vulkan sampler registry epoch overflow");
}
static int texture_capacity, sampler_capacity, texture_limit = 1048576;
static atomic_uint_fast64_t descriptor_epoch;
uint64_t vk_texture_descriptor_epoch(void) { return atomic_load(&descriptor_epoch); }
// All logical descriptor publishers hold resources. Never publish a wrapped
// epoch: a value-only cache may legally survive any number of draws.
static void texture_registry_changed(void) {
  uint64_t prior=atomic_load_explicit(&descriptor_epoch,memory_order_relaxed);
  if(prior==UINT64_MAX)die("Vulkan texture descriptor epoch overflow");
  atomic_store_explicit(&descriptor_epoch,prior+1,memory_order_release);
}
typedef struct retired_sampler {
  struct retired_sampler *next;
  VkSampler sampler;
  uint64_t at, last_use;
} retired_sampler;
static retired_sampler *retired_samplers;
static _Atomic uint32_t live_samplers;
static VkSampler dummy_sampler;
uint32_t vk_sampler_count(void) { return atomic_load(&live_samplers); }
static void sampler_shutdown(void) {
  sampler_registry_changed();
  for (int i = 0; i < sampler_capacity; i++)
    if (sampler_descriptors[i].sampler)
      vkDestroySampler(V.device, sampler_descriptors[i].sampler, NULL);
  while (retired_samplers) {
    retired_sampler *old = retired_samplers;
    retired_samplers = old->next;
    vkDestroySampler(V.device, old->sampler, NULL);
    free(old);
  }
  if (dummy_sampler)
    vkDestroySampler(V.device, dummy_sampler, NULL);
  dummy_sampler = VK_NULL_HANDLE;
  free(sampler_descriptors);
  sampler_descriptors = NULL;
  sampler_capacity = 0;
  atomic_store(&live_samplers, 0);
}
typedef struct transient_view {
  struct transient_view *next;
  struct transient_view *hash_next;
  VkBufferView buffer;
  VkBuffer source;
  VkFormat format;
  VkDeviceSize offset,range;
} transient_view;
typedef struct {
  uint64_t epoch,lifetime_generation;
  int id,storage,has_view;
  // Logical descriptor fact only: no physical image/view pointer is trusted.
  // Occupies the former four-byte alignment gap before owner.
  int preflight_trivial;
  image *owner;
  view_t view;
  VkImageViewCreateInfo description;
  VkImageSubresourceRange access_range;
  VkImageView handle;
  int astc_decode8;
} texture_binding_recipe;
struct vk_texture_submission {
  transient_view *views;
  transient_view *texel_views[256];
  texture_binding_recipe bindings[256];
};
static vk_texture_submission *texture_state;
static atomic_int texture_binding_requested,texture_binding_active;
static int texture_binding_reference;
static uint64_t texture_binding_hits,texture_prepare_hits,texture_binding_misses;
static atomic_uint_fast64_t published_binding_hits,published_prepare_hits,published_binding_misses;
void vk_texture_binding_reference(int reference) { atomic_store(&texture_binding_requested,!!reference); }
void vk_texture_binding_stats(uint64_t *views,uint64_t *prepares,uint64_t *misses,int *reference) {
  *views=atomic_load(&published_binding_hits);*prepares=atomic_load(&published_prepare_hits);
  *misses=atomic_load(&published_binding_misses);*reference=atomic_load(&texture_binding_active);
}
static unsigned texture_binding_slot(int id,int storage) {
  // Refine each former 64-slot bucket into four. Shifting this result by two
  // recovers the original slot (including sampled/storage separation), so
  // increasing capacity cannot introduce collisions between former buckets.
  return ((uint32_t)id*UINT32_C(2654435761)>>24)^((unsigned)!!storage<<2);
}
#ifdef DM_VULKAN_TEST_EMBED
unsigned vk_test_texture_binding_slot(int id,int storage) {
  return texture_binding_slot(id,storage);
}
#endif
static texture_binding_recipe *texture_binding_find(int id,int storage) {
  if(texture_binding_reference || !texture_state || !V.cmd) return NULL;
  uint64_t submit=vk_recording_serial();if(!submit) return NULL;
  texture_binding_recipe *r=&texture_state->bindings[texture_binding_slot(id,storage)];
  // Image views belong to logical images, not recycled submission contexts.
  // The lifetime check precedes every owner dereference; registry replacement
  // rejects the descriptor independently. A promotion keeps owner identity but
  // changes its physical handle. Each real access still renews last_use below.
  if(r->lifetime_generation==image_recipe_generation &&
      r->epoch==atomic_load(&descriptor_epoch) &&
      r->id==id && r->storage==!!storage && r->owner->handle==r->description.image) return r;
  return NULL;
}
// Logical descriptor handoff only: no physical image, view, layout, upload or
// producer state survives here. Preflight and binding still run their original
// physical recipe checks first. Recording-owner scopes isolate nested ghost
// draws; direct-map collisions simply discard an optional handoff.
#define TEXTURE_BINDING_SCOPE_DEPTH 8
#define TEXTURE_BINDING_SCOPE_SLOTS 128
typedef struct {
  uint64_t generation,submit,epoch;
  int id,storage;
  texture_descriptor descriptor;
} texture_binding_snapshot;
typedef struct {
  uint64_t generation;
  texture_binding_snapshot *snapshots;
  int allocation_attempted;
} texture_binding_scope;
static texture_binding_scope texture_binding_scopes[TEXTURE_BINDING_SCOPE_DEPTH];
static unsigned texture_binding_scope_depth;
int vk_texture_binding_scope_active(void) { return texture_binding_scope_depth!=0; }
static uint64_t texture_binding_scope_clock;
static int texture_binding_scope_overflow;
void vk_texture_binding_scope_begin(void) {
  // Depth beyond the bounded stack never borrows its parent's snapshots.
  unsigned depth=texture_binding_scope_depth++;
  if(depth>=TEXTURE_BINDING_SCOPE_DEPTH)return;
  if(!texture_binding_scope_overflow && !++texture_binding_scope_clock)
    texture_binding_scope_overflow=1;
  texture_binding_scopes[depth].generation=texture_binding_scope_overflow?0:texture_binding_scope_clock;
}
void vk_texture_binding_scope_end(void) {
  if(!texture_binding_scope_depth)die("Vulkan texture binding scope underflow");
  --texture_binding_scope_depth;
}
static texture_binding_scope *texture_binding_scope_current(void) {
  if(texture_binding_reference || !texture_state || !V.cmd ||
      !texture_binding_scope_depth || texture_binding_scope_depth>TEXTURE_BINDING_SCOPE_DEPTH)
    return NULL;
  texture_binding_scope *scope=&texture_binding_scopes[texture_binding_scope_depth-1];
  return scope->generation?scope:NULL;
}
static unsigned texture_binding_snapshot_slot(int id,int storage) {
  return ((uint32_t)id*UINT32_C(2654435761)>>25)^!!storage;
}
static void texture_binding_snapshot_save(int id,int storage,
    const texture_descriptor *descriptor,uint64_t epoch) {
  texture_binding_scope *scope=texture_binding_scope_current();if(!scope)return;
  uint64_t submit=vk_recording_serial();if(!submit)return;
  if(!scope->allocation_attempted) {
    scope->allocation_attempted=1;
    scope->snapshots=calloc(TEXTURE_BINDING_SCOPE_SLOTS,sizeof *scope->snapshots);
  }
  if(!scope->snapshots)return;
  texture_binding_snapshot *snapshot=&scope->snapshots[texture_binding_snapshot_slot(id,storage)];
  *snapshot=(texture_binding_snapshot){.generation=scope->generation,.submit=submit,
      .epoch=epoch,.id=id,.storage=!!storage,.descriptor=*descriptor};
}
static int texture_binding_snapshot_copy(int id,int storage,
    texture_descriptor *descriptor,uint64_t *epoch) {
  texture_binding_scope *scope=texture_binding_scope_current();
  if(!scope || !scope->snapshots)return 0;
  const texture_binding_snapshot *snapshot=&scope->snapshots[texture_binding_snapshot_slot(id,storage)];
  if(snapshot->generation!=scope->generation || snapshot->id!=id || snapshot->storage!=!!storage ||
      snapshot->submit!=vk_recording_serial() || snapshot->epoch!=atomic_load(&descriptor_epoch))return 0;
  // Copy before render-end/upload callbacks can re-enter binding or a scope can
  // be reused. No pointer into this table or the registry escapes this helper.
  *descriptor=snapshot->descriptor;*epoch=snapshot->epoch;return 1;
}
static void texture_binding_scope_shutdown(void) {
  for(unsigned n=0;n<TEXTURE_BINDING_SCOPE_DEPTH;n++) {
    free(texture_binding_scopes[n].snapshots);
    texture_binding_scopes[n]=(texture_binding_scope){0};
  }
  texture_binding_scope_depth=0;texture_binding_scope_clock=0;texture_binding_scope_overflow=0;
}
// Pure registry values persist across draws and physical recordings. Only the
// recording owner's TLS scope may use this one table; producer threads keep
// the original locked lookup. A nested ghost may evict a slot: callers copy
// every value before any callback, so no table pointer can span that eviction.
#define TEXTURE_LOOKUP_DEPTH 8
#define TEXTURE_LOOKUP_SLOTS 128
typedef struct {
  uint64_t epoch;
  int id;
  texture_descriptor descriptor;
} texture_lookup_snapshot;
static texture_lookup_snapshot *texture_lookup_snapshots;
static int texture_lookup_allocation_attempted;
static _Thread_local unsigned texture_lookup_depth;
static _Thread_local int texture_lookup_current;
void vk_texture_lookup_scope_begin(void) {
  unsigned depth=texture_lookup_depth++;
  texture_lookup_current=0;
  if(depth>=TEXTURE_LOOKUP_DEPTH)return;
  if(!texture_lookup_allocation_attempted) {
    texture_lookup_allocation_attempted=1;
    texture_lookup_snapshots=calloc(TEXTURE_LOOKUP_SLOTS,sizeof *texture_lookup_snapshots);
    if(texture_lookup_snapshots)
      for(unsigned k=0;k<TEXTURE_LOOKUP_SLOTS;k++)texture_lookup_snapshots[k].id=-1;
  }
  texture_lookup_current=texture_lookup_snapshots!=NULL;
}
void vk_texture_lookup_scope_end(void) {
  if(!texture_lookup_depth)die("Vulkan texture lookup scope underflow");
  unsigned depth=--texture_lookup_depth;
  texture_lookup_current=depth && depth<=TEXTURE_LOOKUP_DEPTH && texture_lookup_snapshots;
}
static void texture_lookup_scope_shutdown(void) {
  free(texture_lookup_snapshots);texture_lookup_snapshots=NULL;
  texture_lookup_allocation_attempted=0;
  texture_lookup_current=0;texture_lookup_depth=0;
}
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
// Cold fixture metadata only; no production observer/counter.
uintptr_t vk_test_texture_lookup_entry(int id) {
  if(!texture_lookup_snapshots)return 0;
  texture_lookup_snapshot *s=&texture_lookup_snapshots[(uint32_t)id*UINT32_C(2654435761)>>25];
  return s->id==id?(uintptr_t)s:0;
}
void vk_test_texture_descriptor_epoch_exhaustion(void) {
  pthread_mutex_lock(&V.resources);
  atomic_store(&descriptor_epoch,UINT64_MAX);
  texture_registry_changed();
  pthread_mutex_unlock(&V.resources);
}
#endif
// Returned values are copied by the caller before any callback. Never expose
// a table/registry pointer through the public API or retain CPU-ready proofs.
static const texture_descriptor *texture_descriptor_lookup(int id,
    texture_descriptor *fallback,uint64_t *epoch) {
  texture_lookup_snapshot *slot=texture_lookup_current?
      &texture_lookup_snapshots[(uint32_t)id*UINT32_C(2654435761)>>25]:NULL;
  if(slot && slot->id==id &&
      slot->epoch==atomic_load(&descriptor_epoch)) {
    if(epoch)*epoch=slot->epoch;
    return slot->descriptor.valid?&slot->descriptor:NULL;
  }
  pthread_mutex_lock(&V.resources);
  int valid=id>=0 && id<texture_capacity && id<texture_limit && texture_descriptors[id].valid;
  uint64_t observed=slot || epoch?atomic_load(&descriptor_epoch):0;
  texture_descriptor *out=slot?&slot->descriptor:fallback;
  if(valid)*out=texture_descriptors[id];else out->valid=0;
  if(slot) {slot->epoch=observed;slot->id=id;}
  pthread_mutex_unlock(&V.resources);
  if(epoch)*epoch=observed;
  return valid?out:NULL;
}
static _Atomic uint64_t texel_view_hits,texel_view_creates;
void vk_texel_view_stats(uint64_t *hits,uint64_t *creates) {
  *hits=atomic_load(&texel_view_hits);*creates=atomic_load(&texel_view_creates);
}
static int texel_view_reference(void) {
  static int reference=-1;
  if(reference<0) {
    const char *v=getenv("DM_VULKAN_TEXEL_VIEW_REFERENCE");
    if(v && strcmp(v,"0") && strcmp(v,"1")) die("DM_VULKAN_TEXEL_VIEW_REFERENCE expects 0 or 1");
    reference=v && !strcmp(v,"1");
  }
  return reference;
}
vk_texture_submission *vk_texture_begin(vk_texture_submission *state) {
  sampled_array_layout_reference=atomic_load(&sampled_array_layout_requested);
  atomic_store(&sampled_array_layout_active,sampled_array_layout_reference);
  texture_binding_reference=atomic_load(&texture_binding_requested);
  atomic_store(&texture_binding_active,texture_binding_reference);
  sampler_lookup_reference=atomic_load(&sampler_lookup_requested);
  atomic_store(&sampler_lookup_active,sampler_lookup_reference);
  texture_dirty_mode_begin();
  depth_layout_init();
  compute_layout_init();
  if (texture_state) die("Vulkan nested texture recording");
  if (!state) {
    state = calloc(1, sizeof *state);
    if (!state) die("Vulkan texture resources allocation");
  }
  if (state->views) die("Vulkan texture resources reused before recycling");
  int reference=atomic_load(&layout_requested);
  if(reference<0) {
    const char *setting=getenv("DM_VULKAN_IMAGE_LAYOUT_REFERENCE");
    if(setting && strcmp(setting,"0") && strcmp(setting,"1"))
      die("DM_VULKAN_IMAGE_LAYOUT_REFERENCE expects 0 or 1");
    reference=setting && strcmp(setting,"0");atomic_store(&layout_requested,reference);
  }
  layout_reference=reference;atomic_store(&layout_active,reference);
  texture_state = state;
  return state;
}
void vk_texture_end(vk_texture_submission *state) {
  if (!state || texture_state != state) die("Vulkan texture ownership at commit");
  if(image_binding_active || image_binding_count) die("Vulkan image operation pending at commit");
  // Single recording producer, publish logical totals once per submit rather
  // than adding an atomic RMW to every texture binding.
  atomic_store_explicit(&published_texture_dirty_hits,texture_dirty_hits,memory_order_relaxed);
  atomic_store_explicit(&published_texture_dirty_checks,texture_dirty_checks,memory_order_relaxed);
  atomic_store_explicit(&layout_transitions,image_layout_recorded.transitions,memory_order_relaxed);
  atomic_store_explicit(&layout_safe_uses,image_layout_recorded.safe,memory_order_relaxed);
  atomic_store_explicit(&layout_fallbacks,image_layout_recorded.fallback,memory_order_relaxed);
  atomic_store_explicit(&compute_optimal_samples,image_layout_recorded.compute,memory_order_relaxed);
  atomic_store_explicit(&depth_attachment_uses,image_layout_recorded.depth_attachment,memory_order_relaxed);
  atomic_store_explicit(&depth_sampled_uses,image_layout_recorded.depth_sampled,memory_order_relaxed);
  atomic_store(&published_sampler_lookup_hits,sampler_lookup_hits);
  atomic_store(&published_sampler_lookup_misses,sampler_lookup_misses);
  atomic_store(&published_binding_hits,texture_binding_hits);
  atomic_store(&published_prepare_hits,texture_prepare_hits);
  atomic_store(&published_binding_misses,texture_binding_misses);
  texture_state = NULL;
}
void vk_texture_recycle(vk_texture_submission *state) {
  if (!state) return;
  if (texture_state == state) die("Vulkan recycling recording texture resources");
  while (state->views) {
    transient_view *v = state->views;
    state->views = v->next;
    vkDestroyBufferView(V.device, v->buffer, NULL);
    free(v);
  }
  memset(state->texel_views,0,sizeof state->texel_views);
}
void vk_texture_destroy(vk_texture_submission *state) {
  vk_texture_recycle(state);
  free(state);
}
void vk_texture_own_buffer_view(VkBufferView view) {
  if(!texture_state || !view) die("Vulkan buffer view outside a submission");
  transient_view *v=calloc(1,sizeof *v);
  if(!v) die("Vulkan buffer-view ownership allocation");
  v->next=texture_state->views;v->buffer=view;texture_state->views=v;
}
static VkBufferView texel_view(VkBuffer buffer,VkFormat format,VkDeviceSize offset,VkDeviceSize range) {
  if (!texture_state) die("Vulkan buffer view outside a submission");
  uint64_t hash=((uint64_t)buffer>>4)^((uint64_t)format<<7)^offset^(offset>>19)^range^(range>>13);
  unsigned bucket=hash%256;
  if(!texel_view_reference())
    for(transient_view *v=texture_state->texel_views[bucket];v;v=v->hash_next)
      if(v->source==buffer && v->format==format && v->offset==offset && v->range==range) {
        atomic_fetch_add(&texel_view_hits,1);return v->buffer;
      }
  VkBufferViewCreateInfo ci={.sType=VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
    .buffer=buffer,.format=format,.offset=offset,.range=range};
  VkBufferView view;
  VK_CHECK(vkCreateBufferView(V.device,&ci,NULL,&view));
  transient_view *v = malloc(sizeof *v);
  if (!v) die("Vulkan buffer-view ownership allocation");
  *v=(transient_view){.next=texture_state->views,.hash_next=texture_state->texel_views[bucket],
    .buffer=view,.source=buffer,.format=format,.offset=offset,.range=range};
  texture_state->views = v;
  texture_state->texel_views[bucket]=v;
  atomic_fetch_add(&texel_view_creates,1);
  return view;
}
void vk_texture_end_submit(void) {
  // 保留翻译快照宽限期，同时检查所有内部/客体提交的真实完成序号。
  // Preserve translation-snapshot grace, independently checking actual
  // completion of every referencing guest/internal submission.
  uint64_t translated = mtl_last_fence(), done = vk_completed_serial();
  pthread_mutex_lock(&V.resources);
  retired_sampler **sp = &retired_samplers;
  while (*sp) {
    retired_sampler *s = *sp;
    if (translated < s->at || translated - s->at < 8 || done < s->last_use) {
      sp = &s->next;
      continue;
    }
    vkDestroySampler(V.device, s->sampler, NULL);
    atomic_fetch_sub(&live_samplers, 1);
    *sp = s->next;
    free(s);
  }
  retired_image **rp = &retired_images;
  while (*rp) {
    retired_image *r = *rp;
    if (translated < r->at) {
      rp = &r->next;
      continue;
    }
#ifdef DM_VULKAN_SHARED_POOLS
    int alias_mode=vk_pool_shared_active() || vk_legacy_astc_copy_mode();
#else
    int alias_mode=0;
#endif
    // Keep the original grace-only lookup cost for the ordinary legacy path.
    if (!alias_mode && translated-r->at<8) {
      rp = &r->next;
      continue;
    }
    image **ip = &images;
    while (*ip && (*ip)->tex.serial != r->serial)
      ip = &(*ip)->next;
#ifdef DM_VULKAN_SHARED_POOLS
    // Logical NVN retirement ends guest-byte authority after every previously
    // enqueued user has translated and every physical GPU user has completed.
    // The extra snapshot/cache grace retains the VkImage, not ownership of
    // recycled guest addresses. This owner-only list edit never writes bytes.
    if (alias_mode && *ip && (*ip)->last_use <= done)
      image_shared_alias_remove(*ip);
#endif
    if (translated < r->at || translated - r->at < 8) {
      rp = &r->next;
      continue;
    }
    if (*ip && (*ip)->last_use > done) {
      rp = &r->next;
      continue;
    }
    if (*ip) {
      image *i = *ip;
      image_destroy(i);
    }
    for (int id = 0; id < texture_capacity; id++)
      if (texture_descriptors[id].texture.serial == r->serial) {
        texture_descriptors[id].valid = 0;
        texture_registry_changed();
      }
    *rp = r->next;
    free(r);
  }
  pthread_mutex_unlock(&V.resources);
#ifdef DM_VULKAN_NATIVE_RESOURCES
  vk_pool_native_image_collect();
#endif
}
// Called only by the existing recording-owner low-memory control boundary.
// Retain logical NVN descriptors: a later use recreates and fully uploads the
// image through image_get_usage. Never discard a GPU-authored result, wait for
// the GPU, or scan on ordinary draws/submissions. Age is queue chunks, not FPS.
unsigned vk_texture_trim_idle(void) {
  if(V.cmd || image_binding_active || vk_texture_binding_scope_active())return 0;
  uint64_t done=vk_completed_serial();unsigned removed=0;
  pthread_mutex_lock(&V.resources);
  image **at=&images;
  while(*at) {
    image *i=*at;
    if(!i->cpu_rebuildable || !i->initialized || !i->tex.has_layout ||
        !i->tex.mem || !i->tex.L.size || done<i->last_use || done-i->last_use<256) {
      at=&i->next;continue;
    }
    image_destroy(i);removed++;
  }
  pthread_mutex_unlock(&V.resources);
  if(removed)LOG("[vulkan] memory pressure retired %u cold CPU-backed images",removed);
  return removed;
}
// Foreground memory pressure notifications are not supplied on Android 34+.
// Bound the reconstructible private cache independently, at a guest-submit
// owner boundary. This is a soft budget: hot, GPU-authored and in-flight
// resources may exceed it. Never wait, touch guest bytes or evict native imports.
void vk_texture_budget_maintain(void) {
  if(!V.device || atomic_load(&rebuildable_private_bytes)<=private_image_budget ||
      V.cmd || vk_recording_serial() || image_binding_active || vk_texture_binding_scope_active())return;
  uint64_t done=vk_completed_serial(),freed=0;
  unsigned scanned=0,removed=0;
  pthread_mutex_lock(&V.resources);
  if(!private_image_cursor)private_image_cursor=images;
  while(private_image_cursor && scanned<32 && removed<4 && freed<(8ull<<20) &&
      atomic_load(&rebuildable_private_bytes)>private_image_budget) {
    image *i=private_image_cursor;private_image_cursor=i->next;
    scanned++;
    if(i->allocation_bytes && i->cpu_rebuildable && !i->external_access &&
        i->initialized && i->tex.has_layout && i->tex.mem && i->tex.L.size &&
        done>=i->last_use && done-i->last_use>=256) {
#ifdef DM_VULKAN_NATIVE_RESOURCES
      if(i->native_entry)die("Native image cannot own private allocation bytes");
#endif
      freed+=i->allocation_bytes;
      atomic_fetch_add(&private_cache_released_bytes,i->allocation_bytes);
      image_destroy(i);removed++;
    }
    if(!private_image_cursor)break; // resume from head at the next boundary
  }
  pthread_mutex_unlock(&V.resources);
}
void vk_texture_cache_stats(uint64_t *total,uint64_t *rebuildable,uint64_t *budget,uint64_t *released) {
  *total=atomic_load(&private_image_bytes);
  *rebuildable=atomic_load(&rebuildable_private_bytes);
  *budget=private_image_budget;
  *released=atomic_load(&private_cache_released_bytes);
}
#ifdef DM_VULKAN_TEST_EMBED
void vk_test_texture_budget(uint64_t budget,uint64_t *bytes) {
  if(V.cmd || image_binding_active || vk_texture_binding_scope_active())die("Owned image budget test during recording");
  private_image_budget=budget;private_image_cursor=images;
  *bytes=atomic_load(&private_image_bytes);
}
// Owned metadata boundary fixture only. This never runs in the game or changes
// a real queue's frontiers; it temporarily withholds a retired entry's logical
// admission, then restores the original watermark before actual reuse.
int vk_test_texture_retired_frontier(uint64_t serial,uint64_t next,uint64_t *previous) {
  int found=0;pthread_mutex_lock(&V.resources);
  for(retired_image *r=retired_images;r;r=r->next)if(r->serial==serial) {
    *previous=r->at;r->at=next;found=1;break;
  }
  pthread_mutex_unlock(&V.resources);return found;
}
#endif
#ifdef DM_VULKAN_TEST_EMBED
int vk_test_texture_rebuildable(uint64_t serial) {
  for(image *i=images;i;i=i->next)if(i->tex.serial==serial)return i->cpu_rebuildable;
  return -1;
}
#endif
void mtl_set_tex_pool_n(int n) {
  if (n < 0 || n > 1048576)
    die("Vulkan texture pool capacity %d", n);
  pthread_mutex_lock(&V.resources);
  if(texture_limit!=n) texture_registry_changed();
  texture_limit = n;
  pthread_mutex_unlock(&V.resources);
}
void mtl_reg_tex(int id, const tex_t *t, const view_t *v) {
  if (id < 0 || id >= 1048576)
    die("Vulkan texture descriptor id %d", id);
  pthread_mutex_lock(&V.resources);
  if (id >= texture_capacity) {
    int n = id + 256;
    texture_descriptor *p = realloc(texture_descriptors, n * sizeof *p);
    if (!p)
      die("Vulkan texture descriptor allocation");
    memset(p + texture_capacity, 0, (n - texture_capacity) * sizeof *p);
    texture_descriptors = p;
    texture_capacity = n;
  }
  texture_descriptor *d = &texture_descriptors[id];
  d->texture = *t;
  d->valid = 1;
  d->has_view = v != NULL;
  if (v)
    d->view = *v;
  texture_registry_changed();
  pthread_mutex_unlock(&V.resources);
}
void mtl_reg_samp(int id, const samp_par *params) {
  if (id < 0 || id >= 1048576)
    die("Vulkan sampler descriptor id %d", id);
  pthread_mutex_lock(&V.resources);
  if (id >= sampler_capacity) {
    int n = id + 256;
    sampler_descriptor *p = realloc(sampler_descriptors, n * sizeof *p);
    if (!p)
      die("Vulkan sampler descriptor allocation");
    memset(p + sampler_capacity, 0, (n - sampler_capacity) * sizeof *p);
    sampler_descriptors = p;
    sampler_capacity = n;
  }
  sampler_descriptor *d = &sampler_descriptors[id];
  // 相同参数继续使用原对象；替换时旧描述符可能仍在 GPU/翻译队列中。
  // Reuse identical parameters. Replaced descriptors can still be in the
  // GPU/translation queue, so retain their sampler for the snapshot grace
  // period.
  if (d->valid && !memcmp(&d->params, params, sizeof *params)) {
    pthread_mutex_unlock(&V.resources);
    return;
  }
  if (d->sampler) {
    retired_sampler *old = malloc(sizeof *old);
    if (!old)
      die("Vulkan sampler retirement allocation");
    *old = (retired_sampler){.next = retired_samplers, .sampler = d->sampler,
        .at = nvn_submissions_enqueued(), .last_use = d->last_use};
    retired_samplers = old;
  }
  d->params = *params;
  d->valid = 1;
  d->sampler = VK_NULL_HANDLE;
  d->last_use = 0;
  sampler_registry_changed();
  pthread_mutex_unlock(&V.resources);
}
static VkImageViewType view_type(int target) {
  switch (target) {
  case NVN_TEXTURE_TARGET_1D:
    return VK_IMAGE_VIEW_TYPE_1D;
  case NVN_TEXTURE_TARGET_1D_ARRAY:
    return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
  case NVN_TEXTURE_TARGET_2D_ARRAY:
  case NVN_TEXTURE_TARGET_2D_MULTISAMPLE_ARRAY:
    return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
  case NVN_TEXTURE_TARGET_3D:
    return VK_IMAGE_VIEW_TYPE_3D;
  case NVN_TEXTURE_TARGET_CUBEMAP:
    return VK_IMAGE_VIEW_TYPE_CUBE;
  case NVN_TEXTURE_TARGET_CUBEMAP_ARRAY:
    return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
  default:
    return VK_IMAGE_VIEW_TYPE_2D;
  }
}
int vk_texture_descriptor(int id,tex_t *out) {
  if(texture_lookup_current) {
    texture_descriptor local;
    const texture_descriptor *d=texture_descriptor_lookup(id,&local,NULL);
    if(d)*out=d->texture;
    return d!=NULL;
  }
  pthread_mutex_lock(&V.resources);
  int ok=id>=0 && id<texture_capacity && id<texture_limit && texture_descriptors[id].valid;
  if(ok)*out=texture_descriptors[id].texture;
  pthread_mutex_unlock(&V.resources);return ok;
}
void vk_texture_packaged_cpu_preflight(int id) {
  tex_t snapshot;int needed;
  if(texture_lookup_current) {
    texture_descriptor local;
    const texture_descriptor *d=texture_descriptor_lookup(id,&local,NULL);
    needed=d && d->texture.has_layout && d->texture.mem && d->texture.L.size;
    if(needed)snapshot=d->texture;
  } else {
    pthread_mutex_lock(&V.resources);
    needed=id>=0 && id<texture_capacity && id<texture_limit && texture_descriptors[id].valid;
    if(needed) {
      const tex_t *t=&texture_descriptors[id].texture;
      needed=t->has_layout && t->mem && t->L.size;
      if(needed)snapshot=*t;
    }
    pthread_mutex_unlock(&V.resources);
  }
  // Publication may wait/replay. Keep only the original value copy across it;
  // every access still performs CPU publication/dirty/ownership checks.
  if(needed)vk_texture_cpu_preflight(&snapshot);
}
int vk_texture_descriptor_footprint(int id,vk_texture_footprint *out) {
  if(texture_lookup_current) {
    texture_descriptor local;
    const texture_descriptor *d=texture_descriptor_lookup(id,&local,NULL);
    if(d) {const tex_t *t=&d->texture;*out=(vk_texture_footprint){t->serial,(uintptr_t)t->mem,t->L.size};}
    return d!=NULL;
  }
  pthread_mutex_lock(&V.resources);
  int ok=id>=0 && id<texture_capacity && id<texture_limit && texture_descriptors[id].valid;
  if(ok) {
    const tex_t *t=&texture_descriptors[id].texture;
    *out=(vk_texture_footprint){t->serial,(uintptr_t)t->mem,t->L.size};
  }
  pthread_mutex_unlock(&V.resources);return ok;
}
int vk_sampler_descriptor(int id,samp_par *out) {
  pthread_mutex_lock(&V.resources);
  int ok=id>=0 && id<sampler_capacity && sampler_descriptors[id].valid;
  if(ok)*out=sampler_descriptors[id].params;
  pthread_mutex_unlock(&V.resources);return ok;
}
int vk_texture_descriptor_view(int id,tex_t *out,view_t *view,int *has_view) {
  if(texture_lookup_current) {
    texture_descriptor local;
    const texture_descriptor *d=texture_descriptor_lookup(id,&local,NULL);
    if(d) {*out=d->texture;*view=d->view;*has_view=d->has_view;}
    return d!=NULL;
  }
  pthread_mutex_lock(&V.resources);
  int ok=id>=0 && id<texture_capacity && id<texture_limit && texture_descriptors[id].valid;
  if(ok) {
    const texture_descriptor *d=&texture_descriptors[id];*out=d->texture;
    *view=d->view;*has_view=d->has_view;
  }
  pthread_mutex_unlock(&V.resources);return ok;
}
static int texture_zero_components(const tex_t *t,const view_t *v,int sampled) {
  if(V.options.pool_defer_publication && t && t->mem && t->L.size &&
      !vk_pool_cpu_source_ready((uintptr_t)t->mem,t->L.size))return 0;
  vk_mip_clear_texture_access(t,v,!sampled);
  // RGBX views synthesize alpha=1 even when all stored bytes are zero.
  if(t && (t->d.format==NVN_FORMAT_RGBX8 || t->d.format==NVN_FORMAT_RGBX8_SRGB ||
      t->d.format==NVN_FORMAT_BGRX8 || t->d.format==NVN_FORMAT_BGRX8_SRGB)) return 0;
  if(!t || (v && ((v->set_format && v->format!=t->d.format) ||
      (v->set_layers && (v->min_layer || v->nlayers>1)) ||
      (v->set_target && v->target!=t->d.target)))) return 0;
  if(t->d.target!=NVN_TEXTURE_TARGET_2D || t->d.samples>1) return 0;
  for(image *i=image_buckets[t->serial%8192];i;i=i->hash_next)
    if(i->tex.serial==t->serial) {
      unsigned channels=zero_channels(i);if(!channels) return 0;
      unsigned first=v && v->set_levels?v->base_level:0;
      if(first>=i->levels) return 0;
      unsigned count=sampled?(v && v->set_levels && v->nlevels>0?
          v->nlevels:i->levels-first):1;
      if(count>i->levels-first) return 0;
      uint32_t mask=zero_mip_mask(first,count);
      if(!mask || (i->zero_mips&mask)!=mask) return 0;
      // A packaged texture may have acquired new CPU contents since its clear.
      // Consume that original upload dependency before trusting the GPU fact.
      image_upload(i);
      // Missing sampled alpha is not zero; the mask never expands RGB into
      // RGBA. All accessible sampled mips, not just the base, must be proven.
      return (i->zero_mips&mask)==mask?channels:0;
    }
  return 0;
}
int vk_texture_zero_target(const tex_t *t,const view_t *v) {
  return texture_zero_components(t,v,0);
}
int vk_texture_zero_descriptor(int id,int sid) {
  pthread_mutex_lock(&V.resources);
  int ok=id>0 && id<texture_capacity && id<texture_limit && texture_descriptors[id].valid;
  texture_descriptor d=ok?texture_descriptors[id]:(texture_descriptor){0};
  if(sid) {
    if(sid<0 || sid>=sampler_capacity || !sampler_descriptors[sid].valid) ok=0;
    else {
      samp_par p=sampler_descriptors[sid].params;
      // Comparison sampling and nonzero border colors do not preserve zero.
      if(p.cmp_mode || ((p.ws==4 || p.ws==5 || p.wt==4 || p.wt==5 ||
          p.wr==4 || p.wr==5) && p.border[3]>.5f)) ok=0;
    }
  }
  pthread_mutex_unlock(&V.resources);
  return ok?texture_zero_components(&d.texture,d.has_view?&d.view:NULL,1):0;
}
// Preflight every physical replacement before producing any stage's descriptors.
// Normal sampled views and already-capable storage images need no upload here.
void vk_texture_prepare_binding(int id,int storage,VkShaderStageFlagBits stage) {
  texture_binding_recipe *cached=NULL;
  if(!texture_binding_reference && texture_state && V.cmd) {
    texture_binding_recipe *r=&texture_state->bindings[texture_binding_slot(id,storage)];
    // This positive fact describes only the immutable logical descriptor. A
    // physical promotion or completed recording cannot make an ordinary
    // sample require preflight replacement. Do not dereference owner here.
    // Registry replacement/retirement/pool-limit changes all advance epoch.
    if(r->id==id && r->storage==!!storage && r->epoch==atomic_load(&descriptor_epoch)) {
      // Deferred mip clears may alias another logical texture through guest
      // memory. Their access drain must run even for an otherwise ordinary
      // descriptor; a logical no-op is not a proof about pending GPU work.
      if(r->preflight_trivial && !vk_mip_clear_pending()) { texture_prepare_hits++;return; }
      if(vk_recording_serial() && r->lifetime_generation==image_recipe_generation &&
          r->owner->handle==r->description.image) cached=r;
    }
  }
  texture_descriptor desc;view_t view;int has_view;image *known=NULL;
  const tex_t *t=&desc.texture;
  uint64_t known_generation=image_recipe_generation,known_submit=vk_recording_serial();
  if(cached) {
    known=cached->owner;view=cached->view;has_view=cached->has_view;texture_prepare_hits++;
    // Preparation can itself emit image accesses or re-enter pass-end replay.
    // Retain this owner for the current serial before any such callback.
    known->last_use=vk_recording_serial();
    // The only callback before the final physical operation is deferred-mip
    // access below. With no pending transaction it is an exact no-op (it does
    // not inspect V.nvn_barrier); alias classification is also pure. Borrow
    // immutable topology until i is selected. Retain the original value copy
    // whenever a drain can retire/promote this owner or re-enter a ghost draw.
    if(vk_mip_clear_pending()) {
      desc.texture=known->tex;
      vk_mip_clear_texture_access(t,has_view?&view:NULL,0);
    } else t=&known->tex;
  } else {
    uint64_t epoch;
    const texture_descriptor *logical=texture_descriptor_lookup(id,&desc,&epoch);
    if(!logical)die("Vulkan unbound texture descriptor %d",id);
    if(logical!=&desc)desc=*logical;
    texture_binding_snapshot_save(id,storage,&desc,epoch);
    view=desc.view;has_view=desc.has_view;
    vk_mip_clear_texture_access(t,has_view?&view:NULL,0);
  }
  const view_t *v=has_view?&view:NULL;
  // A deferred drain can finish this serial and begin another recording,
  // including its normal retirement pass. Never borrow the old owner through
  // that callback; use the same copied logical snapshot as the uncached path.
  if(known_generation!=image_recipe_generation || known_submit!=vk_recording_serial()) known=NULL;
  int mutable=v && v->set_format && v->format!=t->d.format;
  int alias=stage!=VK_SHADER_STAGE_COMPUTE_BIT && !storage && !layout_reference && t->d.levels>1 &&
      t->d.levels<=16 && t->d.target==NVN_TEXTURE_TARGET_2D &&
      t->L.layers==1 && t->d.samples<=1 && sampled_target_alias(t,v);
  if(!storage && !mutable && !alias) return;
  image *i=known?known:image_get(t);
  if((storage && !(i->description.usage & VK_IMAGE_USAGE_STORAGE_BIT)) ||
      (mutable && !(i->description.flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT))) {
    image_upload(i);
    image_binding_format(i,v,storage);
  }
  if(storage) {
    VkPipelineStageFlags stages=image_shader_stages(stage);
    if(!stages) die("Vulkan storage preparation stage");
    image_general_access(i,stages,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
  }
  else if(alias) {
    // Decide feedback fallback for every descriptor before any descriptor is
    // emitted. A later alias must not invalidate an earlier read-only layout.
    VkPipelineStageFlags stages=image_shader_stages(stage);
    if(stages) image_general_access(i,stages,VK_ACCESS_SHADER_READ_BIT);
    else image_general(i);
  }
}
static VkImageView texture_view_binding_impl(int id, int storage,VkShaderStageFlagBits stage,VkImageLayout *layout,int quad_plain2d) {
  uint64_t submit=vk_recording_serial(),epoch;
  texture_binding_recipe *cached=texture_binding_find(id,storage);
  image *i;view_t view;int has_view;VkImageView handle=VK_NULL_HANDLE;
  VkImageViewCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  VkImageSubresourceRange access_range;
  if(cached) {
    // Copy immutable metadata before any callback can perform nested draws
    // and replace a colliding cache slot. Layouts and dependencies are NEVER
    // cached: uploads, alias tests and every actual consuming stage follow.
    i=cached->owner;view=cached->view;has_view=cached->has_view;
    ci=cached->description;access_range=cached->access_range;
    handle=cached->handle;epoch=cached->epoch;
    texture_binding_hits++;
    i->last_use=vk_recording_serial();
    // With neither deferred mip work nor an NVN barrier this callback is an
    // exact no-op. Avoid copying a complete texture snapshot on every hit just
    // to protect a callback which cannot run. Actual image accesses below are
    // still unconditional; pending work keeps the full guarded path.
    if(vk_mip_clear_pending() || V.nvn_barrier) {
      tex_t snapshot=i->tex;
      uint64_t borrowed_generation=image_recipe_generation;
      vk_render_texture_read(&snapshot,stage);
      if(borrowed_generation!=image_recipe_generation || submit!=vk_recording_serial() ||
          epoch!=atomic_load(&descriptor_epoch)) {
        // Pass-end replay may submit/wait/recycle and retire a formerly borrowed
        // owner. Check generations before touching i, then resolve like a MISS.
        cached=NULL;handle=VK_NULL_HANDLE;
        ci=(VkImageViewCreateInfo){.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        i=image_get(&snapshot);
      }
    }
  } else {
    texture_binding_misses++;
    texture_descriptor desc;
    if(!texture_binding_snapshot_copy(id,storage,&desc,&epoch)) {
      const texture_descriptor *logical=texture_descriptor_lookup(id,&desc,&epoch);
      if(!logical)die("Vulkan unbound texture descriptor %d",id);
      if(logical!=&desc)desc=*logical;
    }
    view=desc.view;has_view=desc.has_view;
    vk_render_texture_read(&desc.texture,stage);
    i=image_get(&desc.texture);
  }
#ifdef DM_VULKAN_NATIVE_RESOURCES
  if(i->native_entry && !storage && stage==VK_SHADER_STAGE_FRAGMENT_BIT)
    native_image_use_consumer(i,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
  else
#endif
  image_upload(i);
  const view_t *v=has_view?&view:NULL;
  if (vk_trace_frame_active() && V.trace_directory) {
    char trace_label[32];
    snprintf(trace_label, sizeof trace_label, "sampled-%d", id);
    vk_texture_trace(&i->tex, v, trace_label);
  }
  if(!cached) {
    VkImageSubresourceRange r = range(i, v);
    r.levelCount = v && v->set_levels && v->nlevels ? v->nlevels
                                                  : i->levels - r.baseMipLevel;
    r.layerCount = v && v->set_layers && v->nlayers
                     ? v->nlayers
                     : i->layers - r.baseArrayLayer;
    VkImageViewType type =
      view_type(v && v->set_target ? v->target : i->tex.d.target);
    if (type == VK_IMAGE_VIEW_TYPE_1D || type == VK_IMAGE_VIEW_TYPE_2D ||
      type == VK_IMAGE_VIEW_TYPE_3D)
      r.layerCount = 1;
    if (storage) r.levelCount = 1;
    if (!storage && (r.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT)) r.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    ci.image=i->handle;ci.viewType=type;ci.format=image_binding_format(i,v,storage);
    // image_binding_format may promote physical storage.
    ci.image=i->handle;ci.subresourceRange=r;
    int effective_format=v && v->set_format?v->format:i->tex.d.format;
    if(effective_format==NVN_FORMAT_RGBX8 || effective_format==NVN_FORMAT_RGBX8_SRGB ||
        effective_format==NVN_FORMAT_BGRX8 || effective_format==NVN_FORMAT_BGRX8_SRGB)
      ci.components.a=VK_COMPONENT_SWIZZLE_ONE;
  }
  if(quad_plain2d && (storage || stage!=VK_SHADER_STAGE_FRAGMENT_BIT ||
      ci.viewType!=VK_IMAGE_VIEW_TYPE_2D ||
      (ci.subresourceRange.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT &&
       ci.subresourceRange.aspectMask!=VK_IMAGE_ASPECT_DEPTH_BIT) ||
      i->description.samples!=VK_SAMPLE_COUNT_1_BIT ||
      !i->extent.width || !i->extent.height || i->extent.width>INT32_MAX || i->extent.height>INT32_MAX))
    die("Vulkan readonly quad source requires valid single-sample color/depth 2D view");
  // Owner topology and the view are immutable under the recipe's descriptor,
  // lifetime and physical-handle guards. Normalize once on MISS, not twice
  // per binding. Uploads, current layouts, feedback and hazards still run.
  if(!cached)access_range=image_access_range(i,ci.subresourceRange);
  // Simultaneous attachment sampling and raw-handle callers retain GENERAL.
  // Shader image descriptors must agree across all stages and attachments.
  // Graphics stages share one prepared descriptor batch; vertex/unknown
  // consumers remain conservative. Compute sampling is a separate operation,
  // with its true consuming stage and no simultaneous framebuffer attachments.
  int compute_read=stage==VK_SHADER_STAGE_COMPUTE_BIT && !compute_layout_reference;
  // Compute has no framebuffer attachments. Stale graphics target bindings
  // are not feedback hazards for a dispatch. Its real storage aliases have
  // already pinned GENERAL during preparation of all image descriptors.
  if(!layout || storage || (stage!=VK_SHADER_STAGE_FRAGMENT_BIT && !compute_read) ||
      (!compute_read && !layout_reference && sampled_target_alias(&i->tex,v))) {
    VkPipelineStageFlags stages=image_shader_stages(stage);
    if(stages) image_general_access(i,stages,VK_ACCESS_SHADER_READ_BIT|
        (storage?VK_ACCESS_SHADER_WRITE_BIT:0));
    else image_general(i);
  }
  VkImageLayout binding_layout=image_use_range_normalized(i,access_range,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      image_shader_stages(stage));
#if defined(DM_VULKAN_TEST_EMBED) && defined(DM_VULKAN_NATIVE_RESOURCES)
  // The actual ASTC family fixture uses this real descriptor on the GPU,
  // including after CPU publication, transfer writers and chunk handoffs.
  if(i->native_entry && binding_layout!=VK_IMAGE_LAYOUT_GENERAL)
    die("Native image sampled descriptor must retain GENERAL");
#endif
  if(layout) *layout=binding_layout;
  image_request_add_normalized(i,access_range,image_shader_stages(stage),
      VK_ACCESS_SHADER_READ_BIT|(storage?VK_ACCESS_SHADER_WRITE_BIT:0));
  int decode8=image_astc_decode8(i,&ci,storage?VK_IMAGE_USAGE_STORAGE_BIT:VK_IMAGE_USAGE_SAMPLED_BIT);
  if(cached && cached->astc_decode8!=decode8) handle=VK_NULL_HANDLE;
  if(!handle || ci.image!=i->handle) {
    ci.image=i->handle;
    handle=cached_view(i,&ci,storage?VK_IMAGE_USAGE_STORAGE_BIT:VK_IMAGE_USAGE_SAMPLED_BIT);
  }
  if(!texture_binding_reference && texture_state && submit && submit==vk_recording_serial() &&
      epoch==atomic_load(&descriptor_epoch)) {
    texture_binding_recipe *destination=&texture_state->bindings[texture_binding_slot(id,storage)];
    // Registry identity plus the physical image/view identify this immutable
    // recipe. Recheck AFTER every upload/layout callback: a nested draw may
    // have replaced a colliding slot or promoted the image since the lookup.
    // Dynamic layouts, data uploads and dependencies above are never skipped.
    // Physical recipes already survive submits. A new serial alone changes no
    // immutable field; last_use was renewed before callbacks and the outer
    // guard still requires this exact recording to survive every callback.
    if(destination->epoch!=epoch ||
        destination->lifetime_generation!=image_recipe_generation ||
        destination->id!=id || destination->storage!=!!storage ||
        destination->owner!=i || destination->description.image!=i->handle ||
        destination->description.image!=ci.image || destination->handle!=handle ||
        destination->astc_decode8!=decode8)
      *destination=(texture_binding_recipe){
        .epoch=epoch,.lifetime_generation=image_recipe_generation,
        .id=id,.storage=!!storage,.has_view=has_view,
        // Exclude every possible graphics feedback alias, even for a compute
        // consumer. Current attachment state/layout mode is never predicted.
        .preflight_trivial=!storage &&
          !(v && v->set_format && v->format!=i->tex.d.format) &&
          !(i->tex.d.levels>1 && i->tex.d.levels<=16 &&
            i->tex.d.target==NVN_TEXTURE_TARGET_2D && i->tex.L.layers==1 && i->tex.d.samples<=1),
        .owner=i,.view=view,.description=ci,.access_range=access_range,.handle=handle,.astc_decode8=decode8};
  }
  return handle;
}
VkImageView vk_texture_view_binding(int id,int storage,VkShaderStageFlagBits stage,VkImageLayout *layout) {
  return texture_view_binding_impl(id,storage,stage,layout,0);
}
VkImageView vk_texture_view_binding_quad(int id,int storage,VkShaderStageFlagBits stage,VkImageLayout *layout) {
  return texture_view_binding_impl(id,storage,stage,layout,1);
}

VkImageView vk_texture_view(int id,int storage) { return vk_texture_view_binding(id,storage,0,NULL); }
// All physical attachment replacements must precede shader descriptors, which
// may sample or write another mip of the same image in this draw.
void vk_texture_prepare_attachment(const tex_t *t,const view_t *v) {
  vk_mip_clear_texture_access(t,v,1);
  image *i=image_get_attachment(t);
  image_view_format(i,v);
}
// 宿主图形增强读取已登记的 storage 体纹理；保留原描述符视图的基级与格式。
// Host graphics enhancements inspect registered storage volumes, preserving
// the descriptor view's base mip and format.
int vk_texture_storage_volume(int id, vk_storage_image *out) {
  pthread_mutex_lock(&V.resources);
  int valid = id >= 0 && id < texture_capacity && id < texture_limit &&
              texture_descriptors[id].valid;
  texture_descriptor d = valid ? texture_descriptors[id] : (texture_descriptor){0};
  pthread_mutex_unlock(&V.resources);
  if(valid)vk_mip_clear_texture_access(&d.texture,d.has_view?&d.view:NULL,0);
  if(valid && V.options.pool_defer_publication && d.texture.mem && d.texture.L.size &&
      !vk_pool_cpu_source_ready((uintptr_t)d.texture.mem,d.texture.L.size))return 0;
  if (!valid || !(d.texture.d.flags & NVN_TEXTURE_FLAGS_IMAGE) ||
      (d.has_view && d.view.set_target ? d.view.target : d.texture.d.target) != NVN_TEXTURE_TARGET_3D)
    return 0;
  image *i = image_get(&d.texture);
  if (i->description.imageType != VK_IMAGE_TYPE_3D || i->aspect != VK_IMAGE_ASPECT_COLOR_BIT)
    return 0;
  image_upload(i);
  const view_t *v = d.has_view ? &d.view : NULL;
  VkFormat format=image_view_format(i,v);
  VkFormatProperties props;
  vk_format_properties(format,&props);
  if(!(props.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) return 0;
  // The upstream LUT enhancement is another actual storage consumer. It must
  // use the same full-content promotion as guest shader descriptors.
  format=image_binding_format(i,v,1);
  VkImageSubresourceRange r = range(i, v);
  VkImageViewCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = i->handle, .viewType = VK_IMAGE_VIEW_TYPE_3D,
      .format = format, .subresourceRange = r};
  *out = (vk_storage_image){i->handle, cached_view(i, &ci, VK_IMAGE_USAGE_STORAGE_BIT),
      ci.format, {mip(i->extent.width, r.baseMipLevel), mip(i->extent.height, r.baseMipLevel),
                  mip(i->extent.depth, r.baseMipLevel)},
      {VK_IMAGE_ASPECT_COLOR_BIT, r.baseMipLevel, 0, 1},i->tex.serial};
  return 1;
}
void vk_texture_storage_access(const vk_storage_image *volume,VkPipelineStageFlags stages,VkAccessFlags access) {
  for(image *i=image_buckets[volume->serial%8192];i;i=i->hash_next)
    if(i->tex.serial==volume->serial && i->handle==volume->image) {
      image_general_access(i,stages,access);
      image_request request=image_request_make(i,image_layer_range(volume->subresource),stages,access);
      image_dependencies(&request,1,0,1);
      image_record_normalized(i,request.range,stages,access,1);
      return;
    }
  die("Vulkan storage image access lost its owner");
}
void vk_texture_storage_written(const vk_storage_image *volume,VkPipelineStageFlags producer) {
  for(image *i=image_buckets[volume->serial%8192];i;i=i->hash_next)
    if(i->tex.serial==volume->serial && i->handle==volume->image) {
      image_record(i,image_layer_range(volume->subresource),producer,VK_ACCESS_SHADER_WRITE_BIT,0);
      return;
    }
  die("Vulkan storage image producer lost its owner");
}
static VkSamplerAddressMode wrap(int mode) {
  switch (mode) {
  case 0:
  case 7:
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  case 1:
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
  case 4:
  case 5:
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  case 6:
    return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
  case 2:
  case 3:
    if (!V.sampler_mirror_clamp)
      die("Vulkan mirror-clamp sampler extension unavailable");
    return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
  default:
    die("Vulkan mirror-clamp sampler extension pending: %d", mode);
  }
}
VkSampler vk_sampler(int id) {
  if (id == 0) {
    // 空采样器与原后端一致；用于未绑定槽和只做 texelFetch 的合并描述符。
    // Match the original backend's null sampler for unbound slots and combined
    // texelFetch descriptors.
    if (!dummy_sampler) {
      VkSamplerCreateInfo ci = {
          .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
          .magFilter = VK_FILTER_NEAREST,
          .minFilter = VK_FILTER_NEAREST,
          .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
          .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
          .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
          .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
          .maxLod = VK_LOD_CLAMP_NONE};
      VK_CHECK(vkCreateSampler(V.device, &ci, NULL, &dummy_sampler));
    }
    return dummy_sampler;
  }
  uint64_t use=vk_recording_serial();
  unsigned slot=(unsigned)id&63;
  if(texture_state && use && !sampler_lookup_reference) {
    uint64_t epoch=atomic_load_explicit(&sampler_registry_epoch,memory_order_acquire);
    if(sampler_lookups[slot].id==id && sampler_lookups[slot].sampler &&
        sampler_lookups[slot].submit==use && sampler_lookups[slot].epoch==epoch) {
      sampler_lookup_hits++;return sampler_lookups[slot].sampler;
    }
  }
  if(texture_state)sampler_lookup_misses++;
  pthread_mutex_lock(&V.resources);
  if (id < 0 || id >= sampler_capacity || !sampler_descriptors[id].valid)
    die("Vulkan unbound sampler descriptor %d", id);
  sampler_descriptor *d = &sampler_descriptors[id];
  if (!d->sampler) {
    samp_par p = d->params;
    VkSamplerCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = p.magf ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
        .minFilter = p.minf == 1 || p.minf == 3 || p.minf == 5
                         ? VK_FILTER_LINEAR
                         : VK_FILTER_NEAREST,
        // NVN filters 0/1 do not use mipmaps. A linear mip mode is still
        // illegal for an unfilterable Vulkan format even with maxLod=0.
        .mipmapMode = p.minf < 4
                          ? VK_SAMPLER_MIPMAP_MODE_NEAREST
                          : VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = wrap(p.ws),
        .addressModeV = wrap(p.wt),
        .addressModeW = wrap(p.wr),
        .mipLodBias = p.lod_bias,
        .anisotropyEnable = p.aniso > 1,
        .maxAnisotropy = p.aniso > V.props.limits.maxSamplerAnisotropy
                             ? V.props.limits.maxSamplerAnisotropy
                             : p.aniso,
        .compareEnable = p.cmp_mode != 0,
        .compareOp = p.cmp_func ? p.cmp_func - 1 : VK_COMPARE_OP_LESS,
        .minLod = p.minf < 2 || p.lod_min < 0 ? 0 : p.lod_min,
        .maxLod = p.minf < 2 ? 0 : p.lod_max,
        .borderColor =
            p.border[3] > .5f
                ? (p.border[0] > .5f ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
                                     : VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK)
                : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK};
    if (ci.anisotropyEnable && !V.features.samplerAnisotropy)
      die("Vulkan anisotropy unavailable");
    VK_CHECK(vkCreateSampler(V.device, &ci, NULL, &d->sampler));
    atomic_fetch_add(&live_samplers, 1);
  }
  VkSampler sampler = d->sampler;
  if (use > d->last_use) d->last_use = use;
  if(texture_state && use && !sampler_lookup_reference) {
    sampler_lookups[slot].id=id;sampler_lookups[slot].sampler=sampler;
    sampler_lookups[slot].submit=use;
    sampler_lookups[slot].epoch=atomic_load_explicit(&sampler_registry_epoch,memory_order_relaxed);
  }
  pthread_mutex_unlock(&V.resources);
  return sampler;
}
static vk_attachment texture_attachment(const tex_t *t,const view_t *v,int readonly) {
  vk_mip_clear_texture_access(t,v,1);
  image *i = image_get_attachment(t);
  VkImageSubresourceRange r = attachment_range(i, v);
  VkFormat format=image_view_format(i,v);
  readonly=readonly && !readonly_depth_reference && i->aspect!=VK_IMAGE_ASPECT_COLOR_BIT;
  VkImageLayout layout=image_use_range(i,r,readonly?VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,readonly?
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT:0);
  if(readonly && layout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
    atomic_fetch_add(&readonly_depth_attachments,1);
  VkImageViewCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                              .image = i->handle,
                              .viewType = VK_IMAGE_VIEW_TYPE_2D,
                              .format = format,
                              .subresourceRange = r};
  return (vk_attachment){cached_view(i, &ci, i->aspect == VK_IMAGE_ASPECT_COLOR_BIT
                                               ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                                               : VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT),
                         ci.format,
                         {mip(i->extent.width, r.baseMipLevel),
                          mip(i->extent.height, r.baseMipLevel), 1},
                         i->tex.d.samples > 1
                             ? (VkSampleCountFlagBits)i->tex.d.samples
                             : VK_SAMPLE_COUNT_1_BIT,layout};
}
// Input attachment views are deliberately single-mip, unlike the original
// NVN sampled view. No ordinary read visibility proof authorizes INPUT_READ.
static image *pixel_local_owner(const vk_pixel_local_image *p) {
  if(!p || p->generation!=image_recipe_generation || p->recording!=vk_recording_serial())return NULL;
  for(image *i=image_buckets[p->serial%8192];i;i=i->hash_next)
    if(i->tex.serial==p->serial && i->handle==p->image)return i;
  return NULL;
}
int vk_texture_pixel_local_valid(const vk_pixel_local_image *p) {
  image *i=pixel_local_owner(p);
  return i && (!i->tex.has_layout || (i->stamp==p->stamp &&
      !nvn_dirty_since(i->tex.mem,i->tex.L.size,p->stamp)));
}
int vk_texture_pixel_local_prepare(const tex_t *t,int local,vk_pixel_local_image *out) {
  if(t && (t->d.format==NVN_FORMAT_RGBX8 || t->d.format==NVN_FORMAT_RGBX8_SRGB ||
      t->d.format==NVN_FORMAT_BGRX8 || t->d.format==NVN_FORMAT_BGRX8_SRGB))return 0;
  if(!t || t->d.target!=NVN_TEXTURE_TARGET_2D || t->L.layers!=1 || t->d.samples>1 ||
      !t->d.levels || t->d.levels>16 || aspect_for(t->d.format)!=VK_IMAGE_ASPECT_COLOR_BIT)return 0;
  if(V.options.pool_defer_publication && t->mem && t->L.size &&
      !vk_pool_cpu_source_ready((uintptr_t)t->mem,t->L.size))return 0;
  VkFormatProperties features;vk_format_properties(vk_nvn_format(t->d.format),&features);
  if(!(features.optimalTilingFeatures&VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT))return 0;
  vk_mip_clear_texture_access(t,NULL,0);
  VkImageUsageFlags usage=VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT|(local?VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT:0);
  image *i=image_get_usage(t,usage);
  if(!local)image_upload(i);
  // A single-mip/single-layer 2D image uses the existing whole-image
  // layout/layout_scope representation. Do not manufacture mip tracking or
  // admit complex untracked arrays/3D images through this path.
  int whole=i->levels==1 && i->layers==1 && i->description.imageType==VK_IMAGE_TYPE_2D &&
      i->extent.depth==1 && i->aspect==VK_IMAGE_ASPECT_COLOR_BIT;
  if((!image_mip_tracked(i) && !whole) || i->description.samples!=VK_SAMPLE_COUNT_1_BIT || i->decoded)return 0;
  VkImageSubresourceRange r={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  VkPipelineStageFlags stages=local?VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT:VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  VkAccessFlags access=local?VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT:VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
  VkImageLayout layout=local?VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  image_range_layout_access(i,r,layout,stages,access);
  if(!local) {
    // Invalidate SHADER_READ-only proofs before the dedicated input request.
    i->access_generation++;
    image_cell(i,0,0,0)->visible=0;
    image_request q=image_request_make(i,r,stages,access);image_dependencies(&q,1,0,1);
    image_record(i,r,stages,access,0);
  }
  VkImageViewCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=i->handle,
    .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=i->format,.subresourceRange=r};
  *out=(vk_pixel_local_image){.generation=image_recipe_generation,.serial=t->serial,
    .recording=vk_recording_serial(),.stamp=i->stamp,.image=i->handle,.usage=i->description.usage,.local=local,
    .attachment={cached_view(i,&ci,usage),
      i->format,i->extent,VK_SAMPLE_COUNT_1_BIT,layout}};
  i->last_use=out->recording;
  return 1;
}
int vk_texture_pixel_local_enter(const vk_pixel_local_image *p) {
  image *i=pixel_local_owner(p);if(!i)return 0;image_access *c=image_cell(i,0,0,0);
  i->last_use=vk_recording_serial();i->access_generation++;
  if(p->local) {
    if(image_mip_tracked(i)) {
      c->mip_layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      c->mip_scope=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
      i->layout=c->mip_layout;
      for(unsigned l=1;l<i->levels;l++)if(image_cell(i,0,l,0)->mip_layout!=i->layout) {i->layout=VK_IMAGE_LAYOUT_MAX_ENUM;break;}
    } else {
      // The implicit subpass transition covers the entire single-mip image.
      // Preserve writer clocks/readers in its original access history below.
      if(i->levels!=1 || i->layers!=1 || i->description.imageType!=VK_IMAGE_TYPE_2D ||
          i->aspect!=VK_IMAGE_ASPECT_COLOR_BIT)return 0;
      i->layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      i->layout_scope=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
  }
  // Retain all producer/write clocks and readers. The implicit dependency
  // covers input reads only, never subsequent ordinary shader reads.
  c->visible=0;c->layout_ordered=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  c->layout_ordered_access=VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
  image_record(i,(VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,0);
  return 1;
}
vk_attachment vk_texture_attachment(const tex_t *t,const view_t *v) { return texture_attachment(t,v,0); }
vk_attachment vk_texture_readonly_attachment(const tex_t *t,const view_t *v) { return texture_attachment(t,v,1); }
int vk_texture_pixel_local_depth_preflight(const tex_t *t,const view_t *v,
    const vk_attachment *attachment,int store) {
  if(V.options.pool_defer_publication && t && t->mem && t->L.size &&
      !vk_pool_cpu_source_ready((uintptr_t)t->mem,t->L.size))return 0;
  if(!t || !attachment || image_binding_active || vk_render_active() ||
      t->d.target!=NVN_TEXTURE_TARGET_2D || t->d.levels!=1 || t->L.layers!=1 ||
      t->d.samples>1 || attachment->layout!=VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL ||
      attachment->samples!=VK_SAMPLE_COUNT_1_BIT || (store!=0 && store!=1))return 0;
  // Ending a pass also drains deferred clears. Do this before borrowing an
  // owner; callbacks may promote/destroy images or replace command recording.
  uint64_t generation=image_recipe_generation,recording=vk_recording_serial(),passes=V.passes;
  vk_render_end();
  if(generation!=image_recipe_generation || recording!=vk_recording_serial() ||
      passes!=V.passes || image_binding_active || vk_render_active())return 0;
  image *i;for(i=image_buckets[t->serial%8192];i;i=i->hash_next)if(i->tex.serial==t->serial)break;
  if(!i || !t->serial || i->tex.mem!=t->mem || memcmp(&i->tex.d,&t->d,sizeof t->d) ||
      memcmp(&i->tex.L,&t->L,sizeof t->L) || i->tex.has_layout!=t->has_layout ||
      memcmp(i->tex.layout,t->layout,sizeof t->layout) || i->decoded ||
      i->levels!=1 || i->layers!=1 || i->description.imageType!=VK_IMAGE_TYPE_2D ||
      i->description.samples!=VK_SAMPLE_COUNT_1_BIT || i->extent.depth!=1 ||
      i->aspect==VK_IMAGE_ASPECT_COLOR_BIT || i->layout!=attachment->layout)return 0;
  VkImageSubresourceRange r=attachment_range(i,v);
  image_view *view;for(view=i->views;view;view=view->next)if(view->handle==attachment->view)break;
  if(!view || view->description.image!=i->handle || view->description.viewType!=VK_IMAGE_VIEW_TYPE_2D ||
      view->description.format!=attachment->format || memcmp(&view->description.subresourceRange,&r,sizeof r) ||
      !(view->usage&VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) ||
      attachment->extent.width!=i->extent.width || attachment->extent.height!=i->extent.height)return 0;
  VkPipelineStageFlags stages=VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  VkAccessFlags access=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|
      (store?VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT:0);
  image_request q=image_request_make(i,r,stages,access);
  uint64_t access_generation=i->access_generation;VkImage handle=i->handle;
  image_dependencies(&q,1,0,1);
  // Never dereference a borrowed owner after a callback without first proving
  // its lifetime and recording identity. Already emitted barriers stay valid
  // on fallback; no prospective draw/store was added to access history.
  if(generation!=image_recipe_generation || recording!=vk_recording_serial() || passes!=V.passes ||
      image_binding_active || vk_render_active())return 0;
  if(i->handle!=handle || i->layout!=attachment->layout || i->access_generation!=access_generation)return 0;
  i->access_generation++; // Invalidate old read-only visibility caches.
  for(unsigned a=0;a<3;a++)if(q.range.aspectMask&(1u<<a)) {
    image_access *c=image_cell(i,a,0,0);
    c->layout_ordered=stages;c->layout_ordered_access=access;
  }
  return 1;
}
// Publish only a LOAD that the physical render pass has actually executed.
// No preparation, callbacks or prospective shader batch: even an abandoned
// consumer with STORE_NONE still performed this fixed-function read.
int vk_texture_pixel_local_depth_loaded(const tex_t *t,const view_t *v,
    const vk_attachment *attachment) {
  if(!t || !t->serial || !attachment || vk_render_active())return 0;
  image *i;for(i=image_buckets[t->serial%8192];i;i=i->hash_next)if(i->tex.serial==t->serial)break;
  if(!i || i->tex.mem!=t->mem || memcmp(&i->tex.d,&t->d,sizeof t->d) ||
      memcmp(&i->tex.L,&t->L,sizeof t->L) || i->aspect==VK_IMAGE_ASPECT_COLOR_BIT ||
      i->layout!=attachment->layout)return 0;
  VkImageSubresourceRange r=attachment_range(i,v);
  image_view *view;for(view=i->views;view;view=view->next)if(view->handle==attachment->view)break;
  if(!view || view->description.image!=i->handle || view->description.format!=attachment->format ||
      memcmp(&view->description.subresourceRange,&r,sizeof r))return 0;
  // Preserve all old writers and visibility. A real read invalidates the
  // preflight-only ordering stamp and becomes a later writer's source scope.
  image_record(i,r,VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,0);
  return 1;
}
static void attachment_access(const tex_t *t,const view_t *v,VkImageAspectFlags aspect,int writable,int collect) {
  vk_mip_clear_texture_access(t,v,1);
  image *i=image_get(t);
  VkImageSubresourceRange r=attachment_range(i,v);
  if(aspect) r.aspectMask&=aspect;
  if(!r.aspectMask) return;
  int color=r.aspectMask==VK_IMAGE_ASPECT_COLOR_BIT;
  VkPipelineStageFlags stages=color?VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT:
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  VkAccessFlags access=color?VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT:
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  if(!writable) access&=~(VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
  if(collect && image_binding_active) {
    image_request_add(i,r,stages,access);
    image_bindings[image_binding_count-1].attachment=1;
  }
  else {
    image_record_raster=collect && vk_render_active()?V.passes:0;
    image_record_preserve_readers=!collect;
    image_record(i,r,stages,access,0);
    image_record_raster=0;image_record_preserve_readers=0;
  }
}
void vk_texture_attachment_record(const tex_t *t,const view_t *v,VkImageAspectFlags aspect) {
  attachment_access(t,v,aspect,1,1);
}
void vk_texture_attachment_read(const tex_t *t,const view_t *v) {
  attachment_access(t,v,0,0,1);
}
void vk_texture_attachment_store(const tex_t *t,const view_t *v,int preserve_zero) {
  image *i=image_get(t);VkImageSubresourceRange r=attachment_range(i,v);
  uint32_t zero=i->zero_mips&zero_mip_mask(r.baseMipLevel,r.levelCount);
  attachment_access(t,v,0,1,0);
  // STORE preserves the already proven tile contents. DONT_CARE must retire
  // the fact even if no fragment draw ran in this physical pass.
  if(preserve_zero) i->zero_mips|=zero;
}
void vk_texture_targets_barrier(const p_set_rt *rt,int include_depth) {
  // A clear begins a new physical pass, even if its old pass used these same
  // images with a smaller render area. Do not use the same-pass exemption.
  vk_render_end();
  vk_texture_shader_begin();
  for(int k=0;k<=rt->n;k++) {
    const tex_t *t=k==rt->n?(include_depth?rt->depth:NULL):rt->color[k];
    const view_t *v=k==rt->n?(rt->has_dview?&rt->dview:NULL):
        (rt->has_cview[k]?&rt->cview[k]:NULL);
    if(t) vk_texture_attachment_record(t,v,0);
  }
  vk_texture_graphics_barrier();
  // This was a dependency request, not the clear itself. Actual raster
  // accesses are recorded at their original command position by the caller.
  image_binding_count=0;image_binding_active--;
}

VkBufferView vk_texel_buffer(int id,int storage,int writable,VkPipelineStageFlags stages) {
  pthread_mutex_lock(&V.resources);
  if (id < 0 || id >= texture_capacity || id >= texture_limit ||
      !texture_descriptors[id].valid)
    die("Vulkan unbound texel buffer descriptor %d", id);
  texture_descriptor d = texture_descriptors[id];
  pthread_mutex_unlock(&V.resources);
  if (d.texture.d.target != NVN_TEXTURE_TARGET_BUFFER)
    die("Vulkan texel buffer target %d", d.texture.d.target);
  if(!writable) vk_buffer_shader_read((uintptr_t)d.texture.mem,d.texture.L.size,stages);
  vk_buffer *b;
  uint64_t off, size = d.texture.L.size;
  if (!vk_pool_address_stage((uintptr_t)d.texture.mem,size,(void **)&b,&off,stages))
    die("Vulkan texel buffer outside pool");
  if (off % V.props.limits.minTexelBufferOffsetAlignment)
    die("Vulkan texel buffer alignment");
  VkFormat format = vk_nvn_format(
      d.has_view && d.view.set_format ? d.view.format : d.texture.d.format);
  VkBufferView view=texel_view(b->handle,format,off,size);
  if (writable)
    vk_buffer_writeback_stage((uintptr_t)d.texture.mem,size,stages);
  return view;
}

VkImage vk_texture_native(const tex_t *t) {
  vk_mip_clear_drain();vk_texture_cpu_preflight(t);
  image *i = image_get(t);
#ifdef DM_VULKAN_NATIVE_RESOURCES
  if(i->native_entry)die("Native resource raw image escape has no tracked writer contract");
#endif
  image_upload(i);
  image_general(i);
  image_unknown(i);
  return i->handle;
}
VkImage vk_texture_present_source(const tex_t *t,VkImageLayout *layout) {
  vk_mip_clear_drain();vk_texture_cpu_preflight(t);
  image *i=image_get(t);
  image_upload(i);image_use(i,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  image_transfer(i,(VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},0);
  *layout=i->layout;
  return i->handle;
}
int vk_texture_present_sample(const void *texture,VkFormat destination,
    VkExtent2D output,vk_present_sample *sample) {
  vk_mip_clear_drain();
  const tex_t *t=texture;
  if(t->d.target!=NVN_TEXTURE_TARGET_2D)return 0;
  vk_texture_cpu_preflight(t);
  image *i=image_get(t);
  // Test actual physical metadata. In particular, don't infer RGBA from an
  // NVN format that the upload/decoder may have replaced with another format.
  if(i->layers!=1 || i->levels!=1 || i->aspect!=VK_IMAGE_ASPECT_COLOR_BIT ||
      i->description.imageType!=VK_IMAGE_TYPE_2D ||
      !vk_present_shader_eligible(i->format,i->description.samples,i->extent,
          i->description.usage,destination,output,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,1))return 0;
  image_upload(i);
  VkImageSubresourceRange r={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  VkImageLayout layout=image_use_range(i,r,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  image_request request=image_request_make(i,r,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
  image_dependencies(&request,1,0,1);
  image_record_normalized(i,request.range,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT,1);
  VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image=i->handle,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=i->format,.subresourceRange=r};
  *sample=(vk_present_sample){i->handle,cached_view(i,&vi,VK_IMAGE_USAGE_SAMPLED_BIT),layout,i->format,
      {i->extent.width,i->extent.height}};
  return 1;
}
int vk_rt_texture(const tex_t *texture,vk_rt_image *out) {
  vk_mip_clear_drain();
  if (!texture || texture->d.target!=NVN_TEXTURE_TARGET_2D || texture->d.samples>1) return 0;
  if(V.options.pool_defer_publication && texture->mem && texture->L.size &&
      !vk_pool_cpu_source_ready((uintptr_t)texture->mem,texture->L.size))return 0;
  image *i=image_get_attachment(texture); image_upload(i);
  image_general(i);
  image_unknown(i);
  if (i->layers!=1 || i->extent.depth!=1) return 0;
  VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image=i->handle,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=i->format,
      .subresourceRange={i->aspect,0,1,0,1}};
  *out=(vk_rt_image){.image=i->handle,.format=i->format,.extent=i->extent,.aspect=i->aspect};
  VkImageUsageFlags attachment=i->aspect==VK_IMAGE_ASPECT_COLOR_BIT?
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT:VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  out->attachment=cached_view(i,&vi,attachment);
  vi.subresourceRange.aspectMask=i->aspect&VK_IMAGE_ASPECT_DEPTH_BIT?VK_IMAGE_ASPECT_DEPTH_BIT:i->aspect;
  out->view=cached_view(i,&vi,VK_IMAGE_USAGE_SAMPLED_BIT);
  if (i->aspect&VK_IMAGE_ASPECT_STENCIL_BIT) {
    vi.subresourceRange.aspectMask=VK_IMAGE_ASPECT_STENCIL_BIT;
    out->stencil=cached_view(i,&vi,VK_IMAGE_USAGE_SAMPLED_BIT);
  }
  return 1;
}

// 显式开启的逐绘制读回，检查实际 GPU 输出；文件仅保存在用户的私有调试目录。
// Opt-in per-draw readback inspects actual GPU output; files stay in the user's
// private diagnostic directory.
typedef struct {
  vk_buffer *buffer;
  uint32_t width, height;
  char path[2048];
} trace_image;
static void trace_done(void *opaque) {
  trace_image *t = opaque;
  FILE *f = fopen(t->path, "wb");
  if (!f)
    die("Vulkan trace output open");
  fprintf(f, "P6\n%u %u\n255\n", t->width, t->height);
  for (size_t p = 0; p < (size_t)t->width * t->height * 4; p += 4)
    if (fwrite(t->buffer->map + p, 1, 3, f) != 3)
      die("Vulkan trace output write");
  if (fclose(f))
    die("Vulkan trace output close");
  free(t);
}
void vk_texture_trace(const tex_t *texture, const view_t *view,
                      const char *shader) {
  vk_mip_clear_drain();
  const char *root = V.trace_directory;
  if (!root || !vk_trace_frame_active())
    return;
  image *i = image_get(texture);
  if (i->format != VK_FORMAT_R8G8B8A8_UNORM &&
      i->format != VK_FORMAT_R8G8B8A8_SRGB)
    return;
  if (texture->d.samples > 1)
    return;
  VkImageSubresourceRange r = range(i, view);
  if (!strncmp(shader, "sampled-", 8)) {
    if (i->trace_stamp != V.frame + 1) {
      i->trace_stamp = V.frame + 1;
      i->trace_levels = 0;
    }
    if (r.baseMipLevel >= 64 || (i->trace_levels & (1ull << r.baseMipLevel)))
      return;
    i->trace_levels |= 1ull << r.baseMipLevel;
  }
  trace_image *trace = calloc(1, sizeof *trace);
  if (!trace)
    die("Vulkan trace allocation");
  trace->width = mip(i->extent.width, r.baseMipLevel);
  trace->height = mip(i->extent.height, r.baseMipLevel);
  if (!strncmp(shader, "sampled-", 8)) {
    if (trace->width > 256)
      trace->width = 256;
    if (trace->height > 256)
      trace->height = 256;
  }
  trace->buffer = vk_temp_buffer((uint64_t)trace->width * trace->height * 4);
  snprintf(trace->path, sizeof trace->path, "%s/f%llu-d%llu-%s-t%llu.ppm", root,
           (unsigned long long)V.frame, (unsigned long long)V.draws, shader,
           (unsigned long long)texture->serial);
  VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,
                                                 r.baseMipLevel,
                                                 r.baseArrayLayer, 1},
                            .imageExtent = {trace->width, trace->height, 1}};
  vk_barrier();
  image_general(i);
  vkCmdCopyImageToBuffer(V.cmd, i->handle, VK_IMAGE_LAYOUT_GENERAL,
                         trace->buffer->handle, 1, &copy);
  image_record(i,image_layer_range(copy.imageSubresource),VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT,0);
  vk_buffer_transfer_written();
  vk_completion(trace_done, trace);
}
