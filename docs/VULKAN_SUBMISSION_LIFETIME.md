# Vulkan submission and resource lifetime audit

## Finding and evidence

NVN recording/queue submission does not require the CPU to wait after every submission.
The original interface layer queues translation/presentation work; explicit fences, CPU
readback and swapchain-texture reuse impose necessary waits. Original Metal submits asynchronously.

Vulkan added a stronger bring-up constraint, retained by the default serial reference:
`nvn_vk.c:mtl_sub_begin` calls
`mtl_finish` before every submission, waiting for earlier GPU work and CPU-visible effects.
`nvn_vk_window.c:vk_window_present` also calls `vk_internal_wait` after its copy submission
before presentation. These are not inherent requirements of NVN.
Later sections track resource isolation and the debug overlap switch; overlapping begin
does not by itself imply asynchronous presentation. Stage six below now also connects
device-side presentation dependencies under the same debug option; CPU consumers remain under review.

Original `nvn_mtl.m:mtl_sub_begin` selects an allocator by completed fence, grows the set
when necessary, and waits for the oldest only at `ALLOC_MAX=8`. `mtl_sub_commit` returns
after submitting; completion callbacks publish queries/events and `g_done`. `arena_commit`
and deferred release preserve resource lifetime. Metal still waits at explicit completion,
readback, allocation limits and texture acquire; it is not universally wait-free.

## Device observations

New APK, 2772×1240 output, fixed 3D 1.0, no RenderDoc/validation/custom GPU timing:

- Four actual-present windows, 256 intervals: 110.312 ms / 9.065 fps; zero idle HWUI
  redraws. `.local/logs/android-native-resolution-normal-present.json` uses `control=none`;
  phase labels do not indicate control changes. This is not a controlled speedup comparison
  against the old handheld/720p configuration.
- In approximately 7.989 seconds of Perfetto, translation TID 12751 runs 2.460 seconds
  and sleeps 5.364 seconds. All 142 complete sleep intervals are followed by wakeups from
  GPU-completion TID 12733; runnable queue time is approximately 68 ms. Evidence and SQL:
  `.local/profiles/android-native-resolution-scheduling-v2.*`.
- Vendor FPSGO `C|thread-id|...` metadata produces conflicting process associations;
  there are 1454 systrace parse errors. Analyze actual Linux TIDs verified before/after
  capture, not the superseded PID-only report. Scheduling is not GPU execution timing;
  sleeping can include driver waits, CPU writeback and completion callbacks.
- GLES reports r32p1; Vulkan driverVersion is 32.1.0. No GPU render-stage tracks were
  obtained. Arm Timeline documents r47p0 or newer for basic driver tracing; a layer cannot
  supply absent driver support. Arm gatord hardware counters remain usable.

This supports prioritizing overly broad synchronization, but proves neither the speedup
from removing it nor the absence of other GPU bottlenecks.

## Wait chain and priorities

NVN is a graphics API, not a synchronous drawing primitive. CPU return, GPU completion
and actual display presentation are distinct points. Preserve observable NVN ordering
without turning every GPU dependency into a CPU fence wait. One submission is not one
frame; Metal's eight allocator slots do not mean eight frames of prerendering.

1. **Confirmed host serialization.** Begin waits for completion before the CPU prepares
   another submission; its next begin again waits for GPU work and CPU effects. Preparation
   and execution cannot overlap fully and can leave alternating gaps. Wakeup/scheduling adds
   overhead. Original Metal comments also describe hot-device waits and CPU downclocking with
   a small fixed allocator ring; that frequency effect still needs independent device evidence.
2. **Real dependencies introduced by Android mirrors.** This device has no usable external
   host-memory import. `sync_pools` writes guest dirty pages into coherent Vulkan buffers;
   completion copies GPU results back into guest memory. HOST_COHERENT removes explicit cache
   flush/invalidate requirements, not CPU/GPU access hazards. Separate descriptor/command pools
   alone cannot make begin asynchronous. Track actual in-flight read/write ranges, version
   temporary read-only data where appropriate, and retain waits for overlapping CPU consumers.
3. **Overly broad GPU dependencies are a separate issue.** `vk_barrier_at` uses ALL_COMMANDS
   and broad read/write access masks, ending the render pass. This does not directly block
   the CPU, but can limit GPU overlap and increase attachment traffic on Mali. Refine stage,
   access and pass boundaries using actual resource usage. Do not delete all barriers or
   derive their cost from CPU sleep durations.

