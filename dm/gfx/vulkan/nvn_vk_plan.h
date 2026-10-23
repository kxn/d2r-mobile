// 原 Metal pass_ds_need/ds_scan 的提交顺序前瞻，不解释游戏常量。
// Submission-order counterpart of upstream pass_ds_need/ds_scan; no game data semantics.
#pragma once
#include "nvn_vk_internal.h"
void vk_pass_plan_op(const uint8_t *op);
// After the original first state/checkpoint, consume a validated pure tail.
// Caller proves all per-op observers inactive and no nested resource operation.
unsigned vk_pass_state_segment(const uint8_t **next,unsigned guard_left);
#if defined(DM_VULKAN_TEST_EMBED) && !defined(DM_ANDROID_APP)
void vk_test_state_segment_stats(uint64_t *segments,uint64_t *operations);
#endif
unsigned vk_pass_depth_need(void);
unsigned vk_pass_depth_writes(void);
int vk_pass_clear_without_draws(void);
uint64_t vk_render_clear_attachment_omissions(void);
void vk_render_clear_reference(int reference);
int vk_render_clear_reference_mode(void);
uint64_t vk_render_depth_omissions(void);
void vk_pass_plan_done(const void *payload);
int vk_pass_store_needed(const tex_t *texture,const view_t *view,VkImageAspectFlagBits plane);
uint64_t vk_render_store_omissions(void);
uint64_t vk_render_load_omissions(void);
typedef struct {
  uint64_t builds,indexed,reference,reference_ops,query_events,comparisons,mismatches,fallbacks;
} vk_pass_index_stats;
void vk_pass_index_snapshot(vk_pass_index_stats *out);
// Recording-owner diagnostic publication; no resource or GPU synchronization.
void vk_pass_stats_publish(void);
void vk_store_descriptor_reference(int reference);
void vk_store_descriptor_stats(uint64_t *hits,uint64_t *misses,int *reference);
void vk_store_slot_stats(uint64_t *stages,uint64_t *skipped);
void vk_store_event_stats(uint64_t *recorded,uint64_t *duplicates);

void vk_sample_liveness_stats(uint64_t *live,uint64_t *dead);
void vk_sample_liveness_reference(int reference);
void vk_sample_query_elision_stats(uint64_t *elided,int *reference);
int vk_pass_samples_live(const void *payload);

// Read-only bounded lookahead; offset zero is the operation being replayed.
int vk_pass_future_op(unsigned offset,const uint8_t **op,int *samples_live);
uint64_t vk_pass_generation(void);

// The complete clear-only interlude ends at an exact restoration of original
// targets. All payloads are immutable command-snapshot values.
typedef struct { const uint8_t *op; p_set_rt targets; p_scissor scissor;
  p_clear_color clear; int has_scissor; } vk_mip_clear_record;
typedef struct { const uint8_t *ops[256]; unsigned op_count,record_count;
  vk_mip_clear_record records[4]; uint64_t generation; } vk_mip_clear_plan;
int vk_pass_mip_clear_plan(const p_set_rt *original,const p_set_rt *current,
                           const p_scissor *sc,vk_mip_clear_plan *out);
