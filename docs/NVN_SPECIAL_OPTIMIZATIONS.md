# Special rendering optimizations in upstream Metal NVN

Audited against upstream `afad65c895386a96dfb863f2317e41d61222a5a9`, especially
[nvn_mtl.m](../dm/gfx/nvn/nvn_mtl.m). This inventory covers rendering and adjacent
host display policies. Existing gameplay features such as automatic potions and
loot annotations are outside the NVN backend. An implemented path is not a claim
that all scenes or its performance have been accepted.

## Technical assessment: Metal optimizations on Vulkan (2026-10-16)

**Port the goal and proof conditions, then implement them using Vulkan semantics.
Line-by-line Metal correspondence is not a performance acceptance criterion.**
Both Apple GPUs and the current Mali can benefit from fewer external-memory round trips
in tile rendering. API rules for content validity, synchronization, residency and binding
still differ. Recommendations below are architectural assessments, not measured speedups.

### Source and physical-device findings

- Read-only `cmd gpu vkjson` reports **Mali-G710 MC10 / Vulkan 1.1.177**, without
  `VK_EXT/KHR_load_store_op_none`, `VK_QCOM_render_pass_store_ops`,
  `VK_KHR_dynamic_rendering` or `VK_EXT_external_memory_host`. Timeline semaphore
  and descriptor indexing extensions are advertised; individual features must still be
  queried and enabled. Advertisement alone is not usable device functionality.
- The phone still runs the accepted store-index APK. Last 20 sparse records give median
  **2100 draws / 63 passes** per frame, **90.56 ms** completed GPU time
  (78.15–102.62 ms), **21.92 ms** draw encoding and **6.83 ms** pool synchronization.
  These are not aligned full-frame traces or controlled A/B measurements. Waits overlap
  GPU execution and **must not be added** to it.
- `nvn_vk.c:mtl_sub_begin` first calls `mtl_finish`, then recycles staging, query,
  descriptor and command objects. This protects current lifetimes but prevents recording
  the next submission concurrently with previous GPU work. Removing the wait alone is
  incorrect: pool mirrors and GPU-to-CPU effects also require per-submission ownership.
- `nvn_vk_shader.c:vk_shader_bindings` allocates/updates each descriptor set anew.
  Constant-copy caching does **not** mean descriptor sets are cached. `nvn_vk_draw.c`
  creates a render pass/framebuffer per physical pass, potentially another render pass
  for final load/store choices. Pipeline caching does not imply these objects are reused.
- `nvn_vk.c:vk_barrier_at` still has a global all-stage broad-access fallback. Actual
  read dependencies already defer pass breaks; required barriers can be made more precise
  by producer/consumer and subresource, rather than deleted.

Local evidence: `.local/logs/android-metal-vulkan-optimization-audit.json`,
`android-metal-vulkan-audit-current.log`, `android-vulkan-optimization-capabilities.json`.
Older GPU-region logs help locate work, but spans include dependencies/overlap and their
labels name the pass's first fragment shader, not exclusive cost of that game shader.

### Mechanism-by-mechanism decisions

