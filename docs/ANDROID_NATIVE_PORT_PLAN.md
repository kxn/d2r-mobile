# Android native port: current state and execution plan

Updated 2026-10-17. Complete Android ARM64 support in the original project under root `AGENTS.md`.
Real game ARM64 instructions execute on the phone CPU through the original offline rewriter,
loader, host gates and HLE. No CPU interpreter, JIT, dynamic translation or copied emulator
system services. Keep the original GPU shader translation chain.

## NVN scope and priority

| Layer | Source correspondence | Scope |
| --- | --- | --- |
| NVN objects/recording | Original hle_nvn.c, nvn_obj.h and ARM64 thunks | Reuse the original command format and entrypoints. |
| Generic execution | Original nvn_mtl.m → nvn_vk*.c | Prioritize resources, shader binding, state, draws, copies, synchronization and presentation. No camera or scene semantics. |
| Existing game-specific additions | Original bright_lut, ghost_*, frz_op and rt_* → separate Vulkan files | Preserve upstream rules/defaults; do not create Android-only game rendering logic. |

Camera matrices belong only to the existing optional rt_* extrapolation, not the generic
NVN API. Reading original constants is distinct from modifying the game's camera. Base
NVN compatibility must work with this optional path disabled. Both executors have case
labels for all 39 recorded opcodes; that source inventory does not prove parity for each
format, flag or draw subtype. Non-indexed QUADS was one such missing subtype and is now
ported using the original shared index pattern. Continue auditing semantics and GPU output.

Performance work follows current Android/Mali profiles, after preserving correct output.
The [Metal-to-Vulkan assessment](NVN_SPECIAL_OPTIMIZATIONS.md) is a reference, not an
optimization backlog to complete first. Choose each change from observed bottlenecks and
accept it with controlled device comparisons. Metal's unwritten
DontCare and history-based cross-submit prediction are not unconditional Vulkan defaults.
Prefer vendor hardware counters/timelines for performance diagnosis; separate frame captures,
synchronization validation, custom per-pass timing and normal performance measurements,
and record collection overhead.

Current native output is2772×1240. Fixed3D1.0 English camp, original inventory,
touch movement, background saving and Surface recovery have device evidence.
The original ARM64 rewrite/loader/HLE path remains; no emulator CPU, new game/
camera logic or stacked game-binary patches were introduced.

The generic submission architecture now isolates descriptors, staging, queries,
transient views, draw jobs and retirement per completed submission. Pool GPU-page
access and CPU-write ranges protect mirror reuse. Real GPU completion gates all
readback, event and fence publication. Acquire/render-finished semaphores follow
their actual consumers; internal submissions never advance guest fences.
`--async-submit` includes asynchronous presentation; `--stream-submits` can submit
GPU work early at real target boundaries. Both remain opt-in with a serial default.
Streaming reproduced about36.6% throughput gain in a same-process comparison,
which cannot be added to gains from other APKs or operating conditions. See the
[lifetime contract](VULKAN_SUBMISSION_LIFETIME.md).

Accepted defaults include occlusion spans, scoped graphics consumer stages,
nonblocking optional CPU proofs, cached readback, used-page uploads, and attachment
index descriptor snapshots/empty-stage scan elision. Image-layout, storage-bias,
performance-hint, descriptor-set reuse and shader experiments did not establish
stable net gains and are not default speedups. The new CPU-known source path
removes about42.8% of readback, but two FPS comparisons are about+1.5%/−2.2%; it
remains disabled by default. Original GPU copies and real completion fences remain.

The requested30FPS performance target is unachieved. With observed effective GPU
ceiling383MHz, current native/fixed3D1.0 windows are about7.2FPS. Controlled same-
process cooldown restored848MHz and13.33FPS over four windows,15.91FPS first-window;
this operating-condition change is not a code gain. The unmodified Arm SDK now
captures all10shader cores/4L2 slices in a window aligned to actual presents.
Vendor counters, CPU/scheduler traces, frame capture, validation and untraced FPS
comparisons remain separate. Next work follows measured GPU memory/compiler cost
and remaining dependency scopes while preserving content/quality. See the
[Mali evidence](MALI_PROFILING.md). Latest experimental correctness totals are
host507392/Mali APK507394; byte/pixel checks are not gameplay scenario counts.

## Physical-device state

PHP110 / MT6983 / Mali-G710 MC10 / 16 GB / Android API 36, actual 4 KB kernel pages.
The debug APK runs three real game modules, original SDK initialization/constructors,
character selection and Act I; original settings now persist English. **Full playability is not accepted; there is no
reliable overall completion percentage.**

Verified real-game paths:

- All class models render; Android keyboard creates Barbarian AndroidTest; touch enters camp.
- Original C stick moves; camp 1 → Blood Moor 2. Original pathfinding opens Charsi dialogue,
  trade and the stash.
