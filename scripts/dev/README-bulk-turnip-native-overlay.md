# Offline bulk native texture composition

`compose-bulk-turnip-native-overlay.py` generalizes the existing one-entry
composer without modifying it or the family-v2/runtime protocol. It performs no
ADB access, runtime build, driver operation, deployment or game binary change.

The ordered original `pack-native-texture.py` specs and `native-pack.json` are
reproduced through `load_entry(spec, N)`. Every entry/table/family marker, payload,
three queried UUIDs, driver SHA and producer fingerprint must match. The root
receipt `REAL_GAME_ONE_NATIVE_TEXTURE_FUNCTIONAL_ACCEPTED_NOT_BULK` anchors the
same driver/protocol and explicitly does not certify the new batch shapes.

The source-pack manifest is the bulk prepare manifest: `schema=1`, `complete`,
`source_context`, ordered `entries` (logical and SHA256 logical ID, source,
texture, input and prepare_report file records), and `selection`. Each original
source declaration must equal the shared source context. The source context pins
`original_build_manifest`, `source_index`, `verified_archives`, `source_cache`,
`tool_inputs`, `NVN_layout_library`, and `full_astc_provenance`.

`full_astc_provenance` pins the existing full4 audit report, complete archive,
index, encoder/tool records, quality TSV and original replacements TSV. Its
optional `historical_tool_files` mapping may relocate only the original
`dm/tools/astc_pack.py` and `dm/tools/casc_overlay.py` bytes, with exactly the old
audit SHA; it cannot substitute another implementation. This accommodates the
old producer sources after later unrelated repository changes. Original reports
remain unchanged. Every selected entry must have its original quality row;
there is no new encoding, quality threshold or claim of new quality acceptance.

The initial selector is Act1 environment plus character textures, single-layer
2D ASTC4, with a distinguishable original thumbnail. All members of pinned
`array_closure.groups[*].members` are excluded. Only selected descriptor
`format`, `layoutData` and necessary matching `thumbnailBase64` may change;
other selected metadata, unselected descriptors, full TVFS path/CFT positions,
original unselected files and all upstream UI are preserved and checked. Original
descriptor filename case is preserved; only CASC lookup names are normalized,
and case-alias duplicates are rejected. Each source EKey and encoded/decoded
length must equal its entry in the pinned full ASTC index.

Composition hashes every distinct source archive once, rechecks immutable file
identity before publishing, and materializes one native texture at a time. It
calls the original CASC `encode_by_espec`/`ekey_for`, reads each result back with
the original reader, and streams the final archive. The original `make_overlay`
receives only the complete cache plus prebuilt texture pointers, avoiding its
whole-batch in-memory replacement accumulation. The final archive is hardlinked
into output and its staging pathname removed: no second batch payload copy is
retained. The existing `prove_tvfs` checks the complete TVFS/root/config delta.

All inputs are explicit:

```
python3 scripts/dev/compose-bulk-turnip-native-overlay.py \
  --raw ORIGINAL_ROMFS --ui-overlay ORIGINAL_UI_OVERLAY --ui-proof UI_PROOF \
  --native-pack PACK --entries ENTRIES --entries-sha256 ENTRIES_SHA \
  --packer ORIGINAL_FAMILY_PACKER --driver-manifest DRIVER_MANIFEST \
  --driver-query ACTUAL_ORIGINAL_PRODUCER_QUERY \
  --source-pack-manifest PREPARED_SOURCE_MANIFEST \
  --source-pack-manifest-sha256 SOURCE_MANIFEST_SHA \
  --acceptance ROOT_ONE_MATERIAL_FUNCTIONAL_RECEIPT \
  --acceptance-sha256 RECEIPT_SHA --artifact-root ORIGINAL_AUDIT_WORKSPACE \
  --out NEW_DIRECTORY
```

Tracked tool source must be frozen. Existing or partial output is rejected;
there is no resume. Only final `composition.json complete=true` is usable as an
offline composition record. Its `deploy_allowed`, `game_acceptance`, and
`new_bulk_shapes_actual_game_accepted` remain false. Output lists exact generated
files; never recursively transfer input/raw RomFS symlinks. A separately reviewed
bulk game experiment must validate Builder shapes, streaming and visuals before
production, and measure RAM/bandwidth/heat independently.

Run `python3 scripts/dev/test_bulk_turnip_native_overlay.py` for owned metadata
negative controls and real original-CASC multi-entry prebuilt roundtrip. These
checks do not execute original game modules or claim actual GPU acceptance.

## Matched standard NVN ASTC4 control

`compose-bulk-turnip-astc-control.py` consumes the same source-pack manifest and
ordered entries before a native bake is available. It writes only those selected
source ASTC4 containers and their original standard NVN descriptors. There is no
DMN2 marker or native table. All unselected files remain the identical original
BC/UI baseline. This avoids comparing the whole27k ASTC pack with6217 native
entries or confusing BC-to-ASTC quality/layout differences with sharing gains.

