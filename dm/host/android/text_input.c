// 原 hle_swkbd.c 的字符规则不变：游戏线程等待，Android UI
// 线程取请求并交还文本。 Keep original hle_swkbd.c character rules: the game
// thread waits while the Android UI thread takes a request and returns text,
// without attaching guest threads to ART.
#include "dm.h"
#include "platform.h"
#include <jni.h>
static pthread_mutex_t keyboard_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t keyboard_changed = PTHREAD_COND_INITIALIZER;
static struct {
  const char *title, *subtitle, *guide, *initial;
  char *output;
  int capacity, maximum, numeric, taken, complete, result;
  uint64_t id;
} request;
static uint64_t next_request;
extern size_t swkbd_u8_to_u16(const char *, size_t, uint16_t *, size_t);
extern size_t swkbd_u16_to_u8(const uint16_t *, size_t, char *, size_t);
extern int g_swkbd_numeric_max;
static jstring text(JNIEnv *env, const char *utf8) {
  uint16_t converted[2048];
  size_t n = swkbd_u8_to_u16(utf8, 2047, converted, 2048);
  return (*env)->NewString(env, converted, n);
}
JNIEXPORT jobjectArray JNICALL
Java_com_d2rmobile_nativeport_MainActivity_nativePollText(JNIEnv *env,
                                                          jclass type) {
  (void)type;
  pthread_mutex_lock(&keyboard_lock);
  jobjectArray result = NULL;
  if (request.id && !request.taken && !request.complete) {
    jclass strings = (*env)->FindClass(env, "java/lang/String");
    result = (*env)->NewObjectArray(env, 7, strings, NULL);
    if (result) {
      char maximum[24], number[24];
      snprintf(maximum, sizeof maximum, "%d", request.maximum);
      snprintf(number, sizeof number, "%llu", (unsigned long long)request.id);
      const char *parts[] = {request.title, request.subtitle,
                             request.guide, request.initial,
                             maximum,       request.numeric ? "1" : "0",
                             number};
      for (int i = 0; i < 7; i++)
        (*env)->SetObjectArrayElement(env, result, i, text(env, parts[i]));
      request.taken = 1;
    }
  }
  pthread_mutex_unlock(&keyboard_lock);
  return result;
}
JNIEXPORT void JNICALL
Java_com_d2rmobile_nativeport_MainActivity_nativeCompleteText(JNIEnv *env,
                                                              jclass type,
                                                              jlong id,
                                                              jstring value) {
  (void)type;
  pthread_mutex_lock(&keyboard_lock);
  if (request.id && (!id || request.id == (uint64_t)id) && !request.complete) {
    request.result = value ? 1 : 0;
    if (value) {
      const jchar *chars = (*env)->GetStringChars(env, value, NULL);
      if (!chars)
        die("Android text result unavailable");
      swkbd_u16_to_u8(chars, (*env)->GetStringLength(env, value),
                      request.output, request.capacity);
      (*env)->ReleaseStringChars(env, value, chars);
    }
    request.complete = 1;
    pthread_cond_broadcast(&keyboard_changed);
  }
  pthread_mutex_unlock(&keyboard_lock);
}
int front_text_input(const char *title, const char *sub, const char *guide,
                     const char *initial, int max_chars, char *out,
                     int out_cap) {
  pthread_mutex_lock(&keyboard_lock);
  while (request.id)
    pthread_cond_wait(&keyboard_changed, &keyboard_lock);
  request.title = title;
  request.subtitle = sub;
  request.guide = guide;
  request.initial = initial;
  request.maximum = max_chars;
  request.numeric = g_swkbd_numeric_max >= 0;
  request.output = out;
  request.capacity = out_cap;
  request.taken = request.complete = request.result = 0;
  request.id = ++next_request;
  while (!request.complete)
    pthread_cond_wait(&keyboard_changed, &keyboard_lock);
  int result = request.result;
  request.id = 0;
  pthread_cond_broadcast(&keyboard_changed);
  pthread_mutex_unlock(&keyboard_lock);
  dm_tsd_check();
  return result;
}
