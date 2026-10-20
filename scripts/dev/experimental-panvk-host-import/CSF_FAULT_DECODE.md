# Bounded CSF fault decoding over the frozen bdd9 driver

This patch is relative to the complete `07ec8d8`/`bdd9df32…` Mesa overlay,
not plain upstream Mesa `5a07217f`. It changes diagnostics only. Apply
`0002-csf-fault-decode.patch` to that verified source closure in a separate
Mesa tree. Do not edit the old driver/source or reuse a new driver's hash for it.

The firmware ring insert/extract counters are monotonic; the physical ring is
**64 KiB**. Captured extract `0x1074e8` needs physical offset `0x74e8`, not
`0x1074e8`. The existing fault decoder used the latter, then its release-build
assert-only memory fetch dereferenced a missing mapping. This is a diagnostic
crash after the real group timeout; fixing it does not fix the GPU timeout.

Both existing fault/timeout decoding entries now validate at most one retained
ring, split the diagnostic range at physical wrap, and log physical addresses.
The CS binary decoder rejects absent, null, truncated or unaligned CS mappings,
keeps valid callers when a CALL target is unavailable, and flushes the existing
dump stream. No actual submit/ring emission, cache operation, synchronization,
shader compiler, resource policy, driver option or GPU workload is changed.
The small geometry helper is used only by diagnostic branches.

The host test links the actual existing Mesa CS builder/binary decoder and util
libraries. It checks both captured cursor cases, previous-job cursor recovery,
physical wrap, overflow/stale/alignment bounds, mapped CS disassembly and five
unavailable mapping cases including a CALL target. The original unmodified
release decoder is a negative control: the same owned mapping case must fail
with SIGSEGV. It is not an ARM64 CPU runtime or GPU command interpreter; only
`pandecode_cs_binary_v10` static disassembly is called. Captured CS instruction
bytes were not saved by the old broken decoder, so they are not reconstructed.

Reproduce the host probe after a normal Meson release host decoder build with
`-Dtools=panfrost -Dmesa-clc=system -Dprecomp-compiler=system` and
`-Dc_args='-ffunction-sections -fdata-sections'` (and matching cpp_args):

```sh
python3 scripts/dev/experimental-panvk-host-import/run-csf-fault-decode-test.py \
  --host-build /absolute/private/host-build \
  --output /absolute/private/host-test \
  --control-decoder /absolute/frozen-bdd9/mesa/src/panfrost/genxml/decode_csf.c
```

Use the original `scripts/build-android.sh` from the verified original PanVK
builder, profile `g615-v11-csf`, API29, NDK28.2.13676358, original host codegen
binaries and `ninja -j2`, with separate `MESA`, `BDIR`, `DDIR`. No copied old
build.ninja or old-source mutation is needed. The source-ready report records
all actual compiler inputs, generated dependencies, options and ELF hashes.

The next device diagnostic remains parent-owned, same 277 APK/raw NSO and
resources, `startup,kbase_diag` with file logging and an absolute private
PANDECODE_DUMP_FILE basename. Keep `sync`, `cs`, `dump`, `trace` absent for the
next comparison: `cs` changes register poisoning/allocation checks; dump scans
all mappings; trace and sync serialize submissions. The existing CSF error
entry performs static decoding only after failure. It may still lack caller
register values, earlier command streams, shaders or mappings, and extract is
firmware fetch progress, not the exact hung shader instruction. A timeout on
subqueue1 means fragment was the waiter; the no-SYNC capture first observed
subqueue0 (vertex/tiler), so neither identifies a fixed faulting stage.
