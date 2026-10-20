# Experimental PanVK native image producer with transfer uses

This is a separate, **default-OFF**, source candidate. It does not change the
system driver, game binary, NVN renderer, resource pack or allocation policy.
It builds from the same pinned Mesa and original kbase patch stack as the
buffer-only `bdd9df32...` candidate, followed by the declared host-import patch
and `0002-native-astc-host-image.patch`, then the separate declared
`0003-native-image-transfer.patch`. The 6b15 driver/source remain frozen. Do not modify the earlier frozen tree.

Both startup options are required:

```
PANVK_DM_HOST_IMPORT=1
PANVK_DM_NATIVE_IMAGE_IMPORT=1
```

The second option is cached during physical-device setup and only admits images
when the existing kernel/coherency/cache capability gate provides real host
import memory types. It is not proof that the new image feature has passed a
hardware data test. Normal releases must keep it off until accepted.

## Actual image capability and validation

`GetPhysicalDeviceImageFormatProperties2` reports **import-only** HOST_ALLOCATION
for exactly these native formats: ASTC 4x4 and 6x6, UNORM and sRGB. Allowed usage
requires `SAMPLED | HOST_TRANSFER` and allows only the optional
`TRANSFER_SRC` / `TRANSFER_DST` bits; each image still has an exact usage recipe.
Type is optimal 2D, flags zero, sample count
one, array layers one, with the original supported mip count. BC emulation,
HDR ASTC, other footprints, render targets, storage,
mutable, sparse, multiplanar, export and foreign-pointer imports are not added.
The same shared predicate is used by format query, CreateImage, memory requirements
and BindImageMemory. This is not a property-only extension advertisement.

The transfer uses follow the original PanVK image layout selection and command
implementations; no tiling algorithm or copy implementation is added. HOST_TRANSFER
already excludes AFBC. A new driver ELF/layout identity still requires newly baked
payloads and real layout queries: old 6b15 sampling success is not proof for this
binary or its new transfer recipes. Source+NDK readiness is not GPU acceptance.
`HOST_TRANSFER` keeps this specific PanVK image in its existing CPU-compatible
native modifier. It must not be added to general renderer images: it can disable
AFBC there.

CreateImage checks the same recipe, host extension enablement and physical
format/extent/mip/sample limits. Imported-image memory requirements expose the
actual supported host-import types. BindImageMemory validates the recipe again,
HOST handle, memory type, 4096-byte offset alignment and the complete image's
bounds in the existing allocation. Binding uses the original
`plane.base = memory.addr.dev + offset`; it allocates no extra image backing.
Caller lifetime rules still apply: the image and all GPU uses must finish before
freeing the imported allocation or the original mapping.

The host-import AllocateMemory path also now rejects **either** non-null field
of `VkMemoryDedicatedAllocateInfo`, as VUID02806 requires. The prior buffer-only
candidate checked only `dedicated.image`; this is a separate old validation
boundary, not a new image fast path or a claim of existing game failure.

## Platform producer contract (not generic Vulkan raw optimal mapping)

The producer is an explicit, versioned **DM native image producer v1**, outside
the ordinary Vulkan host access model. This is necessary: Vulkan defines normal
host image-memory access for linear images; PREINITIALIZED is not a way to
preserve existing optimal external-image contents.

The producer and consumer must use the exact same driver layout UUID, driver
and device identity, image format, complete create recipe and each physical
plane/mip layout. For this limited PanVK contract, the existing
`GetImageSubresourceLayout2EXT` output additionally defines the physical offset,
row/array/depth pitches of each mip for the eligible optimal host image. Those
fields are computed by the original `pan_image_layout_init`; this is an explicit
PanVK implementation promise, **not portable Vulkan layout data**. The existing
host-copy MEMCPY export fills the exact mip slices at those physical offsets.
There is no custom tiling algorithm or new texture encoder.

The producer owns the populated image backing in **GENERAL** outside Vulkan.
After its writes and the required mapped noncoherent Flush, the consumer takes
ownership from **FOREIGN**, GENERAL to SHADER_READ_ONLY_OPTIMAL. It does not
transition from UNDEFINED and hope the bytes survive. External-sharing rules
allow an imported image's UNDEFINED creation layout to be a placeholder; the
external API contract defines the actual initial layout acquired from it.

