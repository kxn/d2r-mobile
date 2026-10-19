#include "touch_ui.h"
#include "../../gfx/vulkan/nvn_vk_window.h"
#include "dm.h"
#include "dm_actions.h"
#include "dm_cursor.h"
#include "dm_hud.h"
#include "dm_uiscreen.h"
#include "../../ios/Sources/dm_touch_native_art.h"
#include "dm_input.h"
#include "dm_world.h"
#include <math.h>

static int front_slot = -1, front_drag, item_asked, item_kind, drag;
static float front_x, front_y;
static unsigned item_seq, release_seq;
static int release_asked, pending_slot = -1;
static double pending_time, pending_wall;
static int binding_slot = -1;
static float binding_x, binding_y;
static uint64_t binding_key;
static uint32_t selected_point;
static double add_at, stat_next;
static double tooltip_until = -2;
static int map_slot = -1, map_back_slot = -1;
static float map_x, map_y;
static double map_started;
static unsigned map_world_seq;
static int ghost_on, ghost_dim;
static float ghost_rect[4];
void mtl_ui_ghost(int, float, float, const float[4], float, float);
int mtl_ui_ghost_rect(float[4]);
void mtl_set_ui_rect_fn(void (*)(double *, double *));
static void ghost_ui_rect(double *y, double *height) {
  int x, top, width, h, real_w, real_h;
  dm_ui_rect(&x, &top, &width, &h);
  if (dm_screen_real(&real_w, &real_h) && real_h > 0 && h > 0) {
    *y = (double)top / real_h; *height = (double)h / real_h;
  } else { *y = 0; *height = 1; }
}
int dm_android_ui_ghost_rect(float rect[4]) {
  if (!ghost_dim) return 0;
  memcpy(rect, ghost_rect, sizeof ghost_rect);
  return 1;
}

static int minimap_hit(dm_tc_state *s, float x, float y) {
  float r[4], sx0, sy0, sx1, sy1;
  if (dm_tc_hit(&s->layout, x, y) >= 0 || !dm_ui_minimap_rect(r) ||
      !vk_window_surface_point(r[0], r[1], &sx0, &sy0) ||
      !vk_window_surface_point(r[2], r[3], &sx1, &sy1)) return 0;
  float top = sy0 * s->layout.height;
  float height = (sy1 - sy0) * s->layout.height;
  float band = height / 4;
  float minimum = 30 * dm_tc_scale_for_size(s->layout.width, s->layout.height);
  if (band < minimum) band = fminf(minimum, height * .4f);
  return x >= sx0 * s->layout.width && x <= sx1 * s->layout.width &&
         y >= top && y <= top + band;
}

