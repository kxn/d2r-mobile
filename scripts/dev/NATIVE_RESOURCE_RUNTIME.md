# Host-native packaged resources: default-OFF runtime candidate

This is a generic NVN resource protocol, not a game patch or an immutability
heuristic. It changes no original NSO, shader, codec, gameplay or filename/draw
classification. The renderer base is frozen c075d41. The producer protocol and
real resource extraction come from 86381c7/3324; those fixture programs are not
linked into the runtime. Native resources currently require the explicit PanVK
native-image driver contract; other vendors use their original resource pack.

## One record decides storage, offsets and creation

`pack-native-texture.py` consumes a JSON list of verified real source declaration,
original `.texture`, and native bake directory. It validates the producer's
fixed 992-byte record, SHA-bound original compact ASTC input, declaration, opaque
payload and metadata. It preserves the source file header except layoutData and
rebuilds every full mip-table span from actual physical offsets, including gaps
and final allocation padding. It never re-encodes ASTC or implements swizzling.
The current eight bytes are `DMN`, byte version2, little-endian u32 family ID.
Version1/full-only tables remain supported with their matching version1 marker.
The whole DMN namespace is reserved: unknown versions never reach Tegra parsing.

Each 1040-byte table entry binds an exact full NVN description to the complete
Vulkan recipe and native producer record. Version2 adds one 16-byte family record
per entry: the original packaged layout8, explicit thumbnailMip and a reserved
zero word. The 160-byte header binds the selected
native content, actual driver ELF SHA, and producer-source SHA. Startup also
checks actual driver/layout/device UUIDs. These are additional checks, not a
substitute for the build provenance. Before binding each image, the renderer
queries the real format/import capabilities, memory requirements and every mip
offset, MEMCPY size, row/array/depth pitch, and compares all of them with the
record. UUID or a self-consistent digest alone cannot authorize a layout.

The same table supplies original NVN Builder StorageSize/Alignment, texture
StorageSize, Initialize and GetViewOffset. No NVN object ABI field is added.
TextureCompare keeps marker identity symmetric. A failed/unknown native marker
is rejected; interpreting it under the original byte0 block-height rule is not
a fallback. Default/non-native resources retain their original code path.

With `--base-cache`, the tool reads the complete already-ASTC cache once and
updates only selected entries' layoutData. Every other field, all thumbnail
bytes and every unselected entry remain equal. It emits `replacements.tsv` for
the original `dm/tools/casc_overlay.py pack` entrypoint. It does not implement a
new CASC writer, replace UI files, deploy anything or modify an existing pack.
Missing or failed output never has a complete manifest; an existing/partial
output directory cannot be reused as a successful cache hit. It processes one
texture payload at a time rather than retaining a second full pack in RAM.

Example (paths must refer to the newly verified driver and its own baked data):

```
python3 scripts/dev/pack-native-texture.py --entries INPUTS.json \
  --driver-manifest DRIVER/build.json --base-cache COMPLETE_DESC_CACHE.json \
  --output NEW_OUTPUT
python3 dm/tools/casc_overlay.py pack ORIGINAL_ROMFS NEW_CASC_PACK \
  NEW_OUTPUT/replacements.tsv UNUSED_CHUNK-ARCHIVE
```

The complete overlay/index/TVFS merge must retain the previously verified
baseline and all unselected entries, using the original CASC pipeline. The
native table and manifest belong to the host startup selection, not a new game
binary import. The current tool deliberately labels its output as not yet a
deployable game pack until the runtime/thumbnail gates below pass.

## Runtime binding and publication

An eligible native VkImage binds the original imported whole-Pool
VkDeviceMemory at the actual NVN storage offset. `vk_memory_image` is not called:
there is no second image allocation, Tegra deswizzle, pool shadow, image upload,
per-draw hash or dirty scope for this path. The committed shared-heap provenance
check is still required. It does not admit arbitrary private malloc memory or
unprotect/retry an unsuccessful import.

The original per-submission Pool GPU-access references, stages and noncoherent
cache maintenance remain active. A separate image reference protects the
bound Pool until image destruction. Pool Finalize retains its original ordering:
finish submissions/callbacks, destroy matching images, then free the Pool.
Destruction can occur with the resources mutex held or from completion; the
image reference counter therefore does not recursively take that mutex.

The external producer initially publishes the native backing in GENERAL.
Images acquire from FOREIGN without an UNDEFINED discard. Each actually used
native image is released to FOREIGN/GENERAL at the original command-chunk end;
only the acquired list is walked, not the entire image cache. The release is not
a CPU wait or permission for a CPU/GPU data race. Subsequent CPU writes still
require the original NVN fence/QueueFinish and FlushMappedRange contract.
Every reacquire invalidates previous read proofs, including same-value CPU
writes. No "file origin means read-only" or "Flush means no-op" shortcut exists.

