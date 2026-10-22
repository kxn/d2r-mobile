// 原 nvn_mtl.m ghost_note/ghost_flush 的 Vulkan 对应：重放同一图标绘制。
// Vulkan counterpart of original nvn_mtl.m ghost_note/ghost_flush: replay the
// original icon draw, with the same grid/area/texture-frequency selection.
#include "nvn_vk_shader.h"
#include <math.h>
#include <stdatomic.h>
static _Atomic int enabled;
static _Atomic uint64_t pressed, center, shelf, found;
static void (*ui_rect_fn)(double *, double *);
void mtl_set_ui_rect_fn(void (*fn)(double *, double *)) { ui_rect_fn = fn; }
static float ui_height(void) {
  double y = 0, height = 1;
  if (ui_rect_fn) ui_rect_fn(&y, &height);
  return height > 0 && height < 1 ? height : 1;
}
static uint64_t pack_rect(const float r[4]) {
  uint64_t packed = 0;
  for (unsigned i = 0; i < 4; i++)
    packed |= (uint64_t)(uint16_t)lroundf(fminf(fmaxf(r[i], 0), 1) * 65535) << (i * 16);
  return packed;
}
static void unpack_rect(uint64_t packed, float r[4]) {
  for (unsigned i = 0; i < 4; i++) r[i] = (float)((packed >> (i * 16)) & 65535) / 65535;
}
void mtl_ui_ghost(int on, float px, float py, const float grid[4], float cx, float cy) {
  if (on) {
    if (!grid || !isfinite(px) || !isfinite(py) || !isfinite(cx) || !isfinite(cy))
      die("Vulkan invalid drag-follow request");
    atomic_store(&shelf, pack_rect(grid));
    atomic_store(&pressed, (uint64_t)(uint32_t)lroundf(fminf(fmaxf(px, 0), 1) * 65535) |
                          (uint64_t)(uint32_t)lroundf(fminf(fmaxf(py, 0), 1) * 65535) << 32);
    atomic_store(&center, (uint64_t)(uint32_t)(int32_t)lroundf(cx * 65535) |
                         (uint64_t)(uint32_t)(int32_t)lroundf(cy * 65535) << 32);
  } else atomic_store(&found, 0);
  atomic_store(&enabled, on != 0);
}
int mtl_ui_ghost_rect(float r[4]) {
  uint64_t rect = atomic_load(&found);
  if (!rect) return 0;
  unpack_rect(rect, r);
  return 1;
}
static struct {
  uint64_t pass;
  unsigned n, nt;
  int busy;
  float area, rect[4];
  struct { vk_state *state; p_draw draw; uint64_t texture; } candidate[4];
  uint64_t textures[128];
  unsigned counts[128];
} ghost;
int vk_ghost_replaying(void) {return ghost.busy;}
int vk_ghost_idle(void) { return !atomic_load(&enabled) && !ghost.n && !ghost.busy; }
static void clear_candidates(void) {
  for (unsigned i = 0; i < ghost.n; i++) free(ghost.candidate[i].state);
  ghost.n = 0;
}
// Pending drag-follow candidates are already-recorded original host draws.
// Include all of them in physical-backing planning before an outer operation
// borrows buffers: render_end may replay the selected candidate reentrantly.
// This only visits logical metadata, without swapping the active NVN state,
// selecting a candidate or causing the pending replay to execute early.
int vk_ghost_pool_ranges(vk_pool_requirement_sink sink,void *context) {
  if(!sink)return 0;
  // The UI can re-enable dragging before a later render_end. Disabling only
  // changes the request flag; it does not retire these saved candidates.
  if(ghost.busy || !ghost.n || ghost.pass!=V.passes)return 1;
  for(unsigned i=0;i<ghost.n;i++) {
    const vk_state *state=ghost.candidate[i].state;
    if(!state || !state->program)return 0;
    vk_shader *shaders[6]={0};vk_program_shaders(state->program,shaders);shaders[5]=NULL;
    if(!vk_graphics_pool_admission(shaders,state,NULL,sink,context))return 0;
  }
  return 1;
}
void vk_ghost_note(const p_draw *draw, vk_shader *fragment) {
  if (!atomic_load(&enabled) || ghost.busy || !fragment || draw->count != 4 ||
      S.stream[0].stride != 20 || S.vertex[0].size != 80 ||
      !S.attr[0].fmt || !S.attr[1].fmt) return;
  if (ghost.pass != V.passes) {
    clear_candidates(); ghost.nt = 0; ghost.pass = V.passes;
  }
  uint64_t texture = 0;
  for (unsigned i = 0; i < fragment->nb; i++) {
    vk_binding *binding = &fragment->bindings[i];
    if ((binding->kind == 3 || binding->kind == 6) && binding->reg >= 0 && binding->reg < 128 &&
        S.tex[1][binding->reg]) { texture = S.tex[1][binding->reg]; break; }
  }
  unsigned k = 0;
  while (k < ghost.nt && ghost.textures[k] != texture) k++;
  if (k == ghost.nt && k < 128) { ghost.textures[k] = texture; ghost.counts[k] = 0; ghost.nt++; }
  if (k < ghost.nt) ghost.counts[k]++;
  if(V.options.pool_defer_publication
#ifdef DM_VULKAN_SHARED_POOLS
      || vk_legacy_astc_copy_mode()
#endif
      ) {
    vk_shader *stages[6]={0};vk_program_shaders(S.program,stages);
    if(!vk_ghost_cpu_draw_ready(stages,&S,draw,NULL))return;
  }
  p_bind_buf *uniform = &S.ubo[0][0];
  if (!uniform->addr || uniform->size < 64 || vk_pool_pending_writes(uniform->addr, 64) ||
      vk_pool_pending_writes(S.vertex[0].address, 80)) return;
  float m[16], rect[4] = {9, 9, -9, -9};
  uint8_t vertices[80];
  // This is a CPU-side geometric proof, not a GPU buffer consumer. Read the
  // original CPU bytes, retaining the nonblocking pending-write guard.
  if(vk_pool_try_read(uniform->addr,sizeof m,m)!=1||
     vk_pool_try_read(S.vertex[0].address,sizeof vertices,vertices)!=1) return;
  for (unsigned i = 0; i < 4; i++) {
    float position[2]; memcpy(position, vertices + 20 * i, sizeof position);
    float x = (m[0] * position[0] + m[4] * position[1] + m[12] + 1) / 2,
          y = (m[1] * position[0] + m[5] * position[1] + m[13] + 1) / 2;
    if (!isfinite(x) || !isfinite(y)) return;
    rect[0] = fminf(rect[0], x); rect[2] = fmaxf(rect[2], x);
    rect[1] = fminf(rect[1], y); rect[3] = fmaxf(rect[3], y);
  }
  rect[1] /= ui_height(); rect[3] /= ui_height();
  uint64_t point = atomic_load(&pressed);
  float px = (uint32_t)point / 65535.f, py = (uint32_t)(point >> 32) / 65535.f;
  if (px < rect[0] || px > rect[2] || py < rect[1] || py > rect[3]) return;
  float grid[4]; unpack_rect(atomic_load(&shelf), grid);
  if (grid[2] <= 0 || grid[3] <= 0) return;
  float width = rect[2] - rect[0], height = rect[3] - rect[1], area = width * height,
        col = (rect[0] - grid[0]) / grid[2], row = (rect[1] - grid[1]) / grid[3],
        cols = width / grid[2], rows = height / grid[3];
  if (fabsf(col - roundf(col)) > .15f || fabsf(row - roundf(row)) > .15f ||
      fabsf(cols - roundf(cols)) > .15f || fabsf(rows - roundf(rows)) > .15f ||
      cols < .85f || rows < .85f || cols > 2.15f || rows > 4.15f) return;
  if (ghost.n && area > ghost.area * 1.03f) return;
  if (ghost.n && area < ghost.area * .97f) clear_candidates();
  if (ghost.n == 4) {
    free(ghost.candidate[0].state);
    memmove(ghost.candidate, ghost.candidate + 1, 3 * sizeof ghost.candidate[0]); ghost.n = 3;
  }
  ghost.area = area; memcpy(ghost.rect, rect, sizeof rect);
  unsigned i = ghost.n++;
  ghost.candidate[i].state = malloc(sizeof S);
  if (!ghost.candidate[i].state) die("Vulkan drag-follow state allocation");
  *ghost.candidate[i].state = S;
  ghost.candidate[i].draw = *draw; ghost.candidate[i].texture = texture;
  // A concurrent UI drag request may become enabled after draw preflight.
  // Capture the original candidate first, then drain through the ordinary
  // reentrant ghost flush so that no host enhancement is silently dropped.
  if(vk_mip_clear_pending())vk_mip_clear_drain();
}
void vk_ghost_flush(void) {
  if (ghost.busy || !ghost.n) return;
  if (ghost.pass != V.passes || !atomic_load(&enabled)) { clear_candidates(); return; }
  unsigned best = 0, least = UINT32_MAX;
  for (unsigned i = 0; i < ghost.n; i++) {
    unsigned count = 0;
    for (unsigned k = 0; k < ghost.nt; k++) if (ghost.textures[k] == ghost.candidate[i].texture)
      count = ghost.counts[k];
    if (count < least) { least = count; best = i; }
  }
  if(V.options.pool_defer_publication
#ifdef DM_VULKAN_SHARED_POOLS
      || vk_legacy_astc_copy_mode()
#endif
      ) {
    const vk_state *state=ghost.candidate[best].state;
    vk_shader *stages[6]={0};vk_program_shaders(state->program,stages);
    if(!vk_ghost_cpu_draw_ready(stages,state,&ghost.candidate[best].draw,NULL)) {
      atomic_store(&found,0);clear_candidates();return;
    }
  }
  vk_state saved = S;
  vk_state_restore(ghost.candidate[best].state);vk_pipeline_state_changed();
  const tex_t *target = S.targets.n ? S.targets.color[0] : NULL;
  if (!target) { vk_state_restore(&saved);vk_pipeline_state_changed(); clear_candidates(); return; }
  unsigned level = S.targets.has_cview[0] && S.targets.cview[0].set_levels
                     ? S.targets.cview[0].base_level : 0;
  float width = target->d.width >> level, height = target->d.height >> level;
  if (!S.has_viewport) {
    S.has_viewport = 1; S.viewport[0][0] = S.viewport[0][1] = 0;
    S.viewport[0][2] = width; S.viewport[0][3] = height;
  }
  uint64_t at = atomic_load(&center);
  float x = (int32_t)(uint32_t)at / 65535.f, y = (int32_t)(uint32_t)(at >> 32) / 65535.f;
  S.viewport[0][0] += (x - (ghost.rect[0] + ghost.rect[2]) / 2) * width;
  S.viewport[0][1] += (y - (ghost.rect[1] + ghost.rect[3]) / 2) * height * ui_height();
  S.has_scissor = 0;
  atomic_store(&found, pack_rect(ghost.rect));
  ghost.busy = 1;
  uint64_t before_draws=V.draws;
  vk_draw(&ghost.candidate[best].draw, NULL);
  if(V.options.pool_defer_publication && V.draws==before_draws)atomic_store(&found,0);
  static int logged;
  if (!logged++) LOG("[vulkan] original shop drag-follow replay pass=%llu candidates=%u texture_uses=%u rect=%.4f,%.4f-%.4f,%.4f",
                    (unsigned long long)V.passes, ghost.n, least,
                    ghost.rect[0], ghost.rect[1], ghost.rect[2], ghost.rect[3]);
  ghost.busy = 0;
  vk_state_restore(&saved);vk_pipeline_state_changed();
  clear_candidates();
}
