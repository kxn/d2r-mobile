# Finite native ASTC fully-linear GPU acceptance package

Source/NDK preparation only. No ADB or GPU has been used by its author. Do not
install a production package. No game modules or game assets are packaged here.
This is a correctness oracle, not a profiler or an FPS/temperature experiment.

The exact candidate is f830285fe68f0a744792269da5f160ed8636c172b1f6511db9d99478e1a2acce.
Its expected **actual queried** layout UUID is ab91c540847448360a44496b50045159;
receiving the prediction in metadata is insufficient. The fixture authenticates
the staged ELF bytes and requires physical Turnip/ASTC and this queried UUID.
The root controller additionally proves the live first-PID driver map's exact
private alias device/inode/bytes. It never falls back to a system/CPU renderer.

## First: finite producer/API matrix

An independent thin `com.d2rmobile.nativeastclinearfixture` app contains its own
Activity, the original producer oracle, and five byte-exact existing AA loader
hooks. It does not contain dmhost, the game's MainActivity, a game binary or
original app assets. The existing ASTC U16 void-extent vectors and compute shader
are reused; there is no added tiler, codec, shader or renderer.

Eight fixed original-producer compilation profiles:

| Width × height | Full levels |
| --- | --- |
| 1 × 1 | 1 |
| 5 × 7 | 3 |
| 63 × 65 | 7 |
| 64 × 64 | 7 |
| 95 × 97 | 7 |
| 128 × 128 | 8 |
| 128 × 256 | 9 |
| 256 × 256 | 9 |

Each exercises ASTC4/6 UNORM+sRGB, both different original pixel patterns and two
independent import/map/unmap/free lifecycles. The producer still uses Mesa host
copy to generate opaque bytes, includes original opaque digests and guard bytes,
and additionally checks ordinary host-copy compressed blocks for every mip.

The original compute sampler's **every pixel and unused-output guard loops are
unchanged**. BaseMip0 samples all original levels. Every later baseMip view then
samples all remaining levels with rebased LOD, including its LOD0. Sampler uses
the original NEAREST filtering; linear/trilinear filtering is not established
by this matrix and must not be claimed.

For each imported version/lifecycle the enforced sequence is: sample the
unaltered imported payload first; perform all-mip GPU Buffer→Image→Buffer and
compare against the original compressed vectors; sample every mip/baseMip again.
This prevents a GPU upload from repairing a bad imported layout before its first
sample. Symmetric B2T/T2B alone cannot pass the independent GPU pixel oracle.
Original whole opaque hash and surrounding memory checks still run afterward.
Existing malformed metadata, handle/type/usage and binding negatives remain.
The exact old AA opaque UUID is additionally rejected; **this is not yet proof
that the production NVN table loader rejected an old table**.

`run-owned.py` defaults offline. Its single cold launch has a 180-second bound,
no retry, strict first-PID ownership, exact installed APK, inode/driver mapping,
ordinary UID and per-shape identity/coverage checks. The original game must be
stopped and match a current read-only baseline. It installs only the independent
package, touches no original directories/settings, stops before pulling bounded
opaque evidence, uninstalls exactly its own APK, cleans its owned temporary files
and rechecks the original baseline. Installer UI is scoped to its exact label
and one live install client. External GPU layers are not enabled or disabled.
API VVL is explicitly disabled; zero error counters do not establish VUID/SYNC
coverage. All success fields refer to actual GPU assertions, never host results.

## Then: original resource and real NVN companion

After an actual clean full-matrix receipt, the **same new independent app** has a
separate `resource-bake` mode. It invokes the original `resource_bake()` body for
only the original hash-pinned 256²/9-mip firepit input. It produces the unchanged
DMNRES1/992-byte recipe through the real driver, retains all three queried UUIDs,
producer fingerprint and original compressed-block roundtrip. This is a producer
result; it does not prove NVN sampling or gameplay. No 6217 batch is baked.

The companion reuses only the prior **thin owned** APK:
`com.d2rmobile.nativewindowfamilyfixture`, SHA
0c5fbf472fcb3b597100de0c02cddebb3ee679d883ce63d23354b2fd62ed459f.
Its six SOs, shaders and original 0a54ef2 runtime are unchanged. The complete
canonical/production APK 19b70… is explicitly refused by the companion controller.

`prepare-NVN-companion.py --mode old-AA-negative` packages exact old AA resources
with this thin app and the new target-driver identity. The original runtime must
reject that real table via its existing identity/producer/pack/recipe guard.
Its expected failure is separate from GPU failure; missing first-PID ownership
is inconclusive and does not authorize an automatic retry.

`--mode new-positive --producer-receipt ACTUAL_RECEIPT` refuses absent/unclean
actual output. It passes the complete original source texture/declaration, new
opaque bytes, wire and actual query through the **unchanged strict family
packer**. No offsets or opaque bytes are invented from the CPU size estimate.
The table binds the new ELF, all queried UUIDs and this new producer's source
fingerprint. Fresh firepit should query 94208 bytes; this must be observed.

`run-native-astc-linear-NVN-companion-v1.py` retains original strict 447 checks,
9 mip full/native samples, 5 original-NVN tail levels, original previous-writer
40 checks, image-only 36 checks, late real Buffer copy, window unions and final
native-lane collection. Only driver/resource identity and exact observed payload
size are parameterized. No game state, binary or production renderer is edited.
The prior complete matrix receipt is mandatory before execution. Original NVN
positive proof remains pending until actual new resource bake and table exist.

## Root execution order and limits

1. Review the frozen source/compiled-inputs/ELF and exact owned controller.
   Refresh root's read-only Q baseline after its current control experiment.
2. One cold full matrix launch using `run-owned.py --mode matrix --shape -1`.
   Stop on any real query, byte, pixel, map, timeout or cleanup failure.
3. One cold `resource-bake --shape 7` using the exact original two input files
   and the successful matrix receipt. Stop/pull/hash via the same controller.
4. Prepare the old-table negative and the newly baked positive resource bundles.
   Run each separately in the unchanged thin NVN app, through original guards;
   each owns its first PID and one cold launch. Preserve raw failures.
5. Only after those correctness gates, consider a matched official profile.
   Linear/trilinear filtering and gameplay/bulk/thermal acceptance are still
   separate gaps. No full bulk rebake or frame-rate claim is authorized here.

The source-ready JSON provides concrete host paths/command arrays. Commands that
consume an actual earlier result stay pending until that real record exists;
they cannot be made ready by manufacturing an acceptance JSON.
