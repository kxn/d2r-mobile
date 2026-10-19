# Optional pool copy accounting

This is a logical work report, not a timer, sampler, hardware bandwidth or RSS
measurement. The tool coverage audit is `.local/logs/pool-copy-accounting-tool-coverage-v1.json`
(SHA256 `fdea54595e87538d2577ef81bc5060efae22b8abab082031d1e22a67e616daa8`).
Existing Simpleperf/Perfetto/Streamline remain responsible for CPU cost,
scheduling and hardware traffic; meminfo/smaps remain responsible for residency.

Start Android with `android-game-launch.py --pool-copy-accounting` (or set
`DM_VULKAN_POOL_COPY_ACCOUNTING=1` before backend initialization on a host).
Default is OFF. Only `0` and `1` are valid environment values. The option is
latched once; a later Intent cannot change it. Enabled runs append one fixed
`pool_copy_accounting` object to the existing game-state output. OFF omits it.

All fields are cumulative logical bytes. They are independently atomic snapshots,
not a transactional snapshot or a GPU completion proof. For a closed interval,
use the existing lifecycle/fence boundaries; final stopped/drained values include
all completed effects. These debug atomics add some work while ON, so do not
report their runs as uninstrumented CPU timing or hardware bandwidth baselines.
No allocation, background task, extra guest/shadow scan or per-word atomics are
added. OFF still has predictable flag branches and the small fixed static state.

| Field | Meaning |
| --- | --- |
| `scan_bytes` | Full default-scanner logical coverage per input, including retry attempts. Guest and shadow each cover this span; not actual DRAM reads. |
| `fallback_compare_span_bytes` | Requested full-span `memcmp` coverage in synchronous/unaligned fallback. Short circuit and machine loads are not measured. |
| `reference_page_span_bytes` | High-level scalar-reference page coverage. Its repeated word comparisons are deliberately not counted; this is not their total or an upper bound on total reads. |
| `initial_mirror_bytes`, `initial_shadow_bytes` | First actual exposure copies, separated by CPU destination. Unused reservation holes do not count. |
| `mirror_bytes`, `shadow_bytes` | Changed-data CPU destination bytes, excluding first exposure and completion effects. Sparse, dual and clipped fallback copies use their actual lengths. |
| `snapshot_bytes` | Actual guest-to-immutable-upload-staging copy length, excluding rounded allocation capacity. A fragmented edit can snapshot a whole page while uploading only changed runs. |
| `gpu_upload_recorded_bytes` | Actual recorded staging-to-mirror `VkBufferCopy` regions, not queued capacity or fence-completed GPU bytes. |
| `known_snapshot_bytes` | Actual immediate caller-value or deferred shadow-value immutable snapshot fill, excluding allocation-only events. |
| `readback_guest_completed_bytes`, `readback_shadow_completed_bytes` | Actual completed staging-to-CPU destination copies after the existing GPU fence and cache invalidation. |
| `known_guest_completed_bytes`, `known_shadow_completed_bytes` | Actual completed immutable-known-value CPU destination copies. They do not perform GPU readback. |
| `migration_gpu_recorded_bytes` | Cold GPU-to-GPU initialized-range preservation commands, separated from guest uploads. |

Existing `buffer_sync.readback_bytes` reports recorded pool GPU-to-staging
logical ranges. `internal_readback_bytes` and `readback_memory.cached_bytes`
are subsets, not additional bytes to sum. Existing `cpu_known_copies.published`
counts a recorded logical range once, despite two CPU destination effects;
`snapshots` counts snapshot allocation sizes. Do not add these existing values
again to completed CPU copy totals. Generic texture/query effects and arbitrary
process `memcpy`, allocation zeroing and generic GPU buffer copies are outside
this pool-copy report. Capacity and initialized-page counts do not imply RSS.
