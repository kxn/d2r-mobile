# Real ASTC resource to host-native physical payload (experimental)

This adds **toolchain code**, not a game renderer/resource protocol. It is
independently opt-in, reuses the exact 3446abb PanVK native producer contract and
does not change that driver, the original NSO, game logic or default resources.
The earlier 948d702 ordinary-UID test proves finite imported image sampling; it
does not prove this new arbitrary-resource bake path or actual game consumption.

## Inputs and producer

`prepare-native-image-resource.py` selects one logical `.texture` from an existing
ASTC4 or quality-fallback ASTC6 CASC pack. It verifies whole archive hashes using
original pack manifests or the complete composition report and its merged-index
hash. It reads the original entry's declared z/n/BLTE encoding. CASC's existing
reader now has an optional encoding argument; its existing default BLTE path is
unchanged. It does not create a second packer or redo ASTC encoding.

The original descriptor and `.texture` header must agree on 2D/single layer,
depth, dimensions, full mip count, ASTC format and packaged layout. Supported
codes remain 95/81 (ASTC4 UNORM/sRGB) and 99/85 (ASTC6 UNORM/sRGB). The existing
`astc_repack.full_texture` parser accepts an explicit NVN format for this path;
its original BC3 default remains unchanged. All sizes and level offsets come
from `nvn_tex.c`, and all extraction/roundtrip comes from the original
`nvn_deswizzle.c`/`nvn_swizzle`, called through `romfs_art.py`. No decoder,
compressor or vendor tiling algorithm is added. Roundtrip compares useful ASTC
blocks; original unspecified NVN padding need not match.

Output is `resource-source.json` (source logical name/descriptor, hashes,
archives/index/encoding, tool fingerprints and every linear mip hash) and a
bounded fixed little-endian `resource-input.bin`. Its `DMRSRC1` header is defined
once in `resource_format.h`, with up to 16 mips and a maximum whole input size of
16 MiB. It contains no pointers. JSON is bound by SHA-256; the C consumer does not
need a new JSON parser. These files are private build artifacts, never checked
into Git. This one-resource prototype verifies packs once per invocation; a
future large batch must retain a verified archive/cache session, not hash the
whole archive or reread the descriptor cache for each image.

Example using the verified full4 pack (substitute actual local paths):

```sh
python3 scripts/dev/prepare-native-image-resource.py \
  --romfs /path/to/original/Update/RomFS \
  --pack-index /path/to/full4/pack/pack_index.tsv \
  --data-dir /path/to/full4/pack \
  --pack-manifest /path/to/full4/pack/pack.json \
  --logical data/hd/env/texture/act1/town/act1_town_camp/act1_town_camp_firepit01_a_alb.texture \
  --output /new/private/input4
```

For a complete quality-fallback composition, use `--composition-report` and its
`merged-pack-index.tsv` plus `romfs_overlay_astc/data`. Unselected/fallback entries
may still be ASTC4; the tool uses the actual descriptor, not a filename guess.

Build the existing tool with its two options explicitly ON. Two new modes are:

```
native_image_payload --verify-resource-input INPUT_DIRECTORY
native_image_payload --resource-bake INPUT_DIRECTORY
```

The first validates the real binary/header/mips/hashes/declaration without
initializing Vulkan. The second requires the actual opt-in PanVK contract. It
checks real format/extent/mip/import properties before creating the identical
sampled-only `HOST_TRANSFER` image. It uploads each complete linear ASTC mip with
the original Host Image Copy API, exports its opaque bytes with MEMCPY to the
**actual physical mip offset**, and reads back to compact linear ASTC with the
same driver to verify the original compressed blocks exactly.

Each native slice is initialized to opaque zeros via the same driver MEMCPY
path before linear upload. Every useful block is then overwritten by the real
ASTC input. This restricted modifier has no AFBC/CRC plane metadata. This makes
otherwise unspecified tile padding canonical without implementing a swizzle.
The extra zero/copy work is offline baking, not a policy imposed on game frames.

