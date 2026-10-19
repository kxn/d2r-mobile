// 无图形输入适配验证：复用原 HID/触屏接口，检查状态、按下沿和样式选择。
// Headless input adaptation tests: use original HID/touch interfaces to check
// state, press edges and style selection.
#include "../../hle_hid.c"
static unsigned checks;
static int operation_mode;
int dm_opmode(void) { return operation_mode; }
uint64_t now_ns(void) { return 1000000000; }
uint64_t guest_ns(void) { return now_ns(); }
#define CHECK(c)                                                               \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(c))                                                                  \
      die("HID test failed at line %d: %s", __LINE__, #c);                     \
  } while (0)
int main(void) {
  NpadState state;
  uint32_t handheld = NPAD_HANDHELD, full = 0;
  CHECK(h_hid_GetStyle(&handheld) == STYLE_HANDHELD);
  CHECK(h_hid_GetStyle(&full) == 0);
  dm_input_set(DM_BTN_A, 0, 0, 0, 0);
  dm_input_set(0, 0, 0, 0, 0);
  h_hid_GetHandheld(&state, &handheld);
  CHECK(state.buttons == DM_BTN_A && state.attributes == ATTR_CONNECTED);
  int64_t first = state.sampling;
  h_hid_GetHandheld(&state, &handheld);
  CHECK(state.buttons == 0 && state.sampling == first + 1);
  dm_pad_set(DM_BTN_B, 123, -456, 789, -987);
  h_hid_GetHandheld(&state, &handheld);
  CHECK(state.buttons == DM_BTN_B);
  CHECK(state.lx == 123 && state.ly == -456 && state.rx == 789 &&
        state.ry == -987);
  dm_input_set(DM_BTN_A, 1000, 2000, 0, 0);
  h_hid_GetHandheld(&state, &handheld);
  CHECK(state.buttons == (DM_BTN_A | DM_BTN_B));
  CHECK(state.lx == 1000 && state.ly == 2000 && state.rx == 789);
  int32_t rx, ry;
  dm_input_sampled_right(&rx, &ry);
  CHECK(rx == 789 && ry == -987);
  dm_input_set(0, 0, 0, 0, 0);
  dm_pad_set(0, 0, 0, 0, 0);
  h_hid_GetHandheld(&state, &handheld);
  CHECK(state.buttons == 0);
  operation_mode = 1;
  CHECK(h_hid_GetStyle(&handheld) == 0 &&
        h_hid_GetStyle(&full) == STYLE_FULLKEY);
  h_hid_GetHandheld(&state, &handheld);
  CHECK(state.attributes == 0 && state.buttons == 0);
  h_hid_GetFullKey(&state, &full);
  CHECK(state.attributes == ATTR_CONNECTED);
  TouchState16 touch;
  CHECK(h_hid_GetTouch(&touch, 0) == 0);
  CHECK(h_hid_GetTouch(&touch, 1) == 1);
  printf("{\"backend\":\"native-arm64\",\"hid_checks\":%u,\"result\":"
         "\"passed\"}\n",
         checks);
  return 0;
}
