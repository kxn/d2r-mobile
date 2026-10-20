// 复用原项目控制段哈希、阶段绑定与 c0/c1/c2 约定；GPU
// 指令离线翻译，运行时只创建 Vulkan 对象。 Reuse original control hashes, stage
// bindings and c0/c1/c2 conventions; GPU instructions translate offline,
// runtime only creates Vulkan objects.
#include "nvn_vk_shader.h"
#include "nvn_vk_pixel_local_runtime.h"
#include "nvn_vk_system_trace.h"
#include "../../host/crypto.h"
#include <stdatomic.h>
#include <sys/stat.h>
#include <errno.h>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif
static vk_shader *shaders;
// Only the Vulkan recording owner updates these diagnostics. Readers use the
// atomic totals published at shader_end, never the mutable recording values.
static struct {
  uint64_t storage_direct,storage_copy_ops,storage_copy_bytes;
  uint64_t driver_last_hits,driver_content_hits,driver_misses;
  uint64_t descriptor_content_hits,descriptor_content_misses,driver_arena_slots;
  uint64_t driver_arena_zeroes,driver_arena_chunks,driver_arena_reuses;
  uint64_t descriptor_history_hits,descriptor_history_misses,descriptor_reuses;
  uint64_t descriptor_allocate_calls,descriptor_allocate_sets;
} shader_recorded;
static _Atomic int storage_bias_requested=-1,storage_bias_active=1;
static _Atomic uint64_t storage_direct,storage_copy_ops,storage_copy_bytes;
static _Atomic int driver_reference_requested=-1,driver_reference_active=1;
static _Atomic uint64_t driver_last_hits,driver_content_hits,driver_misses;
static _Atomic uint64_t descriptor_content_hits,descriptor_content_misses;
static _Atomic uint64_t driver_arena_slots,driver_arena_zeroes,driver_arena_chunks;
static _Atomic uint64_t driver_arena_live,driver_arena_reuses;
void vk_driver_arena_stats(uint64_t *slots,uint64_t *zeroes,uint64_t *chunks,
                          uint64_t *live,uint64_t *reuses) {
  *slots=atomic_load(&driver_arena_slots);*zeroes=atomic_load(&driver_arena_zeroes);
  *chunks=atomic_load(&driver_arena_chunks);
  *live=atomic_load(&driver_arena_live);*reuses=atomic_load(&driver_arena_reuses);
}
static int driver_arena_reference(void) {
  static int reference=-1;
  if(reference<0) {
    const char *v=getenv("DM_VULKAN_DRIVER_ARENA_REFERENCE");
    if(v && strcmp(v,"0") && strcmp(v,"1"))
      die("DM_VULKAN_DRIVER_ARENA_REFERENCE expects 0 or 1");
    reference=v && !strcmp(v,"1");
  }
  return reference;
}
void vk_descriptor_content_stats(uint64_t *hits,uint64_t *misses) {
  *hits=atomic_load(&descriptor_content_hits);*misses=atomic_load(&descriptor_content_misses);
}
static int descriptor_content_reference(void) {
  static int reference=-1;
  if(reference<0) {
    const char *v=getenv("DM_VULKAN_DESCRIPTOR_CONTENT_REFERENCE");
    if(v && strcmp(v,"0") && strcmp(v,"1"))
      die("DM_VULKAN_DESCRIPTOR_CONTENT_REFERENCE expects 0 or 1");
    reference=v && !strcmp(v,"1");
  }
  return reference;
}
void vk_driver_constant_reference(int reference) {
  atomic_store(&driver_reference_requested,!!reference);
}
void vk_driver_constant_stats(uint64_t *last,uint64_t *content,uint64_t *misses,int *reference) {
  *last=atomic_load(&driver_last_hits);*content=atomic_load(&driver_content_hits);
  *misses=atomic_load(&driver_misses);*reference=atomic_load(&driver_reference_active);
}
void vk_storage_bias_reference(int reference) { atomic_store(&storage_bias_requested,!!reference); }
void vk_storage_bias_stats(uint64_t *direct,uint64_t *copies,uint64_t *bytes,int *reference) {
  *direct=atomic_load(&storage_direct);*copies=atomic_load(&storage_copy_ops);
  *bytes=atomic_load(&storage_copy_bytes);*reference=atomic_load(&storage_bias_active);
}
void vk_program_shaders(const prog_t *program, vk_shader *output[6]) {
  // 原 ProgramSetShaders
  // 每次分配新序号；快照序号稳定时不再逐绘制重算控制段哈希。 Original
  // ProgramSetShaders assigns a new serial. A stable snapshot serial avoids
  // hashing every control segment again for every draw.
  static struct {
    uint64_t serial;
    vk_shader *stages[6];
  } cache[1024];
  if (!program->serial)
    die("Vulkan program has no shaders");
  unsigned bucket = program->serial % 1024;
  if (cache[bucket].serial != program->serial) {
    memset(cache[bucket].stages, 0, sizeof cache[bucket].stages);
    for (unsigned i = 0; i < program->nstage; i++) {
      vk_shader *shader = vk_shader_get(program->sd[i].control);
      cache[bucket].stages[shader->stage] = shader;
    }
    cache[bucket].serial = program->serial;
  }
  memcpy(output, cache[bucket].stages, sizeof cache[bucket].stages);
}
typedef struct descriptor_block {
  struct descriptor_block *next;
  VkDescriptorPool pool;
} descriptor_block;
static VkDescriptorSetLayout empty_layout;
static vk_buffer support_constant;
static vk_buffer zero_constant;
typedef struct constant_copy {
  struct constant_copy *next;
  uint64_t address, size, epoch;
  vk_buffer *buffer;
} constant_copy;
typedef struct driver_constant {
  struct driver_constant *next;
  uint64_t hash;
  int slot, stage;
  vk_buffer *buffer;
  uint64_t offset;
  const vk_driver_schema *schema; // NULL is the existing full-key identity
  // CPU-owned immutable comparison key. Never read GPU-mapped memory just to
  // deduplicate host-generated constants; it may be uncached on mobile GPUs.
  uint8_t data[];
} driver_constant;
// Uniform-only storage: neither general staging nor GPU writes may touch it.
// A slot's zero tail is initialized once and remains zero across true fence
// completion/reuse. Every draw keeps its own immutable 64 KiB slot.
typedef struct driver_arena {
  struct driver_arena *next;
  vk_buffer buffer;
  uint64_t stride;
  unsigned used,initialized;
} driver_arena;
// Submission-thread only, like the general completed temporary buffer pool.
// Share completed chunks between contexts instead of retaining every context's
// startup high-water mark. Bound idle storage to 32 chunks (normally 16 MiB).
static driver_arena *available_driver_arenas;
static unsigned available_driver_arena_count;
typedef struct {
  uint64_t address, size, offset;
  vk_buffer *original, *aligned;
  int writable;
} storage_copy;
typedef struct {
  uint64_t object,offset,range;
} descriptor_value;
typedef struct {
  const vk_shader *shader;
  VkDescriptorSetLayout layout;
  VkDescriptorSet set;
  descriptor_value values[128];
} descriptor_content;
// Same-context immutable content cache. Full keys are compared even when
// hashes collide; payloads and resource dependencies are still resolved first.
enum { DESCRIPTOR_HISTORY_SLOTS=256 };
// Only a location hint into this submission's existing history. Never retain
// an entry pointer: a colliding slot may realloc/replace it before the next
// draw. Actual descriptors/dependencies are always resolved before looking up
// the current slot and comparing every immutable value below.
enum { DESCRIPTOR_SHADER_HINT_SLOTS=16 };
typedef struct {
  const vk_shader *shader;
  VkDescriptorSetLayout layout;
  unsigned history_slot;
} descriptor_shader_hint;
_Static_assert(sizeof(descriptor_shader_hint)==24,"Descriptor location hint cost");
typedef struct {
  const vk_shader *shader;
  VkDescriptorSetLayout layout;
  VkDescriptorSet set;
  uint64_t hash,epoch;
  uint32_t capacity;
  descriptor_value values[];
} descriptor_history_entry;
static atomic_int descriptor_history_reference;
static atomic_uint_fast64_t descriptor_history_hits,descriptor_history_misses,descriptor_history_bytes;
static int descriptor_history_test_collision;
void vk_descriptor_history_reference(int reference) { atomic_store(&descriptor_history_reference,!!reference); }
void vk_descriptor_history_test_hash(int force) { descriptor_history_test_collision=!!force; }
void vk_descriptor_history_stats(uint64_t *hits,uint64_t *misses,uint64_t *bytes,int *reference) {
  *hits=atomic_load(&descriptor_history_hits);*misses=atomic_load(&descriptor_history_misses);
  *bytes=atomic_load(&descriptor_history_bytes);*reference=atomic_load(&descriptor_history_reference);
}
static uint64_t descriptor_history_hash(const vk_shader *shader,VkDescriptorSetLayout layout,
                                      const descriptor_value *values) {
  uint64_t h=(uintptr_t)shader^((uint64_t)layout<<1);
  for(unsigned n=0;n<shader->nb;n++) {
    h^=values[n].object^(values[n].offset*UINT64_C(0x9e3779b185ebca87))^
        (values[n].range*UINT64_C(0xc2b2ae3d27d4eb4f));
    h=(h<<13)|(h>>51);h*=UINT64_C(0x165667b19e3779f9);
  }
  return descriptor_history_test_collision?0:h^(h>>32);
}
typedef struct descriptor_batch {
  struct descriptor_batch *next;
  VkDescriptorSetLayout layout;
  VkDescriptorSet sets[16];
  uint32_t count,issued;
  VkDescriptorSet *retained;
  uint32_t retained_count,retained_capacity,retained_used;
} descriptor_batch;
static atomic_int descriptor_batch_reference;
static atomic_int descriptor_reuse_reference;
static atomic_uint_fast64_t descriptor_reuses,descriptor_pool_resets;
void vk_descriptor_reuse_reference(int reference) { atomic_store(&descriptor_reuse_reference,!!reference); }
void vk_descriptor_reuse_stats(uint64_t *hits,uint64_t *resets,int *reference) {
  *hits=atomic_load(&descriptor_reuses);*resets=atomic_load(&descriptor_pool_resets);
  *reference=atomic_load(&descriptor_reuse_reference);
}
static atomic_uint_fast64_t descriptor_allocate_calls,descriptor_allocate_sets;
void vk_descriptor_batch_reference(int reference) { atomic_store(&descriptor_batch_reference,!!reference); }
void vk_descriptor_batch_stats(uint64_t *calls,uint64_t *sets,int *reference) {
  *calls=atomic_load(&descriptor_allocate_calls);*sets=atomic_load(&descriptor_allocate_sets);
  *reference=atomic_load(&descriptor_batch_reference);
}
static void shader_publish_diagnostics(void) {
  atomic_store_explicit(&storage_direct,shader_recorded.storage_direct,memory_order_relaxed);
  atomic_store_explicit(&storage_copy_ops,shader_recorded.storage_copy_ops,memory_order_relaxed);
  atomic_store_explicit(&storage_copy_bytes,shader_recorded.storage_copy_bytes,memory_order_relaxed);
  atomic_store_explicit(&driver_last_hits,shader_recorded.driver_last_hits,memory_order_relaxed);
  atomic_store_explicit(&driver_content_hits,shader_recorded.driver_content_hits,memory_order_relaxed);
  atomic_store_explicit(&driver_misses,shader_recorded.driver_misses,memory_order_relaxed);
  atomic_store_explicit(&descriptor_content_hits,shader_recorded.descriptor_content_hits,memory_order_relaxed);
  atomic_store_explicit(&descriptor_content_misses,shader_recorded.descriptor_content_misses,memory_order_relaxed);
  atomic_store_explicit(&driver_arena_slots,shader_recorded.driver_arena_slots,memory_order_relaxed);
  atomic_store_explicit(&driver_arena_zeroes,shader_recorded.driver_arena_zeroes,memory_order_relaxed);
  atomic_store_explicit(&driver_arena_chunks,shader_recorded.driver_arena_chunks,memory_order_relaxed);
  atomic_store_explicit(&driver_arena_reuses,shader_recorded.driver_arena_reuses,memory_order_relaxed);
  atomic_store_explicit(&descriptor_history_hits,shader_recorded.descriptor_history_hits,memory_order_relaxed);
  atomic_store_explicit(&descriptor_history_misses,shader_recorded.descriptor_history_misses,memory_order_relaxed);
  atomic_store_explicit(&descriptor_reuses,shader_recorded.descriptor_reuses,memory_order_relaxed);
  atomic_store_explicit(&descriptor_allocate_calls,shader_recorded.descriptor_allocate_calls,memory_order_relaxed);
  atomic_store_explicit(&descriptor_allocate_sets,shader_recorded.descriptor_allocate_sets,memory_order_relaxed);
}
struct vk_shader_submission {
  descriptor_block *descriptor_blocks, *descriptor_current;
  constant_copy *constant_copies[1024], *readonly_storage_copies[1024];
  driver_constant *driver_constants[1024];
  driver_constant *driver_last[6][2];
  driver_arena *driver_arenas,*driver_arena_current;
  descriptor_content descriptor_last[6];
  descriptor_history_entry *descriptor_history[DESCRIPTOR_HISTORY_SLOTS];
  uint64_t descriptor_history_epoch;
  descriptor_shader_hint descriptor_shader_hints[DESCRIPTOR_SHADER_HINT_SLOTS];
  descriptor_batch *descriptor_batches[128];
  int storage_bias_reference;
  int driver_reference;
  uint32_t descriptor_retained;
  int descriptor_reuse_reference;
};
static void descriptor_history_release(vk_shader_submission *state) {
  for(unsigned i=0;i<DESCRIPTOR_HISTORY_SLOTS;i++)if(state->descriptor_history[i]) {
    descriptor_history_entry *entry=state->descriptor_history[i];
    atomic_fetch_sub(&descriptor_history_bytes,sizeof *entry+(size_t)entry->capacity*sizeof *entry->values);
    free(entry);state->descriptor_history[i]=NULL;
  }
}
static vk_shader_submission *binding_state;
#ifdef DM_VULKAN_TEST_EMBED
// Owned path-coverage assertion only; absent from production builds.
static unsigned descriptor_shader_hint_test_hits;
unsigned vk_test_descriptor_shader_hint_hits(void) { return descriptor_shader_hint_test_hits; }
#endif
static descriptor_shader_hint *descriptor_shader_hint_slot(vk_shader *s,VkDescriptorSetLayout layout) {
  uintptr_t key=((uintptr_t)s>>4)^((uintptr_t)layout>>4);
  key^=key>>9;
  return &binding_state->descriptor_shader_hints[key&(DESCRIPTOR_SHADER_HINT_SLOTS-1)];
}
static descriptor_history_entry *descriptor_shader_hint_find(vk_shader *s,VkDescriptorSetLayout layout,
    const descriptor_value *values,size_t bytes) {
  const descriptor_shader_hint *hint=descriptor_shader_hint_slot(s,layout);
  if(hint->shader!=s || hint->layout!=layout)return NULL;
  descriptor_history_entry *entry=binding_state->descriptor_history[hint->history_slot];
  if(!entry || entry->epoch!=binding_state->descriptor_history_epoch ||
      entry->shader!=s || entry->layout!=layout || memcmp(entry->values,values,bytes))return NULL;
  return entry;
}
static void descriptor_shader_hint_remember(vk_shader *s,VkDescriptorSetLayout layout,
    descriptor_history_entry **history) {
  *descriptor_shader_hint_slot(s,layout)=(descriptor_shader_hint){
    s,layout,(unsigned)(history-binding_state->descriptor_history)};
}
typedef struct {
  storage_copy storage_copies[128];
  unsigned storage_copy_count;
  int shader_buffer_writes,precise_compute_outputs;
  uint32_t storage_bias_words[6][32];
} shader_operation;
static shader_operation shader_operations[8];
static unsigned shader_operation_depth;
int vk_shader_operation_active(void) { return shader_operation_depth!=0; }
#define current_operation (shader_operations[shader_operation_depth-1])
void vk_shader_operation_begin(void) {
  if(shader_operation_depth==sizeof shader_operations/sizeof *shader_operations)
    die("Vulkan shader operation nesting");
  shader_operation_depth++;
  current_operation.storage_copy_count=0;
  current_operation.shader_buffer_writes=0;
  current_operation.precise_compute_outputs=0;
  // Each active shader clears its own bias words in vk_shader_bindings;
  // unused copy slots must not cost a multi-kilobyte memset on every draw.
}

