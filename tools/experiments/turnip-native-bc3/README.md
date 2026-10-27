# BC3 host-import Turnip candidate

This isolated experiment extends the existing native compressed-image whitelist
to `VK_FORMAT_BC3_UNORM_BLOCK` (137) and `VK_FORMAT_BC3_SRGB_BLOCK` (138).
It changes only `tu_host_image_format_supported()` in `tu_image.cc`.
It does not add an encoder, tiler, Vulkan format feature, ordinary-image policy,
coherency change, UBWC change, or driver identity override.

The frozen baseline is B3 (`b3fce5ab29c6617bd2cf334b3a56c67951de271bd870878c9a3121ff606b1b9f`),
whose build record names source revision `ea5e63f7063fe6511c512992af9dd15d3516f0cf`.
The builder verifies its entire source against the original native-ASTC ledger
plus the four explicitly declared B3 additions/changes. It verifies B3's binary,
builder, setup and cross file, then copies to a new output and checks that the
candidate changes exactly one source file. It rechecks the entire candidate
source after compilation. The review patch must exactly match the built change.
Git may be dirty while preparing this experiment: the actual revision, status,
experiment input hashes and full source ledger are recorded, rather than
pretending that an uncommitted experiment is part of a prior clean revision.

```sh
python3 tools/experiments/turnip-native-bc3/build.py --verify-only
python3 tools/experiments/turnip-native-bc3/build.py \
  --output .local/build/turnip-native-bc3-v1
```

The output directory must not already exist. The builder uses B3's exact Meson
arguments and declared environment, replacing only source/build/cross paths;
the Ninja target is unchanged and concurrency is four. The NDK cross file is
copied unchanged. `build.json`, both source ledgers, patch, setup/compile logs,
and ELF notes identify the result. The normal Mesa build-ID/UUID implementation
is unchanged; real device UUIDs remain unknown until queried on a device.

## Why no additional format implementation is needed here

The frozen Mesa registry defines both BC3 formats as 16-byte 4×4 blocks.
`fd6_format_table.c` already maps DXT5_RGBA and DXT5_SRGBA to hardware DXT5.
`tu_formats.cc` already checks real texture support and exposes host transfer for
supported power-of-two block formats. The existing native image path computes
pitch from `util_format_get_nblocksx()` × `util_format_get_blocksize()` and uses
the existing fully linear FDL path, so BC3 uses that same implementation.

All host-import admission conditions remain: host-import capability and memory
types, sampled + host-transfer usage (optional transfer source/destination only),
2D optimal image, no create flags, a single layer/depth/sample, 1–16 mip levels,
exclusive sharing, undefined initial layout, and only the approved external
image pNext. Existing binding alignment, allocation-size, no-UBWC and no-LRZ
checks remain. Existing `native_astc_linear` variable/comments retain their old
name to keep this format-only patch minimal; the predicate now also admits BC3.

## Validation boundary

A successful build proves neither host-import correctness nor game performance.
Before game use, a device probe must verify format queries, memory requirements,
host-pointer import/binding, per-mip layout, UNORM/sRGB sampling and output against
an ordinary BC3 image. Host visibility and CPU/GPU ownership/barriers must remain
correct. A single-texture probe is the intended next step; full-game resource
production, game deployment, native-table changes and ADB are outside this task.
