// Android 只转接触点与画布；手势、摇杆、技能键和脉冲直接使用原 iOS 的纯 C
// 实现。 Android adapts touch events and the canvas; gestures, sticks, skill
// keys and pulses directly use the original iOS frontend's platform-independent
// C code.
#include "../../gfx/vulkan/nvn_vk_window.h"
#include "../../ios/Sources/dm_touch_controls.h"
#include "dm.h"
#include "dm_input.h"
#include "dm_world.h"
#include "dm_cursor.h"
#include "touch_ui.h"
#include "../../ios/Sources/dm_touch_native_art.h"
#include "dm_hud.h"
#include "dm_actions.h"
#include "dm_i18n.h"
extern uint32_t dm_padmode_rebuilds(void);
#include <jni.h>
#include <math.h>
#include <stdatomic.h>
static dm_tc_state controls;
static dm_tc_scene scene;
static _Atomic int navigation = 1, frontend = 1;
static _Atomic int hud_blocked;
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeBack(
    JNIEnv *env, jclass type) {
  (void)env; (void)type;
  if (dm_ui_map_is_full()) { dm_ui_map_full(0); return; }
  // 原脉冲按客体帧收尾；GPU 慢帧不能把返回键吞掉或永久按住。
  // Original pulses expire in guest frames, avoiding missed/stuck keys on slow frames.
  uint64_t key = atomic_load(&navigation) || atomic_load(&frontend) ? DM_BTN_B : DM_BTN_PLUS;
  dm_ui_key_pulse(key, 4);
  LOG("Android system Back -> original %s pulse", key == DM_BTN_B ? "B" : "PLUS");
}
int dm_android_world_input_ready(void) { return !atomic_load(&navigation); }
int dm_android_front_navigation(void) { return atomic_load(&navigation) || atomic_load(&frontend); }
extern int dm_android_gamepad_mode(void), dm_android_gamepad_touched(void);
extern int dm_padmode_busy(void);
extern float dm_android_present_fps(uint64_t now);
static unsigned char swallowed[DM_TC_MAX_TOUCH];
static int initialized;
static uint64_t pulse_epoch, pulse_samples;
static double pulse_start, pulse_seen, pulse_free;
static unsigned world_tap_seen;
static uint64_t scene_epoch = 1, world_epoch = 1;
extern void uimsg_subscribe(void (*)(const char *, const char *, const char *));
static void publish(void) {
  dm_input_set(controls.buttons, controls.lx, controls.ly,
               controls.rx, controls.ry);
  if (controls.diff_request) {
    dm_hud_request_players(controls.diff_request);
    controls.diff_request = 0;
  }
  if (controls.belt_request) {
    int request = controls.belt_request;
    controls.belt_request = 0;
    if (request == 1 || request == 2) dm_belt_expand(request == 1);
    else dm_belt_use(request & 0xf, request >> 4 & 0xf);
  }
  if (controls.world_tap_seq != world_tap_seen) {
    world_tap_seen = controls.world_tap_seq;
    float x, y;
    if (controls.layout.width > 0 && controls.layout.height > 0 &&
        vk_window_world_point(controls.world_tap_x / controls.layout.width,
                              controls.world_tap_y / controls.layout.height, &x,
                              &y)) {
      unsigned sequence = dm_world_tap(x, y);
      LOG("Android original world tap seq=%u at %.4f,%.4f", sequence, x, y);
    }
  }
}
// dm_gamepad.m's padMode callback: a physical controller taking ownership
// cancels touch state before hiding its overlay. All callers are the UI thread.
void dm_android_touch_padmode(int pad) {
  if (pad && initialized) {
    dm_tc_cancel_all(&controls); dm_android_ui_cancel(); pulse_epoch = 0;
    publish();
  }
}
static void ui_message(const char *category, const char *action,
                       const char *argument) {
  // 原消息总线目前由游戏主线程派发；UI 线程只读原子导航结果。
  // The original bus dispatches on the game main thread; the UI thread only
  // reads the atomic navigation result.
  dm_tc_set_panel_state(dm_ui_panel_state());
  int blocked = atomic_load(&hud_blocked);
  if (!strcmp(category, "PanelManager")) {
    int dm_hud_stack_msg(const char *, const char *);
    blocked = dm_hud_stack_msg(action, argument ? argument : "");
    atomic_store(&hud_blocked, blocked);
  }
  int was_game = scene.game;
  int nav = dm_tc_navigation(&scene, category, action, argument);
  if (scene.game && blocked > 0) nav = 1;
  static int previous_nav = 1;
  static uint32_t rebuild;
  uint32_t current = dm_padmode_rebuilds();
  if (was_game != scene.game || rebuild != current) ++world_epoch;
  rebuild = current;
  if (previous_nav != nav) ++scene_epoch;
  previous_nav = nav;
  dm_touch_game_set_scene(!nav, scene_epoch, world_epoch);
  atomic_store(&navigation, dm_tc_panel_mode(nav, &scene, blocked));
  atomic_store(&frontend, !scene.game);
}
void dm_android_touch_ui_init(void) {
  uimsg_subscribe(ui_message);
  if (!dm_touch_game_attach()) LOG("Android: original binding gate unavailable in this rewrite");
  dm_touch_game_live_check(1);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_TouchControls_nativeSize(
    JNIEnv *env, jclass type, jfloat width, jfloat height) {
  (void)env;
  (void)type;
  if (!initialized) {
    dm_tc_init(&controls, width, height);
    controls.mouse_click_mode = 1;
    initialized = 1;
  } else
    dm_tc_resize(&controls, width, height);
  publish();
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_TouchControls_nativeTouch(
    JNIEnv *env, jclass type, jint phase, jint slot, jfloat x, jfloat y) {
  (void)env;
  (void)type;
  if (slot < 0 || slot >= DM_TC_MAX_TOUCH) return;
  if (phase == 0) {
    int was_pad = dm_android_gamepad_touched();
    swallowed[slot] = was_pad || dm_padmode_busy();
  }
  if (swallowed[slot]) {
    if (phase >= 2) swallowed[slot] = 0;
    return;
  }
  double now = now_ns() / 1e9;
  controls.belt_rows = dm_belt_rows(&controls.belt_open);
  dm_tc_set_joy_block(&controls, atomic_load(&hud_blocked) < 0);
  // 菜单普通手指走原鼠标桥，控制钮仍走原手柄；已开始的界面手指要收尾。
  // Ordinary menu fingers use the original mouse bridge; overlay buttons
  // retain controller input. Existing UI fingers are always allowed to end.
  if (dm_android_ui_finger(&controls,
        atomic_load(&frontend), phase, slot, x, y, now)) {
    publish(); return;
  }
  if (phase == 0) {
    dm_tc_begin_at(&controls, slot, x, y, now);
    if (!controls.navigation_mode && slot >= 0 && slot < DM_TC_MAX_TOUCH &&
        controls.finger[slot].active &&
        controls.finger[slot].target == DM_TC_PORTAL) {
      // 复用原 front_ios.m 的请求路径，游戏函数只在服务端拍执行。
      // Same request path as front_ios.m; game calls stay on the server tick.
      void dm_tp_request(void);
      dm_tp_request();
    }
  } else if (phase == 1)
    dm_tc_move_at(&controls, slot, x, y, now);
  else if (phase == 2)
    dm_tc_end_at(&controls, slot, now);
  else
    dm_tc_cancel_finger(&controls, slot);
  publish();
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_TouchControls_nativeReset(
    JNIEnv *env, jclass type) {
  (void)env;
  (void)type;
  dm_tc_cancel_all(&controls);
  memset(swallowed, 0, sizeof swallowed);
  dm_android_ui_cancel();
  pulse_epoch = 0;
  publish();
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_TouchControls_nativeFrame(
    JNIEnv *env, jclass type, jfloatArray output) {
  (void)type;
  if (!initialized || (*env)->GetArrayLength(env, output) != 164)
    return;
  int nav = atomic_load(&navigation);
  int previous_nav = controls.navigation_mode;
  if (controls.navigation_mode != !!nav) dm_android_ui_cancel();
  dm_tc_set_navigation_mode(&controls, nav);
  // Original production UI: panels use direct touch. Debug virtual pad
  // regions are not part of the shipping touch controls.
  int pad = 0;
  uint32_t mask = nav ? (dm_tc_nav_mask(pad && nav != 2) |
                         (nav == 3 ? 1u << DM_TC_MENU : 0)) :
    (((1u << DM_TC_COUNT) - 1) & ~(1u << DM_TC_MAP));
  if (atomic_load(&frontend)) mask = 0;
  // 只在局内由常态进入面板时收全屏图。菜单尚无 AutoMap 对象。
  // Collapse only on the in-run transition into a panel. The frontend menu
  // has no AutoMap object yet.
  if (nav && !previous_nav && dm_ui_map_is_full()) dm_ui_map_full(0);
  // 原回城按钮由宿主发请求，不兼任 ZR 技能键（同 front_ios.m）。
  // The original host portal request must not also press a ZR skill binding.
  controls.layout.target[DM_TC_PORTAL].button = 0;
  dm_tc_enable_targets(&controls, mask);
  double now = now_ns() / 1e9;
  dm_android_ui_tick(&controls, now);
  dm_tc_tick(&controls, now);
  uint64_t samples = dm_input_samples();
  if (controls.pulse_button) {
    if (pulse_epoch != controls.pulse_epoch) {
      pulse_epoch = controls.pulse_epoch;
      pulse_samples = samples;
      pulse_start = now;
      pulse_seen = pulse_free = 0;
    }
    if (!pulse_seen && samples > pulse_samples)
      pulse_seen = now;
    if (pulse_seen && !pulse_free && !dm_cursor_pending()) pulse_free = now;
    if ((pulse_seen && samples - pulse_samples >= 3 &&
         now - pulse_seen >= .12 && !dm_cursor_pending() &&
         (controls.pulse_hold_s <= 0 ||
          (pulse_free && now - pulse_free >= controls.pulse_hold_s))) ||
        now - pulse_start > 2.0 + controls.pulse_hold_s) {
      dm_tc_pulse_end(&controls, pulse_epoch);
      pulse_samples = samples;
      pulse_start = now;
    }
  } else if (dm_tc_pulse_pending(&controls) &&
             (samples - pulse_samples >= 2 || now - pulse_start > 0.5))
    dm_tc_pulse_resume(&controls);
  publish();
  float data[164] = {0};
  for (int i = 0; i < DM_TC_COUNT; i++) {
    const dm_tc_target *t = &controls.layout.target[i];
    data[i * 4] = t->x;
    data[i * 4 + 1] = t->y;
    data[i * 4 + 2] = mask & (1u << i) ? t->r : 0;
    data[i * 4 + 3] = t->button && (controls.buttons & t->button) == t->button;
    if (i == DM_TC_PORTAL) for (int f = 0; f < DM_TC_MAX_TOUCH; ++f)
      if (controls.finger[f].active && controls.finger[f].target == DM_TC_PORTAL) data[i * 4 + 3] = 1;
  }
  data[48] =
      controls.joy_slot >= 0 ? controls.joy_origin_x : controls.layout.joy_x;
  data[49] =
      controls.joy_slot >= 0 ? controls.joy_origin_y : controls.layout.joy_y;
  data[50] = controls.layout.joy_radius;
  data[51] = data[48] + controls.lx / 32767.f * data[50];
  data[52] = data[49] - controls.ly / 32767.f * data[50];
  data[53] = nav;
  dm_tg_snapshot binding = {0}; dm_touch_game_snapshot(&binding);
  uint64_t t = now_ns();
  for (int i = 0; i < 6; ++i) {
    dm_ta_glyph glyph = i == 5 ? dm_ta_skill(359) :
      dm_ta_current_set(&binding, controls.layout.target[i].button, t, controls.skill_set);
    // 与原 DmTouchOverlay 相同：仅视觉可保留过期图；输入仍严格走原新鲜性闸门。
    // Match original visual-only stale retention. Input keeps its original
    // freshness guard and never uses the retained visual as binding evidence.
    if (i < 5 && glyph.status == DM_TA_UNKNOWN && binding.captured_ns &&
        t > binding.captured_ns && t - binding.captured_ns > DM_TG_MAX_AGE_NS)
      glyph = dm_ta_current_set(&binding, controls.layout.target[i].button,
                                binding.captured_ns, controls.skill_set);
    if (i < 5 && glyph.status != DM_TA_EMPTY && glyph.status != DM_TA_UNKNOWN) {
      int charges = 0;
      data[147 + i * 2] = dm_swapskill_slot(glyph.slot, &charges);
      data[148 + i * 2] = charges;
    }
    int n = 54 + i * 10;
    data[n] = glyph.status; data[n + 1] = glyph.atlas;
    dm_ta_frame normal, pressed;
    if (glyph.status == DM_TA_GLYPH &&
        dm_ta_frame_at(glyph.atlas, glyph.normal_frame, &normal) &&
        dm_ta_frame_at(glyph.atlas, glyph.pressed_frame, &pressed)) {
      data[n + 2] = normal.x; data[n + 3] = normal.y;
      data[n + 4] = normal.width; data[n + 5] = normal.height;
      data[n + 6] = pressed.x; data[n + 7] = pressed.y;
      data[n + 8] = pressed.width; data[n + 9] = pressed.height;
    }
  }
  dm_hud_info hud; dm_hud_get(&hud);
  data[114] = hud.valid; data[115] = hud.level; data[116] = hud.exp_permille;
  data[117] = hud.hp; data[118] = hud.hp_max;
  data[119] = hud.mp; data[120] = hud.mp_max;
  for (int i = 0; i < 4; ++i) data[121 + i] = hud.belt[i];
  data[125] = hud.players; data[126] = hud.portal;
  data[127] = binding.scene_epoch; data[128] = binding.world_epoch;
  data[129] = binding.captured_ns && t >= binding.captured_ns ? (t - binding.captured_ns) / 1e6 : -1;
  data[130] = binding.ready; data[131] = binding.selected_bank;
  data[132] = controls.skill_set;
  data[133] = controls.layout.swap.x; data[134] = controls.layout.swap.y;
  data[135] = controls.layout.swap.r;
  data[136] = dm_hud_bar_visible(&hud, nav && nav != 3,
                                atomic_load(&hud_blocked) > 0);
  data[137] = controls.diff_open;
  data[138] = hud.points; data[139] = hud.quest_unread;
  data[140] = hud.dead; data[141] = dm_lang_zh();
  data[142] = atomic_load(&frontend);
  data[143] = (controls.buttons & DM_BTN_PLUS) != 0;
  data[144] = controls.joy_slot >= 0;
  for (int f = 0; f < DM_TC_MAX_TOUCH; ++f)
    if (controls.finger[f].active &&
        hypotf(controls.finger[f].x - controls.layout.swap.x,
               controls.finger[f].y - controls.layout.swap.y) <= controls.layout.swap.r)
      data[145] = 1;
  data[146] = controls.map_cancel_active;
  data[157] = dm_android_gamepad_mode() || dm_padmode_busy();
  data[158] = dm_android_present_fps(t);
  (*env)->SetFloatArrayRegion(env, output, 0, 164, data);
}
JNIEXPORT jboolean JNICALL Java_com_d2rmobile_nativeport_TouchControls_nativeGhost(
    JNIEnv *env, jclass type, jfloatArray output) {
  (void)type;
  float rect[4];
  if ((*env)->GetArrayLength(env, output) != 4 || !dm_android_ui_ghost_rect(rect)) return JNI_FALSE;
  (*env)->SetFloatArrayRegion(env, output, 0, 4, rect);
  return JNI_TRUE;
}
JNIEXPORT jobjectArray JNICALL Java_com_d2rmobile_nativeport_TouchControls_nativeLabels(
    JNIEnv *env, jclass type) {
  (void)type;
  const char *keys[] = {"dmHudLife", "dmHudMana", "dmTcNavConfirm",
    "dmTcNavBack", "dmTcNavAction", "dmTcNavBind", "dmTcNavNext", "dmTcMap", "dmTcCancel"};
  jclass string = (*env)->FindClass(env, "java/lang/String");
  if (!string) return NULL;
  jobjectArray labels = (*env)->NewObjectArray(env, 9, string, NULL);
  if (!labels) return NULL;
  for (int i = 0; i < 9; ++i) {
    jstring value = (*env)->NewStringUTF(env, dm_ui(keys[i]));
    if (!value) return NULL;
    (*env)->SetObjectArrayElement(env, labels, i, value);
    (*env)->DeleteLocalRef(env, value);
  }
  return labels;
}
