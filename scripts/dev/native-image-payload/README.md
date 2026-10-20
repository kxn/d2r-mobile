# Experimental native image payload roundtrip (default OFF)

This is a bounded **owned-resource API experiment**, not a game-resource loader.
It does not change the NVN renderer, shaders used by the game, or resource codec.
No game modules or game files are loaded. It records no timings or GPU counters.

The experiment uses the existing Volk loader and `build_builtin.py`/glslang/
SPIR-V validator. Vulkan itself creates its native image layout:

1. `--bake`: normal `vkCopyMemoryToImageEXT` uploads an owned linear pattern.
2. `vkGetImageSubresourceLayout2EXT` with `VkSubresourceHostMemcpySizeEXT` gives
   each opaque region's required size. `vkCopyImageToMemoryEXT` with
   `VK_HOST_IMAGE_COPY_MEMCPY_EXT` exports it without application swizzle code.
3. A **separate process** restores a matching image through
   `vkCopyMemoryToImageEXT` with `VK_HOST_IMAGE_COPY_MEMCPY_EXT`.
4. The CPU payload mapping is unmapped **before** GPU sampling; the image then
   transitions to `SHADER_READ_ONLY_OPTIMAL`. The existing
   glslang-compiled compute fixture samples every texel of every mip and checks
   the original pattern plus output guards. It never measures performance.

A host copy still copies data. This is **not zero-copy** or external image
memory import. The declared opaque region byte counts are API buffer sizes,
not measured bytes transferred by the implementation or by DRAM.

## Build and run

From the repository root, using separate build/output directories:

```sh
cmake -S scripts/dev/native-image-payload -B .local/build/native-payload-host \
  -G Ninja -DDM_BUILD_NATIVE_IMAGE_PAYLOAD=ON
cmake --build .local/build/native-payload-host -j2
mkdir -p .local/build/native-payload-owned
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json LP_NUM_THREADS=2 \
  .local/build/native-payload-host/native_image_payload \
  --bake .local/build/native-payload-owned
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json LP_NUM_THREADS=2 \
  .local/build/native-payload-host/native_image_payload \
  --restore .local/build/native-payload-owned
```

Khronos validation and synchronization validation are enabled by default;
missing validation is an error. The explicit `DM_NATIVE_NO_VALIDATION=1` escape
is only for later environments that cannot load the layer; such a run must not
be reported as validated. Missing device extension/feature returns **77** and
`unsupported`; unsupported individual formats are reported, never substituted.

An ARM64 executable also builds with the original Android NDK toolchain:

```sh
cmake -S scripts/dev/native-image-payload -B .local/build/native-payload-android \
  -G Ninja -DDM_BUILD_NATIVE_IMAGE_PAYLOAD=ON \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29
cmake --build .local/build/native-payload-android -j2
```

The CLI uses the system loader. NDK compilation does **not** prove ordinary
Android app-UID access or app-local PanVK/Turnip loading. A later dedicated
app-UID fixture must reuse the project's existing provider loader and request
`hostImageCopy` plus its dependencies before any image operation. The production
renderer currently does not enable this feature/extension; this tool changes
none of its extension negotiation.

## Scope and rejection

Only one-layer optimal 2D, 13x9 NPOT, four mips, sample count one, no image flags,
exclusive sharing, and `SAMPLED | HOST_TRANSFER` usage are accepted. Owned RGBA8,
BC3, ASTC 4x4 and ASTC 6x6 vectors are attempted if the device advertises the
necessary features and exact format/usage support. Synthetic BC3/ASTC constant
blocks are test vectors, not a new encoder. Depth/stencil, arrays, mutable
formats, storage/attachment use, sparse, external memory and arbitrary recipes
are unsupported. The initial two-layer experiment failed the installed VVL's
MEMCPY extent check and was narrowed, not suppressed.