// Surface 有左右留边；原命中、鼠标与光标接口吃的是游戏视图坐标。
// Original hit, mouse and cursor APIs take game-view coordinates, excluding
// the Android Surface's letterbox bars.
static int point(dm_tc_state *s, float x, float y, float *nx, float *ny) {
  if (s->layout.width <= 0 || s->layout.height <= 0) return 0;
  return vk_window_world_point(x / s->layout.width, y / s->layout.height,
                                nx, ny);
}
static void mouse(dm_tc_state *s, int phase, float x, float y) {
  float nx, ny;
  if (phase == 3) dm_ui_touch(3, 0, 0);
  else if (point(s, x, y, &nx, &ny)) dm_ui_touch(phase, nx, dm_ui_ny(ny));
}
static void ghost_sync(dm_tc_state *s) {
  dm_tc_finger *finger = s->pointer_slot >= 0 && s->pointer_slot < DM_TC_MAX_TOUCH
                          ? &s->finger[s->pointer_slot] : NULL;
  if (!finger || !finger->active || !finger->shop_drag) {
    if (ghost_on) mtl_ui_ghost(0, 0, 0, NULL, 0, 0);
    ghost_on = ghost_dim = 0;
    return;
  }
  float grid[4], px, py, cx, cy;
  float lift = DM_TC_POINTER_LIFT_PT * dm_tc_scale_for_size(s->layout.width, s->layout.height);
  if (!dm_ui_shelf_grid(grid) || !point(s, finger->aim_origin_x, finger->aim_origin_y, &px, &py) ||
      !point(s, finger->x, finger->y - lift, &cx, &cy)) {
    // 手指进入 Surface 留边时撤掉旧图，不能留在上个有效落点。
    // Clear the previous image when the finger enters the Surface letterbox.
    if (ghost_on) mtl_ui_ghost(0, 0, 0, NULL, 0, 0);
    ghost_on = ghost_dim = 0;
    return;
  }
  static int registered;
  if (!registered) { mtl_set_ui_rect_fn(ghost_ui_rect); registered = 1; }
  mtl_ui_ghost(1, px, dm_ui_ny(py), grid, cx, dm_ui_ny(cy));
  if (!ghost_on) LOG("Android original shop drag-follow image enabled");
  ghost_on = 1;
  float rect[4], x0, y0, x1, y1;
  ghost_dim = mtl_ui_ghost_rect(rect) && (item_kind != 8 || dm_ui_stack_left() == 1) &&
    vk_window_surface_point(rect[0], dm_ui_ny_back(rect[1]), &x0, &y0) &&
    vk_window_surface_point(rect[2], dm_ui_ny_back(rect[3]), &x1, &y1);
  if (ghost_dim) {
    ghost_rect[0] = x0 * s->layout.width; ghost_rect[1] = y0 * s->layout.height;
    ghost_rect[2] = x1 * s->layout.width; ghost_rect[3] = y1 * s->layout.height;
    static int logged;
    if (!logged++) LOG("Android original shop origin tint rect %.1f,%.1f-%.1f,%.1f",
                      ghost_rect[0], ghost_rect[1], ghost_rect[2], ghost_rect[3]);
  }
}
static unsigned drop_query(dm_tc_state *s, const dm_tc_finger *f) {
  float x, lifted_y, fx, fy;
  float lift = DM_TC_POINTER_LIFT_PT *
               dm_tc_scale_for_size(s->layout.width, s->layout.height);
  if (!point(s, f->x, f->y - lift, &x, &lifted_y) ||
      !point(s, f->x, f->y, &fx, &fy)) return 0;
  return dm_ui_drop_query(x, dm_ui_ny(lifted_y), dm_ui_ny(fy));
}
static void item_answer(dm_tc_state *s) {
  if (!item_asked || s->pointer_slot < 0) return;
  int kind = dm_ui_item_result(item_seq);
  if (kind < 0) return;
  item_asked = 0;
  item_kind = kind;
  dm_tc_pointer_item(s, kind);
}
static void binding_sync(dm_tc_state *s) {
  static int previous;
  int state = dm_ui_bind_active();
  if (state == 2 && previous != 2) {
    float x, y;
    if (vk_window_surface_point(.155f, .5f, &x, &y)) {
      s->pointer_valid = 1;
      s->pointer_x = x * s->layout.width;
      s->pointer_y = y * s->layout.height;
    }
  }
  previous = state;
  float nx, ny, x, y;
  int hit = dm_ui_bind_skill_hit(&nx, &ny);
  s->tap_button = 0; s->tap_move = 0;
  if (!hit) selected_point = 0;
  if (hit && vk_window_surface_point(nx, dm_ui_ny_back(ny), &x, &y)) {
    s->tap_x = x * s->layout.width; s->tap_y = y * s->layout.height;
    if (hit == 1 && state == 2) s->tap_button = DM_BTN_Y;
    else if (!state) s->tap_move = 1;
  }
}
static void finish(dm_tc_state *s, int slot, double now) {
  item_answer(s);
  binding_sync(s);
  int pointer = s->pointer_slot == slot;
  dm_tc_finger *f = &s->finger[slot];
  if (pointer && f->shop_drag) {
    int kind = release_asked ? dm_ui_item_result(release_seq) : 0;
    if (kind < 0) kind = dm_ui_drop_last();
    if (kind < 0) kind = 0;
    float nx, ny;
    if ((kind == 11 || (kind == 5 && dm_ui_drop_bag())) &&
        point(s, f->aim_origin_x, f->aim_origin_y, &nx, &ny)) {
      if (kind == 11) dm_ui_stack_cube_at(nx, dm_ui_ny(ny));
      else if (item_kind == 8) dm_ui_stack_take_at(nx, dm_ui_ny(ny));
      else dm_ui_shop_buy_at(nx, dm_ui_ny(ny));
      s->pointer_x = f->aim_origin_x;
      s->pointer_y = f->aim_origin_y;
    }
  } else if (pointer && f->pointer_dragged) {
    int kind = release_asked ? dm_ui_item_result(release_seq) : 0;
    if (kind < 0) kind = dm_ui_drop_last();
    if (kind < 0) kind = 0;
    if (kind == 7) { dm_shop_sell_cursor(); kind = 0; }
    dm_tc_pointer_release(s, kind);
    float nx, ny, sx, sy;
    // 原桥的 5 = 宿主算出的落点；0 = 物品原位。不能把 5 当触点坐标。
    // Original bridge: 5 is the host-computed destination; 0 is the original
    // item cell. Neither is the finger coordinate.
    int have_point = (kind == 5 && dm_ui_drop_point(&nx, &ny)) ||
                     (kind == 0 && dm_ui_origin_point(&nx, &ny));
    if (have_point &&
        vk_window_surface_point(nx, dm_ui_ny_back(ny), &sx, &sy))
      dm_tc_pointer_release_at(s, sx * s->layout.width, sy * s->layout.height);
  }
  if (pointer && drag) { dm_ui_drag_end(); drag = 0; }
  unsigned tabs = s->tab_tap_seq, use = s->use_seq, ident = s->ident_seq;
  unsigned stack = s->stack_take_seq, press = s->long_press_seq;
  unsigned item = s->item_tap_seq, click = s->mouse_click_seq;
  unsigned moved = s->tap_move_seq;
  dm_tc_end_at(s, slot, now);
  if (pointer && s->navigation_mode)
    tooltip_until = s->tap_move_seq != moved || s->item_tap_seq != item ? -1 :
      now + (s->mouse_click_seq != click ? .05 : 2.8);
  if (s->tap_move_seq != moved) {
    uint32_t hit = dm_ui_hit_id();
    if (hit && hit == selected_point) {
      if (hit <= 4) dm_stat_add((int)hit - 1, 1);
      else add_at = now + .3;
    } else selected_point = hit;
  }
  if (s->tab_tap_seq != tabs) dm_ui_tab_go();
  if (s->use_seq != use) dm_ui_use_item();
  if (s->ident_seq != ident) dm_ui_identify_hovered();
  if (s->stack_take_seq != stack) { void dm_stash_take(int); dm_stash_take(1); }
  if (s->long_press_seq != press) dm_ui_long_press();
  if (s->item_tap_seq != item) dm_ui_item_tap();
  if (s->mouse_click_seq != click) {
    mouse(s, 0, s->mouse_click_x, s->mouse_click_y);
    mouse(s, 2, s->mouse_click_x, s->mouse_click_y);
  }
  release_asked = item_asked = 0;
}
int dm_android_ui_finger(dm_tc_state *s, int frontend, int phase, int slot,
                         float x, float y, double now) {
  if (slot < 0 || slot >= DM_TC_MAX_TOUCH) return 1;
  int hit = dm_tc_hit(&s->layout, x, y);
  int target = hit >= 0 && (s->enabled_targets & (1u << hit));
  if (front_slot == slot || (frontend && phase == 0 && !target)) {
    if (phase == 0) {
      if (front_slot >= 0) return 1;
      front_slot = slot; front_x = x; front_y = y; front_drag = 0;
    }
    if (front_slot != slot) return 1;
    if (phase == 1 && !front_drag && fabsf(y - front_y) > 10 &&
        fabsf(y - front_y) > fabsf(x - front_x) && dm_ui_fe_drag_ok()) {
      float nx, ny;
      if (point(s, front_x, front_y, &nx, &ny)) {
        mouse(s, 3, 0, 0);
        dm_ui_drag_begin(1, nx, dm_ui_ny(ny), 0); front_drag = 1;
      }
    }
    if (front_drag) {
      float nx, ny;
      if (phase == 1 && point(s, x, y, &nx, &ny))
        dm_ui_drag_move(nx, dm_ui_ny(ny));
      else if (phase >= 2) dm_ui_drag_end();
    } else mouse(s, phase, x, y);
    if (phase >= 2) front_slot = -1;
    return 1;
  }
  if (binding_slot == slot) {
    if (phase == 2 && !s->navigation_mode && hypotf(x - binding_x, y - binding_y) <=
                      14 * dm_tc_scale_for_size(s->layout.width, s->layout.height))
      dm_ui_bind_begin(binding_key);
    if (phase >= 2) binding_slot = -1;
    return 1;
  }
  if (map_slot == slot) {
    if (phase == 2 && !s->navigation_mode && now - map_started < .6 &&
        hypotf(x - map_x, y - map_y) <=
          14 * dm_tc_scale_for_size(s->layout.width, s->layout.height)) {
      if (dm_ui_map_is_full()) dm_ui_map_full(0);
      else {
        float nx, ny;
        if (point(s, x, y, &nx, &ny)) map_world_seq = dm_world_tap(nx, ny);
      }
    }
    if (phase >= 2) map_slot = -1;
    return 1;
  }
  if (!s->navigation_mode) {
    if (phase == 0 && s->belt_open && dm_tc_belt_outside(s, x, y)) {
      dm_belt_expand(0);
      return 1;
    }
    if (phase == 0 && binding_slot < 0 && target && hit <= DM_TC_SKILL4 && !dm_ui_bind_active()) {
      dm_tg_snapshot snapshot; dm_touch_game_snapshot(&snapshot);
      uint64_t button = s->layout.target[hit].button;
      dm_ta_glyph glyph = dm_ta_current_set(&snapshot, button, now_ns(), s->skill_set);
      if (glyph.status == DM_TA_EMPTY) {
        binding_slot = slot; binding_x = x; binding_y = y;
        binding_key = button | (s->skill_set ? DM_BTN_ZL : 0);
        return 1;
      }
    }
    if (phase == 0 && map_slot < 0 && minimap_hit(s, x, y)) {
      map_slot = slot; map_x = x; map_y = y; map_started = now;
      return 1;
    }
    if (phase == 0 && dm_ui_map_is_full() && !target &&
        !(x < s->layout.joy_zone_r && y >= s->layout.joy_zone_t)) {
      map_back_slot = slot; map_x = x; map_y = y; map_started = now;
    }
    if (slot == map_back_slot && phase >= 2) {
      if (phase == 2 && dm_ui_map_is_full() && now - map_started < .6 &&
          hypotf(x - map_x, y - map_y) <=
            14 * dm_tc_scale_for_size(s->layout.width, s->layout.height))
        dm_ui_map_full(0);
      map_back_slot = -1;
    }
    return 0;
  }
  if (phase == 0 && target) return 0;
  if (phase == 0) {
    if (pending_slot >= 0) {
      item_answer(s);
      if (!item_asked && (!release_asked || dm_ui_item_result(release_seq) >= 0))
        finish(s, pending_slot, pending_time);
      else {
        // 新触点不能迫使上一根手指在未判类时发出动作。
        // A new touch must not force an unclassified previous action.
        dm_tc_cancel_finger(s, pending_slot);
        dm_android_ui_cancel();
      }
      pending_slot = -1;
    }
    dm_tc_begin_at(s, slot, x, y, now);
    if (s->pointer_slot == slot) {
      float nx, ny;
      item_seq = point(s, x, y, &nx, &ny) ? dm_ui_item_query(nx, dm_ui_ny(ny)) : 0;
      item_asked = item_seq != 0; item_kind = -1;
    }
  } else if (phase == 1) {
    dm_tc_move_at(s, slot, x, y, now);
    item_answer(s);
    dm_tc_finger *f = &s->finger[slot];
    if (s->pointer_slot == slot && f->ui_drag) {
      float nx, ny;
      if (!drag && point(s, f->aim_origin_x, f->aim_origin_y, &nx, &ny)) {
        dm_ui_drag_lift(DM_TC_POINTER_LIFT_PT / s->layout.height);
        dm_ui_drag_begin(f->ui_drag, nx, dm_ui_ny(ny), 44 / s->layout.height);
        drag = 1;
      }
      if (drag && point(s, x, y, &nx, &ny)) dm_ui_drag_move(nx, dm_ui_ny(ny));
    }
  } else if (phase == 2) {
    dm_tc_move_at(s, slot, x, y, now);
    item_answer(s);
    if (s->pointer_slot == slot && s->finger[slot].pointer_dragged) {
      release_seq = drop_query(s, &s->finger[slot]);
      release_asked = release_seq != 0;
    }
    if (s->pointer_slot == slot && (item_asked || release_asked)) {
      pending_slot = slot; pending_time = pending_wall = now;
    } else finish(s, slot, now);
  } else {
    if (s->pointer_slot == slot) {
      dm_tc_finger *finger = &s->finger[slot];
      if (drag) { dm_ui_drag_end(); drag = 0; }
      // front_ios finishTouches(cancelled): return a dragged item without
      // clicking the destination or buying an item dragged from a shelf.
      if (finger->active && (finger->pointer_dragged || finger->shop_drag)) {
        if (!finger->shop_drag) dm_tc_pointer_release(s, 0);
        dm_tc_end_at(s, slot, now);
      } else dm_tc_cancel_finger(s, slot);
    } else dm_tc_cancel_finger(s, slot);
    if (pending_slot == slot) pending_slot = -1;
  }
  return 1;
}
void dm_android_ui_tick(dm_tc_state *s, double now) {
  dm_tc_set_tap_box(dm_ui_tap_box());
  dm_tc_set_cur_stack(dm_ui_cur_stack_id());
  dm_tc_set_hover_unid(dm_ui_hover_unid());
  dm_hud_info h; dm_hud_get(&h);
  dm_tc_set_holding(s, h.valid && h.holding && !dm_ui_targeting());
  item_answer(s);
  binding_sync(s);
  ghost_sync(s);
  if (map_world_seq) {
    int result = dm_world_tap_result(map_world_seq);
    if (result >= 0) {
      map_world_seq = 0;
      if (result == 0 && !s->navigation_mode) dm_ui_map_full(1);
    }
  }
  if (pending_slot >= 0) {
    // 原命中泵按游戏帧回答；慢于 4 fps 时，原前端的 250 ms 兜底会先把
    // NPC 行当普通鼠标点击发出。只在答案回来后收尾，手势时长仍用真实松手时刻。
    // The original hit pump answers in game frames. Below 4 fps, the original
    // 250 ms fallback sends NPC rows as mouse clicks before classification.
    // Finish only on an answer; gesture duration still uses actual release time.
    if (!item_asked && (!release_asked || dm_ui_item_result(release_seq) >= 0)) {
      if (now - pending_wall > .25)
        LOG("Android UI release waited %.0f ms for original hit/drop answers", (now-pending_wall)*1000);
      finish(s, pending_slot, pending_time); pending_slot = -1;
    } else if (now - pending_wall > 5) {
      // 无回答时取消，不能猜成空格点击、丢弃或出售。
      // Cancel unanswered input rather than guessing a click, drop or sale.
      LOG("Android UI release cancelled: original hit/drop query timed out");
      dm_tc_cancel_finger(s, pending_slot);
      dm_android_ui_cancel();
    }
  }
  if (s->navigation_mode && !dm_ui_bind_active() && !item_asked &&
      s->pointer_slot >= 0 && s->pointer_slot < DM_TC_MAX_TOUCH) {
    dm_tc_finger *finger = &s->finger[s->pointer_slot];
    float nx, ny;
    int hit = dm_ui_bind_skill_hit(&nx, &ny);
    if (pending_slot < 0 && finger->active && !finger->pointer_dragged &&
        now - finger->start_time_s >= .5 &&
        hypotf(finger->x - finger->aim_origin_x, finger->y - finger->aim_origin_y) <=
          10 * dm_tc_scale_for_size(s->layout.width, s->layout.height)) {
      uint32_t id = dm_ui_hit_id();
      if (hit == 2 && id >= 1 && id <= 4) {
        if (!finger->pointer_long) { finger->pointer_long = 1; stat_next = now; selected_point = id; }
        if (now >= stat_next) { stat_next = now + .15; dm_stat_add((int)id - 1, 1); }
      } else if (hit == 1 && !finger->pointer_long) {
        float x, y;
        if (vk_window_surface_point(nx, dm_ui_ny_back(ny), &x, &y))
          dm_tc_pointer_press(s, x * s->layout.width, y * s->layout.height, DM_BTN_Y);
      }
    }
  }
  if (add_at > 0 && now >= add_at) {
    dm_points_info points; dm_points_get(&points);
    if (points.tree_open && points.sel_skill >= 0 && !dm_cursor_pending()) {
      add_at = 0;
      if (points.sel_block == 0 || points.sel_block == 5) dm_skill_add_selected();
    } else if (now > add_at + 1 || !points.tree_open) add_at = 0;
  }
  // 原说明框的停留与停靠规则；绑定和仍在处理的点击不提前移走光标。
  // Original tooltip lifetime/parking rules. Binding and pending clicks keep
  // the cursor in place until the game has consumed them.
  if (!s->navigation_mode) tooltip_until = -2;
  else if (s->pointer_slot >= 0) tooltip_until = -1;
  else if (tooltip_until == -2) {
    if (dm_ui_free_cursor()) tooltip_until = now + .3;
  } else if (tooltip_until > 0 && now >= tooltip_until && !s->holding &&
             !dm_ui_bind_active() && !dm_ui_touch_active() && !s->pulse_button &&
             !dm_tc_pulse_pending(s) && !dm_cursor_pending()) {
    float x, y;
    if (vk_window_surface_point(.02f, .5f, &x, &y)) {
      s->pointer_valid = 1;
      s->pointer_x = x * s->layout.width; s->pointer_y = y * s->layout.height;
    }
    tooltip_until = 0;
  }
  float nx, ny;
  dm_tc_finger *pointer = s->pointer_slot >= 0 && s->pointer_slot < DM_TC_MAX_TOUCH
                           ? &s->finger[s->pointer_slot] : NULL;
  // 原 controls 用 (2,2) 把货架说明框移走；这里必须是游戏角落，不能落进 Surface 黑边。
  // The original controls park at (2,2) to hide shelf tooltips. Park inside
  // the game rectangle, rather than inside the Android Surface letterbox.
  if (s->pointer_valid && pointer && pointer->active && pointer->shop_drag &&
      s->pointer_x == 2 && s->pointer_y == 2 &&
      s->layout.width > 0 && s->layout.height > 0)
    dm_cursor_set_screen(2 / s->layout.width, 2 / s->layout.height, 1, 1);
  else if (s->pointer_valid && point(s, s->pointer_x, s->pointer_y, &nx, &ny))
    dm_cursor_set_screen(nx, dm_ui_ny(ny), 1, 1);
  else if (s->navigation_mode) dm_cursor_clear();
  int fingers = front_slot >= 0;
  for (unsigned i = 0; i < DM_TC_MAX_TOUCH; ++i) fingers |= s->finger[i].active;
  dm_ui_hand_idle(!fingers && !s->pulse_button && !dm_tc_pulse_pending(s) &&
                  !dm_cursor_pending() && !dm_ui_touch_active());
}
void dm_android_ui_cancel(void) {
  mtl_ui_ghost(0, 0, 0, NULL, 0, 0);
  ghost_on = ghost_dim = 0;
  if (drag || front_drag) dm_ui_drag_end();
  dm_ui_touch(3, 0, 0); dm_cursor_clear();
  front_slot = pending_slot = -1;
  binding_slot = -1; selected_point = 0; add_at = 0;
  map_slot = map_back_slot = -1; map_world_seq = 0; tooltip_until = -2;
  front_drag = drag = item_asked = release_asked = 0;
}
