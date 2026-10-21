# Explicit static-source native coverage (offline)

The historical `prepare-native-image-batch.py` default remains Act1 environment
and characters, 6217 entries. Its `selected()` and bounded batch wire writer
are unchanged from the existing b111473 producer tools. The two existing offline API adaptations from those tools (CASC entry
encoding and explicit ASTC descriptor format) are included unchanged; their
BLTE/BC3 defaults remain intact. No renderer, game,
driver or producer C code changes are included.

To prepare additions, pass `--logical-manifest FILE --logical-manifest-sha256 SHA`
and `--artifact-root` for the original ASTC artifact records, alongside the
original CASC/index/archive/array/audit arguments. The JSON is schema 1,
`kind: explicit-hash-pinned-nonUI-single-layer-ASTC4-v1`, with sorted unique
`selected_logicals`, and pinned `{path,sha256,bytes}` records `source_receipt`
and `reuse_source_pack`. The former is the descriptor census, independently
checked against the hash-verified CASC cache; the latter is the complete old
6217 preparation manifest. An old entry is rejected as an addition: reuse its
actual bake instead. The selected files must remain non-UI, outside declared
array groups, ASTC4 UNORM/sRGB, 2D/single layer, within the original mip/size/
thumbnail/16MiB limits and original positive ASTC quality records. Full CFT and
original NVN deswizzle/retile proofs are still performed by the original
single-resource preparer. Partial output never gets `complete: true`.

Static file origin confers no read-only runtime authority. Unknown runtime
array use remains a functional qualification boundary. CPU/GPU publication,
mutable resources and lifetime guards remain unchanged.

`plan-native-static-coverage.py` uses the unchanged strict batch validator to
revalidate all old f830 6217 receipts and reuse their existing payload paths.
It verifies the new preparation's actual CASC source bytes again. Without
new actual f830 ordinary-UID bake receipts, its status is
`WAIT_ACTUAL_F830_BAKE_FOR_ADDITIONS`, `actual_bake_complete: false` and
`deploy_allowed: false`. `--require-actual` then fails. An old receipt/index
cannot cover a new source manifest or its different per-entry input hashes.
It never emits a combined native pack or composition. Once new real receipts
exist, the original strict packer and complete CASC/UI membership composition
must still run with exact combined selection coverage; no future receipts or
compositions are manufactured here.

The finite input is 85 new source files / 2,201,088 original NVN payload bytes
(2.099MiB). This is source size, not current resident allocation or expected
RAM/FPS gain. The observed 118.84MiB CPU-rebuildable private allocations cannot
be attributed to these files with existing aggregate state/profile evidence.

Copied historical tool provenance is recorded in the source-ready receipt.
`native_bulk_producer_gate.py`, `native_linear_acceptance.py`, the individual
preparer and producer `resource_format.h` remain byte-identical to their
reviewed source trees. The root acceptance constants are historical receipts,
not permission to deploy additions. No device commands are provided by these
tools. The default is source-only; no APK, GPU bake or NSO build is performed.
