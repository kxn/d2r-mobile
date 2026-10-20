# Official default-backtrace control diagnostic

This source-only successor follows the actual full-unwinder diagnostic failure.
That run activated the official library but never reached HUD and logged 142
UNKNOWN POINTER frees; it is invalid for allocation attribution. The original
report, source/ELF audit and root recovery remain unchanged. This next attempt
only removes `backtrace_full` from the package's official environment:

```
LIBC_DEBUG_MALLOC_OPTIONS="backtrace=16 verbose backtrace_dump_prefix=/data/user/0/com.d2rmobile.nativeport/files/runtime/temp/malloc-debug-control"
```

AOSP PointerData::AddBacktrace selects backtrace_get/_Unwind_Backtrace without
BACKTRACE_FULL; the full branch alone calls AndroidLocalUnwinder/Unwind, whose
MemoryThreadCache allocates pthread-key caches inside ScopedDisableDebugCalls.
The default branch avoids this identified internal-cache path. This source
fact does not prove the vendor binary will avoid every allocator error or that
all guest stacks can unwind. Host/NVN/driver allocation stacks may be sufficient
without managed Java frames. No official library, runtime TLS or allocator is
modified; the actual default path still needs one controlled validation.

Packaging is opt-in (`--malloc-debug-wrap`, `--build`), defaulting to a plan.
Only lib/arm64-v8a/wrap.sh is added to the canonical bcbd751 APK. All six native
libraries, manifest, DEX and assets remain byte-identical; original raw NSO and
resource/driver inputs retain their existing hashes. zipalign and the existing
signer verify the new archive. There is no LD_PRELOAD or global property.
The diagnostic canonical record references the original runtime build.

The original single control transaction is reused, including standard6217
resources, AA185, original HUD/walk route, original save2 and APK/private-data/
PSO restoration. No native phase, concurrent LLDB or heapprofd is permitted.
The input must pin the earlier full-unwinder negative audit and the independent
exact bc lifecycle source/ELF proof. Existing launcher90s/parent120s bounds and
one game launch remain; an intrusive cold-start timeout is not a proven
production runtime failure and must not cause parameter scanning/restarts.

One SIG47 dump is requested only after original PID/UID/TracerPid, exact env,
loaded library, private driver SHA, caught handler and official enabled plus
handler-registration receipts agree. Unknown/bad-value/disabled options and
UNKNOWN POINTER errors reject coverage before signaling. The official complete
dump has a 25s bound, immutable export SHA and exact official header/parser
counts/totals. Unknown/short/no-host-driver allocation bytes are always retained.
The owned PID's official allocator log is checked again after dump, during
finally and after game stop; errors cannot be overwritten by later viewer
success. Missing final health evidence also fails acceptance. Partial output
and actual tool failure remain distinct from runtime correctness evidence.

Cold-menu cleanup uses the exact independently reviewed bc target, not empty
OBS_TICK output. The loaded /data/app ELF must hash-match that proof; same
PID/ordinary UID/no tracer remain mandatory. The same Activity is foregrounded,
HOME requests original onPause, and its own Activity record must reach STOPPED.
A real new asked=1/commits=2 still proves a saved world. Otherwise menu cleanup
requires new focus3 followed by positive GPU-quiescence output, one same-PID
native-start history, exclusively MainMenuLinks/area0/no-player render reports,
and no HUD or background-save branch anywhere. In this exact source/ELF,
dm_ingame_now is diag.c dm_hud_get().valid and a true predicate unconditionally
prints the save result to _IONBF stderr before graphics suspend. This whole
positive lifecycle chain proves no save was required according to the original
pause predicate. A surface-only quiescence line or absent log by itself does
not suffice. Other ELF, missing evidence, world/previous-save history or
replacement PID stays paused/unaccepted and blocks dependent restore.

Official native_heapdump_viewer is reused after the owned game is stopped. Its
preserved upstream plus four-line compatibility patch only retain zero-frame
records and support Python3 subprocess text mode. GNU objdump/addr2line match
its required File-off/-j interfaces; a real canonical ELF fixture validates
symbolization. Its parser, mapping and allocation tree are not reimplemented.
Viewer failure preserves raw evidence and is a tool failure.

All reported sizes are live requested allocations, not RSS/allocator metadata/
GPU physical bytes. Even default backtrace imposes substantial CPU/metadata
costs; profiled PSS/FPS cannot be presented as optimization gains. Header totals
alone never prove full allocation coverage. Unknown, one-frame, short and
no-host/driver bytes are explicit; successful tool operation does not mean all
call sites are resolved. API35 log_allocator_stats_on_signal is lighter but
only gives allocator totals/size classes, not retained call-site attribution;
it is not enabled here. No custom malloc hook, sampler or target function call
is introduced. Source-only preparation performs no device access.