| Upstream mechanism / entrypoint | Vulkan relevance | Implementation, limits and priority |
| --- | --- | --- |
| Initial clears / `first_draw_covers` | **High**: avoid external attachment loads | Use `loadOp=CLEAR/DONT_CARE`. Reuse original static facts and `nvn_cover.h`; rewrite binding/visibility. A fullscreen draw alone is insufficient. New coverage passes host/Mali GPU comparisons and actual area/panel/save regressions; controlled speedup remains unproven. |
| `sdx_decide/sd_finish` same-submit lookahead | **High**: omit stores before a proven overwrite | Use `storeOp=DONT_CARE` only with subresource lifetime proof. Metal can change stores at pass end; current Vulkan secondary recording defers beginning the primary render pass. This is a valid adaptation; advance planning is another option with its own CPU cost. GPU/game evidence exists, controlled speedup does not. |
| `pass_ds_need` attachment demand | **High**: avoid unused attachment traffic | Omit a wholly unused combined attachment, as already implemented. Use appropriate read-only usage/layouts when applicable. Separate plane treatment needs Vulkan format/layout rules and device support, rather than Metal's independent texture slots. |
| Continuation, `hoist_try`, `defer_*` | **Useful if net savings justify analysis** | Dependency planning merges compatible work, folds clears into loads and preserves NVN visibility, queries/events, aliases and ordering. Basic continuation/initial clears exist. Reject unlikely candidates cheaply so CPU analysis does not outweigh GPU savings. |
| `fuse_try/fz_p2_draw` same-pixel tile reads | **High potential, substantial implementation cost** | Vulkan 1.1 candidate: render pass/subpasses/input attachments with region dependencies, plus offline SPIR-V variants only when original same-pixel proofs hold. Arbitrary sampling/filtering/neighborhood reads cannot become input attachments. Verify physical tile merging with counters; one API render pass does not prove it. |
| `udc_on` loaded but unwritten attachments | **Useful goal; original DontCare idiom is unsafe to copy** | Vulkan `STORE_OP_DONT_CARE` may discard previous contents even without draw writes. Omit unused attachments; otherwise retain STORE for future readers, or use `STORE_OP_NONE` with enabled support and proof of no writes. **Current phone lacks that capability.** |
| `xv_*` cross-submit learning | Validity tracking is useful; **history is not a future-use proof** | Upstream learns when to discard and explicitly notes that detecting a violation cannot repair the current frame. Base Vulkan retains STORE unless complete lifetime/future overwrite is proven. Learned discard is not a required default generic capability or compatibility criterion. |
| `g_args_dirty/bind_args`, `g_fx_dedup`, reflection caches | **High**: reduce CPU/driver work | Vulkan descriptor-set caches, dynamic offsets/allocation rings, pipeline/dynamic-state deduplication and render-pass object caches. Keys include resource generations/views/ranges/layouts. Respect pool resets, retirement and GPU-writable resources; do not imitate Metal argument-table memory layouts. |
| Blit barriers/resource usage | **High**: preserve stage overlap without undersynchronizing | Derive stage/access/layout dependencies from NVN reads/writes. Vulkan 1.1's barrier API is sufficient; Vulkan 1.3 is not required. Keep conservative fallbacks. Metal implicit ordering/usage and encoder barriers are not one-to-one API substitutions. |
| Arenas, deferred release, allocator rings | **High**: memory use and CPU/GPU overlap | Preserve fence lifetime principles with per-submission command pools, descriptors, uploads, queries and effects, using fences or enabled timeline semaphores. Current serialization is correct; removing `mtl_finish` needs a complete resource-ownership change. |
| `pool_res`, Metal shared/no-copy | Memory/copy goals transfer; **API mechanisms do not** | Use Vulkan allocation/mapping/coherency and actual external-memory support. Current phone lacks external host import, so arbitrary guest pointers cannot be assumed zero-copy. Continue dirty-page/generation syncing; assess allocation/range improvements separately. |
| Memoryless/transient tile storage | Only for proven pass-local lifetimes | `TRANSIENT_ATTACHMENT` plus suitable `LAZILY_ALLOCATED` memory is conditional. Images sampled, used as storage/copied or retained across later passes/submissions cannot simply be made transient because they are render targets. |
| Extrapolation, freeze, bloom reduction, FXAA bypass, zero-source skip | Original policies can be assessed separately; **not Metal API acceleration** | Isolate existing policies/defaults. Brightening/drag icons are not speedups. Quality reduction, reprojection and frozen scenes cannot conceal ordinary NVN overhead. Remaining ports are listed below. |
| Adaptive scale, 30/60 tiers, presentation | Goals transfer; implementation is display-platform dependent | Retain original controller; use Android Surface/swapchain, refresh hints and actual present timing. Metal display links/present allocators are not copied directly. Target 60 is not measured 60. |