```
python3 scripts/dev/compose-bulk-turnip-astc-control.py \
  --raw ORIGINAL_ROMFS --ui-overlay ORIGINAL_UI_OVERLAY --ui-proof UI_PROOF \
  --entries ORDERED_ENTRIES --entries-sha256 ENTRIES_SHA \
  --source-pack-manifest PREPARED_SOURCE_MANIFEST \
  --source-pack-manifest-sha256 SOURCE_MANIFEST_SHA \
  --artifact-root ORIGINAL_AUDIT_WORKSPACE --out NEW_CONTROL_DIRECTORY
```

Both manifests expose `source_pack_manifest_sha256` and
`selected_logicals_sha256`; these must match in a later A/B. Both also independently
prove all unselected CFT/cache/UI contents. Initial6217 includes actual Act1
environment foliage and VFX character meshes in addition to ordinary texture and
character paths; declared terrain-array members remain excluded. The shared
source gate accepts both the producer's `selection.formats` spelling and older
one-material control records' `selection.format`, and reads array groups from the
pinned complete composition or its standalone original-declaration proof.

The device experiment must additionally fix the same runtime, loaded driver,
PSO/cache/quality and `.6` 3D scale. Offline composition reports no FPS, resident
RAM, bandwidth or thermal improvement and never marks either output deployable.

## New batch producer identity and actual coverage

The batch wrapper changes the producer source fingerprint. Its8064b800… identity
must not be relabeled as the old34ec895… one-material producer. Native composition
additionally requires these explicit flags:

```
--producer-source-ready NEW_PRODUCER_SOURCE_READY \
--producer-source-ready-sha256 SOURCE_READY_SHA \
--producer-source-sha256 NEW_PRODUCER_SHA \
--actual-batch-receipts ACTUAL_RECEIPT_INDEX \
--actual-batch-receipts-sha256 ACTUAL_INDEX_SHA
```

The old root game receipt anchors the unchanged AA driver, all three UUIDs,
DMNRES1/DMNTBL2 protocol and one validated shape only. The new source-ready gate
checks the actual APK/six ELFs, independently reproduces its CMake seven-source
identity/profile, and checks the embedded producer SHA. The new ordinary-UID
batch outputs must all use that exact identity. `--driver-query` must point to
`native-resource.json` from one of these actual current batch outputs; an old
single-material query is rejected even when its driver UUIDs match.

The receipt index is orchestration metadata:

```json
{
  "schema": 1,
  "complete": true,
  "producer_source_sha256": "NEW_PRODUCER_SHA",
  "source_pack_manifest_sha256": "PREPARE_MANIFEST_SHA",
  "results": [{"path": "/actual/batch/result.json", "sha256": "RESULT_SHA"}]
}
```

It references unmodified original batch-controller results, complete JSON and
per-entry JSONL receipts. Each result must have the exact canonical APK/ELFs,
controller/driver/source fingerprints, one ordinary-UID owned PID, successful
cleanup and unchanged original game. Every selected input/declaration ID and
ordinal, UUID/producer, native payload/metadata/992-byte wire hashes and native
extent must match. All prepared batches and selected entries must be covered
exactly once before native composition can be complete. First128 evidence never
certifies6217. Original packer specs retain original source/texture paths and
point `baked` directly at their actual batch output directory; opaque outputs are
not recopied or relabeled.

Missing, partial or failed actual receipts cannot produce a complete native
composition. A complete bake still is not actual-game bulk adoption, sampling,
visual, bandwidth, RAM or thermal evidence; `deploy_allowed` and new bulk game
acceptance remain false. Standard NVN ASTC4 control is independent of producer
bakes and retains its already frozen source/manifest and exact selected set.

Run `test_native_bulk_producer_gate.py` for owned receipt negative controls.
These synthetic schema tests do not substitute for actual ordinary-UID batches.

The f830 successor separates unchanged compiled 8064 producer provenance
(canonical AA driver record is retained) from the actual current f830 controller
and loaded provider. `compose-native-linear-bulk.py` defaults to a source-only
plan. `--compose` requires the complete, clean single 49-batch/6217-entry final
transaction, actual per-entry metadata/wire/payload hashes, and root-reviewed
original NVN plus fractional-filter fixtures. It calls the unchanged strict
family packer with this batch's actual 8064 query, then this CASC composer.
The current source-ready adapter is a new report; historical AA reports/APKs
are not edited. Original BC/unselected CFTs and upstream UI remain unchanged.
Matched control is the same 6217 ordinary NVN ASTC4 source set. New composition
remains game_acceptance=false/deploy_allowed=false until separate real-game
validation, regardless of fixture or all-batch hostcopy success. No device access.