- Actual melee kill, XP 0→21. Original aim gate and touch drag lock another target and kill,
  XP 21→54; axes/buttons return to zero after release.
- Original free-portal module casts; actual portal taps complete area 2→1→2.
- Drop a starter potion and touch to pick it back up, belt 4→3→4. This does not establish
  natural monster-drop coverage.
- Original item bridge selects a scroll, shows its tooltip and drags it to another inventory
  cell; save/quit/restart preserves the destination.
- Original trade sells an identify scroll, gold 0→1, then buys it back from the shelf,
  gold 1→0; save succeeds.
- Complete touch-run-v4 rewrite is deployed (36 gates, 48 cuts, 21 observation gates),
  together with the original mobile RomFS overlay and startup sequence.
- Original v4 HUD glyphs/numbers passed: life 55/55, mana 10/10, experience bar and four belt counts. Real player-count 1→8→1 and minimap→fullscreen→minimap passed.
- Real HUD binding getter passes FP/object identity checks: ready snapshot, identity_scope=1,
  group_seen=63, twelve slots. Tapping empty B runs the original skill tree/binding modal,
  binds normal attack and commits through the original save transaction.
- Save/quit, APK relaunch/reload and same-process Surface destruction/recreation have evidence.

Thirty-five original host feature modules (actions, cursor, HUD, belt automation, drop filter,
rift, chronicle and others) are linked directly, alongside original diag/crash/target gates
and the binding bridge. **Linking and reaching original pumps does not accept every optional
feature.** Continue real-path coverage below.

## Remaining work, in execution order

| Stage | Work and acceptance | Current gap |
|---|---|---|
| 1 Phone interactions | Original skill binding/set switching, drag/double tap, trade, belt, map, panels, save round trips | Reload after binding writes, other gestures/classes/item types; more item types, charged skills and controller switching need coverage |
| 2 Graphics correspondence | Audit behavior actually implemented in nvn_mtl.m, implement Vulkan equivalents, verify fixtures and real scenes | Descriptor arrays, formats/views, partial clears/copies, instancing and other actual original branches; menu rendering is insufficient |
| 3 Real gameplay | Natural drops/pickup, dungeons/transitions, effects, multiple classes, death/revival, quests, stash/enhancements | Outdoor combat exists; full loop remains uncovered; never fake outcomes by writing player state |
| 4 Performance | Preserve correct output; choose changes from current Android/Mali profiles and controlled comparisons | Accepted defaults/streaming are documented; native hot windows are about7.2FPS, cooled windows13.33FPS, below30FPS. Separate gains cannot be added. |
| 5 Stability/delivery | Repeated background/Surface/cold/warm starts, save-failure recovery, resource import, diagnostics export, release APK/device coverage | Native TLSDESC APK runs camp/movement without preload and passed three background cycles; release configuration/signing, 16 KB hardware and extended stability remain |

Continue directly between milestones without waiting for routine permission. First functional
success does not remove performance, stability or packaging work. Android ARM64 is the first
acceptance platform. ARM64 Linux can share ELF/POSIX/Vulkan foundations but still needs window,
input and distribution work. x86 Linux builds, extracts, translates and runs original GPU fixtures;
it does not emulate the game CPU. A Switch emulator is an optional independent reference,
not a runtime dependency or substitute for native-device acceptance.

## Build and verification

```sh
scripts/dev/build-android-native.sh --touch --tlsdesc
python scripts/dev/build-android-apk.py --tlsdesc
adb install --no-incremental -r .local/build/android-apk/d2r-native-tlsdesc-debug.apk
```

`--touch --tlsdesc` writes HOST_ABI=2 original rules into `.local/build/native-touch-tlsdesc-rewrite` and
builds `.local/build/android-romfs-overlay`. Without it, `--rewrite` keeps the CLI boundary's
separate native cache. The old initial-exec debug path remains with --touch without --tlsdesc (HOST_ABI=1). Never mix ABI artifacts. Deployment and
parameters are documented in `dm/android/README.md`.

- Original frozen assets now pass. The encoder is zlib-ng compatibility mode; the build
  wrapper loads its pinned library only in generation subprocesses. All twelve PNGs, two
  official tables and three C metadata constants pass unchanged checks.
- 488629 GPU fixture checks passed on Linux lavapipe and Mali. APK evidence uses packaged
  Khronos synchronization validation; Mali CLI runs do not have that layer. Added original
  screenshot queue/delay/crop/PNG output, independently decoded with Pillow to check pixels.
- Original first-draw coverage now selects Vulkan loadOp; at least 1800 actual-game attachment
  loads were elided with area/English-inventory/background-save regression. All 6931 SPIR-V
  binaries match the old cache; 20792 files were fully hashed on the phone before publication.
  See android-vulkan-load-cover-verification.json. Arm vendor counters are available; complete
  SurfaceFlinger windows before the Android overlay fix showed about 7.4–7.6 fps. Short sequential collection overhead
  comparisons were approximately 0.54% and 1.99%; neither proves zero overhead or controlled
  speedup from load elision across old/new processes.