Outputs, only after successful API operations, are `native-resource.bin` (raw
whole-image physical address range, including canonical gaps) and
`native-resource.json`. The latter records payload/input/declaration/source
hashes, producer-source fingerprint, exact layout/device/driver UUIDs, full
VkImage recipe, memory size/alignment, each physical mip offset/size/pitches and
source mip hash, GENERAL/FOREIGN producer publication contract. The CMake source
fingerprint is not a substitute for the outer compiler/tool/ELF/driver build
record. Output files are exclusive-create; a previous or partial bake is not a
cache hit. No dynamic RT/image protocol or game immutability is inferred.

This producer exercises CPU Host Image Copy, **not GPU sampling**. It does not
claim a new ordinary-UID or hardware result merely from NDK compilation. The
frozen Android JNI entry/controller still allow only the old two owned modes;
new resource mode needs an explicitly reviewed cold wrapper before device use.

## Concrete packaged8B integration proposal (not implemented or deployable)

Use all eight bytes as a versioned opaque marker:

```
byte[0..2] = 'D','M','N'
byte[3]    = 1
byte[4..7] = little-endian uint32 entry ID
```

Do not spoof byte0 as a Tegra block height. A startup-selected layout table is
bound to the verified pack manifest plus exact driver/layout/device UUID and
producer ABI. Each entry ID names the source descriptor, full VkImage recipe,
native storage size/alignment, complete mip physical offsets/sizes and payload
hash. A marker is never accepted in the old layout parser. Unsupported UUID or
recipe rejects the native pack before the game loads it; a fallback pack needs
its original byte layout, not reinterpretation under the same marker.

Proposed resource-layer interfaces (names provisional):

- `native_layout_lookup(layout8, desc, selected_profile)` validates the entire
  marker/entry/recipe tuple once during creation. It gives no read-only proof.
- `native_layout_storage(record)` supplies `GetStorageSize`, alignment and
  `GetViewOffset` from the same native record, rather than recomputing Tegra
  offsets. Full file mip tables must include native gaps correctly; the final
  physical image extent, pool offset and imported allocation bounds must agree.
- `native_image_bind(record, pool, offset)` creates the identical image and binds
  the already imported whole-pool memory, preserving FIFO/owner lifetime. It
  allocates no second image backing. This is the existing proven platform
  producer contract, not ordinary raw optimal Vulkan CPU access.
- `native_image_publish(pool, byte_range, CPU_version)` records a **real** CPU
  publication after its defined NVN synchronization, maps changed byte ranges to
  native mip spans and performs required NC Flush before subsequent GPU access.
  It must not hash every draw or assume a write occurred only because a pointer
  was bound. Same-value CPU writes remain writes when authority matters.
- Queue completion/fence records guard CPU reuse, destruction and next producer
  update; the GPU layout/ownership must be released/obtained consistently with
  that publication. Fence time and ownership are not implied by file arrival.

The original source currently preserves pool flags, but
`nvnMemoryPoolFlushMappedRange` is a no-op and QueueFenceSync records `x_fence()`;
QueueFinish drains translation and finishes the backend. These are source facts,
not a complete published NVN cache specification. `nvn_slots.tsv` lists **static
call sites**, not runtime call frequencies. Metal's existing packaged-image
cache also uses dirty generations and replaces textures on streaming changes;
that is an implementation choice, not proof those copies are required by NVN.

Before replacing dirty polling, inspect the actual source path's pool cache
flags and FlushMappedRange/submit/fence lifetime contract. If legitimate CPU
updates are completely published by these interfaces, native resource versions
can use those exact ranges/epochs instead of retaining a second shadow merely
because the file streams. CPU_NO_ACCESS, file origin or a packaged marker alone
does not establish immutability. For noncached NVN memory backed by CPU-cached
host pages, required host cache maintenance still has to be implemented.

