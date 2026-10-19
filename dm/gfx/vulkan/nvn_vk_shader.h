// 原着色器控制段哈希查找离线 SPIR-V 与固定格式反射。
// Look up offline SPIR-V and fixed-format reflection using original shader
// control hashes.
#pragma once
#include "nvn_vk_metadata.h"
#include "nvn_vk_state.h"
#include "nvn_vk_prewarm.h"
#include "nvn_vk_pool_ranges.h"
enum { VK_UNIFORM_OFFSET_CAPACITY=16 };
// Optional immutable second-word predicate recipe. Mutable admitted tuples are
// owned by the sole recording thread; a paired descriptor alternative never
// inherits this module because its descriptor schema/module are different.
typedef struct {
  VkShaderModule module;
  uint32_t slot,offset,id,count;
  struct { uint32_t op,and_mask,compare,bit; } predicates[4];
  uint32_t values[16][2],variants;
  int disabled;
} vk_uniform_predicate;
// Optional complete static read-set proof, immutable for shader lifetime.
typedef struct vk_driver_schema {
  uint32_t count;
  uint16_t words[]; // indices into the existing compact full key
} vk_driver_schema;
typedef struct vk_shader {
  struct vk_shader *next;
  struct vk_shader *texel_storage_alternative;
  char key[17];
  uint8_t fingerprint[32];
  uint32_t stage, set, nb, nc1, local[3], input_mask;
  int pos_attr, pos_w1, may_discard, writes_smask;
  uint32_t color_mask;
  uint32_t storage_bias_mask;
  vk_binding *bindings;
  uint32_t image_read_count,storage_descriptor_count;
  int *image_read_slots;
  uint8_t *c1;
  vk_buffer c1_buffer;
  VkShaderModule module;
  VkShaderModule specialization_module;
  vk_uniform_predicate *predicate;
  uint32_t specialization_slot,specialization_offset,specialization_id;
  uint32_t specialization_values[16],specialization_count;
  int specialization_integer_only,specialization_disabled,has_resource_writes;
  int has_writable_buffers; // immutable buffer-only classification; excludes image writes
  vk_driver_schema *driver_schema[2]; // c0 / c2, NULL keeps exact full key
  VkDescriptorSetLayout descriptors,dynamic_descriptors[2];
  VkDescriptorUpdateTemplate descriptor_templates[3];
  uint8_t dynamic_uniform_ordinals[2][128]; // narrow/extended, one-based binding order
  uint32_t dynamic_uniform_count[2];
  VkPipelineLayout compute_layout;
  VkPipeline compute;
  int compute_warm;
  int texel_storage_checked;
} vk_shader;
enum { VK_TEXEL_STORAGE_BINDINGS=16 };
typedef struct {
  vk_shader *shader;
  uint32_t count;
  struct { uint32_t binding; uint64_t address,size; } reads[VK_TEXEL_STORAGE_BINDINGS];
} vk_texel_storage_selection;
// Actual draw/dispatch state only; never called by future-command planners.
vk_shader *vk_shader_texel_pick(vk_shader *base,uint32_t extra_storage_budget,vk_texel_storage_selection *selection);
void vk_texel_storage_enabled(int enabled);
void vk_texel_storage_stats(uint64_t *selected,uint64_t *fallback);
vk_shader *vk_shader_get(const void *control);
vk_shader *vk_shader_load_key(const char *key);
int vk_shader_fingerprint(const char *key,uint8_t digest[32]);
int vk_shader_prewarm_stage(vk_shader *shader,VkShaderModule module,vk_prewarm_stage *stage);
vk_shader *vk_shader_prewarm_load(const vk_prewarm_stage *stage,VkShaderModule *module);
int vk_compute_prewarm(vk_shader *shader);
void vk_program_shaders(const prog_t *program, vk_shader *shaders[6]);
// 仅提交线程：begin 取得已回收的一份，end 交给在途提交；recycle 只在完成后调用。
// Submit thread only: begin takes a recycled set, end hands it to the in-flight
// submission, and recycle runs only after GPU work and CPU effects complete.
typedef struct vk_shader_submission vk_shader_submission;
vk_shader_submission *vk_shader_begin(vk_shader_submission *recycled);
void vk_shader_end(vk_shader_submission *submission);
void vk_shader_recycle(vk_shader_submission *submission);
void vk_shader_destroy(vk_shader_submission *submission);
typedef struct { uint32_t count,offsets[VK_UNIFORM_OFFSET_CAPACITY]; } vk_uniform_offsets;
VkDescriptorSet vk_shader_bindings_selected(vk_shader *s,int dynamic,vk_uniform_offsets *offsets,
                                           const vk_texel_storage_selection *selection);
