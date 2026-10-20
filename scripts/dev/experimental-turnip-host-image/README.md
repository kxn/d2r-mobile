# Experimental coherent host-backed ASTC images

This is a separate driver package based on pinned Mesa26.2.5 plus the frozen
3c871 host-buffer import source. It is not installed or enabled by this build.
The existing cold `TU_DM_HOST_IMPORT=true` and coherent KGSL user-page probe
remain mandatory. System/default driver behavior is unchanged.

The extra image profile accepts HOST_ALLOCATION only, optimal 2D ASTC4x4/6x6
UNORM or sRGB, arrayLayers1/depth1, sample1, 1..16 mips, flags0, exclusive
sharing and initial UNDEFINED. SAMPLED and HOST_TRANSFER are required; optional
TRANSFER_SRC/DST are allowed. No storage/attachments/sparse/mutable/format or
compression override chains. HOST_MAPPED_FOREIGN_MEMORY is rejected;
QUEUE_FAMILY_FOREIGN ownership remains available to the native protocol.
Query, create and bind use the same format/usage gate. Requirements admit only
the existing HOST_VISIBLE|HOST_CACHED|HOST_COHERENT types, rounded to host-page
alignment, with nondedicated pool suballocation supported. Bind validates
logical and GPU-address alignment, allocation bounds, identity and no UBWC/LRZ.
A dedicated allocation is optional and must name an eligible image.
No KGSL ioctl/interface, memory ownership or BO release code is changed.

Rebuild (no device commands):

    source scripts/dev/env.sh
    python /absolute/package/build-turnip-host-image.py \
      --work-dir /new/private/build --ndk "$ANDROID_NDK_HOME" \
      --source-archive /verified/mesa-26.2.5.tar.xz \
      --meson /path/to/meson --build-tools-bin /path/to/host-tools/usr/bin -j2

The builder verifies the entire pinned archive plus complete combined patch,
including symlinks, and rejects changed cache outputs. The manifest includes
source/patch/tree/tool/options/driver hashes. No source/driver from the frozen
3c871 directory is modified.

Producer reuse: the separate project worktree
`.local/worktrees/turnip-native-resource-producer-v1` keeps DMNRES1/DMNTBL2
unchanged. Its explicit `DM_NATIVE_TURNIP_HOST_ASTC=ON` profile selects the actual
MESA_TURNIP driverID; default remains the original PanVK contract. The profile
is included in the producer hash. Existing host-copy APIs create/export opaque
mips and preserve original compressed blocks; no app tiler is added.

`pack-native-texture.py --driver-query native-resource.json` accepts existing
actual producer metadata, comparing the independent queried layout/device/
driver UUIDs and producer source to every baked entry. No UUID is inferred
from ELF BuildID. The complete selected driver file hash is still required.
The outer owned-device controller MUST verify that this exact driver is loaded;
a host packer cannot attest a device's loaded module. No real Turnip query,
resource payload, game pack or imported image has been produced by this work.
Without actual query metadata the old PanVK path remains unchanged; a Turnip
record lacking its expected old PanVK BuildID is rejected, never relabeled.

Required next validation, by root's device scheduling: existing native fixture
producer/import roundtrip, ASTC4/6 UNORM/sRGB all mips and edges, GPU sample
against original compressed block oracle, buffer/image aliases and CPU update
ordering, optional dedicated and offset/memoryType/shape negative cases, VVL
and fence/lifetime. Driver compilation and metadata tests do not establish GPU
correctness, zero-copy performance, resident memory savings or game acceptance.
