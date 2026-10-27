# Existing Mesa GPU CSV analysis

These scripts organize **existing** Mesa u_trace output. They do not capture, replay, sample, add GPU timestamps, or report a new FPS measurement.

```sh
python3 tools/experiments/mesa-pass-analysis/analyze.py mesa-after.csv \
  --before-checkpoint mesa-before.csv --output new-analysis-directory
python3 tools/experiments/mesa-pass-analysis/inspect_groups.py new-analysis-directory \
  --output new-analysis-directory/group-metadata.json
```

The optional checkpoint must be an exact byte prefix of the later CSV. Only newly published bytes are analyzed, with an explicit relative-line offset. It is **not** a host/Surface window mapping. A partial first row, missing starts/ends, negative ranges and same-kind nested ambiguity remain visible. Complete command-buffer envelopes and complete/unambiguous child scopes are reported separately. Nothing assumes one batch is one frame or one command buffer, a49-pass main buffer, or the last300 observations. Whole-input analysis is also supported by omitting the checkpoint.

Command-buffer classes use observed structural shape and end metadata. A recurrent class's aggregate reported draw footprint identifies a workload candidate; the longest buffer is never automatically called the main/HDR pass. All classes, mismatched counts and edge fragments remain available. Per-pass groups include actual dimensions, attachment/draw counts, UBWC flags, sysmem/tiled policy, load/store metadata and pass ordinals. Compute groups retain actual native shader hashes and workgroup geometry. Inclusive child phases are not additive; interval unions are supplied only as envelope coverage, never as a hardware busy counter or actual DRAM bandwidth.

`analysis.json`, `intervals.jsonl` and `SUMMARY.txt` retain actual CSV fields, source hashes and provenance. `inspect_groups.py` adds ordinal LRZ metadata and zero-draw pass envelope bounds. `avgPerSampleBandwidth` is Mesa model metadata, not measured bus traffic. Likewise `lrz`, its write-disable reason and the GPU direction-buffer byte are distinct facts; a `DISABLED` direction string alone does not prove whole-pass LRZ was unused.

The CSV clock is driver GPU ticks scaled to integers (this Turnip source uses ticks×52ns), not BOOTTIME. No GPU/host mapping or Surface FPS inference is fabricated. An official joint clock calibration and matching workload/layer boundaries are required for cross-tool correlation.

`select_complete.py` streams a large existing draw CSV and retains a bounded
number of explicitly requested complete-CB structures. This is an identity
subset, not a measurement window; the selection criterion is recorded and must
not become an implicit assumption in the generic parser.

For a separately captured identity-only diagnostic with standard Mesa draw tracepoints and IR3 dumps:

```sh
python3 tools/experiments/mesa-pass-analysis/link_draws.py \
  --analysis current-draw-analysis --ir3 actual-ir3.log --output native-identity-link.json
```

This matches actual VS/FS native hashes to exact stage/hash IR3 dump blocks inside the corresponding current CB class/pass ordinal. It preserves unmatched/ambiguous draws and checks paired draw counts against render-pass metadata. Draw timing is never used as a shader cost estimate, especially on tiled GPUs. Draw/index counts are not fragment invocation counts. The IR3 hash covers native code and double-thread size; static opcode text and compiler statistics remain distinct from dynamic operation cost. Historical native programs are never selected merely by size or program number.

`map_sources.py` associates exact native hashes with that compile block's NIR
`source_blake3`, decodes the eight uint32 words little-endian, and uses the
standard BLAKE3 package to find the exact original SPIR-V payload (including
declared spec/predicate container offsets). It never substitutes a module merely
because its key or size looks similar. `inspect_vs_selectors.py` reuses the
existing Uniform-selector qualifier read-only for actual VS payloads and reports
unproved dependencies. No alternate shader is emitted. `inspect_fetches.py`
uses official SPIRV-Tools normalization to inspect exact descriptor-fetch paths
that reach integer bit observations. A missing such path is not an equivalence
proof; runtime format/range and dynamic use remain outside this static analysis.

The optional production-ready-input counting diagnostic and its separate
qualification boundary are documented in [PREDICATE_OBSERVER.md](PREDICATE_OBSERVER.md).
It is distinct from timing/profiling and remains cold/default OFF.

Tests:

```sh
python3 tools/experiments/mesa-pass-analysis/test_analysis.py
python3 tools/experiments/mesa-pass-analysis/test_link_draws.py
python3 tools/experiments/mesa-pass-analysis/test_source_selection.py
python3 tools/experiments/mesa-pass-analysis/test_inspect_fetches.py
python3 tools/experiments/mesa-pass-analysis/test_predicate_observer.py
```

They cover14 CSV/interval cases,4 native-identity join cases,3 source/selection
cases,3 fetch-use cases and3 real-C observer contracts (ASan/UBSan). This includes
JSON parameters, partial checkpoints, mixed CB structures, nested/overlapping
phases, missing boundaries, metadata mismatch, scheduling-prefix spacing,
little-endian source hashes, raw-bit observations and unchanged baseline module
selection. An expensive startup compute buffer must not become the main-render
candidate.
