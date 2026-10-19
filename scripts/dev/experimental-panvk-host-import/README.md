# Experimental PanVK host allocation import

This source package adds buffer-only `VK_EXT_external_memory_host` to the fixed
Android kbase PanVK fork. It is separate from the original native ARM64 game
runtime and does not change game allocation, import guest memory, replace the
system GPU driver, or enable PanVK for gameplay. `PANVK_DM_HOST_IMPORT` defaults
to false and is cached during physical-device setup.

## Current scope

The coherent-only predecessor is frozen as project commit `43d509f`, with a
separate cookie-release correction and build. Its actual PHP110/Mali-G710 test
reported property60=31 and extension absent, including when explicitly enabled.
It never submitted the host-import GPU oracle. The original baseline PanVK ICD
has separately run the original GPU memory/DS-plane fixture successfully; that
result is not inherited by this new extension or driver build.

The noncoherent USER_BUFFER candidate at project commit `07ec8d8` passed the
standard own-page oracle on the PHP110/Mali-G710 under ordinary application
UID10174:57,819 checks, four independent8KiB imports,4096-byte alignment,
noncoherent HOST_CACHED type2(flags0xb) and64-byte atom. The explicit disabled
cold process did not advertise the extension. CPU edits after import, mapped
Flush/Invalidate, GPU Copy/Fill, pending timeline-gated submissions and release
were exercised. No failed page pinning was retried. Android validation layers
were explicitly disabled for this direct-ICD path; host Lavapipe plus official
validation/synchronization layers separately passed57,746 checks using coherent
memory. Neither result establishes behavior with the game's dirty-page
protection or game allocations. The driver SHA-256 was
`bdd9df32c38f7c82fb58d815d6a021e8641a545f9fdb1b401416d6908d265d5d`.

These bounded byte/lifetime results do not establish WSI, game rendering,
performance, temperature, memory-use gains or complete leak coverage. The
original game APK and private files were restored and the app process stopped.
No driver or application is installed by the build wrapper.

## Implementation

The extension reports host allocation buffer import only; foreign pointers,
images, sparse/protected buffers, export and capture replay are unsupported.
The standard host-pointer query, external buffer query, alignment property,
device entrypoint, allocation, map/unmap and release paths agree on their types.

For coherent CSF systems (selected mode0/1), the kernel must grant CPU-cached,
system-coherent USER_BUFFER import. The separate cached/coherent Vulkan type
also requires coherent allocation and opaque-fd import for its ordinary uses.

For noncoherent CSF systems, the existing HOST_VISIBLE|HOST_CACHED memory type
is exposed accurately without HOST_COHERENT. This path requires the existing
Mesa userspace cache-operations route. The compatibility override
`PANVK_KBASE_USER_CACHE_SYNC=0` disables this path instead of sending USER_BUFFER
cache synchronization to an unsupported ioctl. Host import alignment is the
maximum of kernel page size and Mesa's cache granularity. The original PanVK
`nonCoherentAtomSize` remains derived from `util_cache_granularity()`.

Noncoherent import performs one kernel pin/map operation for the allocation:

1. Reserve the import's GPU VA using the kernel mmap-cookie contract.
2. Allocate a temporary CSF_EVENT status page and a private KCPU queue.
3. Enqueue MAP_IMPORT, CQS_SET and FENCE_SIGNAL as separate supported ioctls.
4. Wait for the standard sync_file fence and check its successful status.
   Independently check CQS value/error: a signaled fence alone does not propagate
   `queue->has_error` from a failed MAP_IMPORT on the reference kernel.
5. Delete/drain that temporary queue and release its status page. Retain the
   kernel context's sticky import reference until Vulkan memory destruction.

On VkFreeMemory, after the application's required GPU completion, the existing
synchronous STICKY_RESOURCE_UNMAP ioctl releases the sticky reference, followed
by GPU region cleanup. It is not issued during range synchronization.

The original CPU pointer is retained; Vulkan map/unmap/free never remap or free
that application-owned mapping. Placed or reserving maps are rejected. The
caller must keep the imported range accessible until memory destruction.
Cookie handles and real GPU VAs are distinguished by the kernel cookie range;
SAME_VA, NEED_MMAP and whether munmap frees the region are separate conditions.
Real-VA mappings keep their GPU VA and require MEM_FREE.

Flush/Invalidate reuse `pan_kmod_queue_bo_map_sync` and existing Mesa
`util_flush*_range` operations; host-import calls complete their post-fence
before returning. There is no shadow copy, page scan, byte comparison, new
cache-maintenance instruction implementation, or PTE teardown in those calls.
The original CSF HOST_WRITE dependency invalidates GPU caches, and HOST_READ
cleans GPU L2/LS; the application still needs the proper GPU barrier and fence
before CPU invalidation/read. This candidate does not make HOST_COHERENT claims
for a noncoherent memory type.

Queue-retirement failure is not accepted as success. If neither a successful
fence nor queue deletion proves a queued import command has retired, its import
and status mappings are retained until process restart, and the device is marked
lost. Unexpected sticky-release failure likewise retains that import and records
an error. This deliberate fatal-error quarantine prevents freeing memory which
may still be referenced; it is an additional resource cost on failure, not a
successful lifetime or leak-free result. Normal map/free must pass independently.

