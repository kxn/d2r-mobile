# Native Android TLS ABI

Android's dynamic linker owns ELF TLS allocation. Initial-exec TLS requires the library to
exist at process startup; dynamic TLSDESC supports an ordinary dlopen/System.loadLibrary.
See the [Bionic ELF TLS design](https://android.googlesource.com/platform/bionic/+/HEAD/docs/elf-tls.md)
and [Arm ELF64 TLS descriptor relocations](https://github.com/ARM-software/abi-aa/blob/main/aaelf64/aaelf64.rst).

The original offline rewriter/loader now have three explicit ELF models. Darwin stays unchanged.

| Rewrite | HOST_ABI | Key-page values | Host build |
|---|---|---|---|
| DM_HOST_ABI=elf | 1 | Fixed signed TP-relative slot offsets | Original initial-exec debug preload |
| DM_HOST_ABI=elfdesc | 2 | Native linker-created TLS descriptor pointer | DM_TLS_DESC, ordinary dynamic loading |
| DM_HOST_ABI=elfdesc-gate | 3 | The same eight TLS slots, plus a read-only process gate address | DM_TLS_DESC, ordinary dynamic loading |

For ABI 2, platform_elf.c declares compiler-managed dynamic TLS and obtains its native GOT
descriptor through AArch64 TLSDESC relocations. Each offline slot-address veneer calls that
system resolver, adds the current hardware thread pointer and logical slot offset, then
continues executing the original instruction. It preserves x0/x1/LR and NZCV; the TLSDESC
calling convention preserves the other registers. There is no CPU interpreter, runtime
instruction translation or private Bionic TSD/DTV layout lookup. Guest-thread initialization
touches the TLS block before guest execution. Key pages remain read-only after installation.

ABI 3 leaves single-instruction TLS access templates byte-identical to ABI 2. The process-wide constant `dm_gate`
address occupies a separate read-only key-page slot 8 (`HOST_GATE_IDX=8`), so SVC, counter
and observation gates avoid a dynamic TLS resolver call just to obtain that address.
The ordinary gate template drops from 25 instructions to 6. All eight thread slots,
thread initialization, jump tables and gate register preservation remain unchanged.
Use `--abi elfdesc-gate` to rebuild the complete bundle from original NSOs. Old hosts
reject ABI 3; never relabel or patch processed binaries. ABI 2 remains available as reference.

On 2026-10-19, ABI 3 passed real ARM64 ordinary-dlopen/preexisting-thread/64-thread,
integer/floating-state, 256-way jump-table and original concurrency fixtures. Original
build 94012 main/sdk/subsdk0 modules were rebuilt with verified provenance and ran
character selection, camp and field before two original lifecycle save commits and stop.
No controlled whole-frame speedup is claimed from the instruction reduction. Private
evidence is under `.local/logs/process-gate-v1-*`; the verified module bundle is
`.local/build/native-touch-elfdesc-process-gate-v1`.

ABI 3 also supports offline grouping of adjacent ordinary x18 instructions:
`rewrite-native.py --abi elfdesc-gate --x18-group-size 3` (the ABI 3 default).
Two or three instructions share one real TLS address lookup, while each instruction
immediately publishes logical x18. Every original instruction retains an independent
entry, including a complete lookup when entered in the middle of a group. SP, control
flow, system and atomic instructions are excluded. Later rule overrides trigger final
validation and fallback. `--x18-group-size 1` retains individual lookups as reference.
The option is included in provenance and cache fingerprints; rebuild original NSOs.
ABI 2 does not enable grouping.

Real ARM64 tests cover middle entries, registers/NZCV, signals, concurrent exclusive
increments and 64 threads. Both options also ran the real game in camp with the same
APK. Both were thermally capped at 30 FPS, so no controlled whole-frame gain is claimed.
Reduced lookup/instruction work is not a whole-device speedup. Evidence is under
`.local/logs/x18-groups-v2-*`; bundles are
`.local/build/native-touch-x18-groups-v2-reference` and
`.local/build/native-touch-x18-groups-v2-candidate`.

The loader rejects incompatible model combinations. Output directories and rewrite fingerprints are separate.
Native fixtures run all three models, including x18, original-SP/writeback, x0/x1/LR destinations,
NZCV/SIMD preservation, forced HLE, signals and 200000 concurrent exclusive increments.
The dynamic library also passes a thread synchronized before dlopen plus 64 concurrent probes.

On PHP110/API 36/4 KB pages, the no-wrap APK passed ordinary System.loadLibrary; observed
per-thread TP offsets differ and thread isolation passes. Its actual process environment has
no LD_PRELOAD. Three real game modules reach character selection and camp, touch movement
works, and three HOME/Surface rebuild cycles preserve PID and resume fresh world/binding
snapshots. The same APK passes 35429 original NVN/Vulkan checks with packaged Khronos
synchronization validation. These results establish native loading, not full game acceptance,
release signing/distribution, or 16 KB-page-device coverage.

Build and matching deployment paths are in [the Android shell guide](../dm/android/README.md).
Private evidence includes android-native-tlsdesc-test.log, android-native-ie-regression.log,
android-native-wrong-tls-abi.log, android-tlsdesc-game-probe.json,
android-tlsdesc-background-roundtrip.json and android-vulkan-tlsdesc.log under .local/logs/.
