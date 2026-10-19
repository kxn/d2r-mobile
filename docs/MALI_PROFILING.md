# Mali profiling on physical hardware

Target: PHP110 / MT6983 / Mali-G710 MC10 / Android API 36.
Profile actual native ARM64 execution; no CPU emulator or game-module modification.

## Existing tools first (2026-10-17)

AGENTS.md rule 12 prohibits reimplementing capabilities supplied by existing tools.
This turn's added region/barrier timers, hot controls and fixtures were withdrawn;
12 runtime/test files and the launcher were restored from exact backups. Evidence
is archived in `.local/archive/self-timing-attribution-20261017/`. The phone uses the
verified `6b2c7221…` APK with `--external-profile`, disabling custom CPU/GPU timers.

Official gatord captured 20 seconds. Streamline's own `with-metrics` template
exported queue utilization: arithmetic means of 2068 ten-millisecond bins were
57.24% fragment, 14.91% vertex and 6.01% compute. This supports investigating
serialization/dependencies, not attributing exclusive cost to any one barrier.
See `.local/profiles/streamline-return-scheduling.apc` and
`streamline-return-scheduling-report/timeline.csv`. CPU symbols were resolved by
Streamline after matching the actually loaded host/libc/Mali ELF hashes; see
`streamline-return-scheduling-symbols.apc/symbol-inputs.json` and
`streamline-return-cpu-report/`. The `dm_thread_enter` tail branch is a guest entry,
not evidence that its 32-byte host stub consumes the attributed execution time.

The device enumeration reports GPU frequency counters unavailable. The bundled
`enable-mali-timeline-events.html` requires r40p0+, with previews on some r36p0
devices. Working hardware counters and CPU stacks on this r32 phone do not prove
per-pass GPU scheduling events work. Verify official tool capabilities and report
entries first; this gap does not justify building another timing system.

## Tool selection

### RenderDoc on official replay: memory limit established (2026-10-17)

After a fresh original HOME save (`asked=1 commits=2`), the game was stopped and
RenderDoc 1.45 captured the official Arm/GFXReconstruct replay instead. Vulkan was
selected and capture progress was received, but Android explicitly killed PID
16313 for LOW_MEMORY at 11:04:09. Perfetto reports peak sampled RSS 11.42 GB and
swap 2.92 GB; `dumpsys activity exit-info` identifies LOW_MEMORY. No valid RDC or
per-event timings were obtained. Wi-Fi ADB remained responsive during this attempt;
this does not establish the cause of the earlier stalled ADB service.

The unmodified official `gfxrecon-optimize` removed unused initialization data:
661987985 to 378839504 bytes. Official replay `--screenshots 1` output matches all
2772x1240 RGB pixels of the reference, SHA256 `0e229cea5a24b434…`. A prior Android
system screenshot included status/navigation bars and a different viewport; its
pixel comparison was rejected. Provenance and equivalent-surface comparison:
`.local/build/mali-camp523-official-optimized.provenance.json` and
`.local/logs/mali-camp523-official-optimized-swapchain-comparison.json`.

Retrying this smaller capture with RenderDoc's official softMemoryLimit=200 MB
also ended in explicit LOW_MEMORY (PID 17855, 11:06:59, peak sampled RSS 11.67 GB).
The option spills some capture data to disk, not a hard cap on total application
memory. Both attempts are terminal failures, not measurements of pass cost.
Keep using working Streamline/LWI and Frame Advisor evidence; do not add custom
timers to replace unavailable event timings. Official Perfetto traces and SQL
results are `renderdoc-replay-system-{second,third}.pftrace` under `.local/profiles/`
and `renderdoc-replay-{second,third}-perfetto-analysis.txt` under `.local/logs/`.
These traces have vendor atrace parsing errors and one unsupported ftrace event;
process-memory counters, Android logs and independent exit-info support the
reported memory failure, not a GPU-driver-crash attribution.

Aggregation of the older official converter output counts 2844 images, 15664
buffers and 94 memory allocations (4.658 GB). Image requirements total 2.099 GB,
of which RGBA8 UNORM/SRGB account for 1.826 GB. This is captured-state structure,
not current-APK residency, texture bandwidth, or a proven cause of 7 FPS. Buffer
requirements include aliases and cannot be added to allocations as physical usage.
See `.local/logs/camp523-official-resource-memory-summary.json`. Original settings
were restored exactly and the ordinary game was relaunched at native resolution.

The workstation alternative was also exercised with the official replay binary,
lavapipe only and a 2772x1240 Xvfb display. `-m remap` cannot map the captured Mali
memory types; official `-m rebind` can render, but host pixels differ (mean absolute
RGB difference 1.10, maximum 212), so host pixel equality is not claimed. The
official RenderDoc user layer was registered with `renderdoccmd vulkanlayer
--register --user`. Capture still did not complete: the registered onscreen target
disconnected during initialization, logging device-address enablement errors;
a separate GDB run exited normally with the replayer's explicit
VK_ERROR_OUT_OF_DATE_KHR failure, not a demonstrated SIGSEGV. The official
offscreen/frame-boundary option rejects this driver because VK_EXT_frame_boundary
is unavailable. No valid host RDC or mobile remote timings were obtained.
Do not infer that the device-address errors alone explain the disconnect.
Evidence: `renderdoc-capture-host-official-replay*.txt`,
`gfxrecon-renderdoc-lavapipe-gdb.txt` and
`camp523-host-lavapipe-pixel-comparison.json` under `.local/logs/`.
All diagnostic GUIs/replayers exited. Ordinary Mali game PID18594 has original
camp/HUD, no injected layers, native2772x1240/fixed3D1/async+stream and another
fresh HOME save/resume with two commits. This is not a new FPS result.

### Correctness findings from tool validation (2026-10-17)

The old `ReadTexelsStrided` path overwrote CPU row/slice padding and imposed Vulkan
buffer-copy alignment on CPU destination pitches. A regression reproduced the
first corrupted padding byte at offset 12 (128 instead of sentinel 215). The fix
matches Metal's tight GPU readback followed by row copies, published by a real
completion callback while staging/context are alive. Tests cover mips, array views,
3D slices, odd pitches and untouched guards; no game/shader changes or FPS claim.

An old mirror-clamp test sampled exact texel boundaries; Adreno returned neighboring
texel (3,7) instead of expected (4,8). The test now samples mirrored texel centers
while retaining exact pixel checks. Khronos validation then exposed
`VUID-vkCmdDraw-mipmapMode-04770`: NVN non-mip filters selected LINEAR mip mode,
illegal for Adreno's unfilterable D32 format. Use nearest mip mode and LOD zero to
match Metal's NotMipmapped behavior, including nonzero NVN LOD clamp parameters.
This does not reduce the game's filtering quality.

Final APK `639d5e34…` matches all 244 source inputs and its packaged host ELF.
Full validation passes: lavapipe 508933, Mali 508938, Adreno 505995; neither device
has validation errors. Mali PID 13540 returned to the original English camp/HUD,
experience 54, HP 55/55, at native 2772×1240, fixed 3D=1.0, external-profile and
async+stream, without injected GPU layers. Fresh original HOME save confirmed
`asked=1 commits=2`, followed by same-process resume. No new speedup is claimed.
The 6 GB Snapdragon tablet's real-game launch was killed after menu processing:
exit-info explicitly says LOW_MEMORY with 4.5 GB PSS. Passing GPU fixtures does not
prove game readiness. Mali camp meminfo reports approximately 7.36 GiB PSS,
including 4.15 GiB Graphics; account for this in capture/memory investigations.

Wi-Fi RenderDoc injection reproduced stalled ADB shell commands; no valid RDC was
received, so no per-event timings are claimed. After the user rebooted, the phone
uses `10.10.10.44:41937`; original GPU settings and perf_harden=1 were restored.
Persistent exit/crash records do not establish the 10:30 failure's cause; boot reason
is userrequested. A stale task-owned RenderDoc probe GUI and its forwards were also
removed, without claiming causation. Keep using working official LWI/Streamline;
record service/memory events with Perfetto before any further reproduction attempt.

### Official LWI frame reports verified (2026-10-17)

The earlier “Cannot find any frames” error is resolved. Official
`streamline_me.py --lwi-mode counters --lwi-api vulkan --gpu-timeline no`
loads `VK_LAYER_ARM_LWI` on this r32 driver and supplies frame/API annotations
independently of r40 GPU scheduling events. Its headless argument forwarding
misparsed `run_game`; the established gatord wrapper instead attached to the
already injected real game. Neither the vendor tool nor game code was modified.

`capture-mali.py --preset advisor` only selects official counters needed by the
stock G710 report, including atomic/varying/primitive/fragment-task inputs. It
does not implement collection or timing. Capture:
`.local/profiles/streamline-official-lwi-advisor-full.apc`, 30 seconds, PID 11149,
APK `6b2c7221…`, native 2772×1240, fixed 3D=1.0, original camp. Reproduce with:

```sh
.local/tools/Arm_Performance_Studio_2026.5/streamline/Streamline-cli -pa -t json,html --clip-start=2000 --clip-end=28000 -d .local/profiles/streamline-lwi-advisor-full-clipped .local/profiles/streamline-official-lwi-advisor-full.apc
```

Official `performance_advisor-261017-102710.json/.html` has all series, with no
missing-series warnings. The unclipped JSON omitted summaries present in CSV;
using the official clip options to exclude capture edges restored those summaries.
Results: 7.2 FPS, 80.7% GPU utilization, 100% fragment-bound classification;
approximately 49.12M GPU cycles, 28.12M fragment cycles, 10.05M arithmetic,
7.95M load/store, 5.03M texture cycles, 667.93 MB external traffic and 2346.5 draws
per frame. Counters overlap and cannot be summed into exclusive time. External
traffic covers the system GPU; fragment-bound classification is not utilization.
Cross-pass pixels/overdraw do not attribute redundant pixels to individual draws.
This does not identify a particular barrier/shader's exclusive cost or meet 30 FPS.

An initial shorter capture changed from roughly 130 ms to 64 ms within one
process; that is not optimization evidence. Subsequent captures remained at
7.2–7.3 FPS. Control thermal/frequency state and observation overhead. After a
fresh original HOME save (`asked=1 commits=2`), original layer settings and
`security.perf_harden=1` were restored; normal PID 12265 has no LWI/Timeline layer.

