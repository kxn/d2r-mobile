// Functional Android controller adapter test. Host/device independent: original
// JNI entry points are exercised, while the existing HLE publication boundary
// is observed without a game image or an invented controller implementation.
#include <assert.h>
#include <stdio.h>
#include "../input.c"
static uint64_t observed;
static int32_t observed_axes[4];
static int mode_request, touch_cancel, nav, portal_free, portal_requests;
void dm_pad_set(uint64_t b,int32_t lx,int32_t ly,int32_t rx,int32_t ry) {
  observed=b;observed_axes[0]=lx;observed_axes[1]=ly;observed_axes[2]=rx;observed_axes[3]=ry;
}
void dm_padmode_request(int p) {mode_request=p;}
void dm_android_touch_padmode(int p) {touch_cancel+=p;}
int dm_android_front_navigation(void) {return nav;}
int dm_freetp_zr_free(int zl) {(void)zl;return portal_free;}
void dm_tp_request(void) {++portal_requests;}
static void key(int d,int c,int down) {assert(Java_com_d2rmobile_nativeport_GameInput_nativeKey(NULL,NULL,d,c,down));}
static void axes(int d,float x,float y,float rx,float ry,float hx,float hy,float lt,float rt) {
  Java_com_d2rmobile_nativeport_GameInput_nativeAxes(NULL,NULL,d,x,y,rx,ry,hx,hy,lt,rt);
}
static void reset(void) {Java_com_d2rmobile_nativeport_GameInput_nativeReset(NULL,NULL);}
int main(void) {
  const int codes[]={AKEYCODE_BUTTON_A,AKEYCODE_BUTTON_B,AKEYCODE_BUTTON_X,AKEYCODE_BUTTON_Y,
    AKEYCODE_BUTTON_THUMBL,AKEYCODE_BUTTON_THUMBR,AKEYCODE_BUTTON_L1,AKEYCODE_BUTTON_R1,
    AKEYCODE_BUTTON_L2,AKEYCODE_BUTTON_R2,AKEYCODE_BUTTON_START,AKEYCODE_BUTTON_SELECT,
    AKEYCODE_DPAD_LEFT,AKEYCODE_DPAD_UP,AKEYCODE_DPAD_RIGHT,AKEYCODE_DPAD_DOWN};
  for(int i=0;i<16;++i) {key(1,codes[i],1);assert(observed==(1u<<i));key(1,codes[i],0);assert(!observed);}
  key(1,AKEYCODE_BUTTON_A,1);key(2,AKEYCODE_BUTTON_A,1);key(1,AKEYCODE_BUTTON_A,0);assert(observed==1);
  key(1,AKEYCODE_BUTTON_B,1);assert(observed==3);
  Java_com_d2rmobile_nativeport_GameInput_nativeRemoved(NULL,NULL,2);assert(observed==2);
  key(1,AKEYCODE_BUTTON_B,0);assert(!observed);
  key(1,AKEYCODE_ENTER,1);key(1,AKEYCODE_SPACE,1);key(1,AKEYCODE_ENTER,0);assert(observed==1);
  key(1,AKEYCODE_SPACE,0);assert(!observed);
  axes(1,.8f,-.4f,0,0,-1,-1,1,1);assert(observed==0x3300);
  assert(observed_axes[0]==26214&&observed_axes[1]==-13107);
  axes(2,-.9f,.5f,.3f,-.4f,0,0,0,0);assert(observed_axes[0]==-29490&&observed_axes[1]==16384);
  assert(observed_axes[2]==9830&&observed_axes[3]==-13107);
  axes(2,0,0,0,0,0,0,0,0);axes(1,0,0,0,0,0,0,0,0);
  assert(!observed && !observed_axes[0]);
  reset();axes(1,.24f,0,0,0,0,0,0,0);assert(!observed_axes[0]);
  axes(1,.26f,0,0,0,0,0,0,0);assert(observed_axes[0]>0&&mode_request==1);
  axes(1,.20f,0,0,0,0,0,0,0);assert(observed_axes[0]>0);
  axes(1,.14f,0,0,0,0,0,0,0);assert(!observed_axes[0]);
  axes(1,.8f,0,0,0,0,0,0,0);assert(dm_android_gamepad_touched()==1&&mode_request==0&&!observed_axes[0]);
  axes(1,.9f,0,0,0,0,0,0,0);assert(!observed_axes[0]&&!dm_android_gamepad_mode());
  axes(1,0,0,0,0,0,0,0,0);axes(1,.8f,0,0,0,0,0,0,0);assert(observed_axes[0]>0);
  assert(dm_android_gamepad_touched()==1);key(1,AKEYCODE_BUTTON_X,1);assert(observed==4&&dm_android_gamepad_mode());
  reset();assert(!observed&&!observed_axes[0]&&!dm_android_gamepad_mode());
  nav=0;portal_free=1;key(1,AKEYCODE_BUTTON_R2,1);assert(!observed&&portal_requests==1);
  nav=1;key(2,AKEYCODE_BUTTON_A,1);assert(observed==1);key(1,AKEYCODE_BUTTON_R2,0);key(2,AKEYCODE_BUTTON_A,0);
  key(1,AKEYCODE_BUTTON_R2,1);assert(observed==0x200&&portal_requests==1);key(1,AKEYCODE_BUTTON_R2,0);
  reset();assert(touch_cancel>0);puts("Android original controller policy: PASS");
  return 0;
}