void vk_descriptor_template_reference(int reference);
int vk_descriptor_template_reference_mode(void);
void vk_descriptor_history_reference(int reference);
void vk_descriptor_history_stats(uint64_t *hits,uint64_t *misses,uint64_t *bytes,int *reference);
void vk_descriptor_history_test_hash(int force);
void vk_descriptor_reuse_reference(int reference);
void vk_descriptor_reuse_stats(uint64_t *hits,uint64_t *resets,int *reference);
void vk_descriptor_batch_reference(int reference);
void vk_descriptor_batch_stats(uint64_t *calls,uint64_t *sets,int *reference);
void vk_dynamic_uniform_reference(int reference);
int vk_dynamic_uniform_reference_mode(void);
void vk_dynamic_uniform_narrow_reference(int reference);
int vk_dynamic_uniform_narrow_reference_mode(void);
int vk_program_dynamic_uniforms(vk_shader *const shaders[6]);
VkDescriptorSet vk_shader_bindings_mode(vk_shader *s,int dynamic,vk_uniform_offsets *offsets);
VkPipelineLayout vk_program_layout_mode(vk_shader *const shaders[6],int dynamic);
VkDescriptorSet vk_shader_bindings(vk_shader *s);
void vk_shader_operation_begin(void);
void vk_shader_prepare_images(vk_shader *const shaders[6],int compute);
int vk_shader_pool_ranges(vk_shader *const shaders[6],int compute,
    vk_pool_requirement_sink sink,void *context);
int vk_shader_pool_ranges_state(vk_shader *const shaders[6],const vk_state *state,int compute,
    vk_pool_requirement_sink sink,void *context);
// Metadata proof only; false selects conservative whole-pool handling and
// discards partial output. Zero-length mandatory draw bindings are not proven.
int vk_graphics_pool_ranges(vk_shader *const shaders[6],const vk_state *state,
    const p_draw_idx *indexed,vk_pool_requirement_sink sink,void *context);
void vk_texture_prepare_binding(int id,int storage,VkShaderStageFlagBits stage);
typedef struct {
  VkPipelineLayout layout;
  uint32_t words[32];
} vk_push_bias_state;
void vk_shader_push_bias(vk_shader *const shaders[6],VkPipelineLayout layout,int compute,
                         vk_push_bias_state *previous);
void vk_storage_bias_reference(int reference);
void vk_storage_usage_reference(int reference);
void vk_attachment_usage_reference(int reference);
void vk_depth_layout_reference(int reference);
void vk_compute_layout_reference(int reference);
void vk_readonly_depth_reference(int reference);
void vk_texture_prepare_attachment(const tex_t *texture,const view_t *view);
void vk_driver_constant_reference(int reference);
#ifdef DM_VULKAN_TEST_EMBED
unsigned vk_driver_schema_words(const vk_shader *s,unsigned slot);
#endif
void vk_driver_constant_stats(uint64_t *last,uint64_t *content,uint64_t *misses,int *reference);
void vk_driver_arena_stats(uint64_t *slots,uint64_t *zeroes,uint64_t *chunks,
                          uint64_t *live,uint64_t *reuses);
