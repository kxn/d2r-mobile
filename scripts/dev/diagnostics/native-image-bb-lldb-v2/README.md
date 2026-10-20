# bb4 finite bind/TRANSFER/FRAGMENT official LLDB hook v2

Protocol native-bind-transfer-fragment-v2. The frozen v1 and previous actual
bind milestone remain unchanged, as do the bb4 runtime APK/ELF and asset bytes.

Call python3 run-debugger.py FIRST_PID EXISTING_UNIQUE_OUTPUT. The original game
transaction owns cold install/launch, first ordinaryUID/PID, resource identity,
HUD screenshot, original inputs, save2/stop and rollback. It deploys exact NDK28.2
ARM64 lldb-server in its owned temp. This hook owns only its server/client/forward.

The exact two instruction breakpoints remain bind entry0x32a954 and successful
use0x32afac. Entry is one-shot. The official use callback reads W19 first and
fully snapshots only first TRANSFER0x1000 and first FRAGMENT0x80; all other hits
continue without typed traversal. It detaches after those three snapshots or
the original65s tool timeout. Filtering still incurs actual debugger traps,
remote continue and Python callback work: do not measure FPS/CPU/heat in this
run. The tool-only filtered count is current at a successful final snapshot;
timeout may retain only the preceding saved count. No runtime timer/counter or
sampler is added.

Events keep kind bind_entry/use_validated. arguments.stages and use_stage_role
distinguish uses. typed_B2T reads the actual p_copy_b2t and VkBufferImageCopy c:
parameters include src/tex/view/region/flags; destination_tex has serial/memory;
actual_copy holds actual Vulkan mip/layer/ROI and buffer stride/offset fields.
row/img are read from the actual caller. When optimized out at use, the earlier
bind snapshot can supply them only if immutable p contents/pointer AND outer SP
match this same call. Actual mip is never inferred from flags or dimensions.

FS adds actual shader prepare/binding caller, key/stage/module and binding
kind/register, same native image/serial/entry, original layout bytes, UUID/hash
metadata and fixed writer-scope pointers. Only stage1 sampled kind3/6 can pass;
unavailable metadata is inconclusive. Actual shader execution/pixels require
separate evidence.

debugger-result.json requires matched full135168-byte bind, TRANSFER ROI inside
the9mip recipe and the same immutable token/image through FS sampled binding.
The caller must require native_binding_accepted, transfer_and_fragment_proven
and owned_debugger_cleanup_complete. No stop/no FS is inconclusive, not runtime
failure. Logs/save2/stop/restore are mandatory even after hook error. No retry.

Only SBFrame/SBValue/SBType/ReadMemory; no target expressions/functions.
python3 test_guards.py checks synthetic metadata guards. Official LLDB checks
exact ELF/DWARF/B2T-copy types and locations without an inferior. Offline tests
are not actual device results.