The bounded little-endian file includes all fields of this exact image recipe,
format, extents, mip/layer records, device and driver identity, the driver's
`optimalTilingLayoutUUID`, full-file SHA-256 and an **owned-fixture-only** immutable
marker. Truncation, checksum, layout UUID, immutable marker and recipe mismatch
are rejected before image upload; each target opaque size is re-queried. The
marker is not a claim of immutability for arbitrary game memory. No encoded game
assets or native payload binaries belong in Git.

The local LVP 25.0.7 run validates requests, metadata and restored GPU sampling
for RGBA8/BC3; ASTC is explicitly unsupported. It does **not** prove Mali/Adreno
native payloads, nor the absence of implementation tiling work. The separately inspected
Mesa 26.2.5 LVP source (not byte-identical installed-driver provenance) does not distinguish the MEMCPY flag in these copy entrypoints,
and queries whole-plane size per mip. Consequently file padding/API region
sizes can exceed the image allocation and cannot be treated as real copy cost.
PanVK and Turnip have explicit native MEMCPY branches; those need their own
ordinary app-UID byte tests before use.

## Applying this to static game resources

Prefer already-native compressed sampled-only ASTC. In the frozen PanVK source,
`HOST_TRANSFER` prevents AFBC; adding it globally to render targets or ordinary
uncompressed images can lose compression. Compare exact format usage with
`VkHostImageCopyDevicePerformanceQueryEXT`; do not infer a net gain from flags.
PanVK's custom BC-decode shadow path also needs explicit host-copy validation.

A **sidecar** can bind a tool-verified resource hash, original NVN description,
thumbnail/full mip identity, exact host recipe and layout UUID to an opaque
payload. Its cold lookup can replace deswizzle and driver tiling without a
per-draw whole-texture hash. It adds disk bytes; mmap can be transient as above.
It does not by itself authorize skipping dirty/version checks, release the
original game pool memory, or avoid the one-time payload-to-image copy.

A **replacement host-native packaged resource** could instead avoid retaining
the original Tegra payload in the resource pack, but requires a declared,
versioned toolchain protocol. The NVN opaque 8-byte layout value can only be a
magic/version plus verified table handle; it cannot hold a UUID and full recipe.
The table must define StorageSize, alignment, every mip/layer offset, full and
thumbnail mip tables, streaming publication and CPU texel access. All relevant
HLE size/view-offset/read/write paths must recognize the same protocol. The
current HLE interprets byte zero as Tegra block height and has no implemented
TextureWriteTexels convenience entry; changing only that byte is invalid.

Most importantly, original resources first upload thumbnails and later stream
other mips into game memory. Neither a filename nor `has_layout` proves those
bytes immutable. To remove even dirty tracking, a real producer/consumer
contract must publish immutable, separately owned versions, define legal later
writes/replacement and preserve CPU-read/GPU-write aliases. This experiment
establishes no such game contract. Read-only static payloads must not silently
convert dynamic render targets or streaming updates into stale data.

Primary references:

- [Khronos proposal](https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_host_image_copy.html)
- [Host image copy API](https://docs.vulkan.org/spec/latest/chapters/copies.html)
- [Khronos preswizzled data / layout UUID explanation](https://www.khronos.org/blog/copying-images-on-the-host-in-vulkan)
- [Host image properties](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceHostImageCopyPropertiesEXT.html)

## Separate direct-import experiment

`DM_BUILD_NATIVE_IMPORTED_IMAGE=ON` adds the explicit PanVK-native producer
contract modes `--native-bake` and `--native-import`. They are outside this
portable host-copy experiment and accept only their opt-in PanVK driver. See
[the driver contract](../experimental-panvk-native-image/README.md) for external
ownership, full image offsets, noncoherent Flush, lifetime and remaining game
resource requirements. Neither this option nor the driver option is on by
default. LVP returning unsupported is not a hardware acceptance.

`DM_BUILD_NATIVE_IMAGE_EMBEDDED=ON` also builds a cold-app fixture object with
`dm_native_image_fixture_run`. A dedicated fresh process must pass the existing
provider's `vkGetInstanceProcAddr`; it must report whether validation is available
and must not initialize alongside a running game Vulkan context. Each mode is a
separate cold process, preserving files only within the owned fixture directory.
