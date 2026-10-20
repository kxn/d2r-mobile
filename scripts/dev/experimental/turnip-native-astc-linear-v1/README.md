# Native HOST ASTC fully-linear experiment

**Independent AA185 driver candidate; not production or GPU accepted.** No game
logic, binary, codec, ASTC block contents, private optimal images or RT changes.
No ADB command is part of this package.

Only an image in the original AA native host-image profile selects this path:
external handle exactly HOST_ALLOCATION; ASTC4/6 UNORM or sRGB; optimal API tiling;
2D, one layer, depth1, sample1, 1..16 mips, flags0; sampled+host-transfer plus
optional transfer usage; no externally provided plane layout/modifier. Existing
host-pointer capability and usable memory-type checks remain mandatory.

`tu_image_init` uses the existing FDL fully-linear **explicit pitch** path with
`align(blockRowBytes,256)`. The FDL layout, descriptor, CPU host-copy and KGSL code
are unchanged from AA185. Original FDL last-mip four-row overfetch padding stays;
original host-external memory requirements round the final allocation to actual
host-pointer alignment (4096 on tested Q). This uses the existing explicit FDL
path internally; the public VkImage remains OPTIMAL and the existing exact
profile/binding checks still apply. It is not an externally supplied DRM layout.

Opaque layout identity is separately salted as
`turnip-v3-native-astc-whole-linear256`. Original AA and abandoned mixed-tail
payload UUIDs must not match. Host extraction of the actual UUID implementation
reconstructs Q's known AA layout UUID and predicts v3 UUID
`ab91c540847448360a44496b50045159`; a future real driver query must confirm it.
Existing producer/runtime wire and exact driver/device/layout identity checks
must be used, not bypassed. Old resources are not rewritten or silently reused.

## Why the mixed-tail candidate is blocked

Keeping a tiled1024 pitch but lowering the linear minimum loses a proven baseMip
contract. For128² ASTC4, original baseMip0 seed1024 gives absolute mip2 pitch256.
The actual baseMip1 descriptor instead has width64, PITCH1024 and PITCHALIGN2;
rebasing the same FDL helper from that descriptor gives next-mip pitch512.
This is a source/CPU inconsistency, not a measured GPU fault. We do not invent an
undocumented iterative GPU clamp to resolve it. The failed experiment and exact
host counterexample are retained here; it must not be deployed.

Entirely linear images have one constant minimum. Existing FDL rebase checks
then agree for pitches **and offsets** at every baseMip. This is necessary source
consistency, still not hardware sampling acceptance.

## Host evidence and expected space

ASan/UBSan: existing fd5/fd6 tests, ASTC4/6 UNORM+sRGB64 shapes (odd/POT/1²),
476 baseMip descriptors,952 CPU compressed-block mip copies,12656 checks.
Nine exact shapes in the actual6217 metadata have307 additional baseMip/submip
pitch+offset checks. Source extraction tests33 policy acceptance/refusal cases,
including ordinary/private/RT/non-ASTC/sparse/mutable/array/MSAA/explicit layouts.
The actual old/new UUID implementations differ in576 finite configurations.
These are CPU tests; neither Vulkan nor a GPU is opened.

Using real FDL results for all nine actual shape families and actual counts,
6217 opaque payloads would total443588608B (423.039MiB), versus original AA
696385536B (664.125MiB):252796928B /241.086MiB /36.30% less packaged allocation.
Examples:256²/9mips135168→94208B;128²/8mips69632→28672B;
64²/7mips36864→12288B. No new6217 payload batch has been baked.
This is not a resident-PSS/FPS/bandwidth/temperature result.

Smaller unused padding/pages can reduce capacity pressure; valid sampled ASTC
blocks and shader work are unchanged. Linear block rows may make2D filtering
less cache-local and increase real DRAM traffic. Lower allocation size therefore
must not be described as lower sampling bandwidth or cooler rendering.

## Required next acceptance, before gameplay or bulk baking

1. Query the exact candidate driver/UUIDs, extension/features, actual alignment
   and memory types in an owned ordinary-UID fixture. Old AA payload/table must
   be refused by the existing identity check. No false bind/rebind flags.
2. Bake a finite representative set through the original producer: ASTC4/6,
   UNORM/sRGB;1², odd boundaries,64/96 threshold,128²/256². Verify every mip's
   host-copy compressed blocks, padding bounds and full opaque roundtrip.
3. Real Adreno735 sample each mip and every baseMip view (including LOD0),
   full-tail sampling, nearest/linear/trilinear, sRGB, ROI copies, buffer↔image,
   host copy and imported-host lifetime/cache operations. Require real sampled
   results, not merely correct query values or CPU copies.
4. After correctness, profile the same scene/fixed.60/assets/quality under the
   **same candidate driver**, with private ASTC control versus new native layout.
   Use existing official GPU counters/bandwidth and CPU tools; matched PSO/cache
   and thermal conditions. Compare working-set PSS separately. Preserve the
   original AA/resources and full CAS rollback. No automatic full6217 rebake.

## Source build

`build.py --work-dir NEW_DIRECTORY` verifies the frozen AA source ledger/driver,
applies only the two production-file patch and builds Android ARM64 with the
same declared AA flags/NDK. At most two jobs. It never installs or runs anything.
It requires the existing pinned AA source bundle and host build tools; it is not
an accepted-device receipt. Host source tests accept `--work-dir` pointing to the
prepared tree. Actual build commands, compiled dependency hashes and full source
ledger accompany the source-ready report in `.local/logs/`.

For host reproduction, copy the two `fd6_native_astc_linear_*.cc` sources into
`src/freedreno/fdl/`, apply `host-tests-meson.patch`, then use the exact recorded
`host-setup-command.json` flags with independent source/build directories. Run
only `fd5_layout`, `fd6_layout`, `fd6_native_astc_linear_test` and
`fd6_native_astc_linear_assess` with ASan/UBSan; this builds CPU FDL targets, not a
host Vulkan/GPU test. Source policy and UUID tests reuse that host build's Mesa
library. Host Meson initially downloaded pinned optional libarchive/libxml2 wrap
sources; the full ledger preserves those extra bytes, and neither host nor NDK
compiler dependency closure references them. Production re-builder uses AA's
`wrap_mode=nofallback` and does not require those optional libraries.

The original AA Android HAL exports `HMI` rather than public `vk_icd*` symbols;
the candidate preserves exactly that export set and original `DT_NEEDED`
(including `libhardware.so`). It must use the already validated AA app loader
and dependency namespace; this package does not change Android loading policy.