Actual native shader/transfer accesses reuse the existing Pool dependency
engine (`vk_buffer_native_image_read`, `vk_transfer_buffer_dependencies`), because
image-only histories would miss VkBuffer aliases. Native transfer writers
publish actual physical mip ranges through original writer/writeback metadata;
shared completion does the original fence-ordered NC Invalidate, with no CPU
snapshot copying. CPU events/queries retain the original fixed-shared dependency
rules. This path does not attempt to remove necessary producer/consumer barriers.

## Startup-only opt-in

Compile `build-android-apk.py --shared-pools --native-resources`; both are off by
default. An explicit cold Android Intent provides `native_resource_table`
(absolute private path), `native_resource_pack_sha256` and
`native_resource_producer_sha256`. All are selected before the first device or
game thread; later Intents cannot change them. The original nativeStart and
nativeTests signatures are unchanged. A disabled build rejects a native request.

The equivalent host startup environment is:

```
DM_VULKAN_SHARED_POOLS=1
DM_VULKAN_NATIVE_RESOURCE_TABLE=/absolute/native-layouts.bin
DM_VULKAN_NATIVE_PACK_SHA256=<native-pack.json pack_content_sha256>
DM_VULKAN_NATIVE_PRODUCER_SHA256=<verified producer_source_sha256>
```

The process-local PanVK loader must also select the matching driver with both
`PANVK_DM_HOST_IMPORT` and `PANVK_DM_NATIVE_IMAGE_IMPORT` enabled before loading.
The Android cold path sets these with the explicit native/shared request. A
wrong driver/recipe/pack fails before interpreting marked textures; fallback
requires selecting the original byte-layout pack at startup.

## Costs and remaining acceptance gates

Version2 runtime metadata is **160 + 1056 × family_count bytes**, retained for
process lifetime after a validated copy; at the hard 65536-family limit it is
about 66 MiB. Compatibility version1 remains 160 + 1040 × entry_count bytes. Startup temporarily holds the file snapshot plus the installed copy.
Image access state and the original per-submission dependency bookkeeping also
remain. The consumer still pays the game's original file/decompression write
into its shared Pool; "one backing" does not mean zero loading work.

The separate driver01efc12 retains strict public recipes while preserving the
original CreateInfo certificate across PanVK's private meta flags. Its actual
ordinary-UID ASTC4/6 copy validation is separate from this runtime candidate.
TRANSFER can select LINEAR instead of the sampled-only modifier: choose actual
usage per baked recipe, never globally assign both transfer bits as an assumed
performance optimization. Old6b15 or failed5709 payloads are not reusable under
new UUID/recipe/binary fingerprints. No steady-frame bandwidth/FPS claim follows
from these source changes or from host correctness checks.

ThumbnailBase64 remains compact linear ASTC, with its original mip/count table
and bytes. The generic native destination B2T path can upload such linear blocks
into a view of the full native image; it is not memcpy of opaque native bytes.
Version2 accepts two exact Builder descriptions for a family. The full shape
uses the verified opaque native recipe. The one tail shape explicitly declared
by thumbnailMip uses the original packaged layout8 and the original NVN backend.
HLE restores its original pkg_block_h/layout bytes before Initialize, so its
StorageSize/Alignment/GetViewOffset and CPU source decoding retain the original
semantics; the marker itself is never used as a Tegra block height. Both direct
CPU packaged initialization and original linear B2T remain legitimate. No
native-ready flag is assigned to the tail, and its small conventional VkImage
allocation/conversion still exists. This is a declared source encoding, not a
runtime prediction, an assumed static texture, or a full-native-memory claim.

Unknown intermediate shapes/flags/versions are rejected. A nonempty thumbnail
whose thumbnailMip is0 or missing has the same full Builder shape: this protocol
cannot distinguish its linear versus opaque source origin. The tool leaves
such assets in the original pack by refusing to select them (33 entries in the
current full4 cache), rather than pretending they are native. Actual full/tail
game calls and storage/streaming behavior still need real-game acceptance.

The new NVN binding/ownership path still needs its own ordinary-UID GPU oracle
(including native-to-legacy and legacy-to-native copies, CPU stream publication,
same-Pool buffer aliases, destruction and neighbors) before a game pack is
enabled. Host LVP cannot execute this private ASTC/PanVK import contract; its
full regression covers only unaffected original renderer paths. Native raw
image escape, mutable-format/RT/storage recipes and arbitrary legacy/native
layout aliases are not promised by this restricted sampled/transfer protocol.

Native GPU writers lazily acquire a separate dirty scope for each actually
written physical mip, after successful whole-Pool import. Pure sampling never
acquires one. Scopes remain until physical image retirement, after GPU refs,
fence/cache publication and the independent prewrite gate have completed. The
base c075 core has no such gate: noncoherent native GPU writers fail closed
unless the separately reviewed runtime supplies its frozen gate-mode query.
This is a composition requirement, not a claim that the new NC path has run.
The initial writer range is conservatively the full native mip; its larger
protected envelope/callback cost must not be called a precise ROI optimization.