Thumbnail data is another explicit boundary: existing thumbnailBase64 describes
compact linear ASTC for only its actual low mip chain. `WriteTexels` has a linear
source contract. Do not replace its bytes with opaque host-native spans and hope
the game copies them identically. Initially retain that small linear upload
through the driver Host Image Copy path, or prove a complete toolchain/runtime
protocol that changes both sides without changing the NVN API meaning. Full
streaming, arbitrary CPU/GPU linear aliases, ReadTexels, WriteTexels, GPU writers,
partial mip updates, pool resize/destroy/ABA and asynchronous in-flight versions
all need their original semantics preserved before a real game pack is enabled.

## Paired real-resource ordinary UID verification

The separate `native-image-resource-app-v1` candidate enables the existing cold
entry for `--resources-bake` and `--resources-import`. Both process modes visit
exactly `resource4` and `resource6`: the frozen 86381c7 declarations for the same
real 256x256 / nine-mip campfire ALB, in ASTC4 sRGB and ASTC6 sRGB. No wider game
resource policy is enabled. Driver 6b15 and the earlier 3446 synthetic helper and
shader remain unchanged.

Baking additionally emits `native-resource-layout.bin`: a fixed 992-byte,
zero-reserved record binding all image parameters/UUIDs/physical mip layouts and
SHA-256 of the raw payload, original source input/declaration and JSON metadata.
The import consumer checks these against the selected driver and newly queried
identical image before importing a finite shared allocation at a nonzero offset.
It takes GENERAL/FOREIGN ownership, with actual NC Flush, using the original
platform producer contract. It never transitions imported pixels from UNDEFINED.

For correctness the consumer creates a separate **test-only** reference image.
The exact original compact ASTC source bytes are uploaded through standard
`vkCmdCopyBufferToImage`; they do not go through native-export restore. Both
images are sampled over every texel in all nine mips with the existing
`sample.comp`. Complete GPU RGBA outputs must match, and the reference must be
varied. No new ASTC decoder, shader, CPU sRGB transform or error guess is used.
Reference/staging/output allocations are oracle overhead and must never be
reported as production memory savings. Native payload immutability, neighbors,
release/fence completion and original mapping survival after Free are checked.

The independent package is `com.d2rmobile.nativeresourcefixture`. The existing
Java/JNI class stays unchanged. The signed package and its six libraries, actual
producer/includes/generated shader inputs and the four private real input files
are fingerprinted. The controller executes only the fixed two fresh processes;
it inherits strict first-PID/crash ownership and full original package/resource
baseline cleanup from bcac/948. Direct ICD validation remains explicitly
unavailable. No device result is implied by source, host helper tests or APK
compilation. A small host test calls the same standard reference-upload/sampling
helpers with the original owned RGBA vectors under LVP/VVL/sync; it validates
that standard path only, not Mali ASTC or native import.


## Explicit native transfer contract experiment

The separate `--resources-copy-bake` / `--resources-copy-import` modes use the
exact recipe `SAMPLED | HOST_TRANSFER | TRANSFER_SRC | TRANSFER_DST` (0x400007).
The original bake/import modes retain 0x400004. A different frozen driver and
new producer fingerprint are required; old native-image proof does not cover
these new usage flags. The dedicated controller binds driver SHA 57b7c6f1…
from source 5709f74 and keeps original game artifacts read-only.

Import first checks every original mip against standard GPU uploads, then
updates one nonzero ASTC block ROI through Buffer-to-Image, another ROI through
Image-to-Image, and copies all mips from the imported image into a standard
image. References use modified valid source ASTC blocks and standard uploads,
without a CPU ASTC decoder. GPU sampling must show actual changed pixels while
all untouched pixels/mips match. Original backing neighbors and file hashes
remain checked. Imported pool bytes are CPU-inspected only after GPU fence,
HOST visibility, FOREIGN release and required noncoherent Invalidate. This
experiment makes no claim about concurrent CPU writes during GPU execution.

`--copy-reference-selftest unused` runs the identical copy/reference algorithm
with ordinary BC3 images under Lavapipe and official VVL/SYNC. It does not
exercise native ASTC import, the private driver, the Android game, performance
or heat. The first selftest exposed an unaligned synthetic header offset; that
failure is retained and fixed by the same 64-byte payload alignment used by
real resource inputs.