void vk_shader_operation_abandon(void) {
  if(!shader_operation_depth) die("Vulkan shader operation underflow");
  // Actual alignment uploads/copies retain their independently recorded
  // effects. An unexecuted shader must not copy back prospective writable
  // storage or publish a shader-buffer producer/epoch during a draw retry.
  current_operation.storage_copy_count=0;
  current_operation.shader_buffer_writes=0;
  current_operation.precise_compute_outputs=0;
  shader_operation_depth--;
}

static VkPipelineStageFlags producer_stage(unsigned stage) {
  static const VkPipelineStageFlags stages[]={VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT,
    VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT,VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT};
  if(stage>=sizeof stages/sizeof *stages) die("Vulkan shader producer stage");
  return stages[stage];
}
#include "nvn_vk_shader_pool_ranges.inc"
void vk_shader_writebacks(void) {
  if(!shader_operation_depth) die("Vulkan shader operation underflow");
  for (unsigned i = 0; i < current_operation.storage_copy_count; i++)
    if (current_operation.storage_copies[i].writable) {
      VkBufferCopy c = {.dstOffset = current_operation.storage_copies[i].offset,
                        .size = current_operation.storage_copies[i].size};
      vk_buffer_copyback_dependencies(current_operation.storage_copies[i].address,
          current_operation.storage_copies[i].size);
      vkCmdCopyBuffer(V.cmd, current_operation.storage_copies[i].aligned->handle,
                      current_operation.storage_copies[i].original->handle, 1, &c);
      vk_buffer_transfer_written_range(current_operation.storage_copies[i].address,
          current_operation.storage_copies[i].size);
      shader_recorded.storage_copy_ops += 1;
      shader_recorded.storage_copy_bytes += c.size;
      vk_buffer_writeback_stage(current_operation.storage_copies[i].address,
          current_operation.storage_copies[i].size,VK_PIPELINE_STAGE_TRANSFER_BIT);
    }
  current_operation.storage_copy_count = 0;
  if (current_operation.shader_buffer_writes) {
    if(current_operation.shader_buffer_writes==VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT) {
      if(!current_operation.precise_compute_outputs) vk_compute_produced();
    }
    else vk_barrier();
    V.buffer_epoch++;
  }
  current_operation.shader_buffer_writes = 0;
  shader_operation_depth--;
}
static void *read_cache(const char *key, const char *suffix, size_t *size) {
  const char *root = V.options.shader_root;
  if (!root)
    die("DM_VULKAN_SHADERS must point to the offline Vulkan cache");
  char path[2048];
  if (snprintf(path, sizeof path, "%s/%s.%s", root, key, suffix) >=
      (int)sizeof path)
    die("Vulkan shader path too long");
  FILE *f = fopen(path, "rb");
  if (!f)
    die("Vulkan shader cache missing: %s", path);
  struct stat st;
  if (fstat(fileno(f), &st) || st.st_size <= 0 || st.st_size > (32 << 20))
    die("Vulkan shader cache size: %s", path);
  void *data = malloc(st.st_size);
  if (!data || fread(data, 1, st.st_size, f) != (size_t)st.st_size)
    die("Vulkan shader cache read: %s", path);
  fclose(f);
  *size = st.st_size;
  return data;
}
#include "nvn_vk_uniform_specialization.inc"
VkShaderStageFlagBits vk_stage(uint32_t stage) {
  const VkShaderStageFlagBits flags[] = {
      VK_SHADER_STAGE_VERTEX_BIT,
      VK_SHADER_STAGE_FRAGMENT_BIT,
      VK_SHADER_STAGE_GEOMETRY_BIT,
      VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
      VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
      VK_SHADER_STAGE_COMPUTE_BIT};
  if (stage >= 6)
    die("Vulkan shader stage %u", stage);
  return flags[stage];
}
static VkDescriptorType descriptor_type(const vk_binding *b) {
  switch (b->kind) {
  case 0:
  case 1:
    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  case 2:
  case 8:
  case VK_BINDING_TEXEL_STORAGE:
    return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  case 3:
    return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  case 4:
    return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  case 5:
    return VK_DESCRIPTOR_TYPE_SAMPLER;
  case 6:
    return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  case VK_BINDING_PIXEL_LOCAL:
    return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
  case 7:
    return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
  case 9:
    return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
  default:
    die("Vulkan binding kind %d", b->kind);
  }
}
typedef union {
  VkDescriptorBufferInfo buffer;
  VkDescriptorImageInfo image;
  VkBufferView texel;
} descriptor_payload;
static atomic_int descriptor_template_reference;
void vk_descriptor_template_reference(int reference) { atomic_store(&descriptor_template_reference,!!reference); }
int vk_descriptor_template_reference_mode(void) { return atomic_load(&descriptor_template_reference); }
static VkDescriptorUpdateTemplate descriptor_template_create(const vk_shader *s,int dynamic) {
  if(!s->nb || !vkCreateDescriptorUpdateTemplate || !vkUpdateDescriptorSetWithTemplate)return VK_NULL_HANDLE;
  VkDescriptorUpdateTemplateEntry entries[128];
  for(uint32_t n=0;n<s->nb;n++) {
    if(s->bindings[n].count!=1)return VK_NULL_HANDLE; // same existing array fallback
    entries[n]=(VkDescriptorUpdateTemplateEntry){.dstBinding=s->bindings[n].binding,.descriptorCount=1,
      .descriptorType=dynamic && s->dynamic_uniform_ordinals[dynamic-1][n]?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:descriptor_type(&s->bindings[n]),
      .offset=n*sizeof(descriptor_payload),.stride=sizeof(descriptor_payload)};
  }
  VkDescriptorUpdateTemplateCreateInfo ci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO,
    .descriptorUpdateEntryCount=s->nb,.pDescriptorUpdateEntries=entries,
    .templateType=VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET,
    .descriptorSetLayout=dynamic?s->dynamic_descriptors[dynamic-1]:s->descriptors};
  VkDescriptorUpdateTemplate result;VK_CHECK(vkCreateDescriptorUpdateTemplate(V.device,&ci,NULL,&result));return result;
}
#include "nvn_vk_layout_cache.inc"
// Classify only immutable schema eligibility. Dynamic addresses, sizes, texel
// descriptors, alignment and access/ownership checks remain operation-local.
// Invalid/unknown schemas retain the original scan and its exact failure path.
static void shader_pool_binding_mask_build(vk_shader *s) {
  s->pool_binding_mask[0]=s->pool_binding_mask[1]=0;
  s->pool_binding_mask_ready=0;
  if(s->nb>128 || (s->nb&&!s->bindings))return;
  for(unsigned k=0;k<s->nb;k++) {
    const vk_binding *b=&s->bindings[k];int eligible=0;
    if(b->count!=1)return;
    if(b->kind==1) {
      if(b->reg<0 || b->reg>=19)return;
      eligible=b->reg>=3;
    } else if(b->kind==2 || b->kind==8) {
      if(b->reg<0 || b->reg>=16)return;
      eligible=1;
    } else if(b->kind==7 || b->kind==9) {
      if(b->reg<0 || b->reg>=(b->kind==9?32:128))return;
      eligible=1;
    } else if(b->kind!=0 && (b->kind<3 || b->kind>6))return;
    if(eligible)s->pool_binding_mask[k/64]|=UINT64_C(1)<<(k%64);
  }
  s->pool_binding_mask_ready=1;
}
#ifdef DM_VULKAN_TEST_EMBED
// Cold functional boundary coverage only; no observer or counter on the draw.
void vk_test_shader_pool_binding_mask_build(vk_shader *s) {shader_pool_binding_mask_build(s);}
#endif
// image_read_slots is a store-lookahead projection of logical slots, not
// binding indices: it also includes texel-buffer kinds7/9 and the optional
// texel-storage alternative. Reinterpreting it would change that contract.
static void shader_image_binding_mask_build(vk_shader *s) {
  s->image_binding_mask[0]=s->image_binding_mask[1]=0;
  s->image_binding_mask_ready=0;
  if(s->nb>128 || (s->nb&&!s->bindings))return;
  unsigned selected=0;
  for(unsigned k=0;k<s->nb;k++) {
    const vk_binding *b=&s->bindings[k];
    if(b->count!=1)return;
    if(b->kind!=3 && b->kind!=4 && b->kind!=6)continue;
    if(b->reg<0 || b->reg>=(b->kind==4?32:128))return;
    s->image_binding_mask[k/64]|=UINT64_C(1)<<(k%64);selected++;
  }
  // All-image schemas skip no entries, so retain their contiguous scan.
  s->image_binding_mask_ready=selected<s->nb;
}
#ifdef DM_VULKAN_TEST_EMBED
void vk_test_shader_image_binding_mask_build(vk_shader *s) {shader_image_binding_mask_build(s);}
unsigned vk_test_shader_image_binding_indices(const vk_shader *s,unsigned *indices,unsigned capacity,unsigned *visited) {
  unsigned n=0,k;*visited=0;
  for(vk_image_binding_iterator it=vk_image_binding_begin(s);vk_image_binding_next(s,&it,&k);) {
    (*visited)++;const vk_binding *b=&s->bindings[k];
    if(b->kind!=3 && b->kind!=4 && b->kind!=6)continue;
    if(n==capacity)die("Vulkan image binding test capacity");indices[n++]=k;
  }
  return n;
}
#endif
static void shader_descriptor_layouts(vk_shader *s) {
  // Base active-binding compaction and every texel/pixel alternative conversion
  // finish before this shared constructor. Never inherit a different schema's
  // index mask through the alternatives' struct copies.
  shader_pool_binding_mask_build(s);
  shader_image_binding_mask_build(s);
  s->storage_descriptor_count=0;
  s->has_writable_buffers=0;
  VkDescriptorSetLayoutBinding bindings[128];
  // Immutable reflection classification, independent of descriptor contents
  // and submission epochs. Avoid rescanning uniform/sampler/buffer bindings
  // for every draw in attachment-store lookahead.
  if(s->nb) {
    s->image_read_slots=malloc(s->nb*sizeof *s->image_read_slots);
    if(!s->image_read_slots) die("Vulkan image read slot allocation");
  }
  for (uint32_t n = 0; n < s->nb; n++) {
    const vk_binding *b = &s->bindings[n];
    // Final active schema only. This helper also runs after an alternative
    // shader has converted its binding kinds; no inherited flag can go stale.
    // Dynamic buffer addresses/texel validity still resolve in every draw.
    if((b->kind==2 || b->kind==8 || b->kind==9) && vk_binding_writable(b))
      s->has_writable_buffers=1;
    int64_t slot;
    if(b->kind==4||b->kind==9) slot=INT64_C(128)+b->reg;
    else if(b->kind==3||b->kind==6||b->kind==7||b->kind==VK_BINDING_TEXEL_STORAGE) slot=b->reg;
    else slot=INT64_MIN;
    if(slot!=INT64_MIN)
      s->image_read_slots[s->image_read_count++]=slot>=0&&slot<160?(int)slot:-1;
    if (b->count < 1 || b->count > 128)
      die("Vulkan invalid descriptor count");
    if(descriptor_type(b)==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)s->storage_descriptor_count+=b->count;
    bindings[n] =
        (VkDescriptorSetLayoutBinding){.binding = b->binding,
                                       .descriptorType = descriptor_type(b),
                                       .descriptorCount = b->count,
                                       .stageFlags = vk_stage(s->stage)};
  }
  s->descriptors=descriptor_layout_get(bindings,s->nb);
  // Both variants are immutable schemas. Extra guest Uniform offsets may
  // change per draw without rewriting an otherwise identical descriptor set.
  // c1 is immutable shader data; leave it static. Never widen buffer ranges.
  for(unsigned variant=0;variant<2;variant++) {
    unsigned limit=variant?VK_UNIFORM_OFFSET_CAPACITY:4;
    for(uint32_t n=0;n<s->nb;n++) {
      const vk_binding *b=&s->bindings[n];
      int narrow=b->reg==0 || b->reg==2 || b->reg==3 || b->reg==4;
      int eligible=b->reg==0 || b->reg==2 || (b->reg>=3 && b->reg<19);
      if(b->kind==1 && b->count==1 && (variant?eligible:narrow))
        s->dynamic_uniform_ordinals[variant][n]=1,s->dynamic_uniform_count[variant]++;
    }
    if(s->dynamic_uniform_count[variant]>limit ||
        s->dynamic_uniform_count[variant]>V.props.limits.maxDescriptorSetUniformBuffersDynamic) {
      if(variant) {
        memcpy(s->dynamic_uniform_ordinals[1],s->dynamic_uniform_ordinals[0],sizeof s->dynamic_uniform_ordinals[0]);
        s->dynamic_uniform_count[1]=s->dynamic_uniform_count[0];
      } else {
        memset(s->dynamic_uniform_ordinals[0],0,sizeof s->dynamic_uniform_ordinals[0]);s->dynamic_uniform_count[0]=0;
      }
    }
    for(uint32_t n=0;n<s->nb;n++) {
      bindings[n].descriptorType=descriptor_type(&s->bindings[n]);
      if(!s->dynamic_uniform_ordinals[variant][n])continue;
      uint32_t ordinal=1;
      for(uint32_t j=0;j<s->nb;j++)if(s->dynamic_uniform_ordinals[variant][j] && bindings[j].binding<bindings[n].binding)ordinal++;
      s->dynamic_uniform_ordinals[variant][n]=ordinal;
      bindings[n].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    }
    s->dynamic_descriptors[variant]=s->dynamic_uniform_count[variant]?descriptor_layout_get(bindings,s->nb):s->descriptors;
  }
  s->descriptor_templates[0]=descriptor_template_create(s,0);
  for(unsigned variant=0;variant<2;variant++)
    s->descriptor_templates[variant+1]=s->dynamic_uniform_count[variant]?descriptor_template_create(s,variant+1):s->descriptor_templates[0];
}
#include "nvn_vk_driver_keys.inc"
#include "nvn_vk_texel_storage.inc"
#include "nvn_vk_pixel_local.inc"