- Android overlay redraw and pipeline lookup work follows measured CPU hotspots, not the Metal
  optimization checklist. The final resolved-state reuse version passed 130056 real-game full-key comparisons,
  area transitions/inventory/save and synchronization validation. See [device profiling](MALI_PROFILING.md)
  for separate performance controls and the Mali padding bug caught by the reuse proof.
- Native hardware fixtures complete 200000 concurrent atomic increments, signals, observation
  composition and SHA-1 vectors; original binding-identity fixture passes 63 checks.
  New TLSDESC fixtures cover ordinary dlopen, a preexisting thread, 64 concurrent threads, NZCV/SIMD preservation and incompatible-ABI rejection; the IE regression also passes.
  t_native_hud_bridge requires private mapping/binding files and one invocation failed for
  missing arguments; that invocation is not a pass.
- Original Metal LUT brightening now runs through Vulkan on the real 32³ RGBA16F game table,
  reusing unchanged dm_bright_k. GPU reference comparisons, fixed-exposure English camp and
  default dynamic exposure pass; game modules remain unchanged. The original optional
  world-freeze rendering branch has GPU/real-game evidence. Runtime extrapolation now has
  GPU kernel/pass-role fixtures for native D24S8/D32S8, including a two-pass stencil path for Mali.
  Real-game D24S8 recognition, encoded camp/outdoor frames, movement, area 1→2→1, inventory,
  same-PID 60→30→60 and background save/resume pass. More final-composite scenes, controlled
  performance acceptance and other graphics enhancements/diagnostics remain incomplete.
- 6931 offline Vulkan 1.1 SPIR-V modules validate. Assets, keys, derived data and saves stay ignored.

Private evidence lives in `.local/logs/` and app `files/runtime/`. Key records include
`android-original-binding-success*`, `android-original-ui-drag-reloaded.png`,
`android-original-shop-{sale,buy}*` and `android-vulkan-full-touch.log`.
HUD/map evidence is android-original-hud-* / android-original-map-full.png. No-preload evidence is android-tlsdesc-*; GPU validation is android-vulkan-tlsdesc.log. Full playability remains unaccepted. See [TLS ABI](ANDROID_TLS_ABI.md).

## Latest: background saves and presentation recovery

Original diag.c dm_save_wait commits twice in roughly 361 ms from camp and 323 ms from the paused menu. The original freeze path preserves world snapshots and HP. Moving a scroll, going HOME without Save and Quit, terminating the process and restarting preserves its new inventory cell.
Discarded background presents now still advance the original presentation count and report -1, preventing numbering drift after resume. game-state.jsonl present_sample_ns updates only for positive intervals derived from actual presentation timestamps; same-process resume shows fresh samples progressing.
Evidence: android-background-item-reloaded.png, android-original-background-save-paused.log, android-present-background-roundtrip.json and android-present-background-game.log.

Optional GPU region queries group render passes by their first fragment shader and target size, and dispatches by compute shader. Enable with --ez gpu_profile true; disabled by default. Durations may include dependencies and overlap, so they are not additive exclusive shader time. This diagnoses further performance work and does not establish a frame-rate improvement.

## Port scope and artifact provenance

Prioritize a stable Android platform layer and reproducible tooling. Game logic reuses existing upstream changes or necessary reinforcement; do not add Android-specific gameplay, balance, AI or quests. Combat/map milestones are compatibility acceptance, not new game modifications. Each change needs an upstream counterpart or a platform root cause.

rewrite-native.py invokes original patch_nso.py once from original NSO0 inputs and records source hashes/build IDs, pinned upstream revision, script/tool versions, explicit options and hashes of every image/side table. Cache hits verify artifact integrity. Negative checks reject modified images, missing gates, changed observers and rewritten-image inputs. deploy-android-modules.py accepts only verified complete bundles, reads every artifact hash back from the phone before switching directories, and retains the previous modules without modifying saves/RomFS.

The native rule domain no longer adds the Android aim alias; the full touch APK keeps original upstream touch-run-v4 aim rules. Regenerated main/sdk/subsdk0 hashes match the existing phone images. The audit compares cuts, observers, parameters, scan sites and gate semantics against the pinned upstream and checks current default Apple images byte-for-byte; platform branch relocations into appended veneers are checked separately.

The upstream audit passed for all modules against afad65c895386a96dfb863f2317e41d61222a5a9: main retains 48 cuts, 21 observers and 36 host gates; nine differences are branch relocations into platform veneers. sdk/subsdk0 add no cuts. All default Apple images are byte-identical. Evidence: android-upstream-rule-audit.json; android-current-bundle-upstream-link.json proves every module artifact in the latest bundle is identical to that audited bundle. On-device deployment/cold-start reports are android-provenance-deployment.json and android-provenance-cold-launch.json.
