# Matched original-game native texture experiment

Offline by default. This controller reuses the accepted original-game Device,
directory inventory/CAS rename, installer acknowledgement and lifecycle-save
helpers. It does not patch an APK, game module, shader, driver or resource.

An explicitly hashed input manifest pins the already built runtime, independently
verified original-NSO bundle, AA driver, current stopped-device baseline, actual
driver fixture receipts and both composed overlays. Both phases use the same
runtime, driver, shader chain, selected ASTC4 textures, 16 MiB Buffer windows,
2750x1260 output and fixed 0.60 world scale. Only the composed texture layout and
native table differ. The original cache, temporary files, modules, overlay and
driver are retained by stopped-process directory renames and restored exactly.
The user's save directory is never backed up, overwritten or reset.

Only the enumerated regular overlay leaves are deployed; never recursively copy
a generated RomFS directory containing original-archive symlinks. Each phase
starts with the same original pipeline-cache seed, if present. Phase results
include the original HUD, normal joystick route, screenshots, existing game
state and a single system `dumpsys meminfo` snapshot. These are functional and
residency evidence, not a sustained thermal/FPS result. Official profiling may
be added only through the existing tools, in a separate window from a debugger.

An optional `counter_snapshot` input has exactly three pinned file records:
`ready`, `config`, and `server`. It uses the frozen `4984509` official LLDB
helper after the normal route, outside any profiling window. The helper reads
four existing global counters once and detaches; its configuration must match
the paired APK/ELF and canonical build record. The exact NDK server is deployed
only into each experiment's isolated temporary directory. No production
runtime counters or per-frame logging are added. A helper failure still runs
the original save/stop path for the owned first PID. Counts describe imported
address ranges and metadata, not resident or unique physical RAM; keep the
system memory snapshot as a separate measurement.

Run with `--inputs PATH --inputs-sha256 SHA --report PATH`. Add `--execute` only
after reviewing the exact immutable manifest and prerequisite receipts. There
is no implicit retry, device discovery, installer settings change, game-state
mutation, hot graphics-policy switch or recursive deletion of unknown files.

## Optional source-only bcbd751 throughput/CPU plan

`performance_tools` pins the copied existing official99Hz/stat helper and the
existing `capture-android-scheduler.py`. Same6217/.60/AA185/16MiB transaction,
no runtime change. The one original3point route finishes first; helper does not
repeat the old5point route. Original bounded PSO/variant readiness then separate
stat8s,99HzDWARF8s,Perfetto sched+frame8s windows all finish BEFORE optional LLDB.
Each tool window records its own target/PSO/variants and public thermal context.
No cross-window sum or ADB/tick bracket as exact frame denominator.

Important observed Q limitation: `--frame-timeline` produces display-wide
actual_frame_timeline_slice (and occasionally Activity TX), but existing Q
captures do not have game SurfaceView actual slices. They DO contain official
SurfaceFlinger per-buffer PresentFenceSignaled events for a unique exact game
SurfaceView. Report those separately as official surface-buffer completion
cadence, not app FrameTimeline or game simulation frames. If the new trace lacks
an unambiguous target SurfaceView or has loss, throughput is inconclusive; do not
substitute all-display slices, overlayFPS, shader_present.selected or guestticks.
Official CPU running within that same Perfetto window can be compared to its
own surface events; stat and sampledCPU are independent windows.

Preparation/plan is offline. Root reviews one exact pair before any execution;
original firstPID/save2/6dir CASrestore/settings/toolcleanup remain unchanged.

### Readiness fix successor

The first c5d51c8 plan was not executed: its readiness journal list was missing
from initialization. The successor only initializes `pre_record_cache_states`.
`test-readiness-fake-device.py` executes the actual helper via runpy with all
real subprocess launches denied: fresh4ticks, PSO/variant reset,30unstable reads,
repeatedtick rejection and repeatedthenfresh pass. Successful tests stop at the
thread-list command before any sampling; they are not fake performance results.

Existing scheduler failure boundary is unchanged: its configured Perfetto session
is duration-bounded8s, but `capture-android-scheduler.py` has no outer finally for
remote trace cleanup. An unsuccessful perfetto command/pull/postcondition can
leave its unique `/data/misc/perfetto-traces/d2r-scheduler-*.pftrace` file and an
incomplete sidecar; the parent still attempts original owned-game save2/stop and
6dir restore. Do not claim scheduler cleanup complete merely because game restore
succeeded. Preserve original capture logs/UUID/path, no automatic retry, and let
the device owner establish stopped tool/session state before another transaction.
This is a documented pre-existing tool boundary, not a new cleanup implementation.
# Finite import window comparison

