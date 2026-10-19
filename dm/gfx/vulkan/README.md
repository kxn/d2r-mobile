# Native Vulkan backend

See [upstream special rendering optimizations](../../../docs/NVN_SPECIAL_OPTIMIZATIONS.md)
for game/mobile-specific behavior, defaults and Android implementation status.

Executes original nvn_obj.h records, hle_nvn.c objects/recording/translation queue and ARM64
thunks through the original mtl_* ABI. Game instructions execute directly on the phone CPU;
no CPU interpreter, JIT or emulator CPU backend. See the [port plan](../../../docs/ANDROID_NATIVE_PORT_PLAN.md).

## Verified scope

PHP110 / Mali-G710 MC10 / API 36 / actual 4 KB pages runs three real game modules and renders
original mobile Chinese character selection/camp and English camp/vendor panels. Touch movement/drag-aim kills, portal,
world interactions, item drag/trade, binding and save/reload have evidence. Thirty-five
original host enhancement modules are linked and original v4 HUD numbers render.
Full playability remains unaccepted: more skills/maps/natural drops, complete enhancements,
frame rate, extended stability and distribution remain. Native fixed3D1.0 observations range roughly7–16fps with operating conditions;30fps is not achieved.

Latest experimental Linux lavapipe/Mali APK checks pass507392/507394; the new path remains default-off pending performance acceptance. Linux and APK runs use
Khronos synchronization validation; Mali CLI runs lack that layer and must be reported separately.
The count includes individual pixel/channel comparisons, not separate gameplay scenarios.
Coverage includes:

- Pixels/compute/occlusion; completion-driven writeback/events/counters; internal submits do not advance guest fences.
- Recorded constants, unaligned storage, image writes, BC fallback, half floats, viewport winding/culling, scaled copies.
- Descriptor-pool growth, pipeline/view/staging reuse, in-flight texture/replaced-sampler retirement.
- Timestamp query-block boundaries, submit-tail reporting, actual GPU timestamps and ring invalidation.
- Original screenshot queue/delay/crop/PNG output, with separate Pillow decoding of all 48 pixels in an 8×6 image.
- Original Metal stepRate equivalents: divisors 2/3, nonzero baseInstance and distinct pipeline cache keys.
  Capable devices use VK_EXT_vertex_attribute_divisor. The current Mali lacks it; the fallback
  waits for actual completion, reads coherent buffers and expands attribute data while retaining
  one original instanced draw and original InstanceIndex/BaseInstance. Forced fallback tests also
  cover a GPU update to source data within the same submit.
- Present discard returns before texture/Surface access and creates no guest fence, allowing
  game ticks to continue while saving before suspension.

6931 game offline SPIR-V modules validate for Vulkan 1.1: 6927 original translations and four
original built-in counter kernels. New original fixture shaders are separate and do not change
that game-cache count.

## Build

```sh
scripts/dev/build-android-native.sh --touch --tlsdesc
scripts/dev/build-vulkan-shaders.sh
python scripts/dev/build-android-apk.py --tlsdesc
```

Deployment is in [the shell guide](../../android/README.md). test-android-vulkan.sh runs CLI
fixtures; APK --ez run_vulkan_tests true covers packaged on-device synchronization validation.
See [TLS ABI](../../../docs/ANDROID_TLS_ABI.md) for native loading without preload.

## Implementation and boundaries

The shader builder reuses original extraction, job_for, facts and dmsh_xlate, separates stage
sets, adds image capabilities/read-only declarations and validates every module. Fingerprints
cover the translation chain, metadata ABI and tools. Runtime object changes do not invalidate
offline shaders. All reflected bindings in the current 6931 modules have count=1; descriptor
arrays remain explicitly unsupported. Array textures are different from descriptor arrays.

Real fences gate writeback/events. Internal readback, present and vertex-expansion submissions
do not advance guest NVN fences. Original write-protect dirty generations drive coherent pool
mirrors; Android tagged pointers are normalized. Constants and readonly unaligned-storage
snapshots are reused by actual GPU-write epoch; staging retention is bounded at 256 MiB.
Pipelines cache actual static state, including divisors. Textures follow original submit stamps
and an eight-submit grace period. Consecutive draws retain compatible render passes; dependencies,
transfers and queries split them when necessary.

