# API36 SurfaceControl/AHB correctness fixture

Independent ordinary-UID package `org.kxn.scahb`; it does not load game modules or replace the installed game. The custom driver, adrenotools loader and SDK build/signing flow are reused from `turnip-swapchain-ubwc`. This is a functional test, with no profiler or FPS measurement.

Build from a verified driver ledger:

```sh
python3 tools/experiments/surface-control-ahb/build-fixture.py \
  --driver-build .local/build/turnip-native-bc3-v1 \
  --output .local/build/surface-control-ahb-fixture-v1
```

The builder checks the exact driver hash and reused hook APK, records all source/header hashes, SDK build tools/compiler and platform JAR fingerprints, and verifies signing/alignment. Its API36 performance-hint declarations come from the already downloaded official header in `.local/logs/cpu-gpu-dvfs-feedback-v1/adpf-whole-frame-review/sources/`; that dependency is explicitly hashed. The NDK35 linker stubs are used, and the new API36 entry points are looked up by name; the manifest requires API36. The compatibility struct for the internal-queue query matches the frozen driver tree's Khronos-generated header, also hashed.

Start `.ProbeActivity` with boolean extra `query_only=true` first. Its private `files/sc-ahb.log` records:

- Actual device/driver UUIDs and advertised AHB, external semaphore FD and foreign-queue-family extensions.
- Actual SYNC_FD semaphore import/export features.
- Actual `VK_KHR_internally_synchronized_queues` advertisement and `internallySynchronizedQueues` feature. This only queries the feature; it does not enable it or create a queue with the flag.
- Exactly one official `APerformanceHint_isFeatureSupported` call each for SESSIONS, SURFACE_BINDING, GRAPHICS_PIPELINE, AUTO_CPU and AUTO_GPU. No hint session is created and no frequency setting is changed.

The query-only branch ends with `SCAHB_QUERY_TERMINAL`. It creates no logical device, swapchain, AHB, SurfaceControl or timing sampler. Missing features are recorded, never synthesized from Mesa source.

With `query_only=false`, the app creates three full-window RGBA AHBs, imports each as a Vulkan image, performs a GPU clear of a 64×64 source and blits it to the selected AHB. `ASurfaceTransaction_setBufferWithRelease` receives the actual GPU production sync FD. Backpressure is enabled so dropped transactions cannot masquerade as a successful speedup. An ordinary Android TextView remains over the SurfaceView to check UI composition. Color extra 0/1/2/3 selects red/green/blue/white; changing it in a new intent updates subsequent frames.

The ring is bounded. A callback returns the consumer release FD; a valid FD is imported as a temporary semaphore payload and waited by the GPU before the next target write. The callback alone is **not** treated as safe reuse. FOREIGN ownership transfers and GENERAL external layout are explicit. A per-slot Vulkan fence protects command-pool reuse; there is no CPU `sync_wait` of Android release FDs and no per-frame queue/device idle. Teardown hides the layer and detaches its last buffer, waits for every promised callback, imports outstanding consumer fences into a final GPU drain, then releases resources. A lifecycle callback timeout is a functional failure: it retains the resources until process exit instead of freeing in-flight objects. Its timeout clock is only a standard condition-variable shutdown deadline, never a profiler measurement.

Before any game integration, device validation must independently establish:

1. The exact frozen APK/ELF/driver runs as the separate app UID, and cold-query output is genuine.
2. All three AHB imports and real production/release fence transfers succeed; progress spans multiple complete ring cycles.
3. Screenshots of all four colors have the expected orientation, dimensions, alpha and visible Java label.
4. Pause/resume and Surface resize produce terminal callback counts equal to per-slot submission counts, with no leaked FD/use-after-free or lifecycle failure.
5. Only then compare an optional game adapter behind a cold flag using official Perfetto/SDP. This fixture passing is not game compatibility or performance evidence.

The smaller standard internally synchronized Vulkan queue path takes priority if actual device qualification succeeds. This AHB route remains a separate fallback experiment; it is not required merely to remove the application's whole-`vkQueuePresentKHR` queue mutex.
