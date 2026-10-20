# Controller-only bounded native resource transaction v2

The producer APK, Java/JNI, C implementation, 8064 producer fingerprint and
DMRSRC1/DMNRES1/DMNTBL2 remain the exact b111473 build. No rebuild is needed.
Old single-batch controller and source-ready records remain unchanged.

`run-turnip-native-resource-batches-v2.py` takes explicit `--first-batch N` and
`--batch-count N` (1..49). Local plan mode never calls ADB. Execute installs the
one independent ordinary-UID package once, then runs the selected batches in
order. Each batch starts with no package PID; it has a fresh owned private input
directory, renamed to the fixed `files/runtime` path required by the unchanged
APK. No symlink, warm JNI reselection or input reuse is used. Previous PID is
confirmed absent before the next launch; legitimate numeric PID reuse is allowed.

Each original oracle terminal, UID, three UUIDs and exact aa185 driver inode/SHA
are checked. After live maps capture the owned process is force-stopped, the
runtime directory is renamed back to its unique owned phase path, and one
standard Android tar of the known output leaf list is written in the unique shell
temporary directory. One `adb pull` and whole-archive SHA replaces hundreds of
per-file remote requests. Inputs are still per-entry hash checked in one command.

Host extraction uses standard `tarfile`, accepts exactly the known regular leaf
names, rejects links/directories/sparse/PAX/duplicates/unknown or missing entries,
limits header count and each file size before any output is written, and copies
one file at a time. Original per-entry payload/metadata/layout and source/UUID/
producer/size/alignment receipt checks are retained. A batch must be fully pulled
and verified before the next cold launch. No error is swallowed or retried.

Installer helper is an exact source copy of the previously accepted e391-lineage
installer: one pending install, fresh checkbox snapshot before continue, exact
D2R Mobile UI, no verification/service/account changes. The outer transaction has
one final uninstall and temporary cleanup. Original game is never launched or
written; original APK/modules/resources/chain are rechecked at the end. Original
cache/temp are not restored from any historical snapshot.

After successful outer cleanup only, compatible per-batch `batches/NNNN/result.json`
views and `actual-batch-receipt-index.json` are published for the existing composer.
They record the same global cleanup and original-before/after evidence. The
`baked-entries.json` points directly to native outputs; opaque payload is not copied
again. Index complete means the requested range completed: the composer separately
requires coverage of all prepared entries before declaring a full native pack.

This is resource processing, with functional tool timeouts only. It executes no
game, custom timer, sampler or shader interpreter and makes no performance claim.