The input may select `pool_window_bytes` as 1048576 or 16777216 (default).
Launch, official-tool endpoint checks and the offline plan use the same value.
This only selects the existing runtime option; it does not change the runtime,
resource recipes or NSO rewriting. Smaller imports can avoid pinning unrelated
pages but may increase driver objects, unions and descriptor changes. Compare
actual Buffer backing, original-game CPU and memory; do not infer resident RAM
or FPS savings from the chosen window size alone.

The f830 successor preserves the AA path and all existing lifecycle, 1MiB/16MiB,
official tool, PSO readiness and restoration logic. A f830 input must additionally
pin the exact root-reviewed original NVN positive/old-AA table refusal and
fractional-filter receipts. Native composition/table must agree on complete
DMNTBL2 extent/count/checksum/pack/producer and every DMNRES1 checksum plus all
three queried UUIDs for the fixed Q735 device. Old AA bytes cannot be relabeled.
Actual f830 bulk composition is still required before creating executable pair
inputs; passing source checks or finite fixtures does not imply real-game/bulk
visual or performance acceptance. Never reuse an AA native phase with f830.

## Optional official SDP Realtime window (default OFF)

An `official_gpu_tools` input adds only `helper`, `config`, and
`scheduler_helper` `{path, sha256}` pins. The helper must be this tree's
`sdp_realtime.py`; the scheduler remains the existing official Perfetto runner.
The original CPU/stat/record/scheduler windows finish first, then one 12s SDP
Realtime window contains an independent 8s Perfetto Surface/sched capture, all
before LLDB. Omission retains the original flow. No GPU timer or counter reader
is implemented here: the installed Qualcomm2026.9 GUI/service does collection.

Preparation must own one new GUI/Xvfb and every new official remote collector
while the game is stopped; record PID/starttime/executable SHA/UID. Configure the
nine actual Q system metrics, verify the official session's unit descriptions,
and Pause. The prepared retained GUI's actual window PID/geometry and observed
Pause control are checked; do not use this config on a replacement GUI/layout.
The verified keyboard File Save path is `Alt+F`, `S`, selected Name absolute path,
`Return` (Ctrl+S is not a valid shortcut). Each phase saves to a **new** unique
`official-session.sdp`, checks the two original zip members and exact newly
covered timestamps, and retains original `.sdp`, `sdp.db`, version and raw rows.
Before CPU windows and after each GPU window, two count/maxstamp reads of the
owned official DB must remain fixed. These are tool-state guards, not sampling.

Read/Write/TextureBW/Clocks/L1/L2/TP are mandatory with known original definitions,
finite values, no gaps over500ms, and real active rate/TP samples. Missing or
all-idle mandatory rates fail as a tool applicability gap; zero L2 misses remains
valid. Both phases use the same f830 driver/runtime/render options: ordinary
private TILED control versus authenticated native LINEAR. This is not an AA
native-vs-f830 native comparison and no new ordinary-AA baseline is requested.

All raw data includes global compositor/other GPU activity. The 12s counter
bracket is **not** the8s trace. Analysis must crop counter rows to the official
Perfetto clock-snapshot/trace-bounds interval before using that trace's actual
game SurfaceView fence-completion count. Never divide12s bytes by8s frames;
never treat CPU markers as GPU pass time or sum TBDR draw durations. No current
GMEM/per-pass conclusion is supplied. Existing vendor max-frequency fallback
warnings remain. No temperature/sysfs polling, layer injection or shader edit.

Outer `finally` closes only the exact adopted GUI/Xvfb/collector identities,
including failure, independently of original save2/stop/CAS restoration. A
replacement/unknown process is preserved and cleanup failure is explicit. The
existing scheduler helper's failure-cleanup limits remain: a helper still live
after its bound is reported, not silently called terminated. Source-only plan
never starts tools or ADB. Source-pinned current prepared-session metadata is a
single-use handoff, not permission to adopt arbitrary pre-existing tools.

### SDP successor: actual device interval and independent validity

The owned GUI config additionally pins `trace_processor: {path, sha256}`.
Fresh rows are strictly newer than the paused DB maxstamp. Their per-metric
common coverage must contain the complete official 8s capture interval, mapped
by snapshot0 REALTIME/BOOTTIME and the trace end. Backfilled trace-bound history
is retained but excluded from this denominator. The host 12s orchestration
bracket is diagnostic only. Required definitions, finite/nonidle values and
internal-gap checks remain; no offset is guessed and missing values are never
filled with zero. The preserved first capture had valid Turnip counters covering
this official 8s interval; it failed the former host-bracket endpoint guard.

