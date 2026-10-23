// Private retained-secondary lifetime. Current draw preparation must still
// prove and pin every physical resource on each use; this owns recordings only.
#pragma once
#include "nvn_vk_internal.h"
typedef struct vk_record_owner vk_record_owner;
enum { VK_RECORD_OWNER_MAX_SLOTS=64 };
vk_record_owner *vk_record_owner_create(unsigned slots);
void vk_record_owner_destroy(vk_record_owner *owner);
int vk_record_owner_available(const vk_record_owner *owner);
// Begin/reset is refused while this original submission context is in flight.
VkCommandBuffer vk_record_owner_begin(vk_record_owner *owner,unsigned slot,
                                     const VkCommandBufferInheritanceInfo *inherit);
// Zero: physical retirement during capture; never reuse that recording.
int vk_record_owner_end(vk_record_owner *owner,unsigned slot);
VkCommandBuffer vk_record_owner_cached(vk_record_owner *owner,unsigned slot);
// Only after inclusion in a real submission, with vk_recording_serial().
void vk_record_owner_pending(vk_record_owner *owner,uint64_t queue_serial);
// Sole production caller is the original graphics-recycle boundary, after
// its real fence, CPU effects and original command-pool reset have completed.
void vk_record_owner_reclaim(vk_record_owner *owner,uint64_t queue_serial);
// Enabled only by creating an experimental owner. No per-draw object registry.
uint64_t vk_record_object_epoch(void);
