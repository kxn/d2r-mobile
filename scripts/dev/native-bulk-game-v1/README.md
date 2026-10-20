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