int vk_shader_fingerprint(const char *key,uint8_t digest[32]) {
  if(strlen(key)!=16 || strspn(key,"0123456789abcdef")!=16 || !V.options.shader_root)return 0;
  const char *suffixes[]={"meta","spv","spec","pred","tssbo"};
  uint8_t hashes[5][32]={{0}};
  for(unsigned i=0;i<5;i++) {
    char path[2048];
    if(snprintf(path,sizeof path,"%s/%s.%s",V.options.shader_root,key,suffixes[i])>=(int)sizeof path)return 0;
    FILE *f=fopen(path,"rb");
    if(!f) { if(i<2 || errno!=ENOENT)return 0;continue; }
    struct stat st;
    if(fstat(fileno(f),&st)||st.st_size<=0||st.st_size>(32<<20)){fclose(f);return 0;}
    void *data=malloc(st.st_size);int ok=data && fread(data,1,st.st_size,f)==(size_t)st.st_size;
    fclose(f);if(!ok){free(data);return 0;}
    CC_SHA256(data,st.st_size,hashes[i]);free(data);
  }
  // The startup-disabled path uses only the original five-artifact identity:
  // no DPL1 file probe/read is added. Enabled input variants include the full
  // optional closure during shader creation/prewarm verification, never draw
  // lookup. A later prewarm verification may recompute this fingerprint.
  CC_SHA256(hashes,sizeof hashes,digest);
  if(vk_pixel_local_enabled()) {
    size_t local_bytes;void *local=pixel_local_read(key,"plocal",&local_bytes);
    if(local) {
      uint8_t combined[64];memcpy(combined,digest,32);
      CC_SHA256(local,local_bytes,combined+32);free(local);CC_SHA256(combined,sizeof combined,digest);
    }
  }
  return 1;
}
vk_shader *vk_shader_get(const void *control) {
  uint8_t digest[32];
  CC_SHA256(control, 0x880, digest);
  char key[17];
  for (int n = 0; n < 8; n++)
    snprintf(key + n * 2, 3, "%02x", digest[n]);
  return vk_shader_load_key(key);
}
const char *vk_shader_quad_transfer_error(const vk_shader *s,VkSampleCountFlagBits samples,
    VkPrimitiveTopology topology,VkPolygonMode polygon) {
  if(!s || !s->quad_transfer)return NULL;
  if(s->stage!=1)return "fragment stage required";
  if(samples!=VK_SAMPLE_COUNT_1_BIT)return "single-sample rasterization required";
  if(polygon!=VK_POLYGON_MODE_FILL)return "filled triangles required";
  if(topology!=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST &&
     topology!=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP &&
     topology!=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN)return "triangle topology required";
  // Raw FragCoord includes helper pixels immediately outside framebuffer edges.
  // Below 2^23, FP32 represents every pixel-center and its unit displacement;
  // signed floor-to-int also represents the negative edge helper coordinates.
  // Use immutable device caps so dynamic viewport/scissor changes need no hot
  // per-draw checks and cannot bypass this proof through prepared-PSO reuse.
  if(V.props.limits.maxFramebufferWidth>=(UINT32_C(1)<<23) ||
     V.props.limits.maxFramebufferHeight>=(UINT32_C(1)<<23))
    return "framebuffer coordinate precision not proven";
  return NULL;
}
const char *vk_shader_quad_readonly_stages_error(vk_shader *const list[6]) {
  if(!list[1] || !list[1]->quad_readonly_sources)return NULL;
  if(!list[1]->quad_transfer || list[1]->stage!=1)return "readonly quad sources require lowered fragment shader";
  if(!V.features.robustBufferAccess)return "readonly quad sources require enabled robustBufferAccess";
  for(unsigned i=0;i<6;i++)if(list[i] && list[i]->has_resource_writes)
    return "readonly quad sources require all graphics stages without resource writes";
  return NULL;
}
vk_shader *vk_shader_load_key(const char *key) {
  if(strlen(key)!=16 || strspn(key,"0123456789abcdef")!=16)die("Vulkan invalid shader key");
  for (vk_shader *s = shaders; s; s = s->next)
    if (!strcmp(s->key, key)) {
      // Startup prewarming may load the base before a supported diagnostic
      // representation is enabled. Resolve that immutable sidecar once on
      // the recording owner; never let warm-up remove later valid choices.
      if(s->stage==1 && !s->texel_storage_checked && texel_storage_mode()) {
        size_t size;uint32_t *spv=read_cache(key,"spv",&size);
        texel_storage_load(s,spv,size);free(spv);
      }
      return s;
    }
  size_t bytes;
  uint32_t *metadata = read_cache(key, "meta", &bytes);
  unsigned header=bytes>=4&&metadata[0]==VK_META_V3?16:bytes>=4&&metadata[0]==VK_META_V2?12:8;
  if (bytes < header*4 || (metadata[0] != VK_META_V1 && metadata[0] != VK_META_V2 && metadata[0] != VK_META_V3) || metadata[1] >= 6 ||
      metadata[2] >= V.props.limits.maxBoundDescriptorSets ||
      metadata[3] > 128 || bytes != header*4 + (size_t)metadata[3] * 32 + metadata[4])
    die("Vulkan shader metadata invalid: %s", key);
  vk_shader *s = calloc(1, sizeof *s);
  if (!s)
    die("Vulkan shader allocation");
  s->canonical_base=s;
  strcpy(s->key, key);
  if(!vk_shader_fingerprint(key,s->fingerprint))die("Vulkan shader fingerprint: %s",key);
  s->stage = metadata[1];
  s->set = metadata[2];
  s->nb = metadata[3];
  s->nc1 = metadata[4];
  s->pos_attr=-1;s->may_discard=s->writes_smask=1;
  if(header>=12) {
    if((int32_t)metadata[8]<-1||(int32_t)metadata[8]>=16||(metadata[9]&~(7u|DM_VK_META_QUAD_TRANSFER|DM_VK_META_QUAD_READONLY_SOURCES))||(metadata[10]&~255u)||metadata[11]!=1)
      die("Vulkan raster metadata invalid: %s",key);
    s->pos_attr=(int32_t)metadata[8];s->pos_w1=metadata[9]&1;
    s->may_discard=(metadata[9]>>1)&1;s->writes_smask=(metadata[9]>>2)&1;s->color_mask=metadata[10];
    s->quad_transfer=metadata[9]&DM_VK_META_QUAD_TRANSFER;
    s->quad_readonly_sources=metadata[9]&DM_VK_META_QUAD_READONLY_SOURCES;
    if(s->quad_readonly_sources && !s->quad_transfer)die("Vulkan readonly quad source trait requires quad transfer: %s",key);
    if(s->quad_transfer && s->stage!=1)die("Vulkan quad transfer requires fragment stage: %s",key);
  }
  if(header==16) {
    if((s->stage!=0&&s->stage!=1&&s->stage!=5)||!metadata[12]||
       (s->stage!=5&&(metadata[12]&0xffff0000u))||
       metadata[13]!=(s->stage==1?64u:0u)||metadata[14]!=(s->stage==5?32u:16u)||metadata[15]!=1)
      die("Vulkan storage bias metadata invalid: %s",key);
    s->storage_bias_mask=metadata[12];
  }
  memcpy(s->local, metadata + 5, 12);
  s->bindings = (void *)(metadata + header);
  s->c1 = (void *)(s->bindings + s->nb);
  uint32_t *spv = read_cache(key, "spv", &bytes);
  if (bytes % 4 || bytes < 20 || spv[0] != 0x07230203)
    die("Vulkan shader module invalid: %s", key);
  // 离线优化已删除的资源不再分配描述符，也不反复复制未读取的常量。
  // Do not allocate descriptors or recopy constants for resources removed by
  // offline optimization.
  uint8_t active_bindings[128] = {0};
  for (size_t n = 5; n < bytes / 4;) {
    uint32_t count = spv[n] >> 16, op = spv[n] & 0xffff;
    if (!count || count > bytes / 4 - n)
      die("Vulkan malformed descriptor module");
    if (op == 71 && count == 4 && spv[n + 2] == 33) {
      if (spv[n + 3] >= 128)
        die("Vulkan descriptor binding exceeds metadata ABI");
      active_bindings[spv[n + 3]] = 1;
    }
    n += count;
  }
  uint32_t used = 0;
  for (uint32_t n = 0; n < s->nb; n++) {
    if (s->bindings[n].binding < 0 || s->bindings[n].binding >= 128)
      die("Vulkan metadata binding out of range");
    if (active_bindings[s->bindings[n].binding]) {
      s->has_resource_writes|=vk_binding_writable(&s->bindings[n]);
      s->bindings[used++] = s->bindings[n];
    }
  }
  s->nb = used;
  if (s->stage == 0) {
    if (spv[3] > 1048576)
      die("Vulkan SPIR-V ID bound: %s", key);
    uint32_t *locations = malloc((size_t)spv[3] * sizeof *locations);
    if (!locations)
      die("Vulkan vertex reflection allocation");
    memset(locations, 255, (size_t)spv[3] * sizeof *locations);
    for (size_t n = 5; n < bytes / 4;) {
      uint32_t count = spv[n] >> 16, op = spv[n] & 0xffff;
      if (!count || count > bytes / 4 - n)
        die("Vulkan malformed vertex module");
      if (op == 71 && count == 4 && spv[n + 2] == 30 && spv[n + 1] < spv[3])
        locations[spv[n + 1]] = spv[n + 3];
      n += count;
    }
    for (size_t n = 5; n < bytes / 4;) {
      uint32_t count = spv[n] >> 16, op = spv[n] & 0xffff;
      if (op == 59 && count >= 4 && spv[n + 3] == 1 && spv[n + 2] < spv[3]) {
        uint32_t location = locations[spv[n + 2]];
        if (location != UINT32_MAX) {
          if (location >= 16)
            die("Vulkan vertex input location %u", location);
          s->input_mask |= 1u << location;
        }
      }
      n += count;
    }
    free(locations);
  }
  for (size_t n = 5; n < bytes / 4;) {
    uint32_t count = spv[n] >> 16, op = spv[n] & 0xffff;
    if (!count || count > bytes / 4 - n)
      die("Vulkan malformed SPIR-V %s", key);
    if (op == 17 && count == 2) {
      if(spv[n+1]==9 && !V.shader_float16)
        die("Vulkan shader %s requires unsupported shaderFloat16",key);
      if (spv[n + 1] == 4427 && !V.shader_draw_parameters)
        die("Vulkan shader %s requires shaderDrawParameters", key);
      if (spv[n + 1] == 10 && !V.features.shaderFloat64)
        die("Vulkan shader %s requires unsupported shaderFloat64", key);
      if (spv[n + 1] == 11 && !V.features.shaderInt64)
        die("Vulkan shader %s requires unsupported shaderInt64", key);
      if (spv[n + 1] == 55 && !V.features.shaderStorageImageReadWithoutFormat)
        die("Vulkan shader %s requires formatless image reads", key);
      if (spv[n + 1] == 56 && !V.features.shaderStorageImageWriteWithoutFormat)
        die("Vulkan shader %s requires formatless image writes", key);
    }
    n += count;
  }
  VkShaderModuleCreateInfo ci = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = bytes,
      .pCode = spv};
  int module_trace=vk_system_trace_begin("Vulkan shader module creation");
  VK_CHECK(vkCreateShaderModule(V.device, &ci, NULL, &s->module));
  vk_system_trace_end(module_trace);
  uniform_specialization_load(s,spv,bytes);
  shader_descriptor_layouts(s);
  texel_storage_load(s,spv,bytes);
  driver_schema_load(s);
  free(spv);
  s->next = shaders;
  shaders = s;
  LOG("[vulkan] shader %s stage=%u bindings=%u dynamic_ubos=%u/%u", key, s->stage, s->nb,s->dynamic_uniform_count[0],s->dynamic_uniform_count[1]);
  return s;
}
static descriptor_block *descriptor_create(void) {
  unsigned max_sets = V.options.descriptor_sets;
  const VkDescriptorPoolSize sizes[] = {
      {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, max_sets * VK_PIXEL_LOCAL_MAX_INPUTS},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, max_sets * 8},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, max_sets * VK_UNIFORM_OFFSET_CAPACITY},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, max_sets * 8},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, max_sets * 16},
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, max_sets * 16},
      {VK_DESCRIPTOR_TYPE_SAMPLER, max_sets * 16},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, max_sets * 4},
      {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, max_sets * 4},
      {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, max_sets * 4}};
  unsigned omit_inputs=!vk_pixel_local_enabled();
  VkDescriptorPoolCreateInfo ci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = max_sets,
      .poolSizeCount = sizeof sizes / sizeof *sizes-omit_inputs,
      .pPoolSizes = sizes+omit_inputs};
  descriptor_block *block = calloc(1, sizeof *block);
  if (!block)
    die("Vulkan descriptor block allocation");
  VK_CHECK(vkCreateDescriptorPool(V.device, &ci, NULL, &block->pool));
  return block;
}
void vk_shader_recycle(vk_shader_submission *state) {
  if (!state) return;
  if (binding_state == state)
    die("Vulkan recycling recording descriptor resources");
  memset(state->driver_last,0,sizeof state->driver_last);
  memset(state->descriptor_last,0,sizeof state->descriptor_last);
  memset(state->descriptor_shader_hints,0,sizeof state->descriptor_shader_hints);
  if(!++state->descriptor_history_epoch)die("Vulkan descriptor history epoch overflow");
  // Completed contexts need no content history. Bound idle CPU cache storage;
  // active/in-flight contexts keep their immutable sets and exact keys intact.
  if(atomic_load(&descriptor_history_bytes)>(16u<<20))descriptor_history_release(state);
  // Reuse only after GPU completion AND CPU completion effects. Every issued
  // set is fully rewritten before its next use; descriptor contents are never
  // a cross-submission cache. Layouts here are device-lifetime interned objects.
  // Bound retained driver objects to one configured pool's max-set budget;
  // scene/layout churn resets at this safe boundary instead of growing forever.
  int reset=state->descriptor_reuse_reference ||
      atomic_load(&descriptor_reuse_reference) ||
      state->descriptor_retained>V.options.descriptor_sets;
  for(unsigned i=0;i<128;i++)for(descriptor_batch *b=state->descriptor_batches[i];b;b=b->next) {
    b->count=b->issued=b->retained_used=0;
    if(reset) {
      free(b->retained);b->retained=NULL;
      b->retained_count=b->retained_capacity=0;
    }
  }
  if(reset)state->descriptor_retained=0;
  while(state->driver_arenas) {
    driver_arena *a=state->driver_arenas;state->driver_arenas=a->next;a->used=0;
    if(available_driver_arena_count<32) {
      a->next=available_driver_arenas;available_driver_arenas=a;
      available_driver_arena_count++;
    } else {
      atomic_fetch_sub(&driver_arena_live,a->buffer.size);
      vk_buffer_destroy(&a->buffer);free(a);
    }
  }
  state->driver_arena_current=NULL;
  for (unsigned i = 0; i < 1024; i++)
    while (state->driver_constants[i]) {
      driver_constant *copy = state->driver_constants[i];
      state->driver_constants[i] = copy->next;
      free(copy);
    }
  for (unsigned i = 0; i < 1024; i++)
    while (state->readonly_storage_copies[i]) {
      constant_copy *copy = state->readonly_storage_copies[i];
      state->readonly_storage_copies[i] = copy->next;
      free(copy);
    }
  for (unsigned i = 0; i < 1024; i++)
    while (state->constant_copies[i]) {
      constant_copy *copy = state->constant_copies[i];
      state->constant_copies[i] = copy->next;
      free(copy);
    }
  if(reset) {
    for (descriptor_block *block = state->descriptor_blocks; block; block = block->next) {
      VK_CHECK(vkResetDescriptorPool(V.device, block->pool, 0));
      atomic_fetch_add(&descriptor_pool_resets,1);
    }
    state->descriptor_current = NULL;
  }
}
#ifdef DM_VULKAN_TEST_EMBED
unsigned vk_test_descriptor_shader_hint_edges(void) {
  vk_shader_submission *saved=binding_state,*state=calloc(1,sizeof *state);
  if(!state)die("Descriptor hint owned state allocation");
  vk_shader shaders[33]={0};descriptor_value value={11,16,32},changed={11,32,32};
  VkDescriptorSetLayout layout=(VkDescriptorSetLayout)(uintptr_t)16;
  binding_state=state;state->descriptor_history_epoch=7;
  descriptor_history_entry *entry=calloc(1,sizeof *entry+sizeof value);
  if(!entry)die("Descriptor hint owned entry allocation");
  atomic_fetch_add(&descriptor_history_bytes,sizeof *entry+sizeof value);
  *entry=(descriptor_history_entry){.shader=&shaders[0],.layout=layout,
    .set=(VkDescriptorSet)(uintptr_t)22,.epoch=7,.capacity=1};
  entry->values[0]=value;state->descriptor_history[9]=entry;
  descriptor_shader_hint_remember(&shaders[0],layout,state->descriptor_history+9);
  unsigned checks=0;
#define HINT_CHECK(x) do{if(!(x))die("Descriptor hint edge failed line %d",__LINE__);checks++;}while(0)
  HINT_CHECK(descriptor_shader_hint_find(&shaders[0],layout,&value,sizeof value)==entry);
  HINT_CHECK(!descriptor_shader_hint_find(&shaders[0],layout,&changed,sizeof changed));
  HINT_CHECK(!descriptor_shader_hint_find(&shaders[0],(VkDescriptorSetLayout)(uintptr_t)32,&value,sizeof value));
  entry->epoch=6;HINT_CHECK(!descriptor_shader_hint_find(&shaders[0],layout,&value,sizeof value));entry->epoch=7;
  entry->shader=&shaders[1];HINT_CHECK(!descriptor_shader_hint_find(&shaders[0],layout,&value,sizeof value));entry->shader=&shaders[0];
  state->descriptor_history[9]=NULL;HINT_CHECK(!descriptor_shader_hint_find(&shaders[0],layout,&value,sizeof value));
  state->descriptor_history[9]=entry;
  // Two independently allocated entries may replace the SAME slot; no old
  // entry pointer can survive the current slot lookup, even when realloc moves.
  descriptor_history_entry *replacement=malloc(sizeof *entry+sizeof value);
  if(!replacement)die("Descriptor hint owned replacement allocation");
  memcpy(replacement,entry,sizeof *entry+sizeof value);free(entry);entry=replacement;
  state->descriptor_history[9]=entry;
  HINT_CHECK(descriptor_shader_hint_find(&shaders[0],layout,&value,sizeof value)==replacement);
  unsigned first=33,collision=33;
  for(unsigned a=0;a<33 && first==33;a++)for(unsigned b=a+1;b<33;b++)
    if(descriptor_shader_hint_slot(&shaders[a],layout)==descriptor_shader_hint_slot(&shaders[b],layout)) {
      first=a;collision=b;break;
    }
  HINT_CHECK(first<33 && collision<33);
  entry->shader=&shaders[first];
  descriptor_shader_hint_remember(&shaders[first],layout,state->descriptor_history+9);
  descriptor_shader_hint_remember(&shaders[collision],layout,state->descriptor_history+9);
  HINT_CHECK(!descriptor_shader_hint_find(&shaders[first],layout,&value,sizeof value));
  descriptor_shader_hint_remember(&shaders[0],layout,state->descriptor_history+9);
  binding_state=NULL;vk_shader_recycle(state);binding_state=state;
  HINT_CHECK(state->descriptor_history_epoch==8);
  HINT_CHECK(!descriptor_shader_hint_find(&shaders[0],layout,&value,sizeof value));
  for(unsigned n=0;n<DESCRIPTOR_SHADER_HINT_SLOTS;n++)HINT_CHECK(!state->descriptor_shader_hints[n].shader);
  descriptor_history_release(state);free(state);binding_state=saved;
#undef HINT_CHECK
  return checks;
}
#endif
// Only completed/recycled arenas enter this list. Active constants and
// immutable in-flight descriptors retain arenas on their submission context.
void vk_shader_trim_idle_arenas(void) {
  if(binding_state)die("Vulkan idle arena trim during descriptor recording");
  while(available_driver_arenas) {
    driver_arena *a=available_driver_arenas;available_driver_arenas=a->next;
    if(!available_driver_arena_count || a->used)die("Vulkan idle arena ownership");
    available_driver_arena_count--;
    atomic_fetch_sub(&driver_arena_live,a->buffer.size);
    vk_buffer_destroy(&a->buffer);free(a);
  }
}
#ifdef DM_VULKAN_TEST_EMBED
unsigned vk_test_idle_driver_arenas(void) {return available_driver_arena_count;}
#endif
void vk_shader_destroy(vk_shader_submission *state) {
  if (!state) return;
  vk_shader_recycle(state);
  while (state->descriptor_blocks) {
    descriptor_block *block = state->descriptor_blocks;
    state->descriptor_blocks = block->next;
    vkDestroyDescriptorPool(V.device, block->pool, NULL);
    free(block);
  }
  for(unsigned i=0;i<128;i++)while(state->descriptor_batches[i]) {
    descriptor_batch *b=state->descriptor_batches[i];state->descriptor_batches[i]=b->next;free(b->retained);free(b);
  }
  descriptor_history_release(state);
  free(state);
}
vk_shader_submission *vk_shader_begin(vk_shader_submission *state) {
  if (binding_state) die("Vulkan nested descriptor recording");
  int reference=atomic_load(&storage_bias_requested);
  if(reference<0) {
    const char *setting=getenv("DM_VULKAN_STORAGE_BIAS_REFERENCE");
    if(setting&&strcmp(setting,"0")&&strcmp(setting,"1"))
      die("DM_VULKAN_STORAGE_BIAS_REFERENCE expects 0 or 1");
    reference=setting&&!strcmp(setting,"1");
    atomic_store(&storage_bias_requested,reference);
  }
  atomic_store(&storage_bias_active,reference);
  if (!state) {
    state = calloc(1, sizeof *state);
    if (!state) die("Vulkan descriptor resources allocation");
  }
  binding_state = state;
  binding_state->storage_bias_reference=reference;
  reference=atomic_load(&driver_reference_requested);
  if(reference<0) {
    const char *setting=getenv("DM_VULKAN_DRIVER_CONSTANT_REFERENCE");
    if(setting&&strcmp(setting,"0")&&strcmp(setting,"1"))
      die("DM_VULKAN_DRIVER_CONSTANT_REFERENCE expects 0 or 1");
    reference=setting&&!strcmp(setting,"1");
    atomic_store(&driver_reference_requested,reference);
  }
  binding_state->driver_reference=reference;
  atomic_store(&driver_reference_active,reference);
  if (!binding_state->descriptor_blocks)
    binding_state->descriptor_blocks = descriptor_create();
  if(!binding_state->descriptor_current)
    binding_state->descriptor_current = binding_state->descriptor_blocks;
  binding_state->descriptor_reuse_reference=atomic_load(&descriptor_reuse_reference);
  return state;
}
void vk_shader_end(vk_shader_submission *state) {
  if (binding_state != state || !state || shader_operation_depth)
    die("Vulkan descriptor submit ended with pending writes");
  shader_publish_diagnostics();texel_storage_publish();uniform_predicate_publish();
  binding_state = NULL;
}
static void driver_arena_store(driver_constant *copy,const uint8_t *data,size_t bytes) {
  enum { SLOTS=8, RANGE=65536 };
  driver_arena **at=&binding_state->driver_arena_current;
  if(!*at) at=&binding_state->driver_arenas;
  while(*at && (*at)->used==SLOTS) at=&(*at)->next;
  if(!*at) {
    driver_arena *a=available_driver_arenas;
    if(a) {
      available_driver_arenas=a->next;available_driver_arena_count--;
      a->next=NULL;shader_recorded.driver_arena_reuses += 1;
    } else {
      a=calloc(1,sizeof *a);
      if(!a) die("Vulkan driver uniform arena allocation");
      uint64_t align=V.props.limits.minUniformBufferOffsetAlignment;
      if(!align) align=1;
      a->stride=(RANGE+align-1)/align*align;
      vk_buffer_create(&a->buffer,a->stride*SLOTS,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
      shader_recorded.driver_arena_chunks += 1;
      atomic_fetch_add(&driver_arena_live,a->buffer.size);
    }
    *at=a;
  }
  driver_arena *a=*at;binding_state->driver_arena_current=a;
  copy->buffer=&a->buffer;copy->offset=a->used*a->stride;
  if(a->used==a->initialized) {
    memset(a->buffer.map+copy->offset,0,RANGE);
    a->initialized++;shader_recorded.driver_arena_zeroes += 1;
  }
  memcpy(a->buffer.map+copy->offset,data,bytes);
  a->used++;shader_recorded.driver_arena_slots += 1;
}
#ifdef DM_VULKAN_TEST_EMBED
void vk_test_idle_driver_store(const uint8_t *data,size_t bytes,vk_buffer *buffer,uint64_t *offset) {
  driver_constant copy={0};if(!binding_state || bytes>65536)die("Vulkan idle arena fixture");
  driver_arena_store(&copy,data,bytes);*buffer=*copy.buffer;*offset=copy.offset;
}
#endif
// Only these bytes vary in the driver ABI. Build exact keys from current S on
// every call, including direct state restores; no dirty-generation prediction.
// c0 packs address:u64,size:u32 (the ABI record's remaining u32 is zero).
// c2 packs low-u32 handles (the ABI cell's upper u32 and all gaps are zero).
static size_t driver_key_build(int slot,unsigned stage,uint8_t key[1056]) {
  if(slot==2) {
#if defined(__aarch64__)
    // vmovn keeps each handle's low32 bits, matching the scalar uint32_t
    // conversion. These unaligned-safe stores write all128 values in order.
    // The sole caller supplies local scratch, disjoint from global S.
    for(unsigned n=0;n<128;n+=4) {
      uint32x4_t tex=vcombine_u32(vmovn_u64(vld1q_u64(S.tex[stage]+n)),
                                 vmovn_u64(vld1q_u64(S.tex[stage]+n+2)));
      uint32x4_t samp=vcombine_u32(vmovn_u64(vld1q_u64(S.samp[stage]+n)),
                                  vmovn_u64(vld1q_u64(S.samp[stage]+n+2)));
      vst1q_u8(key+n*4,vreinterpretq_u8_u32(tex));
      vst1q_u8(key+512+n*4,vreinterpretq_u8_u32(samp));
    }
#else
    for(unsigned n=0;n<128;n++) {
      uint32_t tex=S.tex[stage][n],samp=S.samp[stage][n];
      memcpy(key+n*4,&tex,4);memcpy(key+512+n*4,&samp,4);
    }
#endif
    for(unsigned n=0;n<8;n++) {
      uint32_t img=S.img[stage][n];memcpy(key+1024+n*4,&img,4);
    }
    return 1056;
  }
  if(stage!=0 && stage!=1 && stage!=5)die("Vulkan driver c0 stage %u",stage);
  for(unsigned bank=0;bank<(stage==5?2u:1u);bank++)
    for(unsigned n=0;n<16;n++) {
      const p_bind_buf *u=bank?&S.ubo[stage][n]:&S.ssbo[stage][n];
      uint32_t size=u->size;uint8_t *at=key+bank*192+n*12;
      memcpy(at,&u->addr,8);memcpy(at+8,&size,4);
    }
  return stage==5?384:192;
}
static void driver_key_expand(int slot,unsigned stage,const uint8_t *key,uint8_t data[0xa00]) {
  memset(data,0,0xa00);
  if(slot==2) {
    for(unsigned n=0;n<128;n++) {
      memcpy(data+0x168+n*8,key+n*4,4);memcpy(data+0x568+n*8,key+512+n*4,4);
    }
    for(unsigned n=0;n<8;n++)memcpy(data+0x120+n*8,key+1024+n*4,4);
  } else {
    unsigned base=stage==0?0x110:stage==1?0x510:0x310;
    for(unsigned n=0;n<16;n++) {
      memcpy(data+base+n*16,key+n*12,12);
      if(stage==5)memcpy(data+0x210+n*16,key+192+n*12,12);
    }
  }
}
// Keep the independent full-ABI reference construction for controlled tests.
static void driver_data_reference(int slot,unsigned stage,uint8_t driver_data[0xa00]) {
  memset(driver_data,0,0xa00);
  if (slot == 2) {
    for (int n = 0; n < 128; n++) {
      uint32_t tex = S.tex[stage][n], samp = S.samp[stage][n];
      memcpy(driver_data + 0x168 + n * 8, &tex, 4);
      memcpy(driver_data + 0x568 + n * 8, &samp, 4);
    }
    for (int n = 0; n < 8; n++) {
      uint32_t img = S.img[stage][n];
      memcpy(driver_data + 0x120 + n * 8, &img, 4);
    }
  }
  if (slot == 0) {
    int base = stage == 0   ? 0x110
               : stage == 1 ? 0x510
               : stage == 5 ? 0x310
                               : -1;
    if (base < 0)
      die("Vulkan driver c0 stage %u", stage);
    for (int n = 0; n < 16; n++) {
      p_bind_buf *u = &S.ssbo[stage][n];
      memcpy(driver_data + base + n * 16, &u->addr, 8);
      uint32_t size = u->size;
      memcpy(driver_data + base + n * 16 + 8, &size, 4);
      if (stage == 5) {
        u = &S.ubo[5][n];
        memcpy(driver_data + 0x210 + n * 16, &u->addr, 8);
        size = u->size;
        memcpy(driver_data + 0x210 + n * 16 + 8, &size, 4);
      }
    }
  }
}
static vk_buffer *constant(vk_shader *s, int slot,uint64_t *offset) {
  *offset=0;
  if (slot < 0) {
    if (!support_constant.handle) {
      vk_buffer_create(&support_constant, 65536,
                       VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
      memset(support_constant.map, 0, 65536);
      for (int n = 0; n < 73; n++)
        *(float *)(support_constant.map + 192 + n * 16) = 1;
    }
    return &support_constant;
  }
  if (slot == 1) {
    if (s->nc1 > 65536)
      die("Vulkan c1 oversized");
    if (!s->c1_buffer.handle) {
      vk_buffer_create(&s->c1_buffer, 65536,
                       VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
      memset(s->c1_buffer.map, 0, 65536);
      memcpy(s->c1_buffer.map, s->c1, s->nc1);
    }
    return &s->c1_buffer;
  }
  if (slot >= 3) {
    int reg = slot - 3;
    if (reg >= 16)
      die("Vulkan constant bank %d", slot);
    const p_bind_buf *u = &S.ubo[s->stage][reg];
    if (!u->addr || !u->size) {
      if (!zero_constant.handle) {
        vk_buffer_create(&zero_constant, 65536,
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        memset(zero_constant.map, 0, 65536);
      }
      return &zero_constant;
    }
#ifdef DM_VULKAN_SHARED_POOLS
    // A historical copied constant is still a semantic read of this NVN
    // range. Do not bypass a later propagated-encoding refusal on cache HIT.
    if(vk_legacy_astc_copy_mode() && !vk_pool_shared_active())
      vk_texture_shared_linear_access(u->addr,u->size);
#endif
    unsigned bucket = ((u->addr >> 4) ^ (u->addr >> 20) ^ u->size) % 1024;
    for (constant_copy *copy = binding_state->constant_copies[bucket]; copy; copy = copy->next)
      if (copy->address == u->addr && copy->size == u->size &&
          copy->epoch == V.buffer_epoch)
        return copy->buffer;
  }
  // Immutable host-generated tables: keys and GPU bytes remain frozen until
  // this physical submission completes. Slot/stage are independent identities.
  if (slot == 0 || slot == 2) {
    // Key dies at a hit, or is frozen in copy before miss expansion. Share
    // scratch storage so hits do not add another 1056 bytes to the stack.
    union { uint8_t key[1056],data[0xa00]; } scratch;
    uint8_t *key=scratch.key,*driver_data=scratch.data;
    int reference=binding_state->driver_reference;
    const vk_driver_schema *schema=reference?NULL:s->driver_schema[slot/2];
    size_t bytes;
    const uint8_t *content;
    if(reference) {
      driver_data_reference(slot,s->stage,driver_data);
      content=driver_data;bytes=sizeof scratch.data;
    } else {
      bytes=schema?driver_sparse_key_build(schema,slot,s->stage,key):driver_key_build(slot,s->stage,key);content=key;
    }
    driver_constant **last=&binding_state->driver_last[s->stage][slot/2];
    if(!reference&&*last&&(*last)->schema==schema&&!memcmp((*last)->data,content,bytes)) {
      shader_recorded.driver_last_hits += 1;
      *offset=(*last)->offset;
      return (*last)->buffer;
    }
    uint64_t hash = 14695981039346656037ull;
    if(!reference)hash=(hash^((uint64_t)s->stage<<32 | (unsigned)slot))*1099511628211ull;
    if(schema)hash=(hash^(uintptr_t)schema)*1099511628211ull;
    for (size_t i = 0; i < bytes; i += 8) {
      uint64_t word;
      memcpy(&word, content + i, 8);
      hash = (hash ^ word) * 1099511628211ull;
    }
    unsigned bucket = hash % 1024;
    for (driver_constant *copy = binding_state->driver_constants[bucket]; copy;
         copy = copy->next)
      if (copy->hash == hash && copy->slot == slot && copy->stage == s->stage && copy->schema==schema &&
          !memcmp(reference?copy->buffer->map+copy->offset:copy->data,content,bytes)) {
        *last=copy;
        shader_recorded.driver_content_hits += 1;
        *offset=copy->offset;
        return copy->buffer;
      }
    driver_constant *copy = calloc(1, sizeof *copy + (reference?0:bytes));
    if (!copy)die("Vulkan immutable driver constant allocation");
    copy->hash = hash;copy->slot = slot;copy->stage = s->stage;
    copy->schema=schema;
    if(!reference) {
      memcpy(copy->data,content,bytes);
      // A sparse key is only an equality proof, never a GPU table format.
      // Reuse the original complete ABI construction on this rarer MISS;
      // no second full-key scratch allocation/spill is needed on every hit.
      if(schema)driver_data_reference(slot,s->stage,driver_data);
      else driver_key_expand(slot,s->stage,copy->data,driver_data);
    }
    if(driver_arena_reference()) {
      copy->buffer = vk_temp_buffer(65536);
      memset(copy->buffer->map, 0, 65536);
      memcpy(copy->buffer->map, driver_data, sizeof scratch.data);
    } else driver_arena_store(copy,driver_data,sizeof scratch.data);
    copy->next = binding_state->driver_constants[bucket];
    binding_state->driver_constants[bucket] = copy;*last=copy;
    shader_recorded.driver_misses += 1;
    *offset=copy->offset;
    return copy->buffer;
  }
  vk_buffer *b = vk_temp_buffer(65536);
  memset(b->map, 0, 65536);
  int reg = slot - 3;
  if (reg < 0 || reg >= 16)
    die("Vulkan constant bank %d", slot);
  p_bind_buf *u = &S.ubo[s->stage][reg];
  if (u->addr && u->size) {
    vk_buffer *src;
    uint64_t off;
    if (!vk_pool_address_stage(u->addr,u->size,(void **)&src,&off,VK_PIPELINE_STAGE_TRANSFER_BIT))
      die("Vulkan uniform outside pool");
    uint64_t size = u->size < 65536 ? u->size : 65536;
    // 对齐要求属于描述符偏移；暂存拷贝保留原 Metal 的字节范围。
    // Descriptor offsets require alignment; staging copies preserve the
    // original Metal byte range, including odd offsets and trailing bytes.
    VkBufferCopy copy = {.srcOffset = off, .size = size};
    vk_render_end();
    vk_buffer_shader_read(u->addr,size,VK_PIPELINE_STAGE_TRANSFER_BIT);
    vkCmdCopyBuffer(V.cmd, src->handle, b->handle, 1, &copy);
    vk_buffer_transfer_written();
    constant_copy *cached = calloc(1, sizeof *cached);
    if (!cached)
      die("Vulkan uniform copy cache allocation");
    cached->address = u->addr;
    cached->size = u->size;
    cached->epoch = V.buffer_epoch;
    cached->buffer = b;
    unsigned bucket = ((u->addr >> 4) ^ (u->addr >> 20) ^ u->size) % 1024;
    cached->next = binding_state->constant_copies[bucket];
    binding_state->constant_copies[bucket] = cached;
  }
  return b;
}
static VkResult descriptor_allocate_many(VkDescriptorSetLayout layout,uint32_t count,VkDescriptorSet *sets) {
  VkDescriptorSetLayout layouts[16];for(uint32_t i=0;i<count;i++)layouts[i]=layout;
  VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    .descriptorPool=binding_state->descriptor_current->pool,.descriptorSetCount=count,.pSetLayouts=layouts};
  shader_recorded.descriptor_allocate_calls += 1;
  VkResult result=vkAllocateDescriptorSets(V.device,&ai,sets);
  if(result==VK_SUCCESS)shader_recorded.descriptor_allocate_sets += count;
  return result;
}
static int descriptor_pool_full(VkResult result) { return result==VK_ERROR_OUT_OF_POOL_MEMORY || result==VK_ERROR_FRAGMENTED_POOL; }
VkDescriptorSet vk_descriptor_allocate(VkDescriptorSetLayout layout) {
  // Unwritten sets remain private to this submission/context. Shader/compute
  // layout changes use separate bins; no in-flight descriptor is ever updated.
  descriptor_batch *batch=NULL;VkDescriptorSet one;
  uint32_t count=1;
  int reuse=!binding_state->descriptor_reuse_reference;
  if(reuse || !atomic_load(&descriptor_batch_reference)) {
    unsigned bucket=((uint64_t)layout>>4)%128;
    for(batch=binding_state->descriptor_batches[bucket];batch && batch->layout!=layout;batch=batch->next){}
    if(!batch) {
      batch=calloc(1,sizeof *batch);if(!batch)die("Vulkan descriptor batch allocation");
      batch->layout=layout;batch->next=binding_state->descriptor_batches[bucket];binding_state->descriptor_batches[bucket]=batch;
    }
    if(reuse && batch->retained_used<batch->retained_count) {
      shader_recorded.descriptor_reuses += 1;
      return batch->retained[batch->retained_used++];
    }
    if(batch->issued<8)batch->issued++;
    if(!reuse && batch->count)return batch->sets[--batch->count];
    // Rare layouts reserve nothing. Grow only after demonstrated demand;
    // cap at eight so an early target boundary cannot strand sixteen sets.
    count=atomic_load(&descriptor_batch_reference)?1:batch->issued<2?1:batch->issued<4?2:batch->issued<8?4:8;
    if(count>V.options.descriptor_sets)count=V.options.descriptor_sets;
  }
  VkDescriptorSet *sets=batch?batch->sets:&one;
  VkResult allocated=descriptor_allocate_many(layout,count,sets);
  if(descriptor_pool_full(allocated) && count>1) {
    // A failed batch is all-or-nothing. Try one in the same pool to consume
    // remaining capacity instead of wasting it or allocating another pool.
    count=1;allocated=descriptor_allocate_many(layout,1,sets);
  }
  if(descriptor_pool_full(allocated)) {
    if(!binding_state->descriptor_current->next)binding_state->descriptor_current->next=descriptor_create();
    binding_state->descriptor_current=binding_state->descriptor_current->next;
    allocated=descriptor_allocate_many(layout,count,sets);
  }
  if(allocated!=VK_SUCCESS)die("Vulkan descriptor allocation: %d",allocated);
  if(reuse) {
    uint32_t needed=batch->retained_count+count;
    if(needed<batch->retained_count)die("Vulkan retained descriptor overflow");
    if(needed>batch->retained_capacity) {
      uint32_t capacity=batch->retained_capacity?batch->retained_capacity*2:16;
      if(capacity<needed)capacity=needed;
      VkDescriptorSet *grown=realloc(batch->retained,(size_t)capacity*sizeof *grown);
      if(!grown)die("Vulkan retained descriptor handles allocation");
      batch->retained=grown;batch->retained_capacity=capacity;
    }
    memcpy(batch->retained+batch->retained_count,sets,count*sizeof *sets);
    batch->retained_count=needed;binding_state->descriptor_retained+=count;
    return batch->retained[batch->retained_used++];
  }
  if(batch) { batch->count=count-1;return batch->sets[count-1]; }
  return one;
}
void vk_shader_prepare_images(vk_shader *const list[6],int compute) {
  for(unsigned stage=compute?5:0;stage<(compute?6:5);stage++) if(list[stage])
    for(vk_image_binding_iterator it=vk_image_binding_begin(list[stage]);;) {
      unsigned k;if(!vk_image_binding_next(list[stage],&it,&k))break;
      const vk_binding *b=&list[stage]->bindings[k];
      if(b->kind!=3 && b->kind!=4 && b->kind!=6) continue;
      if(b->reg<0 || b->reg>=(b->kind==4?32:128)) die("Vulkan texture slot unknown");
      uint64_t handle=b->kind==4?S.img[stage][b->reg]:S.tex[stage][b->reg];
      vk_texture_prepare_binding(handle & 0xfffff,b->kind==4,vk_stage(stage));
    }
}
// Only immutable-content misses need the Vulkan update arrays. Keep their
// stack storage and driver calls out of the much more frequent binding/cache
// path; all resource resolution and dependencies remain in that original path.
static __attribute__((noinline)) VkDescriptorSet descriptor_create_selected(
    vk_shader *s,int dynamic,VkDescriptorSetLayout layout,const descriptor_value *values,
    descriptor_content *last,descriptor_history_entry **history,uint64_t hash,size_t key_bytes) {
  descriptor_payload payload[128];
  for(uint32_t n=0;n<s->nb;n++) {
    int kind=s->bindings[n].kind;
    const descriptor_value *value=&values[n];
    if(kind==0 || kind==1 || kind==2 || kind==8 || kind==VK_BINDING_TEXEL_STORAGE)
      payload[n].buffer=(VkDescriptorBufferInfo){
        .buffer=(VkBuffer)(uintptr_t)value->object,.offset=value->offset,.range=value->range};
    else if((kind>=3 && kind<=6) || kind==VK_BINDING_PIXEL_LOCAL)
      payload[n].image=(VkDescriptorImageInfo){
        .sampler=(VkSampler)(uintptr_t)value->offset,.imageView=(VkImageView)(uintptr_t)value->object,
        .imageLayout=(VkImageLayout)value->range};
    else payload[n].texel=(VkBufferView)(uintptr_t)value->object;
  }
  VkDescriptorSet set=vk_descriptor_allocate(layout);
  VkDescriptorUpdateTemplate update=s->descriptor_templates[dynamic];
  if(update && !atomic_load(&descriptor_template_reference)) {
    // Immutable schema/packing is compiled once. This set is fresh/private;
    // dependencies and full value equality above are identical to reference.
    vkUpdateDescriptorSetWithTemplate(V.device,set,update,payload);
  } else {
    VkWriteDescriptorSet writes[128];
    for(uint32_t n=0;n<s->nb;n++) {
      const vk_binding *b=&s->bindings[n];
      writes[n]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet=set,.dstBinding=b->binding,.descriptorCount=1,
        .descriptorType=dynamic && s->dynamic_uniform_ordinals[dynamic-1][n]?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:descriptor_type(b)};
      if(b->kind==0 || b->kind==1 || b->kind==2 || b->kind==8 || b->kind==VK_BINDING_TEXEL_STORAGE)writes[n].pBufferInfo=&payload[n].buffer;
      else if((b->kind>=3 && b->kind<=6) || b->kind==VK_BINDING_PIXEL_LOCAL)writes[n].pImageInfo=&payload[n].image;
      else writes[n].pTexelBufferView=&payload[n].texel;
    }
    vkUpdateDescriptorSets(V.device,s->nb,writes,0,NULL);
  }
  last->shader=s;last->layout=layout;last->set=set;memcpy(last->values,values,key_bytes);
  if(history) {
    descriptor_history_entry *entry=*history;
    if(!entry || entry->capacity<s->nb) {
      size_t old_bytes=entry?sizeof *entry+(size_t)entry->capacity*sizeof *entry->values:0;
      size_t bytes=sizeof *entry+key_bytes;
      descriptor_history_entry *grown=realloc(entry,bytes);
      if(!grown)die("Vulkan descriptor history allocation");
      entry=*history=grown;entry->capacity=s->nb;
      atomic_fetch_add(&descriptor_history_bytes,bytes-old_bytes);
    }
    entry->shader=s;entry->layout=layout;entry->set=set;
    entry->hash=hash;entry->epoch=binding_state->descriptor_history_epoch;
    memcpy(entry->values,values,key_bytes);
    descriptor_shader_hint_remember(s,layout,history);
  }
  shader_recorded.descriptor_content_misses += 1;
  return set;
}
static VkDescriptorSet shader_bindings_selected_impl(vk_shader *s,int dynamic,vk_uniform_offsets *offsets,
                                           const vk_texel_storage_selection *selection,const VkImageView *local_views,const VkImageLayout *local_layouts) {
  // The immutable dynamic schema assigns every ordinal 0..count-1 once.
  // Only that live prefix is observable; do not clear the unused array tail.
  if(offsets)offsets->count=0;
  if(dynamic && s->dynamic_uniform_count[dynamic-1] && !offsets)die("Vulkan missing dynamic Uniform offsets");
  VkDescriptorSetLayout layout=dynamic?s->dynamic_descriptors[dynamic-1]:s->descriptors;
  if(offsets && dynamic)offsets->count=s->dynamic_uniform_count[dynamic-1];
  // Graphics push packing reads 16 words from each of VS/FS. Their upper
  // halves have no consumer; compute and other stages keep the full reset.
  unsigned bias_stage=s->stage;
  uint32_t *bias_words=current_operation.storage_bias_words[bias_stage];
  memset(bias_words,0,16*sizeof *bias_words);
  if(bias_stage>=2)memset(bias_words+16,0,16*sizeof *bias_words);
  descriptor_value values[128];
  for (uint32_t n = 0; n < s->nb; n++) {
    const vk_binding *b = &s->bindings[n];
    if (b->count != 1)
      die("Vulkan descriptor arrays pending: %s binding %d", s->key,
          b->binding);
    VkDescriptorBufferInfo buffer = {0};
    VkBufferView texel = VK_NULL_HANDLE;
    VkDescriptorImageInfo image;
    if (b->kind == 0 || b->kind == 1) {
      vk_buffer *v = NULL;
      uint64_t offset = 0;
      if (b->kind == 1 && b->reg >= 3 && b->reg < 19) {
        const p_bind_buf *u = &S.ubo[s->stage][b->reg - 3];
        if(s->stage<5) vk_buffer_shader_read(u->addr,u->size,producer_stage(s->stage));
        if (u->addr && u->size &&
            vk_pool_address_stage(u->addr,u->size,(void **)&v,&offset,producer_stage(s->stage)) &&
            !(offset % V.props.limits.minUniformBufferOffsetAlignment)) {
          // 原 Metal 路径直接引用池内常量；符合 Vulkan 对齐时同样直接绑定，保留
          // GPU 更新顺序。 Like original Metal, directly bind pool uniforms
          // when Vulkan alignment permits it, preserving command-ordered GPU
          // updates.
          buffer.buffer = v->handle;
          buffer.offset = offset;
          buffer.range = u->size < 65536 ? u->size : 65536;
        }
      }
      if (!buffer.buffer) {
        v = constant(s, b->kind == 0 ? -1 : b->reg,&offset);
        buffer.buffer = v->handle;
        buffer.offset = offset;
        buffer.range = 65536;
      }
    } else if (b->kind == 2 || b->kind == 8) {
      if (b->flags & 1)
        current_operation.shader_buffer_writes |= producer_stage(s->stage);
      if (b->reg < 0 || b->reg >= 16)
        die("Vulkan storage slot %d", b->reg);
      p_bind_buf *u =
          b->kind == 8 ? &S.ubo[s->stage][b->reg] : &S.ssbo[s->stage][b->reg];
      if(s->stage<5 && !(b->flags&1))
        vk_buffer_shader_read(u->addr,u->size,producer_stage(s->stage));
      vk_buffer *v;
      uint64_t off;
      if (!u->addr || !u->size) {
        v = vk_temp_buffer(65536);
        memset(v->map, 0, 65536);
        off = 0;
        buffer.range = 65536;
      } else {
        if (!vk_pool_address_stage(u->addr,u->size,(void **)&v,&off,producer_stage(s->stage)))
          die("Vulkan storage outside pool");
        buffer.range = u->size;
        uint64_t prefix=off%V.props.limits.minStorageBufferOffsetAlignment;
        unsigned bias_slot=b->reg+(b->kind==8?16:0);
        int biased=prefix&&!(off%4)&&(s->storage_bias_mask&(1u<<bias_slot))&&
          !binding_state->storage_bias_reference&&
          u->size<=V.props.limits.maxStorageBufferRange&&
          prefix<=V.props.limits.maxStorageBufferRange-u->size;
        if(biased) {
          // Descriptor starts at the required alignment; the offline-proven
          // uint-array shader adds the prefix to each address. Logical writes
          // and CPU visibility still cover only the original NVN byte range.
          current_operation.storage_bias_words[s->stage][bias_slot]=prefix/4;
          off-=prefix;buffer.range+=prefix;
          shader_recorded.storage_direct += 1;
          if(b->flags&1) vk_buffer_writeback_stage(u->addr,u->size,producer_stage(s->stage));
        } else if (prefix) {
          // NVN 接受的子缓冲偏移可能小于 Vulkan 对齐要求；经 GPU
          // 暂存与拷回保持命令顺序。 NVN sub-buffer offsets can be smaller than
          // Vulkan alignment; GPU staging and copyback preserve command
          // ordering.
          unsigned index = 0;
          while (index < current_operation.storage_copy_count &&
                 (current_operation.storage_copies[index].address != u->addr ||
                  current_operation.storage_copies[index].size != u->size))
            index++;
          if (index == current_operation.storage_copy_count) {
            if (index == 128)
              die("Vulkan storage alignment copy capacity exceeded");
            vk_buffer *aligned = NULL;
            unsigned bucket =
                ((u->addr >> 4) ^ (u->addr >> 20) ^ u->size) % 1024;
            // 只读对齐副本跨绘制复用；GPU
            // 写代号改变后重拷，可写绑定仍逐绘制拷回。 Reuse read-only
            // alignment copies across draws. GPU writes invalidate them;
            // writable bindings still copy back per draw.
            if (!(b->flags & 1))
              for (constant_copy *copy = binding_state->readonly_storage_copies[bucket]; copy;
                   copy = copy->next)
                if (copy->address == u->addr && copy->size == u->size &&
                    copy->epoch == V.buffer_epoch) {
                  aligned = copy->buffer;
                  break;
                }
            if (!aligned) {
              vk_buffer *copy_source;uint64_t copy_offset;
              if(!vk_pool_address_stage(u->addr,u->size,(void **)&copy_source,&copy_offset,VK_PIPELINE_STAGE_TRANSFER_BIT))
                die("Vulkan storage copy outside pool");
              if(copy_source!=v || copy_offset!=off) die("Vulkan storage copy source changed");
              aligned = vk_temp_buffer(u->size);
              VkBufferCopy c = {.srcOffset = off, .size = u->size};
              vk_render_end();
              vk_buffer_shader_read(u->addr,u->size,VK_PIPELINE_STAGE_TRANSFER_BIT);
              vkCmdCopyBuffer(V.cmd, v->handle, aligned->handle, 1, &c);
              vk_buffer_transfer_written();
              shader_recorded.storage_copy_ops += 1;
              shader_recorded.storage_copy_bytes += c.size;
              if (!(b->flags & 1)) {
                constant_copy *copy = calloc(1, sizeof *copy);
                if (!copy)
                  die("Vulkan readonly storage cache allocation");
                copy->address = u->addr;
                copy->size = u->size;
                copy->epoch = V.buffer_epoch;
                copy->buffer = aligned;
                copy->next = binding_state->readonly_storage_copies[bucket];
                binding_state->readonly_storage_copies[bucket] = copy;
              }
            }
            current_operation.storage_copies[index].address = u->addr;
            current_operation.storage_copies[index].size = u->size;
            current_operation.storage_copies[index].offset = off;
            current_operation.storage_copies[index].original = v;
            current_operation.storage_copies[index].aligned = aligned;
            current_operation.storage_copies[index].writable = 0;
            current_operation.storage_copy_count++;
          }
          if (b->flags & 1)
            current_operation.storage_copies[index].writable = 1;
          v = current_operation.storage_copies[index].aligned;
          off = 0;
        } else if (b->flags & 1)
          vk_buffer_writeback_stage(u->addr,u->size,producer_stage(s->stage));
      }
      buffer.buffer = v->handle;
      buffer.offset = off;
    } else if (b->kind == 3 || b->kind == 4 || b->kind == 5 || b->kind == 6) {
      image=(VkDescriptorImageInfo){.imageLayout = VK_IMAGE_LAYOUT_GENERAL};
      // Image producers are owned by the image dependency batch. They do not
      // write guest buffer mirrors or invalidate cached buffer constants, and
      // must not force the buffer fallback's global post-draw barrier.
      int limit = b->kind == 4 ? 32 : 128;
      if (b->reg < 0 || b->reg >= limit)
        die("Vulkan texture slot unknown");
      uint64_t h = b->kind == 4   ? S.img[s->stage][b->reg]
                   : b->kind == 5 ? S.samp[s->stage][b->reg]
                                  : S.tex[s->stage][b->reg];
      if (b->kind != 5) {
        // Only plain 2D resources can belong to the bounded speculative
        // closure. Other original FS resources retain their existing views.
        int quad_plain=s->quad_readonly_sources && s->stage==1 &&
            (b->kind==3 || b->kind==6) && b->type==3;
        image.imageView = quad_plain ?
            vk_texture_view_binding_quad(h & 0xfffff,b->kind==4,vk_stage(s->stage),&image.imageLayout):
            vk_texture_view_binding(h & 0xfffff,b->kind==4,vk_stage(s->stage),&image.imageLayout);
      }
      if (b->kind == 3 || b->kind == 5) {
        int sid = b->kind == 3 && b->sampler >= 0
                      ? S.samp[s->stage][b->sampler] >> 20
                      : h >> 20;
        image.sampler = vk_sampler(sid);
      }
    } else if (b->kind==VK_BINDING_PIXEL_LOCAL) {
      if(!local_views || !local_layouts || b->sampler<0 || b->sampler>=VK_PIXEL_LOCAL_MAX_INPUTS || !local_views[b->sampler] ||
         (local_layouts[b->sampler]!=VK_IMAGE_LAYOUT_GENERAL && local_layouts[b->sampler]!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
        die("Vulkan missing proved pixel-local input");
      image=(VkDescriptorImageInfo){.imageView=local_views[b->sampler],.imageLayout=local_layouts[b->sampler]};
    } else if (b->kind==VK_BINDING_TEXEL_STORAGE) {
      buffer=texel_storage_descriptor(s,b,selection);
    } else if (b->kind == 7 || b->kind == 9) {
      if (b->kind == 9 && vk_binding_writable(b))
        current_operation.shader_buffer_writes |= producer_stage(s->stage);
      if (b->reg < 0 || b->reg >= (b->kind == 9 ? 32 : 128))
        die("Vulkan texel buffer slot %d", b->reg);
      uint64_t h =
          b->kind == 9 ? S.img[s->stage][b->reg] : S.tex[s->stage][b->reg];
      texel = vk_texel_buffer(h & 0xfffff,b->kind==9,vk_binding_writable(b),producer_stage(s->stage));
    } else
      die("Vulkan binding pending: %s kind=%d", s->key, b->kind);
    if (b->kind==0 || b->kind==1 || b->kind==2 || b->kind==8 || b->kind==VK_BINDING_TEXEL_STORAGE) {
      if(dynamic && s->dynamic_uniform_ordinals[dynamic-1][n]) {
        // Dependencies above use the original byte range. Only the descriptor
        // representation changes: aligned high bits stay immutable, low bits
        // are supplied again on every draw, including a descriptor cache hit.
        unsigned ordinal=s->dynamic_uniform_ordinals[dynamic-1][n]-1;
        offsets->offsets[ordinal]=(uint32_t)buffer.offset;
        buffer.offset &= ~UINT64_C(0xffffffff);
      }
      values[n]=(descriptor_value){(uint64_t)buffer.buffer,buffer.offset,buffer.range};
    }
    else if ((b->kind>=3 && b->kind<=6) || b->kind==VK_BINDING_PIXEL_LOCAL) {
      values[n]=(descriptor_value){(uint64_t)image.imageView,(uint64_t)image.sampler,image.imageLayout};
    }
    else {
      values[n]=(descriptor_value){(uint64_t)texel,0,0};
    }
  }
  // Resolve every descriptor and its dependencies before checking the final
  // immutable contents. Buffer data changes still take the normal upload /
  // writeback paths; the set only stores a handle, offset and range.
  descriptor_content *last=&binding_state->descriptor_last[s->stage];
  size_t key_bytes=s->nb*sizeof *values;
  if(!descriptor_content_reference() && last->shader==s && last->layout==layout &&
     !memcmp(last->values,values,key_bytes)) {
    shader_recorded.descriptor_content_hits += 1;return last->set;
  }
  descriptor_history_entry **history=NULL;
  uint64_t hash=0;
  if(!descriptor_content_reference() && !atomic_load(&descriptor_history_reference)) {
    // Keep the original stage-last HIT above completely unchanged. On its
    // MISS, shader/layout locality can avoid hashing every descriptor. This
    // is only a hint: slot
    // replacement/recycle and any resolved handle/offset/range change reject.
    descriptor_history_entry *hinted=descriptor_shader_hint_find(s,layout,values,key_bytes);
    if(hinted) {
#ifdef DM_VULKAN_TEST_EMBED
      descriptor_shader_hint_test_hits++;
#endif
      // Preserve the original stage-last locality as well: after A/B/A,
      // consecutive A draws must regain the cheapest unchanged first check.
      last->shader=s;last->layout=layout;last->set=hinted->set;memcpy(last->values,values,key_bytes);
      shader_recorded.descriptor_history_hits += 1;
      shader_recorded.descriptor_content_hits += 1;
      return hinted->set;
    }
    hash=descriptor_history_hash(s,layout,values);
    history=&binding_state->descriptor_history[hash&(DESCRIPTOR_HISTORY_SLOTS-1)];
    descriptor_history_entry *entry=*history;
    if(entry && entry->epoch==binding_state->descriptor_history_epoch &&
        entry->hash==hash && entry->shader==s && entry->layout==layout &&
        !memcmp(entry->values,values,key_bytes)) {
      last->shader=s;last->layout=layout;last->set=entry->set;memcpy(last->values,values,key_bytes);
      descriptor_shader_hint_remember(s,layout,history);
      shader_recorded.descriptor_history_hits += 1;
      shader_recorded.descriptor_content_hits += 1;return entry->set;
    }
    shader_recorded.descriptor_history_misses += 1;
  }
  // Only a true driver-set MISS needs Vulkan's update representation. The
  // immutable key above already contains every field, including the aligned
  // dynamic descriptor offset; cache hits must not build an unused second
  // 24-byte representation for each binding. Never defer resource work.
  return descriptor_create_selected(s,dynamic,layout,values,last,history,hash,key_bytes);
}
VkDescriptorSet vk_shader_bindings_selected(vk_shader *s,int dynamic,vk_uniform_offsets *offsets,
                                           const vk_texel_storage_selection *selection) {
  return shader_bindings_selected_impl(s,dynamic,offsets,selection,NULL,NULL);
}
VkDescriptorSet vk_shader_pixel_local_bindings(const vk_pixel_local_proof *proof,int dynamic,
    vk_uniform_offsets *offsets,const VkImageView views[VK_PIXEL_LOCAL_MAX_INPUTS],
    const VkImageLayout layouts[VK_PIXEL_LOCAL_MAX_INPUTS],const vk_texel_storage_selection *selection) {
  if(!proof || !proof->count || !proof->shader)die("Vulkan missing pixel-local descriptor proof");
  vk_texel_storage_selection local;
  if(selection) {
    if(selection->shader!=proof->source_shader)die("Vulkan pixel-local source schema changed");
    local=*selection;local.shader=proof->shader;selection=&local;
  }
  return shader_bindings_selected_impl(proof->shader,dynamic,offsets,selection,views,layouts);
}
VkDescriptorSet vk_shader_bindings_mode(vk_shader *s,int dynamic,vk_uniform_offsets *offsets) {
  return vk_shader_bindings_selected(s,dynamic,offsets,NULL);
}
VkDescriptorSet vk_shader_bindings(vk_shader *s) { return vk_shader_bindings_mode(s,0,NULL); }
VkPipelineLayout vk_program_layout_mode(vk_shader *const list[6],int dynamic) {
  if(!empty_layout)empty_layout=descriptor_layout_get(NULL,0);
  VkDescriptorSetLayout layouts[6];
  for(unsigned i=0;i<6;i++)layouts[i]=empty_layout;
  uint32_t count=0;int biased=0;
  for(unsigned i=0;i<6;i++)if(list[i]) {
    if(list[i]->set>=6)die("Vulkan descriptor set number");
    biased|=!!list[i]->storage_bias_mask;
    layouts[list[i]->set]=dynamic?list[i]->dynamic_descriptors[dynamic-1]:list[i]->descriptors;
    if(count<=list[i]->set)count=list[i]->set+1;
  }
  return pipeline_layout_get(layouts,count,biased);
}
VkPipelineLayout vk_program_layout(vk_shader *const list[6]) { return vk_program_layout_mode(list,0); }
void vk_shader_push_bias(vk_shader *const list[6],VkPipelineLayout layout,int compute,
                         vk_push_bias_state *previous) {
  int biased=0;
  for(unsigned i=0;i<6;i++) if(list[i]&&list[i]->storage_bias_mask) biased=1;
  if(!biased) { if(previous) previous->layout=VK_NULL_HANDLE;return; }
  uint32_t words[32]={0};
  if(compute) memcpy(words,current_operation.storage_bias_words[5],sizeof words);
  else {
    memcpy(words,current_operation.storage_bias_words[0],64);
    memcpy(words+16,current_operation.storage_bias_words[1],64);
  }
  if(previous && previous->layout==layout && !memcmp(previous->words,words,sizeof words)) return;
  vkCmdPushConstants(V.cmd,layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_COMPUTE_BIT,
                     0,sizeof words,words);
  if(previous) { previous->layout=layout;memcpy(previous->words,words,sizeof words); }
}
int vk_shader_prewarm_stage(vk_shader *s,VkShaderModule module,vk_prewarm_stage *out) {
  memset(out,0,sizeof *out);if(!s)return !module;
  vk_shader *base=shaders;while(base && base!=s)base=base->next;
  if(!base)return 0; // paired descriptor alternatives have a separate schema
  unsigned kind=module==s->module?1:module==s->specialization_module?2:
    s->predicate&&module==s->predicate->module?3:0;
  if(!kind || !module)return 0;
  memcpy(out->key,s->key,17);memcpy(out->fingerprint,s->fingerprint,32);out->module=kind;return 1;
}
vk_shader *vk_shader_prewarm_load(const vk_prewarm_stage *in,VkShaderModule *module) {
  *module=VK_NULL_HANDLE;
  if(in->module<1 || in->module>3 || in->key[16] || in->key[17] || in->key[18] || in->key[19])return NULL;
  vk_shader *loaded=shaders;
  while(loaded&&strcmp(loaded->key,in->key))loaded=loaded->next;
  uint8_t fingerprint[32];
  if(loaded)memcpy(fingerprint,loaded->fingerprint,32);
  else if(!vk_shader_fingerprint(in->key,fingerprint))return NULL;
  if(memcmp(fingerprint,in->fingerprint,32))return NULL;
  vk_shader *s=vk_shader_load_key(in->key);
  *module=in->module==1?s->module:in->module==2?s->specialization_module:s->predicate?s->predicate->module:VK_NULL_HANDLE;
  return *module?s:NULL;
}
static int compute_create(vk_shader *s,int optional) {
  if (!s->compute) {
    vk_shader *list[6] = {0};
    list[5] = s;
    s->compute_layout = vk_program_layout(list);
    VkComputePipelineCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                  .module = s->module,
                  .pName = "main"},
        .layout = s->compute_layout};
    int compile_trace=vk_system_trace_begin("Vulkan compute PSO creation");
    vk_pipeline_cache_begin();
    VkResult created = vkCreateComputePipelines(V.device,V.pipeline_cache,1,&ci,NULL,&s->compute);
    vk_pipeline_cache_end(created==VK_SUCCESS);
    vk_system_trace_end(compile_trace);
    if (created != VK_SUCCESS) {
      if(optional) { if(s->compute)vkDestroyPipeline(V.device,s->compute,NULL);s->compute=VK_NULL_HANDLE;return 0; }
      uint32_t blocks, allocations;
      uint64_t bytes;
      vk_memory_stats(&blocks, &allocations, &bytes);
      LOG("[vulkan compute failure] shader=%s result=%d stage=%u set=%u bindings=%u memory_blocks=%u allocations=%u bytes=%llu",
          s->key, created, s->stage, s->set, s->nb, blocks, allocations,
          (unsigned long long)bytes);
      // 校验失败时做相同输入的受控诊断；成功的重试也不继续执行游戏。
      // Controlled diagnostics after failure. Even successful retries do not
      // resume gameplay or silently turn into a runtime workaround.
      if (V.options.validate) {
        VkPipeline trial = VK_NULL_HANDLE;
        VkResult without_cache = vkCreateComputePipelines(V.device, VK_NULL_HANDLE, 1, &ci, NULL, &trial);
        if (trial) vkDestroyPipeline(V.device, trial, NULL);
        size_t code_size;
        uint32_t *code = read_cache(s->key, "spv", &code_size);
        VkShaderModuleCreateInfo module_info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                                .codeSize = code_size, .pCode = code};
        VkShaderModule fresh;
        VK_CHECK(vkCreateShaderModule(V.device, &module_info, NULL, &fresh));
        free(code);
        ci.stage.module = fresh;
        trial = VK_NULL_HANDLE;
        VkResult fresh_module = vkCreateComputePipelines(V.device, VK_NULL_HANDLE, 1, &ci, NULL, &trial);
        if (trial) vkDestroyPipeline(V.device, trial, NULL);
        VkPipelineLayout fresh_layout = vk_program_layout(list);
        ci.layout = fresh_layout;
        trial = VK_NULL_HANDLE;
        VkResult fresh_objects = vkCreateComputePipelines(V.device, VK_NULL_HANDLE, 1, &ci, NULL, &trial);
        if (trial) vkDestroyPipeline(V.device, trial, NULL);
        vkDestroyPipelineLayout(V.device, fresh_layout, NULL);
        vkDestroyShaderModule(V.device, fresh, NULL);
        LOG("[vulkan compute diagnostics] cache_off=%d fresh_module=%d fresh_module_and_layout=%d",
            without_cache, fresh_module, fresh_objects);
      }
      die("Vulkan compute pipeline %s creation failed: %d", s->key, created);
    }
  }
  return s->compute!=VK_NULL_HANDLE;
}
int vk_compute_prewarm(vk_shader *s) { return compute_create(s,1); }
static void dispatch_record(const p_dispatch *p) {
  if (!S.program)die("Vulkan dispatch without program");
  vk_shader *stages[6];vk_program_shaders(S.program,stages);vk_shader *s=stages[5];
  if(!s)die("Vulkan dispatch without compute shader");
  if(!s->compute) {
    compute_create(s,0);
    vk_prewarm_recipe recipe={.kind=2};
    if(vk_shader_prewarm_stage(s,s->module,&recipe.stages[5]))vk_prewarm_record(&recipe);
  }
  if(s->compute_warm==1){s->compute_warm=2;vk_prewarm_note_hit();}
  vk_texture_binding_scope_begin();
  vk_shader_prepare_images(stages,1);
  vk_shader_operation_begin();
  vk_texture_shader_begin();
  VkDescriptorSet set = vk_shader_bindings(s);
  vk_texture_binding_scope_end();
  vk_buffer_use uses[128];
  compute_pool_requirements requirements={.uses=uses};
  if(!vk_shader_pool_ranges(stages,1,compute_pool_requirement,&requirements))
    die("Vulkan incomplete compute pool requirements");
  unsigned use_count=requirements.count;
  // Query all buffer uses together after descriptor copies have been recorded,
  // before publishing this dispatch's producers. Images have their own batch.
  vk_compute_buffer_dependencies(uses,use_count,0);
  vk_texture_shader_barrier();
  unsigned gpu_region = vk_gpu_region_begin("compute", s->key, p->x, p->y);
  vkCmdBindPipeline(V.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s->compute);
  vk_shader_push_bias(stages,s->compute_layout,1,NULL);
  vkCmdBindDescriptorSets(V.cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                          s->compute_layout, s->set, 1, &set, 0, NULL);
  vkCmdDispatch(V.cmd, p->x, p->y, p->z);
  for(unsigned k=0;k<use_count;k++) if(uses[k].writable) {
    int copied=0;
    for(unsigned j=0;j<current_operation.storage_copy_count;j++)
      if(current_operation.storage_copies[j].address==uses[k].address &&
          current_operation.storage_copies[j].size==uses[k].size) copied=1;
    if(!copied) vk_buffer_shader_written(uses[k].address,uses[k].size,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  }
  current_operation.precise_compute_outputs=1;
  vk_texture_shader_record();
  vk_gpu_region_end(gpu_region);
  vk_shader_writebacks();
  V.draws++;
  vk_bright_lut(s);
}

static int dispatch_pool_collect(void *context,vk_pool_requirement_sink sink,void *out) {
  (void)context;vk_shader *stages[6]={0};if(!S.program)return 0;
  vk_program_shaders(S.program,stages);
  return vk_shader_pool_ranges_state(stages,&S,1,sink,out);
}
void vk_dispatch(const p_dispatch *p) {
  int cpu_preflight=V.options.pool_defer_publication;
#ifdef DM_VULKAN_SHARED_POOLS
  cpu_preflight|=vk_pool_shared_active() || vk_legacy_astc_copy_mode();
#endif
  if(cpu_preflight) {
    vk_shader *stages[6]={0};vk_program_shaders(S.program,stages);
    vk_shader_cpu_sources(stages,&S,1);
  }
  int token=vk_pool_operation_begin(dispatch_pool_collect,NULL);
  dispatch_record(p);vk_pool_operation_end(token);
}
