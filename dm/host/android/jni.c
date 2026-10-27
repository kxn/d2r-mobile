// Android APK 到原宿主的入口；不创建另一套装载器或客体执行器。
// Android APK entry into the original host; no separate loader or guest CPU
// executor.
#include "../../gfx/vulkan/nvn_vk_window.h"
#include "../../gfx/vulkan/nvn_vk_renderdoc.h"
#include "../../gfx/vulkan/nvn_vk_loader.h"
#include "dm.h"
#include "guest_heap_startup.h"
#include "platform.h"
#include "display.h"
#include "dm_performance_hint.h"
#include "hle.h"
#include "dm_cursor.h"
#include "dm_custom.h"
#include <android/native_window_jni.h>
#include <jni.h>
#include <stdatomic.h>
#include "fixture_output.h"
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>
extern int dm_android_main(int argc, char **argv);
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeRenderdocCapture(
    JNIEnv *env, jclass klass, jstring directory) {
  (void)klass;
  const char *path = (*env)->GetStringUTFChars(env, directory, NULL);
  if (!path) return;
  int accepted = vk_renderdoc_request(path);
  LOG("[renderdoc] capture request accepted=%d", accepted);
  (*env)->ReleaseStringUTFChars(env, directory, path);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeVmaMemoryDumpRequest(
    JNIEnv *env,jclass klass) {
  (void)env;(void)klass;extern int dm_gfx_vma_dump_request(void);
  LOG("[vulkan VMA dump] explicit request accepted=%d; consumes at next normal suspend",dm_gfx_vma_dump_request());
}
extern void vk_graphics_byte_hash(int legacy);
extern void vk_graphics_prepare_mode(int mode);
extern void vk_readback_coherent_reference(int reference);
extern void vk_cpu_copy_reference(int reference);
extern void vk_driver_constant_reference(int reference);
extern void vk_sample_liveness_reference(int reference);
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeSampleLivenessReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;vk_sample_liveness_reference(reference);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeDriverConstantReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;vk_driver_constant_reference(reference);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeCpuCopyReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;vk_cpu_copy_reference(reference);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativePerformanceHintReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;dm_android_hint_reference(reference);
}
extern void vk_storage_bias_reference(int reference);
extern void vk_pool_sync_reference(int reference);
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativePoolLookupReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;
  extern void vk_pool_lookup_reference(int);
  vk_pool_lookup_reference(reference);
}
extern void vk_pool_mirror_reference(int reference);
extern void vk_pool_window_bytes(uint64_t bytes);
extern void mtl_pool_defer_publication(int enabled);
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativePoolDeferPublication(
    JNIEnv *env,jclass klass,jboolean enabled) {
  (void)env;(void)klass;mtl_pool_defer_publication(enabled);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativePoolWindowBytes(
    JNIEnv *env,jclass klass,jlong bytes) {
  (void)klass;
  if(bytes<0 || (bytes && (bytes<4096 || ((uint64_t)bytes&((uint64_t)bytes-1))))) {
    jclass error=(*env)->FindClass(env,"java/lang/IllegalArgumentException");
    if(error)(*env)->ThrowNew(env,error,"pool_window_bytes must be 0 or a power of two >= 4096");
    return;
  }
  vk_pool_window_bytes((uint64_t)bytes);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeTextureBindingReference(
    JNIEnv *env, jclass klass, jboolean reference) {
  (void)env; (void)klass;
  extern void vk_texture_binding_reference(int);
  vk_texture_binding_reference(reference);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeTexelStorage(
    JNIEnv *env, jclass klass, jboolean enabled) {
  (void)env; (void)klass;
  extern void vk_texel_storage_enabled(int);
  vk_texel_storage_enabled(enabled);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeShaderPresent(
    JNIEnv *env, jclass klass, jboolean enabled) {
  (void)env; (void)klass;
  extern void vk_window_shader_present(int);
  vk_window_shader_present(enabled);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeSamplerLookupReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;
  extern void vk_sampler_lookup_reference(int);
  vk_sampler_lookup_reference(reference);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeUploadScanReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;
  extern void vk_upload_scan_reference(int);
  vk_upload_scan_reference(reference);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativePoolMirrorReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;vk_pool_mirror_reference(reference);
}
extern void vk_image_layout_reference(int reference);
extern void vk_storage_usage_reference(int reference);
extern void vk_attachment_usage_reference(int reference);
extern void vk_depth_layout_reference(int reference);
extern void vk_compute_layout_reference(int reference);
extern void vk_readonly_depth_reference(int reference);
extern void vk_render_clear_reference(int reference);
extern void vk_texture_dirty_reference(int reference);
extern void vk_zero_draw_reference(int reference);
extern void vk_bloom_quarter_enabled(int enabled);
extern void vk_nofxaa_enabled(int enabled);
extern void vk_uniform_specialization_enabled(int enabled);
extern void vk_uniform_predicate_enabled(int enabled);
extern void vk_mip_clear_enable(int enabled);
extern void vk_dynamic_uniform_reference(int reference);
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeDynamicUniformNarrowReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;
  extern void vk_dynamic_uniform_narrow_reference(int);
  vk_dynamic_uniform_narrow_reference(reference);
}
extern void vk_descriptor_batch_reference(int reference);
extern void vk_descriptor_reuse_reference(int reference);
extern void vk_descriptor_history_reference(int reference);
extern void vk_window_mailbox_present(int enabled);
extern void vk_descriptor_template_reference(int reference);
extern void vk_loader_set_driver(const char *provider,const char *directory,const char *hooks);
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeVulkanDriver(
    JNIEnv *env,jclass klass,jstring provider,jstring directory,jstring hooks) {
  (void)klass;
  const char *kind=provider?(*env)->GetStringUTFChars(env,provider,NULL):NULL;
  if(provider && !kind) return;
  const char *driver=directory?(*env)->GetStringUTFChars(env,directory,NULL):NULL;
  if(directory && !driver) {
    if(kind) (*env)->ReleaseStringUTFChars(env,provider,kind);
    return;
  }
  const char *libraries=hooks?(*env)->GetStringUTFChars(env,hooks,NULL):NULL;
  if(hooks && !libraries) {
    if(driver) (*env)->ReleaseStringUTFChars(env,directory,driver);
    if(kind) (*env)->ReleaseStringUTFChars(env,provider,kind);
    return;
  }
  vk_loader_set_driver(kind,driver,libraries);
  if(libraries) (*env)->ReleaseStringUTFChars(env,hooks,libraries);
  if(driver) (*env)->ReleaseStringUTFChars(env,directory,driver);
  if(kind) (*env)->ReleaseStringUTFChars(env,provider,kind);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeTextureDirtyReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;vk_texture_dirty_reference(reference);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeZeroDrawReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;vk_zero_draw_reference(reference);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeBloomQuarterEnabled(
    JNIEnv *env,jclass klass,jboolean enabled) {
  (void)env;(void)klass;vk_bloom_quarter_enabled(enabled);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeMailboxPresent(
    JNIEnv *env,jclass klass,jboolean enabled) {
  (void)env;(void)klass;vk_window_mailbox_present(enabled);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativePresentWorkerReference(
    JNIEnv *env,jclass klass,jboolean reference,jboolean shared_queue) {
  (void)env;(void)klass;vk_window_present_worker_mode(reference?0:shared_queue?2:1);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeDescriptorTemplateReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;vk_descriptor_template_reference(reference);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeDescriptorHistoryReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;vk_descriptor_history_reference(reference);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeDescriptorReuseReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;vk_descriptor_reuse_reference(reference);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeDescriptorBatchReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;vk_descriptor_batch_reference(reference);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeDynamicUniformReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;vk_dynamic_uniform_reference(reference);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeUniformSpecializationEnabled(
    JNIEnv *env,jclass klass,jboolean enabled) {
  (void)env;(void)klass;vk_uniform_specialization_enabled(enabled);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeUniformPredicateEnabled(
    JNIEnv *env,jclass klass,jboolean enabled) {
  (void)env;(void)klass;vk_uniform_predicate_enabled(enabled);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeDeferredMipClearsEnabled(
    JNIEnv *env,jclass klass,jboolean enabled) {
  (void)env;(void)klass;vk_mip_clear_enable(enabled);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeNoFxaaEnabled(
    JNIEnv *env,jclass klass,jboolean enabled) {
  (void)env;(void)klass;vk_nofxaa_enabled(enabled);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeClearAttachmentReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;vk_render_clear_reference(reference);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeReadonlyDepthReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;vk_readonly_depth_reference(reference);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeComputeLayoutReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;vk_compute_layout_reference(reference);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeDepthLayoutReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;vk_depth_layout_reference(reference);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeAttachmentUsageReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;vk_attachment_usage_reference(reference);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeStorageUsageReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;vk_storage_usage_reference(reference);
}
extern void vk_stream_submit_reference(int reference);
extern void vk_store_descriptor_reference(int reference);
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeStoreDescriptorReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;vk_store_descriptor_reference(reference);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeStreamSubmitReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;vk_stream_submit_reference(reference);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeImageLayoutReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;vk_image_layout_reference(reference);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeSampledArrayLayoutReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  extern void vk_sampled_array_layout_reference(int);
  (void)env;(void)klass;vk_sampled_array_layout_reference(reference);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeBufferMemoryReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  extern void vk_buffer_memory_reference(int);
  (void)env;(void)klass;vk_buffer_memory_reference(reference);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativePoolSyncReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;vk_pool_sync_reference(reference);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeStorageBiasReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;vk_storage_bias_reference(reference);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeReadbackCoherentReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;vk_readback_coherent_reference(reference);
}
extern void vk_cover_wait_reference(int reference);
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeCoverWaitReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;vk_cover_wait_reference(reference);
}
extern void vk_graphics_stage_reference(int reference);
extern void vk_submission_barrier_reference(int reference);
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeSubmissionBarrierReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;vk_submission_barrier_reference(reference);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeGraphicsStageReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;vk_graphics_stage_reference(reference);
}
extern void vk_counter_span_mode(int enabled);
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeQueryPerDrawReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)env;(void)klass;vk_counter_span_mode(!reference);
}
// 原管线哈希的同进程性能对照入口；不改变游戏、管线键或 NVN 状态。
// Same-process control for the previous pipeline hash; game/key/NVN state stay intact.
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativePipelineByteHash(
    JNIEnv *env,jclass klass,jboolean legacy) {
  (void)env;(void)klass;vk_graphics_byte_hash(legacy);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativePipelinePrepareMode(
    JNIEnv *env,jclass klass,jint mode) {
  (void)env;(void)klass;vk_graphics_prepare_mode(mode);
}
typedef struct { int64_t offset; int isolated; } tls_probe;
static void *tls_worker(void *context) {
  tls_probe *probe = context;
  probe->offset = dm_tsd_offset(g_key18);
  probe->isolated = dm_tsd_get(g_key18) == NULL;
  dm_tsd_set(g_key18, (void *)0x5731);
  probe->isolated &= dm_tsd_get(g_key18) == (void *)0x5731;
  return NULL;
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeProbe(
    JNIEnv *env, jclass klass, jstring directory) {
  (void)klass;
  const char *path = (*env)->GetStringUTFChars(env, directory, NULL);
  if (!path)
    return;
  int64_t offset = dm_tsd_offset(g_key18);
  dm_tsd_set(g_key18, (void *)0x1357);
  pthread_t thread;
  tls_probe worker = {0};
  if (pthread_create(&thread, NULL, tls_worker, &worker) ||
      pthread_join(thread, NULL) || !worker.isolated ||
      dm_tsd_get(g_key18) != (void *)0x1357)
    die("APK TLS thread isolation failed");
  dm_tsd_set(g_key18, NULL);
#ifndef DM_TLS_DESC
  if (worker.offset != offset) die("APK initial TLS differs across threads");
#endif
  char file[2048];
  snprintf(file, sizeof file, "%s/tls.json", path);
  FILE *f = fopen(file, "w");
  if (!f)
    die("APK TLS report open");
  fprintf(f,
          "{\"native_tls_preloaded\":%s,\"offset\":%lld,\"stable_across_"
          "threads\":%s,\"thread_isolation\":true,\"tls_model\":\"%s\"}\n",
#ifdef DM_TLS_DESC
          "false", (long long)offset, worker.offset == offset ? "true" : "false", "tlsdesc"
#else
          "true", (long long)offset, "true", "initial-exec"
#endif
  );
  fclose(f);
  (*env)->ReleaseStringUTFChars(env, directory, path);
}
static char runtime_root[2048];
static ANativeWindow *window;
static int validate_game, trace_frame;
static int lifecycle_suspended;
// Android mode is a startup choice; never send an in-process mode-change event.
static pthread_mutex_t operation_mode_mutex=PTHREAD_MUTEX_INITIALIZER;
static int operation_mode=1,operation_mode_fixed;
static int pixel_local_startup,pixel_local_shared_ds_startup;
static int shared_guest_heap_startup;
static int shared_lazy_windows_startup;
static int shared_image_raw_observe_startup=1;
static int image_buffer_split_startup=1;
static int state_segments_startup=1;
static int inline_translation_startup;
static int guest_worker_qos_startup;
static unsigned guest_wait_budget_startup;
static int native_buffer_geometry_startup;
static int query_attachment_reference_startup;
static int submission_plan_reference_startup;
static int keep_attachment_stores_startup;
static int render_record_mode_startup;
static unsigned translation_cpu_mask_startup;
static char turnip_autotune_startup[32];
// -1 preserves an external reference experiment; explicit profiles own 0/1.
static int turnip_swapchain_ubwc_startup=-1;
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeQueryAttachmentReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)klass;pthread_mutex_lock(&operation_mode_mutex);
  if(operation_mode_fixed) {
    pthread_mutex_unlock(&operation_mode_mutex);
    jclass error=(*env)->FindClass(env,"java/lang/IllegalStateException");
    if(error)(*env)->ThrowNew(env,error,"query_attachment_reference is fixed before native startup");
    return;
  }
  query_attachment_reference_startup=reference!=0;
  pthread_mutex_unlock(&operation_mode_mutex);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeBufferGeometry(
    JNIEnv *env,jclass klass,jboolean enabled) {
  (void)klass;pthread_mutex_lock(&operation_mode_mutex);
  if(operation_mode_fixed) {
    pthread_mutex_unlock(&operation_mode_mutex);
    jclass error=(*env)->FindClass(env,"java/lang/IllegalStateException");
    if(error)(*env)->ThrowNew(env,error,"native_buffer_geometry is fixed before native startup");
    return;
  }
  native_buffer_geometry_startup=enabled!=0;
  pthread_mutex_unlock(&operation_mode_mutex);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeGuestWorkerQos(
    JNIEnv *env,jclass klass,jboolean enabled) {
  (void)klass;pthread_mutex_lock(&operation_mode_mutex);
  if(operation_mode_fixed) {
    pthread_mutex_unlock(&operation_mode_mutex);
    jclass error=(*env)->FindClass(env,"java/lang/IllegalStateException");
    if(error)(*env)->ThrowNew(env,error,"guest_worker_qos is fixed before native startup");
    return;
  }
  guest_worker_qos_startup=enabled!=0;
  pthread_mutex_unlock(&operation_mode_mutex);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeGuestWaitBudget(
    JNIEnv *env,jclass klass,jint microseconds) {
  (void)klass;pthread_mutex_lock(&operation_mode_mutex);
  if(operation_mode_fixed || microseconds<0 || microseconds>100) {
    pthread_mutex_unlock(&operation_mode_mutex);
    jclass error=(*env)->FindClass(env,"java/lang/IllegalStateException");
    if(error)(*env)->ThrowNew(env,error,"guest wait budget must be 0..100 us before native startup");
    return;
  }
  guest_wait_budget_startup=(unsigned)microseconds;
  pthread_mutex_unlock(&operation_mode_mutex);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeTurnipAutotune(
    JNIEnv *env,jclass klass,jstring selection) {
  (void)klass;
  const char *text=selection?(*env)->GetStringUTFChars(env,selection,NULL):"default";
  if(!text)return;
  int valid=!strcmp(text,"default") || !strcmp(text,"bandwidth") ||
      !strcmp(text,"profiled") || !strcmp(text,"prefer_sysmem");
  pthread_mutex_lock(&operation_mode_mutex);
  int frozen=operation_mode_fixed;
  if(valid && !frozen)strcpy(turnip_autotune_startup,text);
  pthread_mutex_unlock(&operation_mode_mutex);
  if(selection)(*env)->ReleaseStringUTFChars(env,selection,text);
  if(!valid || frozen) {
    jclass error=(*env)->FindClass(env,"java/lang/IllegalArgumentException");
    if(error)(*env)->ThrowNew(env,error,"turnip_autotune requires a valid cold selection");
  }
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeTurnipSwapchainUbwc(
    JNIEnv *env,jclass klass,jint selection) {
  (void)klass;
  pthread_mutex_lock(&operation_mode_mutex);
  int valid=selection>=-1 && selection<=1 && !operation_mode_fixed;
  if(valid)turnip_swapchain_ubwc_startup=selection;
  pthread_mutex_unlock(&operation_mode_mutex);
  if(!valid) {
    jclass error=(*env)->FindClass(env,"java/lang/IllegalArgumentException");
    if(error)(*env)->ThrowNew(env,error,"turnip_swapchain_ubwc requires a cold selection");
  }
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeSubmissionPlanReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)klass;pthread_mutex_lock(&operation_mode_mutex);
  if(operation_mode_fixed) {
    pthread_mutex_unlock(&operation_mode_mutex);
    jclass error=(*env)->FindClass(env,"java/lang/IllegalStateException");
    if(error)(*env)->ThrowNew(env,error,"submission_plan_reference is fixed before native startup");
    return;
  }
  submission_plan_reference_startup=reference!=0;
  pthread_mutex_unlock(&operation_mode_mutex);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeKeepAttachmentStores(
    JNIEnv *env,jclass klass,jboolean enabled) {
  (void)klass;pthread_mutex_lock(&operation_mode_mutex);
  if(operation_mode_fixed) {
    pthread_mutex_unlock(&operation_mode_mutex);
    jclass error=(*env)->FindClass(env,"java/lang/IllegalStateException");
    if(error)(*env)->ThrowNew(env,error,"keep_attachment_stores is fixed before native startup");
    return;
  }
  keep_attachment_stores_startup=enabled!=0;
  pthread_mutex_unlock(&operation_mode_mutex);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeRenderRecordMode(
    JNIEnv *env,jclass klass,jint mode) {
  (void)klass;pthread_mutex_lock(&operation_mode_mutex);
  if(operation_mode_fixed || mode<0 || mode>2) {
    pthread_mutex_unlock(&operation_mode_mutex);
    jclass error=(*env)->FindClass(env,"java/lang/IllegalArgumentException");
    if(error)(*env)->ThrowNew(env,error,"render_record_mode requires a cold 0,1,2 selection");
    return;
  }
  render_record_mode_startup=mode;
  pthread_mutex_unlock(&operation_mode_mutex);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeTranslationCpuMask(
    JNIEnv *env,jclass klass,jint mask) {
  (void)klass;pthread_mutex_lock(&operation_mode_mutex);
  if(operation_mode_fixed || mask<0) {
    pthread_mutex_unlock(&operation_mode_mutex);
    jclass error=(*env)->FindClass(env,"java/lang/IllegalArgumentException");
    if(error)(*env)->ThrowNew(env,error,"translation_cpu_mask is a nonnegative cold setting");
    return;
  }
  translation_cpu_mask_startup=(unsigned)mask;
  pthread_mutex_unlock(&operation_mode_mutex);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeInlineTranslation(
    JNIEnv *env,jclass klass,jboolean enabled) {
  (void)klass;pthread_mutex_lock(&operation_mode_mutex);
  if(operation_mode_fixed) {
    pthread_mutex_unlock(&operation_mode_mutex);
    jclass error=(*env)->FindClass(env,"java/lang/IllegalStateException");
    if(error)(*env)->ThrowNew(env,error,"inline_translation is fixed before native startup");
    return;
  }
  inline_translation_startup=enabled!=0;
  pthread_mutex_unlock(&operation_mode_mutex);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeStateSegments(
    JNIEnv *env,jclass klass,jboolean enabled) {
  (void)klass;pthread_mutex_lock(&operation_mode_mutex);
  if(operation_mode_fixed) {
    pthread_mutex_unlock(&operation_mode_mutex);
    jclass error=(*env)->FindClass(env,"java/lang/IllegalStateException");
    if(error)(*env)->ThrowNew(env,error,"state_segments is fixed before native startup");
    return;
  }
  state_segments_startup=enabled!=0;
  pthread_mutex_unlock(&operation_mode_mutex);
}
static int source_partition_startup;
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeSourcePartition(
    JNIEnv *env,jclass klass,jboolean enabled) {
  (void)klass;pthread_mutex_lock(&operation_mode_mutex);
  if(operation_mode_fixed) {
    pthread_mutex_unlock(&operation_mode_mutex);
    jclass error=(*env)->FindClass(env,"java/lang/IllegalStateException");
    if(error)(*env)->ThrowNew(env,error,"source_partition is fixed before native startup");
    return;
  }
  source_partition_startup=enabled!=0;
  pthread_mutex_unlock(&operation_mode_mutex);
}
static int native_read_group_front_startup=0;
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeReadGroupFront(
    JNIEnv *env,jclass klass,jboolean enabled) {
  (void)klass;pthread_mutex_lock(&operation_mode_mutex);
  if(operation_mode_fixed) {
    pthread_mutex_unlock(&operation_mode_mutex);
    jclass error=(*env)->FindClass(env,"java/lang/IllegalStateException");
    if(error)(*env)->ThrowNew(env,error,"native_read_group_front is fixed before native startup");
    return;
  }
  native_read_group_front_startup=enabled!=0;
  pthread_mutex_unlock(&operation_mode_mutex);
}
static int attachment_job_reuse_startup=1;
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeAttachmentJobReuse(
    JNIEnv *env,jclass klass,jboolean enabled) {
  (void)klass;pthread_mutex_lock(&operation_mode_mutex);
  if(operation_mode_fixed) {
    pthread_mutex_unlock(&operation_mode_mutex);
    jclass error=(*env)->FindClass(env,"java/lang/IllegalStateException");
    if(error)(*env)->ThrowNew(env,error,"attachment_job_reuse is fixed before native startup");
    return;
  }
  attachment_job_reuse_startup=enabled!=0;
  pthread_mutex_unlock(&operation_mode_mutex);
}
static int attachment_raster_run_startup=1;
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeAttachmentRasterRun(
    JNIEnv *env,jclass klass,jboolean enabled) {
  (void)klass;pthread_mutex_lock(&operation_mode_mutex);
  if(operation_mode_fixed) {
    pthread_mutex_unlock(&operation_mode_mutex);
    jclass error=(*env)->FindClass(env,"java/lang/IllegalStateException");
    if(error)(*env)->ThrowNew(env,error,"attachment_raster_run is fixed before native startup");
    return;
  }
  attachment_raster_run_startup=enabled!=0;
  pthread_mutex_unlock(&operation_mode_mutex);
}
static int buffer_consumption_plan_startup=1;
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeBufferConsumptionPlan(
    JNIEnv *env,jclass klass,jboolean enabled) {
  (void)klass;pthread_mutex_lock(&operation_mode_mutex);
  if(operation_mode_fixed) {
    pthread_mutex_unlock(&operation_mode_mutex);
    jclass error=(*env)->FindClass(env,"java/lang/IllegalStateException");
    if(error)(*env)->ThrowNew(env,error,"buffer_consumption_plan is fixed before native startup");
    return;
  }
  buffer_consumption_plan_startup=enabled!=0;
  pthread_mutex_unlock(&operation_mode_mutex);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeImageBufferSplit(
    JNIEnv *env,jclass klass,jboolean enabled) {
  (void)klass;pthread_mutex_lock(&operation_mode_mutex);
  if(operation_mode_fixed) {
    pthread_mutex_unlock(&operation_mode_mutex);
    jclass error=(*env)->FindClass(env,"java/lang/IllegalStateException");
    if(error)(*env)->ThrowNew(env,error,"image_buffer_split is fixed before native startup");
    return;
  }
  image_buffer_split_startup=enabled!=0;
  pthread_mutex_unlock(&operation_mode_mutex);
}
static int driver_span_reference_startup;
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeDriverSpanReference(
    JNIEnv *env,jclass klass,jboolean reference) {
  (void)klass;pthread_mutex_lock(&operation_mode_mutex);
  if(operation_mode_fixed) {
    pthread_mutex_unlock(&operation_mode_mutex);
    jclass error=(*env)->FindClass(env,"java/lang/IllegalStateException");
    if(error)(*env)->ThrowNew(env,error,"driver_span_reference is fixed before native startup");
    return;
  }
  driver_span_reference_startup=reference!=0;
  pthread_mutex_unlock(&operation_mode_mutex);
}
extern int vk_loader_is_initialized(void);
#ifdef DM_VULKAN_NATIVE_RESOURCES
static int native_resource_fixture_validation=1;
static char native_resource_startup_table[4096],native_resource_startup_pack[65],native_resource_startup_producer[65],native_resource_fixture_directory[4096];
#endif
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeNativeResources(
    JNIEnv *env,jclass klass,jstring table,jstring pack,jstring producer,jstring fixture,jboolean validation) {
  (void)klass;(void)validation;
  pthread_mutex_lock(&operation_mode_mutex);
  if(operation_mode_fixed || vk_loader_is_initialized()) {
    pthread_mutex_unlock(&operation_mode_mutex);
    jclass error=(*env)->FindClass(env,"java/lang/IllegalStateException");
    if(error)(*env)->ThrowNew(env,error,"native resources are fixed before native startup");return;
  }
#ifdef DM_VULKAN_NATIVE_RESOURCES
  const char *path=table?(*env)->GetStringUTFChars(env,table,NULL):NULL;
  const char *p=pack?(*env)->GetStringUTFChars(env,pack,NULL):NULL;
  const char *q=producer?(*env)->GetStringUTFChars(env,producer,NULL):NULL;
  const char *f=fixture?(*env)->GetStringUTFChars(env,fixture,NULL):NULL;
  int valid=!table && !pack && !producer && !fixture;
  if(path && p && q && path[0]=='/' && strlen(path)<sizeof native_resource_startup_table &&
      strlen(p)==64 && strlen(q)==64) {
    valid=1;for(unsigned n=0;n<64;n++)
      if(!((p[n]>='0'&&p[n]<='9')||(p[n]>='a'&&p[n]<='f')) ||
         !((q[n]>='0'&&q[n]<='9')||(q[n]>='a'&&q[n]<='f')))valid=0;
  }
  if(fixture && (!f || !path || f[0]!='/' || strlen(f)>=sizeof native_resource_fixture_directory))valid=0;
  if(valid) {
    native_resource_fixture_validation=!!validation;
    snprintf(native_resource_fixture_directory,sizeof native_resource_fixture_directory,"%s",f?f:"");
    snprintf(native_resource_startup_table,sizeof native_resource_startup_table,"%s",path?path:"");
    snprintf(native_resource_startup_pack,sizeof native_resource_startup_pack,"%s",p?p:"");
    snprintf(native_resource_startup_producer,sizeof native_resource_startup_producer,"%s",q?q:"");
  }
  if(path)(*env)->ReleaseStringUTFChars(env,table,path);
  if(p)(*env)->ReleaseStringUTFChars(env,pack,p);
  if(q)(*env)->ReleaseStringUTFChars(env,producer,q);
  if(f)(*env)->ReleaseStringUTFChars(env,fixture,f);
#else
  int valid=!table && !pack && !producer && !fixture;
#endif
  pthread_mutex_unlock(&operation_mode_mutex);
  if((*env)->ExceptionCheck(env))return;
  if(!valid) {
    jclass error=(*env)->FindClass(env,"java/lang/IllegalArgumentException");
    if(error)(*env)->ThrowNew(env,error,"native resource build option/path/complete digests required");
  }
}
#ifdef DM_VULKAN_NATIVE_RESOURCES
static void native_resource_environment(void) {
  if(native_resource_startup_table[0]) {
    if(!shared_guest_heap_startup)die("Native resources require the shared guest heap startup policy");
    if(setenv("DM_VULKAN_NATIVE_RESOURCE_TABLE",native_resource_startup_table,1) ||
       setenv("DM_VULKAN_NATIVE_PACK_SHA256",native_resource_startup_pack,1) ||
       setenv("DM_VULKAN_NATIVE_PRODUCER_SHA256",native_resource_startup_producer,1) ||
       setenv("PANVK_DM_NATIVE_IMAGE_IMPORT","true",1))die("Native resource cold environment");
  } else {
    unsetenv("DM_VULKAN_NATIVE_RESOURCE_TABLE");unsetenv("DM_VULKAN_NATIVE_PACK_SHA256");
    unsetenv("DM_VULKAN_NATIVE_PRODUCER_SHA256");
  }
}
#endif
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeSharedGuestHeap(
    JNIEnv *env,jclass klass,jboolean enabled) {
  (void)klass;
  pthread_mutex_lock(&operation_mode_mutex);
  if(operation_mode_fixed) {
    pthread_mutex_unlock(&operation_mode_mutex);
    jclass error=(*env)->FindClass(env,"java/lang/IllegalStateException");
    if(error)(*env)->ThrowNew(env,error,"shared_guest_heap is fixed before native startup");
    return;
  }
  shared_guest_heap_startup=enabled!=0;
  pthread_mutex_unlock(&operation_mode_mutex);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeSharedLazyWindows(
    JNIEnv *env,jclass klass,jboolean enabled) {
  (void)klass;pthread_mutex_lock(&operation_mode_mutex);
  if(operation_mode_fixed) {
    pthread_mutex_unlock(&operation_mode_mutex);
    jclass error=(*env)->FindClass(env,"java/lang/IllegalStateException");
    if(error)(*env)->ThrowNew(env,error,"shared_lazy_windows is fixed before native startup");
    return;
  }
  shared_lazy_windows_startup=enabled!=0;
  pthread_mutex_unlock(&operation_mode_mutex);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeSharedImageRawObserve(
    JNIEnv *env,jclass klass,jboolean enabled) {
  (void)klass;pthread_mutex_lock(&operation_mode_mutex);
  if(operation_mode_fixed) {
    pthread_mutex_unlock(&operation_mode_mutex);
    jclass error=(*env)->FindClass(env,"java/lang/IllegalStateException");
    if(error)(*env)->ThrowNew(env,error,"shared_image_raw_observe is fixed before native startup");
    return;
  }
  shared_image_raw_observe_startup=enabled!=0;
  pthread_mutex_unlock(&operation_mode_mutex);
}
static int legacy_astc_startup,legacy_astc_fixture,legacy_astc_configured;
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeLegacyAstcCopy(
    JNIEnv *env,jclass klass,jboolean enabled,jboolean fixture) {
  (void)klass;pthread_mutex_lock(&operation_mode_mutex);
  if(operation_mode_fixed || (legacy_astc_configured &&
      (legacy_astc_startup!=(enabled!=0) || legacy_astc_fixture!=(fixture!=0)))) {
    pthread_mutex_unlock(&operation_mode_mutex);
    jclass error=(*env)->FindClass(env,"java/lang/IllegalStateException");
    if(error)(*env)->ThrowNew(env,error,"legacy_astc_copy is fixed before native startup");return;
  }
  legacy_astc_configured=1;legacy_astc_startup=enabled!=0;legacy_astc_fixture=fixture!=0;pthread_mutex_unlock(&operation_mode_mutex);
}
static int upload_dual_startup;
static int astc_decode_startup,astc_decode_fixture;
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeAstcDecode(
    JNIEnv *env,jclass klass,jboolean enabled,jboolean fixture) {
  (void)klass;
  pthread_mutex_lock(&operation_mode_mutex);
  if(operation_mode_fixed) {
    pthread_mutex_unlock(&operation_mode_mutex);
    jclass error=(*env)->FindClass(env,"java/lang/IllegalStateException");
    if(error)(*env)->ThrowNew(env,error,"astc_decode_unorm8 is fixed before native startup");
    return;
  }
  astc_decode_startup=enabled!=0;astc_decode_fixture=fixture!=0;
  pthread_mutex_unlock(&operation_mode_mutex);
}
static int pool_copy_accounting_startup;
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativePoolCopyAccounting(
    JNIEnv *env,jclass klass,jboolean enabled) {
  (void)klass;
  pthread_mutex_lock(&operation_mode_mutex);
  if(operation_mode_fixed) {
    pthread_mutex_unlock(&operation_mode_mutex);
    jclass error=(*env)->FindClass(env,"java/lang/IllegalStateException");
    if(error)(*env)->ThrowNew(env,error,"pool_copy_accounting is fixed before native startup");
    return;
  }
  pool_copy_accounting_startup=enabled!=0;
  pthread_mutex_unlock(&operation_mode_mutex);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeUploadDualStore(
    JNIEnv *env,jclass klass,jboolean enabled) {
  (void)klass;
  pthread_mutex_lock(&operation_mode_mutex);
  if(operation_mode_fixed) {
    pthread_mutex_unlock(&operation_mode_mutex);
    jclass error=(*env)->FindClass(env,"java/lang/IllegalStateException");
    if(error)(*env)->ThrowNew(env,error,"upload_dual_store is fixed before native startup");
    return;
  }
  upload_dual_startup=enabled!=0;
  pthread_mutex_unlock(&operation_mode_mutex);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativePixelLocal(
    JNIEnv *env,jclass klass,jboolean enabled) {
  (void)klass;
  pthread_mutex_lock(&operation_mode_mutex);
  if(operation_mode_fixed) {
    pthread_mutex_unlock(&operation_mode_mutex);
    jclass error=(*env)->FindClass(env,"java/lang/IllegalStateException");
    if(error)(*env)->ThrowNew(env,error,"pixel_local is fixed before native startup");
    return;
  }
  pixel_local_startup=enabled!=0;
  pthread_mutex_unlock(&operation_mode_mutex);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativePixelLocalSharedDs(
    JNIEnv *env,jclass klass,jboolean enabled) {
  (void)klass;
  pthread_mutex_lock(&operation_mode_mutex);
  if(operation_mode_fixed) {
    pthread_mutex_unlock(&operation_mode_mutex);
    jclass error=(*env)->FindClass(env,"java/lang/IllegalStateException");
    if(error)(*env)->ThrowNew(env,error,"pixel_local_shared_ds is fixed before native startup");
    return;
  }
  pixel_local_shared_ds_startup=enabled!=0;
  pthread_mutex_unlock(&operation_mode_mutex);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeOpusMode(
    JNIEnv *env,jclass klass,jint mode) {
  (void)klass;
  if(mode<0 || mode>2) {
    (*env)->ThrowNew(env,(*env)->FindClass(env,"java/lang/IllegalArgumentException"),"Invalid Opus mode");
    return;
  }
  if(mode==1) setenv("DM_ANDROID_OPUS_SHADOW","1",1);
  else unsetenv("DM_ANDROID_OPUS_SHADOW");
  if(mode==2) setenv("DM_ANDROID_OPUS_NATIVE","1",1);
  else unsetenv("DM_ANDROID_OPUS_NATIVE");
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeOperationMode(
    JNIEnv *env,jclass klass,jint mode) {
  (void)klass;
  if(mode!=0 && mode!=1) {
    (*env)->ThrowNew(env,(*env)->FindClass(env,"java/lang/IllegalArgumentException"),"operation mode must be 0 or 1");
    return;
  }
  pthread_mutex_lock(&operation_mode_mutex);
  if(operation_mode_fixed) {
    pthread_mutex_unlock(&operation_mode_mutex);
    (*env)->ThrowNew(env,(*env)->FindClass(env,"java/lang/IllegalStateException"),"operation mode is fixed before native startup");
    return;
  }
  operation_mode=mode;
  pthread_mutex_unlock(&operation_mode_mutex);
}
static void *game_thread(void *unused) {
  (void)unused;
  char path[4096];
  const struct {
    const char *name, *leaf;
  } paths[] = {{"DM_ROMFS", "RomFS"},
               {"DM_SAVE", "save"},
               {"DM_TEMP", "temp"},
               {"DM_VULKAN_SHADERS", "shaders"},
               {"DM_HID_RECEIPT_LOG", "hid.jsonl"},
               {"DM_GAME_STATE", "game-state.jsonl"},
               {"DM_SKILL_AIM_LOG", "skill-aim.jsonl"},
               {"DM_VULKAN_FRAME_DUMP", "frame.ppm"}};
  if (validate_game) {
    setenv("DM_VULKAN_VALIDATE", "1", 1);
    setenv("DM_VULKAN_SYNC_VALIDATE", "1", 1);
  }
  // 调试壳的观测不依赖 GPU 校验层，可在无校验开销下复现界面流程。
  // Debug-shell observations remain available without GPU validation overhead.
  if (!getenv("DM_VULKAN_EXTERNAL_PROFILE")) setenv("DM_VULKAN_STATS", "1", 1);
  setenv("DM_UIMSG", "1", 1);
  // 原覆盖层存在时启用原菜单路径；资源和代码必须成套验证。
  // Enable original menu routing when its real resource overlay is present.
  snprintf(path, sizeof path, "%s/romfs_overlay/data/.build.config", runtime_root);
  int have_overlay = access(path, R_OK) == 0;
  if (have_overlay) {
    snprintf(path, sizeof path, "%s/romfs_overlay", runtime_root);
    setenv("DM_ROMFS_OVERLAY", path, 1);
  }
  setenv("DM_FE_AUTO", have_overlay ? "1" : "0", 1);
  // The original binding checks stay active. Their periodic diagnostic log
  // is only requested by explicit validation/capture runs, as on iOS.
  if (validate_game || trace_frame >= 0) setenv("DM_BINDING_DIAG", "1", 1);
  else unsetenv("DM_BINDING_DIAG");
  setenv("DM_TOUCH_DEFAULTS", "1", 1);
  setenv("DM_ROOT", runtime_root, 1);
  // 与原 iOS/Mac 壳一致：挂存档前就能读取语言等自定义设置。
  // Match the original iOS/Mac shells: custom settings must be readable
  // before mounting saves, when the original language setting is applied.
  dm_custom_init(runtime_root);
  setenv("DM_OPMODE", operation_mode ? "1" : "0", 1);
  if (trace_frame >= 0) {
    snprintf(path, sizeof path, "%s/trace", runtime_root);
    mkdir(path, 0700);
    setenv("DM_VULKAN_TRACE_DIR", path, 1);
    snprintf(path, sizeof path, "%d", trace_frame);
    setenv("DM_VULKAN_TRACE_FRAME", path, 1);
  }
  setenv("DM_VULKAN_FRAME_DUMP_AT", "30", 1);
  for (unsigned i = 0; i < sizeof paths / sizeof *paths; i++) {
    snprintf(path, sizeof path, "%s/%s", runtime_root, paths[i].leaf);
    setenv(paths[i].name, path, 1);
  }
  snprintf(path, sizeof path, "%s/temp", runtime_root);
  mkdir(path, 0700);
  snprintf(path, sizeof path, "%s/native.log", runtime_root);
  if (!freopen(path, "w", stderr))
    _exit(90);
  setvbuf(stderr, NULL, _IONBF, 0);
  LOG("Android native start pid=%d", getpid());
  int actual_mode=dm_opmode();
  if(actual_mode!=operation_mode)die("Android operation mode initialized before startup selection");
  uint64_t handheld=front_game_res(0),docked=front_game_res(1);
  LOG("Android operation mode actual=%d (%s); handheld profile=%ux%u docked profile=%ux%u",
      actual_mode,actual_mode?"docked":"handheld",(unsigned)handheld,(unsigned)(handheld>>32),
      (unsigned)docked,(unsigned)(docked>>32));
  // 记录实际宿主编译配置，不从 APK 文件名或启动意图推断。
  // Record the compiled host setting, not an APK-name/intent assumption.
  LOG("Android native host optimization=-O%d", DM_ANDROID_RUNTIME_OPT_LEVEL);
  snprintf(path, sizeof path, "%s/modules", runtime_root);
  char *args[] = {"dmhost_android", path, "--run", NULL};
  // Opt-in only. Paths, log, provider and Surface have already been selected;
  // freeze the SAME device before any loader/SDK/SVC can publish a guest heap.
#ifdef DM_VULKAN_SHARED_POOLS
  extern void vk_legacy_astc_copy_startup(int);
  if(legacy_astc_startup)vk_legacy_astc_copy_startup(1);
#else
  if(legacy_astc_startup)die("Legacy ASTC copy experiment not compiled");
#endif
  const char *shared_heap = getenv("DM_GUEST_HEAP_SHARED");
  if (shared_heap && strcmp(shared_heap,"0") && strcmp(shared_heap,"1"))
    die("DM_GUEST_HEAP_SHARED must be 0 or 1");
  int requested_shared_heap = shared_heap && !strcmp(shared_heap,"1");
  int (*heap_probe)(void) = NULL;
#ifdef DM_VULKAN_SHARED_POOLS
  extern int vk_pool_shared_mode(void);
  heap_probe = vk_pool_shared_mode;
#endif
  int use_shared_heap = dm_guest_heap_startup_freeze(requested_shared_heap,heap_probe);
  if (use_shared_heap < 0) die("Guest heap backing selection was late or conflicting");
  if (requested_shared_heap && !use_shared_heap)
    LOG("Shared guest heap request unsupported: immutable legacy/private startup");
  LOG("Android guest heap startup requested=%d actual_shared=%d",requested_shared_heap,use_shared_heap);
  dm_android_main(3, args);
  return NULL;
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeStart(
    JNIEnv *env, jclass klass, jobject surface, jstring directory,
    jboolean validate, jfloat refresh, jint trace, jint watch_kind,
    jint watch, jboolean gpu_profile, jfloat bright_k, jboolean panel_freeze, jboolean runtime_extrap, jboolean external_profile,
    jstring game_res, jfloat scale_3d, jboolean fixed_3d, jboolean async_submit) {
  (void)klass;
  pthread_mutex_lock(&operation_mode_mutex);
  if(operation_mode_fixed) {
    pthread_mutex_unlock(&operation_mode_mutex);
    die("APK native game already started");
  }
  extern void vk_driver_span_reference_startup(int);
  vk_driver_span_reference_startup(driver_span_reference_startup);
  operation_mode_fixed=1;
  pthread_mutex_unlock(&operation_mode_mutex);
  extern void dm_android_guest_worker_qos_startup(int);
  dm_android_guest_worker_qos_startup(guest_worker_qos_startup);
  extern int dm_thread_wait_budget_startup(unsigned);
  if(dm_thread_wait_budget_startup(guest_wait_budget_startup))die("Guest short-wait startup selection");
  // This driver policy is app-private and frozen before Vulkan initialization.
  // Leaving it default preserves external profiler/property configuration.
  if(turnip_autotune_startup[0] && strcmp(turnip_autotune_startup,"default") &&
      setenv("TU_AUTOTUNE_ALGO",turnip_autotune_startup,1))
    die("Android Turnip autotune startup environment");
  if(turnip_swapchain_ubwc_startup>=0 &&
      setenv("TU_UBWC_SWAPCHAIN",turnip_swapchain_ubwc_startup?"1":"0",1))
    die("Android Turnip swapchain UBWC startup environment");
  LOG("Android Turnip swapchain UBWC selection=%d",turnip_swapchain_ubwc_startup);
  char translation_cpu_mask[16];snprintf(translation_cpu_mask,sizeof translation_cpu_mask,"%x",translation_cpu_mask_startup);
  char render_record_mode[2]={(char)('0'+render_record_mode_startup),0};
  if(setenv("DM_NVN_CPU_MASK",translation_cpu_mask,1) ||
      setenv("DM_VULKAN_NATIVE_BUFFER_GEOMETRY",native_buffer_geometry_startup?"1":"0",1) ||
      setenv("DM_VULKAN_QUERY_ATTACHMENT_REFERENCE",query_attachment_reference_startup?"1":"0",1) ||
      setenv("DM_NVN_INLINE_SHARED",inline_translation_startup?"1":"0",1) ||
      setenv("DM_VULKAN_SUBMISSION_PLAN_REFERENCE",submission_plan_reference_startup?"1":"0",1) ||
      setenv("DM_VULKAN_STORE_KEEP_ALL",keep_attachment_stores_startup?"1":"0",1) ||
      setenv("DM_VULKAN_RENDER_RECORD_MODE",render_record_mode,1) ||
      setenv("DM_VULKAN_SOURCE_PARTITION",source_partition_startup?"1":"0",1) ||
      setenv("DM_VULKAN_STATE_SEGMENTS",state_segments_startup?"1":"0",1) ||
      setenv("DM_VULKAN_NATIVE_READ_GROUP_FRONT",native_read_group_front_startup?"1":"0",1) ||
      setenv("DM_VULKAN_ATTACHMENT_JOB_REUSE",attachment_job_reuse_startup?"1":"0",1) ||
      setenv("DM_VULKAN_ATTACHMENT_RASTER_RUN",attachment_raster_run_startup?"1":"0",1) ||
      setenv("DM_VULKAN_BUFFER_CONSUMPTION_PLAN",buffer_consumption_plan_startup?"1":"0",1) ||
      setenv("DM_VULKAN_IMAGE_BUFFER_SPLIT",image_buffer_split_startup?"1":"0",1) ||
      setenv("DM_VK_SHARED_LAZY_WINDOWS",shared_lazy_windows_startup?"1":"0",1) ||
      setenv("DM_VK_SHARED_IMAGE_RAW_OBSERVE",shared_image_raw_observe_startup?"1":"0",1) ||
      setenv("DM_GUEST_HEAP_SHARED",shared_guest_heap_startup?"1":"0",1) ||
      setenv("DM_VULKAN_SHARED_POOLS",shared_guest_heap_startup?"1":"0",1) ||
      setenv("PANVK_DM_HOST_IMPORT",shared_guest_heap_startup?"true":"false",1) ||
      // The app-local Turnip driver consumes its opt-in during physical-device
      // creation, before the startup byte probe freezes the heap backing.
      setenv("TU_DM_HOST_IMPORT",shared_guest_heap_startup?"true":"false",1))
    die("Android shared guest heap cold request environment");
#ifdef DM_VULKAN_NATIVE_RESOURCES
  if(native_resource_fixture_directory[0])die("Native resource oracle cannot launch game modules");
  native_resource_environment();
#endif
  // Freeze the optional module/descriptor policy before any game or Vulkan
  // worker exists. The backend samples this environment only once; Activity
  // recreation and later Intents must never change established PSO identities.
  if(setenv("DM_VULKAN_PIXEL_LOCAL",pixel_local_startup?"1":"0",1))
    die("Android pixel-local startup environment");
  if(setenv("DM_VULKAN_POOL_COPY_ACCOUNTING",pool_copy_accounting_startup?"1":"0",1))
    die("Android pool copy accounting startup environment");
  if(setenv("DM_VULKAN_UPLOAD_DUAL_STORE",upload_dual_startup?"1":"0",1))
    die("Android dual upload startup environment");
  if(setenv("DM_VULKAN_ASTC_DECODE_UNORM8",astc_decode_startup?"1":"0",1))
    die("Android ASTC decode startup environment");
  if(setenv("DM_VULKAN_PIXEL_LOCAL_SHARED_DS",pixel_local_shared_ds_startup?"1":"0",1))
    die("Android pixel-local shared DS startup environment");
  extern int vk_pixel_local_shared_ds_enabled(void);
  if(vk_pixel_local_shared_ds_enabled()!=pixel_local_shared_ds_startup)
    die("Android pixel-local shared DS initialized before startup selection");
  LOG("Android pixel-local shared readonly DS startup enabled=%d",pixel_local_shared_ds_startup);
  extern int vk_pixel_local_enabled(void);
  if(vk_pixel_local_enabled()!=pixel_local_startup)
    die("Android pixel-local policy initialized before startup selection");
  LOG("Android pixel-local startup enabled=%d",pixel_local_startup);
  const char *path = (*env)->GetStringUTFChars(env, directory, NULL);
  if (!path)
    return;
  if (strlen(path) >= sizeof runtime_root)
    die("APK runtime path too long");
  strcpy(runtime_root, path);
  (*env)->ReleaseStringUTFChars(env, directory, path);
  validate_game = validate != 0;
  if (async_submit) setenv("DM_VULKAN_ASYNC", "1", 1);
  // 厂商计数器采集关闭自定义 CPU/GPU 计时，不关闭游戏的查询语义。
  // Vendor counter captures disable our CPU/GPU timers, not guest query semantics.
  if (external_profile) {
    if (gpu_profile || validate || trace >= 0) die("external profiling conflicts with internal capture/validation");
    setenv("DM_VULKAN_EXTERNAL_PROFILE", "1", 1);
  }
  if (gpu_profile) setenv("DM_VULKAN_GPU_PROFILE", "1", 1);
  // 仅转接原比例控制开关；不增加游戏地址或改写规则。
  // Forward original scale controls without new game addresses or rewrite rules.
  if (scale_3d != -1) {
    if (!isfinite(scale_3d) || scale_3d < .5f || scale_3d > 1)
      die("Original 3D scale override out of range");
    char value[32]; snprintf(value, sizeof value, "%.9g", (double)scale_3d);
    setenv("DM_3D_SCALE", value, 1);
  }
  if (fixed_3d) setenv("DM_3D_SCALE_DYN", "0", 1);
  // 仅调试时启用原可选冻结握手；默认仍与上游一样关闭。
  // Debug opt-in for the original optional freeze handshake; upstream default stays off.
  if (panel_freeze) setenv("DM_PANEL_FREEZE", "1", 1);
  // 仅调试转接原 DM_RT_AB；与上游一样默认关闭。
  // Debug forwarding of original DM_RT_AB; default remains off as upstream.
  if (runtime_extrap) setenv("DM_RT_AB","1",1);
  // 仅调试 Intent 转接原 DM_BRIGHT_K；缺省仍走原宿主动态曝光。
  // Debug Intent forwards original DM_BRIGHT_K; normal runs keep dynamic exposure.
  if (bright_k != -1) {
    if (!isfinite(bright_k) || bright_k < 1 || bright_k > 4)
      die("Original bright exposure override out of range");
    char value[32]; snprintf(value, sizeof value, "%.9g", (double)bright_k);
    setenv("DM_BRIGHT_K", value, 1);
  }
  trace_frame = trace;
  if (watch < -1 || watch > 0xffffff)
    die("World watch class out of range");
  if (watch_kind < 0 || watch_kind > 5)
    die("World watch type out of range");
  // 原监视器只排原子请求；在启动线程前设置，避免覆盖后来的诊断 Intent。
  // The original watcher only queues an atomic request. Set it before starting
  // the thread so a later diagnostic Intent cannot be overwritten at startup.
  void dm_world_watch(int, uint32_t);
  dm_world_watch(watch < 0 ? -1 : watch_kind, (uint32_t)watch);
  window = ANativeWindow_fromSurface(env, surface);
  if (!window)
    die("APK native window unavailable");
  const char *resolution = game_res ? (*env)->GetStringUTFChars(env, game_res, NULL) : NULL;
  if (game_res && !resolution) return;
  int configured = dm_android_display_configure(ANativeWindow_getWidth(window),
                                                ANativeWindow_getHeight(window), resolution);
  if (game_res) (*env)->ReleaseStringUTFChars(env, game_res, resolution);
  if (!configured) die("Android game resolution invalid");
  uint64_t output = front_game_res(operation_mode);
  LOG("Android game output %ux%u; 3D scale=%s dynamic=%s",
      (unsigned)output, (unsigned)(output >> 32), scale_3d == -1 ? "mobile default" : getenv("DM_3D_SCALE"),
      fixed_3d ? "off" : "original policy");
  vk_android_window(window, refresh);
  pthread_t thread;
  if (pthread_create(&thread, NULL, game_thread, NULL))
    die("APK game thread");
  pthread_detach(thread);
}

JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeWatch(
    JNIEnv *env, jclass type, jint kind, jint unit_class) {
  (void)env;
  (void)type;
  if (kind < 0 || kind > 5 || unit_class < -1 || unit_class > 0xffffff)
    die("World watch request out of range");
  void dm_world_watch(int, uint32_t);
  dm_world_watch(unit_class < 0 ? -1 : kind, (uint32_t)unit_class);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeThermalLimit(
    JNIEnv *env, jclass klass, jint limit) {
  (void)env; (void)klass;
  extern void vk_set_thermal_limit(int);
  vk_set_thermal_limit(limit);
  dm_android_3d_thermal_limit(limit);
}
// Read existing CPU-side published state only while the debug panel is visible.
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeManualLimit(
    JNIEnv *env, jclass klass, jint limit) {
  (void)env; (void)klass; vk_set_manual_limit(limit);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeManualScale(
    JNIEnv *env, jclass klass, jint percent) {
  (void)env; (void)klass; dm_android_3d_scale_manual(percent);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeDebugState(
    JNIEnv *env, jclass klass, jfloatArray output) {
  (void)klass;
  if ((*env)->GetArrayLength(env,output)!=9) return;
  uint32_t gw,gh,sw,sh; vk_window_output_sizes(&gw,&gh,&sw,&sh);
  extern int mtl_fps_target(void);
  float values[]={vk_requested_fps(),mtl_fps_target(),vk_thermal_limit(),vk_manual_limit(),
                  gw,gh,sw,sh,dm_3d_scale_cur()};
  (*env)->SetFloatArrayRegion(env,output,0,9,values);
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeTrimMemory(
    JNIEnv *env,jclass klass) {
  (void)env;(void)klass;
  extern void dm_gfx_trim_memory_request(void);dm_gfx_trim_memory_request();
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeSuspend(
    JNIEnv *env, jclass klass, jboolean paused) {
  (void)env;
  (void)klass;
  extern void dm_gfx_suspend(int), dm_audio_suspend(int);
  extern void guest_clock_freeze_for(int, int);
  if (lifecycle_suspended == !!paused) return;
  lifecycle_suspended = !!paused;
  void mtl_present_discard(int);
  if (paused) {
    extern void dm_gfx_trim_memory_request(void);dm_gfx_trim_memory_request();
    dm_focus_set(3);
    dm_ui_lifecycle(1);
    mtl_present_discard(1);
    // 与原 front_ios.m 一样先等原游戏存档事务，再冻结时钟/音频/图形。
    // Match front_ios.m: wait for the original game's save transaction
    // before freezing the clock/audio/graphics. Surface callbacks may repeat.
    if (dm_ingame_now()) {
      // 原停着存盘会直接读游戏属性；ART 的 UI 线程也须有原客体线程槽。
      // Original paused-save diagnostics call game stat readers directly;
      // give the ART UI thread the original guest thread slots as well.
      static _Thread_local int save_thread_ready;
      if (!save_thread_ready) {
        void guest_thread_init(void);
        guest_thread_init();
        save_thread_ready = 1;
      }
      int dm_save_wait(double, uint64_t *, int *, double *);
      uint64_t commits = 0; int froze = 0; double ms = 0;
      int asked = dm_save_wait(2.5, &commits, &froze, &ms);
      LOG("Android background save asked=%d commits=%llu froze=%d elapsed_ms=%.1f%s",
          asked, (unsigned long long)commits, froze, ms,
          commits ? "" : " INCOMPLETE");
    }
    guest_clock_freeze_for(1, 1);
    dm_audio_suspend(1);
    dm_gfx_suspend(1);
    // GPU recording is quiescent after the original lifecycle save/suspend.
    // Persist optional driver PSO data here, never on an active frame path.
    extern void vk_pipeline_cache_checkpoint(void);
    vk_pipeline_cache_checkpoint();
  } else {
    dm_ui_lifecycle(0);
    extern void dm_3d_scale_resumed(void);
    dm_3d_scale_resumed();
    dm_gfx_suspend(0);
    dm_audio_suspend(0);
    guest_clock_freeze_for(1, 0);
    mtl_present_discard(0);
    dm_focus_set(1);
  }
}
JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeSurface(
    JNIEnv *env, jclass klass, jobject surface, jfloat refresh) {
  (void)klass;
  extern void dm_gfx_suspend(int);
  dm_gfx_suspend(1);
  window = surface ? ANativeWindow_fromSurface(env, surface) : NULL;
  vk_android_window(window, refresh);
}

JNIEXPORT void JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeTests(
    JNIEnv *env, jclass klass, jstring directory, jboolean async_submit,
    jboolean acquire_scope, jboolean memory_only, jboolean validation_enabled, jboolean readback_batch) {
  (void)klass;
  if(readback_batch) {
    static _Atomic int once;
    if(atomic_exchange(&once,1) || !async_submit || acquire_scope || memory_only || validation_enabled ||
        astc_decode_fixture || legacy_astc_startup || legacy_astc_fixture)
      die("Readback batch fixture requires one cold uncombined async invocation");
    if(setenv("DM_READBACK_BATCH_FIXTURE","1",1))die("Readback batch fixture selection");
  }
#ifdef DM_VULKAN_NATIVE_RESOURCES
  if(native_resource_fixture_directory[0]) {
    pthread_mutex_lock(&operation_mode_mutex);
    if(operation_mode_fixed || vk_loader_is_initialized())die("Native resource oracle requires a cold process");
    operation_mode_fixed=1;pthread_mutex_unlock(&operation_mode_mutex);
    if(!shared_guest_heap_startup)die("Native resource oracle requires explicit fixed shared mode");
    if(setenv("DM_VULKAN_SHARED_POOLS","1",1) || setenv("PANVK_DM_HOST_IMPORT","true",1) ||
       setenv("DM_NATIVE_RESOURCE_NVN_FIXTURE",native_resource_fixture_directory,1))die("Native resource oracle cold setup");
    native_resource_environment();
  }
#endif
  if(setenv("DM_VULKAN_ASTC_DECODE_UNORM8",astc_decode_startup?"1":"0",1) ||
      (astc_decode_fixture?setenv("DM_ASTC_DECODE_FIXTURE","1",1):unsetenv("DM_ASTC_DECODE_FIXTURE")))
    die("Android ASTC fixture startup environment");
#ifdef DM_VULKAN_SHARED_POOLS
  extern void vk_legacy_astc_copy_startup(int);
  if(legacy_astc_startup || legacy_astc_fixture)vk_legacy_astc_copy_startup(1);
  if(legacy_astc_fixture && setenv("DM_LEGACY_ASTC_COPY_FIXTURE","1",1))die("Legacy ASTC fixture selection");
#else
  if(legacy_astc_startup || legacy_astc_fixture)die("Legacy ASTC experiment not compiled");
#endif
  if (async_submit) setenv("DM_VULKAN_ASYNC", "1", 1);
  if (acquire_scope) setenv("DM_VULKAN_ACQUIRE_TEST", "1", 1);
  if (memory_only) setenv("DM_VULKAN_MEMORY_TEST", "1", 1);
  const char *path = (*env)->GetStringUTFChars(env, directory, NULL);
  if (!path)
    return;
  char file[4096];
  snprintf(file, sizeof file, "%s/vulkan-fixture", path);
  setenv("DM_VULKAN_SHADERS", file, 1);
  setenv("DM_CAPTURE_TEST_DIR", path, 1);
  if(validation_enabled) {
    if(setenv("DM_VULKAN_VALIDATE","1",1) || setenv("DM_VULKAN_SYNC_VALIDATE","1",1))die("Vulkan fixture validation environment");
  } else if(unsetenv("DM_VULKAN_VALIDATE") || unsetenv("DM_VULKAN_SYNC_VALIDATE"))die("Vulkan fixture validation environment");
  snprintf(file, sizeof file, "%s/vulkan-test.log", path);
  int output_error=dm_fixture_output_open(file);if(output_error)_exit(output_error);
  printf("{\"native_vulkan_app_configuration\":true,\"uid\":%u,\"pid\":%u,"
         "\"validation_layer\":\"%s\",\"synchronization_validation\":\"%s\","
         "\"legacy_ASTC_requested\":%s,\"readback_batch_requested\":%s,\"game_modules_executed\":false}\n",
         (unsigned)getuid(),(unsigned)getpid(),validation_enabled?"requested":"disabled_explicitly",
         validation_enabled?"requested":"disabled_explicitly",legacy_astc_fixture?"true":"false",readback_batch?"true":"false");
  (*env)->ReleaseStringUTFChars(env, directory, path);
#ifdef DM_VULKAN_NATIVE_RESOURCES
  if(native_resource_fixture_directory[0])printf("{\"native_resource_app_configuration\":true,\"uid\":%u,\"async\":%s,\"game_modules_executed\":false,\"validation\":%s}\n",
      (unsigned)getuid(),async_submit?"true":"false",native_resource_fixture_validation?"true":"false");
#endif
  int dm_vulkan_test_main(void);
  int result=dm_vulkan_test_main();
  printf("{\"native_vulkan_app_terminal\":true,\"uid\":%u,\"pid\":%u,\"rc\":%d}\n",
         (unsigned)getuid(),(unsigned)getpid(),result);
#ifdef DM_VULKAN_NATIVE_RESOURCES
  if(native_resource_fixture_directory[0])printf("{\"native_resource_app_terminal\":true,\"rc\":%d,\"game_modules_executed\":false}\n",result);
#endif
}

// Separate owned test selector; never freezes game startup policy or touches
// an already initialized Vulkan device. The normal nativeTests path is unchanged.
JNIEXPORT jint JNICALL Java_com_d2rmobile_nativeport_MainActivity_nativeSharedImageTests(
    JNIEnv *env,jclass klass,jstring directory,jint mode) {
  (void)klass;
#ifdef DM_VULKAN_SHARED_POOLS
  static _Atomic int selected;
  if(mode<0 || mode>1 || vk_loader_is_initialized() || atomic_exchange(&selected,1))
    return 78;
  const char *path=(*env)->GetStringUTFChars(env,directory,NULL);
  if(!path) return 79;
  char file[4096];
  int count=snprintf(file,sizeof file,"%s/shared-image-test.log",path);
  if(count<0 || (size_t)count>=sizeof file) _exit(89);
  int output_error=dm_fixture_output_open(file);if(output_error) _exit(output_error);
  printf("{\"shared_image_app_configuration\":true,\"uid\":%u,\"mode\":\"%s\","
         "\"validation_layer\":\"disabled_explicitly\",\"synchronization_validation\":\"disabled_explicitly\","
         "\"game_modules_executed\":false}\n",(unsigned)getuid(),mode?"retile":"nc-gate");
  vk_pool_window_bytes(0);vk_pool_mirror_reference(0);mtl_pool_defer_publication(0);
  if(setenv("PANVK_DM_HOST_IMPORT","true",1) || setenv("DM_VULKAN_SHARED_POOLS","1",1) ||
     setenv("DM_VULKAN_ASYNC","1",1) ||
     setenv(mode?"DM_SHARED_IMAGE_DIRTY_FIXTURE":"DM_SHARED_IMAGE_NC_GATE_FIXTURE","1",1) ||
     (mode && setenv("DM_SHARED_IMAGE_COPY_TEST","1",1)) ||
     unsetenv("DM_VULKAN_VALIDATE") || unsetenv("DM_VULKAN_SYNC_VALIDATE"))
    die("Shared image fixture cold environment");
  count=snprintf(file,sizeof file,"%s/vulkan-fixture",path);
  if(count<0 || (size_t)count>=sizeof file) _exit(89);
  if(setenv("DM_VULKAN_SHADERS",file,1) || setenv("DM_CAPTURE_TEST_DIR",path,1))
    die("Shared image fixture paths");
  (*env)->ReleaseStringUTFChars(env,directory,path);
  int dm_vulkan_test_main(void);int rc=dm_vulkan_test_main();
  printf("{\"shared_image_app_terminal\":true,\"mode\":\"%s\",\"rc\":%d,\"game_modules_executed\":false}\n",mode?"retile":"nc-gate",rc);
  return rc;
#else
  (void)env;(void)directory;(void)mode;return 77;
#endif
}