With ideal overlap, steady-state intervals approach the larger of CPU preparation and GPU
execution rather than their sum, subject to real cross-frame dependencies, display pacing
and resource limits. Available GPU timeline evidence does not justify a target-FPS promise.

## Resources that must outlive submissions

The table records dependencies before this refactor. The first implemented step is
listed below; unfinished ownership still retains the safety waits.

| Resource/behavior | Current serialization dependency | Required asynchronous ownership |
| --- | --- | --- |
| Primary/secondary command buffers and pools | Begin reclaims `vk_retired` and old draw jobs | Retain per submission; reclaim after completion on owner thread; externally synchronize pool access |
| Descriptor pools | `vk_shader_begin` resets every pool | Separate pools for in-flight contexts, reset only after completion |
| Constant/storage/driver-data caches | Begin clears tables; staging is globally reused | Keep both recording metadata and actual GPU data per context |
| Upload/copy staging buffers | Begin recycles all temporaries | Recycle objects only after their submission completes |
| Render passes/framebuffers | `vk_graphics_begin` destroys old jobs | Retain through last referencing submission, including secondary buffers |
| Timestamp/occlusion/region queries | Shared pools/indices and global completion metadata | Per-submission slots and result descriptions; no reset before consumption |
| Guest CPU pools and Vulkan coherent mirrors | `sync_pools` uploads after global completion | Track in-flight read/write ranges; wait for overlapping CPU changes or version the data |
| GPU writeback/events/query results | Completion publishes effects before guest fences | Preserve GPU completion → CPU effects → notification order |
| Image/view/program/sampler retirement | Current completion ordering and snapshot grace | Cover every in-flight reference; elapsed submission count alone is not completion |
| Android acquire/present | CPU waits for acquire fence and copy completion | GPU/display semaphores with proven per-image/per-context reuse rules |

GPU hazards still require barriers and appropriate queue dependencies; a single queue
does not replace memory visibility. Keep CPU waits at actual CPU consumption, guest explicit
waits, unfinished resource reuse, background saves and destruction boundaries.
`VK_KHR_timeline_semaphore` is now enabled optionally after querying the actual feature;
devices without the extension/feature retain the fence path. Binary semaphores and fences
can support the same architecture; Vulkan 1.3 is not required.

## Implementation and acceptance order

1. Introduce completion-indexed submission contexts corresponding to original Metal,
   retaining the serial path as a reference.
2. Move every resource, query description and effect into its proper lifetime; pass
   correctness checks while retaining the global wait initially.
3. Exercise multiple outstanding submissions, old GPU reads versus new CPU data, query
   reuse, early resource finalize and CPU-visible effects with actual GPU pixels and timing.
4. Relax submission-entry and presentation CPU waits separately. Require host/device sync
   validation and real camp/movement/panel/background-save regressions, then compare net
   performance at matching resolution and NVN settings.

Game draw commands, camera, gameplay and game binaries remain unchanged. Missing draws,
writebacks or image quality cannot count as an optimization.

## Implemented: staging and diagnostic-query ownership

`nvn_vk.c` now gives each submission a `submit_resources` set containing upload/readback
staging, whole-submit timing queries, region queries and their metadata. Commit transfers
the set to the in-flight submission. Completion reads that submission's queries/metadata,
finishes CPU writebacks/callbacks and hands the whole submission back. The submit thread
detaches completed submissions, reclaims command buffers and reuses bounded staging/query
resources. The completion thread never accesses the Vulkan command pool.

Diagnostic enablement is latched at begin. Disabling it no longer writes an old query pool
just because it was previously allocated. New real-GPU checks alternate enable/disable,
change the environment during recording and verify GPU fills, CPU copies, callbacks and
guest-fence completion order. Host lavapipe normal/sync validation passes 489446 checks;
external profiling passes 489443. The Mali APK fixture passes 489446 with no VUID/SYNC errors.
Evidence: `.local/logs/vulkan-submission-resources-host-{sync,external}.log` and
`.local/logs/android-submission-resources-vulkan-test.log`.