## Follow-up: tracked linear writers and the original NVN oracle

The follow-up physical ownership registry is sorted by host-page envelope with
a prefix maximum end. A known Buffer writer finds all intersecting native mip
pages in O(log N + matches), rather than scanning all native textures. Entries
remain until physical image retirement; cold insertion/removal shifts metadata.
The registry costs 32 bytes per allocated slot (geometric capacity, below twice
live entries after growth), and lazy scope pointers add 128 bytes to each image
in an explicitly compiled native-resource build. No texture bytes are stored.
Page-neighbour overlap intentionally admits the conservative mip scope as well.

Native image reads cannot reuse named VkBuffer barrier visibility. The new
alias-read entry uses original actual producer intervals and a VkMemoryBarrier
with real producer and consumer stages. A writer-generation and per-image
stage proof avoid repeating that work when no writer occurred; the lookup is
for the complete native image, not a partial-mip proof reused for other mips.
Saturation disables proof reuse. All actual named producer changes and history
reset invalidate this key. The original Buffer consumer path is unchanged.
Unknown raw VkImage escape is rejected because its writes cannot be accounted.
This does not certify arbitrary incompatible overlapping image recipes.

`DM_NATIVE_RESOURCE_NVN_FIXTURE=/owned/directory` selects the new original-NVN
fixture (only in the explicit native-resource build). It reads the selected
single-entry startup table plus `native-resource.bin` and `resource-input.bin`,
checks their producer SHA bindings, and creates only a finite owned MAP_SHARED
Pool. The fixture uses original NVN Builder, command recording, queue submits,
ReadTexels and existing sampling shaders. The ordinary reference uses standard
linear ASTC B2T uploads. Cases cover all nine mip block bytes and samples,
first linear-writer scope admission, levels4..8 thumbnail-shaped B2T, legal CPU
native publication after QueueFinish plus Flush, native/ordinary T2T in both
directions, a Buffer alias read, neighbours, and physical retirement. The
thumbnail case is an owned full-Builder contract, not evidence of the game's
unknown Builder sequence. Native GPU execution is NOT covered by host LVP.

For Android, the additional cold `native_resource_fixture_dir` Intent is only
accepted along with `run_vulkan_tests=true` and the explicit table/digest/shared
selection. The original nativeStart/nativeTests signatures remain unchanged;
only the new nativeNativeResources setter gets its own fourth argument. An
oracle request cannot launch game modules. A custom-driver ordinary-UID app
transaction and the separately reviewed prewrite-gate core are still required
before executing this fixture on actual noncoherent hardware.


## Integrated ordinary-UID candidate (14dae base)

The `native-family-shared-integration-v1` branch composes the native runtime and
family-v2 oracle with the real b1ea commit/completion prewrite gate, the original
paired FS write leases, and the current GPU canonical retile implementation.
`vk_pool_shared_gpu_gate_mode` is a required linked API here, not a weak stand-in
for commit wiring. Full DMN resources are excluded at both retile preparation
and eligibility; a declared small tail restores its source layout in the NVN
Texture and retains the original generic conversion/retile behavior.

A cold `native_resource_fixture_dir` Intent selects the original NativeTests
entry before any Surface, game UI or game module starts. It requires explicit
PanVK/shared/async/test=true and game=false. NativeStart and NativeTests retain
their original signatures. Only the new startup table setter additionally
retains a fixture validation boolean. Validation defaults to true; the direct
PanVK ICD ordinary-UID transaction explicitly requests false and records that
fact, because this loading path cannot compose Android loader layers. Host LVP
regressions run with the official VVL and synchronization validation instead;
they do not execute the new private ASTC native path.

`build-android-native-family-fixture.py --inputs DECLARATION.json` packages the
already built signed host under `com.d2rmobile.nativefamilyfixture`, retaining
the original Java/JNI Activity name and all six native payload libraries. It
copies two SHA-bound real ASTC inputs, their already baked native payloads,
family-v2 tables, and the exact existing shader fixture. These are private local
artifacts, never committed game assets. `run-panvk-native-family-fixture.py`
plans by default without calling ADB. Execution additionally requires the
current production APK/ELF/28-module/6-resource/shaderchain bindings and always
checks them before and after. Only two fresh owned processes are allowed, one
per ASTC format, with no retry or unsupported-as-pass fallback. The original
PID ownership/crash preservation and exact-package cleanup remain in place.

The original NVN oracle verifies every full native mip against standard B2T,
all mip sample outputs, first Buffer alias writer/gate admission, native B2T/T2T,
real CPU packaged thumbnail tails and linear B2T into the tail, neighbors,
Finalize and mapping survival. A reference image exists only for this functional
comparison, not in the production memory saving. Passing an isolated fixture
would not prove real-game Builder/streaming acceptance or performance.
