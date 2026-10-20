# Bounded original-resource batch producer

This is offline resource tooling and an isolated ordinary-UID producer. No game
modules, driver changes, image marker/wire changes or performance measurement.
The existing native image contract, exact queried layout/device/driver UUIDs and
original per-mip host-copy roundtrip remain authoritative.

`prepare-native-image-batch.py` uses one verified CASC/cache/index context and the
existing original NVN deswizzle/swizzle library, sequentially. It selects existing
ASTC4 ACT1 environment textures plus characters, excluding known declared terrain
array members and indistinguishable thumbnail tails. This first set has 6,217
entries. The old full ASTC4 inputs are reused without encoding. The original
ASTC audit and historical encoder/CASC source files are pinned separately from
the current preparation tools. No residency or in-game use follows from asset
count or byte totals.

Output `batch-prepare.json` has `source_context`, `selection`, `entries` and
`batches`. Each entry has `id` (SHA256 of normalized logical pathname), `logical`,
`source`, `texture`, `input`, `prepare_report` file records. `entries.json` preserves
the existing packer schema `[{source,texture,baked}]`. Each source declaration
retains the original complete provenance/mip hashes. `source.texture` is the
original processed CFT file with its matching thumbnail, not a new runtime copy.
The future native payload bytes are unknown until actual driver queries.

Batch text is orchestration only: literal `DM_RESOURCE_BATCH_V1\n`, then 1..128
fixed rows of `id64<TAB>input_sha256<TAB>declaration_sha256<LF>`. Only lowercase
hex identifiers are accepted; no caller path component is used. Each device
batch root has `resource-batch.tsv` plus `items/<id>/resource-{input.bin,source.json}`.
No new binary/native marker format is introduced.

Cold `--resource-bake-batch` runs under the original Java/JNI once-selected gate,
initializes Vulkan once, then calls the original bake per entry and frees that
entry's input, native payload, Vulkan image and allocation before the next.
At most 128 small records and one entry's bounded input/output live. Individual
input, original useful blocks, declaration, actual driver UUIDs, size/alignment,
metadata and opaque payload hashes remain checked. The producer-source hash now
includes `resource_batch.inc`; all entries must use this exact producer identity.

Original `native-resource.{bin,json}` and 992-byte DMNRES1 layout records stay
unchanged. Each completed entry is fsynced into `resource-batch-receipts.jsonl`;
`resource-batch-complete.json` is created exclusively only after every entry
succeeds. A CHECK failure terminates the process: partial files/receipts are
failed evidence, not an accepted complete or automatically resumable batch.
No GPU sampling claim is made by host image-copy roundtrip.

`run-turnip-native-resource-batch.py` reuses the existing owned transaction for
one explicit `--batch-index`, an exact producer APK, aa185 driver and prior actual
three-UUID query. It never starts the original game, checks/restores original
package/modules/resources/chain read-only, captures exact owned first-PID maps,
stops the producer before slow file pulls and uninstalls the independent package.
It uses the two exact Android private aliases only after matching stat device,
inode, size and SHA; maps must match those values. There is no automatic retry.
It is not a game native-image adoption or performance controller.

The source-size prefilter does not prove actual optimal image size <=16MiB;
that remains the original producer's per-image query gate. Actual thumbnail,
array/Buffer alias, dynamic write, lifetime and sampling restrictions remain in
the unchanged native-family runtime. Composition must selectively merge only
processed entries into the matching BC/UI4 baseline, preserving all other bytes.
The separate composer reuses the original packer/CASC writer; it must not attach
an entire ASTC cache to unconverted BC resources.
