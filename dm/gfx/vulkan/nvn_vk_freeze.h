// 原世界冻结绘制的状态与只读诊断；不替代宿主冻结握手。
// Original frozen-world drawing state and readonly diagnostics, not the host handshake.
#pragma once
#include <stdint.h>
typedef struct {
  int state; // 0 正常 / 1 完整帧 / 2 冻结帧 | normal / full / frozen
  uint64_t full_frames, frozen_frames, skipped_targets, skipped_operations;
} vk_freeze_stats;
int vk_freeze_op(uint32_t op, const void *payload);
int vk_replay_preserves_ops(void);
void vk_freeze_snapshot(vk_freeze_stats *out);
void mtl_world_frozen(int on);
void mtl_rt_force_full(int frames);