Vulkan reasoning follows Khronos's [attachment guide](https://docs.vulkan.org/samples/latest/samples/performance/render_passes/README.html),
[StoreOp specification](https://docs.vulkan.org/refpages/latest/refpages/source/VkAttachmentStoreOp.html),
[subpass guide](https://docs.vulkan.org/samples/latest/samples/performance/subpasses/README.html),
[barrier guide](https://docs.vulkan.org/samples/latest/samples/performance/pipeline_barriers/README.html) and
[descriptor guide](https://docs.vulkan.org/samples/latest/samples/performance/descriptor_management/README.html).
Priorities and expected device benefits are inferences from this source audit; another
sample's speedup percentage is not evidence for this game.

### Execution and acceptance order

Vendor tools are now installed; see [Mali profiling](MALI_PROFILING.md) for captures and overhead isolation.

1. Use current Android/Mali profiles to select work after checking output correctness. The table
   above is a reference, not a required optimization sequence. First-draw load elision has device
   functional acceptance with shader/toolchain provenance; its net speedup remains unproven.
2. Compare the same scene/scale/settings at similar temperature. Correctness runs use synchronization
   validation/pixel comparisons; performance runs disable them. Record complete present interval
   distributions, CPU encoding, completed GPU work, pass-break reasons and estimated load/store bytes.
   Estimates are not hardware bandwidth measurements.
3. Address observed Vulkan costs: object/descriptor reuse, resource-specific barriers and proven
   pass-traffic elision. Use independently switchable comparisons to attribute results.
4. Then consider clear hoisting/deferred draws and same-pixel subpasses. Concurrent submissions first
   need a complete in-flight resource-ring design covering actual completion effects. Adjust ordering
   based on measured net benefit because these mechanisms interact.
5. Keep correct generic fallbacks when a proof or feature is missing or behavior is only observed on
   Apple drivers. Add no camera/map/game-address exceptions or compensating game-binary patches.

**Metal correspondence means consistent NVN semantics, traceable optimization conditions,
correct Vulkan implementation and measurable net benefit, not enabling every Metal trick.**

## Game or original mobile UI dependent paths

| Path | Upstream behavior | Upstream default | Android Vulkan status |
| --- | --- | --- | --- |
| Runtime extrapolation `rt_*` / `nvn_rtex.inc` | Alternates full and extrapolated frames; reads camera matrices, reprojects historical color/depth, fills holes; recognizes ten skin shader keys and handles characters, UI, translucent effects and regional postprocessing. Camera reads belong to this optional path. | **Off**, opt in with `DM_RT_AB=1`. Upstream records A17 stuttering and character-selection ghosting. The 30 fps tier renders fully. | Separate `nvn_vk_runtime.c` / `nvn_vk_extrap.c` implementation. GPU color/depth/stencil and actual camp, movement, area transitions, panels and background recovery exercised. Broader image comparison, scenes and controlled performance remain open. **Off by default**. |
| Panel world freeze `frz_op` | Original host stops world progression; renders a full world once, then retains it and redraws UI/panels. This is not merely hiding an advancing world. | **Off**, opt in with `DM_PANEL_FREEZE=1`. | `nvn_vk_freeze.c` implemented. Actual panels, sale/buyback, forced redraw on scale changes, background save/recovery exercised. **Off by default**, uses original freeze gates. |
| LUT brightening `bright_lut` | After compute key `552f45c348a61b86`, modifies the 3D LUT using original host region/daylight policy and smooth exposure. UI composites later. Appearance adaptation rather than acceleration. | Dynamic policy; factor near 1 leaves the LUT unchanged. `DM_BRIGHT_K` is a test override. | `nvn_vk_bright.c` calls original `dm_bright_k`. Independent numerical/GPU comparisons and the real camp LUT exercised. |
| Quarter-resolution bloom `bloomq_*` | Recognizes four shader keys (`2938…`, `019c…`, `cc26…`, `2e2d…`); skips B0, combines extraction/downsampling into B1, redirects composition to B1. Changes sampling and bloom output. | **On**, subject to TBDR and chain checks; disable with `DM_NVN_BLOOM_Q=0`. | **Not ported**. The original full game bloom chain still runs. This upstream optimization is not covered. |
| FXAA bypass | Recognizes `ebcfcd1fa0a77dbc`, omits FXAA, redirects subsequent `52aac1f2306b8fc5` sharpening/upscaling to the FXAA input. Trades antialiasing quality for cost. | **Off**, experimental `DM_NVN_NOFXAA=1`. | **Not ported**. Original game FXAA remains. |
| Zero-source outline skip `zskip_*` | Fact table names outline blur shader `4a819818b8e489c5`; proves zero sampled input plus finite coefficient gives zero output, then checks render state before skipping work. It is not a general black-texture heuristic. | **On**, subject to TBDR; disable with `DM_NVN_ZSKIP=0`. | **Not ported**; normal draws still execute. Listed separately because the fact table depends on this game's shader. |
| Drag-follow icon `ghost_note/ghost_flush` | Uses original shelf grid, quad geometry and texture frequency to select an item icon, shifts viewport and replays its draw under the finger. Touch display adaptation rather than acceleration. | Requested by original UI during a drag; idle draws are unaffected. | `nvn_vk_ghost.c` implemented; GPU selection and actual shop dragging/trading exercised. |
| Tablet UI projection offset `ui_mat_patch` | Original iPad layout places the 16:9 UI at the bottom of a full-screen picture; copies and offsets projection constants for identified UI draws, adjusts scissors, handles full-screen dimming separately. This matrix change is specifically UI projection. | Only with a registered non-full-screen UI rectangle; normal iPhone/Mac path unchanged. | **Full path not ported**. Current Android uses fixed 16:9 letterboxing. Registering the drag icon callback does not implement the projection/scissor path. |

## Original host display policies, outside NVN API semantics

| Policy | Original behavior/default | Android status |
| --- | --- | --- |
| Adaptive 3D scale | Mobile starts at 0.75; smoothness priority lowers scale on dropped frames, default floor 0.60. UI/text resolution stays unchanged. Quality priority (`q3d=1`) restores configured scale. | Reuses original `hle_oe.c` controller. Actual camp progression 0.75→0.70→0.65→0.60 observed. Affects 3D sharpness. |
| 30/60 fps tiers | Power saving `eco_fps` defaults off, targeting 60; enabling targets 30, aligned to display cadence. | Original setting/acquisition pacing implemented; actual same-process 60→30→60 exercised. Android display refresh-rate hint remains open. **Target rate is not measured frame rate**. |

## General GPU optimizations and remaining correspondence

| Metal mechanism | Current Vulkan counterpart |
| --- | --- |
| Pipeline/shader caches, repeated state/argument reuse | General pipeline/shader/constant-copy caches exist, invalidated by actual resource write epochs. Metal argument encoders and Vulkan descriptors differ, so implementation is not literal. |
| Same-target pass continuation, initial clears folded into load | Basic continuous pass reuse and initial clear folding exist; dependencies, transfers and queries end passes when necessary. |
| On-demand depth/stencil attachment | Submission-wide command planning now omits combined attachments when provably unused, with GPU on/off comparisons. Individual plane omission remains open when either plane is needed. |
| Same-submit full-overwrite store lookahead | Secondary command buffers defer storeOp selection; color/depth/stencil proofs are independent and preserve contents before sampling/copy/partial clears. GPU on/off comparisons pass. Per-texture event buckets/binary search now include storage aliases and descriptor-epoch fallback; validation compares each result with the scanner. Original freeze/extrapolation paths that may filter commands still store. |
| Additional load elision, clear hoisting, deferred draws, same-pixel tile fusion | **Not fully ported**. New first-draw coverage passes host/Mali GPU and actual-game regressions. Working rendering does not establish equal bandwidth/performance. Upstream narrow `DM_NVN_RT_RANGE` continuation is off by default. |
| Unwritten-attachment stores and cross-submit XV ledger | **Reassess using Vulkan semantics**, rather than list as mandatory literal ports. Current phone lacks STORE_OP_NONE; historical learning cannot prove future non-use. See the technical assessment above. |
| Dirty pages/resource generations, in-flight retirement, upload/staging reuse | Vulkan counterparts exist; validated around real completion fences and visibility without character/camera/map rules. |

`DM_NVN_SHMAP` selects the original offline shader key→stage mapping table; it is
**not a replacement-game-shader optimization switch**. BC decoding and mobile
texture packs/ASTC belong to asset tooling/format support rather than gameplay.

2026-10-17 upstream compiler audit: `XLATE_REDUCED=1` is already enabled. Rebuilding
an original hotspot with the option on/off yields 9/1377 `NoContraction` decorations;
the enabled result matches the previously deployed SPIR-V byte for byte. This relaxes
rounding/contraction requirements, rather than globally converting FP32 to FP16.
No separate general register-spill-elimination option was found in pinned upstream
or latest inspected upstream `21cf97b91823111ead237858ea79929afacf71a5`.

Upstream's optional `XLATE_DEBUG=0` expression propagation is now restricted to
stable pure expressions, preventing reads of mutable/address-taken locals from
moving past mutations. Regression tests reproduce the former black-character bug
and pass with the fix. Replacing all captured shader stages produces identical RGB;
official replay L2 load/store reads per frame decrease 7.03%, while GPU cycles per
frame increase 1.23%. The full 6931-shader safe cache is deployed. The latest actual
native game measurement is 14.075 FPS at 2772×1240 and fixed 3D scale 1.0, not 30 FPS.
Different clock/thermal conditions prohibit attributing the difference from the
old 7 FPS run entirely to code changes.

The new C1 folding experiment is not an upstream optimization. It remains available
but defaults off because official B-A-B-A counters show increased GPU cycles/frame
and L2 load/store traffic, not merely because FPS failed to rise. The original
BC3→ASTC 4×4 Legacy Device Pack tool has converted and deployed all 27215 BC3 textures
without changing resolution/mips. The original camp renders normally. Android's
Graphics memory snapshots decrease from 4351464 to 2715216 KiB; these are category
totals rather than allocation-by-format attribution. Official LWI measures 16.96 FPS
after deployment, still below 30. DVFS/thermal history differs from the preceding
14.075 FPS run, so the FPS difference is not an isolated texture optimization gain.

## Diagnostics, not product optimizations

`DM_NVN_SKIP_FS` / `DM_NVN_SKIP_SMALL` deliberately drop draws. Highlighting,
visual rulers, pass dumps, same-process A/B and GPU timing support diagnosis and
comparison; incomplete pictures cannot establish compatibility or speedup.
Android GPU profiling, synchronization validation and original fixtures are
also validation tools.

The mainline remains original NVN resource, shader, fixed-state, draw, transfer,
synchronization and presentation semantics. Generic fixes add no game shader
whitelists, camera guesses or patches to processed game binaries.

2026-10-17: The dependency-driven upload candidate makes ghost_note's CPU proof
read original CPU bytes through vk_pool_try_read, retaining the nonblocking
pending-GPU-write guard. Grid/frequency selection and replay behavior are unchanged.
The instance-divisor fallback also reads CPU bytes after its original GPU completion
boundary. These remove reliance on whole-pool mirror uploads; no new game decisions
are introduced. Host/device fixtures cover forced divisor fallback and ghost pixels.