The same APK passed real-game camp/English inventory/actual movement round-trip/original
background saving at 2772×1240 and fixed 3D 1.0, with 1152079 full pipeline-key checks,
107 region-GPU result entries and no observed synchronization-validation errors.
`.local/logs/android-submission-resources-verification.json` links source/APK hashes,
actual player/output state and two fresh original save commits. This run enables full
validation/timing and is not a normal performance baseline. Ordinary execution without
injection is restored afterward in external-profile mode.
Four normal windows with 256 actual-present intervals measure 9.044 fps, 110.569 ms,
with zero HWUI redraws, still approximately 9 fps. No net speedup is claimed for this
ownership change. Device main/sdk/subsdk0 hashes still match the original build manifest.

Full submissions are not yet concurrent: descriptor pools, draw jobs and mirror hazards
still require begin's global wait. Serial fixtures do not prove safety with multiple
submissions in flight. Continue separating ownership before adding that acceptance test.

## Implemented: descriptor/cache and draw-job ownership

The second step adds `vk_shader_submission` and `vk_graphics_submission` to
`submit_resources`. Descriptor pools/cursors, constant/read-only-storage/driver-data caches
and storage-writeback records belong to each submission, as do render passes, framebuffers
and secondary command buffers. Commit detaches CPU recording cursors. Completion never
accesses the command pool. The submit thread resets descriptors, clears caches and destroys
draw jobs only for completed submissions, before staging reuse. Shaders, compiled pipelines
and immutable constants remain shared; NVN keys, draw commands and quality are preserved.

`native_vulkan_submit.inc` exercises two descriptor owners and three real Vulkan submits.
A/B wait on separate host-signaled timeline semaphores. Release/wait A alone; with B still
confirmed incomplete, recycle A and reuse it to record C. Eighteen descriptor sets cross the
fixture's four-set pool limit. Verify all 1152 GPU-computed words and unchanged guest-fence
numbering. Host sync validation passes 490606 checks; external profiling passes 490603;
Mali APK sync validation passes 490606. The device executes the test without skipping and
actually enables the feature. Evidence: `.local/logs/vulkan-submit-bindings-host-{sync,external}.log`
and `.local/logs/android-submit-bindings-vulkan-test.log`.

This proves descriptor isolation with real outstanding GPU work, not complete NVN submission
concurrency. Begin's global wait remains. Mirror hazards, GPU→CPU writebacks, transient
image/buffer views, image retirement and presentation synchronization still require work.
In particular, `vk_texture_end_submit` still destroys the previous transient views globally;
`vk_buffer_writeback` still reads shared mirrors that later GPU writes could overwrite
before CPU copying. Separate both before removing begin's wait. Timeline semaphores are synchronization
primitives; they do not establish Arm GPU Timeline profiling support on the r32 driver.

The same APK passed real-game native output/fixed 3D 1.0, English inventory open/close,
actual movement round-trip and two original background-save commits. There were 276693
full pipeline-key checks, 52 GPU region result entries and no observed synchronization
errors. `.local/logs/android-submit-bindings-verification.json` links source/APK/unchanged
game-image hashes and actual state.
After restoring normal execution without validation/injection, four windows with 251
actual-present intervals measured 8.876 fps, 112.668 ms and zero HWUI redraws, still about
9 fps. This is not an asynchronous-submission performance result. Different cold starts
in sequence are not a controlled speedup/regression comparison. Final device state remains
native output/fixed 3D 1.0 with both USB and Wi-Fi ADB healthy.

## Implemented: transient texel views and writeback snapshots

The third step retains transient `VkBufferView` objects in `vk_texture_submission`, owned
by `submit_resources`, destroying them only after GPU completion/CPU callbacks and descriptor
pool recycling. The unused transient image-view field had no creator and was removed.
Actual image views remain in image caches; image retirement still needs work.

`vk_buffer_writeback` now records ordered byte intervals per pool, merging overlapping or
adjacent ranges but preserving gaps. At the submission boundary,
`vk_buffer_writebacks_flush` copies final GPU contents into exclusive submission staging
and registers guest/shadow effects. Later GPU overwrites of shared mirrors cannot alter
these snapshots. Completion still publishes all copies before callbacks/events and guest
fences. `vk_pool_read` checks unflushed ranges and existing effects, preserving dependencies
despite deferred snapshot allocation. New writes after an explicit flush must be registered.