An optional SDP pause/capture failure records `gpu_validity: false` and its
original evidence, disables subsequent SDP windows without restarting the GUI,
and attempts exact owned-tool cleanup. Original CPU/game/memory/save2/restore
continues; cleanup errors remain explicit. Actual game/FATAL and CPU failures
still use the original abort rules. The next pair inputs omit `counter_snapshot`
so no optional LLDB step blocks memory acceptance. No old failed result changes.

The paired launch uses `--external-profile` only to disable custom timing.
Actual performance hints and present worker are reference/inactive, matching
current defaults; their opt-in flags are independent of external profiling.
Do not attribute a frequency or throughput difference to external-profile, and
do not call approximately30FPS the ordinary game's limit.

If an SDK pause check fails and exact cleanup cannot confirm closure, that and
subsequent phases explicitly skip CPU windows (`cpu_validity: false`) while
keeping functional/memory/save2 restoration. Confirmed paused or confirmed
closed tools preserve original CPU collection. No contaminated CPU success is
reported; unknown/replacement collectors remain untouched.

### Owned collector exit/reuse guard

Remote identity reads bracket exe/SHA/UID metadata with two proc stat snapshots.
All shell reads tolerate a disappearing proc file without returning failure.
Absent/zombie at the final snapshot means no executing process; a live partial
snapshot, changed starttime, or reused PID is preserved and reported. A former
nonzero command exit is gone only if a new stat confirms absent/zombie. No
replacement can be claimed from a basename or one stale stat. Original failure
results remain unchanged; cleanup follow-up is separate evidence.

## Native-only 30-minute original-input follow-up

`run-game.py` accepts an optional `sustained_play` object. Both prior control and
native compositions are still fully hashed and authenticated; only native is
published/executed. The exact accepted 6f8188e runtime, f830 driver, 6217 table,
1 MiB windows and paired root acceptance are required. No new game binary,
resources, rendering, sampler, timing probe, SDP GUI or LLDB code is introduced.

Default v2 route repeats the original B(5225,6295) / C(5235,6289) points through
`android-game-walk-to.py`. B→C actually succeeded in v1; reverse C→B remains
pending first actual validation. This is a bounded camp-load route experiment,
not pre-proven whole-route gameplay correctness. Each call retains its before/after position receipt;
a missed target, unexpected area, changed PID or stale world stops the test and
is not silently retried. A navigation failure alone is not a crash or thermal
failure. Root may supply separately proven actual-input area transitions; the
cycle must close. `resume_step` chooses a verified route point for a NEW run;
separated runs cannot be summed into a continuous pass. Town load proves no
combat/field/all-scene stability, and the old Mali thermal result is no Q A/B.

0/10/20/30-minute checkpoints preserve original game state/PSO/target, official
meminfo, overlay screenshot and existing PID-filtered `DmThermal` public-API
logs. No ADB thermal service or sysfs queries are used. Four official Perfetto
8-second windows request scheduler/frequency/app markers and Surface timelines;
actual emitted tracks must be checked before deriving any FPS. Heavy endpoint
observations and logcat reads are outside those windows. Host monotonic deadlines
only bound/schedule the requested duration, not measure frame/GPU performance.
The scheduler wrapper reuses the existing official configuration verbatim and
adds exact UUID-output cleanup in `finally`. Perfetto retains its official
8-second duration, host command bound38s; no claim of a separately verified
collector PID is made on a transport failure.

SIGINT/SIGTERM cooperatively request save/stop AFTER the current bounded child
walk/capture returns, so the input child can release its touch. Alternatively,
create `native/stop-request.json` in the report directory containing exactly
`{"token":"<transaction_token from native/phase.json>","action":"save-stop"}`.
Do not SIGKILL the controller. Any such stop or public-API thermal background
suspend makes `continuous_pass=false`. No automatic resume/restart is attempted.
The original lifecycle helper may briefly foreground the SAME PID solely to
request a fresh save; it immediately backgrounds and confirms commits2 before
force-stop. If it cannot confirm a save, preserve the process/backups and fail.
An already exited process is never restarted to fake saving.

Original APK, six directories (including original PSO/cache), driver, owner/CAS
checks and cleanup are retained. Actual normal-input gameplay/save progress IS
saved and retained; it is not restored from a backup or edited. A complete result
requires uninterrupted route completion plus fresh same-PID save2, stop, full
original-APK/directory restoration and cleanup. The helper's local pass flag
alone is not this transaction-level acceptance.

V1 failed the fourth C→A direct walk after15s at(5221,6293), while original
game remained alive/input-ready, no public-API thermal pause. Three prior walks
were successful; source-only cycle-area closure did not prove that geometric
return path clear. The failed v1 report remains unchanged and its cold8s trace
remains a valid short window, never a30min pass. V2 removes A, retains all fail
gates, and explicitly tests the first reverse rather than claiming it passed.