## Fixed sources and reproduction

- Patch-layer repository: `zenithblue-oss/panvk-kbase-android`, commit
  `5b87b74d3e2580d5816e7f2c629f6f016a462f28`.
- Mesa commit: `5a07217f034b3e50d8c7c7794f97a2df1742613b`.
- Original universal profile `g615-v11-csf`:179 original patches,
  SHA-256 `2837740bc363fa778ae578ae856bc5040f3ed1924e01f35d78371cd072a71f90`.
  The original profile includes G710/v10 and does not identify the test device.
- Apply `0000-original-builder-jobs.patch` only to the patch-layer checkout;
  it limits the original Ninja invocations to two jobs.
- Apply `0001-csf-host-import-ext.patch` after the full original stack. Do not
  also apply the predecessor's extension patch. `source-lock.json` binds the
  full resulting tracked diff and untracked source overlays.
- NDK28.2.13676358, compiler API29. Use the original native codegen tools,
  target libdrm and Android/X11-header prefixes and host Meson dependencies.

```sh
python3 scripts/dev/experimental-panvk-host-import/build-panvk-host-import.py \
  --checkout /absolute/path/to/fixed-panvk-kbase-checkout \
  --mesa /absolute/path/to/clean-pinned-mesa \
  --host-tools /absolute/path/to/original/host-tools/bin \
  --ndk /absolute/path/to/ndk/28.2.13676358 \
  --build-dir /absolute/path/to/build \
  --dist-dir /absolute/path/to/output \
  --prepare
```

Use the original build setup's PATH and host-tools runtime dependencies.
The wrapper invokes the original patch applicator, Android builder and ELF
validator. `--prepare` applies once to a clean Mesa checkout; later invocations
verify rather than stack patches. `--check-only` verifies sources without loading
a driver. Outputs include the ICD hash/build ID, Meson options, compiler and
host-tool hashes, actual Ninja dependency metadata/hashes and compile commands.

## Standard own-page oracle

The existing native Vulkan fixture returns before NVN/game initialization. It
prefers HOST_CACHED|HOST_COHERENT imported memory; when the queried import bits
only allow HOST_CACHED noncoherent memory, it uses standard mapped-range
Flush/Invalidate with atom-aligned ranges. The imported memory remains Vulkan
mapped while invoking those APIs. The original CPU pointer is used for every
comparison, including after Vulkan unmap/free.

Each of four lifetimes repeats CPU writes after import, GPU Copy to independent
readback, GPU Fill, HOST_READ barrier and standard Fence completion. Two submits
remain pending behind a host-signaled timeline gate. Range operations do not
replace GPU completion. Output records the selected coherence and atom size.
A host Lavapipe/VVL result cannot prove the Android noncoherent path was executed.

Use the existing developer entry with separate cold processes:
`run_game=false`, `run_vulkan_tests=true`, `vulkan_driver=panvk`,
`vulkan_test_validation=false`, `host_import_test=true`, and explicit
`host_import_opt_in=false`/`true`. Direct-ICD validation composition is unavailable;
zero error counters are not Android VUID/synchronization-validation coverage.
Physical-device access requires a separately coordinated lease and transactional
restoration of the current stable APK/private directories, then process shutdown.

## Kernel and cache-operation evidence

The device-family OEM source is pinned to
[OnePlus Ace 2V Android16 commit63502090421af828a2f44715ff20ea116c24b28f](https://github.com/OnePlusOSS/android_kernel_modules_oneplus_mt6983/tree/63502090421af828a2f44715ff20ea116c24b28f/vendor/mediatek/kernel_modules/gpu/gpu_mali/mali_valhall/mali-r38p1/drivers/gpu/arm/midgard):

- `mali_kbase_core_linux.c:5292` writes the selected coherency enum to the raw
  property field; `mali_kbase_gpuprops.c:839` exports it as property60.
- `mali_kbase_mem_linux.c:1158` accepts MEM_SYNC for UMM/dma-buf only; this
  candidate never assumes that ioctl synchronizes USER_BUFFER.
- `csf/mali_kbase_csf_kcpu.c` performs process-context pinning in MAP_IMPORT
  preparation, propagates command errors through CQS_SET, and executes the
  sticky-resource acquire. FENCE_SIGNAL alone does not propagate queue errors.
- Common CSF ioctl handling in `mali_kbase_core_linux.c:2081` exposes synchronous
  STICKY_RESOURCE_UNMAP; `mali_kbase_mem.c` releases the external mapping and pins.
- Mesa's existing `src/util/cache_ops_aarch64.c` performs CVAC/CIVAC and DSB SY;
  its own helpers define the cache granularity. Existing successful original
  CSF GPU tests already exercise userspace cache instructions, but the imported
  cached-page byte oracle must still establish the new path's visibility.

Published source-family matches do not establish byte identity with the running
OEM kernel. No fixed-address guest mapping or previously rejected diagnostic is
used by this candidate.