Negative viewports preserve NVN winding. Android prefers identity Surface transforms,
letterboxes and crops sources before sampling. Original mobile 3D scale adapts 0.75→0.60 with
native Android Surface pixels (2772×1240 on this phone). Fixed3D1.0 is used for controlled comparisons. Actual TOP/BOTTOM timestamps respect validBits/period and accumulate per
original frame tag; fence-wait wall time is separate. Presentation times use the driver extension.
Uncalibrated GPU/host spans and unavailable instruction/cycle counters are not measurement
evidence; ELF memory is labeled RSS.

Remaining correspondence work must audit behavior actually implemented in nvn_mtl.m: more
formats/views/copies, further runtime extrapolation acceptance and graphics diagnostics.
The latter enhancement paths remain incomplete; linking host modules does not implement their GPU work.
List operations missing in Metal separately; a generic NVN feature list is not original-backend
coverage. Unknown actual operations fail explicitly rather than silently skip.
Private evidence is under .local/logs/, currently android-vulkan-buffer-bytes.log and
vulkan-buffer-bytes-host.log for GPU checks. Readonly game-state.jsonl includes original
player/HUD/trade/binding observations; observation alone does not accept all gameplay.

## Format views and GPU diagnosis

Format-changing views follow original tex_upgrade_pfv: recreate lazily with MUTABLE_FORMAT, copy every GPU mip/layer and retire old views/storage only after completion. Unchanged textures stay immutable. View usage is restricted to sampling, storage or attachment as appropriate, so an sRGB sampling view does not inherit an unsupported storage usage. View-format clears preserve sRGB encoding. GPU checks cover same-submit writes before upgrade, sRGB decode/encode, raw bytes, base-view restoration and a two-layer, three-mip upgrade preserving all other contents. Format classes follow the [Vulkan specification](https://docs.vulkan.org/spec/latest/chapters/formats.html#formats-compatibility). Lazy recreation follows the original backend and avoids the general mutable-image performance cost described by the [Khronos guide](https://docs.vulkan.org/guide/latest/extensions/VK_KHR_image_format_list.html).

--ez gpu_profile true enables optional actual GPU timestamps for render passes and compute dispatches, grouped by first fragment/compute shader and extent. Default off. Region durations may include dependencies and overlap; do not add them as exclusive shader costs. Native fixtures exercise this path with synchronization validation. Real camp evidence: android-gpu-region-game.log and android-gpu-region-camp-state.json. This instrumentation establishes no frame-rate improvement.

Image shader-write usage now follows original tex_desc and requires the NVN IMAGE flag. The storage-image fixture explicitly declares that flag. This removes an overly broad usage declaration; device checks and real camp rendering pass, but no controlled performance improvement is claimed.


## Original drag-follow and texture clears

The original ghost_note/ghost_flush algorithm now replays the actual shelf icon draw through
Vulkan: original quad/grid hit, smallest rectangle, least-used reflected texture, translated
viewport, and state restoration. GPU fixtures verify the icon selection and pixels. Android
maps the original touch layout and origin tint through the same Canvas scale and cancels
stale imagery in letterbox bars. Real vendor drags and release/edge behavior are checked
separately from GPU fixtures. Real evidence includes hiding the original tooltip while
dragging and a scroll sale/purchase with gold 0→1→0 through the original host trade path.
The press point is restored before purchase; no player/item memory is edited.

Original clear_tex supports full XY clears across selected array layers and 3D planes, using
the view base level/layer plus zoffset/depth. Integer-view clears preserve raw bits; float/ui
clear values convert numerically to the attachment format. Depth/stencil uses the original
separate value slots. GPU readbacks cover untouched mips/planes, signed/unsigned values,
integer reinterpretation and both depth/stencil aspects. Partial XY/masked texture clears
remain unsupported in both the original Metal clear_tex and this backend. Volume slice
attachments use [Vulkan 2D-compatible 3D image views](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageViewCreateInfo.html).


## Original LUT brightening

nvn_vk_bright.c implements the upstream k_lut_msl/bright_lut path after compute shader
552f45c348a61b86, using the unchanged original dm_bright_k exposure policy. It copies the
just-generated 3D LUT, applies the same PQ/inverse PQ and highlight protection, then writes
back before tone mapping. UI composition remains after tone mapping. Push constants preserve
each dispatch's exposure; copies, descriptors and retired textures remain alive until GPU
completion. Unsupported LUT types retain the original table with a diagnostic, matching Metal.

Host GLSL is compiled offline by builtins.cmake/build_builtin.py and validated for Vulkan 1.1.
The generated SPIR-V/header are identical for Linux and Android; source/builder/output hashes
and compiler/validator versions are recorded and the APK packages the build manifest. This
built-in is separate from the 6931 game-derived shader cache and changes no game module.
Formatless storage writes require the feature and its supported float formats, following the
[Vulkan shader interface rules](https://docs.vulkan.org/spec/latest/chapters/interfaces.html).

GPU tests compare every output channel against a double-precision CPU reference with independent
trilinear sampling. They cover 8-bit/half-float, non-multiple workgroup dimensions, selected mip
preservation, alpha, highlight protection, exposure skip/cap and regenerating the LUT twice in
one submit. Real English camp hits a 32³ RGBA16F LUT; k=1/k=4 captures and a synchronization
validation run confirm the original game path. That run has no LUT/synchronization errors but
retains a Surface preTransform performance warning from the existing presentation path.
--ef bright_k 1..4 (launcher --bright-k) only forwards the original DM_BRIGHT_K diagnostic;
default launches use the original dynamic policy. No frame-rate improvement is claimed.


## Original frozen-world rendering

nvn_vk_freeze.c implements the world-freeze branch of original frz_op. The original
mtl_world_frozen/mtl_host_latch/mtl_host_apply handshake is latched per submit; window
identities come from the original NVN WindowInitialize path. The first frame draws the
complete world without UI-stencil draws. Later frames retain world results, drawing UI and
final composition while skipping original world target/clear/draw/copy/compute operations.
State changes, events, counters, buffer operations and texture uploads continue. Missing
frame structure (no depth-only pass) skips nothing. Closing the panel returns to full rendering.

This renderer does not infer freezing from Android panels or replace the original host
world-freeze handshake. --ez panel_freeze true (launcher --panel-freeze) only enables existing DM_PANEL_FREEZE;
the default remains off, matching upstream. Existing four observer gates 10–13 are reused;
no new game rewrite rules or binary changes. Runtime extrapolation is documented below;
normal rendering remains full by default. Original force-full requests retain submit latching
and now refresh the frozen world for the requested number of complete frames, including
UI-stencil suppression. This reinforces the original hint used before scale/target changes;
no game rewrite is added. Multiple submits within one frame consume one requested frame.

GPU tests exercise an actual NVN window, stencil masking, shrinking panels, nested OP_CALL
lists, world compute/copy/clear suppression, continuing events/timestamps/uploads, unfreeze,
and unrecognized frame structures. The original DM_NVN_TEST_FRZ_NEG negative control fails
by exposing stale red world pixels beneath a smaller panel, as required. Linux and Mali
synchronization validation pass. Real-game freeze/unfreeze preserves the original monster
snapshot and player HP; item movement, background commit and same-PID frozen-panel resume
have separate device evidence. game-state.jsonl publishes readonly world_freeze snapshots.
With synchronization validation, actual vendor sale/buyback changed gold 0→1→0 at a
constant world tick, then closing the vendor resumed ticks with unchanged original world
snapshot and HP. Android's slow-frame hit-query wait and directional-pulse duration were
reinforced in the existing frontend bridge. Evidence: android-vulkan-freeze-verification.json.
GPU tests replace a 16×8 world target with an 8×4 target while frozen, verify both requested
full frames, then verify cached pixels survive the next frozen frame. A before-fix run
reads stale red pixels. The default remains opt-in, matching upstream.
In the actual game with synchronization validation, opening inventory immediately after
entry freezes at scale 0.75. The unchanged controller then lowers it to 0.70/0.65/0.60;
each change draws four full frames, for 13 total including the initial frame. The original
world tick stays constant, item tooltips update, and closing inventory resumes the world
with the same original snapshot and HP. Evidence: android-vulkan-freeze-resize-verification.json.

## Byte-range buffers and original frame-rate policy

Buffer copies and recorded uniform updates preserve the original byte ranges, including
unaligned offsets and lengths. Uniform updates copy their recorded snapshot through staging;
they do not use vkCmdUpdateBuffer. Zero-length updates issue no GPU command. Bounds,
overlap checks, barriers and completion-driven CPU/shadow writeback remain in effect.
This follows original mtl_sub_copy and the byte-range semantics in the
[Vulkan 1.1 copy specification](https://github.com/KhronosGroup/Vulkan-Docs/blob/v1.1.130/chapters/copies.txt#L95).
GPU fixtures cover 128 source/destination alignment/length combinations, dependent same-pool
and cross-pool transfers, whole-pool guards, recorded-data lifetime, exact pool-end copies
and CPU edits after GPU writeback. Before-fix runs fail separately at both old alignment checks.

nvn_vk_pace.c uses original dm_fps_setting/DM_FPS policy and original nvn_pace_hz tiers.
Android acquire pacing applies this target directly because there is no Apple display link;
DM_PRESENT_HZ and the game's present interval retain precedence. Policy tests run on Linux
and native ARM64 with default, 30/60 overrides, invalid overrides, live setting changes,
process-latched environment and eight display rates (105 checks per platform, separate from
GPU counts). game-state.jsonl publishes target_fps, pace_hz and the period actually used by
the original NVN acquire pacer; a 30 fps target is not evidence of 30 rendered frames/s.
On the phone, actual taps/scrolls in the original Custom page and Apply produce a same-PID
60→30→60 target/period roundtrip. Readonly observations are taken after resuming the world:
the game's pause menu stops the server observer, so an old snapshot is not evidence of
the current setting. Android display refresh-rate hints remain separate platform work.
Combined evidence: android-vulkan-buffer-pacing-verification.json. Original game modules
and rewrite rules remain unchanged; the final normal launch keeps English and the 60 tier.


## Original runtime extrapolation

nvn_vk_runtime.c ports the original rt_* pass/state rules; nvn_vk_extrap.c and its offline
GLSL port rx_fs1 from nvn_rtex.inc. The double-precision inverse and ten skinned-shader keys
are identical to upstream. Original UI protection, skinned depth/shadow/world draws,
translucent effect marking, regional postprocessing, LDR history shifts and camera/scale
fallbacks are preserved. Missing history renders full frames. --ez runtime_extrap true
(launcher --runtime-extrap) forwards original DM_RT_AB=1; the upstream default stays off.

Vulkan retains NVN D24S8, which Metal promotes to D32S8. Both native formats are recognized
and GPU-tested. Stencil-export devices use the original single-pass operation; Mali uses
an R8_UINT hole mask and a second stencil-only pass. Both preserve current UI and all
stencil bits except the original 0x80 hole bit. History, descriptors and framebuffers retire
after actual GPU completion. The generic vk_pool_read helper synchronizes only overlapping
queued writes (including events/query records), then reads the original CPU pool. Unrelated
buffer updates no longer force camera reads to submit/wait. The optional stencil policy
lives entirely in nvn_vk_runtime.c; generic drawing consumes the resulting pipeline state.

Fixtures compare color/depth/stencil with independent scalar cubic sampling, including
translation, fractional shift, unit/UI holes, all-hole fallback and large world coordinates.
Synthetic original pass sequences additionally cover alternation, moving-unit removal,
camera jumps, exact force-full frame counts and unchanged guest-fence advancement. They
check opaque output before synthetic postprocessing, not the game's final composite. Both
stencil paths pass synchronization validation on lavapipe; the two-pass D24S8/D32S8 paths
pass on Mali. Forced pipeline unavailability retains full rendering (272295 checks).

Real-game acceptance recognizes D24S8, encodes reprojections in camp and area 2, moves by
original touch input, crosses 1→2→1, opens/closes English inventory and returns from 30-fps
full rendering to 60-fps extrapolation in the same PID. Captured screens were inspected;
more scenes and controlled image/performance comparisons remain. No FPS gain is claimed.
Same-PID HOME/save/Surface rebuild/resume continues encoded frames with unchanged HP.
Evidence: android-vulkan-extrap-verification.json, android-vulkan-extrap-d24.log and
android-extrap-d24-{game.log,eco30-state.json,eco60-state.json}. No game module is changed.


## Generic NVN correspondence

Uniform/storage descriptor alignment staging preserves byte ranges too: only the final
Vulkan descriptor offset must meet device alignment. GPU copy offsets/sizes no longer
carry a false four-byte restriction. This follows original Metal address binding without
interpreting game constants. Sixteen uniform/read-only-storage cases cover offsets 0–3,
sizes 17/19, and old/new data in three screen regions across a command-ordered update.
Thirty-two writable-storage cases cover source/destination offsets 0–3, sizes 257/259,
two dependent compute dispatches and a final GPU copy, comparing an entire 64 KiB data
region including guards. Separate pre-fix runs fail at uniform/storage alignment checks.
Lavapipe and Mali APK synchronization validation pass. Evidence:
vulkan-binding-bytes-before-{uniform,storage}-fix.log, vulkan-binding-bytes-host.log and
android-vulkan-binding-bytes.log. These fixtures execute no game modules; real-game
regression evidence is recorded separately.

Non-indexed QUADS now follows original draw()/g_quad_ib: a shared immutable 32-bit index
buffer maps each four vertices to 0,1,2,0,2,3, with first supplied as baseVertex. The original
65536-quad capacity is checked; incomplete trailing vertices produce no primitive. Indexed
quads remain unsupported in both original Metal and this backend. Eleven GPU pixel cases
cover zero/incomplete counts, multiple quads, nonzero first/BaseVertex, both cull directions,
mixing with ordinary indexed triangles and a subsequent non-indexed triangle. The pre-fix
fixture fails at topology 7. This test uses its own shader and no real game modules.

vk_pool_read provides coherent readonly CPU access during replay without a global GPU-write
epoch wait. Serialized begin already drains prior submissions. Current buffer-writeback,
event and query callback ranges identify whether the requested bytes require completion.
CPU edits made after begin are read from the original pool, rather than a stale mirror.
GPU tests check half-open boundaries, other pools, invalid ranges, CPU edits, dependent GPU
copies, event/query publication and unchanged guest fences. The initial implementation's
missing event dependency is preserved as a failing negative control before the range fix.
The real optional extrapolation caller performed 360 reads with no overlapping waits in
one stable camp sample. Sequential before/after timings differ in thermal state and do
not establish a controlled FPS improvement.

Original Metal OP_CLEAR_COLOR does not implement partial color-mask attachment clears either
(nvn_mtl.m diagnoses and skips them); Vulkan fails explicitly for that unsupported use. It
must not be listed as completed Metal functionality or addressed by changing game rendering logic.

The two executors dispatch all 39 recorded opcodes, but case labels alone are not semantic
coverage. Additional formats/flags/subtypes still need audit. Vulkan now plans depth/stencil
demand and same-submit full-overwrite store elision; additional load elision is in progress.
Cross-submit prediction and unwritten-plane store omission require Vulkan validity/device
assessment rather than literal Metal DontCare ports; see the special-optimization assessment.
Several original graphics statistics still return placeholders. These generic/diagnostic
gaps have priority over additional game-specific optimizations. Scope and policy are in
AGENTS.md and the port plan; game modules and rewrite rules are unchanged by these changes.
Evidence: android-vulkan-quads.log, vulkan-quads-{export,mask,fallback}-host.log and
android-vulkan-nvn-generic-verification.json.

## Pass depth/stencil demand planning

nvn_vk_plan.c corresponds to upstream pass_ds_need/ds_scan. At submit start mtl_sub_index
flattens original OP_CALL/OP_JUMP/OP_END order across all handles, then performs one backward
scan of draw state, avoiding repeated remainder scans per pass. Replay checks each operation
address; mismatches, nesting/operation limits or unavailable indexing retain attachments.
Remaining handles from mtl_exec_rest are already represented in this submission index.
DM_NVN_TBDR=0 disables the optimization.

When a whole pass provably uses neither depth test/write nor stencil test, color draws and
initial color clears omit the depth/stencil attachment. Mid-pass clears remain conservative.
If either plane is needed, the combined attachment still loads/stores both; Metal's individual
plane omission is not yet ported. A later same-target draw requiring an omitted attachment
ends the old pass and reattaches it. Pipeline caches distinguish actual attachment signatures.
Final depth/stencil state supplied by existing optional policies participates in current demand;
generic planning recognizes no game characters, camera data or shader keys.

Same-device on/off comparisons exercise D24S8/D32S8, initial clears, later test/write activation,
nested calls/returns, jumps, later handles and same-target resets. Forty scenarios independently
read back every color/depth/stencil pixel. Disabled runs record no omissions; enabled runs omit
only proven-unused passes. Lavapipe and Mali APK synchronization validation pass. Actual game
startup logs confirm the omission path is used; separate game regression is recorded in
android-vulkan-pass-depth-verification.json. Evidence: vulkan-pass-depth-{host,mask-host,fallback-host}.log
and android-vulkan-pass-depth.log. Vulkan render-pass store policy is specified up front by
[attachment descriptions](https://docs.vulkan.org/refpages/latest/refpages/source/VkAttachmentDescription.html), unlike
Metal's mutable end-of-pass policy. Demand planning alone does not implement store elision;
the separate implementation follows below. No controlled frame-rate improvement is claimed.

## Same-submit store lookahead and deferred pass recording

Following upstream sd_finish: old attachment contents need not be stored if the same subresource
is fully cleared later in this submission before any reader. Color, depth and stencil are judged
independently. Sampling conservatively reads the whole texture; attachments distinguish mip/layer.
Fragment/compute reads use original offline reflection. Copy endpoints, texture clears, attachment
draws before clears, partial clears and uncertainty retain STORE. End-of-submit uncertainty stores;
there is no cross-submission prediction. DM_NVN_TBDR=0 disables elision.

Generic drawing first records a Vulkan secondary command buffer. At actual pass end it chooses
the final render-pass storeOp, then begins that pass in the primary and executes the secondary.
Pipeline/framebuffer/inheritance signatures remain identical; only storeOp changes, as permitted
by [Vulkan render-pass compatibility](https://docs.vulkan.org/spec/latest/chapters/renderpass.html#renderpass-compatibility).
Queries, timestamps and vertex/index/descriptor state are recorded in the appropriate buffers.
Secondary buffers and both render-pass objects retire after real GPU completion. The completed
current draw is not treated as a future reader; extra draws such as the original drag icon cannot
incorrectly mark an unexecuted recorded operation complete.

If original freeze/extrapolation modules may filter future operations, their separate boundary
declares the overwrite proof unavailable and stores remain enabled. Generic planning reads no
camera/game state. Further store optimizations for these optional paths remain open. Current
queries use Metal-style per-texture event buckets and binary search, described below.

Sixty same-process on/off GPU scenarios cover full overwrites, partial clears, prior sampling/copy,
other mips, different/same array layers, nested calls/later handles, and D24S8/D32S8 depth-only,
stencil-only and combined clears. Every color/depth/stencil pixel and actual omitted-store count
is checked; partial coverage and other subresources preserve old values. Both stencil paths pass
lavapipe synchronization validation, as does the Mali APK. Actual camp logs confirm elision is
used; game regression is recorded in android-vulkan-store-verification.json. Logs:
vulkan-store-{host,mask-host,fallback-host}.log and android-vulkan-store.log. Check counts include
per-pixel comparisons, not independent gameplay scenes or demonstrated frame-rate gains.

## Store event index and differential checks

nvn_vk_store_index.inc follows upstream sdx event buckets and stable scatter. Each submission
builds per-texture read, attachment and color/depth/stencil-clear events once. Pass-end queries
binary-search by replay sequence rather than replaying all future state. Mips/layers remain
distinct. An address-sorted alias adjacency list accounts for distinct snapshots sharing or
partially overlapping storage. Unknown reads and pool boundaries create conservative global needs.

Descriptor registration/invalidation and pool-range changes advance an epoch. Changes during
building/querying fall back to the original scanner. Replay mismatches, resource/event limits
and optional command filters retain conservative behavior. DM_VULKAN_STORE_REFERENCE=1
selects the old scanner as a functional control. DM_VULKAN_STORE_CHECK=1, or enabling
DM_VULKAN_VALIDATE, compares each indexed result against the scanner and fails explicitly on
a difference. Normal runs do not perform duplicate queries. Read-only counters report builds,
indexed queries, reference operations, examined events, comparisons, mismatches and fallbacks.
The scanner remains available for diagnosis and invalidated indexes.

The existing sixty GPU on/off scenarios still pass. Ten additional metadata-only checks do
**not execute those streams as GPU draws**: shared/partially overlapping snapshots, unrelated
copies, unknown pool boundaries and descriptor registration after indexing. A stream with 64
unrelated state commands requires the scanner to visit at least 64 operations and the index
at most two relevant events. This excludes index construction cost and is not an end-to-end
speedup measurement. Both lavapipe stencil paths and Mali APK synchronization validation pass;
an independent forced-reference run also passes. Logs: vulkan-store-index-{host,mask-host,fallback-host}.log,
vulkan-store-reference-host.log and android-vulkan-store-index.log. Actual game validation
also performs differential checks; evidence is in android-vulkan-store-index-verification.json.
No controlled frame-rate improvement is claimed.

## First-draw full-coverage attachment load elision

`nvn_vk_cover.inc` follows original `attr_read/first_draw_covers`, directly including
unchanged `nvn_cover.h`. Only the first actual draw of a physical pass is considered:
position must be proven to come from one float2/3/4 attribute, with original component
defaults, first, 16/32-bit indices and baseVertex. GPU-updated data waits for real completion
and writeback. Reads precede per-draw descriptor/staging allocations so submission splits
cannot leave stale objects.

Check actual mip/layer extent, viewport/scissor/culling/clipping, blending/channel masks,
a2c, logic operation, depth/stencil rejection, discard/sample mask and reads aliasing target
storage. Unknowns retain LOAD. Only proven color loads become `DONT_CARE`; explicit CLEAR
survives, and MRT attachments are evaluated separately. Existing secondary recording permits
the final compatible render-pass description before beginning the primary pass. Shader
instructions are unchanged.

Offline V2 metadata includes conservative original `dmsh_build.raster_facts`. MSL export is
solely input to that original extractor; Metal is neither compiled nor executed. V1 remains
readable and conservatively retains LOAD. Chain fingerprints cover extractor, Vulkan
producer/ABI and spirv-cross; metadata also has SHA-256. Cache `vk-3a0ffe903acf` has identical
SPIR-V for all 6931 shaders versus `vk-f4ac07bbf82c`. Only 5 of 2344 vertex shaders have a
position proof; the rest reject elision. This is not an optimization of every fullscreen draw.

`deploy-android-shaders.py` verifies the current chain, upstream geometry/extractor, regenerated
metadata and built-ins, optionally requiring identical baseline SPIR-V. All 20792 files are
hashed on the phone before directory publication, retaining the previous cache. NSOs, saves
and RomFS are outside this transaction. The new cache is deployed through this path.

Original GPU fixtures add 30 conditions × optimization on/off × default/sentinel loads,
120 draw scenarios. Whole-image comparisons and actual omission counts cover MRT, explicit
clears, mips/layers, first/indices, scissor/blend/depth/stencil, discard/sample mask, old metadata,
GPU-updated positions and a non-covering first draw followed by a covering draw.
Linux lavapipe and the synchronization-validated Mali APK pass 480145 cumulative checks;
the 14 original geometry checks pass. External-profile host mode passes 480142 checks,
disabling custom timers but retaining guest NVN queries/events/fences. Counts include pixel
checks, not separate game scenarios.

Original LOADPROBE's two constant/NaN selections are implemented; default and first constant
are fixture-tested. Second constant/NaN and original automatic end-pass remaining-sentinel
counting are not fully accepted. Real-game synchronization validation records at least 1800
elided loads, area 1→2→1, English inventory and original background save, with 39615 indexed
decisions matching the reference scanner and no synchronization errors. Screenshots establish
scene regression, not deterministic double-render pixel equivalence of dynamic game frames.
Game modules/rewrite artifacts are unchanged.

Evidence: `android-vulkan-load-cover-verification.json`, `android-load-cover-shader-deploy.json`,
`vulkan-load-cover-{host,mask-host,fallback-host}.log`, `android-vulkan-external-profile-regression.log`.
See [Mali profiling](../../../docs/MALI_PROFILING.md) for complete present histories/vendor counters.
No controlled FPS gain is claimed for this load elision yet.


## Explicit Vulkan compiler profiles

The original translator's subgroup-width query defaults to 32. Vulkan builds can set
`DMSH_VK_SUBGROUP_SIZE=16|32|64|128` from the measured target properties. The Android Adreno
650 tested here reports 64. `DMSH_VK_BALLOT_HELPER=1` emits a shared DontInline ballot helper
at the original invocation sites, preserving predicates, execution scopes and returned masks.
This avoids a reproduced old Adreno V502 compiler failure: seven ballot operations compile,
eight trigger `No UGPR spill support`. It is an explicit driver compatibility option, disabled
by default, without shader-key or game-state branches. Neither option enables the previously
rejected AST expression optimization.

Both options enter the Vulkan cache fingerprint, manifest and per-shader reflection. Use the
same environment during build and deployment. Rebuild `dmsh_xlate` after its source changes:

```sh
source scripts/dev/env.sh
dotnet build dm/gfx/tools/dmsh_xlate/dmsh_xlate.csproj -c Release --artifacts-path "$PWD/.local/build/dotnet-adreno-ballot"
export XLATE="$PWD/.local/build/dotnet-adreno-ballot/bin/dmsh_xlate/release/dmsh_xlate"
export DMSH_VK_SUBGROUP_SIZE=64 DMSH_VK_BALLOT_HELPER=1
scripts/dev/build-vulkan-shaders.sh
```

Adreno evidence: 8192 actual GPU ballot output words match the CPU reference, including a
ballot in divergent control flow. The complete `vk-d0941f1dfe7c` cache validates 6931 modules
and the original deployment script verifies all 20792 device file hashes. This passes the
startup compute-pipeline failure; the 6 GB tablet then hits Android LOW_MEMORY during the
main menu. Full tablet rendering/FPS acceptance remains pending. Keep this device profile
separate from the older Mali cache; do not reuse a width-64 cache on an unverified GPU.

Occlusion queries now follow original Metal's visibility-slot scope: a contiguous render-pass
span ends at a samples report, reset or pass end. Reports retain immutable prefixes, including
resets inside a pass and cross-submit accumulation. Actual Mali same-process ABBA shows
118.21 → 106.17 ms mean present intervals at 2772×1240 / scale 1.0, without changing game,
shaders or draw content. `--ez query_per_draw_reference true` / `DM_VULKAN_QUERY_SPAN=0`
retain the old per-draw query path for comparisons. See the dated Mali profiling record for
fixture counts, original-game validation, full present histories and limitations.
