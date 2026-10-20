# Decode the three retained root streams after a CSF timeout

Apply `0003-csf-timeout-root-streams.patch` after the frozen e8fc0ae
`0002-csf-fault-decode.patch` in the original bdd9 Mesa source closure.
Only `panvk_vX_gpu_queue.c` changes. Normal submit, command emission,
cache operations, waits and shader/resource code remain unchanged.

The completed v4 fault listing omitted the active COMPUTE root even though
its last-stream address and size were present in the timeout snapshot. The
first CPU waiter cannot identify which dependent GPU queue stopped progress.
At that same existing timeout entry, snapshot all three last-stream address
and size pairs, validate alignment/overflow, and call the existing bounded
`pandecode_cs_binary` for each. This is a static listing of retained streams,
not an instruction pointer, execution trace, time measurement or new decoder.
Existing mapped-range checks reject missing/truncated roots visibly.

Cost is fault-only: three existing decoder calls, their existing temporary
allocations and potentially larger diagnostic output. The local snapshot is
48 logical bytes. Compiler stack reservation is not a performance claim.
The original e8 decoder and ring helper are byte-identical, so its 24 real
Mesa decoder host checks and original-decoder SIGSEGV negative control are
inherited evidence, not a new execution of this timeout callback. The full
original NDK driver build compiles the new callback. Actual device validation
and further CS dependency analysis remain separate.

Build using the original Android builder, private directories, original host
codegen binaries, profile g615-v11-csf, API29, NDK28.2.13676358 and ninja -j2.
Keep the same 277 APK/NSO/resources for the next parent-owned diagnostic run:
startup,kbase_diag; no sync/cs/dump/trace. Restore the original driver and
properties transactionally. Decoded CS may still lack runtime progress-base
registers; this change does not itself prove a dependency cycle or GPU fix.