New GPU cases check final contents, source overwrites after flush, unaligned/duplicate/
overlapping/adjacent intervals and gaps. Original NVN texel registration/binding/dispatch
runs an original R32UI fixture shader. Two real view owners remain in flight while one
completes and is recycled/reused; output words are verified. Host sync validation passes
492220 checks, external profiling 492217, and Mali APK sync validation 492220, without
observed VUID/SYNC errors. Evidence: `.local/logs/vulkan-submit-readback-host-{sync,external}.log`
and `.local/logs/android-submit-readback-vulkan-test.log`.

This is not complete concurrent-NVN acceptance. Begin's global wait remains. CPU uploads
versus in-flight GPU accesses, CPU consumption of earlier unpublished effects, image/sampler
retirement and acquire/present synchronization still need work. Snapshot/coalescing
performance requires separate measurement; fixture counts do not establish frame rates.

The same APK passed real English camp/inventory, actual movement round-trip and two fresh
original background-save commits at native 2772×1240/fixed 3D 1.0. There were 273049 full
pipeline-key checks, 51 GPU region result entries and no observed synchronization errors.
`.local/logs/android-submit-readback-verification.json` links source/APK/unchanged three
game-image hashes and actual state. Validation execution is not a normal FPS baseline.
Four restored normal-mode windows contain 246 actual-present intervals at 8.783 fps,
113.861 ms and zero HWUI redraws. No net speedup is claimed. Final state remains native
output/fixed 3D 1.0, with full submission waits retained.

## Implemented: actual completion serials and pending CPU ranges

The fourth step reserves a queue serial per recording set and checks it at commit.
Completed serials publish only after GPU work, queries and all CPU effects. Images and
replaced samplers retain the serial of their last recording use. Retirement requires both
original translation-snapshot grace and actual completion, including internal submissions;
submitted/guest-fence counts are not GPU completion. CPU-only sampler queries cannot clear
the last-use serial.

Each submission snapshots immutable CPU-write ranges for in-flight queries. These outlive
effect nodes freed by completion and are reclaimed by the submit thread. `vk_pool_read`
distinguishes current unflushed/flushed writes from earlier in-flight writes, releasing
locks before waiting for overlapping earlier results. Original item-drag ghosting uses
the same query so deferred snapshots do not hide unflushed writes. No game-specific
conditions or matrix edits are added.

An original GPU test advances grace through real backend submissions, then blocks actual
sampling with a timeline semaphore. Finalize the image, replace the sampler and attempt
retirement; both must survive. Release GPU work, verify 64 sampled values and CPU-range
publication, then verify destruction. Counters/game state are not patched to manufacture
grace. Unflushed/flushed/in-flight/completed writes, gaps and endpoints have assertions.
Host normal/external sync validation passes 492322/492319 checks; Mali APK passes 492322,
without observed VUID/SYNC errors. Evidence: `.local/logs/vulkan-submit-retirement-host-{sync,external}.log`
and `.local/logs/android-submit-retirement-vulkan-test.log`.

Full begin waits remain. CPU→GPU uploads still need in-flight GPU-access conflict handling;
packaged-texture/other CPU mirror reads and presentation dependencies need coverage too.
Isolated cases do not establish complete concurrent NVN or full-game acceptance.

The same APK passed real English camp/inventory, movement round-trip and two fresh
original background-save commits at 2772×1240/fixed 3D 1.0. There were 424715 full pipeline
key checks, 59 GPU region result entries and no observed synchronization errors.
`.local/logs/android-submit-retirement-verification.json` links source/APK/unchanged game
images and actual state. This turn validates dependency queries for ghosting; panel
open/close is not presented as complete item-drag interaction acceptance.
Four normal-mode windows without validation/injection contain 253 actual-present intervals
at 8.965 fps, 111.540 ms and zero HWUI redraws. No net speedup is claimed. Final state is
native output/fixed 3D 1.0, with full begin waits retained.

## Implemented: pool-access hazards and bounded overlap

The fifth step records merged GPU-access intervals per submission and retains each pool
until GPU use and CPU effects finish. Pool synchronization pins a snapshot of the pool list,
then releases the resource lock before visiting dirty pages. It waits for prior CPU writes
before comparing guest/shadow, and waits for GPU accesses covering the actual upload span
only when bytes changed. Unrelated pages proceed; completion waits never hold `V.resources`.
Finalizing a pool waits for its references before destroying its buffer.

