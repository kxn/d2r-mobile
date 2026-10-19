// Android 输入只发布到原 hle_hid.c，按钮定义仍只有 dm_input.h 一份。
// Android input only publishes to original hle_hid.c; dm_input.h remains the
// single definition of guest button bits.
#include "dm_input.h"
#include <android/keycodes.h>
#include <jni.h>
#include <math.h>
#include <string.h>
// Same ownership, hysteresis and merge policy as ios/Sources/dm_gamepad.m.
// Android reports letter-labelled KEYCODE_BUTTON_*; no positional remapping.
enum { PAD_SLOTS = 6 };
static struct pad_state {
  int used, device;
  unsigned char held[512];
  uint64_t keys, axes_buttons;
  int32_t sticks[4];
} pads[PAD_SLOTS];
static int pad_mode, need_center, stick_active[2], portal_owned;
static uint64_t previous_buttons, previous_portal;
extern int dm_android_front_navigation(void);
extern void dm_padmode_request(int);
extern void dm_android_touch_padmode(int);
static struct pad_state *pad_for(int device, int create) {
  struct pad_state *free_slot = NULL;
  for (int i=0;i<PAD_SLOTS;++i) {
    if (pads[i].used && pads[i].device==device) return &pads[i];
    if (!pads[i].used && !free_slot) free_slot=&pads[i];
  }
  if (create && free_slot) { memset(free_slot,0,sizeof(*free_slot));free_slot->used=1;free_slot->device=device;return free_slot; }
  return NULL;
}
static void set_mode(int pad) {
  if (pad_mode==pad) return;
  pad_mode=pad; dm_padmode_request(pad); dm_android_touch_padmode(pad);
}
int dm_android_gamepad_mode(void) { return pad_mode; }
static void publish(void) {
  uint64_t buttons=0;int32_t ax[4]={0};double best[2]={0};
  for(int i=0;i<PAD_SLOTS;++i) if(pads[i].used) {
    buttons |= pads[i].keys | pads[i].axes_buttons;
    for(int k=0;k<2;++k) {
      double magnitude=hypot(pads[i].sticks[k*2],pads[i].sticks[k*2+1]);
      if(magnitude>best[k]) {best[k]=magnitude;ax[k*2]=pads[i].sticks[k*2];ax[k*2+1]=pads[i].sticks[k*2+1];}
    }
  }
  for(int k=0;k<2;++k) stick_active[k]=best[k]/DM_STICK_MAX >= (stick_active[k]?.15:.25);
  if(!stick_active[0]&&!stick_active[1]) need_center=0;
  if((buttons&~previous_buttons)||((stick_active[0]||stick_active[1])&&!need_center))set_mode(1);
  previous_buttons=buttons;
  if(!pad_mode||!stick_active[0])ax[0]=ax[1]=0;
  if(!pad_mode||!stick_active[1])ax[2]=ax[3]=0;
  // Upstream's ZR ownership lasts through release. Panels retain original ZR.
  if((buttons&DM_BTN_ZR)&&!(previous_portal&DM_BTN_ZR)) {
    int dm_freetp_zr_free(int);void dm_tp_request(void);
    portal_owned=!dm_android_front_navigation()&&dm_freetp_zr_free((buttons&DM_BTN_ZL)!=0);
    if(portal_owned)dm_tp_request();
  }
  previous_portal=buttons;
  if(portal_owned)buttons&=~(uint64_t)DM_BTN_ZR;
  if(!(previous_portal&DM_BTN_ZR))portal_owned=0;
  dm_pad_set(buttons,ax[0],ax[1],ax[2],ax[3]);
}
int dm_android_gamepad_touched(void) {
  int was=pad_mode;
  if(was){need_center=1;set_mode(0);publish();}
  return was;
}
uint64_t dm_android_button(int key) {
  switch (key) {
  case AKEYCODE_BUTTON_A:
  case AKEYCODE_ENTER:
  case AKEYCODE_SPACE:
    return DM_BTN_A;
  case AKEYCODE_BUTTON_B:
  case AKEYCODE_ESCAPE:
    return DM_BTN_B;
  case AKEYCODE_BUTTON_X:
    return DM_BTN_X;
  case AKEYCODE_BUTTON_Y:
    return DM_BTN_Y;
  case AKEYCODE_BUTTON_THUMBL:
    return DM_BTN_LS;
  case AKEYCODE_BUTTON_THUMBR:
    return DM_BTN_RS;
  case AKEYCODE_BUTTON_L1:
    return DM_BTN_L;
  case AKEYCODE_BUTTON_R1:
    return DM_BTN_R;
  case AKEYCODE_BUTTON_L2:
    return DM_BTN_ZL;
  case AKEYCODE_BUTTON_R2:
    return DM_BTN_ZR;
  case AKEYCODE_BUTTON_START:
  case AKEYCODE_MENU:
    return DM_BTN_PLUS;
  case AKEYCODE_BUTTON_SELECT:
    return DM_BTN_MINUS;
  case AKEYCODE_DPAD_LEFT:
    return DM_BTN_LEFT;
  case AKEYCODE_DPAD_UP:
    return DM_BTN_UP;
  case AKEYCODE_DPAD_RIGHT:
    return DM_BTN_RIGHT;
  case AKEYCODE_DPAD_DOWN:
    return DM_BTN_DOWN;
  default:
    return 0;
  }
}
JNIEXPORT jboolean JNICALL Java_com_d2rmobile_nativeport_GameInput_nativeKey(
    JNIEnv *env, jclass type, jint device, jint code, jboolean down) {
  (void)env;(void)type;
  uint64_t button=dm_android_button(code);
  if(!button || code<0 || code>=512)return JNI_FALSE;
  struct pad_state *pad=pad_for(device,down!=0);
  if(!pad)return JNI_TRUE;
  pad->held[code]=down!=0;pad->keys=0;
  for(int i=0;i<512;++i)if(pad->held[i])pad->keys|=dm_android_button(i);
  publish();return JNI_TRUE;
}
static int32_t axis(float value) {
  if(!isfinite(value))return 0;
  return (int32_t)lroundf(fmaxf(-1,fminf(1,value))*DM_STICK_MAX);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_GameInput_nativeAxes(
    JNIEnv *env,jclass type,jint device,jfloat lx,jfloat ly,jfloat rx,jfloat ry,
    jfloat hx,jfloat hy,jfloat lt,jfloat rt) {
  (void)env;(void)type;
  struct pad_state *pad=pad_for(device,1);if(!pad)return;
  pad->sticks[0]=axis(lx);pad->sticks[1]=axis(ly);pad->sticks[2]=axis(rx);pad->sticks[3]=axis(ry);
  pad->axes_buttons=(hx<-.5f?DM_BTN_LEFT:0)|(hx>.5f?DM_BTN_RIGHT:0)|
    (hy<-.5f?DM_BTN_UP:0)|(hy>.5f?DM_BTN_DOWN:0)|(lt>.5f?DM_BTN_ZL:0)|(rt>.5f?DM_BTN_ZR:0);
  publish();
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_GameInput_nativeRemoved(
    JNIEnv *env,jclass type,jint device) {
  (void)env;(void)type;
  struct pad_state *pad=pad_for(device,0);if(pad)memset(pad,0,sizeof(*pad));publish();
  for(int i=0;i<PAD_SLOTS;++i)if(pads[i].used)return;
  set_mode(0);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_GameInput_nativeReset(JNIEnv *env,jclass type) {
  (void)env;(void)type;
  memset(pads,0,sizeof(pads));memset(stick_active,0,sizeof(stick_active));
  need_center=portal_owned=0;previous_buttons=previous_portal=0;
  publish();set_mode(0);
}
