# Matched HDR native compilation screening

This independent ordinary-UID fixture creates Vulkan graphics pipelines and
reads the driver's own Mesa IR3 output. It never records or submits a command
buffer, draws, loads game modules, samples performance, or implements a timer.
It reuses the already qualified UBO compile fixture's C initialization body,
original adrenotools/Volk loader, Android activity and SDK packaging tools.

The existing official `gfxrecon-extract` tool recovered the two current HDR
specialized fragment SPIR-V payloads and their actually paired vertex modules
from an earlier GFXReconstruct capture. All four SHA256 values exactly match
current draw-linked sources. The corresponding official API JSON supplies the
actual descriptor layouts (including dynamic UBOs), 128-byte push-constant
range, vertex formats/strides, B10G11R11+D24S8 render pass, depth/stencil,
blend and dynamic states. The builder emits these fields using a bounded JSON
to C serializer that rejects unknown pointers and extension chains. The
capture is older than the present game run: the two original native hashes
must reproduce the current real-game hashes before candidates are interpreted.

Each fragment family keeps its actual vertex shader, interface, render pass
and Spec0=1. The nine PSOs compare:

- Original `.spec` SPIR-V payload.
- Original `dmsh_texel_ssbo.pair(..., identity)` generator's readonly float4
  representation; only set1/binding2 changes descriptor type.
- Original DPR1 generator's observed mask3; 5c also tests rare mask2.
- The already proved exact scalar-int32 address normalization, without changing
  precision, memory ranges, robustness policy or specialization constants.

The reused original generator preserves base/specialized pairing and declares
RGBA32F/stride16/range contracts. **Successful compilation does not establish
production float4/NaN/alias/range equivalence or correct pixels.** Previous
raw-uint4 GPU tests cannot substitute for those conditions.

Recover the frozen PSO inputs from existing official artifacts, without a new
capture or device access:

```sh
python3 tools/experiments/hdr-representation-compile/prepare.py \
  --capture .local/logs/inline-upstream-rootcause-v1/api-tools/own/official-api-frame/capture.gfxr \
  --official-api .local/logs/inline-upstream-rootcause-v1/api-tools/own/official-api-frame/official-api.jsonl \
  --base-cache .local/build/adreno-shader-upstream-rewrites-v2/vk-29e56b51bce5 \
  --output /absolute/new-PSO-input-directory
```

Build without touching the device (`--inputs-root` can select that new directory;
the audit's already frozen directory is the default):

```sh
python3 tools/experiments/hdr-representation-compile/build-probe.py \
  --driver-build .local/build/turnip-internal-queue-sync-v1 \
  --output /absolute/new-owned-build-directory
python3 -m unittest discover -s tools/experiments/hdr-representation-compile -v
```

This audit's required frozen input/cache/extracted PSO paths are explicit in
the builder. Its output records shader, source, tool command, driver and APK
identities. The current builder uses the existing scoped installer's visible
`D2R Mobile` label. The actually executed first version used `HDR compile-only
fixture`; the existing installer correctly did not acknowledge that unexpected
label. Its one original pending transaction was later confirmed against the
real exact label and resolved without a second install. The executed builder
is preserved byte-for-byte separately; no driver failure or rerun is concealed.

Actual results are under `.local/logs/hdr-representation-compile-v1/`:

- `actual-compile-v2/raw-ir3.log`: Mesa output; nine successful PSOs on exact
  12b8 driver, no draw/submit.
- `native-comparison.json`: original `mesa-pass-analysis/link_draws.py` parser
  output, including every emitted native variant.
- `official-extraction-source-ledger.json`: existing official capture/extractor
  input hashes and matched actual PSOs.
- `FS5c-normalize-proof/` and `FS38-normalize-proof/`: both23 changed integer
  results proved equal for all32-bit bit patterns with Z3, plus official
  SPIRV-Tools validation.
- `actual-compile-v1/`: original installer timeout, preserved as failed.
- `pending-original-transaction-actions.json`, `pending-install-cleanup.json`:
  original pending install resolution and exact cleanup.

The original FS hashes exactly reproduce actual game native
`1fce1866...`/`fa9cb4b7...`, 2096/2124 instructions,19 full registers/10 waves.
Readonly SSBO increases them to2592/2598; dominant DPR mask3 gives2110/2133
without lowering registers/waves. Address normalization gives1993/2038 and
23→7 dynamic LDC instructions, preserving the same23 scalar components as
5 vec4 +1 vec2 +1 scalar. These are compile-screening results, **not FPS or
runtime semantic acceptance**. Extra safe-constlen driver variants are retained
and must not be confused with the native variants matched to actual draws.