Debug option `DM_VULKAN_ASYNC=1` / launcher `--async-submit` replaces unconditional begin
finish with completed-resource reuse, capped at eight sets; only exhaustion waits for the
oldest pending submission. Serial remains default. Actual GPU timeline gates verify that
unrelated uploads proceed, overlapping uploads wait, and old GPU/new CPU data survive.
A second test uses two actual `mtl_sub_begin/commit` calls with both submissions pending,
checks separate staging and CPU-write metadata, then verifies output and completion.

Host asynchronous normal/external modes pass 492666/492663 checks; the same Mali APK's
asynchronous/serial modes pass 492666/492527, without observed VUID/SYNC errors. Serial
explicitly skips the simultaneous-submission case. Its observation timeout occurred while
OEM Hans froze the screen-off process; the same test completed after wake, so this pause
is not evidence of a Vulkan deadlock. Logs: `.local/logs/vulkan-pool-hazards-host-{async,async-external}.log`
and `.local/logs/android-pool-hazards-vulkan-{async,serial}-test.log`.

The same APK in asynchronous debug mode passed real English camp/inventory, actual movement
round-trip and two fresh original background-save commits at 2772×1240/fixed 3D 1.0.
There were 45 GPU-region records and no observed sync-validation errors. Source/APK hashes,
unchanged three game modules and actual state are linked in
`.local/logs/android-pool-hazards-verification.json`. This does not accept full asynchronous
compatibility or performance: presentation still CPU-waits; packaged-texture CPU decoding/
deswizzling, compressed-copy fallbacks and other CPU consumers require review. Splitting
submissions deep inside callers holding descriptor/image pointers can invalidate resources.

The first interval-list APK's normal-mode serial/asynchronous/serial comparison measured
6.699/6.636/6.780 fps, four actual-present windows each, zero HWUI redraws, with translation
at roughly 0.49/0.50/0.49 core-seconds per second. Begin overlap alone showed no gain.
`.local/logs/android-pool-hazards-comparison.json` records the comparison. The growing
interval list also exposed a CPU hot-path cost: small bindings repeatedly walk from its head.
This bookkeeping cost is not intrinsic to Vulkan.

GPU use now has a direct current-recording index per pool and a per-submission 4 KiB page
bitmap. Small ranges index their bits directly; whole interior words are filled in bulk.
Queries check every page covered by the actual copied span, allowing conservative waits
but no missed byte accesses. Exact GPU-to-CPU writeback intervals are unchanged. Completion
clears a pool index only when its serial matches, preserving a newer recording's reference.
Pool ownership and CPU-effects-before-completion publication remain unchanged.

Additional GPU cases cover the first bit, a 64-bit word boundary, complete interior words,
the pool tail and cross-submission reuse of one pool. Host serial/asynchronous/external
asynchronous pass 492648/492787/492784 checks; the same Mali APK's asynchronous/serial pass
492787/492648 without observed sync errors. Logs: `.local/logs/vulkan-pool-index-host-{serial,async,async-external}.log`
and `.local/logs/android-pool-index-vulkan-{async,serial}-test.log`. The new APK's asynchronous
native camp/inventory, movement and two fresh original background-save commits also pass.
Its 51 GPU-region entries, no observed sync errors, and source/APK/unchanged-module hashes
are linked in `.local/logs/android-pool-index-verification.json`. Full asynchronous
compatibility is still unaccepted, and presentation still CPU-waits.

Normal-mode, same-APK bitmap serial/asynchronous/serial measured 8.636/8.588/8.676 fps,
translation 0.327/0.330/0.328 core-seconds per second, four windows each and zero HWUI redraws.
`.local/logs/android-pool-index-comparison.json` records it. The bitmap removes the first
list version's clear CPU regression; begin overlap itself still shows no net throughput gain.
Returning to approximately 9 fps does not solve the low-frame-rate problem.

## Structural performance work

Assess changes against a 30 fps/33.3 ms frame budget. Current intervals are about 115 ms,
with roughly 38 ms of translation-thread CPU per actual present; removing waits alone is
insufficient. Earlier same-APK native/720p/native runs changed pixel count by 3.73x but
intervals by only about 1.30x, showing substantial costs that do not scale with output pixels.

