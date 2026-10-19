# Experimental app-local PanVK host allocation import

This isolated source package adds `VK_EXT_external_memory_host` buffer import to
Zenithblue's Android kbase PanVK fork. It does not change the game allocator,
import guest addresses, replace the system driver, or enable PanVK for gameplay.
`PANVK_DM_HOST_IMPORT` defaults to false and is cached at physical-device setup.

The first implementation requires a CSF kbase context whose **selected** CPU/GPU
coherency mode is ACE-Lite (0) or ACE (1). A missing property or NONE (31) does not
advertise the extension. This is a restriction of this implementation; it is not
a claim that all Mali systems cannot import user memory.

## Current evidence

The fixed original ICD has already executed the existing Android app-UID GPU
memory/DS-plane fixture on the PHP110 Mali-G710 MC10. That result does not test
this extension, Android WSI, game rendering, or performance.

On this same device, the first extension candidate reported RAW property 60 as
31. Both explicit cold opt-in settings returned `extension_absent`. The enabled
run logged memory type bits 0 and selected coherency 31. **No host import or GPU
byte oracle was executed.** This is an unsupported result, not a passing import.
The app APK, original driver directory and TLS file were restored, with no app
process left running.

A later source correction separates mmap cookies, real GPU VAs, and region
release ownership. It has a separate NDK-built ICD hash and has not been tested
on hardware. Preserve the first candidate and its unsupported result separately.
No FPS, thermal, memory reduction, or stutter improvement is established here.

## Import and lifetime

The patch implements standard host-pointer properties, external buffer
properties, extension dispatch and alignment properties, allocation, map/unmap,
and release. Only `HOST_ALLOCATION` buffer import is advertised. Foreign host
memory, image import, sparse/protected buffer flags, host-import export and
capture replay are not advertised or accepted by this path.

The import requires CPU/GPU read/write access, CPU-cached memory, and
`COHERENT_SYSTEM_REQUIRED`; the returned cached/coherent flags must also agree.
The reference kernel pins coherent USER_BUFFER pages on import. There is no
fallback shadow buffer or import-time copy presented as sharing. A separate
cached/coherent memory type also requires coherent kbase allocation when used
without host import, including its opaque-fd import path.

The original CPU pointer is retained; `vkUnmapMemory` and `vkFreeMemory` never
unmap or free that application-owned allocation. Placed/reserving maps are
rejected for imported host memory. The caller must keep all imported pages
accessible and retain their contents/ownership until Vulkan memory destruction,
and must complete GPU users before freeing that memory.

The fork's old `same_va` convention is not used to infer the new import's VA.
The import separately records SAME_VA, whether mmap is required, whether the
returned handle is in the kernel's page-sized cookie range, and whether closing
the new driver mapping frees the region. Cookie mappings use the returned mmap
address; NEED_MMAP with a real VA keeps that VA and needs explicit MEM_FREE.
Failure cleanup preserves errno. Other existing dma-buf code is left intact.

## Fixed sources and build

- Patch layer: `zenithblue-oss/panvk-kbase-android`, commit
  `5b87b74d3e2580d5816e7f2c629f6f016a462f28`.
- Mesa: `5a07217f034b3e50d8c7c7794f97a2df1742613b`.
- Original universal `g615-v11-csf` stack: 179 patches; computed original series
  SHA-256 `2837740bc363fa778ae578ae856bc5040f3ed1924e01f35d78371cd072a71f90`.
  This profile includes G710/v10; the profile name does not describe the device.
- Apply `0000-original-builder-jobs.patch` only to the patch-layer checkout.
  It limits the original host/target Ninja invocations to two jobs.
- The extension patch applies **after** the complete original stack, including
  JM overlays. `source-lock.json` binds its hash and the complete resulting
  tracked diff/untracked overlay files.
- Build with NDK 28.2.13676358, target compiler API29. Use the original project's
  native host codegen tools and NDK-built libdrm/X11-header dependency prefixes;
  their hashes and actual compiler dependencies are recorded by the builder.

The wrapper invokes the original project's patch applicator, Android builder,
and ELF validator. It does not download or run a prebuilt release. With a clean
pinned Mesa checkout and the original dependency prefixes prepared:

