// 原 OBS_TICK 观测与原 diag.c 共用玩家读取；只读取真实游戏，不模拟状态。
// Observe original OBS_TICK with shared diag.c player reads, never simulation.
#include "../../gfx/vulkan/nvn_vk_window.h"
#include "../../gfx/vulkan/nvn_vk_freeze.h"
#include "../../gfx/vulkan/nvn_vk_pool_copy_stats.h"
#include "dm_guest_heap.h"
#include "../../gfx/vulkan/nvn_vk_extrap.h"
#include "../../gfx/vulkan/nvn_vk_pixel_local_runtime.h"
#include "dm.h"
#include "opus_shadow.h"
#include "dm_guest_player.h"
#ifdef DM_NATIVE_FEATURES
#include "dm_hud.h"
#include "dm_actions.h"
#include "../../ios/Sources/dm_touch_game_state.h"
#endif
#include "dm_world.h"
#include "platform.h"
#include "dm_performance_hint.h"
#include <stdatomic.h>
static uint8_t *main_base;
static FILE *state_log;
static _Atomic uint64_t current_path;
static _Atomic int player_valid;
static _Atomic uint64_t current_game;
static _Atomic uint32_t current_area;
static _Atomic int64_t present_interval_us;
static _Atomic uint64_t present_sample_ns;
static _Atomic int present_fps_tenths;
// Display-only aggregation of existing driver intervals, not a profiler.
static double overlay_interval_sum;
static unsigned overlay_interval_count;
// 保留原帧间隔 -> 3D 控制器路径；没有真实上屏时刻时传入 0。
// Retain the original presentation-interval -> 3D controller path; unavailable
// real presentation timestamps feed zero rather than a host-time estimate.
void dm_android_record_present(double interval) {
  uint64_t now = now_ns(), last = atomic_load(&present_sample_ns);
  if (last && now - last > 2000000000ull) {
    overlay_interval_sum = 0;
    overlay_interval_count = 0;
    atomic_store(&present_fps_tenths, 0);
  }
  atomic_store(&present_interval_us, (int64_t)(interval * 1000));
  if (interval > 0) {
    overlay_interval_sum += interval;
    ++overlay_interval_count;
    if (overlay_interval_sum >= 1000) {
      atomic_store(&present_fps_tenths,
                   (int)(overlay_interval_count * 10000.0 / overlay_interval_sum + .5));
      overlay_interval_sum = 0;
      overlay_interval_count = 0;
    }
    atomic_store(&present_sample_ns, now);
  }
#ifndef DM_NATIVE_FEATURES
  dm_3d_scale_feed(interval);
#endif
}
float dm_android_present_fps(uint64_t now) {
  uint64_t last = atomic_load(&present_sample_ns);
  return last && now >= last && now - last < 2000000000ull
      ? atomic_load(&present_fps_tenths) / 10.f : 0;
}
static int read_guest(uint64_t address, void *out, size_t size) {
  if (!address)
    return 0;
  extern int dm_guest_heap_readable(uint64_t, size_t);
  if (dm_guest_heap_readable(address, size)) {
    memcpy(out, (const void *)(uintptr_t)address, size);
    return 1;
  }
  return dm_self_read(out, (const void *)(uintptr_t)address, size) ==
         (long)size;
}
#ifndef DM_NATIVE_FEATURES
void dm_perf_present(double interval) { dm_android_record_present(interval); }
uint32_t dm_cur_tick(void) {
  uint32_t tick = 0;
  uint64_t game = atomic_load(&current_game);
  if (game && dm_ingame_now())
    read_guest(game + 0x170, &tick, 4);
  return tick;
}
#endif
void dm_native_world_snapshot(int *area, int *valid) {
  *valid = dm_ingame_now();
  *area = *valid ? (int)atomic_load(&current_area) : 0;
}
#ifndef DM_NATIVE_FEATURES
int dm_ingame_now(void) {
  if (!atomic_load(&player_valid))
    return 0;
  // 原 game_live 的会话槽检查，退出后不能保留旧玩家。
  // Original game_live session-slot check prevents stale players after quit.
  uint64_t slots[0x400];
  if (!main_base ||
      !read_guest((uint64_t)main_base + 0x2d665d8, slots, sizeof slots))
    return 0;
  for (unsigned i = 0; i < 0x400; i++)
    if (slots[i] + 1 >= 2)
      return 1;
  return 0;
}
int dm_diag_player_fixed(uint32_t *x, uint32_t *y) {
  uint32_t position[2];
  uint64_t path = atomic_load(&current_path);
  if (!dm_ingame_now() || !path || !read_guest(path, position, sizeof position))
    return 0;
  *x = position[0];
  *y = position[1];
  return 1;
}
#endif
static void observe_tick(const uint64_t *registers) {
  uint64_t game = registers[0];
  uint64_t player = dm_guest_player_unit((uint64_t)main_base, game, read_guest);
  uint64_t inventory = 0, path = 0;
  uint32_t signature = 0, x = 0, y = 0, area = 0, tick = 0, mode = 0;
  int valid =
      player && read_guest(player + 0x90, &inventory, 8) && inventory &&
      read_guest(inventory, &signature, 4) && signature == 0x01020304 &&
      dm_guest_player_location(player, read_guest, &x, &y, &area, &path);
  read_guest(game + 0x170, &tick, 4);
  if (player)
    read_guest(player + 0xc, &mode, 4);
  atomic_store(&current_path, valid ? path : 0);
  atomic_store(&current_game, game);
  atomic_store(&current_area, area);
  extern _Atomic int g_dm_game_ver;
  uint8_t version;
  if (read_guest(game + 0x106, &version, 1))
    atomic_store(&g_dm_game_ver, version);
  atomic_store(&player_valid, valid);
#if defined(DM_ANDROID_APP) && !defined(DM_NATIVE_FEATURES)
  // 与原 diag.c 相同，在服务端拍执行原回城模块，不能在 ART/UI 线程调用游戏。
  // Run the original portal module on its server tick, as diag.c does.
  void dm_freetp_server_step(uint8_t *, uint64_t, uint64_t);
  dm_freetp_server_step(main_base, game, valid ? player : 0);
#endif
  // 沿原 hud_tick，在服务端拍调用原属性函数，包含装备累计层。
  // Like original hud_tick, call the accumulated-stat getter on the server
  // tick.
  int level = 0, experience = 0, hp = 0, hp_max = 0, mp = 0, mp_max = 0, gold = 0;
  if (valid) {
    int (*get)(uint64_t, int, int) = (void *)(main_base + 0x5b9290);
    level = get(player, 12, 0);
    experience = get(player, 13, 0);
    gold = get(player, 14, 0);
    hp = get(player, 6, 0) >> 8;
    hp_max = get(player, 7, 0) >> 8;
    mp = get(player, 8, 0) >> 8;
    mp_max = get(player, 9, 0) >> 8;
  }
  static uint64_t last_log;
  static uint32_t last_area;
  uint64_t now = now_ns();
  if (now - last_log < 1000000000ull && area == last_area)
    return;
  last_log = now;
  last_area = area;
  if (state_log) {
    float watch_x = 0, watch_y = 0, source_x, source_y;
    int watch_cell_x = 0, watch_cell_y = 0;
    int near_area = 0, near_x = 0, near_y = 0;
    if (valid)
      dm_world_near_level(&near_area, &near_x, &near_y);
    extern int dm_android_world_input_ready(void) __attribute__((weak));
    int client_area = dm_world_client_area();
    // 服务端先换图，客户端可能仍在旧场景；不能将旧导航读数当新图可操作。
    // Server transitions precede the client scene. Old navigation readiness
    // must not accept input while the two original level reads disagree.
    int world_ready = valid && client_area == (int)area &&
                      dm_android_world_input_ready &&
                      dm_android_world_input_ready();
    int found = valid && dm_world_watch_cells(&watch_cell_x, &watch_cell_y);
    int watched =
        found && dm_world_watch_pos(&source_x, &source_y) &&
        vk_window_surface_point(source_x, source_y, &watch_x, &watch_y);
    uint32_t game_w, game_h, surface_w, surface_h;
    vk_window_output_sizes(&game_w, &game_h, &surface_w, &surface_h);
    fprintf(
        state_log,
        "{\"time_ns\":%llu,\"tick\":%u,\"valid\":%s,\"area\":%u,"
        "\"x\":%u,\"y\":%u,\"mode\":%u,\"level\":%d,\"experience\":%d,"
        "\"hp\":%d,\"hp_max\":%d,\"mp\":%d,\"mp_max\":%d,"
        "\"present_interval_ms\":%.3f,\"present_sample_ns\":%llu,\"scale_3d\":%.2f,"
        "\"world_input_ready\":%s,\"watch_found\":%s,\"watch_visible\":%s,"
        "\"watch_x\":%.6f,"
        "\"watch_y\":%.6f,"
        "\"watch_cell_x\":%d,\"watch_cell_y\":%d,"
        "\"near_area\":%d,\"near_x\":%d,\"near_y\":%d,\"client_area\":%d",
        (unsigned long long)now, tick, valid ? "true" : "false", area, x, y,
        mode, level, experience, hp, hp_max, mp, mp_max,
        atomic_load(&present_interval_us) / 1000.0,
        (unsigned long long)atomic_load(&present_sample_ns), dm_3d_scale_cur(),
        world_ready ? "true" : "false", found ? "true" : "false",
        watched ? "true" : "false", watch_x, watch_y, watch_cell_x,
        watch_cell_y, near_area, near_x, near_y, client_area);
    fprintf(state_log, ",\"game_output\":{\"width\":%u,\"height\":%u},"
                       "\"surface_output\":{\"width\":%u,\"height\":%u}",
            game_w, game_h, surface_w, surface_h);
#ifdef DM_NATIVE_FEATURES
    int mtl_fps_target(void); double mtl_pace_hz(void);
    uint64_t dm_nvn_present_period_ns(void);
    fprintf(state_log, ",\"frame_pacing\":{\"target_fps\":%d,\"pace_hz\":%.3f,\"period_ns\":%llu}",
            mtl_fps_target(),mtl_pace_hz(),(unsigned long long)dm_nvn_present_period_ns());
    dm_opus_shadow_stats opus;
    dm_opus_shadow_snapshot(&opus);
    // Functional counts only. Default SDK has no packet hook, so zero tracked
    // packets cannot be used as a denominator for SDK/native CPU comparisons.
    fprintf(state_log, ",\"opus\":{\"attached\":%llu,\"live\":%llu,\"native_selected\":%llu,"
            "\"native_packets\":%llu,\"native_samples\":%llu,\"native_scalar_samples\":%llu,"
            "\"native_resets\":%llu,\"native_core_errors\":%llu,\"native_api_rejects\":%llu,"
            "\"init_fallbacks\":%llu,\"stack_rejects\":%llu,\"tracked_sdk_packets\":%llu,"
            "\"capacity_rejects\":%llu}",
            (unsigned long long)opus.attached,(unsigned long long)opus.live,
            (unsigned long long)opus.native_selected,(unsigned long long)opus.native_packets,
            (unsigned long long)opus.native_samples,(unsigned long long)opus.native_scalar_samples,
            (unsigned long long)opus.native_resets,(unsigned long long)opus.native_core_errors,
            (unsigned long long)opus.native_api_rejects,(unsigned long long)opus.init_fallbacks,
            (unsigned long long)opus.stack_rejects,(unsigned long long)opus.tracked_sdk_packets,
            (unsigned long long)opus.capacity_rejects);
    uint64_t prepare_reuses,prepare_lookups,prepare_checks;
    vk_graphics_prepare_stats(&prepare_reuses,&prepare_lookups,&prepare_checks);
    fprintf(state_log, ",\"pipeline_prepare\":{\"reuses\":%llu,\"lookups\":%llu,\"checks\":%llu,\"pipelines\":%u}",
            (unsigned long long)prepare_reuses,(unsigned long long)prepare_lookups,
            (unsigned long long)prepare_checks,vk_graphics_pipeline_count());
    void vk_counter_query_stats(uint64_t *,uint64_t *,int *);
    uint64_t query_begins,query_ends;int query_span;
    vk_counter_query_stats(&query_begins,&query_ends,&query_span);
    fprintf(state_log, ",\"occlusion_queries\":{\"span\":%s,\"begins\":%llu,\"ends\":%llu}",
        query_span?"true":"false",(unsigned long long)query_begins,
        (unsigned long long)query_ends);
    void vk_graphics_stage_stats(uint64_t *,uint64_t *,int *);
    uint64_t relaxed_barriers,fallback_barriers;int stage_reference;
    vk_graphics_stage_stats(&relaxed_barriers,&fallback_barriers,&stage_reference);
    fprintf(state_log, ",\"stage_barriers\":{\"reference\":%s,\"relaxed\":%llu,\"fallback\":%llu}",
        stage_reference?"true":"false",(unsigned long long)relaxed_barriers,
        (unsigned long long)fallback_barriers);
    int vk_submission_barrier_mode(void);
    fprintf(state_log, ",\"submission_barriers\":{\"reference\":%s}",
        vk_submission_barrier_mode()?"true":"false");
    void vk_sample_liveness_stats(uint64_t *,uint64_t *);
    void vk_sample_query_elision_stats(uint64_t *,int *);
    void vk_storage_bias_stats(uint64_t *,uint64_t *,uint64_t *,int *);
    dm_android_hint_stats hint;dm_android_hint_snapshot(&hint);
    fprintf(state_log, ",\"performance_hint\":{\"reference\":%s,\"available\":%d,\"active\":%d,\"duration_v2\":%s,\"sessions\":%llu,\"reports\":%llu,\"failures\":%llu,\"last_error\":%d,\"tid\":%d,\"target_ns\":%llu,\"cpu_ns\":%llu,\"wall_ns\":%llu,\"preferred_ns\":%lld}",
      hint.reference?"true":"false",hint.available,hint.active,hint.v2?"true":"false",
      (unsigned long long)hint.sessions,(unsigned long long)hint.reports,(unsigned long long)hint.failures,
      hint.last_error,hint.tid,(unsigned long long)hint.target_ns,(unsigned long long)hint.cpu_ns,
      (unsigned long long)hint.wall_ns,(long long)hint.preferred_ns);
    void vk_pool_sync_policy_stats(uint64_t *,uint64_t *,uint64_t *,int *);
    uint64_t vk_pool_overwrite_skipped_pages(void);
    void vk_pool_mirror_stats(uint64_t *,uint64_t *,uint64_t *,uint64_t *,int *);
    void vk_image_layout_stats(uint64_t *,uint64_t *,uint64_t *,int *);
    void vk_stream_stats(uint64_t *,uint64_t *,uint64_t *,int *);
    void vk_store_descriptor_stats(uint64_t *,uint64_t *,int *);
    void vk_store_slot_stats(uint64_t *,uint64_t *);
    void vk_store_event_stats(uint64_t *,uint64_t *);
    void vk_bloom_quarter_blit_stats(uint64_t *,uint64_t *,uint64_t *);
    void vk_attachment_raster_stats(uint64_t [4]);
    int vk_attachment_raster_enabled(void);
    uint64_t raster[4];vk_attachment_raster_stats(raster);
    fprintf(state_log, ",\"attachment_raster_run\":{\"enabled\":%s,\"captures\":%llu,\"hot_records\":%llu,\"scatters\":%llu,\"expansions\":%llu}",
      vk_attachment_raster_enabled()?"true":"false",(unsigned long long)raster[0],
      (unsigned long long)raster[1],(unsigned long long)raster[2],(unsigned long long)raster[3]);
    void vk_image_split_stats(uint64_t [6]);
    int vk_image_split_enabled(void);
    uint64_t image_split[6];vk_image_split_stats(image_split);
    fprintf(state_log, ",\"image_buffer_split\":{\"enabled\":%s,\"builds\":%llu,\"hits\":%llu,\"skipped_items\":%llu,\"expansions\":%llu,\"expanded_items\":%llu,\"invalidations\":%llu}",
      vk_image_split_enabled()?"true":"false",(unsigned long long)image_split[0],
      (unsigned long long)image_split[1],(unsigned long long)image_split[2],
      (unsigned long long)image_split[3],(unsigned long long)image_split[4],(unsigned long long)image_split[5]);
    void vk_driver_constant_stats(uint64_t *,uint64_t *,uint64_t *,int *);
    int vk_driver_span_reference_mode(void);
    uint64_t driver_last,driver_content,driver_misses;int driver_reference;
    vk_driver_constant_stats(&driver_last,&driver_content,&driver_misses,&driver_reference);
    fprintf(state_log, ",\"driver_constants\":{\"reference\":%s,\"span_reference\":%s,\"last_hits\":%llu,\"content_hits\":%llu,\"misses\":%llu}",
      driver_reference?"true":"false",vk_driver_span_reference_mode()?"true":"false",(unsigned long long)driver_last,
      (unsigned long long)driver_content,(unsigned long long)driver_misses);
    void vk_descriptor_batch_stats(uint64_t *,uint64_t *,int *);
    uint64_t descriptor_calls,descriptor_sets;int descriptor_batch_reference;
    vk_descriptor_batch_stats(&descriptor_calls,&descriptor_sets,&descriptor_batch_reference);
    fprintf(state_log, ",\"descriptor_allocation\":{\"reference\":%s,\"calls\":%llu,\"sets\":%llu}",
      descriptor_batch_reference?"true":"false",(unsigned long long)descriptor_calls,(unsigned long long)descriptor_sets);
    void vk_bloom_quarter_clears(uint64_t *,uint64_t *);
    uint64_t bloom_clear_a,bloom_clear_b1;vk_bloom_quarter_clears(&bloom_clear_a,&bloom_clear_b1);
    fprintf(state_log, ",\"bloom_dead_clears\":{\"before_a\":%llu,\"before_b1\":%llu}",
      (unsigned long long)bloom_clear_a,(unsigned long long)bloom_clear_b1);
    void vk_descriptor_history_stats(uint64_t *,uint64_t *,uint64_t *,int *);
    uint64_t history_hits,history_misses,history_bytes;int history_reference;
    vk_descriptor_history_stats(&history_hits,&history_misses,&history_bytes,&history_reference);
    fprintf(state_log, ",\"descriptor_history\":{\"reference\":%s,\"hits\":%llu,\"misses\":%llu,\"bytes\":%llu}",
      history_reference?"true":"false",(unsigned long long)history_hits,(unsigned long long)history_misses,(unsigned long long)history_bytes);
    void vk_descriptor_reuse_stats(uint64_t *,uint64_t *,int *);
    uint64_t descriptor_reuses,descriptor_resets;int descriptor_reuse_reference;
    vk_descriptor_reuse_stats(&descriptor_reuses,&descriptor_resets,&descriptor_reuse_reference);
    fprintf(state_log, ",\"descriptor_reuse\":{\"reference\":%s,\"hits\":%llu,\"pool_resets\":%llu}",
      descriptor_reuse_reference?"true":"false",(unsigned long long)descriptor_reuses,(unsigned long long)descriptor_resets);
    void vk_window_presentation_mode_stats(int *,int *,unsigned *);
    int mailbox_requested,presentation_mode;unsigned present_modes;
    vk_window_presentation_mode_stats(&mailbox_requested,&presentation_mode,&present_modes);
    fprintf(state_log, ",\"presentation_mode\":{\"mailbox_requested\":%s,\"actual\":%d,\"supported_mask\":%u}",
      mailbox_requested?"true":"false",presentation_mode,present_modes);
    void vk_window_present_worker_stats(uint64_t *,uint64_t *,int *,int *);
    uint64_t wsi_queued,wsi_finished;int wsi_active,wsi_reference;
    vk_window_present_worker_stats(&wsi_queued,&wsi_finished,&wsi_active,&wsi_reference);
    fprintf(state_log, ",\"present_worker\":{\"reference\":%s,\"active\":%s,\"queued\":%llu,\"finished\":%llu}",
      wsi_reference?"true":"false",wsi_active?"true":"false",(unsigned long long)wsi_queued,(unsigned long long)wsi_finished);
    int vk_descriptor_template_reference_mode(void);
    fprintf(state_log, ",\"descriptor_template_reference\":%s",vk_descriptor_template_reference_mode()?"true":"false");
    int vk_dynamic_uniform_reference_mode(void);
    fprintf(state_log, ",\"dynamic_uniform_reference\":%s",vk_dynamic_uniform_reference_mode()?"true":"false");
    int vk_dynamic_uniform_narrow_reference_mode(void);
    fprintf(state_log, ",\"dynamic_uniform_narrow_reference\":%s",vk_dynamic_uniform_narrow_reference_mode()?"true":"false");
    int vk_upload_scan_reference_mode(void);
    void vk_sampler_lookup_stats(uint64_t *,uint64_t *,int *);
    void vk_texture_binding_stats(uint64_t *,uint64_t *,uint64_t *,int *);
    uint64_t sampler_hits,sampler_misses;int sampler_reference;
    vk_sampler_lookup_stats(&sampler_hits,&sampler_misses,&sampler_reference);
    fprintf(state_log, ",\"sampler_lookup\":{\"reference\":%s,\"hits\":%llu,\"misses\":%llu}",
      sampler_reference?"true":"false",(unsigned long long)sampler_hits,(unsigned long long)sampler_misses);
    uint64_t binding_views,binding_prepares,binding_misses;int binding_reference;
    vk_texture_binding_stats(&binding_views,&binding_prepares,&binding_misses,&binding_reference);
    fprintf(state_log, ",\"texture_bindings\":{\"reference\":%s,\"views\":%llu,\"prepares\":%llu,\"misses\":%llu}",
      binding_reference?"true":"false",(unsigned long long)binding_views,(unsigned long long)binding_prepares,(unsigned long long)binding_misses);
    void vk_texel_storage_stats(uint64_t *,uint64_t *);
    uint64_t texel_selected,texel_fallback;
    vk_texel_storage_stats(&texel_selected,&texel_fallback);
    fprintf(state_log, ",\"texel_storage\":{\"selected\":%llu,\"fallback\":%llu}",
      (unsigned long long)texel_selected,(unsigned long long)texel_fallback);
    void vk_window_shader_present_stats(uint64_t *,uint64_t *,int *,int *);
    uint64_t present_selected,present_fallback;int present_requested,present_available;
    vk_window_shader_present_stats(&present_selected,&present_fallback,&present_requested,&present_available);
    fprintf(state_log, ",\"shader_present\":{\"requested\":%s,\"available\":%s,\"selected\":%llu,\"fallback\":%llu}",
      present_requested?"true":"false",present_available?"true":"false",
      (unsigned long long)present_selected,(unsigned long long)present_fallback);
    fprintf(state_log, ",\"upload_scan_reference\":%s",vk_upload_scan_reference_mode()?"true":"false");
    uint64_t vk_upload_page_rechecks_skipped(void);
    fprintf(state_log, ",\"upload_page_rechecks_skipped\":%llu",(unsigned long long)vk_upload_page_rechecks_skipped());
    void vk_pool_lookup_stats(uint64_t *,uint64_t *,uint64_t *,int *);
    uint64_t lookup_hits,lookup_ranges,lookup_slow;int lookup_reference;
    vk_pool_lookup_stats(&lookup_hits,&lookup_ranges,&lookup_slow,&lookup_reference);
    fprintf(state_log, ",\"pool_lookup\":{\"reference\":%s,\"hits\":%llu,\"ranges\":%llu,\"slow\":%llu}",
      lookup_reference?"true":"false",(unsigned long long)lookup_hits,(unsigned long long)lookup_ranges,(unsigned long long)lookup_slow);
    void vk_descriptor_content_stats(uint64_t *,uint64_t *);
    uint64_t vk_zero_draw_skipped(void);
    void vk_uniform_specialization_stats(uint64_t *,uint64_t *,uint64_t *,uint64_t *,int *);
    uint64_t spec_hits,spec_pending,spec_limits,spec_variants;int spec_enabled;
    vk_uniform_specialization_stats(&spec_hits,&spec_pending,&spec_limits,&spec_variants,&spec_enabled);
    fprintf(state_log, ",\"uniform_specialization\":{\"enabled\":%s,\"hits\":%llu,\"pending\":%llu,\"limits\":%llu,\"variants\":%llu}",
      spec_enabled?"true":"false",(unsigned long long)spec_hits,(unsigned long long)spec_pending,
      (unsigned long long)spec_limits,(unsigned long long)spec_variants);
    void vk_uniform_predicate_stats(uint64_t *,uint64_t *,uint64_t *,uint64_t *,uint64_t *,uint64_t [16],int *);
    uint64_t pred_hits,pred_pending,pred_limits,pred_variants,pred_rejected,pred_masks[16];int pred_enabled;
    vk_uniform_predicate_stats(&pred_hits,&pred_pending,&pred_limits,&pred_variants,&pred_rejected,pred_masks,&pred_enabled);
    fprintf(state_log, ",\"uniform_predicate\":{\"enabled\":%s,\"hits\":%llu,\"pending\":%llu,\"limits\":%llu,\"variants\":%llu,\"rejected\":%llu,\"masks\":[",
      pred_enabled?"true":"false",(unsigned long long)pred_hits,(unsigned long long)pred_pending,
      (unsigned long long)pred_limits,(unsigned long long)pred_variants,(unsigned long long)pred_rejected);
    for(unsigned n=0;n<16;n++)fprintf(state_log,"%s%llu",n?",":"",(unsigned long long)pred_masks[n]);
    fputs("]}",state_log);
    void vk_mip_clear_stats(uint64_t *,uint64_t *,uint64_t *,uint64_t *,int *);
    uint64_t mip_admitted,mip_materialized,mip_continued,mip_rejected;int mip_enabled;
    vk_mip_clear_stats(&mip_admitted,&mip_materialized,&mip_continued,&mip_rejected,&mip_enabled);
    fprintf(state_log, ",\"deferred_mip_clears\":{\"enabled\":%s,\"admitted\":%llu,\"materialized\":%llu,\"continued\":%llu,\"rejected\":%llu}",
      mip_enabled?"true":"false",(unsigned long long)mip_admitted,(unsigned long long)mip_materialized,
      (unsigned long long)mip_continued,(unsigned long long)mip_rejected);
    void vk_nofxaa_stats(uint64_t *,int *);
    void vk_bloom_quarter_stats(uint64_t *,uint64_t *,uint64_t *,uint64_t *,uint64_t *,int *);
    uint64_t bloom_chains,bloom_skipped,bloom_replaced,bloom_redirected,bloom_fallbacks;int bloom_enabled;
    vk_bloom_quarter_stats(&bloom_chains,&bloom_skipped,&bloom_replaced,&bloom_redirected,&bloom_fallbacks,&bloom_enabled);
    fprintf(state_log, ",\"bloom_quarter\":{\"enabled\":%s,\"chains\":%llu,\"skipped\":%llu,\"replaced\":%llu,\"redirected\":%llu,\"fallbacks\":%llu}",
      bloom_enabled?"true":"false",(unsigned long long)bloom_chains,(unsigned long long)bloom_skipped,
      (unsigned long long)bloom_replaced,(unsigned long long)bloom_redirected,(unsigned long long)bloom_fallbacks);
    uint64_t no_fxaa_replacements;int no_fxaa;
    vk_nofxaa_stats(&no_fxaa_replacements,&no_fxaa);
    fprintf(state_log, ",\"no_fxaa\":{\"enabled\":%s,\"replacements\":%llu}",
      no_fxaa?"true":"false",(unsigned long long)no_fxaa_replacements);
    uint64_t vk_texture_zero_copy_skipped(void);
    int vk_zero_draw_reference_mode(void);
    void vk_zero_draw_proof_stats(uint64_t *,uint64_t [7]);
    uint64_t zero_attempts,zero_reasons[7];vk_zero_draw_proof_stats(&zero_attempts,zero_reasons);
    fprintf(state_log, ",\"zero_draws\":{\"reference\":%s,\"skipped\":%llu,\"attempts\":%llu,\"rejects\":[%llu,%llu,%llu,%llu,%llu,%llu,%llu],\"copies_skipped\":%llu}",
      vk_zero_draw_reference_mode()?"true":"false",(unsigned long long)vk_zero_draw_skipped(),
      (unsigned long long)zero_attempts,(unsigned long long)zero_reasons[0],(unsigned long long)zero_reasons[1],
      (unsigned long long)zero_reasons[2],(unsigned long long)zero_reasons[3],(unsigned long long)zero_reasons[4],
      (unsigned long long)zero_reasons[5],(unsigned long long)zero_reasons[6],
      (unsigned long long)vk_texture_zero_copy_skipped());
    uint64_t vk_render_clear_attachment_omissions(void);
    int vk_render_clear_reference_mode(void);
    fprintf(state_log, ",\"clear_only_attachments\":{\"reference\":%s,\"omitted\":%llu}",
      vk_render_clear_reference_mode()?"true":"false",
      (unsigned long long)vk_render_clear_attachment_omissions());
    uint64_t vk_render_readonly_store_omissions(void);
    fprintf(state_log, ",\"readonly_store\":{\"omitted\":%llu}",
      (unsigned long long)vk_render_readonly_store_omissions());
    void vk_driver_arena_stats(uint64_t *,uint64_t *,uint64_t *,uint64_t *,uint64_t *);
    uint64_t driver_arena_slots,driver_arena_zeroes,driver_arena_chunks,driver_arena_live,driver_arena_reuses;
    vk_driver_arena_stats(&driver_arena_slots,&driver_arena_zeroes,&driver_arena_chunks,
                         &driver_arena_live,&driver_arena_reuses);
    fprintf(state_log, ",\"driver_arena\":{\"slots\":%llu,\"initial_zeroes\":%llu,\"chunks\":%llu,\"live_bytes\":%llu,\"chunk_reuses\":%llu}",
      (unsigned long long)driver_arena_slots,(unsigned long long)driver_arena_zeroes,
      (unsigned long long)driver_arena_chunks,(unsigned long long)driver_arena_live,
      (unsigned long long)driver_arena_reuses);
    void vk_texture_cache_stats(uint64_t *,uint64_t *,uint64_t *,uint64_t *);
    uint64_t image_private,image_rebuildable,image_budget,image_released;
    vk_texture_cache_stats(&image_private,&image_rebuildable,&image_budget,&image_released);
    fprintf(state_log, ",\"private_image_cache\":{\"allocation_bytes\":%llu,\"cpu_rebuildable_bytes\":%llu,\"budget_bytes\":%llu,\"released_allocation_bytes\":%llu}",
      (unsigned long long)image_private,(unsigned long long)image_rebuildable,
      (unsigned long long)image_budget,(unsigned long long)image_released);
    uint64_t descriptor_content_hits,descriptor_content_misses;
    vk_descriptor_content_stats(&descriptor_content_hits,&descriptor_content_misses);
    fprintf(state_log, ",\"descriptor_contents\":{\"hits\":%llu,\"misses\":%llu}",
      (unsigned long long)descriptor_content_hits,(unsigned long long)descriptor_content_misses);
    void vk_query_pool_stats(uint64_t *,uint64_t *);
    uint64_t query_pool_reuses,query_pool_creates;
    vk_query_pool_stats(&query_pool_reuses,&query_pool_creates);
    fprintf(state_log, ",\"query_pool_cache\":{\"reuses\":%llu,\"creates\":%llu}",
      (unsigned long long)query_pool_reuses,(unsigned long long)query_pool_creates);
    void vk_texel_view_stats(uint64_t *,uint64_t *);
    void vk_texture_dirty_stats(uint64_t *,uint64_t *);
    int mtl_pool_defer_publication_mode(void);
    fprintf(state_log, ",\"pool_publication\":{\"deferred\":%s}",
      mtl_pool_defer_publication_mode()?"true":"false");
    int vk_texture_dirty_reference_mode(void);
    uint64_t texture_dirty_hits,texture_dirty_checks;
    vk_texture_dirty_stats(&texture_dirty_hits,&texture_dirty_checks);
    fprintf(state_log, ",\"texture_dirty_proofs\":{\"reference\":%s,\"hits\":%llu,\"checks\":%llu}",
      vk_texture_dirty_reference_mode()?"true":"false",
      (unsigned long long)texture_dirty_hits,(unsigned long long)texture_dirty_checks);
    uint64_t texel_view_hits,texel_view_creates;
    vk_texel_view_stats(&texel_view_hits,&texel_view_creates);
    fprintf(state_log, ",\"texel_view_cache\":{\"hits\":%llu,\"creates\":%llu}",
      (unsigned long long)texel_view_hits,(unsigned long long)texel_view_creates);
    uint64_t descriptor_hits,descriptor_misses;int descriptor_reference;
    vk_store_descriptor_stats(&descriptor_hits,&descriptor_misses,&descriptor_reference);
    fprintf(state_log, ",\"store_descriptors\":{\"reference\":%s,\"hits\":%llu,\"misses\":%llu}",
      descriptor_reference?"true":"false",(unsigned long long)descriptor_hits,(unsigned long long)descriptor_misses);
    uint64_t slot_stages,slot_skipped;vk_store_slot_stats(&slot_stages,&slot_skipped);
    fprintf(state_log, ",\"store_shader_slots\":{\"stages\":%llu,\"skipped_bindings\":%llu}",
      (unsigned long long)slot_stages,(unsigned long long)slot_skipped);
    uint64_t store_events,store_duplicate_reads;vk_store_event_stats(&store_events,&store_duplicate_reads);
    fprintf(state_log, ",\"store_read_events\":{\"recorded\":%llu,\"duplicates_skipped\":%llu}",
      (unsigned long long)store_events,(unsigned long long)store_duplicate_reads);
    uint64_t bloom_blits,bloom_query_fallbacks,bloom_resource_fallbacks;
    vk_bloom_quarter_blit_stats(&bloom_blits,&bloom_query_fallbacks,&bloom_resource_fallbacks);
    fprintf(state_log, ",\"bloom_materialization\":{\"blits\":%llu,\"query_fallbacks\":%llu,\"resource_fallbacks\":%llu}",
      (unsigned long long)bloom_blits,(unsigned long long)bloom_query_fallbacks,(unsigned long long)bloom_resource_fallbacks);
    uint64_t stream_splits,stream_boundaries,stream_draws;int stream_reference;
    vk_stream_stats(&stream_splits,&stream_boundaries,&stream_draws,&stream_reference);
    fprintf(state_log, ",\"stream_submits\":{\"reference\":%s,\"splits\":%llu,\"boundaries\":%llu,\"draws\":%llu}",
      stream_reference?"true":"false",(unsigned long long)stream_splits,
      (unsigned long long)stream_boundaries,(unsigned long long)stream_draws);
    uint64_t image_transitions,image_safe_uses,image_fallbacks;int image_reference;
    extern int vk_sampled_array_layout_reference_state(void);
    extern int vk_buffer_memory_reference_state(void);
    fprintf(state_log, ",\"buffer_memory\":{\"reference\":%s}",
      vk_buffer_memory_reference_state()?"true":"false");
    fprintf(state_log, ",\"sampled_array_layouts\":{\"reference\":%s}",
      vk_sampled_array_layout_reference_state()?"true":"false");
    vk_image_layout_stats(&image_transitions,&image_safe_uses,&image_fallbacks,&image_reference);
    fprintf(state_log, ",\"image_layouts\":{\"reference\":%s,\"transitions\":%llu,\"safe_uses\":%llu,\"fallbacks\":%llu}",
      image_reference?"true":"false",(unsigned long long)image_transitions,
      (unsigned long long)image_safe_uses,(unsigned long long)image_fallbacks);
    int shared_heap=dm_guest_heap_shared_mode();
    fprintf(state_log,",\"guest_heap\":{\"backing_initialized\":%s,\"shared\":%s}",
      shared_heap>=0?"true":"false",shared_heap==1?"true":"false");
    vk_pool_copy_stats copy_work;vk_pool_copy_snapshot(&copy_work);
    if(copy_work.enabled) {
      fprintf(state_log,",\"pool_copy_accounting\":{\"enabled\":true");
#define VK_POOL_COPY_JSON(name) fprintf(state_log,",\"" #name "\":%llu",(unsigned long long)copy_work.name);
      VK_POOL_COPY_FIELDS(VK_POOL_COPY_JSON)
#undef VK_POOL_COPY_JSON
      fputc('}',state_log);
    }
    uint64_t full_pool_bytes,used_pool_bytes,protected_pool_bytes;int pool_sync_reference;
    vk_pool_sync_policy_stats(&full_pool_bytes,&used_pool_bytes,&protected_pool_bytes,&pool_sync_reference);
    fprintf(state_log, ",\"pool_uploads\":{\"reference\":%s,\"full_bytes\":%llu,\"used_bytes\":%llu,\"protected_bytes\":%llu,\"overwrite_skipped_pages\":%llu}",
      pool_sync_reference?"true":"false",(unsigned long long)full_pool_bytes,
      (unsigned long long)used_pool_bytes,(unsigned long long)protected_pool_bytes,
      (unsigned long long)vk_pool_overwrite_skipped_pages());
    uint64_t pool_registered,pool_backed,pool_count,pool_materialized;int mirror_reference;
    uint64_t depth_attachments,depth_sampled;int depth_reference;
    uint64_t readonly_depth_uses;int readonly_depth_reference;
    vk_readonly_depth_stats(&readonly_depth_uses,&readonly_depth_reference);
    fprintf(state_log, ",\"readonly_depth\":{\"reference\":%s,\"attachments\":%llu}",
      readonly_depth_reference?"true":"false",(unsigned long long)readonly_depth_uses);
    uint64_t compute_sampled;int compute_reference;
    vk_compute_layout_stats(&compute_sampled,&compute_reference);
    fprintf(state_log, ",\"compute_layouts\":{\"reference\":%s,\"sampled\":%llu}",
      compute_reference?"true":"false",(unsigned long long)compute_sampled);
    vk_depth_layout_stats(&depth_attachments,&depth_sampled,&depth_reference);
    fprintf(state_log, ",\"depth_layouts\":{\"reference\":%s,\"attachments\":%llu,\"sampled\":%llu}",
      depth_reference?"true":"false",(unsigned long long)depth_attachments,(unsigned long long)depth_sampled);
    vk_pool_mirror_stats(&pool_registered,&pool_backed,&pool_count,&pool_materialized,&mirror_reference);
    fprintf(state_log, ",\"pool_mirrors\":{\"reference\":%s,\"registered_bytes\":%llu,\"backed_bytes\":%llu,\"pool_count\":%llu,\"backed_pools\":%llu,\"initialized_bytes\":%llu}",
      mirror_reference?"true":"false",(unsigned long long)pool_registered,(unsigned long long)pool_backed,
      (unsigned long long)pool_count,(unsigned long long)pool_materialized,
      (unsigned long long)vk_pool_initialized_bytes());
    void vk_pool_window_stats(uint64_t *,uint64_t *,uint64_t *,uint64_t *,uint64_t *);
    // Functional admission counts from the existing submission publication;
    // these do not measure bandwidth, tile merging or GPU duration.
    void vk_pixel_local_stats(uint64_t *,uint64_t *,uint64_t *);
    uint64_t local_planned,local_draws,local_aborts;
    vk_pixel_local_stats(&local_planned,&local_draws,&local_aborts);
    fprintf(state_log, ",\"pixel_local\":{\"enabled\":%s,\"shared_readonly_ds\":%s,\"planned\":%llu,\"draws\":%llu,\"aborts\":%llu}",
      vk_pixel_local_enabled()?"true":"false",vk_pixel_local_shared_ds_enabled()?"true":"false",(unsigned long long)local_planned,
      (unsigned long long)local_draws,(unsigned long long)local_aborts);
    uint64_t vk_pool_window_fallbacks(void);
    uint64_t window_bytes,window_admissions,window_added_bytes,window_added_spans,window_promotions;
    vk_pool_window_stats(&window_bytes,&window_admissions,&window_added_bytes,
                         &window_added_spans,&window_promotions);
    // Backing policy and cumulative mechanism counts, not process RSS/PSS.
    fprintf(state_log, ",\"pool_windows\":{\"window_bytes\":%llu,\"admissions\":%llu,\"added_bytes\":%llu,\"added_spans\":%llu,\"promotions\":%llu,\"enumeration_fallbacks\":%llu}",
      (unsigned long long)window_bytes,(unsigned long long)window_admissions,
      (unsigned long long)window_added_bytes,(unsigned long long)window_added_spans,
      (unsigned long long)window_promotions,(unsigned long long)vk_pool_window_fallbacks());
#ifdef DM_VULKAN_SHARED_POOLS
    void vk_pool_lazy_window_stats(int *,uint64_t *,uint64_t *,uint64_t *,uint64_t *);
    int lazy_active;uint64_t lazy_unique,lazy_mapped,lazy_nodes,lazy_covers;
    vk_pool_lazy_window_stats(&lazy_active,&lazy_unique,&lazy_mapped,&lazy_nodes,&lazy_covers);
    int vk_pool_shared_image_raw_observe_mode(void);
    // Enabled policy only: each observation still proves its Pool/page eligibility.
    fprintf(state_log, ",\"shared_image_raw_observe\":{\"experimental\":true,\"enabled_policy\":%s}",
      vk_pool_shared_image_raw_observe_mode()?"true":"false");
    fprintf(state_log, ",\"shared_lazy_windows\":{\"experimental\":true,\"active\":%s,\"logical_unique_bytes\":%llu,\"import_allocation_bytes\":%llu,\"import_nodes\":%llu,\"cover_nodes\":%llu}",
      lazy_active?"true":"false",(unsigned long long)lazy_unique,(unsigned long long)lazy_mapped,
      (unsigned long long)lazy_nodes,(unsigned long long)lazy_covers);
#endif
    uint64_t storage_direct,storage_copies,storage_bytes;int storage_reference;
    vk_storage_bias_stats(&storage_direct,&storage_copies,&storage_bytes,&storage_reference);
    fprintf(state_log, ",\"storage_alignment\":{\"reference\":%s,\"direct\":%llu,\"copies\":%llu,\"bytes\":%llu}",
      storage_reference?"true":"false",(unsigned long long)storage_direct,
      (unsigned long long)storage_copies,(unsigned long long)storage_bytes);
    uint64_t sample_live_draws,sample_dead_draws;vk_sample_liveness_stats(&sample_live_draws,&sample_dead_draws);
    uint64_t sample_elided_draws;int sample_reference;
    vk_sample_query_elision_stats(&sample_elided_draws,&sample_reference);
    fprintf(state_log, ",\"sample_liveness\":{\"live_draws\":%llu,\"overwritten_before_report\":%llu,\"elided_draws\":%llu,\"reference\":%s}",
        (unsigned long long)sample_live_draws,(unsigned long long)sample_dead_draws,
        (unsigned long long)sample_elided_draws,sample_reference?"true":"false");
    vk_buffer_sync_stats memory_sync;vk_buffer_sync_snapshot(&memory_sync);
    fprintf(state_log, ",\"buffer_sync\":{\"guest_submits\":%llu,\"internal_submits\":%llu,\"readback_ranges\":%llu,\"readback_bytes\":%llu,\"internal_readback_bytes\":%llu,\"cpu_reads\":%llu,\"cpu_submit_waits\":%llu,\"cpu_pending_waits\":%llu,\"proof_reads\":%llu,\"proof_pending\":%llu,\"upload_waits\":%llu,\"cover_reference\":%s}",
      (unsigned long long)memory_sync.guest_submits,(unsigned long long)memory_sync.internal_submits,
      (unsigned long long)memory_sync.readback_ranges,(unsigned long long)memory_sync.readback_bytes,
      (unsigned long long)memory_sync.internal_readback_bytes,(unsigned long long)memory_sync.cpu_reads,
      (unsigned long long)memory_sync.cpu_submit_waits,(unsigned long long)memory_sync.cpu_pending_waits,
      (unsigned long long)memory_sync.proof_reads,(unsigned long long)memory_sync.proof_pending,
      (unsigned long long)memory_sync.upload_waits,memory_sync.cover_reference?"true":"false");
    fprintf(state_log, ",\"readback_memory\":{\"coherent_reference\":%s,\"cached_bytes\":%llu,\"invalidations\":%llu}",
      memory_sync.readback_reference?"true":"false",(unsigned long long)memory_sync.cached_readback_bytes,
      (unsigned long long)memory_sync.readback_invalidations);
    dm_hud_info hud; dm_hud_get(&hud);
    uint64_t copy_snapshots,copy_published,copy_fallbacks;int copy_reference;
    vk_cpu_copy_stats(&copy_snapshots,&copy_published,&copy_fallbacks,&copy_reference);
    fprintf(state_log, ",\"cpu_known_copies\":{\"reference\":%s,\"snapshots\":%llu,\"published\":%llu,\"fallbacks\":%llu}",
      copy_reference?"true":"false",(unsigned long long)copy_snapshots,
      (unsigned long long)copy_published,(unsigned long long)copy_fallbacks);
    vk_runtime_stats runtime;vk_runtime_snapshot(&runtime);
    fprintf(state_log, ",\"runtime_extrap\":{\"enabled\":%s,\"disabled\":%s,\"state\":%d,\"full\":%llu,\"candidates\":%llu,\"encoded\":%llu,\"missed\":%llu,\"jumps\":%llu,\"camera_reads\":%llu,\"camera_waits\":%llu}",
        runtime.enabled?"true":"false",runtime.disabled?"true":"false",runtime.state,
        (unsigned long long)runtime.full,(unsigned long long)runtime.extrapolated,
        (unsigned long long)runtime.encoded,(unsigned long long)runtime.missed,(unsigned long long)runtime.jumps,
        (unsigned long long)runtime.camera_reads,(unsigned long long)runtime.camera_waits);
    extern _Atomic int g_dm_frozen;
    vk_freeze_stats frozen; vk_freeze_snapshot(&frozen);
    fprintf(state_log, ",\"world_freeze\":{\"host_frozen\":%s,\"render_state\":%d,"
                       "\"full_frames\":%llu,\"frozen_frames\":%llu,"
                       "\"skipped_targets\":%llu,\"skipped_operations\":%llu}",
            atomic_load(&g_dm_frozen)?"true":"false",frozen.state,
            (unsigned long long)frozen.full_frames,(unsigned long long)frozen.frozen_frames,
            (unsigned long long)frozen.skipped_targets,(unsigned long long)frozen.skipped_operations);
    fprintf(state_log, ",\"original_hud_valid\":%s,\"original_hud_level\":%d,"
                      "\"original_hud_exp_permille\":%d,\"original_hud_belt\":[%d,%d,%d,%d],"
                      "\"original_hud_hp\":%d,\"original_hud_mp\":%d,\"original_hud_portal\":%d,"
                      "\"original_hud_players\":%d",
            hud.valid ? "true" : "false", hud.level, hud.exp_permille,
            hud.belt[0], hud.belt[1], hud.belt[2], hud.belt[3],
            hud.hp, hud.mp, hud.portal, hud.players);
#endif
    fprintf(state_log, ",\"gold\":%d", gold);
#ifdef DM_NATIVE_FEATURES
    dm_shop_info shop; dm_shop_get(&shop);
    fprintf(state_log, ",\"shop\":{\"open\":%s,\"gold\":%d,\"stash_gold\":%d,"
                      "\"selected_id\":%u,\"buy_price\":%d,\"can_buy\":%s,"
                      "\"held_id\":%u,\"sell_price\":%d}",
            shop.open ? "true" : "false", shop.gold, shop.stash_gold,
            shop.sel_valid ? shop.sel_id : 0, shop.buy_price,
            shop.can_buy ? "true" : "false", shop.held_valid ? shop.held_id : 0,
            shop.sell_price);
#endif
#ifdef DM_NATIVE_FEATURES
    dm_tg_snapshot binding = {0}; dm_touch_game_snapshot(&binding);
    fprintf(state_log, ",\"native_bindings\":{\"supported\":%s,\"ready\":%s,"
                      "\"identity_scope\":%d,\"group_seen\":%u,\"bank\":%d,"
                      "\"captured_ns\":%llu,\"raw_ids\":[",
            binding.supported ? "true" : "false", binding.ready ? "true" : "false",
            binding.identity_scope, binding.group_seen, binding.selected_bank,
            (unsigned long long)binding.captured_ns);
    for (int i = 0; i < DM_TG_SLOTS; ++i)
      fprintf(state_log, "%s%u", i ? "," : "", binding.raw_skill_id[i]);
    fputs("]}", state_log);
#endif
    fputs("}\n", state_log);
    fflush(state_log);
  }
}
static void state_ui(const char *category, const char *action,
                     const char *argument) {
  if (!strcmp(category, "PanelManager") && !strcmp(action, "OpenPanel") &&
      !strcmp(argument, "MainMenuButtonRibbon")) {
    atomic_store(&player_valid, 0);
    atomic_store(&current_path, 0);
  }
}
void dm_android_game_state_init(void) {
  for (int i = 0; i < g_nmod; i++)
    if (!strcmp(g_mod[i].name, "main"))
      main_base = g_mod[i].base;
  if (!main_base || !gate_obs_present(OBS_TICK))
    die("Original game state observation gate missing");
  const char *path = getenv("DM_GAME_STATE");
  if (path) {
    state_log = fopen(path, "w");
    if (!state_log)
      die("Game state log open");
  }
  extern void uimsg_subscribe(
      void (*)(const char *, const char *, const char *));
  uimsg_subscribe(state_ui);
#ifdef DM_NATIVE_FEATURES
  gate_obs_add(OBS_TICK, observe_tick);
#else
  gate_obs_set(OBS_TICK, observe_tick);
#endif
}
