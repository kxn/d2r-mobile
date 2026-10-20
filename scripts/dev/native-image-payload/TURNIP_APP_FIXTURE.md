# Turnip host-image ordinary-UID experiment

This isolated fixture reuses the existing native-image producer and the existing
Android JNI/Activity entry. It does not load game modules or modify the original
app's files. Its package is `com.d2rmobile.turniphostimagefixture`.

Only the experimental aa185374ef6c1135ddcbff08506aa4dc720e29b82e45a9ae9f5ce767f54564db
Turnip build is accepted by the controller. Driver file SHA is separate from the
three actual queried layout/device/driver UUIDs. The native oracle, not a host
formula or the ELF BuildID, obtains offsets and the opaque payload.

Build in the isolated worktree after committing its source:

```sh
source scripts/dev/env.sh
export DM_ROOT=/home/dl/d2r-mobile/work
export DM_ROMFS_SRC="$DM_ROOT/data/switch/3.3.94012/Update/RomFS"
export DM_ORIGINAL_ROMFS="$DM_ROMFS_SRC"
cmake -S dm/host/android -B .local/build/android-native -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 \
  -DDM_ANDROID_TLS_DESC=ON -DDM_ANDROID_NATIVE_IMAGE_FIXTURE=ON \
  -DDM_ANDROID_RUNTIME_OPT_LEVEL=3 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
python3 scripts/dev/build-android-apk.py --tlsdesc --opt-level 3 --native-jobs 2
python3 scripts/dev/build-android-native-image-fixture.py
```

Archive the resulting independent APK as `candidate.apk` together with its
`candidate.apk.build.json`. The controller's default is an entirely offline plan.
Root supplies its current stopped-device lease and original baseline values:

```sh
python3 scripts/dev/run-turnip-native-image-fixture.py \
  --serial "$SERIAL" --artifact "$FIXTURE_ARCHIVE" --driver "$EXACT_AA185_DRIVER" \
  --original-artifact "$EXACT_ORIGINAL_APK" --module-report "$DEPLOYED_28_MODULE_REPORT" \
  --expected-original-apk "$ORIGINAL_APK_SHA256" --expected-original-elf "$ORIGINAL_ELF_SHA256" \
  --expected-shaderchain "$ORIGINAL_CHAIN" --output "$NEW_UNIQUE_OUTPUT"
```

Add `--execute` only with the explicit device lease. There is one install and
exactly two cold starts: bake then import. No game is launched; normal completion
stops the owned test PID and uninstalls only the independent fixture. An ambiguous
or naturally crashing PID is preserved rather than adopted or force-stopped.
Original APK/ELF/28 modules/chain are checked before and after. Optional resource
report hashes are read-only. There is no performance timer or performance claim.

The profile tests 8x8 ASTC4x4/6x6 UNORM and sRGB, 4 mips, one 2D layer. Existing
byte and GPU sample assertions retain all mip pixels, source payload digest,
sentinel bytes, two patterns and destroy/reimport lifetimes. Query negatives
include storage/attachment/unsupported shape/foreign-host handle; imported bind
negatives include unaligned offset and insufficient allocation. Queue family
FOREIGN is deliberately retained for the existing external-producer ownership
contract; it is distinct from the rejected HOST_MAPPED_FOREIGN handle type.

The fixture requires actual cached coherent host memory, nonzero independently
queried UUIDs and the app-private exact driver in process maps. The direct ICD
fixture disables validation explicitly. The negative binds call the experimental
driver directly; absence of VUID text is not validation coverage. Compilation,
mocked ownership tests and shell-UID probes do not establish ordinary app-UID GPU
correctness. Even a successful future fixture run will not establish real-game
acceptance, RSS reduction or FPS benefit.
