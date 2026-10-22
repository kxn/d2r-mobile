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


### V4: flushed official JDB and a genuinely unloaded host module

Actual V3 observed the JDWP PID and safely detached/saved/restored, but no
startup breakpoint or JDB magic appeared. The packaged NDK28 `start_jdb`
function writes both `print` and `exit` to buffered stdin without flushing.
The unchanged extracted function and a real host child reproduced that no
command arrived before the child deadline; a flush-only variant delivered it.
V4 directly runs the same official JDK JDB SocketAttach arguments, same print
magic, 0.3-second settling delay and exit command, with both writes flushed.
Output is bounded to 64KiB and the sequence to20s. The original wrapper owns
its separate process group and JDWP forward and keeps its original cleanup.
A real local official JVM/JDI attach completed this flushed sequence. That is
host tool qualification, not Android qualification.

`target create formal-host.so` misidentified the not-yet-loaded SO as process
executable in V3. Official LLDB rejects no-file CLI `target create`; the SB API
can create an empty AArch64 target. Importantly, pre-attach `modules add` makes
GetExecutable nonempty again. V4 therefore creates the empty target, attaches
first so the server establishes the real executable, then adds exact host
symbols. Before continuing, it rejects a formal SO selected as executable or
any already-assigned host load address; UUID must match the exact f75 file.
The later original `guest_thread_init` path still requires the actual loaded
UUID, `dm_android_main` ancestry, actual module table and unchanged244 SDK bytes.
No guessed slide is applied. Local LLDB proves empty target creation, source
lookup/UUID and unloaded-address behavior; real remote dlopen resolution still
requires the next bounded device transaction and is not claimed here.

All failed V1/V3 artifacts remain unchanged. The fixed consumer only removes
host orchestration blockers and does not qualify target allocation origins,
performance, page reclamation or a successful game execution.

### V5: exact actual Android executable and linker

V4's remote empty target never acquired an executable. CLI `script` assertions
printed an error but continued; adding the host then selected the wrong main.
The hook rejected this and original restoration completed. That failed run is
preserved as `sdk-startup-large-allocation-f75-restore-a322-Q735-v4`.

V5 requires `debuggee_inputs={path,sha256}` for the exact acquired Q system
executable/linker manifest (`36842fa9...`). The wrapper verifies that manifest,
both local byte hashes and sizes, and both actual remote byte hashes before
starting its server. Frozen tool inputs pin the manifest and both actual files.
It creates `app_process64` as the main executable using `remote-android` and its
acquired-file sysroot. Official `SetPlatformFileSpec` associates the local
copies with their exact `/system/bin/app_process64` and actual APEX linker
paths; the linker is added as a module, and formal host symbols only afterward.
No target code or file is modified.

The official CLI `--remote-file` attempt could not open the unconnected remote
file; that local failure is preserved. V5 uses the locally proven
`SetPlatformFileSpec` API instead. Both calls returned true locally; the main
remained app_process64, all three UUIDs matched and the host breakpoint had no
load address. The .gnu_debugdata LZMA warning remains in the official output.
This establishes local setup only, not Android dynamic-loader success.

All decisive checks now raise inside the acquisition hook: real main filename,
main/linker exact UUID and remote platform path, unique file-offset-zero maps
from the current owned PID, each module header load address equal to its actual
mapping, and formal host still unloaded before startup. The existing actual
loaded host UUID, original main ancestry, module table and 244-byte original
SDK code checks still apply. A failed CLI operation cannot authorize arming.
No guessed slide, synthetic executable identity or ignored unrelated stop is
accepted. Allocation/JDB/cleanup bounds and original controller are unchanged.


### V6: consume public LLDB process events before inspecting a stop

V5 reached the original `guest_thread_init`, exact f75 UUID/caller and live
SDK244B checks, then observed a wrong allocator/size during a conditional
breakpoint's auto-continue transition. That failed actual transaction remains
unchanged. Polling GetState/GetStopID sees the stopped state before LLDB has
finished evaluating a software breakpoint condition.

V6 subscribes an owned SBListener to process-state events. It consumes official
GetNextEvent results and ignores a stopped event only when
SBProcess.GetRestartedFromEvent explicitly says LLDB auto-continued it.
A genuine non-restarted stop still must be a fresh stable stop, with the
original exact breakpoint/PC/register/allocator/size qualifications. Unrelated
processes/events/stops, exit/crash or wrong registers still fail closed; there
is no manual continue for rejected register values. Cleanup removes only its
own listener; the original interrupt/detach/save/CAS behavior remains.

Before checking an allocation stop, it saves bounded raw debugger stop ID,
PID, stop reasons/BP data and original registers in
`allocation-last-observed-stop.json`; unknown register reads remain UNKNOWN.
At most16 active stop threads/32 reason data words each are retained, explicitly
marked if truncated. That diagnostic plus accepted metadata shares the existing
1MiB bound. No register data are read from auto-restarted/running events.

The actual host-native test uses 200 false register conditions followed by one
true condition, another genuine breakpoint and a deadline. Official LLDB19
reproduces old polling accepting rdi=1, while the public-event path skips200
explicitly restarted stops and returns rdi=200. It also preserves/rejects the
unrelated breakpoint and removes its listener/ends each owned fixture process.
This is native host debugger orchestration qualification, not a game execution
or CPU emulation milestone. A prior DWARF-variable-condition attempt with
unavailable/JIT diagnostics is preserved and is not qualification evidence.

Primary API/source:
https://lldb.llvm.org/python_api/lldb.SBProcess.html#lldb.SBProcess.GetRestartedFromEvent
https://github.com/llvm/llvm-project/blob/llvmorg-19.1.7/lldb/source/Target/Process.cpp
`ProcessEventData::DoOnRemoval` handles public conditional-stop actions and
sets Restarted before PrivateResume. SBProcess exposes that official result.

Reproduce with the system C compiler (host x86), then official LLDB19:
`cc -O0 -g test-native-process-events.c -o OUTPUT/native-conditional`; set
`DM_SDK_EVENT_TEST_OUTPUT=OUTPUT`, import `test_native_process_events.py` and
call `test_native_process_events.run(lldb.debugger)`. It does not use ADB.
