// Android Surface 与原 NVN 出图的桥接，不涉及客体 CPU 执行。
// Bridge Android Surface to original NVN presentation without guest CPU
// execution.
#pragma once
#include <stdint.h>
// Startup-only. Rendering/compute/transfer remain on the original NVN queue;
// the optional second queue is only for the WSI worker's presentation waits.
// 0: inline reference; 1: worker with second queue; 2: worker on render queue.
// Mode 2 isolates host-thread overlap from a change in the Vulkan queue graph.
void vk_window_mailbox_present(int enabled);
void vk_window_presentation_mode_stats(int *requested,int *actual,unsigned *supported);
void vk_window_present_worker_mode(int mode);
// Reserve optional swapchain attachment usage on the first creation; start
// enabled for same-process ON/OFF comparisons. Unsupported cases still blit.
void vk_window_shader_present(int enabled);
void vk_window_shader_present_stats(uint64_t *selected,uint64_t *fallback,int *requested,int *available);
int vk_window_present_worker_requested_mode(void);
void vk_window_present_worker_stats(uint64_t *queued,uint64_t *finished,int *active,int *reference);
void vk_android_window(void *window, float refresh);
int vk_window_attached(void);
double vk_window_refresh(void);
double vk_window_presented_time(uint64_t sequence);
int vk_window_world_point(float x, float y, float *world_x, float *world_y);
int vk_window_surface_point(float world_x, float world_y, float *x, float *y);
void vk_window_surface_create(void);
void vk_window_output_sizes(uint32_t *game_w, uint32_t *game_h,
                            uint32_t *surface_w, uint32_t *surface_h);
uint64_t vk_window_begin_present(void);
void vk_window_drop_present(uint64_t sequence);
uint64_t vk_window_present(const void *texture, const int crop[4], uint64_t sequence);
void vk_window_discard(int on);
int vk_window_discarding(void);

// Application-only debug ceilings; zero follows the existing game policy.
void vk_set_manual_limit(int limit);
int vk_manual_limit(void);
int vk_thermal_limit(void);
int vk_requested_fps(void);