| Official Arm tool | Purpose | Limits |
| --- | --- | --- |
| Streamline + gatord | CPU scheduling/stacks, GPU activity, fragment/non-fragment work, texture/external-memory traffic and frequencies over time | Primary diagnosis tool. Hardware counters still require sampling; stacks and more counters increase collection work. |
| Performance Advisor | HTML/JSON summaries from Streamline captures with frame boundaries | Needs official annotations/LWI for complete frame reports; hardware counters alone are insufficient. |
| Frame Advisor / RenderDoc for Arm GPUs | Inspect a frame's API calls, attachments, dependencies, draws and pixels | Capturing perturbs execution; use for correctness/structure, not normal performance measurements. |
| Mali Offline Compiler (`malioc`) | G710 static analysis of generated Vulkan shaders: cycles, registers, possible spilling | No target execution required. Estimates are not measured exclusive pass time. |
| libGPUCounters (formerly HWCPipe) | Standalone preflight of driver counter accessibility | The **unmodified Arm example** reads counters under shell and app permissions. Not a new in-game sampler or per-frame attribution tool. |

Sources: [Arm suite](https://developer.arm.com/tools-and-software/arm-performance-studio),
[Streamline workflow](https://learn.arm.com/learning-paths/mobile-graphics-and-gaming/ams/streamline/),
[Advisor workflow](https://learn.arm.com/learning-paths/mobile-graphics-and-gaming/ams/pa/),
[official libGPUCounters](https://github.com/ARM-software/libGPUCounters).

AGI/APA/Perfetto can supplement Android system/frame scheduling analysis. They do not replace
Arm hardware-counter analysis. Current `perfetto --query` advertises SurfaceFlinger FrameTimeline
and ftrace but no GPU counter/renderstage sources. This does **not** mean Mali counters are
inaccessible: the independent Arm driver probe successfully reads them.

## Pinned local installation

```sh
python3 scripts/dev/install-arm-performance-studio.py
```

Installs official Arm Performance Studio **2026.5 Linux x86-64** under
`.local/tools/Arm_Performance_Studio_2026.5/`, verifying the published SHA-256.
Record: `.local/tools/arm-performance-studio-install.json`.
No system PATH/driver change or use of workstation compute GPUs.

Counter preflight pins libGPUCounters **2.5.0 / 378c7b59e2dae87cbbffb0ac9cf96ed13c939823**.
The original example, built with NDK r28c, produced 10 nonzero samples each under shell
and `run-as com.d2rmobile.nativeport`. Evidence:
`.local/logs/android-mali-official-counter-probe.json` and shell/app logs.
These counters cover the system GPU, not solely the game; fragment cycle counts can also
sum across shader cores.

Streamline/gatord 9.9 completed a 15-second capture attached to the actual camp process,
followed by official CLI analysis and CSV export. The preset selects 10 G710 hardware counters,
without call stacks, the GPU timeline layer or screenshots. `captured.xml` reports actual
GPU/CPU sample rates of 101 Hz; exported CSV binning is not higher sampling precision.
`security.perf_harden` temporarily changes from 1 to 0 and is restored to 1, without root.
The initial failure to set this property under app permissions is retained, not counted as success.

Reproduce after entering the game in external-profile mode as described below:

```sh
python3 scripts/dev/capture-mali.py --seconds 15 --output .local/profiles/new-camp.apc
.local/tools/Arm_Performance_Studio_2026.5/streamline/Streamline-cli -analyze .local/profiles/new-camp.apc
.local/tools/Arm_Performance_Studio_2026.5/streamline/Streamline-cli -report -timeline -default-template basic -scale 100 -output .local/profiles/new-camp-report .local/profiles/new-camp.apc
```

The runner attaches without restarting the game, restores permission in finally, and records
configuration/daemon fingerprints. Evidence: `.local/profiles/mali-camp-official.apc`,
`mali-camp-official.capture.json` and `mali-camp-official-report/timeline.csv`.
A PID preflight warned that the process did not exist despite the same PID actually running;
perf_event_open succeeded. GPU counter acquisition is accepted; complete CPU stack/symbol
attribution is not. This capture has no LWI frame boundaries, so it is not a complete
Performance Advisor per-frame HTML report. Mali Offline Compiler also produced G710 JSON
analysis of an existing game-derived SPIR-V without modifying it.
RenderDoc for Arm GPUs 1.45 is installed/version checked; device frame capture/replay is pending.

## Separate observation overhead from the bottleneck

```sh
python3 scripts/dev/android-game-launch.py --external-profile --report .local/logs/mali-launch.json
```

This option (APK Intent `--ez external_profile true`) disables custom Vulkan CPU timing/stats
and submission/region GPU timestamp queries. `mtl_gpu_ms` returns unavailable rather than
inventing zero; guest NVN timestamps/occlusion queries, completion fences and writeback semantics
remain intact. Input/state logs remain, so this is not a claim of zero observation overhead.
It conflicts with validation, custom GPU profiling and per-draw tracing.
The launcher stops an existing process: first confirm a fresh original background save with
`commits=2` if a game is running.

Run separately:

1. Correctness: synchronization validation, GPU fixtures/pixel comparisons; no speedup claims.
2. Counters: disable custom timers/validation, select needed Streamline counters, no screenshots
   or per-call trace initially.
3. Timeline/stacks: add relevant CPU stacks or official GPU timeline only for identified intervals,
   recording the extra overhead.
4. Single frame: Frame Advisor/RenderDoc for specific resource/draw/dependency questions.
5. Acceptance: compare complete present interval distributions with collection on/off, at the
   same scene/scale/settings and similar temperature. Assess collection overhead separately
   before comparing optimizations. Sparse printing and vendor tooling do not imply zero overhead.

Select work from current Android profiles after preserving correct output; the Metal
inventory is reference material, not a list that must be completed first.
Prioritize GPU idle/queue overlap, fragment/non-fragment work, external reads/writes, tile
writeback/restores, texture/L2 behavior and frequency. Use this to choose pass-traffic,
barrier, submission-pipelining or shader-translation work. Follow the
[Metal-to-Vulkan assessment](NVN_SPECIAL_OPTIMIZATIONS.md); add no game shader whitelist
or scene-specific exceptions.

## Local short overhead comparison

```sh
python3 scripts/dev/measure-mali-overhead.py --report .local/logs/new-overhead.json
```

Sequential off/on/off 12-second windows retain the same PID, area/position/HP/3D scale.
Read SurfaceFlinger present history only at each boundary, not via per-frame ADB polling.
Reject overflow of the 128-record ring and zero/pending INT64_MAX timestamps; compute
successive actual-present differences. Column semantics follow
[AOSP FrameTracker](https://android.googlesource.com/platform/frameworks/native/+/51de481/services/surfaceflinger/FrameTracker.cpp).

First comparison (PID 11411, camp, scale 0.60):

| Collection | New presented frames | Mean fps | Mean interval |
| --- | --- | --- | --- |
| Off before | 86 | 7.136 | 140.140 ms |
| On | 85 | 7.080 | 141.244 ms |
| Off after | 85 | 7.100 | 140.843 ms |

Collection-on mean interval was **0.54%** above the mean of the two off windows.
Evidence: `.local/logs/android-mali-collection-overhead.json`, including actual frame
timestamps, thermal reports and state observations. This is one sequential short comparison,
not proof of zero overhead across scenes/temperatures. Lighting/frequency may drift; it also
does not establish a rendering optimization's speedup.

A later V2-cache run measured off/on/off 7.581 / 7.356 / 7.425 fps, approximately 1.99%
higher mean interval with collection. See `.local/logs/android-load-cover-official-present.json`.
The difference between rounds is why the first 0.54% estimate is not a fixed overhead guarantee.

## Profile-driven Android overlay change

CPU sampling found HWUI RenderThread at 6.93% inclusive CPU; the Android touch/HUD view
unconditionally invalidated itself at display cadence, including idle scenes. It now polls
the original input/state at the same cadence, but redraws only when painted state changes.
The binding snapshot age (not painted) is excluded; touch changes, icons, language, HUD
values and drag ghost remain tracked. Draw code/assets, game modules and shaders are unchanged.

Two same-process ABBA rounds, PID 17754, camp (5225,6289), HP 55, scale 0.60:

| Implementation | Measurement intervals | Equivalent mean fps | Mean interval | HWUI redraws |
| --- | --- | --- | --- | --- |
| Previous continuous redraw | 4 | 8.10 | 123.46 ms | 1732 |
| Redraw on painted changes | 4 | 11.23 | 89.02 ms | 0 |

Mean interval fell 27.90% (equivalent throughput +38.7%). These are short sequential
windows with complete actual-present records, not a claim for all scenes/temperatures.
Each window rejects history overflow, process changes or position/HP/scale changes.
Evidence: `.local/logs/android-overlay-abba.json` and its eight raw SF/HWUI reports.
English camp/inventory were visually inspected; original inventory open/close, movement
out/back, and background save (`commits=2`)/same-process resume passed. This is a display/input
regression check, not deterministic two-render pixel equivalence for the moving world.

These windows are measurement intervals, not screen windows. Both variants retain the
same two display layers: the Vulkan game `SurfaceView` and a transparent Android HWUI
touch/HUD layer. Game rendering is not converted to GLES. The measured gain comes from
avoiding unchanged HUD redraws; it is not a measurement of removing the second layer.
Its static buffer still needs composition with the game, potentially by display hardware
or by the GPU, depending on the actual SurfaceFlinger/HWC decision. That independent cost
has not been measured, nor has moving the HUD into Vulkan been shown to improve performance.
See [AOSP SurfaceView layering](https://source.android.com/docs/core/graphics/arch-sv-glsv).

Reproduce (true selects the previous implementation; the default is restored on exit):

```sh
python3 scripts/dev/measure-android-render-toggle.py --control overlay_continuous --report .local/logs/new-overlay-abba.json
```

Official Arm counters were reacquired after removing idle overlay redraws:
`.local/profiles/android-overlay-dirty-triage.apc` and its timeline report. System counters
cannot attribute every unit of traffic to the game. CPU sampling uses Android's official
`/system/bin/simpleperf record --in-app -p PID --duration 15 --call-graph fp -e cpu-clock:u -f 99`;
exact ELF SHA checks precede host symbolization. `.local/profiles/cpu-overlay-dirty.data`
recorded 1466 samples, zero lost; FP callchains can truncate across guest assembly.
Self instruction addresses remain usable. About 80% of `vk_draw` self samples map to the
1184-byte serial byte-hash loop (source/ELF disassembly retained), motivating the next
word-hash experiment. This is CPU instruction attribution, not exclusive GPU timing.

The same CPU profile also identified work done by disabled diagnostic tracing. Trace
frame/directory options are now snapshotted once per submission. With tracing off,
texture binding no longer queries these environment variables or formats `sampled-*`
labels, and draws no longer parse the frame number individually. The directory is owned
so environment changes cannot invalidate an in-progress readback; the next submission
refreshes the options. Draws, shaders, resources and game modules are unchanged.
Both lavapipe and the phone's Mali APK GPU fixture passed 489161 checks, including frame
filtering, enable/disable between submissions, directory lifetime and every pixel of
actual GPU PPM readback, with no synchronization-validation errors. Evidence:
`.local/logs/vulkan-trace-latch-host.log`, `.local/logs/android-trace-latch-vulkan-test.log`
and the APK hash in `.local/logs/android-trace-latch-verification.json`. These fixture
results do not yet establish real-game correctness or a net performance improvement.

The same APK entered the English camp under RenderDoc injection, with fresh PID 7961
`--attach` observations and `.local/logs/android-renderdoc-before-selection.png` retained.
This establishes camp output/input readiness only. No acceptable Vulkan `.rdc` was obtained:
ADB stalled during injected execution, including after intercepting automatic `adb root`,
so root is not an established cause. All host capture controllers have exited. After USB
debugging was restarted, device debug settings were restored and ordinary execution without
injection passed; see below. Multiple changed variables prevent attributing recovery to one.

That historical APK had a separate sharpness limitation: Android `front_game_res` fixed the
handheld profile at 1280×720, whereas the original iOS frontend can use native screen pixels.
`.local/logs/android-gpu-region-game.log` records 960×540 at scale 0.75 and 768×432 at
scale 0.60, upscaled by the game to 1280×720 and then linearly scaled by Vulkan to the phone.
Only 3D uses this reduction; original UI retains the game's output resolution, and the
Android touch/HUD uses a separate layer. This baseline is not native-resolution performance
and does not rule out other quality defects. Native output and fixed-scale checks follow.

## Native pixels and submission-wait audit

Android display policy now corresponds to original iOS: native Surface pixels by default,
original handheld sizing and cold-start overrides. The new PHP110 APK passed actual game
and Surface output of 2772×1240 at fixed 3D 1.0, camp/inventory/movement/original background
saving, and 769423 full pipeline-key checks without synchronization-validation errors.
Evidence: `.local/logs/android-native-resolution-validation-summary.json` and
`android-native-resolution-*.png`.

Four actual-present windows without RenderDoc/validation/custom timing measured 9.065 fps,
110.312 ms at native output, with zero HWUI redraws. Same APK/docked mode/fixed 3D 1.0/camp
at 1280×720 measured 11.735 fps, 85.215 ms. These are short sequential windows in different
cold starts with thermal/scene variation, not a controlled code-optimization speedup.
Neither compares directly against old handheld 0.60 scaling. Native output has about 3.73
times as many pixels as this 720p control.
Four windows after returning to native measured 8.953 fps, 111.697 ms, about 1.26% higher
interval than the first native run. Full native→720p→native evidence is retained in
`.local/logs/android-native-resolution-comparison.json`. Final device state is native
output at fixed 3D 1.0.

Android Perfetto plus official trace_processor captures kernel scheduling without app
injection or CPU stack sampling. In 7.989 seconds at native resolution, translation runs
2.460 seconds and sleeps 5.364 seconds; all 142 complete sleeps are followed by GPU-completion
thread wakeups. The approximately 7.943-second 720p capture has 3.014 seconds running and
4.800 seconds sleeping, with 189 sleeps followed by completion-thread wakeups. These are not
exclusive GPU durations. The driver supplied no render-stage tracks and vendor systrace
metadata generates warnings. Queries use actual Linux TIDs verified before/after capture
to avoid FPSGO-induced inferred-process conflicts. Source confirms begin's global wait
and present's CPU copy-completion wait; original Metal retains allocators/arenas by completed
fence rather than draining globally on each begin. See the [lifetime audit](VULKAN_SUBMISSION_LIFETIME.md)
for ownership, mirror dependencies and implementation order. Waits have not been removed;
there is no asynchronous-submission speedup result yet.

USB and previously authorized Wi-Fi ADB work simultaneously without new pairing. Restored
debug settings and normal execution are recorded in `android-renderdoc-cleanup.json` and
`android-native-resolution-normal-ready.json`. Host USB resets include deliberate reconnects
and physical unplugging; older device adbd logs are insufficient to establish the cause.
Cable, root and simultaneous Wi-Fi are not established causes. Ordinary runs and vendor
captures currently retain connectivity. A SurfaceFlinger snapshot places both game and HUD
layers in DEVICE hardware composition; this does not establish zero composition cost or
the same decision in every scene. Evidence: `android-native-resolution-surfaceflinger.txt`.

```sh
python scripts/dev/capture-android-scheduling.py --serial DEVICE --seconds 8 --output .local/profiles/new-scheduling.pftrace
python scripts/dev/measure-android-render-toggle.py --control none --scale 1 --rounds 1 --cpu-stats --report .local/logs/new-presents.json
```

## Pipeline lookup experiments

The pipeline cache already has a hash-table index. Its old lookup still rebuilt and hashed
the entire 1184-byte key for every draw. A 64-bit-word hash retains full-key equality and
shares compiled pipelines between the two A/B indexes. On PID 20131, with idle HUD redraw
removed, two ABBA rounds measured 87.98→84.59 ms (11.37→11.82 fps), a 3.84% interval reduction.
Evidence: `.local/logs/android-word-hash-abba.json`; measured APK SHA256
`5b82d0bfd11643bf9a5e02d1d7510f7d843ec87d7f83b81deb5db16b74d02699`.
Short sequential windows do not establish a universal speedup or predict combined gains.

A further change uses a static-state generation plus effective attachment/primitive/stencil
context to reuse the resolved pipeline, skipping key construction and hashing entirely on
an unchanged context. Dynamic resource addresses, texture bindings, viewports, scissors and
depth bias values do not invalidate static pipeline state. Ghost replay explicitly invalidates
state before/after its snapshot switch. Reference mode always prepares and bypasses generation
comparisons, including invalidation overhead in the net baseline. Modes latch per submission,
invalidating prepared state on a switch. Check mode rebuilds the full key and rejects a mismatch.
These are separate controls from the hash implementation.

The first Mali check rejected reuse because byte 724 was rasterization-struct tail padding
copied from the stack. Keys now copy explicit Vulkan payload fields into zeroed storage,
excluding ABI padding. Preserve the failing log as evidence; a host-only pass was insufficient.
Linux lavapipe and Mali passed 488629 GPU checks. The final real-game validation process performed
130056 full-key reuse checks without mismatch through camp→area 2→camp, inventory open/close
and an original background save (`commits=2`); no Vulkan/synchronization validation errors.
Evidence: `.local/logs/android-pipeline-net-verification.json`,
`android-pipeline-net-validation-game.log` and the earlier validation screenshots/logs.
An earlier revision separately passed 429413 real-game comparisons.

The final net-cost ABBA comparison includes direct-copy reference state updates:

| Pipeline preparation | Mean interval | Equivalent fps | Translation CPU core-seconds per second |
| --- | --- | --- | --- |
| Always rebuild/lookup | 84.81 ms | 11.79 | 0.4244 |
| Reuse unchanged context | 83.24 ms | 12.01 | 0.4187 |

Interval reduction was 1.84%; translation-thread CPU rate fell 1.34%. This is a modest local
result, not the principal fix for low FPS. 632086 draws reused preparation versus 128739
lookups (83.08% reused) in the four enabled windows. No window created a pipeline; idle HWUI
redraws remained zero. Total process CPU rate increased 1.90% while throughput increased
1.87%: do not claim a reduction in total process CPU from the translation-thread result.
Evidence: `.local/logs/android-pipeline-net-cpu-abba.json`, including exact APK SHA, TID,
raw kernel CPU ticks, complete presents, state and thermal records. CPU ticks are read only at
boundaries; the busiest cumulative `nvn-translate` TID is selected because driver helpers may
inherit its name. Older preparation-only comparisons left tracking enabled in the reference;
use this final net comparison, not those partial-cost runs.

```sh
python3 scripts/dev/measure-android-render-toggle.py --control pipeline_prepare_reference --cpu-stats --report .local/logs/new-pipeline-net.json
```


## 2026-10-16: require tool attribution before further optimization

Per user steering, undeployed query-batching work was removed from main source and retained
as `.local/build/query-batch-pending/candidate.patch`; host tests alone establish no performance
benefit. Original ASTC conversion ran only on local quality samples, with no deployed assets.

A fresh 15-second official Streamline 9.9 capture of actual camp PID 4860 includes hardware
counters and CPU stacks: `.local/profiles/android-r32-vendor-attribution.apc`. Official CLI
call trees still contain unknown functions despite attaching the actual APK ELF; symbol
attribution remains unaccepted. Raw data and derived analyses are separate. Frame Advisor
2026.5 local MCP tool discovery works, but actual frame capture remains unaccepted.
Basic Perfetto GPU render stages require r47p0 in official libGPULayers documentation; layer
metadata association needs r51p0, with some OEM r49p1 backports. Even traditional 2022.4 Mali
Timeline required a compatible r40p0 driver. Older tools do not automatically create missing
r32p1 driver telemetry. API capture, hardware counters and static shader analysis are separate.

The user supplied Xiaomi Pad 6 / pipa / 23043RP34C / SM8250 / Adreno 650 / Android 14 API 34.
USB a1971f20 stalled on writes; authorized Wi-Fi ADB 10.10.10.54:43179 works. SDK ADB client
and server are both 37.0.1 with NATIVE USB. The MTP dialog is not an ADB version diagnosis.
Driver GLES V@0502.0 dates to 2021-10-04; Vulkan is 1.1.128 without shaderInt64, timeline
semaphores or stencil export. Same-APK GPU tests fail a sampler-replacement pixel assertion;
this is retained, not accepted as passing. Original deployment verifies 28 module artifacts,
20792 shader files and all 46 RomFS input hashes (about 9 GB). Actual game/performance
acceptance on this device remains pending.

Official AGI 3.3.3 is installed under `.local/tools/agi/`, provenance in
`.local/logs/agi-install.json`. Restrict its server/replays to the tablet with no host fallback.
Device discovery and GraphicsSpy injection work, but GPU profiling validation explicitly
fails: required profiling support is unavailable and the KGSL sysfs switch is inaccessible.
This does not establish working hardware counters/timelines. API frame capture is separate.

HyperOS blocked AGI's background package service and installed-app visibility. A temporary
10-minute deviceidle allowlist unblocks the service. Changing observed rejected helper-only
MIUIOP(10022) from ignore to allow restores package discovery; restore ignore after capture.
Interrupted capture left four GraphicsSpy global selections behind. They were checked and
restored to their prior unset values; verify cleanup after every capture. The first offscreen
fixture capture combined validation layers and failed CreateInstance with extension-not-present;
its zero-byte trace is not a capture. Continue with separate, unvalidated actual-game capture.


## 2026-10-17: vendor tools, Wi-Fi devices and startup compatibility

The MTK/Mali phone uses Wi-Fi ADB `10.10.10.44:40065`, without USB. Streamline has a real
camp capture with GPU hardware counters and CPU stacks; function-symbol attribution remains
unaccepted. Module shares and correlations do not establish a specific drawing bottleneck.
The user requested no further AGI attempts; prioritize the vendor tools.

Official Qualcomm Snapdragon Profiler **2026.9** is installed from the user's download and
connects to the Adreno 650 tablet over Wi-Fi. Archive SHA-256:
`1a0c20a9ec220dc47a6ebaaab17db892f99e1946252e335db492b9db4338e55c`.
`.local/logs/snapdragon-profiler-install.json` records all installed components. Mono 6.12,
GTK# 3.24.24.38 and isolated LLVM 21 libc++ dependencies work. Actual GPU counter reads by
the official plugin fail with `IOCTL_KGSL_PERFCOUNTER_READ errno 1 Operation not permitted`;
see `.local/logs/tablet-sdp-counter-permission.txt`. Connection and metric discovery are not
successful counter capture. Vulkan Trace/Snapshot need separate acceptance. An old website
FAQ's root requirement does not replace device tests; newer release notes and the packaged
manual support debuggable, unrooted Vulkan applications.

Arm Frame Advisor 2026.5 initially failed on the Mali phone because Android package visibility
hid its installed layer provider: `Debug layer app 'com.arm.frameadvisor.replay' not installed`.
The debug shell now declares exact `<queries>` packages for Frame Advisor, RenderDoc and
Qualcomm's ARM64 layer, without querying all applications. APK SHA-256:
`cda2e59c0f7390360f440e08201678b3106eb73177c1a815b455525f81ed9102`.
Mali's complete GPU regression passes 492843 checks, including VMA's 4104 small buffers and
real GPU copies; `.local/logs/mali-vma-full-vulkan-test.log` records the results. Frame Advisor
now connects to the real game and steps Vulkan frames. Completed capture/replay and camp
attribution remain pending. Restore tool layer selections after capture before baseline runs.

Tablet startup has a separate reproducible compatibility failure: original compute shader
`098a126f92bf533a` also triggers `No UGPR spill support` in a minimal Vulkan process. A reduced
case compiles with seven ballots but fails with eight. A DontInline ballot helper preserves
predicates and invocation locations and compiles the actual shader. A GPU test of divergent
and repeated ballots verifies all 8192 output words at subgroup size 64. Explicit build options
`DMSH_VK_SUBGROUP_SIZE=64 DMSH_VK_BALLOT_HELPER=1` regenerate all 6931 shaders from original
inputs, validate each module and verify 20792 deployed file hashes; chain `d0941f1dfe7c`.
Game module bytes and gameplay logic are unchanged.

The tablet passes the compute-pipeline failure, but Android terminates both subsequent main
menu launches as `LOW_MEMORY`, with approximately 4.4 GB process PSS on its approximately
6 GB system. Camp rendering and tablet FPS remain unaccepted. Fixing the VMA allocation-limit
and unused CullDistance VUIDs does not establish complete correctness or performance.


Subsequent acceptance: menu Frame 100 completes capture, device replay and official MCP
analysis with `replayWarnings=[]`. The capture is
`/home/dl/.local/share/Frame Advisor/logs/com.d2rmobile.nativeport_frame-100_20261016T162043Z_capture-1792167643155-1.fac`;
`.local/logs/mali-menu-frame100-analysis.json` records Vulkan, 57281 API records, 1066 draws
and 102 workloads. This menu frame does not establish the camp performance bottleneck.

Host and libc function symbols now resolve in Streamline. Matching captured-APK ELF files
must reside in `capture.apc/images/`; session `<image path="..."/>` entries use basenames
relative to that directory. Placing ELF files in the capture root or merely declaring absolute
paths did not work. The official GUI's Add image / Analyze produced the accepted layout,
which CLI re-analysis reproduces. The derivative capture is
`.local/profiles/android-r32-vendor-absolute-symbols.apc`; raw data stays unchanged. Function
and per-thread call-tree reports are in `.local/logs/mali-streamline-complete-symbols/`, with
hashes/limits in `.local/logs/mali-streamline-symbol-resolution.json`. Private driver symbols
and anonymous original-game code mapping remain incomplete. Guest roots such as dm_thread_enter
and run0 must not be treated as hot loops from their displayed self percentages.

Frame Advisor's Device data timeout limits stepping/data operations. Startup and low-FPS
injection can exceed its default 20 seconds. Official Preferences > Advanced options allows
increasing it; the current value is 600 seconds. MOC and ADB paths explicitly select verified
local tools. Neither injected capture speed nor gfxrecon replay summaries count as game FPS.

Camp capture over Wi-Fi: the live GUI captures Frame 523 after entering the real English camp,
with native 2772×1240 output, fixed 3D scale 1.0 and `external_profile=true`. A fresh original
background save reports two commits immediately before capture. The official GUI saves
`.local/profiles/mali-camp-frame523-partial.fac` (ZIP integrity verified), and official MCP
analysis is preserved in `.local/logs/mali-camp-frame523-analysis.json`. The GUI hierarchy
labels the frame 522 and counts 2356 draws / 114 workloads; MCP counts 2353 modeled draws.
Keep these tool counters distinct. The compact render graph has 64 render-pass nodes,
6 transfers and 3 compute nodes; it is not a list of all 114 hierarchy workloads.

This is a **partial** capture: all three device replay attempts crash at `vkCmdBeginQuery`,
with the first two native frames in the Mali vendor driver. Consequently output images,
overdraw and replay mesh results are unaccepted. Original API, shader and graph evidence
remain available, but compiler estimates and draw counts do not establish GPU timings.
The crash stacks, exit records and host log are under `.local/logs/mali-camp523-*`.
An isolated same-device Vulkan test of precise occlusion queries inside and outside a render
pass succeeds; the crash cannot yet be assigned to the backend's query calls. Inspection of
the immutable GFXR trace finds 49 synthetic query begin/end pairs in the tool's loading-state
command buffer before the captured frame; the minimal test does not reproduce the full replay.
No runtime query logic or game module has been changed to bypass the tool failure.

Replay blocker reproduced and isolated: a fresh command buffer that has never recorded a
reset/render-pass operation crashes in precise occlusion begin at driver PC `0x16c2d54`.
Reusing the reset command buffer succeeds; creating a fresh buffer and recording a full
query-pool reset before begin also succeeds. A fresh buffer with an empty render pass
succeeds. Logs `.local/logs/mali-query-replay-{fresh-outside-crash,fresh-inside,fresh-reset}.txt`
record the distinction. This is an independent Vulkan driver reproduction, not a game patch.

`scripts/dev/prepare-arm-query-replay.py` implements a narrow external-tool workaround:
move the existing full occlusion reset from the snapshot's reset-only command buffer into
its fresh activation command buffer. It rejects unknown state sequences and retains the
original trace. File length, block count and the complete suffix from the first restored
query onward are unchanged, so all real-frame commands and dump indices remain identical.
Input/output/script hashes are written to the output's `.provenance.json`. On the verified
camp capture the source hash is `ae06b439d77cfc6d6c6ce0f4a6e52df3d23e1cee68b5e91ed2529bd4b87358f2`
and output hash is `3f0d5b9887dee981dba13849e5618daeb519f26ee276154346506cb457dce5f7`.
The official, unchanged Arm replay APK completes this derivative trace with `--validate`
and exports the 2772×1240 camp image to `.local/logs/mali-camp523-replay.png`. The original
FAC remains explicitly partial. Do not present the derivative as an untouched capture.

A subsequent replay exports color/depth attachments after the last draw of each selected
command buffer and the original selected compute/transfer resources (about 882 MiB).
This samples diagnostic outputs, not rendering work: all captured frame commands still
execute. `.local/logs/mali-camp523-pass-end-dump-complete.txt` records completion; resource
files are stored under `.local/profiles/mali-camp523-stage-outputs/`. No-input launch avoids
an unrelated input-dispatch ANR caused by sending a key during a long synchronous replay.
Never interpret the tool's replay FPS, validation overhead or resource-dump throughput as
native game performance. Per-draw images, overdraw and full mesh replay remain separate work.

Resource-dump equivalence: the final color attachment exported after draw 113285 matches
the separate, non-dumping replay screenshot in every RGB channel (2772×1240, zero changed
pixels). `.local/logs/mali-camp523-replay-pixel-equivalence.json` records both RGB hashes.
All 200 exported files / 923543711 bytes have local SHA-256 records in
`.local/logs/mali-camp523-stage-outputs-manifest.json`. This verifies these two replay paths,
not bit-exact equality against an unavailable original full-resolution frame readback.

A new normal-game Wi-Fi capture, `.local/profiles/mali-wifi-native-camp-r32.apc`, attaches
to camp PID 21334 for 15 seconds at native 2772×1240 / scale 1.0. Frame Advisor layer settings
are unset, custom timers/validation/extrapolation/freeze are disabled, and perf_harden is
restored. Raw capture remains untouched; derived analysis in
`.local/profiles/mali-wifi-native-camp-r32-symbols.apc` uses the exact installed host ELF
`af1633c4aef26d6acac95ef817b8fb80fc4c4d79b8f4f4b5f159078315a7fd42` plus matching libc/driver.
Official CSVs are in `.local/logs/mali-wifi-camp-{functions,counters,metrics}/`. Hardware/CPU
sampling is 101 Hz. The GPU frequency counter was requested but is absent from captured.xml;
do not infer wall-time GPU utilization from raw cycle totals. Counters remain system-wide,
with no complete r32 GPU timeline or per-pass timing attribution.

The game's frame-900 window records present p50 115.65 ms, p95 132.18 ms (roughly 8–9 FPS
at its median, not an optimization gain). New function samples resolve memcpy, texture/
buffer lookup and binding work; dm_thread_enter/run0 percentages still contain unresolved
guest-frame attribution and must not be treated as entry-loop hotspots. The raw any-queue/
GPU-queued cycle ratio is approximately 0.6731, which is not wall-time utilization. Use these
reports alongside the frame graph to select the next measured experiment, not as proof of
a particular shader or memory-bandwidth bottleneck. The CLI sometimes crashes in analytics
shutdown after producing complete reports; its crash log is preserved separately under
`.local/logs/mali-wifi-streamline-jvm-shutdown-crash.log`.

## 2026-10-17: retain pass-spanning occlusion queries after controlled measurement

Vendor frame capture shows 2353 draw queries; Streamline resolves allocation/driver work
under `vk_counter_draw_prepare`. Original Metal uses one visibility slot per render pass,
splitting at samples reports/resets. Vulkan now follows that scope: begin at the first draw,
end before a samples report/reset or secondary render-pass command-buffer end, and keep
separate immutable report-prefix snapshots. Timestamp reports retain their original command
positions. Query-pool ownership and CPU publication still wait for actual GPU completion.
This removes per-draw query begin/end/allocation and most 256-slot exhaustion pass breaks;
it does not skip draws, change shaders/resolution, or approximate occlusion results.

On the same real camp process, APK `1dcbcf23c59d292c235818b2c6fdb6e4b0a606d70c0b590d124fe40c1afb707d`,
shader chain `3a0ffe903acf`, fixed 2772×1240 / 3D 1.0, external-profile mode and zero HWUI
redraws, two ABBA rounds show mean present intervals **118.2146 → 106.1723 ms**: 10.19%
reduction / 11.34% throughput gain (about 8.46 → 9.42 FPS, not 30 FPS). Query begins per
present fall from approximately 2430–2473 to 48–51. Translation CPU per second stays near
0.345 core-seconds/second as throughput rises; do not claim a similar percentage reduction
in total process CPU. Full present histories, state/mode checks, thermal records and kernel
CPU tick boundaries: `.local/logs/mali-query-span-abba.json`. Reference/candidate camp
screenshots and state are `.local/logs/mali-query-span-{reference,candidate}-camp.png` and
`mali-query-span-candidate-state.json`.

Correctness: 300 draws produce 76800 passed samples, with report prefixes 256 / 65536;
span mode uses three queries versus 300 in reference mode. Additional reports across a
reset inside one pass retain 256 / 256 / 512, and two submissions accumulate 512. Lavapipe
sync-validation passes 492848 checks in both reference and span modes; async span validation
passes 493129. Mali native fixtures pass 492848; APK synchronization validation and original
English camp validation also pass without observed VUIDs. Logs use `query-span` names.
One initial APK poll read a stale previous test log and was explicitly rejected; accepted
APK checks delete the old test log first and require the fresh span marker and passing JSON.

The accepted implementation is now the default. `--ez query_per_draw_reference true`
(or launcher `--query-per-draw-reference`, host `DM_VULKAN_QUERY_SPAN=0`) retains the old
implementation for same-process comparisons. Mode changes apply at submission boundaries.
The measurement runner restores span mode on exit. GPU-specific shader experiments were
not deployed: official MOC on captured shader 159 estimates more spilling/load-store work
for `-O`, `-Os`, SSA/local/scalar variants. Those static estimates use r56p1 while this
phone has r32p1, so they are rejection/triage evidence, not measured driver timings.

The default-span APK `0a88189a26b07ee7c286bee21f2b9882a4091efbcbfa638b587ef4d076933e9c`
passes fresh APK sync validation (492848 checks, span marker present) without any query-mode
Intent extra, and cold-starts the original camp in default span mode. It is retained locally
as `.local/build/android-apk/before-graphics-barrier.apk` for rollback. A subsequent barrier
experiment is separately gated and has no accepted performance result yet.

Barrier investigation starts from the original Arm capture: 164 pipeline barriers in the
frame, 160 with HOST destination stage / HOST_READ access; 159 use ALL_COMMANDS|HOST on
both sides (`.local/logs/mali-camp523-barrier-audit.json`). The initial experiment removes
only the HOST destination from pass-start barriers, retaining every GPU stage/order, the
HOST_WRITE source and all original full barriers at explicit NVN/transfer/submit/readback
boundaries. Original CPU-visible effects still wait for the full submit fence. This is a
controlled hypothesis about redundant host-domain operations, not a confirmed cause of
GPU stalls. Vulkan [domain operations](https://docs.vulkan.org/spec/latest/chapters/synchronization.html)
distinguish GPU consumers from host visibility; [readback examples](https://docs.vulkan.org/guide/latest/synchronization_examples.html)
retain host visibility before CPU consumption. Host async sync-validation passes 493129;
Mali APK sync-validation passes 492848 and confirms 893 GPU-only pass barriers. Real-game
validation and performance comparison remain required before keeping this experiment.

The host-destination-only barrier experiment was rejected after measurement and its tuning
code removed. Actual-game sync validation passed, but same-process ABBA at native output
showed only 0.886% mean interval reduction (`mali-render-barrier-abba.json`), too small to
accept amid observed drift. Candidate source/APK hashes are archived under
`.local/build/render-host-barrier-rejected/`. The accepted query-span optimization remains.
This rules out host destination visibility alone as a meaningful bottleneck; broader GPU
stage dependencies still require separate evidence and correctness checks.

Dependency audit, 2026-10-17: distinguish ordinary VS outputs (rasterization/FS,
no CPU readback) from explicit buffer copies, uniform updates and compute/storage
writes. The detailed producer/consumer inventory is in the Chinese companion.
Pass-start GPU stage narrowing now defaults on only with a conservative proof:
any pending GPU buffer transfer, vertex image consumer, writable vertex storage,
or explicit NVN primitive/indirect ordering falls back to the full barrier. A later
vertex image/storage consumer restores full visibility even within an existing pass.
Explicit transfers/compute/submit/readback/fence semantics retain full dependencies.
Host async sync validation passed 493131 and Mali APK passed 492850; actual camp
validation showed no observed VUIDs. Same-process ABBA throughput improved 4.30%
(`mali-graphics-stage-warm-abba.json`) and 2.02% in an independent confirmation
(`mali-graphics-stage-confirm-abba.json`). Both were native 2772×1240/fixed3D1 with
identical shaders, no pipeline compilation and zero HWUI redraws. Both had Android
thermal status 3; absolute FPS drifted substantially across rounds, so do not compare
cross-round FPS as an optimization result. The first incomplete comparison with
pipeline compilation is explicitly rejected. `--graphics-stage-reference` restores
full-stage pass-start barriers for controlled comparisons.

The audit also adds optional nonblocking CPU reads for coverage proofs: pending GPU
data retains LOAD instead of submitting/waiting for an optional load-elision proof.
Regression fixtures retain byte-exact output for GPU-updated vertices and assert that
this candidate creates no internal submissions. This mode remains gated pending the
actual-game comparison. New counters distinguish buffer readback ranges/bytes,
internal-submission bytes, required CPU waits, optional proof misses and upload waits.
Query liveness is counted only; no query or drawing is skipped by that instrumentation.

A direct Vulkan capability probe found DMA-BUF buffer import support but no export
support on this Mali driver; OPAQUE_FD buffers support neither. The existing allocation
therefore cannot simply be exported and remapped over guest memory. This does not
exclude a separately validated Android HardwareBuffer/platform-allocation import route.
No game heap remapping or game binary modification was introduced.

Actual camp dependency counts (`mali-dependency-audit-counter-delta.json`): 71 guest
submits over a 33.308-second sync-validation window, each averaging 2451988 readback
bytes in 10 ranges; internal-submit readback bytes, optional coverage waits/misses and
upload conflict waits were all zero. These are event counts, not performance timing.
Thus GPU-updated coverage geometry is not supported as the current camp bottleneck.

A query-liveness candidate preserved all draws and sample reports while reducing
queries from about 47 to 1 per submit. Host async validation passed 493953, Mali APK
passed 493674, and actual camp validation showed no VUIDs. However, same-process ABBA
showed a 0.67% frame-interval regression, so the runtime optimization and its controls
were removed. Source/APK are archived under `.local/build/query-liveness-rejected/`;
liveness counts and useful counter/pixel boundary tests remain. Official vendor
reference/candidate captures are retained. Fewer queries are not an accepted FPS gain.

The original GFXR buffer-copy audit distinguishes pool→pool, pool→temporary and
reverse directions; pool→temporary can include alignment copies, and the reverse
can include storage copyback, so those categories alone do not prove CPU readbacks
or CPU-known inline data. The current memory candidate instead prefers HOST_CACHED
allocations only for actual CPU writeback snapshots, invalidates each allocation
only after its real GPU fence, then publishes guest/shadow effects. Uploads retain
coherent memory. Mali selected non-coherent flags 0xb; APK sync validation passed
493674 with 473 actual invalidations. Host validation includes four reused 2MiB GPU
readbacks compared byte-for-byte (493961 async checks). Actual camp validation passed;
performance remains pending. See Vulkan memory/synchronization and VMA readback/cache
maintenance guidance. No completion boundary or CPU-visible effects were removed.

Cached readbacks are accepted and now default: same-process native-resolution ABBA
throughput improved 5.63%, with an independent 2.82% confirmation (mean intervals
-5.33% / -2.75%). The confirmation tracks the actual GPU completion TID: CPU time
fell from 0.05360 to 0.02777 core-s/s, about 48% per second. This is CPU completion
cost, not GPU utilization. Raw presents, temperatures, APK hashes and confirmation
measurement-script hash are retained; absolute FPS drifted thermally across rounds.
Mali native async validation passed 493961 checks with real non-coherent HOST_CACHED
flags 0xb and 477 invalidations, including four repeated 2MiB GPU copies compared
byte-for-byte. Invalidation/publication still follows the real fence and precedes
guest completion; cached readbacks cannot be reused as coherent uploads. The
allocation reference is `--coherent-readback-reference`. Game/shaders/quality and
all drawing remain unchanged.

### Compute producers and storage alignment dependencies (2026-10-17)

Ordinary VS outputs feed rasterization/interpolation, not CPU readbacks. An API
review of the original camp523 GFXR finds **11 dispatches**; the reduced graph's
three compute nodes are not a dispatch total. Pipeline/module/SPIR-V hashes map
these to original LUT combine, fluid update/cull/accumulate, light depth bounds/
binning/grid, terrain query and a counter kernel, plus one host LUT module.
See `.local/logs/mali-camp523-compute-audit.json`. Source names identify work, not
its eventual CPU/vertex consumers; precise resource bindings still matter.

Five reverse storage transfers have earlier matching pool→temporary copies and
unaligned offsets; one range is 1310720 bytes. The experimental generic offline
pass aligns descriptors down and rebases proven runtime uint32 array addresses,
including ArrayLength and index-overflow handling. Unsupported pointer flows,
non-word addresses or excessive descriptor ranges retain GPU copies. Logical
write ranges, post-shader barriers, real-fence CPU publication and all drawing
remain intact. No game ARM64 or gameplay changes are involved.

`DMSH_VK_STORAGE_BIAS=1` produces checked V3 metadata/push-constant ABI; runtime
`--storage-offset-bias` is opt-in and switches only at submission boundaries.
V1/V2 remain compatible. Chain 9dad8209e657 validates all 6931 modules/builtins,
transforming 5937 proven modules. Reference chain 7d4d057a8aa5 disables the pass
from the same source. All 4559 FS, 22 CS and four builtins match installed 3a
byte-for-byte; VS/HS/DS contain the previously completed unused-CullDistance
removal, so this is not an entirely byte-identical 3a rebuild. Publication follows
on-device verification of all 20792 artifacts and retains the previous cache.

Host V2, V3 reference and V3 direct paths each passed 502173 checks. Mali native
async passed 502173; APK synchronization validation passed 502175, zero VUIDs.
Cases cover runtime lengths, 0..3 trailing bytes, whole-pool guards, compute UBO
pointers, compute→VS dependencies, distinct VS/FS prefixes and pixel results after
GPU updates. Fixture copies fell 843→372 with 780 direct bindings; fixture counts
are not game FPS. Actual native-resolution camp validation and visual inspection
passed; game-throughput comparisons are in progress.

A resolution diagnostic keeps APK/3a shaders/world/fixed scale1 unchanged:
2772×1240 measured 10.2883 FPS before, 1610×720 measured 12.2458, restored native
measured 10.2148. Each uses four actual-present windows, zero HWUI draws, thermal
status3, retained temperatures and boundary CPU ticks. About one-third the pixels
improved throughput only about 20%; pixel load alone cannot explain most frame
time, nor does this prove which processor is saturated. A 1386×620 attempt clipped
the selection UI and never reached the world; it is rejected, with no game FPS.
Resolution was restored, and reduced quality is not counted as an optimization.

The inherited HLE CommandBufferWaitSync/WaitEvent no-ops depend on the upstream
single queue, earlier submitted/initialized signals and existing dependencies.
Additional queues or narrower relevant barriers require renewed proof or real
wait implementations; this is not a claim of general no-op NVN wait semantics.

Storage alignment's within-V3 toggles improve throughput 6.68%, confirmed 5.92%,
but both sides include address-rebasing instructions. A cold V2 run measured
10.2067 FPS and a later V3 run 7.4049 during a performance/thermal step; the first
V3 attempt was rejected for pipeline compilation. This cannot establish net
benefit or rule out shader overhead. Keep the shader transform experimental and
opt-in (`mali-storage-bias-performance-status.json`).

A separate dependency-driven upload candidate follows the vendor CPU stacks:
73/330 parented translation-thread samples occur in pool sync under internal
present begin, with another 28 under commit. Dirtying a small part of a large
pool can scan protection metadata across the entire pool. The candidate uploads
only pages registered by actual GPU buffer accesses at submission. Images retain
their independent CPU source/dirty tracking; an image-only present need not scan
buffer pools. Per-page generations prevent hidden deferred edits, coexist with
full-pool generations and preserve pending-access/writeback waits.

`--lazy-pool-sync` / `DM_VULKAN_POOL_SYNC_REFERENCE=0` is accepted/default; full
sync remains selectable. pool_uploads records considered full/used and protected range bytes, not
actual copied bytes. Host V2/V3/reference paths passed 502511; Mali native async
502511 and APK sync validation 502513, zero VUIDs. New cases retain dirty edits in
untouched pages, prove an empty image-only submit does no pool work, and exercise
full/partial transitions. Original ghost proof and divisor fallback now read CPU
bytes rather than relying on earlier mirror upload; their behavior is unchanged.
Actual-game validation, performance measurements and vendor confirmation follow.

Dependency-driven uploads are accepted/default: same-process ABBA throughput
+10.69%, independently +13.36% (intervals -9.66% / -11.79%). Translation CPU/frame
fell 36.49→28.31ms and 37.39→28.08ms using kernel ticks at window boundaries.
These are CPU costs, not GPU time. Native fixed-scale1 currently reaches about
11.2 FPS, still below 30. `--full-pool-sync-reference` retains the old policy.

Official follow-up captures mali-pool-sync-{full,used}.apc retain identical PID,
APK, V2 chain, scene and 174 warmed pipelines in provenance snapshots. Matching
ELF/driver/libc symbols are attached only to derived copies. Translation samples
fall 522→317, pool-sync parented samples 128→0, dirty-protect samples 86→1, matching
the removed scans. Sampling counts are not GPU utilization or game FPS. The full
report JVM crashed in analytics after exporting valid CSV, not in the game. An
initial one-second toggle check was rejected for stale mode, then candidate
capture ran only once the actual mode was false.

The fresh default APK passed 502513 synchronization checks, zero VUIDs, explicitly
logging pool reference0 and storage reference1; shader rebasing stays experimental.
APK packaging now declares appCategory game, enabling the previously rejected
GameManager query. The device offers standard/custom only; no unsupported mode
was forced. Config opts out of backbuffer downscaling/FPS overrides to retain NVN
output/pacing. This is a packaging/diagnostic fix, not an established FPS gain;
see official Android Game Mode and intervention opt-out documentation.

### Actual compute-buffer consumers and Android hints (2026-10-17)

The read-only `scripts/dev/audit-arm-buffer-dependencies.py` decodes descriptor
updates/bindings, dispatch/draw calls and buffer copies, checking the captured
dispatch→pipeline mapping and input hashes. Original camp523 has 11 dispatches
and six reflected writable storage-buffer bindings, in addition to image writes
and host modules whose write reflection is unavailable. It does not edit captures.
`mali-camp523-buffer-dependencies-verified.json` records these declared overlaps:

| Producer | Subsequent captured buffer dependencies |
| --- | --- |
| fluid update, 16 KiB | temp→pool→temp, then fluid cull/accumulate compute bindings |
| fluid cull, 64 KiB | accumulate compute binding and end-of-frame copy |
| light depth bounds, 5 KiB | temp→pool→temp, then light bin compute binding |
| light bin, 5 KiB / 1280 KiB | pool/read-only alignment copies, then fragment set1 bindings at 675 draws |
| terrain query, 256 B | 80 B subrange copied onward, plus end-of-frame 256 B copy |

These are declared bound ranges, not proof of executed byte accesses. Propagation
conservatively retains overwritten origins; image alias/subresources and accessed
vertex/index ranges are unresolved. Guest CPU loads are absent. None of this
proves the light buffers require CPU reads or permits dropping completion-time
visibility. Ordinary VS outputs are not counted as buffer writes.

The optional public NDK Performance Hint path fills a missing Android counterpart
to Metal thread deadlines. It uses the original XJ_FRAME budget, monotonic work
start/wall duration and measured thread CPU time. Available AWorkDuration V2 gets
GPU duration 0 (unmeasured), never fabricated blocking-as-GPU time. Dynamic lookup
preserves API29 compatibility; affinity, clocks and thermal controls are untouched.
Actual dumpsys/state confirms an active session, 16666665 ns target, repeated
reports and zero failures. Real-game sync validation/English native visual checks
passed; GPU/host regression counts remain 502513 / 502511.

Do not enable by default: APK728e5f… / PID31908 / V2 chain7d4d057a8aa5 / native
fixed scale1, eight ABBA windows measured 84.80569→84.68141 ms, only +0.147%
throughput with zero HWUI frames. An initial run was explicitly rejected for
pipeline creation; the accepted round followed another warmup. Keep the explicit
`--performance-hints` experiment off by default. See
`mali-performance-hint-warm-abba.json` and `mali-performance-hint-result.json`.

The accepted default's native actual-present baseline is 11.483 FPS. A separate
async process measured 11.959 FPS; cross-process drift does not establish a default
policy gain. Kernel Perfetto events do expose this phone's GPU frequency: one
10-second trace observed 214–383 MHz, translation mostly on CPU4–7, running 1.529s
and sleeping 8.343s. Raw `mali-lazy-async-scheduler.pftrace` and derived CSV retain
the evidence. Low frequency alone proves neither thermal caps nor GPU idleness.
Use Current HAL temperatures, not the distinct stale Cached CPU temperatures.
Saving/pausing/cooling the same process gave an initial 12.55 FPS window followed
by about 6.55 FPS; it is neither a code gain nor a proven explanation. Device
frequency and thermal policy were not overridden.

### Submission timing, native sync arguments and image layouts (2026-10-17)

Normal minimal submit-boundary timestamps (no per-draw GPU queries) show four
stable 300-frame windows: GPU span p50 51.15–51.17 ms, frame p50 83.31–84.18 ms,
translation CPU about 24–25 ms/frame. Preserve the native log and derived
`mali-minimal-gpu-timing-summary.json`. TOP/BOTTOM spans include intra-submit
stalls; these are not shader-only active times or proof all waiting is readback.
Native LLDB entry probes, removed before normal detach, observed two
QueueFenceSync calls with condition0 (ALL_GPU_COMMANDS_COMPLETE), flags1, and
ten SignalEvent calls with location2 (BOTTOM). Register evidence is in
`mali-nvn-sync-argument-audit.json`. This sample does not justify vertex-complete
or TOP early release, nor does it characterize every possible scene. Performance
measurements use a separate fresh process after debugger use.

The single-module SSA/dead-code derived GFXR retains every other block byte for
byte. Official validation replay produced zero differing RGB pixels at
2772×1240. Official infinite single-frame ABBA measured reference 89.55735 ms,
candidate 90.01755 ms, about -0.51% throughput: no benefit, no deployed cache
change. A finite-loop final-iteration handle error was rejected. Infinite mode
first passed 20 validation iterations, then sampling ended by stopping its own
replay process. Replay timings are not game FPS. Loop/branch hints left MOC
metrics unchanged and were not deployed; r56 MOC is not the phone's r32 compiler.

The optional `--optimal-image-layouts` experiment follows
[Arm's image-layout guidance](https://documentation-service.arm.com/static/67a62b17091bfc3e0a947695)
and [Khronos' layout-transition sample](https://docs.vulkan.org/samples/latest/samples/performance/layout_transitions/README.html).
Only single-mip/layer, non-storage color images use persistent attachment / fragment
read-only layouts. Depth, arrays, multiple mips, storage, non-fragment consumers,
attachment feedback and raw-handle exports retain GENERAL. Actual old layouts,
existing barriers, bytes, precision and drawing are preserved; no game-binary edits.

New official FRAG_TRANS_ELIM counters confirm the hardware effect: original
baseline 14861406 tiles / 448 eliminated. Matched APKba43a2… / PID11472 / 185
warmed pipelines measured GENERAL 15052546 / 0 versus candidate 16154088 /
4913893, about 30.42%. These are system GPU counts; different windows' absolute
totals are not per-frame traffic or FPS. Raw APCs, mode/thermal brackets and CSVs
remain as `mali-layout-{general,optimal}-pair*`.

Do not accept this as a default speedup: the extra-global-barrier version increased
same-process ABBA intervals 6.97%; image-only color→fragment dependencies still
increased them 3.28%. Enabling a hardware path does not establish net benefit.
The latter passed host 502513 / Mali APK 502515 checks, actual game validation,
hot transitions and English native visual checks with zero VUIDs. See
`mali-image-layout-fragment-abba.json`. Keep the experiment off by default.

### Streaming Vulkan batches inside one NVN submission (2026-10-17)

Experimental `--stream-submits --async-submit` cuts at real render-target changes after
at least 256 draws, with at most four cuts per logical NVN submit. Forced attachment
stores preserve subsequent LOADs. Internal queue serials advance; the guest fence
advances only once at the final submit. Actual ALL/BOTTOM sync semantics are not
released early. Both options remain off by default pending broader acceptance.

A real-GPU six-pass/576-draw fixture checks GPU uniform updates, prefix counters,
reset/cumulative counters, events, guest fence advancement and every output pixel,
including switching back to reference. Host/Mali pass 504076/504078 checks, zero VUID.
Original game validation and native English camp image also pass.

Same APK387601… / PID18084 / shader V2 7d4d057a8aa5 / native2772×1240 fixed3D1:
the initial eight-window ABBA underwent a large speed step (reference11.79→~5fps),
so its aggregate +48.13% is confounded. A subsequent same-process confirmation has
reference5.060/5.177fps versus candidate7.251/6.955fps: 195.397→140.845ms mean interval,
+38.73% throughput. Candidate uses four cuts per guest submit, unchanged ~2344draws
and ~2.338MiB total readbacks per frame, zero HWUI redraws. CPU translation slightly
increases: overlap is the intended mechanism. These are not evidence of sustained
15/30fps. Reports: `mali-stream-submit-{abba,confirm-abba}.json`.

The speed step is not attributed: slow-state Perfetto GPU frequency349–383MHz does
not establish a lower clock than the earlier214–383MHz trace. Temperature correlation
is not causal proof. An independent diagnostic built against the unmodified bundled
Arm HWCPipe device library reads80 samples, but this driver explicitly lacks GPU/core
group/shader cycle metadata (all three feature flags false). Absent values are not
zero clock measurements. Raw output: `mali-clock-probe-hot.jsonl`.

Optional Android ATrace annotations distinguish NVN planning/replay, attachment index,
queue submit, CPU publication and render-area dimensions. Submit-to-observed-fence
includes queue/driver/GPU/wakeup delay, not pure GPU time. Markers are dormant unless
tracing is enabled and add no per-draw queries. Capture via the system tracing script's
`--app-markers` option.

### Vendor GPU ceilings and read-only boundary snapshots (2026-10-17)

APP-marker captures expose GED `gpu_freq_max`:848MHz in one faster state versus
383MHz in slower states. [Official OnePlus MT6983 source](https://github.com/OnePlusOSS/android_kernel_5.10_oneplus_mt6983/blob/oneplus/mt6983_t_13.0_oneplus_ace_2v/drivers/gpu/mediatek/ged/src/ged_dvfs.c)
emits the current effective ceiling, with hardcoded synthetic PID5566; it must not
be attributed to an unrelated live thread with that number. This is a platform-family
reference, not the exact installed firmware source. Comparison evidence is retained
in `mali-vendor-gpu-ceiling-trace-comparison.json`.

Independent GPL helper `scripts/dev/probes/mali-ged-query.c` is never linked into
the game. `query-mali-dvfs.py --deploy` builds/deploys it under the package UID on the
audited MT6983 platform. QUERY_INFO and QUERY_GPU_DVFS_INFO use no boost, frequency,
thermal, GIFT or target-PID setters (the latter's controls are all zero). A normal,
untraced read reports system GPU load96%, current366MHz, ceiling383MHz, hardware
max848MHz. These are global counters, not exclusive game/pass utilization. The DVFS
record reflects the latest GPU-done update and may be stale while presentation is
paused. Untargeted target_fps/gpu_time=-1 are not game settings or timings.

The measurement runner's `--mali-ged-probe` verifies source/helper hashes, reads only
at boundaries, and rejects the entire comparison if the effective ceiling changes
at any observed boundary. Raw rejected observations remain recorded. This adds no
GPU layer, per-frame queries or ATrace, and does not eliminate instantaneous DVFS/CPU
or thermal drift. The active limiter identity is still unknown. Current HAL skin
49.855°C/status3 coexists with the cap; reported cooling state0 does not rule out
vendor policies. The available atrace thermal category emitted no thermal/cdev events
in a10-second capture; no absence-of-throttling claim follows.

Android16 exports new GraphicsPipeline/Surface/AutoTiming APIs, but a separate
read-only capability probe reports sessions=true and graphics_pipeline, surface_binding,
auto_cpu,auto_gpu=false. Unsupported modes were not implemented just because symbols
exist. Existing optional CPU hints remain off; GPU wait time is not fabricated as GPU
work time. APP-only capture now avoids the global gfx category. The new scheduler
analyzer verifies trace hashes and exports native wall/queue spans, time-weighted
GPU counter residency and parser-health evidence. MTK plain-text print records produce
systrace parser failures; preserve/check them rather than confusing them with lost
native markers or silently suppressing them.

### Accepted descriptor snapshots for the attachment dependency index (2026-10-17)

Cache descriptor-id→texture-bucket only within one index build; clear between builds
and retain epoch invalidation/reference fallback. Skip an unknown-stage scan only
when all160 handles are zero, retaining exact conservative dependencies otherwise.
New checks rebind the same id across builds and toggle back to reference. Host/Mali
pass504100/504102 checks, original game validation/image checks pass.

Same APK3daf2f… / PID26601 / V2 / native2772×1240 fixed3D1, unchanged async+stream:
boundary checks establish383000kHz effective ceiling throughout both accepted runs.
Eight-window ABBA:144.325→140.862ms,+2.46% throughput; independent four-window
confirmation:145.593→139.815ms,+4.13%. First-run translation CPU32.295→29.284ms/frame.
Reports:`mali-store-zero-skip-ceiling-{abba,confirm-abba}.json`. A prior unmonitored
run had an abrupt last-window change and is not used for acceptance. APP-only traces
separately show index4.096→1.120ms with383MHz ceilings in both; these trace counts are
not FPS measurements. Enabled by default, with `--uncached-store-descriptors-reference`
retained. Other experimental/default policies are unchanged.

### Matched Arm SDK counter windows and controlled cooldown (2026-10-17)

After a fresh original HOME lifecycle save, the existing PID26601 was suspended
for about five minutes and resumed without changing clocks, thermal policy or game
state. HAL skin fell from49.9°C/status3 to43.434°C/status0. The GED ceiling can be
stale while paused; resumed presents confirmed848MHz. Four actual-present windows
were15.91,7.99,14.71,15.72FPS,13.33FPS over343 intervals overall. This is a change
in operating conditions, **not a code speedup or30FPS acceptance**. Instantaneous
frequency still varies under a constant ceiling. Raw reports:
`mali-ceiling-controlled-cooldown.json`, `mali-ceiling-cooled-presents.json`.

The standalone `capture-mali-frame-counters.py` now builds against the **unmodified**
hwcpipe2 device library and G710 counter names shipped with Arm Performance Studio
2026.5. No SDK code is linked into the game. It collects40 primary-counter samples
at100ms spacing, retaining individual shader-core/cache-slice values and observed
monotonic/raw clock bounds. This is system GPU scope, without per-draw queries,
GPU layers or CPU-stack collection.

```sh
python scripts/dev/capture-mali-frame-counters.py --serial 10.10.10.44:40065 --deploy --output .local/profiles/new-sdk-window
python scripts/dev/analyze-mali-frame-counters.py --input .local/profiles/new-sdk-window --report .local/logs/new-sdk-window-analysis.json
```

Capture records helper/SDK source inputs, installed APK and shader hashes, stable
PID/world/pipeline counts and boundary ceilings. Present-history polling occurs
only after hardware sampling. Analysis checks contiguous samples, errors, hashes,
clock alignment and enclosing present history; an extra1ms boundary allowance
produces present-count bounds. Hardware cycles are not GPU nanoseconds, and
summed per-core/slice active counts are not exclusive elapsed durations.

Matched windows:63presents/4.147s at848MHz ceiling (count bound63–64), and29/4.176s
at383MHz (bound29–29). Slow-window nominal per-present FMA/LS short reads changed
+0.61%/+1.34%, external read/write beats +1.30%/+1.10%, rather than doubling with
lower FPS. GPU implementation, V2 shaders and options match between these APKs.
See `mali-sdk-{cooled,hot}-canonical-analysis.json`. Other GPU clients and boundary
phase remain; these measurements do not identify a particular thermal limiter.

The public helper was rebuilt/deployed successfully on PID5921:40samples,10cores,
4L2 slices, measured AXI128bit/warp16,30actual presents in4.127s (bound30–30).
`mali-sdk-canonical-live-reference-bytes.json` uses the recorded bus width and the
[Arm G710 counter reference](https://documentation-service.arm.com/static/667183a7e98d6a2ff18647c6)
for byte conversions. External accesses can hit system cache; shader-core traffic
categories overlap bus totals. Byte counts alone do not establish saturation.

### Withdrawn immutable descriptor-set experiment (2026-10-17)

A strict raw-capture audit found2540 exact duplicate descriptor tables among4717
allocations within their pool/reset lifetimes. The opt-in implementation reused
immutable sets only after resolving all resources/dependencies, and its tests
covered multiple in-flight owners and recycling one owner while another remained
pending. Host/Mali checks passed504136/504138 with synchronization validation.

Actual-game eight-window ABBA at848MHz observed ceiling reduced allocations/updates
by about54%, but throughput changed−0.82% and translation CPU time increased.
Restricting reuse to at most4bindings (all observed hits) still changed−0.31% in
an independent eight-window run with observed383MHz ceiling. The intervening
comparison whose ceiling changed was rejected entirely. Cache construction costs
did not produce a net gain: **all ten experiment source/integration files were
restored from the exact pre-experiment backup**, and the accepted36ed95c… APK was
restored. This is separate from the accepted attachment-index descriptor cache.
Raw reports: `mali-descriptor-set-ceiling-abba.json`,
`mali-descriptor-small-hot-abba.json`; exact rejected sources and inputs are
archived under `.local/build/descriptor-set-intern-rejected-final/`.

The APK build entry now removes its previous success output before building and
writes an APK/library/source-input hash manifest on success. A failed earlier
experiment build left a stale old APK; that result was explicitly rejected, and
new experiment validation required the installed hash plus expected check count.
Never deploy a generated APK without its matching completed build manifest.

### Immutable-c1 folding: replay-only evidence, not deployed (2026-10-17)

A diagnostic derivative replaces96 static loads of immutable shader c1 with
bit-identical constants from current metadata. Runtime c1 is already copied once
and zero-filled at creation. No precision/control-flow changes, game binaries or
production shader-cache edits. Exactly one shader-creation block differs; all
114586 other capture blocks remain byte-identical.

Official Arm replay synchronization validation passed, with all RGB bytes equal
at2772×1240. MOC r56 estimated spill traffic606→462bytes and LS106→95.8cycles for
that shader; the phone uses r32, so these are not on-device measurements. Four
replay windows averaged118.55→109.27ms (+8.49%), but intervals varied88–171ms and
only end-boundary ceilings matched. **This does not establish a game speedup.**

Matched SDK reference/candidate windows contained46/36actual replay presents.
LS short reads changed+0.89% per present,−0.31% normalized to raster quads, without
the substantial traffic reduction predicted offline. System GPU scope, producer/
display phase, separate replay processes and instantaneous clock changes remain.
The transform was not added to the production shader cache; the original game
was restored to36ed95c…/V2/native1/async+stream. See
`mali-camp523-c1-fold-{replay-provenance,pixel-comparison,loop-abba}.json` and
`mali-c1-replay-hardware-comparison.json`. Ordinary VS outputs remain separate
from explicit storage-buffer writes and CPU readbacks.

### CPU-known publication: correct, performance unaccepted (2026-10-17)

The generic `--cpu-known-copies` / `DM_VULKAN_CPU_COPY_REFERENCE=0` candidate remains
default-off. Original GPU copies/draws/events and actual fences remain. Pool-copy
sources must overlap no current, flushed or in-flight GPU/CPU writeback; this proof
never waits. Snapshots use exact shadow bytes after actual pool uploads and before
vkQueueSubmit; inline uniform updates use their recorded payload. Unknown, partial
or adjacent overwrites invalidate merged proofs. Guest/shadow publication still
follows real GPU completion and precedes event/fence visibility.

Byte guards, source writes before/after copy, destination overwrite/adjacency,
known-over-unknown coverage, explicit flush, late CPU edits and a timeline-gated
pending GPU producer pass host507392/Mali507394 synchronization-validated checks.
Real camp/touch movement/return near the original position and original HOME saving
pass; validation PID14111 has no VUID/FATAL. This is not full gameplay acceptance.

Same APKc083a247…/PID15270/V2 chain7d4d057a8aa5/native2772×1240/fixed3D1.0/async+stream:
eight windows at observed383MHz ceilings yield+1.52% throughput; independent four
windows at383MHz yield−2.17%. Per NVN submit, readback falls2.338398→1.338242MiB
(42.77%), with1.000156MiB CPU-known publication. First-run translation CPU rises
29.406→29.761ms/present, completion falls5.665→5.534ms. Fewer readback bytes do not
establish FPS gains, so the candidate stays disabled. Two earlier comparisons were
rejected because the measurement tool omitted the new state field; only the fixed
observed-mode comparisons count. See `cpu-known-copy-result.json` for raw reports,
input fingerprints, validation and metrics.


## 2026-10-17: robustness diagnostic rejected; CPU binding hotspot

An official Arm replay diagnostic changed only `vkCreateDevice.robustBufferAccess`;
114586 other blocks were byte-identical. One native 2772x1240 RGB frame matched
exactly, with no VUIDs. Initial replay timing suggested +23.32%, which is not game
FPS. Independent ABBA using unmodified Arm SDK primary counters, after at least
80 replay iterations, found LS short reads/quad -0.012%, short writes +2.272%, and
external read beats -1.495%. Presentation timing was inconsistent. No substantial
memory-work reduction was established; production robustness stays enabled.
See `.local/logs/mali-robust-sdk-comparison.json`. The initial loop report now
explicitly records this rejection.

Official gatord then collected 15.361 seconds of user-space CPU call stacks from
real game PID19413, APK c083a247..., V2 chain7d4d057a8aa5. Pipeline count was 167
at both capture boundaries; perf_harden was restored. Of 1488 samples, translation
TID19513 had 550; `memcmp` beneath `vk_shader_bindings` had 58 self samples.
Symbols match the exact loaded host ELF, libc and Mali driver hashes; evidence:
`.local/profiles/mali-post-dependencies-cpu-symbols.apc/symbol-inputs.json` and
`.local/logs/mali-post-dependencies-cpu-functions/`. `dm_thread_enter` tail-branches
into guest code, so its apparent self samples do not prove that the 32-byte host
stub consumes that CPU time. Anonymous guest code/unwind attribution remains a
separate issue.

Source inspection found c0/c2 deduplication comparing GPU-mapped memory. A new
candidate, disabled by default, uses CPU-owned immutable keys and last-content
reuse. Normalized byte comparisons handle direct global-state restoration; cached
pointers belong only to their submission. GPU writes, synchronization, draws and
game logic are preserved. Any performance claim requires subsequent phone ABBA.


Official gatord confirmation of the first constant-key candidate used the same
PID22568/APK29d95fb8... and 173 pipelines at all capture boundaries. Camp,
coordinates, HP and native resolution were unchanged. `memcmp` under graphics
bindings fell from 32 to 2 self samples; binding-inclusive samples were 108/886
versus 74/872. This is low-rate CPU sampling evidence, not an FPS measurement.
Full records and exact loaded ELF hashes are in
`.local/logs/driver-constants-official-cpu-comparison.json` and the two
`mali-driver-constant-{reference,candidate}-cpu-symbols.apc` directories.
The first timing ABBA was rejected because the effective GPU ceiling changed.
A second candidate retains the same cache algorithm, but allocates CPU keys only
in candidate mode so the reference does not pay their allocation/copy overhead.
Host 508164 and Mali 508166 correctness checks passed. Candidate V1 also passed
validated real camp movement/return and fresh original lifecycle save.


V2 controlled timing is complete on APK6b2c7221.../PID24638/V2 chain7d4d057a8aa5,
native2772x1240/fixed3D1/async+stream, with 383MHz effective ceiling at all observed
boundaries. Eight windows: translation CPU 30.020 -> 27.908ms/present (-7.04%),
throughput -1.32%. Independent four windows: 30.334 -> 27.971ms (-7.79%), throughput
+0.08%. CPU cost reduction reproduced; no FPS benefit established. Default stays
off. See `driver-constants-v2-{measured,confirm}-abba-analysis.json` and
`.local/logs/driver-constants-result.json`. This is a CPU binding improvement,
not a remedy for the remaining GPU-dominated frame interval.


## 2026-10-17: retain reductions in work without requiring immediate FPS uplift

Per the user's correction, FPS uplift is not a prerequisite for retaining a sound optimization. Record the saved work, additional costs, visual/stability validation, and whole-frame effects separately. Correctness defects or excessive added costs remain valid blockers.

The runtime now defaults to CPU-owned driver-constant keys/last-value reuse, immutable CPU-known copy publication, and omission of occlusion queries whose counts are proven reset before observation. GPU copies, completion-fence publication, real drawing and cross-submission sample accumulation are preserved. Reference controls remain available as `--mapped-driver-constants-reference`, `--gpu-readback-copies-reference`, and `--dead-sample-queries-reference` in the Android launcher.

Existing evidence supports approximately 7–8% lower translation CPU cost for constant reuse and 42.8% less readback volume for CPU-known copies. Occlusion-query omission was previously withdrawn solely after a roughly 0.67% frame-interval change despite reducing about 47 scopes to 1; that decision has been corrected. The restored implementation reuses the existing reverse RESET/REPORT proof, with no additional submission scan. Both query modes are tested for nested/repeated calls, multiple handles, count prefixes and actual shaded output.

A diagnostic isolated fragment AST candidate produces identical RGB and reduces official Streamline GPU cycles/frame by about 3.34% and L2 load/store reads by 46.15%; this useful result is retained even though replay FPS does not improve consistently. Generic all-fragment AST and blanket RelaxedPrecision still visibly corrupt player/material shading and remain preserved experiments pending fixes. No per-game-key production optimization branch was introduced.

Verified final artifact/source manifest: `.local/build/retained-query-candidate/`. Host and Mali/Adreno GPU regressions passed; detailed decisions and evidence: `.local/logs/optimization-retention-decision.json`. These results do not establish 30 FPS or full Vulkan parity.

## 2026-10-17: full native Vulkan RDC and TBDR attribution correction

The official RenderDoc application API, explicitly bound to the Vulkan instance,
captured the real game at 2772×1240 and fixed 3D scale 1.0: 2404 draws and 11
dispatches. `.local/profiles/renderdoc-native-app-api/native.rdc` is 580964547
bytes, SHA256 `b971f360ec6b3d8b10bd58a258b9f43ebfade1d91edcf8c17f68e7e511921af8`.
It completed on-device before file transfer; no remote controller was connected
during capture, and ADB remained responsive. This does not establish the cause of
earlier ADB stalls. Global injection settings were restored before requesting capture.

Deferred execution and ambiguous per-draw timing attribution are **not a
Mali-specific limitation**. TBDR batches work; recording, tiling and actual tile
shading need not execute at each draw's apparent command boundary. Attribution
also depends on query stages, driver and tool instrumentation. A later expensive
interval may include earlier accumulated work. The 19.57-second sum of overlapping
generic EventGPUDuration results is invalid, and the 24.84-ms interval cannot be
assigned to one draw or automatically treated as exclusive pass cost. See
[Arm's Vulkan draw timing explanation](https://community.arm.com/forums/f/mobile-graphics-and-gaming-forum/54832/vulkan-app-drawcall-timing).

Prioritize actual Streamline load/store/external traffic, instruction and
fragment/quad counts, scheduling/stall counters, and RDC dependencies and redundant
operations. Counter categories can overlap; high traffic alone does not prove
bandwidth saturation, and offline MOC predictions are not device measurements.
Use timings only across understood execution boundaries with comparable controls.
The RenderDoc Arm counter RPC timed out with no valid rows; enumeration did not
establish successful hardware sampling. Existing Streamline captures remain valid.

The depth resource 2693 feeds dispatch 19257, then dispatches 19264/19271 and buffer
144 consumed by subsequent fullscreen and main-color draws. This real dependency
rules out naive depth/color pass fusion. Main color is one R11G11B10_FLOAT target,
not a multi-attachment G-buffer.

The existing RDC's structured command data also proves a redundant full-image
RGBA8 crop copy followed by a full swap-image clear and full-coverage blit.
The generic window backend now blits full sources directly and skips clears only
for full destination coverage. Partial crops retain independent filtering bounds;
letterbox margins still clear. Source GENERAL layout and the visibility barrier
remain correct. Per eligible native frame this removes one copy and one clear:
41,247,360 bytes of logical uncompressed texel traffic and a crop allocation whose
captured memory requirement was 14,643,200 bytes. These are work/resource counts,
not a measured external-bandwidth reduction or exclusive GPU time.

Verified APK/source manifest and live native camp visuals passed. The official
LWI/gatord 2–18s capture measured 18.851 FPS at 2772×1240, fixed 3D scale 1.0,
without freezing or extrapolation. This is not a controlled comparison with the
previous 17.366 FPS and does not meet 30 FPS. Evidence:
`.local/logs/present-copy-elision-evidence.json` and
`.local/logs/present-copy-elision-native-official-summary.json`.
An initial export crashed in the vendor analytics sender after producing the
complete CSV; re-exporting the same capture with the officially documented
`ARM_DISABLE_ANALYTICS=1` succeeded and produced byte-identical CSV data.
