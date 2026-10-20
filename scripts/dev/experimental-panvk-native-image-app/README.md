# Dedicated ordinary UID native-image fixture

This wrapper is an isolated experimental package, not the game build. It is
based on the already accepted bcacdf1 cold-controller/70848bd capability lineage
and imports the exact frozen 3446abb native-image source package. All original
fixture sources and the earlier driver remain unchanged.

The Android build option `DM_ANDROID_NATIVE_IMAGE_FIXTURE` is **OFF** by default.
When explicitly enabled, a separate cold JNI entry invokes the original provider
loader, enables both experimental driver options before its first initialization
and calls only `dm_native_image_fixture_run`. No game NSO or original NVN test is
loaded or run. The existing `nativeTests` JNI signature is unchanged.

The independent installed package is **com.d2rmobile.nativeimagefixture**; the
qualified Java/JNI class remains `com.d2rmobile.nativeport.MainActivity`. Official
aapt2/zipalign/apksigner preserve each base APK payload except the manifest and
optional unused validation layer. Six native libraries and the Java dex remain
byte-identical to the independently built base APK. The source manifest includes
the standalone C/include/shader/CMake inputs and generated SPIR-V header.

`run-panvk-native-image-fixture.py` has no device side effects without `--execute`.
It requires current original APK/ELF/module/resource/shaderchain bindings for an
actual run. It never installs over the original package or writes its files.
It accepts only the exact frozen native-image driver 6b15cf08… and executes a
fixed sequence of **two** fresh owned processes: `--native-bake`, then
`--native-import`. It does not retry a failed import, unprotect pages or launch
any other test. Within those functions, real format/create/UUID/physical-offset
checks precede image binding and actual GPU byte sampling.

The direct PanVK ICD does not compose Android validation layers. Java requires
explicit `vulkan_test_validation=false`, and the JNI log reports validation and
synchronization validation as **disabled_explicitly**. Zero errors cannot be
reported as VUID/SYNC acceptance. The host LVP old API regression is independent
and does not exercise native ASTC/import.

Both launch modes also require `run_game=false`, `native_image_test=true`,
`vulkan_driver=panvk`, with `native_image_mode=--native-bake` or `--native-import`.
The original Vulkan fixtures and game startup are mutually exclusive with this
entry. One installation is removed only after its exact APK hash and first PID
ownership are verified. Crash children/replacement/ambiguous PIDs are never
adopted or force-stopped; the original failure is retained with bounded natural
exit handling. Final original package hashes and absence of its process must
match the preflight. The offline mock test reuses these exact parsed controller
helpers and launch loop to cover this ownership/failure behavior.

Actual Mali GPU results are not provided by source compilation or an offline
plan. This wrapper only prepares the owned proof. It does not implement a game
packaged8B/StorageSize/mip-streaming or immutable-texture authority protocol and
does not claim a frame rate, bandwidth or memory saving for the game.
