#pragma once
#include "nvn_vk_shader.h"
// Experimental descriptor delivery only. Single recording owner. Original
// resource preparation, hazards and lifetime must be satisfied by the caller.
#define VK_UAB_MAX_BINDINGS 128
#define VK_UAB_MAX_DRAWS 1024
#define VK_UAB_MAX_VERSIONS 3
typedef struct {
  uint32_t set,count;
  VkDescriptorSetLayoutBinding bindings[VK_UAB_MAX_BINDINGS];
  uint8_t dynamic_ordinal[VK_UAB_MAX_BINDINGS];
} vk_uab_schema;
typedef struct vk_uab_packet vk_uab_packet;
typedef struct vk_uab_batch vk_uab_batch;
// Recording-owner only, one explicit scope per graphics context. No new cache.
// Capacity is VkWriteDescriptorSet count (1..1024); payload storage never moves.
vk_uab_batch *vk_uab_batch_create(unsigned capacity);
int vk_uab_batch_begin(vk_uab_batch *);
void vk_uab_batch_flush(vk_uab_batch *);
int vk_uab_batch_end(vk_uab_batch *);
int vk_uab_batch_destroy(vk_uab_batch *); // refuses an active scope
int vk_uab_packet_write_batch(vk_uab_packet *,unsigned version,unsigned draw,
    const vk_split_descriptor_value *complete_values,vk_uab_batch *);

int vk_uab_schema_from_shader(const vk_shader *,int dynamic,vk_uab_schema *);
int vk_uab_normalize_values(const vk_uab_schema *,const vk_split_descriptor_value *,
                           const vk_uniform_offsets *,vk_split_descriptor_value *);
vk_uab_packet *vk_uab_packet_create(const vk_uab_schema *,uint32_t draws,uint32_t versions);
VkDescriptorSetLayout vk_uab_packet_layout(const vk_uab_packet *);
// Slots indexed by original SPIR-V set number; NULL holes get an empty layout.
// Explicit-push helper returns caller-owned pipeline layout. Set layouts borrow
// the original device-lifetime intern. Game PSOs use vk_program_uab_layout.
int vk_uab_pipeline_layout_create(vk_uab_packet *const sets[6],
    const VkPushConstantRange *push,uint32_t push_count,VkPipelineLayout *out);
// A returned index is exclusively writable; -1 means busy/unavailable, never wait.
int vk_uab_packet_begin(vk_uab_packet *);
// Each editing slot may be written once; duplicate touched slots are refused.
// 1=written, 0=invalid/immutable change. Failure does not partially update a set.
int vk_uab_packet_write(vk_uab_packet *,unsigned version,unsigned draw,
                        const vk_split_descriptor_value *complete_values);
// A nonempty partial block can seal; untouched sets remain inaccessible.
int vk_uab_packet_seal(vk_uab_packet *,unsigned version);
// Editing/sealed access only, and only a fully initialized, touched slot.
// First publication flushes the shared batch before exposing immutable bindings.
// Warm UAB-only updates may remain queued: flush before consumer submission.
// pending/abandon/destroy also flush this packet's outstanding batch references.
VkDescriptorSet vk_uab_packet_set(const vk_uab_packet *,unsigned version,unsigned draw);
// Mark the actual original recording serial before delivery; resources and the
// persistent CB must remain valid through its original GPU+CPU-effect completion.
int vk_uab_packet_pending(vk_uab_packet *,unsigned version,uint64_t recording_serial);
int vk_uab_packet_abandon(vk_uab_packet *,unsigned version);
// Refuses pending versions; caller first retires CB references to these sets.
// Descriptor set layouts remain in the original device-lifetime intern.
int vk_uab_packet_destroy(vk_uab_packet *);
