# Same-binary official CPU A/A

Set `official_cpu_aa` to `{ "repetitions": 3, "record_seconds": 30,
"sample_frequency_Hz": 99, "DWARF_bytes": 49152, "keep_failed_debug_info": true }`
in the existing reviewed host-experiment input. The normal helper remains unchanged.
The original single native phase performs its existing three walks once. All five
traces and three records then use the same PID without further movement input.

The order is Perfetto-only, three separate record-plus-Perfetto windows, then
Perfetto-only. Each record uses official `--start_profiling_fd 1` before launching
the existing eight-second scheduler helper. Capture completion does **not** prove
overlap or reproducibility. Failed readiness, collector exit, cancellation or
invalid data cannot be recorded as complete. Original game save2/stop/CAS remains
in the existing controller; owned collector cleanup precedes it.

After the device transaction has restored, run `report-official-cpu-aa.py` with
`--capture`, the exact `--binary-cache`, `--expected-build-id`, and a new `--output`.
It uses official ReportLib sample times and TraceProcessor `to_monotonic` with
`clock_snapshot`, and creates individual Inferno/all-thread/report_html graphs.
No pooled graph precedes individual-window review. Compare the same Surface CPU,
frequency, PSO/variant/work counters and public API logs across bookends and
concurrent traces. Keeping debug info/48KiB stacks changes instrumentation from
the historical eight-second16KiB captures; no strict cross-method gain claim.

For an independent FP sensitivity run use exactly `{ "repetitions": 3,
"record_seconds": 30, "sample_frequency_Hz": 99, "callgraph": "fp" }`.
It uses official `--call-graph fp` with no DWARF size, post-unwind, joiner or
failed-stack flags. All three windows use the same method; outputs/metadata
identify FP separately, with no claim that FP and DWARF measure identically.
