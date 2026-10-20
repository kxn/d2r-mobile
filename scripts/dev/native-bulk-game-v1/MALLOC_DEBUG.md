# Official malloc_debug single-control diagnostic

This is an intrusive allocation-attribution diagnostic, not a performance or
resident-memory comparison. It does not change runtime, driver, game modules,
resource bytes, or allocation code. It does not use a custom malloc hook or
sampler. No device is accessed by packaging or by the controller's default plan.

`build-android-malloc-debug-apk.py --malloc-debug-wrap` reads a pinned canonical
APK. Only `lib/arm64-v8a/wrap.sh` is added; every original ZIP payload entry,
including six native libraries, DEX, manifest and assets, remains byte-identical.
The existing signer is retained and zipalign/apksigner verify the new archive.
Without `--build`, packaging is a plan. The diagnostic record points to the
original canonical build, not a new runtime build. It is explicitly opt-in.
The wrapper exports only the official package-scoped setting:

```
LIBC_DEBUG_MALLOC_OPTIONS="backtrace=16 backtrace_full verbose backtrace_dump_prefix=/data/user/0/com.d2rmobile.nativeport/files/runtime/temp/malloc-debug-control"
```

There is no LD_PRELOAD or system property. Android's wrap mechanism activates
its own libc_malloc_debug in a debuggable APK. Whether the device accepts the
options and its full unwinder can cross guest-stack gateways is an actual-run
question. An installed public library alone does not prove activation.

Use a standard original-game control phase with a diagnostic canonical record
and a `malloc_debug` input containing pinned packaging, runtime_symbols and
driver_symbols, plus mode `control-only`. The native phase is never run by this
first diagnostic. No concurrent LLDB/counter or heapprofd is permitted. Existing
28-module raw-NSO rules, driver identity, resources, baseline, PSO backup and
original transaction restore checks remain in force.

There is one cold launch: the original launcher has a 90-second bound and its
parent observation has a 120-second bound, with no retry. After the original
HUD/route the tool verifies the first PID, ordinary UID, TracerPid=0, exact
startup environment and private driver SHA. It additionally requires the loaded
official library, caught signal 47 and the official successful signal-handler
registration message for that PID. A missing capability is a tool limitation;
no signal is sent to a process without the verified registration. A preexisting
dump is rejected. Signal 47 is requested once; completion has a 25-second bound
and requires the official END marker. There is no expression/allocator function
call. The real official dump is exported with matching device/host SHA. On
failure, any safely owned partial output is retained as explicitly untrusted.

Tool failure does not replace the independent original-game lifecycle: with the
same first PID, no tracer and fresh original save commits=2, the game is stopped
and checked absent. An unknown/replacement PID is never saved or stopped. The
existing APK/private-directory/PSO rollback then runs. The official viewer is
run on the host only after the owned game has stopped; a viewer failure remains
a distinct tool failure with the raw evidence preserved. No profiler daemon is
started. One diagnostic may substantially slow cold launch or exceed its bound;
this is not a runtime correctness or FPS result and must not cause repeated
starts with parameter scanning.

The pinned AOSP native_heapdump_viewer is retained byte-for-byte with provenance.
Its four-line compatibility patch adds Python3 subprocess text mode at two
calls and permits zero-frame records at two parser conditions. Without these,
the original script throws a bytes/string error during actual symbolization
and silently drops allocations with no stack. Its parser, address mapper,
symbolizer and allocation tree are reused; no new heap capture or parser is
implemented. Use GNU objdump/addr2line (pinned in the plan): the official script
requires GNU's File-off column and addr2line's `-j`; LLVM versions do not have
the same interface. An offline known-function fixture checks actual ELF
symbolization and retains the original-script failure.

Acceptance requires a complete official v1.2 dump, exact header totals/record
counts, original backtrace size, mappings and nonzero host/driver frame coverage.
Zero-frame, one-frame, fewer-than-four-frame and no-host/driver bytes remain
reported explicitly. These are weighted live requested allocation bytes;
short/unknown stacks cannot be assigned to Buffer windows or silently excluded.
They are not resident RAM, allocator metadata, mmap totals or GPU physical
memory. Viewer success does not claim every stack is resolved. Process-wide
backtrace_full tracks every malloc/free and can impose large CPU and metadata
costs, hence its PSS/FPS must not be compared with the earlier unprofiled pair.
There is no official per-thread/module capture filter for this mode.

The lighter official API35 `log_allocator_stats_on_signal` invokes allocator
`mallopt(M_LOG_STATS,0)` at the next allocation after signal49. It can inspect
allocator totals/size classes without recording every stack, but does not
attribute retained bytes to call sites and cannot answer the current driver vs
runtime allocation question. It is not enabled by this tool. `am dumpheap -n`
needs pre-enabled malloc tracking; debuggerd captures current stacks, and
libmemunreachable covers leaks rather than all retained allocations. The two
failed heapprofd traces stay invalid and are not reused as full attribution.
