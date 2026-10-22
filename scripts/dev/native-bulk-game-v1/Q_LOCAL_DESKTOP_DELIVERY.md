# Q-local verified desktop publication

`publish-qualified-desktop.py` is separate from `run-game.py`. The latter and its
restore-on-exit policy are unchanged. Default is a local plan. There is no new
runtime, rewrite, shader converter, profiler, or installer implementation.

This tool is not currently executable against a qualified new desktop APK: that
APK, its original-NSO rebuild, canonical/root review and real qualification do
not exist yet. Never use synthetic unit-test metadata as delivery approval.

## Inputs

The SHA-pinned outer JSON has exactly:

```
schema: 1
qualified_inputs: {path, sha256, optional bytes}
delivery_receipt: {path, sha256, optional bytes}
output: absolute .local/logs/... directory
execution_ready: false
```

The original runner's `proof()` validates `qualified_inputs` without `--execute`.
That preserves original source/canonical/freshRAW28/provider/assets checks and
historical `production_default_enable=false` / `deploy_allowed=false` records.
A new root-reviewed delivery receipt must independently contain:

```
status: ROOT_REVIEWED_Q_LOCAL_DESKTOP_DELIVERY_CANDIDATE
serial: 10CG570J59001U1
device_fingerprint: exact qualified Android Build.FINGERPRINT
runtime: actual new desktop runtime source commit
canonical, raw, provider, composition, qualified_inputs: exact pinned input rows
controlled_game_qualification: pinned new root qualification with the same runtime
Q_local_publish_allowed: true
global_default_enable: false
historical_asset_receipts_unchanged: true
profile: the DESKTOP_STARTUP_PROFILE.md string values except receipt_sha256
```

The controlled-game qualification must state
`actual_saved_stopped_original_restored=true`. `receipt_sha256` is derived from
the actual new delivery receipt; the receipt does not contain its own/profile
hash. This avoids a recursive digest contract. The name is used to match the
real Java `D2RLaunchProfile` startup message; policies alone are not proof that
the private desktop file was consumed.

Schema 1 accepts the precisely qualified f830/shared/native-full2838/1MiB/lazy
combination, not any driver/model guess. The table relative path is checked
against the actual native directory the publisher stages. The original950
shader tree remains byte-for-byte unchanged. Resolution/scale/FPS/thermal values
are not allowed in the profile; existing HUD settings are not overwritten.

## Original primitives and the one missing primitive

- Original `run-game.proof`, canonical signer/NSO/provider/asset qualification.
- Original `installer.install`: one owned installer client, its Vivo UI handling
  and unresolved-install refusal. There is no second installer implementation.
- Original `Device.identity`, `inventory`, `rename_dir`, hashes and journal.
- Original `android-game-launch --attach`: observation only after an ordinary
  MAIN/LAUNCHER launch; it does not add cold Intent extras.
- Original lifecycle save2 helper, same owned PID and stop rules.

The only new low-level operation is `move_file`, the stopped-app private regular
file counterpart of `rename_dir`: hash-CAS, no symlinks, destination absent, PID
absent, atomic mv. It is needed because the final receipt/profile are files,
not one of the six resource directories. It also retires the precise owned
marker in the app cache after validation, so the published module baseline has
exactly28 artifacts, not an extra transaction owner file.

## Transaction and rollback

1. Require no game process, exact old identity/inventories/fingerprint, no GPU
   layer, unchanged original shader inventory, and unchanged user preferences.
   A pre-existing user PID is never adopted or force-stopped.
2. Stage the existing native phase's matched raw28, driver, overlay/table plus
   owner markers, profile and receipt. Use the original six-directory CAS and
   original installer; preserve the original learned PSO files only.
3. Verify exact installed identity; publish receipt first and profile last via
   file CAS. Do not touch saved-game directories or shader bytes.
4. Twice launch ordinary MAIN/LAUNCHER with no extras, observe the original HUD,
   the actual owned-PID Java profile message and real shared/native cold policy,
   then original save2 and stop. This is functional desktop acceptance, not FPS,
   thermal, whole-game, or all-textures-native coverage.
5. Remove only exact owned markers, record a delivered baseline, retain the pinned original host APK
   archive and private directory/profile rollback backups and leave the new installation.

On failure restore only owned matching outputs, using original CAS and
installer; do not restore the saved-game directory. Unknown edits, replacement
PIDs and an unresolved installer retain backups/lock and report failure rather
than overwrite external state. The original failure/journal remains available.
A partially written transfer is not blindly deleted when its hash differs.

## Offline validation

`test-desktop-publisher.py` uses temporary host directories and the real original
identity/digest/inventory/rename functions; only ADB transport, APK installation
and gameplay are fake. No fake receipt reaches CLI `--execute`. Cases verify the
successful two-round publication, install/second-round rollback, newest-save
preservation, foreign profile/shader writes, unresolved installer, replacement
PID refusal, and strict profile fields. These are tooling tests, not Android
acceptance. The first cache-marker prefix failure is retained, not relabeled.

A real future invocation is:

```
python3 scripts/dev/native-bulk-game-v1/publish-qualified-desktop.py \
  --inputs /absolute/frozen-delivery-inputs.json --inputs-sha256 <actual SHA> \
  --report /absolute/declared-output/result.json
```

Only after source review plus actual new APK/raw/delivery receipts can the same
input be explicitly enabled and run with `--execute`. No such run was performed
when this candidate was authored.
