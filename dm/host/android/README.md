# Android native host

This target adapts the original `dm/host`. Game ARM64 instructions execute directly on the phone CPU.
It has no Unicorn, CPU interpreter, JIT or emulator service dependency. See the repository's AGENTS.md
and [port plan](../../../docs/ANDROID_NATIVE_PORT_PLAN.md).

## Build and device validation

From the repository root, using the existing `.local` toolchain and workspace:

```bash
bash scripts/dev/build-android-native.sh --rewrite
bash scripts/dev/test-android-native.sh
# With multiple devices:
bash scripts/dev/test-android-native.sh DEVICE_SERIAL
```

Requires NDK r28c, CMake, Ninja, Python lz4/capstone, and clang/LLVM/LLD.
The target uses ELF TLS and requires Android ARM64 API 29+. Game inputs live under
`$DM_ROOT/data/switch/3.3.94012/Update/{ExeFS,RomFS}`. Rewrites are cached by input/rewriter fingerprint;
the default Apple rewrite mode is preserved.

Artifacts are in `.local/build/android-native/`: dmhost_native, native_rule_test, native_fs_test,
native_hid_test and native_touch_test. ADB deployment uses `/data/local/tmp/d2r-native/` without root
or Switch firmware. The commands here exercise the dedicated command-line boundary target; the separate full Vulkan APK is documented in dm/android/README.md. Initial resources total approximately
8.97 GB; subsequent deployments skip unchanged files.

## Verified scope

On 2026-10-16, PHP110 / MT6983, Android API 36, 4 KB pages:

- Original loader, HLE, entry, memory and threading sources load three real modules, complete SDK startup
  and module construction, and enter nnMain.
- Hardware tests cover x18 virtualization, original-SP addressing/writeback, TLS, timing gates, forced
  HLE, and 200,000 concurrent atomic increments under signal delivery. TLS offsets are checked per thread.
- All 68 original filesystem/save tests, 13 original touch tests and 15 headless HID tests pass.
- The game reaches the NVN nvnDeviceGetProcAddress query. The headless boundary explicitly exits 78;
  this does not establish rendering, gameplay or full compatibility.
- ELF LOAD alignment is 16 KB, linking only libm/libdl/libc. A 16 KB-page device has not been tested.

Report: `.local/logs/android-native-result.json`; detailed stage logs: `android-native-*.log`.
Injected filesystem failures are expected test output; the final failure count must be zero.

## Boundaries

platform_elf.c maps original key-page slots to compiler-managed ELF TLS and preserves host x18.
platform_fs.c supplies Linux snapshots and atomic exchange; hle_fs.c retains transaction semantics.
headless.c supplies frontend/diagnostic boundaries; main.c follows host/main.c startup order.
The dedicated dmhost_native target uses graphics_boundary.c to stop at the original nvnBootstrapLoader takeover. dmhost_vulkan and the APK replace this boundary with the Vulkan implementation behind the original NVN layer. Unknown strong imports/SVCs fail; missing weak imports resolve to zero.

The full APK now renders the real camp, accepts original touch controls and system keyboard input, and passes save/relaunch and Surface recreation. Actual combat kills, item trade/drag, native skill binding and original background save now have device evidence. Thirty-five original feature modules are linked; optional feature coverage, performance, extended stability and distribution remain. The full APK also supports native TLSDESC loading without wrap.sh/preload; see the port plan and dm/android/README.md. Boundary tests alone do not establish APK playability. Digest sources and licenses are retained in
`dm/third_party/runtime-codecs/`.