`EXT_external_memory_acquire_unmodified` is not asserted on this first acquire.
It only describes data unchanged since a previous release, which does not exist
for the first publication. Subsequent producer edits happen only after an actual
GPU-completed release back to FOREIGN/GENERAL, followed by a new Flush/acquire.
The original shared mapping is MAP_SHARED; no private/COW mapping or protected
page GUP retry is involved.

## Bounded owned fixture

The adjacent `native-image-payload` tool has separate opt-in `--native-bake DIR`
and `--native-import DIR` modes when built with both build options:

```sh
cmake -S scripts/dev/native-image-payload -B .local/build/native-image-import \
  -G Ninja -DDM_BUILD_NATIVE_IMAGE_PAYLOAD=ON \
  -DDM_BUILD_NATIVE_IMPORTED_IMAGE=ON
cmake --build .local/build/native-image-import -j2
```

It accepts only the exact PanVK driver contract. LVP is explicitly unsupported;
its UUID/layout and host-copy implementation cannot stand in for PanVK. The
prior standard API `--bake`/`--restore` modes remain available independently.

`--native-bake` uses the existing standard driver host-copy operations to bake
two owned patterns for four native formats, 13x9 NPOT and four mips each. The
payload is a whole physical image address range with explicit per-mip offsets,
not the old sidecar's compact 64-byte-aligned concatenation of opaque regions.
The file includes source pattern identity, checksums, all recipe fields,
UUIDs, import handle and GENERAL/FOREIGN producer contract.

`--native-import` loads those bytes into one finite MAP_SHARED pool, imports the
whole pool once and binds an image at a non-zero offset. It does **not** allocate
a separate VkImage memory object or call CopyMemoryToImage to restore pixels.
Each image is sampled across every mip. It exercises two producer versions,
release/fence/Flush/acquire, two independent import lifetimes, prefix/suffix
neighbors, unchanged read-only payload and original mapping survival after
Unmap/Free. Header/UUID/usage/extent/size rejection precedes binding. Unsupported
query recipes and misaligned/out-of-range producer bind metadata are checked
without issuing invalid Vulkan commands under validation.

An optional `DM_BUILD_NATIVE_IMAGE_EMBEDDED=ON` object exports
`dm_native_image_fixture_run(mode, directory, get_proc, validation)`. This is for
a **cold dedicated ordinary-app-UID fixture**, using the project's existing
app-local provider loader before calling it. It must never run beside an active
game or another Vulkan context. The direct PanVK ICD cannot compose Android
validation layers; a false validation argument must be reported explicitly.
NDK compilation or host unsupported results do not establish Mali correctness.

## Remaining game/toolchain work

This is not yet an NVN packaged-texture replacement. A real pack needs a
versioned 8-byte opaque layout identifier plus a verified table containing the
full host recipe, UUID and native mip sizes/offsets, and consistent StorageSize,
alignment, GetViewOffset, thumbnail/full mip tables and streaming publications.
No byte-zero Tegra block-height spoofing is allowed.

The controlled owned producer publishes immutable versions; it does not prove
that arbitrary game memory is read-only. Raw CPU writes, linear GPU aliases,
ReadTexels/WriteTexels and render-target ownership remain separate contracts.
Only actual proven static resources may skip their previous dirty/conversion
bookkeeping. File origin or `has_layout` is insufficient. Turnip's native image
import path is a separate future implementation, not covered by this PanVK
candidate. No FPS, bandwidth, temperature or real-game success is claimed.

References:

- [External resource sharing](https://docs.vulkan.org/spec/latest/chapters/resources.html#resources-external-sharing)
- [Image layouts and host access](https://docs.vulkan.org/spec/latest/chapters/resources.html#resources-image-layouts)
- [Host pointer import](https://docs.vulkan.org/refpages/latest/refpages/source/VkImportMemoryHostPointerInfoEXT.html)
- [Acquire unmodified contract](https://docs.vulkan.org/refpages/latest/refpages/source/VkExternalMemoryAcquireUnmodifiedEXT.html)