void vk_descriptor_content_stats(uint64_t *hits,uint64_t *misses);
void vk_storage_bias_stats(uint64_t *direct,uint64_t *copies,uint64_t *bytes,int *reference);
VkDescriptorSet vk_descriptor_allocate(VkDescriptorSetLayout layout);
VkShaderStageFlagBits vk_stage(uint32_t stage);
void vk_ghost_note(const p_draw *draw, vk_shader *fragment);
int vk_ghost_pool_ranges(vk_pool_requirement_sink sink,void *context);
VkPipelineLayout vk_program_layout(vk_shader *const shaders[6]);
VkImageView vk_texture_view(int id, int storage);
int vk_texture_descriptor(int id,tex_t *out);
int vk_texture_descriptor_view(int id,tex_t *out,view_t *view,int *has_view);
void vk_bloom_quarter_enabled(int enabled);
void vk_bloom_quarter_clears(uint64_t *before_a,uint64_t *before_b1);
void vk_bloom_quarter_blit_stats(uint64_t *blits,uint64_t *query_fallbacks,uint64_t *resource_fallbacks);
void vk_bloom_quarter_stats(uint64_t *chains,uint64_t *skipped,uint64_t *replaced,uint64_t *redirected,uint64_t *fallbacks,int *enabled);
void vk_bloom_quarter_test_facts(const char *keys[4]);
void vk_bloom_quarter_test_blit_format(VkFormat format);
uint64_t vk_texture_descriptor_epoch(void);
typedef struct {
  VkImage image;
  VkImageView view;
  VkFormat format;
  VkExtent3D extent;
  VkImageSubresourceLayers subresource;
  uint64_t serial;
} vk_storage_image;
int vk_texture_storage_volume(int id, vk_storage_image *out);
void vk_texture_storage_access(const vk_storage_image *image,VkPipelineStageFlags stages,VkAccessFlags access);
void vk_texture_storage_written(const vk_storage_image *image,VkPipelineStageFlags producer);
int vk_bright_apply(int id, float exposure);
void vk_bright_lut(vk_shader *shader);
VkSampler vk_sampler(int id);
int vk_sampler_descriptor(int id,samp_par *out);
void vk_texture_end_submit(void);
typedef struct vk_texture_submission vk_texture_submission;
vk_texture_submission *vk_texture_begin(vk_texture_submission *recycled);
void vk_texture_end(vk_texture_submission *submission);
void vk_texture_recycle(vk_texture_submission *submission);
void vk_texture_destroy(vk_texture_submission *submission);
void vk_texel_view_stats(uint64_t *hits,uint64_t *creates);
void vk_texture_dirty_stats(uint64_t *hits,uint64_t *checks);
void vk_texture_dirty_reference(int reference);
int vk_texture_dirty_reference_mode(void);
// Proven zero stored/sample components: RGB=7, RGBA=15, unknown=0. Missing
// sampled alpha is never zero. Every physical writer retires these facts.
int vk_texture_zero_target(const tex_t *texture,const view_t *view);
int vk_texture_zero_descriptor(int texture_id,int sampler_id);
uint64_t vk_texture_zero_copy_skipped(void);
uint64_t vk_zero_draw_skipped(void);
int vk_zero_draw_reference_mode(void);
void vk_zero_draw_proof_stats(uint64_t *attempts,uint64_t reasons[7]);
void vk_zero_draw_reference(int reference);
void vk_zero_draw_test_fact(const char *key);
void vk_nofxaa_enabled(int enabled);
void vk_nofxaa_stats(uint64_t *replacements,int *enabled);
void vk_nofxaa_test_fact(const char *key);
void vk_texture_own_buffer_view(VkBufferView view);
typedef struct {
  VkImageView view;
  VkFormat format;
  VkExtent3D extent;
  VkSampleCountFlagBits samples;
  VkImageLayout layout;
} vk_attachment;
vk_attachment vk_texture_attachment(const tex_t *t, const view_t *v);
vk_attachment vk_texture_readonly_attachment(const tex_t *t,const view_t *v);
VkImageView vk_texture_view_binding(int id,int storage,VkShaderStageFlagBits stage,VkImageLayout *layout);
void vk_texture_shader_begin(void);
void vk_texture_shader_barrier(void);
void vk_texture_graphics_barrier(void);
void vk_texture_pass_begin(void);
VkCommandBuffer vk_render_pending_primary(void);
void vk_render_view_retired(VkImageView view);
int vk_texture_shader_collecting(void);
void vk_texture_shader_record(void);
void vk_texture_attachment_record(const tex_t *texture,const view_t *view,VkImageAspectFlags aspect);
void vk_texture_attachment_read(const tex_t *texture,const view_t *view);
void vk_texture_attachment_store(const tex_t *texture,const view_t *view,int preserve_zero);
void vk_texture_targets_barrier(const p_set_rt *targets,int include_depth);
typedef struct vk_graphics_submission vk_graphics_submission;
vk_graphics_submission *vk_graphics_begin(vk_graphics_submission *recycled);
void vk_graphics_end(vk_graphics_submission *submission);
void vk_graphics_recycle(vk_graphics_submission *submission);
void vk_defer_pass(VkRenderPass pass, VkFramebuffer framebuffer);

void vk_shader_writebacks(void);

VkBufferView vk_texel_buffer(int id,int storage,int writable,VkPipelineStageFlags stages);

void vk_uniform_specialization_enabled(int enabled);
void vk_uniform_specialization_stats(uint64_t *hits,uint64_t *pending,uint64_t *limits,uint64_t *variants,int *enabled);
VkShaderModule vk_uniform_specialization_pick(vk_shader *shader,uint32_t *value);
void vk_uniform_predicate_enabled(int enabled);
void vk_uniform_predicate_stats(uint64_t *hits,uint64_t *pending,uint64_t *limits,
                               uint64_t *variants,uint64_t *rejected,uint64_t masks[16],int *enabled);
VkShaderModule vk_uniform_hybrid_pick(vk_shader *shader,uint32_t values[2]);
VkShaderModule vk_uniform_predicate_rejected(vk_shader *shader,uint32_t values[2]);
