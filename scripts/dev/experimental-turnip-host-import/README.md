# Experimental Turnip coherent host import, public API v2

This independent package adds a Buffer-only implementation of
`VK_EXT_external_memory_host` on the KGSL backend. It does not change NVN,
guest allocation, game data or the system driver. v1 remains frozen separately.
No device execution, installation or performance measurement was performed.

## Activation and capability

By default the feature is disabled. `TU_DM_HOST_IMPORT=true` is read once at
physical-device startup and requests an experimental capability test. The
option alone never advertises support: the driver must also support ordinary
cached-coherent memory and successfully import one driver-owned page-aligned
RW anonymous page through the exact same KGSL import/INFO validator used by
real allocations, then release its kernel ID and original mapping. Failure
keeps the extension absent. This probe is ordinary driver discovery, not a
guest-memory or fixed-address-space diagnostic; this package has not run it.

The shared import validator requires final flags to retain writeback caching
and IO coherence, exact size, writable nonsecure allocation, nonwrapping valid
GPU IOVA and no CPU/GPU-address-equality requirement. The installed Vivo kernel
remains unverified until a normal authorized Vulkan test actually runs. The
probe checks the kernel mapping contract, not an actual GPU round trip; real
CPU/GPU data tests are still necessary before normal enablement.

## Public contract

- Advertise EXT only after the real capability test. Report the OS page size
  as minImportedHostPointerAlignment. This alignment is read before physical
  properties initialization, independently of the upstream 4096-byte fallback.
- GetMemoryHostPointerPropertiesEXT supports only HOST_ALLOCATION and reports
  actual HOST_VISIBLE|HOST_CACHED|HOST_COHERENT compatible memory types.
- AllocateMemory handles VkImportMemoryHostPointerInfoEXT separately from
  ordinary allocations, checks available heap capacity before pinning pages,
  includes normal atomic heap accounting/import reports and rolls back failures
  without double-free or heap subtraction.
- External Buffer query advertises importability only for HOST_ALLOCATION
  with ordinary create flags. Creation/binding reject unsupported combinations
  and validate physical binding range. Device-buffer requirements retain the
  normal supported type set; callers intersect it with host-pointer properties.
- Foreign mapped memory, imported images, sparse imports, capture/replay and
  export combinations are rejected; they are not silently claimed.
- Imported host pointer is externally owned. Map returns its original alias;
  placed mapping succeeds only if it requests the already existing address.
  No code remaps that VMA. Unmap is a no-op; reserve-unmap fails; FreeMemory
  releases the kernel object/page pins without unmapping/freeing original CPU
  memory. Caller must retain the mapping identity through completion/free.
- Coherent does not imply execution synchronization. Normal fences, memory
  barriers and CPU/GPU ordering remain mandatory.

## Reproducible build

The builder defaults to `.local/build/turnip-host-import-ext-v2` beneath this
project checkout; `--work-dir` can select an existing verified cache.
`--source-archive` can reuse an already downloaded archive; its hash is still
required. Meson and its generator Python dependencies must be available in the
specified build environment.

Input is unchanged Mesa 26.2.5 archive SHA256
794b3d1729075dae7ebd47c7283b7ca0cdb5dec668f7f11cc0a5d82f6138a0a5
plus the declared patch in this package. Full source and symlink targets are
verified against archive-plus-patch on every build, including cache hits.
Cached output must match its last manifest hash. The embedded Mesa Git stamp
is fixed to the patch hash prefix rather than incidental checkout HEAD.

```sh
source scripts/dev/env.sh
python scripts/dev/experimental-turnip-host-import/build-turnip-host-import.py \
  --ndk "$ANDROID_NDK_HOME" \
  --meson /path/to/build-venv/bin/meson \
  --build-tools-bin /path/to/bison-flex-m4/bin \
  -j 2
```

Builder does not install or run the driver. Its manifest records archive, patch,
source tree, builder/cross-file and output hashes, compiler/NDK versions and
Meson flags. The original fixed Mesa tree is never patched.

## Required real-driver acceptance

1. With opt-in absent, EXT remains absent; ordinary driver rendering remains
   available. With opt-in present, record capability result and actual API
   advertisement; failure means unsupported, not a license to bypass it.
2. Allocate a small owned aligned CPU region; import through standard API using
   queried type bits intersected with actual Buffer requirements. CPU pattern
   -> imported Buffer -> normal Buffer copy must retain data. GPU FillBuffer
   -> host visibility barrier -> fence must update original CPU pointer.
3. Exercise mapping-preserving free/unmap, repeated import/free, no-leak error
   cleanup and unsupported query/allocation/binding combinations. Run existing
   validation tools. Do not infer correctness from compilation.
4. Only after acceptance, add bounded actual NVN Buffer windows as a separate
   patch. Import pins/faults all pages in its extent, so full logical pools may
   consume more resident memory. Keep needed texture/shader invalidation and
   all completion/dependency semantics.
5. Compare import off/on under the same private driver with official profilers.
   No FPS, heat, residency or game zero-copy improvement is claimed here.
