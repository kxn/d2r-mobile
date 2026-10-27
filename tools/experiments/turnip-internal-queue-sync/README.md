# Single Android opt-in: internally synchronized queues

This independent driver candidate derives from the exact frozen `0e53ffa2…` native-BC3 driver. It adds one entry to Mesa's Android extension whitelist: `VK_KHR_internally_synchronized_queues` at the existing API34 **build** floor. Turnip's hardware/API feature and standard runtime locking already exist. The original Android strict whitelist suppressed advertisement and rejected device creation before this change.

This is an app-local experiment, not Android CTS approval or a claim that all Android34 devices qualify. Intended device testing is restricted to the already identified Android16 Q735. Keeping the original `platform-sdk-version=34` is deliberate: changing the global SDK to36 would expose other extensions too. The KHR extension itself adds a feature and queue flag using the existing Vulkan1.1 `vkGetDeviceQueue2`, not a new Android function.

Only `src/vulkan/util/vk_extensions.py` may change. The builder verifies all18,218 baseline source entries, the exact0e driver/ledger, original compiler/tools and cross file. It uses an independent source/build directory and the original setup options. After compilation it compares the entire generated `vk_extensions.c` byte-for-byte, accepting only this single Android allow-table field's change from false to `ANDROID_API_LEVEL >= 34`.

```sh
python3 tools/experiments/turnip-internal-queue-sync/build.py --verify-only
python3 tools/experiments/turnip-internal-queue-sync/build.py \
  --output .local/build/turnip-internal-queue-sync-v1
```

No image layout, layout-profile salt, UUID algorithm, memory import/coherency, compression, WSI or driver dispatch entry point is changed. Actual driver/layout/pipeline UUIDs must still be queried on-device; this source scope does not prove their values remain identical. Old resources may be reused only after the existing identity checks accept the actual new driver.

Qualification remains staged: verify actual public extension advertisement and feature in the isolated ordinary-UID cold-query APK; then explicitly enable the feature, create a queue with flag4 and obtain it using matching `vkGetDeviceQueue2` flags; then test the existing WSI worker with its whole-present external lock removed only for the actual flagged queue. Swapchain ownership, per-image semaphore reuse, GPU dependencies and teardown draining remain required. A successful build is not device compatibility or a performance result.
