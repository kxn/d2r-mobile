# Native BC3 tiled-layout candidate

Independent source/build only. Baseline driver SHA256 is
`0e53ffa2bbbff0a4f520eb080039a848f0853276ece59caa8df9a029cf2b4024`.
No ADB, deployment, native resource reuse, GPU acceptance or performance claim.

The candidate makes precisely two changes:

1. BC3 UNORM/sRGB no longer enters `native_astc_linear` in `tu_image_init`.
   Existing admitted host images use the original FDL `TILE6_3` path and normal
   small-mip linear fallback. This adds no tiler or encoder. ASTC4/6 retains the
   original whole-linear override and 256-byte minimum pitch.
2. The opaque layout profile salt changes from
   `turnip-v3-native-astc-whole-linear256` to
   `turnip-v4-native-bc3-tiled-astc-linear256`. BC3 physical layout changes, so
   retaining its old layout identity would incorrectly admit incompatible
   opaque payloads. The UUID generation algorithms remain unchanged, including
   driver UUID and pipeline-cache UUID algorithms. Actual UUIDs require a device
   query; they must not be guessed from the ELF or reused from the old driver.

Host import/capability, coherent memory, binding alignment, whitelist, WSI and
UBWC constraints are preserved. Compressed formats already exclude UBWC in the
ordinary driver; tiling and UBWC are separate properties here.

```sh
python3 tools/experiments/turnip-native-bc3-tiled/build.py --verify-only
python3 tools/experiments/turnip-native-bc3-tiled/build.py \
  --output .local/build/turnip-native-bc3-tiled-v1
```

The builder reuses the original BC3 builder's complete frozen B3 source checks,
then proves that the complete 0e53 source is exactly B3 plus the declared BC3
whitelist expansion. It verifies the actual 0e53 binary, ledger, experiment and
tool hashes; copies to a new output; changes only the two declared files; and
rechecks the full candidate tree after compiling. NDK/Meson arguments and
declared environment are preserved, paths replaced, and Ninja concurrency is4.
The original trees and binaries are never overwritten.

Next device acceptance must query the new driver identity, reject old payloads,
and freshly bake/import/sample BC3 UNORM/sRGB with every mip/baseMip view,
including small/odd images and mip fallback boundaries. Existing source producer
9194548d…ace8fa is unchanged and can be used with the new explicit driver/query
identity. Its previously baked linear payloads cannot be used. A successful
build alone proves neither sampling correctness nor a bandwidth/FPS benefit.