```sh
python3 scripts/dev/experimental-panvk-host-import/build-panvk-host-import.py \
  --checkout /absolute/path/to/panvk-kbase-checkout \
  --mesa /absolute/path/to/clean-pinned-mesa \
  --host-tools /absolute/path/to/original/host-tools/bin \
  --ndk /absolute/path/to/ndk/28.2.13676358 \
  --build-dir /absolute/path/to/build \
  --dist-dir /absolute/path/to/output \
  --prepare
```

Put Meson/Ninja, the original host-tools runtime dependencies and NDK toolchain
on PATH as in the original project's builder setup. `--prepare` is used once;
subsequent calls verify the exact prepared source instead of stacking patches.
`--check-only` verifies sources without building or loading a driver.
The output contains the ICD, ELF report, original Meson options, Ninja dependency
metadata and hashes, compile commands and `build.json`. These are build evidence,
not GPU execution results.

For the standard own-page oracle, reuse branch
`codex/host-import-vulkan-oracle-v1`, commit `dfa05277286adb7cd65278df6658ce85c74c0604`.
It returns before NVN/game initialization. Developer Intents are
`run_game=false`, `run_vulkan_tests=true`, `vulkan_driver=panvk`,
`vulkan_test_validation=false`, `host_import_test=true`, and an explicit
`host_import_opt_in=false` or `true` in separate cold processes. Direct-ICD
validation composition is unavailable; zero error counters do not prove VVL or
synchronization validation coverage. Device ownership/restoration is coordinated
separately; this package does not install or run anything automatically.

## Kernel responsibility and next route

The selected-coherency export chain is visible in Google's fixed kbase source:

- [selected mode written back to raw_props.coherency_mode](https://android.googlesource.com/kernel/google-modules/gpu/+/341562a4de3c72d7b9f732b079cbb6115a774ad3/mali_kbase/mali_kbase_core_linux.c#5480)
- [property 60 exports that field](https://android.googlesource.com/kernel/google-modules/gpu/+/341562a4de3c72d7b9f732b079cbb6115a774ad3/mali_kbase/mali_kbase_gpuprops.c#764)
- [coherent pin-on-import and import cookies](https://android.googlesource.com/kernel/google-modules/gpu/+/341562a4de3c72d7b9f732b079cbb6115a774ad3/mali_kbase/mali_kbase_mem_linux.c#1559)
- [KCPU USER_BUFFER pin/map/unmap](https://android.googlesource.com/kernel/google-modules/gpu/+/341562a4de3c72d7b9f732b079cbb6115a774ad3/mali_kbase/csf/mali_kbase_csf_kcpu.c#47)

The same writeback/export chain is also present in the device-family OEM
[OnePlus Ace 2V Android16 source, fixed commit 63502090421af828a2f44715ff20ea116c24b28f](https://github.com/OnePlusOSS/android_kernel_modules_oneplus_mt6983/blob/63502090421af828a2f44715ff20ea116c24b28f/vendor/mediatek/kernel_modules/gpu/gpu_mali/mali_valhall/mali-r38p1/drivers/gpu/arm/midgard/mali_kbase_core_linux.c#L5292):
selected mode is written back at lines5292–5293 and exported as property60 in
`mali_kbase_gpuprops.c:839`. This source also retains the USER_BUFFER MEM_SYNC
rejection at `mali_kbase_mem_linux.c:1158`. These fixed published sources do not
establish byte identity with the installed OEM kernel; the captured raw value
remains in the test record.

Noncoherent USER_BUFFER needs additional work. In this reference kernel,
`KBASE_IOCTL_MEM_SYNC` routes imported resources through
`kbase_mem_do_sync_imported`, which rejects types other than dma-buf/UMM.
Therefore copying the native-BO Flush/Invalidate implementation is insufficient.
KCPU map/unmap does perform DMA cache maintenance but also changes pins and GPU
PTEs; repeatedly unmapping an entire allocation cannot stand in for independent
range flushes while other ranges are in flight.

A possible next implementation can reuse Mesa's existing
`util/cache_ops_aarch64.c` range cache maintenance, after proving its platform
contract, together with a properly completed KCPU import mapping held for the
allocation lifetime. It must expose noncoherent memory accurately, implement
real range flush/invalidation and GPU completion/CPU visibility, detect KCPU
errors, and leave the original allocation alive. None of that noncoherent path
is enabled or claimed by the present patch.
