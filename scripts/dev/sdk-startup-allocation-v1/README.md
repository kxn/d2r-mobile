# Original SDK startup allocation provenance (tools only)

This opt-in extends the original native-bulk-game-v1 controller from 9d8ea6f.
It does not rebuild/patch the game, replace an allocator, call an inferior SDK
function, profile CPU time, or confer any memory-reclamation permission.

The actual new startup sequence is:

1. All original canonical, fresh-raw28, native assets, prior actual f75 game,
   install identity and CAS gates run unchanged. A successful 44c read-only
   postroute snapshot helper/config is still required. New input only:
   `startup_allocation_capture: {helper: {path,sha256}, config: {path,sha256}}`.
   It conflicts with all CPU/GPU sampling, counter and sustained-play modes.
2. Original launcher receives `--wait-java-debugger`, adding **only** `-D` to
   its normal `am start -W -n ...` intent. Existing extras remain unchanged.
   This is a per-launch flag, not persistent `set-debug-app`. The controller
   already observes/claims the first ordinary app PID while launcher runs.
3. Before the original HUD wait completes, invoke the frozen helper using the
   same CLI as the accepted snapshot: `python run-debugger.py PID OUTDIR
   --config CONFIG --config-sha256 SHA`. `--plan` is host-only. It verifies
   canonical/APK/ELF/SDK and tools bytes. Actual startup requires a JDWP endpoint
   and no host library yet mapped; late attach is rejected, not called success.
4. Reuse accepted 44c owned lldb-server socket readiness, attach, cleanup and
   TracerPid checks. Unlike postroute attach, no premature mapped-host wait.
   Formal ELF is target-created offline; an official pending `guest_thread_init`
   breakpoint is installed, process continued, and a PID/ELF arm acknowledgment
   written. Only then does the wrapper start the **original NDK28** executable:

   ```
   python NDK/prebuilt/linux-x86_64/bin/ndkgdb.pyz \
     --internal-wakeup-pid-with-jdb ADB SERIAL JDB OWNED_PID True
   ```

   Its existing `start_jdb` function owns SocketAttach, readiness print,
   settling delay and `exit`. It uses tcp:65534; an existing forward is refused.
   Wrapper owns the process group and removes only its same-PID forward.
5. At the host breakpoint, require the original `dm_android_main` ancestor,
   actual f75 host UUID and mapped file hash. Read named `g_mod`/`g_nmod` through
   exact DWARF, verify unique main/sdk and original SDK Allocate's unchanged
   0xf4 live bytes against pinned original flat input. This point follows
   load_modules but precedes `_init_libc0`/SDK startup/constructors/nnMain.
6. Remove startup breakpoint; set original SDK `base+0x2f9a54` with pure-register
   condition `x21==actual_main_base+0x32137a0 && x20>=134217728`. Original verified
   prologue preserves size in x20, alignment x19, allocator x21; x0 is return.
   Record up to configured 1..16 returns (recommend **12**) within **60s total**,
   raw registers, saved LR `[x29+8]`, and official `thread backtrace -c 64`.
   No guessed frame parents. Unknown guest ancestry remains UNKNOWN.
7. Interrupt if needed, remove own breakpoints, detach, confirm server/JDWP/
   TracerPid cleanup, continue original HUD/threewalk/save flow. At original
   postroute snapshot, reuse the exact 44c SDK decoder to correlate return
   extents with the **same PID's new** partition and registered Pool ranges.
   Intervening free/reuse is not tracked: address overlap is not continuous
   lifetime proof. Never join an old PID or old absolute addresses.

`startup_allocation` in the helper config (all other identity keys same as44c):

```json
{"mode":"f75-main-allocator-large-return-v1","minimum_bytes":134217728,
 "max_returns":12,"natural_wait_seconds":60,"metadata_budget_bytes":1048576}
```

No guarantee the two previously observed 250/260MiB spans will occur or fit this
window. Count and time truncation are recorded. Conditional **software**
breakpoints can trap on every Allocate even when the condition rejects it:
this is an intrusive finite provenance experiment, never a performance window.
Metadata budget is 1MiB for accepted return/stack records; target reads are
named module metadata +244 original code bytes +8 saved-LR bytes per hit.
The already-qualified later SDK/PTE snapshot keeps its original budgets.

Unrelated stop, unknown module/code, changed PID, Java release failure and no
large hits fail/inconclusive with detach. The original save2 path remains when
nativeStart ran; a confirmed pre-native Java-wait failure stops the owned app
without claiming a save (there is no new game state). A replacement PID or
foreign tracer is never stopped. No global ADB/server/system-service changes.

Official sources: [AOSP startup debugging](https://source.android.com/docs/core/tests/debug/gdb#debug-app-startup),
local NDK28 `ndkgdb.pyz:ndkgdb.py` `start_jdb`, [NDK debugger](https://developer.android.com/ndk/guides/ndk-gdb).
Host local JDK/JDI and pending symbol resolution are checked separately;
Android early-start behavior is **not yet device-qualified**.


### JDWP readiness consumer (V3)

The first actual startup attempt failed before native startup/server creation:
`subprocess.check_output(adb jdwp, timeout=20)` waited for EOF. The unchanged
failed transaction is `sdk-startup-large-allocation-f75-restore-a322-Q735-v1`.
AOSP `client/commandline.cpp` blob `5cd3ab66dd0a7ba0173b01f954fbe76c21269483`
uses separate `jdwp` and `track-jdwp` services; both use `adb_connect_command`
with `read_and_dump`. This evidence does **not** mean `jdwp` is a track-jdwp
stream or that every implementation must remain open.
Source: https://android.googlesource.com/platform/packages/modules/adb/+/refs/heads/main/client/commandline.cpp

V3 still runs the original official `adb -s SERIAL jdwp`. It consumes stdout
until the exact owned PID appears as a complete line (20-second deadline,
64KiB combined observed stdout/stderr bound). It accepts finite or still-open
output without waiting for EOF. A partial PID, substring, stderr PID, absent
PID, observed client error or deadline failure is not readiness. It preserves
observed stdout/stderr bytes and `official-adb-jdwp-readiness.json` with EOF,
exit/stream-at-match and owned-reader cleanup facts. After a match or failure,
it terminates/reaps **only the local reader child it created**; it neither
restarts the ADB server nor stops another client or Android service.

This fixes host orchestration only. It leaves the exact SDK 244-byte check,
NDK JDB release, LLDB callbacks, allocation conditions/budgets, postroute
snapshot and original save2/stop/CAS policy unchanged. Conditional SDK software
traps can still perturb startup. Reaching a JDWP PID is not evidence of native
startup or successful allocation capture. A character-select `save asked=0`
still fails closed and cannot authorize unsaved stopping/restoration.

Host regression: `python3 scripts/dev/sdk-startup-allocation-v1/test-jdwp-readiness.py -v`.