- CMake still forced the whole Vulkan/HLE host and original feature modules to `-O1`.
  New `DM_ANDROID_RUNTIME_OPT_LEVEL` / APK `--opt-level` defaults to `3`, preserving symbols
  and frame pointers without fast-math. Require device correctness and actual-present
  measurements with unchanged shaders/native output; compilation alone proves no benefit.
- Every draw/dispatch currently allocates and updates descriptor sets. Reuse must follow
  actual resource identity/version while preserving writebacks and view upgrades. CPU-source
  alignment snapshots can use mapped upload rings to avoid GPU transfers/pass breaks;
  GPU-produced inputs must retain GPU dependencies. Quantify operation counts before
  choosing and implementing these generic paths.
- Replace global barriers with resource-use/state dependencies and device-side
  acquire/copy/present synchronization, including crop lifetime, source swapchain reuse and
  background saving. Removing a CPU wait in isolation is insufficient.
- Original `set_time_constraint_comp` does nothing on Android. The phone supports ADPF
  sessions with five graphics threads, but the game has no session and the Manifest does
  not declare a game category. Add/measure Android scheduling integration, without copying
  Apple's real-time policy, forcing clocks or reporting GPU waits as CPU work.

New official counters are in `.local/profiles/android-pool-index-structural.apc` and its
export directory. No CPU stacks/screenshots/GPU layer; temporary permissions restored.
These are system GPU counters, not exclusive pass timings. Static G710 analysis of 111
actually loaded shaders finds 16 spilling fragment shaders, with hot estimates of 396–606
bytes of spill traffic. `.local/logs/android-mali-structural-shaders.json` records estimates
from compiler driver r56p1 versus the phone's r32p1; they cannot be converted directly to fps.

Enabling original AST expression optimization first reproduced missing function-argument
storage during compilation. A candidate fix passed AST regression, all 6931 SPIR-V modules,
and byte-identical debug references for five shaders. However chain `11d81bd4309a` produced
visible player/NPC black artifacts on device: **rejected, with no performance acceptance**.
After fresh original background saving, the complete previous cache manifest was hash-checked
and `3a0ffe903acf` restored, recovering the image. Experimental vendor/compiler changes were
also withdrawn from the main source. Patch/static reports/tests remain in
`.local/build/mali-optimized-ast/`; rejection/rollback is recorded in
`.local/logs/android-optimized-ast-rejected.json`. Bit-preserving C1 folding and GLSL
roundtripping remain local compiler experiments, never deployed. Future compiler changes
need numerical/pixel differential evidence; full-library compilation is not semantic proof.

## Dependency order for the next changes

1. Cover CPU consumers first. `image_upload` CPU-deswizzles/decodes `t->mem`; pending GPU
   results must be handled before reading bytes or testing dirty generations. Decoded BC's
   B2T fallback still scans only `V.effects.e.src`, missing unflushed deferred snapshots;
   use the unified pending-write query and synchronize at a safe outer entry. Do not split
   submissions after allocating descriptors/render jobs or while holding retireable image
   pointers. Conversely, current constant and read-only storage alignment snapshots use
   `vkCmdCopyBuffer`; GPU-to-GPU dependencies do not need additional CPU waits. Coverage-proof
   vertex/index CPU reads already use `vk_pool_read`.
2. Connect acquire semaphores to copy submission, and retain one render-finished semaphore
   per swapchain image for presentation to wait on. A submit fence does not prove presentation
   has consumed its semaphore. The global crop intermediate also needs in-flight ownership
   before asynchronous reuse/resizing; an old image cannot be destroyed while referenced.
   Return an internal queue-completion ticket, preserving original NVN AcquireTexture
   backpressure when it reuses that source texture. `mtl_present_wait` must use that same
   internal serial domain, not interpret internal serials as guest fences. Preserve Surface
   replacement draining and background-save discard behavior.
3. Compare complete serial/overlapped actual presents and scheduling gaps. If GPU execution
   still dominates, narrow `ALL_COMMANDS` barriers and render-pass boundaries by actual
   resource use and inspect Mali attachment traffic. CPU sleep is not removable GPU time,
   and GPU timestamp spans are not exclusive busy time. Do not change game scene/camera/draw logic.

