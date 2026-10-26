// Android 外壳只管理 Surface 与生命周期；游戏运行在原生宿主线程。
// The Android shell manages Surface and lifecycle; the original native host runs the game thread.
package com.d2rmobile.nativeport;
import android.app.Activity;
import android.os.Bundle;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import java.io.File;
public final class MainActivity extends Activity implements SurfaceHolder.Callback {
    static { System.loadLibrary("dmhost_android"); }
    private static native String[] nativePollText();
    private static native void nativeCompleteText(long id, String value);
    private android.app.AlertDialog textDialog;
    private static native void nativeProbe(String directory);
    private static native void nativeRenderdocCapture(String directory);
    private static native void nativeStart(Surface surface, String directory, boolean validate, float refresh, int trace, int watchType, int watchClass, boolean gpuProfile, float brightK, boolean panelFreeze, boolean runtimeExtrap, boolean externalProfile, String gameRes, float scale3d, boolean fixed3d, boolean asyncSubmit);
    private static native void nativeOperationMode(int mode);
    private static native void nativeOpusMode(int mode);
    private static native void nativeWatch(int type, int unitClass);
    private static native int nativeSharedImageTests(String directory, int mode);
    private static boolean sharedImageFixtureStarted;
    private static boolean nativeResourceFixtureStarted;
    private boolean sharedImageFixture;
    private static native void nativeTests(String directory, boolean asyncSubmit, boolean acquireScope, boolean memoryOnly, boolean validation, boolean readbackBatch);
    private static native void nativeSuspend(boolean paused);
    private static native void nativeTrimMemory();
    private static native void nativeVmaMemoryDumpRequest();
    private static native void nativeThermalLimit(int limit);
    private static native void nativeManualLimit(int limit);
    private static native void nativeManualScale(int percent);
    private static native void nativeDebugState(float[] output);
    private static final DebugPanel.Settings debugSettings = new DebugPanel.Settings();
    private DebugPanel debugPanel;
    private final float[] debugState = new float[9];
    private static native void nativeSurface(Surface surface, float refresh);
    private static native void nativeBack();
    private static native void nativePipelineByteHash(boolean legacy);
    private static native void nativePipelinePrepareMode(int mode);
    private static native void nativeReadbackCoherentReference(boolean reference);
    private static native void nativeCpuCopyReference(boolean reference);
    private static native void nativeDriverConstantReference(boolean reference);
    private static native void nativeDriverSpanReference(boolean reference);
    private static native void nativeImageBufferSplit(boolean enabled);
    private static native void nativeBufferConsumptionPlan(boolean enabled);
    private static native void nativeReadGroupFront(boolean enabled);
    private static native void nativeAttachmentJobReuse(boolean enabled);
    private static native void nativeAttachmentRasterRun(boolean enabled);
    private static native void nativeStateSegments(boolean enabled);
    private static native void nativeSourcePartition(boolean enabled);
    private static native void nativeInlineTranslation(boolean enabled);
    private static native void nativeSubmissionPlanReference(boolean reference);
    private static native void nativeKeepAttachmentStores(boolean enabled);
    private static native void nativeRenderRecordMode(int mode);
    private static native void nativeTranslationCpuMask(int mask);
    private static native void nativeTurnipAutotune(String selection);
    private static native void nativeSampleLivenessReference(boolean reference);
    private static native void nativeStorageBiasReference(boolean reference);
    private static native void nativePoolSyncReference(boolean reference);
    private static native void nativePoolLookupReference(boolean reference);
    private static native void nativeUploadScanReference(boolean reference);
    private static native void nativeSamplerLookupReference(boolean reference);
    private static native void nativeTextureBindingReference(boolean reference);
    private static native void nativeTexelStorage(boolean enabled);
    private static native void nativeShaderPresent(boolean enabled);
    private static native void nativePoolMirrorReference(boolean reference);
    private static native void nativePoolWindowBytes(long bytes);
    private static native void nativeSharedGuestHeap(boolean enabled);
    private static native void nativeSharedLazyWindows(boolean enabled);
    private static native void nativeSharedImageRawObserve(boolean enabled);
    private static native void nativeLegacyAstcCopy(boolean enabled, boolean fixture);
    private static native void nativeNativeResources(String table, String packSha256, String producerSha256, String fixtureDirectory, boolean validation);
    private static native void nativePoolDeferPublication(boolean enabled);
    private static native void nativePixelLocal(boolean enabled);
    private static native void nativePixelLocalSharedDs(boolean enabled);
    private static native void nativeUploadDualStore(boolean enabled);
    private static native void nativeAstcDecode(boolean enabled, boolean fixture);
    private static native void nativePoolCopyAccounting(boolean enabled);
    private static native void nativePerformanceHintReference(boolean reference);
    private static native void nativeImageLayoutReference(boolean reference);
    private static native void nativeSampledArrayLayoutReference(boolean reference);
    private static native void nativeBufferMemoryReference(boolean reference);
    private static native void nativeStorageUsageReference(boolean reference);
    private static native void nativeAttachmentUsageReference(boolean reference);
    private static native void nativeDepthLayoutReference(boolean reference);
    private static native void nativeComputeLayoutReference(boolean reference);
    private static native void nativeReadonlyDepthReference(boolean reference);
    private static native void nativeClearAttachmentReference(boolean reference);
    private static native void nativeTextureDirtyReference(boolean reference);
    private static native void nativeZeroDrawReference(boolean reference);
    private static native void nativeNoFxaaEnabled(boolean enabled);
    private static native void nativeUniformSpecializationEnabled(boolean enabled);
    private static native void nativeUniformPredicateEnabled(boolean enabled);
    private static native void nativeDeferredMipClearsEnabled(boolean enabled);
    private static native void nativeDynamicUniformReference(boolean reference);
    private static native void nativeDynamicUniformNarrowReference(boolean reference);
    private static native void nativeDescriptorHistoryReference(boolean reference);
    private static native void nativeDescriptorReuseReference(boolean reference);
    private static native void nativeDescriptorBatchReference(boolean reference);
    private static native void nativeDescriptorTemplateReference(boolean reference);
    private static native void nativeMailboxPresent(boolean enabled);
    private static native void nativePresentWorkerReference(boolean reference, boolean sharedQueue);
    private static native void nativeBloomQuarterEnabled(boolean enabled);
    private static native void nativeVulkanDriver(String provider, String directory, String hooks);
    private static native void nativeStreamSubmitReference(boolean reference);
    private static native void nativeStoreDescriptorReference(boolean reference);
    private static native void nativeCoverWaitReference(boolean reference);
    private static native void nativeGraphicsStageReference(boolean reference);
    private static native void nativeSubmissionBarrierReference(boolean reference);
    private static native void nativeQueryPerDrawReference(boolean reference);
    private android.window.OnBackInvokedCallback backCallback;
    private static boolean hostStarted, readbackBatchFixtureStarted;
    private static boolean desktopProfileResolved;
    private static VerifiedStartupProfile desktopProfile;
    private boolean desktopProfileRejected;
    private boolean started, resumed;
    private SurfaceHolder surfaceHolder;
    private TouchControls touchControls;
    private void resetInput() { GameInput.reset(); if (touchControls != null) touchControls.cancel(); }
    private void immersive() {
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            android.view.WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.hide(android.view.WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else getWindow().getDecorView().setSystemUiVisibility(
            View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
            View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
    }
    private float refresh() { return getWindowManager().getDefaultDisplay().getRefreshRate(); }
    // Optional display-policy experiment; the native game keeps its own 60 Hz
    // rendering target. Select only a mode with the current physical pixels.
    private void requestDisplayRefresh(Surface surface) {
        float hz = getIntent().getFloatExtra("display_refresh_hz", 0);
        if (!Float.isFinite(hz) || hz < 0 || hz > 240) hz = 0;
        android.view.Display display = getWindowManager().getDefaultDisplay();
        android.view.WindowManager.LayoutParams params = getWindow().getAttributes();
        params.preferredRefreshRate = hz;
        params.preferredDisplayModeId = 0;
        if (hz > 0) {
            boolean matched = false;
            android.view.Display.Mode current = display.getMode();
            for (android.view.Display.Mode mode : display.getSupportedModes()) {
                if (mode.getPhysicalWidth() == current.getPhysicalWidth() &&
                        mode.getPhysicalHeight() == current.getPhysicalHeight() &&
                        Math.abs(mode.getRefreshRate() - hz) < 0.5f) {
                    params.preferredDisplayModeId = mode.getModeId();
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                hz = 0;
                params.preferredRefreshRate = 0;
            }
        }
        getWindow().setAttributes(params);
        if (android.os.Build.VERSION.SDK_INT >= 30 && surface != null && surface.isValid())
            surface.setFrameRate(hz, Surface.FRAME_RATE_COMPATIBILITY_DEFAULT);
        android.util.Log.i("D2RDisplay", "requested refresh=" + hz +
                " mode=" + params.preferredDisplayModeId + " observed=" + display.getRefreshRate());
    }
    private boolean applyDesktopStartupProfile() {
        android.content.Intent incoming = getIntent();
        boolean explicit = incoming.getExtras() != null && !incoming.getExtras().isEmpty();
        if (!VerifiedStartupProfile.ordinaryLauncher(incoming.getAction(),
                incoming.hasCategory(android.content.Intent.CATEGORY_LAUNCHER), explicit)) return true;
        // An explicitly launched native host cannot acquire a new cold policy
        // through a later Activity recreation or a launcher tap.
        if (!desktopProfileResolved && hostStarted) return true;
        try {
            if (!desktopProfileResolved) {
                desktopProfile = VerifiedStartupProfile.load(new File(getFilesDir(), "runtime"),
                        new File(getApplicationInfo().sourceDir), android.os.Build.FINGERPRINT);
                String[] splits = getApplicationInfo().splitSourceDirs;
                if (desktopProfile != null && splits != null && splits.length != 0)
                    throw new java.io.IOException("Installed startup profile requires a monolithic APK");
                desktopProfileResolved = true;
                android.util.Log.i("D2RLaunchProfile", desktopProfile == null
                        ? "Desktop launch: original platform defaults; no installed profile"
                        : "Desktop launch: " + desktopProfile.name);
            }
            if (desktopProfile != null) {
                android.content.Intent selected = new android.content.Intent(incoming);
                for (java.util.Map.Entry<String, Object> row : desktopProfile.extras.entrySet()) {
                    Object value = row.getValue();
                    if (value instanceof Boolean) selected.putExtra(row.getKey(), ((Boolean)value).booleanValue());
                    else if (value instanceof Integer) selected.putExtra(row.getKey(), ((Integer)value).intValue());
                    else if (value instanceof Long) selected.putExtra(row.getKey(), ((Long)value).longValue());
                    else selected.putExtra(row.getKey(), (String)value);
                }
                setIntent(selected);
            }
            return true;
        } catch (java.io.IOException error) {
            desktopProfileRejected = true;
            android.util.Log.e("D2RLaunchProfile", "Desktop launch profile rejected", error);
            new android.app.AlertDialog.Builder(this)
                    .setTitle("启动配置 / Launch profile")
                    .setMessage("运行配置与当前安装不匹配，请重新部署。\nProfile mismatch. Please redeploy.")
                    .setCancelable(false)
                    .setPositiveButton(android.R.string.ok, (dialog, which) -> finish())
                    .show();
            return false;
        }
    }
    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        if (!applyDesktopStartupProfile()) return;
        if (getIntent().getBooleanExtra("readback_batch_test", false)) {
            if (saved != null || hostStarted || readbackBatchFixtureStarted || sharedImageFixtureStarted ||
                    !getIntent().hasExtra("run_game") || getIntent().getBooleanExtra("run_game", true) ||
                    !getIntent().getBooleanExtra("run_vulkan_tests", false) ||
                    !getIntent().getBooleanExtra("async_submit", false) ||
                    !"system".equals(getIntent().getStringExtra("vulkan_provider")) ||
                    getIntent().getBooleanExtra("shared_guest_heap", false) ||
                    getIntent().getBooleanExtra("legacy_astc_copy", false) ||
                    getIntent().getBooleanExtra("legacy_astc_copy_test", false) ||
                    getIntent().getBooleanExtra("astc_decode_test", false) ||
                    getIntent().getBooleanExtra("memory_test", false) ||
                    getIntent().getBooleanExtra("acquire_scope_test", false) ||
                    !getIntent().hasExtra("validate") || getIntent().getBooleanExtra("validate", true) ||
                    !getIntent().hasExtra("vulkan_test_validation") || getIntent().getBooleanExtra("vulkan_test_validation", true))
                throw new IllegalArgumentException("Readback oracle requires one cold system/async tests-only process, no other fixture or validation");
            readbackBatchFixtureStarted = true;
        }
        // Isolated functional oracle: choose the same app-local ICD before any
        // game/Surface/device exists; recreation and new Intents cannot rerun it.
        if (getIntent().hasExtra("native_resource_fixture_dir")) {
            if (hostStarted || nativeResourceFixtureStarted || getIntent().hasExtra("shared_image_test") ||
                    !getIntent().hasExtra("run_game") || getIntent().getBooleanExtra("run_game", true) ||
                    !getIntent().getBooleanExtra("run_vulkan_tests", false) ||
                    !getIntent().getBooleanExtra("async_submit", false) ||
                    !getIntent().getBooleanExtra("shared_guest_heap", false) ||
                    getIntent().getBooleanExtra("memory_test", false) || getIntent().getBooleanExtra("acquire_scope_test", false) ||
                    !"adreno".equals(getIntent().getStringExtra("vulkan_provider")) ||
                    getIntent().getBooleanExtra("validate", false))
                throw new IllegalArgumentException("Native resource oracle requires one cold explicit Turnip shared async test without game/other tests");
            nativeResourceFixtureStarted = sharedImageFixture = true;
            File fixtureRoot = new File(getFilesDir(), "runtime");
            if (!fixtureRoot.isDirectory() && !fixtureRoot.mkdirs()) throw new IllegalStateException("runtime directory");
            nativeProbe(fixtureRoot.getAbsolutePath());
            nativeSharedGuestHeap(true);
            nativeNativeResources(getIntent().getStringExtra("native_resource_table"),
                    getIntent().getStringExtra("native_resource_pack_sha256"),
                    getIntent().getStringExtra("native_resource_producer_sha256"),
                    getIntent().getStringExtra("native_resource_fixture_dir"),
                    getIntent().getBooleanExtra("vulkan_test_validation", true));
            nativeVulkanDriver("adreno", new File(fixtureRoot, "vulkan-driver").getAbsolutePath(), getApplicationInfo().nativeLibraryDir);
            new Thread(() -> nativeTests(fixtureRoot.getAbsolutePath(), true, false, false, getIntent().getBooleanExtra("vulkan_test_validation", true), false), "native-resource-test").start();
            return;
        }
        if (getIntent().hasExtra("shared_image_test")) {
            String kind = getIntent().getStringExtra("shared_image_test");
            int mode = "nc-gate".equals(kind) ? 0 : "retile".equals(kind) ? 1 : -1;
            if (mode < 0 || hostStarted || sharedImageFixtureStarted ||
                    !getIntent().hasExtra("run_game") || getIntent().getBooleanExtra("run_game", true) ||
                    !getIntent().hasExtra("run_vulkan_tests") || getIntent().getBooleanExtra("run_vulkan_tests", true) ||
                    !getIntent().getBooleanExtra("async_submit", false) ||
                    !"panvk".equals(getIntent().getStringExtra("vulkan_provider")) ||
                    !getIntent().hasExtra("validate") || getIntent().getBooleanExtra("validate", true) ||
                    !getIntent().hasExtra("vulkan_test_validation") || getIntent().getBooleanExtra("vulkan_test_validation", true))
                throw new IllegalArgumentException("Shared image oracle requires one cold explicit nc-gate/retile, PanVK, async, game/tests/validation=false");
            sharedImageFixtureStarted = sharedImageFixture = true;
            File fixtureRoot = new File(getFilesDir(), "runtime");
            if (!fixtureRoot.isDirectory() && !fixtureRoot.mkdirs()) throw new IllegalStateException("runtime directory");
            nativeProbe(fixtureRoot.getAbsolutePath());
            nativeVulkanDriver("panvk", new File(fixtureRoot, "vulkan-panfrost").getAbsolutePath(), null);
            new Thread(() -> nativeSharedImageTests(fixtureRoot.getAbsolutePath(), mode), "shared-image-test").start();
            return;
        }
        // Select the codec before construction and retain it for each decoder's
        // lifetime. Explicit SDK/shadow references remain available; Activity
        // recreation cannot switch an already advanced decoder's backend.
        if (!hostStarted) {
            nativeDriverSpanReference(getIntent().getBooleanExtra("driver_span_reference", false));
            nativeImageBufferSplit(getIntent().getBooleanExtra("image_buffer_split", true));
            nativeBufferConsumptionPlan(getIntent().getBooleanExtra("buffer_consumption_plan", true));
            nativeReadGroupFront(getIntent().getBooleanExtra("native_read_group_front", false));
            nativeAttachmentJobReuse(getIntent().getBooleanExtra("attachment_job_reuse", true));
            nativeAttachmentRasterRun(getIntent().getBooleanExtra("attachment_raster_run", true));
            nativeStateSegments(getIntent().getBooleanExtra("state_segments", true));
            nativeSourcePartition(getIntent().getBooleanExtra("source_partition", false));
            nativeInlineTranslation(getIntent().getBooleanExtra("inline_translation", false));
            nativeSubmissionPlanReference(getIntent().getBooleanExtra("submission_plan_reference", false));
            nativeKeepAttachmentStores(getIntent().getBooleanExtra("keep_attachment_stores", false));
            nativeRenderRecordMode(getIntent().getIntExtra("render_record_mode", 0));
            nativeTranslationCpuMask(getIntent().getIntExtra("translation_cpu_mask", 0));
            String autotune = getIntent().getStringExtra("turnip_autotune");
            if (autotune != null && !"default".equals(autotune) &&
                    !"adreno".equals(getIntent().getStringExtra("vulkan_provider")) &&
                    !getIntent().getBooleanExtra("custom_vulkan_driver", false))
                throw new IllegalArgumentException("Turnip autotune requires the app-local Adreno provider");
            nativeTurnipAutotune(autotune);
            nativeSharedGuestHeap(getIntent().getBooleanExtra("shared_guest_heap", false));
            nativeSharedLazyWindows(getIntent().getBooleanExtra("shared_lazy_windows", false));
            nativeSharedImageRawObserve(getIntent().getBooleanExtra("shared_image_raw_observe", true));
            nativeLegacyAstcCopy(getIntent().getBooleanExtra("legacy_astc_copy", false),
                    getIntent().getBooleanExtra("run_vulkan_tests", false) &&
                    getIntent().getBooleanExtra("legacy_astc_copy_test", false));
            nativeNativeResources(getIntent().getStringExtra("native_resource_table"),
                    getIntent().getStringExtra("native_resource_pack_sha256"),
                    getIntent().getStringExtra("native_resource_producer_sha256"),
                    getIntent().getBooleanExtra("run_vulkan_tests", false) ? getIntent().getStringExtra("native_resource_fixture_dir") : null, true);
            // Source-proved subpasses fall back when recipes, resources or
            // dependencies do not qualify. Shared-depth remains opt-in.
            nativePixelLocal(getIntent().getBooleanExtra("pixel_local", true));
            nativePixelLocalSharedDs(getIntent().getBooleanExtra("pixel_local_shared_ds", false));
            nativeUploadDualStore(getIntent().getBooleanExtra("upload_dual_store", false));
            nativeAstcDecode(getIntent().getBooleanExtra("astc_decode_unorm8", false),
                    getIntent().getBooleanExtra("run_vulkan_tests", false) &&
                    getIntent().getBooleanExtra("astc_decode_test", false));
            nativePoolCopyAccounting(getIntent().getBooleanExtra("pool_copy_accounting", false));
            nativePoolDeferPublication(getIntent().getBooleanExtra("defer_pool_publication", false));
            // Select backing before Vulkan initialization. Finite maintenance
            // keeps a complete binding contiguous without expanding every pool.
            if (getIntent().hasExtra("pool_window_bytes")) {
                Object value = getIntent().getExtras().get("pool_window_bytes");
                if (!(value instanceof Long))
                    throw new IllegalArgumentException("pool_window_bytes must be a long (--el)");
                long bytes = (Long)value;
                if (bytes < 0 || (bytes != 0 && (bytes < 4096 || (bytes & (bytes - 1)) != 0)))
                    throw new IllegalArgumentException("pool_window_bytes must be 0 or a power of two >= 4096");
                nativePoolWindowBytes(bytes);
            } else nativePoolWindowBytes(16L << 20);
            boolean shadow = getIntent().getBooleanExtra("opus_shadow", false);
            boolean nativeOpus = getIntent().getBooleanExtra("native_opus", !shadow);
            if (shadow && nativeOpus) throw new IllegalArgumentException("Opus modes are mutually exclusive");
            nativeOpusMode(nativeOpus ? 2 : shadow ? 1 : 0);
        }
        GameInput.initialize(this);
        getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        requestDisplayRefresh(null);
        File root = new File(getFilesDir(), "runtime");
        if (!root.isDirectory() && !root.mkdirs()) throw new IllegalStateException("runtime directory");
        nativeProbe(root.getAbsolutePath());
        nativePipelineByteHash(getIntent().getBooleanExtra("pipeline_byte_hash", false));
        nativeReadbackCoherentReference(getIntent().getBooleanExtra("readback_coherent_reference", false));
        nativeCpuCopyReference(getIntent().getBooleanExtra("cpu_copy_reference", false));
        nativeDriverConstantReference(getIntent().getBooleanExtra("driver_constant_reference", false));
        nativeSampleLivenessReference(getIntent().getBooleanExtra("sample_liveness_reference", false));
        nativeStorageBiasReference(getIntent().getBooleanExtra("storage_bias_reference", false));
        nativePoolSyncReference(getIntent().getBooleanExtra("pool_sync_reference", false));
        nativePoolLookupReference(getIntent().getBooleanExtra("pool_lookup_reference", false));
        nativeUploadScanReference(getIntent().getBooleanExtra("upload_scan_reference", false));
        nativeSamplerLookupReference(getIntent().getBooleanExtra("sampler_lookup_reference", false));
        nativeTextureBindingReference(getIntent().getBooleanExtra("texture_binding_reference", false));
        nativeTexelStorage(getIntent().getBooleanExtra("texel_storage", false));
        if (getIntent().hasExtra("shader_present"))
            nativeShaderPresent(getIntent().getBooleanExtra("shader_present", false));
        nativePoolMirrorReference(getIntent().getBooleanExtra("pool_mirror_reference", false));
        nativePerformanceHintReference(getIntent().getBooleanExtra("performance_hint_reference", true));
        nativeImageLayoutReference(getIntent().getBooleanExtra("image_layout_reference", false));
        nativeSampledArrayLayoutReference(getIntent().getBooleanExtra("sampled_array_layout_reference", false));
        nativeBufferMemoryReference(getIntent().getBooleanExtra("buffer_memory_reference", false));
        nativeStorageUsageReference(getIntent().getBooleanExtra("storage_usage_reference", false));
        nativeAttachmentUsageReference(getIntent().getBooleanExtra("attachment_usage_reference", false));
        nativeDepthLayoutReference(getIntent().getBooleanExtra("depth_layout_reference", false));
        nativeComputeLayoutReference(getIntent().getBooleanExtra("compute_layout_reference", false));
        nativeReadonlyDepthReference(getIntent().getBooleanExtra("readonly_depth_reference", false));
        nativeClearAttachmentReference(getIntent().getBooleanExtra("clear_attachment_reference", false));
        nativeTextureDirtyReference(getIntent().getBooleanExtra("texture_dirty_reference", false));
        nativeZeroDrawReference(getIntent().getBooleanExtra("zero_draw_reference", false));
        nativeNoFxaaEnabled(getIntent().getBooleanExtra("no_fxaa", false));
        nativeBloomQuarterEnabled(getIntent().getBooleanExtra("bloom_quarter", true));
        nativeMailboxPresent(getIntent().getBooleanExtra("mailbox_present", false));
        nativePresentWorkerReference(getIntent().getBooleanExtra("present_worker_reference", true),
                getIntent().getBooleanExtra("present_worker_shared_queue", false));
        nativeDescriptorTemplateReference(getIntent().getBooleanExtra("descriptor_template_reference", false));
        nativeDescriptorHistoryReference(getIntent().getBooleanExtra("descriptor_history_reference", false));
        nativeDescriptorReuseReference(getIntent().getBooleanExtra("descriptor_reuse_reference", false));
        nativeDescriptorBatchReference(getIntent().getBooleanExtra("descriptor_batch_reference", false));
        nativeDynamicUniformReference(getIntent().getBooleanExtra("dynamic_uniform_reference", false));
        nativeDynamicUniformNarrowReference(getIntent().getBooleanExtra("dynamic_uniform_narrow_reference", false));
        nativeUniformSpecializationEnabled(getIntent().getBooleanExtra("uniform_specialization", true));
        nativeUniformPredicateEnabled(getIntent().getBooleanExtra("uniform_predicate", false));
        nativeDeferredMipClearsEnabled(getIntent().getBooleanExtra("deferred_mip_clears", false));
        if (!hostStarted) {
            String provider = getIntent().getStringExtra("vulkan_provider");
            if (provider == null) provider = getIntent().getBooleanExtra("custom_vulkan_driver", false) ? "adreno" : "system";
            if (!"system".equals(provider) && !"adreno".equals(provider) && !"panvk".equals(provider))
                throw new IllegalArgumentException("vulkan_provider must be system, adreno or panvk");
            if ("panvk".equals(provider) && getIntent().getBooleanExtra("validate", false))
                throw new IllegalArgumentException("PanVK direct ICD does not compose loader validation");
            nativeVulkanDriver(provider, "system".equals(provider) ? null :
                    new File(root, "panvk".equals(provider) ? "vulkan-panfrost" : "vulkan-driver").getAbsolutePath(),
                    getApplicationInfo().nativeLibraryDir);
        }
        nativeStreamSubmitReference(getIntent().getBooleanExtra("stream_submit_reference", false));
        nativeStoreDescriptorReference(getIntent().getBooleanExtra("store_descriptor_reference", false));
        nativeCoverWaitReference(getIntent().getBooleanExtra("cover_wait_reference", false));
        nativeGraphicsStageReference(getIntent().getBooleanExtra("graphics_stage_reference", false));
        nativeSubmissionBarrierReference(getIntent().getBooleanExtra("submission_barrier_reference", false));
        nativeQueryPerDrawReference(getIntent().getBooleanExtra("query_per_draw_reference", false));
        nativePipelinePrepareMode(getIntent().getBooleanExtra("validate", false) ? 2 :
                getIntent().getBooleanExtra("pipeline_prepare_reference", false) ? 1 : 0);
        if (getIntent().getBooleanExtra("run_vulkan_tests", false)) {
            new Thread(() -> nativeTests(root.getAbsolutePath(), getIntent().getBooleanExtra("async_submit", false),
                    getIntent().getBooleanExtra("acquire_scope_test", false),
                    getIntent().getBooleanExtra("memory_test", false),
                    getIntent().getBooleanExtra("vulkan_test_validation", true),
                    getIntent().getBooleanExtra("readback_batch_test", false)), "native-vulkan-test").start();
        }
        SurfaceView view = new SurfaceView(this);
        view.getHolder().addCallback(this);
        android.widget.FrameLayout content = new android.widget.FrameLayout(this);
        content.addView(view);
        touchControls = new TouchControls(this, getIntent().getBooleanExtra("touch_debug", false));
        touchControls.continuousRedraw(getIntent().getBooleanExtra("overlay_continuous", false));
        content.addView(touchControls);
        debugPanel = new DebugPanel(this, debugSettings, new DebugPanel.Actions() {
            @Override public void close() { showDebugPanel(false); }
            @Override public void thermalCap(boolean enabled) {
                debugSettings.thermalCap = enabled; thermalPolicy.setLimitEnabled(enabled);
            }
            @Override public void thermalPause(boolean enabled) {
                debugSettings.thermalPause = enabled; thermalPolicy.setPauseEnabled(enabled);
            }
            @Override public void manualCap(int fps) { debugSettings.manualCap = fps; nativeManualLimit(fps); }
            @Override public void scalePercent(int percent) { debugSettings.scalePercent = percent; nativeManualScale(percent); }
        });
        content.addView(debugPanel);
        nativeManualLimit(debugSettings.manualCap);
        nativeManualScale(debugSettings.scalePercent);
        thermalPolicy.setLimitEnabled(debugSettings.thermalCap);
        thermalPolicy.setPauseEnabled(debugSettings.thermalPause);
        setContentView(content);
        immersive();
        if (android.os.Build.VERSION.SDK_INT >= 33) {
            backCallback = this::gameBack;
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT, backCallback);
        }
        keyboardHandler.post(keyboardPoll);
    }
    // 系统返回沿原游戏的返回/菜单路径执行；HOME 仍由正常生命周期存盘。
    // System Back uses original game back/menu actions; HOME uses lifecycle saving.
    private void gameBack() {
        if (debugPanelVisible()) { showDebugPanel(false); return; }
        if (!started) return;
        resetInput();
        nativeBack();
    }
    private boolean debugPanelVisible() { return debugPanel != null && debugPanel.getVisibility() == View.VISIBLE; }
    void showDebugPanel(boolean visible) {
        if (debugPanel == null || visible == debugPanelVisible()) return;
        // Cancel, never UP: opening over a held skill/joystick must not release a skill.
        touchControls.blockForDebug(visible);
        GameInput.reset();
        debugPanel.setVisibility(visible ? View.VISIBLE : View.GONE);
        if (visible) debugPanel.sync();
    }
    void updateDebugPanel(float fps, String battery, int status) {
        if (!debugPanelVisible()) return;
        nativeDebugState(debugState);
        debugPanel.update(fps, battery, status, debugState);
    }
    @Override public void onBackPressed() { gameBack(); }
    // 仅 UI 线程进入 JNI；游戏线程不附加到 ART，保留原原生线程的 TLS 和栈环境。
    // Only the UI thread enters JNI; keep game threads outside ART with their original native TLS and stacks.
    private final android.os.Handler keyboardHandler = new android.os.Handler(android.os.Looper.getMainLooper());
    private final Runnable keyboardPoll = new Runnable() {
        @Override public void run() {
            if (resumed && textDialog == null) {
                String[] request = nativePollText();
                if (request != null) showText(request);
            }
            keyboardHandler.postDelayed(this, 32);
        }
    };
    private void showText(String[] request) {
        resetInput();
        touchControls.running(false);
        long id = Long.parseLong(request[6]);
        int maximum = Integer.parseInt(request[4]);
        android.widget.EditText input = new android.widget.EditText(this);
        input.setSingleLine(true);
        input.setInputType(request[5].equals("1") ? android.text.InputType.TYPE_CLASS_NUMBER : (android.text.InputType.TYPE_CLASS_TEXT | android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS));
        input.setText(request[3]);
        input.setSelection(input.length());
        input.setHint(request[2]);
        if (maximum > 0) input.setFilters(new android.text.InputFilter[]{new android.text.InputFilter.LengthFilter(maximum)});
        textDialog = new android.app.AlertDialog.Builder(this).setTitle(request[0]).setMessage(request[1]).setView(input)
            .setPositiveButton(android.R.string.ok, (dialog, which) -> nativeCompleteText(id, input.getText().toString()))
            .setNegativeButton(android.R.string.cancel, (dialog, which) -> {})
            .create();
        textDialog.setOnDismissListener(dialog -> { textDialog = null; resetInput(); nativeCompleteText(id, null); if (resumed) touchControls.running(true); });
        textDialog.show();
        textDialog.getWindow().setSoftInputMode(android.view.WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE);
        input.requestFocus();
    }
    @Override protected void onDestroy() {
        if (sharedImageFixture || desktopProfileRejected) { super.onDestroy(); return; }
        if (android.os.Build.VERSION.SDK_INT >= 33 && backCallback != null)
            getOnBackInvokedDispatcher().unregisterOnBackInvokedCallback(backCallback);
        keyboardHandler.removeCallbacks(keyboardPoll);
        thermalHandler.removeCallbacksAndMessages(null);
        if (textDialog != null) textDialog.dismiss();
        GameInput.shutdown();
        super.onDestroy();
    }
    @Override protected void onNewIntent(android.content.Intent intent) {
        super.onNewIntent(intent);
        if (desktopProfileRejected) return;
        if (readbackBatchFixtureStarted || intent.hasExtra("readback_batch_test")) {
            android.util.Log.w("D2RVulkan", "Readback oracle is cold-process-only; ignoring new Intent");
            return;
        }
        if (sharedImageFixture || intent.hasExtra("shared_image_test")) {
            android.util.Log.w("D2RPool", "Shared image oracle is cold-process-only; ignoring new Intent");
            return;
        }
        if (intent.hasExtra("legacy_astc_copy") || intent.hasExtra("legacy_astc_copy_test")) {
            android.util.Log.w("D2RASTC", "legacy_astc_copy is startup-only; cold restart required");
            intent.removeExtra("legacy_astc_copy");intent.removeExtra("legacy_astc_copy_test");
        }
        for (String key : new String[]{"native_resource_table", "native_resource_pack_sha256", "native_resource_producer_sha256", "native_resource_fixture_dir"}) {
            if (intent.hasExtra(key)) {
                android.util.Log.w("D2RNativeResources", "native resources require a cold restart");
                intent.removeExtra(key);
            }
        }
        if (intent.hasExtra("image_buffer_split")) {
            android.util.Log.w("D2RDriver", "image_buffer_split is startup-only; cold restart required");
            intent.removeExtra("image_buffer_split");
        }
        if (intent.hasExtra("state_segments")) {
            android.util.Log.w("D2RDriver", "state_segments is startup-only; cold restart required");
            intent.removeExtra("state_segments");
        }
        if (intent.hasExtra("native_read_group_front")) {
            android.util.Log.w("D2RDriver", "native_read_group_front is startup-only; cold restart required");
            intent.removeExtra("native_read_group_front");
        }
        if (intent.hasExtra("attachment_job_reuse")) {
            android.util.Log.w("D2RDriver", "attachment_job_reuse is startup-only; cold restart required");
            intent.removeExtra("attachment_job_reuse");
        }
        if (intent.hasExtra("attachment_raster_run")) {
            android.util.Log.w("D2RDriver", "attachment_raster_run is startup-only; cold restart required");
            intent.removeExtra("attachment_raster_run");
        }
        if (intent.hasExtra("buffer_consumption_plan")) {
            android.util.Log.w("D2RDriver", "buffer_consumption_plan is startup-only; cold restart required");
            intent.removeExtra("buffer_consumption_plan");
        }
        if (intent.hasExtra("driver_span_reference")) {
            android.util.Log.w("D2RDriver", "driver_span_reference is startup-only; cold restart required");
            intent.removeExtra("driver_span_reference");
        }
        if (intent.hasExtra("shared_lazy_windows")) {
            android.util.Log.w("D2RPool", "shared_lazy_windows is startup-only; cold restart required");
            intent.removeExtra("shared_lazy_windows");
        }
        if (intent.hasExtra("shared_image_raw_observe")) {
            android.util.Log.w("D2RPool", "shared_image_raw_observe is startup-only; cold restart required");
            intent.removeExtra("shared_image_raw_observe");
        }
        if (intent.hasExtra("shared_guest_heap")) {
            android.util.Log.w("D2RPool", "shared_guest_heap is startup-only; cold restart required");
            intent.removeExtra("shared_guest_heap");
        }
        if (intent.hasExtra("vulkan_provider") || intent.hasExtra("custom_vulkan_driver")) {
            android.util.Log.w("D2RVulkan", "Vulkan provider is startup-only; cold restart required");
            intent.removeExtra("vulkan_provider");intent.removeExtra("custom_vulkan_driver");
        }
        if (intent.hasExtra("defer_pool_publication")) {
            android.util.Log.w("D2RPool", "defer_pool_publication is startup-only; cold restart required");
            intent.removeExtra("defer_pool_publication");
        }
        if (intent.hasExtra("pool_copy_accounting")) {
            android.util.Log.w("D2RPool", "pool_copy_accounting is startup-only; cold restart required");
            intent.removeExtra("pool_copy_accounting");
        }
        if (intent.hasExtra("upload_dual_store")) {
            android.util.Log.w("D2RUpload", "upload_dual_store is startup-only; cold restart required");
            intent.removeExtra("upload_dual_store");
        }
        if (intent.hasExtra("pool_window_bytes")) {
            android.util.Log.w("D2RPool", "pool_window_bytes is startup-only; cold restart required");
            intent.removeExtra("pool_window_bytes");
        }
        if (intent.hasExtra("pixel_local_shared_ds")) {
            android.util.Log.w("D2RPixelLocal", "pixel_local_shared_ds is startup-only; cold restart required");
            intent.removeExtra("pixel_local_shared_ds");
        }
        if (intent.hasExtra("pixel_local")) {
            android.util.Log.w("D2RPixelLocal", "pixel_local is startup-only; cold restart required");
            intent.removeExtra("pixel_local");
        }
        setIntent(intent);
        if (intent.hasExtra("display_refresh_hz")) {
            getIntent().putExtra("display_refresh_hz", intent.getFloatExtra("display_refresh_hz", 0));
            requestDisplayRefresh(surfaceHolder == null ? null : surfaceHolder.getSurface());
        }
        if (intent.getBooleanExtra("vma_dump_on_suspend", false)) {
            nativeVmaMemoryDumpRequest();
            intent.removeExtra("vma_dump_on_suspend"); // one explicit request, not a preference
        }
        if (intent.getBooleanExtra("renderdoc_capture", false)) {
            File directory = new File(getFilesDir(), "runtime/profiles/renderdoc");
            if (directory.isDirectory() || directory.mkdirs())
                nativeRenderdocCapture(directory.getAbsolutePath());
        }
        if (intent.hasExtra("watch_class"))
            nativeWatch(intent.getIntExtra("watch_type", 1), intent.getIntExtra("watch_class", -1));
        // 仅供同进程性能对照，保留相同输入轮询与画面内容。
        // Same-process performance control, preserving input polls and pixels.
        if (intent.hasExtra("overlay_continuous") && touchControls != null)
            touchControls.continuousRedraw(intent.getBooleanExtra("overlay_continuous", false));
        if (intent.hasExtra("pipeline_byte_hash"))
            nativePipelineByteHash(intent.getBooleanExtra("pipeline_byte_hash", false));
        if (intent.hasExtra("pipeline_prepare_reference"))
            nativePipelinePrepareMode(intent.getBooleanExtra("pipeline_prepare_reference", false) ? 1 : 0);
        if (intent.hasExtra("performance_hint_reference"))
            nativePerformanceHintReference(intent.getBooleanExtra("performance_hint_reference", true));
        if (intent.hasExtra("image_layout_reference"))
            nativeImageLayoutReference(intent.getBooleanExtra("image_layout_reference", false));
        if (intent.hasExtra("sampled_array_layout_reference"))
            nativeSampledArrayLayoutReference(intent.getBooleanExtra("sampled_array_layout_reference", false));
        if (intent.hasExtra("buffer_memory_reference"))
            nativeBufferMemoryReference(intent.getBooleanExtra("buffer_memory_reference", false));
        if (intent.hasExtra("stream_submit_reference"))
            nativeStreamSubmitReference(intent.getBooleanExtra("stream_submit_reference", true));
        if (intent.hasExtra("store_descriptor_reference"))
            nativeStoreDescriptorReference(intent.getBooleanExtra("store_descriptor_reference", true));
        if (intent.hasExtra("pool_sync_reference"))
            nativePoolSyncReference(intent.getBooleanExtra("pool_sync_reference", false));
        if (intent.hasExtra("storage_bias_reference"))
            nativeStorageBiasReference(intent.getBooleanExtra("storage_bias_reference", false));
        if (intent.hasExtra("readback_coherent_reference"))
            nativeReadbackCoherentReference(intent.getBooleanExtra("readback_coherent_reference", false));
        if (intent.hasExtra("cpu_copy_reference"))
            nativeCpuCopyReference(intent.getBooleanExtra("cpu_copy_reference", false));
        if (intent.hasExtra("driver_constant_reference"))
            nativeDriverConstantReference(intent.getBooleanExtra("driver_constant_reference", false));
        if (intent.hasExtra("sample_liveness_reference"))
            nativeSampleLivenessReference(intent.getBooleanExtra("sample_liveness_reference", false));
        if (intent.hasExtra("cover_wait_reference"))
            nativeCoverWaitReference(intent.getBooleanExtra("cover_wait_reference", false));
        if (intent.hasExtra("graphics_stage_reference"))
            nativeGraphicsStageReference(intent.getBooleanExtra("graphics_stage_reference", false));
        if (intent.hasExtra("submission_barrier_reference"))
            nativeSubmissionBarrierReference(intent.getBooleanExtra("submission_barrier_reference", false));
        if (intent.hasExtra("clear_attachment_reference"))
            nativeClearAttachmentReference(intent.getBooleanExtra("clear_attachment_reference", false));
        if (intent.hasExtra("texture_dirty_reference"))
            nativeTextureDirtyReference(intent.getBooleanExtra("texture_dirty_reference", false));
        if (intent.hasExtra("zero_draw_reference"))
            nativeZeroDrawReference(intent.getBooleanExtra("zero_draw_reference", false));
        if (intent.hasExtra("descriptor_template_reference"))
            nativeDescriptorTemplateReference(intent.getBooleanExtra("descriptor_template_reference", false));
        if (intent.hasExtra("descriptor_history_reference"))
            nativeDescriptorHistoryReference(intent.getBooleanExtra("descriptor_history_reference", false));
        if (intent.hasExtra("descriptor_reuse_reference"))
            nativeDescriptorReuseReference(intent.getBooleanExtra("descriptor_reuse_reference", false));
        if (intent.hasExtra("descriptor_batch_reference"))
            nativeDescriptorBatchReference(intent.getBooleanExtra("descriptor_batch_reference", false));
        if (intent.hasExtra("dynamic_uniform_reference"))
            nativeDynamicUniformReference(intent.getBooleanExtra("dynamic_uniform_reference", false));
        if (intent.hasExtra("dynamic_uniform_narrow_reference"))
            nativeDynamicUniformNarrowReference(intent.getBooleanExtra("dynamic_uniform_narrow_reference", false));
        if (intent.hasExtra("pool_lookup_reference"))
            nativePoolLookupReference(intent.getBooleanExtra("pool_lookup_reference", false));
        if (intent.hasExtra("upload_scan_reference"))
            nativeUploadScanReference(intent.getBooleanExtra("upload_scan_reference", false));
        if (intent.hasExtra("texture_binding_reference"))
            nativeTextureBindingReference(intent.getBooleanExtra("texture_binding_reference", false));
        if (intent.hasExtra("texel_storage"))
            nativeTexelStorage(intent.getBooleanExtra("texel_storage", false));
        if (intent.hasExtra("shader_present"))
            nativeShaderPresent(intent.getBooleanExtra("shader_present", false));
        if (intent.hasExtra("sampler_lookup_reference"))
            nativeSamplerLookupReference(intent.getBooleanExtra("sampler_lookup_reference", false));
        if (intent.hasExtra("uniform_specialization"))
            nativeUniformSpecializationEnabled(intent.getBooleanExtra("uniform_specialization", true));
        if (intent.hasExtra("uniform_predicate"))
            nativeUniformPredicateEnabled(intent.getBooleanExtra("uniform_predicate", false));
        if (intent.hasExtra("deferred_mip_clears"))
            nativeDeferredMipClearsEnabled(intent.getBooleanExtra("deferred_mip_clears", false));
        if (intent.hasExtra("no_fxaa"))
            nativeNoFxaaEnabled(intent.getBooleanExtra("no_fxaa", false));
        if (intent.hasExtra("bloom_quarter"))
            nativeBloomQuarterEnabled(intent.getBooleanExtra("bloom_quarter", true));
        if (intent.hasExtra("query_per_draw_reference"))
            nativeQueryPerDrawReference(intent.getBooleanExtra("query_per_draw_reference", false));
    }
    @Override public void surfaceCreated(SurfaceHolder holder) {
        surfaceHolder = holder;
        requestDisplayRefresh(holder.getSurface());
        if (!getIntent().getBooleanExtra("run_game", true) || getIntent().getBooleanExtra("run_vulkan_tests", false)) return;
        started = true;
        if (!hostStarted) {
            String operationMode = getIntent().hasExtra("operation_mode") ?
                    getIntent().getStringExtra("operation_mode") : "docked";
            if (!"docked".equals(operationMode) && !"handheld".equals(operationMode))
                throw new IllegalArgumentException("operation_mode must be docked or handheld");
            nativeOperationMode("handheld".equals(operationMode) ? 0 : 1);
            hostStarted = true;
            nativeStart(holder.getSurface(), new File(getFilesDir(), "runtime").getAbsolutePath(),
                getIntent().getBooleanExtra("validate", false), refresh(),
                getIntent().getIntExtra("trace_frame", -1), getIntent().getIntExtra("watch_type", 1),
                getIntent().getIntExtra("watch_class", -1), getIntent().getBooleanExtra("gpu_profile", false),
                getIntent().getFloatExtra("bright_k", -1), getIntent().getBooleanExtra("panel_freeze", false),
                getIntent().getBooleanExtra("runtime_extrap", false), getIntent().getBooleanExtra("external_profile",
                        !getIntent().getBooleanExtra("validate", false) &&
                        !getIntent().getBooleanExtra("gpu_profile", false) &&
                        getIntent().getIntExtra("trace_frame", -1) < 0),
                getIntent().getStringExtra("game_res"), getIntent().getFloatExtra("scale_3d", -1),
                getIntent().getBooleanExtra("fixed_3d", false), getIntent().getBooleanExtra("async_submit", true));
        } else {
            nativeSurface(holder.getSurface(), refresh());
        }
        if (resumed) {
            nativeSuspend(false);
            deviceThermalStatus(lastThermalStatus);
        }
    }
    private final android.os.Handler thermalHandler = new android.os.Handler(android.os.Looper.getMainLooper());
    private int lastThermalStatus = -1;
    private final ThermalPolicy thermalPolicy = new ThermalPolicy(new ThermalPolicy.Scheduler() {
        @Override public long now() { return android.os.SystemClock.uptimeMillis(); }
        @Override public void postDelayed(Runnable task, long delay) { thermalHandler.postDelayed(task, delay); }
        @Override public void removeCallbacks(Runnable task) { thermalHandler.removeCallbacks(task); }
    }, new ThermalPolicy.Actions() {
        @Override public void limit(int fps) { nativeThermalLimit(fps); }
        @Override public void pause() {
            android.util.Log.w("DmThermal", "Persistent/severe thermal pressure: original save and background suspend");
            android.widget.Toast.makeText(MainActivity.this, "设备过热，正在保存并暂停，请降温后继续", android.widget.Toast.LENGTH_LONG).show();
            moveTaskToBack(true);
        }
    });
    void deviceThermalStatus(int status) {
        if (status != lastThermalStatus) {
            lastThermalStatus = status;
            android.util.Log.i("DmThermal", "Android API thermal status=" + status);
        }
        thermalPolicy.setActive(resumed && started);
        thermalPolicy.status(status);
    }
    @Override protected void onResume() {
        super.onResume();
        if (sharedImageFixture || desktopProfileRejected) return;
        resumed = true;
        lastThermalStatus = -1;
        thermalPolicy.setActive(started);
        if (touchControls != null) {
            touchControls.running(true);
            touchControls.monitorThermal(true);
        }
        if (started && surfaceHolder != null) nativeSuspend(false);
    }
    @Override public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (!focused && started) resetInput();
        if (focused) immersive();
    }
    @Override public boolean dispatchKeyEvent(android.view.KeyEvent event) {
        if (debugPanelVisible()) {
            if (event.getKeyCode() == android.view.KeyEvent.KEYCODE_BACK) {
                if (event.getAction() == android.view.KeyEvent.ACTION_UP) showDebugPanel(false);
                return true;
            }
            // Skip GameInput, but retain platform volume keys and widget navigation.
            return super.dispatchKeyEvent(event);
        }
        return started && GameInput.key(event) || super.dispatchKeyEvent(event);
    }
    @Override public boolean onGenericMotionEvent(android.view.MotionEvent event) {
        if (debugPanelVisible()) return true;
        return started && GameInput.motion(event) || super.onGenericMotionEvent(event);
    }
    @Override public void onTrimMemory(int level) {
        super.onTrimMemory(level);
        if (sharedImageFixture || desktopProfileRejected) return;
        nativeTrimMemory();
    }
    @Override public void onLowMemory() {
        super.onLowMemory();
        if (sharedImageFixture || desktopProfileRejected) return;
        nativeTrimMemory();
    }
    @Override protected void onPause() {
        if (sharedImageFixture || desktopProfileRejected) { super.onPause(); return; }
        resumed = false;
        thermalPolicy.setActive(false);
        if (textDialog != null) textDialog.dismiss();
        nativeCompleteText(0, null);
        resetInput();
        if (touchControls != null) {
            touchControls.running(false);
            touchControls.monitorThermal(false);
        }
        if (started) nativeSuspend(true);
        super.onPause();
    }
    @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {}
    @Override public void surfaceDestroyed(SurfaceHolder holder) {
        if (started) {
            nativeSuspend(true);
            nativeSurface(null, refresh());
        }
        surfaceHolder = null;
    }
}
