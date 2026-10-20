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