References: [Vulkan synchronization](https://docs.vulkan.org/spec/latest/chapters/synchronization.html),
[Swapchain semaphore reuse](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html),
[Arm Timeline driver requirements](https://github.com/ARM-software/libGPULayers/blob/main/layer_gpu_timeline/README_LAYER.md).


## Stage six: device-side acquire/copy/present dependencies

`DM_VULKAN_ASYNC=1` now also enables asynchronous presentation; the default remains serial.
Acquire uses a binary semaphore waited by the copy submit at TRANSFER. Each swapchain
image owns a render-finished semaphore for present. Acquire slots retire by internal copy
completion; render-finished reuse follows reacquisition of its swapchain image, not an
ordinary submit fence. Present returns an internal copy ticket for original NVN source-texture
reuse backpressure; `mtl_present_wait` uses the internal serial domain. Replaced crop images
retire after referencing submissions finish, and Surface replacement retains queue draining.

The new binary GPU-chain/internal-ticket/CPU-effect case complements the existing test
that separately gates actual overlapping submissions. Host serial/async/external async pass
492648/492929/492926 checks; the same Mali APK passes 492929/492648 async/serial. Real native-output
camp/inventory/movement, original HOME saving and same-process Surface recreation pass;
109 GPU-region records have no observed synchronization errors. Source/APK/unchanged-game
hashes and evidence are linked in `.local/logs/android-present-chain-verification.json`.

**No clear throughput gain.** Same APK, 2772x1240, fixed 3D 1.0, unchanged shaders, no
validation/injection/custom timers: serial/async/serial yield 8.664/8.704/8.611 fps, four
actual-present windows each, zero HWUI redraws. Reports:
`.local/logs/android-present-chain-{serial-a,async-b,serial-c}-present.json`.
O3 also passes device correctness, but prior normal O1/O3 measurements were approximately
8.632/8.591 fps, with no established benefit. Completing asynchronous architecture or
compiler switches does not establish progress toward the 30 fps performance target.

## Acquire synchronization scope correction

Original `nvnWindowAcquireTexture` CPU-waits the selected texture's presentation ticket,
then unnecessarily assigns the latest whole-queue `x_fence()` to its acquire sync. An idle
texture thereby inherits unrelated queue work. After the existing per-texture wait, acquire
now returns an already-satisfied sync; explicit `nvnQueueFenceSync` remains unchanged.
Fresh-process mode `DM_VULKAN_ACQUIRE_TEST=1 DM_VULKAN_ASYNC=1` records/submits through original
NVN while a real GPU timeline gate blocks unrelated queue work. Before correction both
unused textures time out; afterwards they signal immediately, while the explicit queue
fence remains pending until gate release. Release precedes assertions to avoid stranded GPU
work on failure. Host logs: `.local/logs/vulkan-acquire-scope-{before,after}.log`.
On Mali APK use `run_vulkan_tests=true async_submit=true acquire_scope_test=true`; sync
validation passes. This proves synchronization scope, not game use of that CPU wait or
frame-rate benefit. Actual-game comparisons remain separate.

## Experimental streaming at target boundaries

`DM_VULKAN_STREAM_SUBMIT_REFERENCE=0` requires async mode and preserves one logical
NVN submit across several internal Vulkan submits. At most four natural target
changes after 256 draws flush a prefix. Every cut closes the pass with STORE, retires
its resources by internal queue serial, and starts a fresh recording. The next pass
loads real attachment contents. The logical replay context survives; custom upstream
freeze/extrapolation paths disable cuts. Only the final commit advances the guest
fence. Existing query sample references carry prefix results across resource sets;
event publication and CPU effects follow each real GPU fence in queue order. No
ALL/BOTTOM sync is interpreted as an early vertex-stage release.

`native_vulkan_stream.inc` covers prefix/reset/final queries, GPU writes, events,
LOAD preservation, pixel results and exactly one guest-fence increment. This is an
opt-in candidate; correct internal batching does not establish a sustained game FPS.

## Experimental publication from CPU-known copy sources

`DM_VULKAN_CPU_COPY_REFERENCE=0` / `--cpu-known-copies` keeps the original GPU
copy, execution barriers and actual completion fence. It can replace the trailing
GPU-to-CPU copy with an immutable CPU-known byte snapshot. The default is off.

For inline UpdateUniformBuffer, the recorded payload is copied immediately into
owned CPU storage. For pool-to-pool copy, the source must overlap no current,
flushed or in-flight CPU publication/GPU writeback range. This is a nonblocking
proof: uncertainty retains the original GPU readback. Existing backend buffer
writers must continue registering their complete written ranges; ordinary VS
outputs are not buffer writes.

Pool source snapshots are filled **after commit's pool uploads**, from the exact
uploaded shadow bytes, before vkQueueSubmit. Reading shadow while recording is
incorrect because lazy uploads have not occurred yet; reading guest memory at
completion is also incorrect because the CPU may have changed it. A subsequent
GPU write to the source cannot mutate the owned snapshot.

Destination writeback ranges still follow command order. Any unknown or partial/
adjacent overwrite invalidates a merged range's known proof; a new known write
can replace an old proof only if it covers the entire merged interval. Otherwise
the final GPU contents are read back. Explicit writeback flush seals the existing
snapshot as before. Completion first publishes all guest/shadow byte copies,
then events/callbacks and queue/guest-fence completion; owning snapshots are freed
only in that completion callback pass. No guest or internal fence is signaled early.

The regression includes byte guards, source writes before/after the copy, partial
and adjacent destination writes, known overwrite of an unknown range, explicit
flush, late CPU edits before lazy upload, reference/candidate/reference switching,
and a timeline-gated earlier GPU source write. The gate confirms fallback without
waiting just to obtain a CPU-known proof. Frame-rate and vendor-counter acceptance
remain separate from these correctness checks.


## Distinguish producers from consumers

| Data | Producer and consumer | Required distinction |
| --- | --- | --- |
| Ordinary VS outputs | VS to later graphics stages | Pipeline-internal data, not guest buffer writes or a reason for CPU readback |
| Writable storage/texel buffers | Explicit shader stores to later GPU/CPU users | Register reflected writable ranges; compute is one possible producer, not evidence that CPU consumption occurs |
| Buffer copies/inline updates | GPU transfer to GPU or original pool CPU observers | Mirrors currently publish final bytes before completion becomes visible; CPU-known snapshots only change the readback source |
| Image-to-buffer | GPU transfer to buffer consumers | Destination writeback is conservative; deferring it requires complete cross-submission version tracking |
| Read-only alignment copies | Transfer to shader | GPU-to-GPU dependency; writable alignment copies additionally copy back and publish |
| c0/c2 driver tables | Host generation to shader | Not GPU results. CPU-owned comparison keys avoid reading mapped memory merely to deduplicate host data |
| Timestamp/occlusion reports | GPU query to original NVN report/conditions | Preserve report/event order and submission-owned query lifetime |
| Texture decode/deswizzle/coverage proof | Pool data to host algorithms | Real CPU consumption; optional coverage proofs already abandon pending data without waiting. Texture paths still need the audit described above |

### Synchronous CPU texture reads (2026-10-17)

`mtl_read_texels` now follows the original Metal GPU-readback path: transfer into
tightly packed staging, then scatter only the actual block rows into the CPU
destination. CPU row/image strides are not Vulkan buffer-copy strides. The old
whole-span memcpy overwrote row/slice padding; it also rejected legal odd CPU
pitches through Vulkan alignment checks. A regression reproduced the first bad
padding byte at offset 12 (128 instead of the sentinel 215) before the fix.

The scatter is a registered completion write, executed after the real GPU fence
and before the submission's temporaries can be reclaimed or completion published.
The synchronous internal wait retains its stack context until that callback ends.
Only row bytes are passed to `dm_guest_write_prepare`; conservative dependency
metadata covers the enclosing destination span. No game fence is advanced.

The shared host/Android fixture checks every destination byte, including guards,
for a nonzero mip, array-view base layer, 3D slices, odd row/image pitches, and the
original depth-zero-means-one convention. This is a correctness fix, with no FPS
claim or added profiling timer. Packaged/decoded texture CPU-source parity remains
part of the separate audit; this change does not claim to finish that work.
| Screenshots/draw tracing | GPU image to host files | Explicit diagnostics, disabled during normal external profiling |

A registered GPU write does not prove that the game needs an immediate CPU
readback. Current submit-tail mirror publication is conservative. Do not infer
many synchronous game reads from transfer/storage writeback counts, or classify
ordinary VS varyings as writes to the original vertex pool.
